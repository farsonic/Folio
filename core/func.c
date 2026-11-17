/*
 * func.c - PDF functions (see func.h).
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "func.h"

#define MAXOPS 4096
enum { FT_SAMPLED, FT_EXP, FT_STITCH, FT_PS, FT_ARRAY };
/* PostScript calculator: the operators it knows */
enum { OP_NUM, OP_ABS, OP_ADD, OP_ATAN, OP_CEILING, OP_COS, OP_CVI, OP_CVR, OP_DIV, OP_EXP, OP_FLOOR, OP_IDIV, OP_LN, OP_LOG,
       OP_MOD, OP_MUL, OP_NEG, OP_ROUND, OP_SIN, OP_SQRT, OP_SUB, OP_TRUNCATE, OP_AND, OP_BITSHIFT, OP_EQ, OP_FALSE, OP_GE,
       OP_GT, OP_LE, OP_LT, OP_NE, OP_NOT, OP_OR, OP_TRUE, OP_XOR, OP_IF, OP_IFELSE, OP_COPY, OP_DUP, OP_EXCH, OP_INDEX,
       OP_POP, OP_ROLL, OP_JUMP, OP_JUMPF };
static const char *const opnames[] = { "", "abs", "add", "atan", "ceiling", "cos", "cvi", "cvr", "div", "exp", "floor", "idiv", "ln",
  "log", "mod", "mul", "neg", "round", "sin", "sqrt", "sub", "truncate", "and", "bitshift", "eq", "false", "ge", "gt", "le", "lt",
  "ne", "not", "or", "true", "xor", "if", "ifelse", "copy", "dup", "exch", "index", "pop", "roll" };
typedef struct { int op; double v; } PsOp;

struct PFunc {
  int type, nin, nout;
  double domain[2 * FN_MAXIO], range[2 * FN_MAXIO]; int hasrange;
  /* sampled */
  int size[FN_MAXIO], bps; double encode[2 * FN_MAXIO], decode[2 * FN_MAXIO]; double *samples; long nsamples;
  /* exponential */
  double c0[FN_MAXIO], c1[FN_MAXIO], n;
  /* stitching and arrays */
  PFunc **subs; int nsubs; double *bounds; double *enc3;
  /* calculator */
  PsOp *ops; int nops;
};

static int get_nums(PDoc *d, PObj *a, double *out, int max)
{
  int i, n;
  a = pdf_resolve(d, a);
  if (!a || a->type != PO_ARRAY) return 0;
  n = a->u.arr.n < max ? a->u.arr.n : max;
  for (i = 0; i < n; i++) out[i] = pobj_num(pdf_resolve(d, a->u.arr.v[i]));
  return n;
}
/* the calculator's program compiled from its tokens (ifs and ifelses as jumps) */
static int ps_compile(Lex *l, PsOp *ops, int *n, int depth)
{
  Token t;
  if (depth > 32) return 0;
  for (;;) {
    int ty = lex_next(l, &t);
    if (ty == TK_EOF) { tok_free(&t); return depth == 0; }
    if (ty == TK_BRACE_CLOSE) { tok_free(&t); return 1; }
    if (*n >= MAXOPS - 4) { tok_free(&t); return 0; }
    if (ty == TK_INT || ty == TK_REAL) { ops[*n].op = OP_NUM; ops[*n].v = ty == TK_INT ? (double)t.i : t.r; (*n)++; }
    else if (ty == TK_BRACE_OPEN) {
      /* { a } if  |  { a } { b } ifelse */
      int j1 = *n, j2;
      ops[(*n)++].op = OP_JUMPF;
      if (!ps_compile(l, ops, n, depth + 1)) { tok_free(&t); return 0; }
      tok_free(&t);
      ty = lex_next(l, &t);
      if (ty == TK_BRACE_OPEN) {
        j2 = *n;
        ops[(*n)++].op = OP_JUMP;
        ops[j1].v = *n;
        if (!ps_compile(l, ops, n, depth + 1)) { tok_free(&t); return 0; }
        ops[j2].v = *n;
        tok_free(&t);
        ty = lex_next(l, &t);                 /* "ifelse" */
      } else ops[j1].v = *n;                  /* "if" */
      tok_free(&t);
      continue;
    } else if (ty == TK_KEYWORD) {
      int k;
      for (k = 1; k < (int)(sizeof opnames / sizeof opnames[0]); k++) if (!strcmp(t.s, opnames[k])) break;
      if (k < (int)(sizeof opnames / sizeof opnames[0])) { ops[*n].op = k; ops[*n].v = 0; (*n)++; }
    }
    tok_free(&t);
  }
}
static PFunc *load(PDoc *d, PObj *o, int depth)
{
  PFunc *f;
  PObj *dict;
  int type, i;
  if (depth > 8) return NULL;
  o = pdf_resolve(d, o);
  if (!o) return NULL;
  if (!(f = (PFunc *)calloc(1, sizeof *f))) return NULL;
  if (o->type == PO_ARRAY) {
    f->type = FT_ARRAY;
    f->nsubs = o->u.arr.n < FN_MAXIO ? o->u.arr.n : FN_MAXIO;
    if (!(f->subs = (PFunc **)calloc((size_t)f->nsubs + 1, sizeof(PFunc *)))) { free(f); return NULL; }
    for (i = 0; i < f->nsubs; i++) if (!(f->subs[i] = load(d, o->u.arr.v[i], depth + 1))) { func_free(f); return NULL; }
    f->nin = 1; f->nout = f->nsubs;
    return f;
  }
  dict = pdf_dict_of(d, o);
  if (!dict) { free(f); return NULL; }
  type = (int)pdf_dint(d, dict, "FunctionType", -1);
  f->nin = get_nums(d, pdf_dget(d, dict, "Domain"), f->domain, 2 * FN_MAXIO) / 2;
  if (f->nin < 1) { f->nin = 1; f->domain[0] = 0; f->domain[1] = 1; }
  f->nout = get_nums(d, pdf_dget(d, dict, "Range"), f->range, 2 * FN_MAXIO) / 2;
  f->hasrange = f->nout > 0;
  if (type == 0) {
    size_t len;
    u8 *data;
    long total = 1, k;
    int bits, nb;
    f->type = FT_SAMPLED;
    if (!f->nout || o->type != PO_STREAM) { free(f); return NULL; }
    if (get_nums(d, pdf_dget(d, dict, "Size"), f->encode, FN_MAXIO) < f->nin) { free(f); return NULL; }
    for (i = 0; i < f->nin; i++) { f->size[i] = (int)f->encode[i]; if (f->size[i] < 1 || f->size[i] > 65536) { free(f); return NULL; } total *= f->size[i]; if (total > 4000000) { free(f); return NULL; } }
    f->bps = (int)pdf_dint(d, dict, "BitsPerSample", 8);
    if (f->bps != 1 && f->bps != 2 && f->bps != 4 && f->bps != 8 && f->bps != 12 && f->bps != 16 && f->bps != 24 && f->bps != 32) { free(f); return NULL; }
    for (i = 0; i < f->nin; i++) { f->encode[i * 2] = 0; f->encode[i * 2 + 1] = f->size[i] - 1; }
    get_nums(d, pdf_dget(d, dict, "Encode"), f->encode, 2 * f->nin);
    for (i = 0; i < f->nout; i++) { f->decode[i * 2] = f->range[i * 2]; f->decode[i * 2 + 1] = f->range[i * 2 + 1]; }
    get_nums(d, pdf_dget(d, dict, "Decode"), f->decode, 2 * f->nout);
    f->nsamples = total * f->nout;
    if (!(data = pdf_stream_data(d, o, &len)) || !(f->samples = (double *)malloc((size_t)f->nsamples * sizeof(double)))) { free(data); free(f); return NULL; }
    /* every sample unpacked to 0..1 */
    bits = 0; nb = 0;
    {
      size_t pos = 0;
      double mx = f->bps >= 32 ? 4294967295.0 : (double)((1UL << f->bps) - 1);
      u32 acc = 0;
      for (k = 0; k < f->nsamples; k++) {
        u32 v = 0;
        int need = f->bps;
        while (need > 0) {
          if (nb == 0) { acc = pos < len ? data[pos] : 0; pos++; nb = 8; }
          {
            int take = need < nb ? need : nb;
            v = (v << take) | ((acc >> (nb - take)) & ((1u << take) - 1));
            nb -= take; need -= take;
          }
        }
        f->samples[k] = v / mx;
      }
      (void)bits;
    }
    free(data);
  } else if (type == 2) {
    double c[FN_MAXIO];
    int n0, n1;
    f->type = FT_EXP;
    f->c0[0] = 0; f->c1[0] = 1;
    n0 = get_nums(d, pdf_dget(d, dict, "C0"), c, FN_MAXIO); if (n0) memcpy(f->c0, c, (size_t)n0 * sizeof(double));
    n1 = get_nums(d, pdf_dget(d, dict, "C1"), c, FN_MAXIO); if (n1) memcpy(f->c1, c, (size_t)n1 * sizeof(double));
    f->n = pdf_dnum(d, dict, "N", 1);
    f->nout = n0 > n1 ? n0 : n1;
    if (!f->nout) f->nout = 1;
  } else if (type == 3) {
    PObj *fns = pdf_dget(d, dict, "Functions");
    f->type = FT_STITCH;
    if (!fns || fns->type != PO_ARRAY || fns->u.arr.n < 1 || fns->u.arr.n > 256) { free(f); return NULL; }
    f->nsubs = fns->u.arr.n;
    f->subs = (PFunc **)calloc((size_t)f->nsubs, sizeof(PFunc *));
    f->bounds = (double *)calloc((size_t)f->nsubs + 1, sizeof(double));
    f->enc3 = (double *)calloc((size_t)f->nsubs * 2, sizeof(double));
    if (!f->subs || !f->bounds || !f->enc3) { func_free(f); return NULL; }
    for (i = 0; i < f->nsubs; i++) {
      if (!(f->subs[i] = load(d, fns->u.arr.v[i], depth + 1))) { func_free(f); return NULL; }
      f->enc3[i * 2] = 0; f->enc3[i * 2 + 1] = 1;
    }
    get_nums(d, pdf_dget(d, dict, "Bounds"), f->bounds, f->nsubs - 1);
    get_nums(d, pdf_dget(d, dict, "Encode"), f->enc3, 2 * f->nsubs);
    if (!f->nout) f->nout = f->subs[0]->nout;
  } else if (type == 4) {
    size_t len;
    u8 *data;
    Lex l;
    Token t;
    f->type = FT_PS;
    if (o->type != PO_STREAM || !f->nout || !(data = pdf_stream_data(d, o, &len))) { free(f); return NULL; }
    if (!(f->ops = (PsOp *)malloc(MAXOPS * sizeof(PsOp)))) { free(data); free(f); return NULL; }
    lex_init(&l, data, len, d);
    if (lex_next(&l, &t) != TK_BRACE_OPEN || !ps_compile(&l, f->ops, &f->nops, 1)) { tok_free(&t); free(data); func_free(f); return NULL; }
    tok_free(&t);
    free(data);
  } else { free(f); return NULL; }
  if (f->nout > FN_MAXIO) f->nout = FN_MAXIO;
  return f;
}
PFunc *func_load(PDoc *d, PObj *o) { return load(d, o, 0); }
void func_free(PFunc *f)
{
  int i;
  if (!f) return;
  if (f->subs) for (i = 0; i < f->nsubs; i++) func_free(f->subs[i]);
  free(f->subs); free(f->bounds); free(f->enc3); free(f->samples); free(f->ops);
  free(f);
}
int func_nout(const PFunc *f) { return f ? f->nout : 0; }
static double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
/* the calculator run on its stack */
static void ps_run(const PFunc *f, double *st, int *sp)
{
  int pc = 0, steps = 0, n = *sp;
#define NEED(k) if (n < (k)) { *sp = n; return; }
  while (pc < f->nops && steps++ < 100000) {
    const PsOp *o = &f->ops[pc++];
    double a, b;
    if (n >= 98 && o->op != OP_POP) break;
    switch (o->op) {
    case OP_NUM: st[n++] = o->v; break;
    case OP_ABS: NEED(1) st[n - 1] = fabs(st[n - 1]); break;
    case OP_ADD: NEED(2) st[n - 2] += st[n - 1]; n--; break;
    case OP_SUB: NEED(2) st[n - 2] -= st[n - 1]; n--; break;
    case OP_MUL: NEED(2) st[n - 2] *= st[n - 1]; n--; break;
    case OP_DIV: NEED(2) st[n - 2] = st[n - 1] != 0 ? st[n - 2] / st[n - 1] : 0; n--; break;
    case OP_IDIV: NEED(2) { long x = (long)st[n - 2], y = (long)st[n - 1]; st[n - 2] = y ? (double)(x / y) : 0; n--; } break;
    case OP_MOD: NEED(2) { long x = (long)st[n - 2], y = (long)st[n - 1]; st[n - 2] = y ? (double)(x % y) : 0; n--; } break;
    case OP_NEG: NEED(1) st[n - 1] = -st[n - 1]; break;
    case OP_ATAN: NEED(2) a = atan2(st[n - 2], st[n - 1]) * 180 / 3.14159265358979; if (a < 0) a += 360; st[n - 2] = a; n--; break;
    case OP_CEILING: NEED(1) st[n - 1] = ceil(st[n - 1]); break;
    case OP_FLOOR: NEED(1) st[n - 1] = floor(st[n - 1]); break;
    case OP_ROUND: NEED(1) st[n - 1] = floor(st[n - 1] + 0.5); break;
    case OP_TRUNCATE: case OP_CVI: NEED(1) st[n - 1] = st[n - 1] < 0 ? ceil(st[n - 1]) : floor(st[n - 1]); break;
    case OP_CVR: break;
    case OP_COS: NEED(1) st[n - 1] = cos(st[n - 1] * 3.14159265358979 / 180); break;
    case OP_SIN: NEED(1) st[n - 1] = sin(st[n - 1] * 3.14159265358979 / 180); break;
    case OP_SQRT: NEED(1) st[n - 1] = st[n - 1] > 0 ? sqrt(st[n - 1]) : 0; break;
    case OP_EXP: NEED(2) st[n - 2] = pow(st[n - 2], st[n - 1]); if (!(st[n - 2] == st[n - 2])) st[n - 2] = 0; n--; break;
    case OP_LN: NEED(1) st[n - 1] = st[n - 1] > 0 ? log(st[n - 1]) : 0; break;
    case OP_LOG: NEED(1) st[n - 1] = st[n - 1] > 0 ? log10(st[n - 1]) : 0; break;
    case OP_AND: NEED(2) st[n - 2] = (double)((long)st[n - 2] & (long)st[n - 1]); n--; break;
    case OP_OR: NEED(2) st[n - 2] = (double)((long)st[n - 2] | (long)st[n - 1]); n--; break;
    case OP_XOR: NEED(2) st[n - 2] = (double)((long)st[n - 2] ^ (long)st[n - 1]); n--; break;
    case OP_NOT: NEED(1) st[n - 1] = st[n - 1] == 0 ? 1 : st[n - 1] == 1 ? 0 : (double)~(long)st[n - 1]; break;
    case OP_BITSHIFT: NEED(2) { long x = (long)st[n - 2]; int s = (int)st[n - 1]; st[n - 2] = (double)(s >= 0 ? x << (s & 31) : x >> ((-s) & 31)); n--; } break;
    case OP_EQ: NEED(2) st[n - 2] = st[n - 2] == st[n - 1]; n--; break;
    case OP_NE: NEED(2) st[n - 2] = st[n - 2] != st[n - 1]; n--; break;
    case OP_GE: NEED(2) st[n - 2] = st[n - 2] >= st[n - 1]; n--; break;
    case OP_GT: NEED(2) st[n - 2] = st[n - 2] > st[n - 1]; n--; break;
    case OP_LE: NEED(2) st[n - 2] = st[n - 2] <= st[n - 1]; n--; break;
    case OP_LT: NEED(2) st[n - 2] = st[n - 2] < st[n - 1]; n--; break;
    case OP_TRUE: st[n++] = 1; break;
    case OP_FALSE: st[n++] = 0; break;
    case OP_DUP: NEED(1) st[n] = st[n - 1]; n++; break;
    case OP_EXCH: NEED(2) a = st[n - 1]; st[n - 1] = st[n - 2]; st[n - 2] = a; break;
    case OP_POP: NEED(1) n--; break;
    case OP_COPY: NEED(1) { int k = (int)st[--n], j; if (k < 0 || k > n || n + k > 98) break; for (j = 0; j < k; j++) st[n + j] = st[n - k + j]; n += k; } break;
    case OP_INDEX: NEED(1) { int k = (int)st[n - 1]; if (k < 0 || k >= n - 1) { n--; break; } st[n - 1] = st[n - 2 - k]; } break;
    case OP_ROLL: NEED(2) {
      int j = (int)st[--n], k = (int)st[--n], i;
      double tmp[100];
      if (k <= 0 || k > n) break;
      j %= k; if (j < 0) j += k;
      for (i = 0; i < k; i++) tmp[(i + j) % k] = st[n - k + i];
      for (i = 0; i < k; i++) st[n - k + i] = tmp[i];
      break; }
    case OP_JUMP: pc = (int)o->v; break;
    case OP_JUMPF: NEED(1) b = st[--n]; if (b == 0) pc = (int)o->v; break;
    }
  }
#undef NEED
  *sp = n;
}
void func_eval(const PFunc *f, const double *in, int nin, double *out)
{
  int i;
  double x[FN_MAXIO];
  for (i = 0; i < f->nout; i++) out[i] = 0;
  for (i = 0; i < f->nin; i++) x[i] = clampd(i < nin ? in[i] : 0, f->domain[i * 2], f->domain[i * 2 + 1]);
  switch (f->type) {
  case FT_ARRAY:
    for (i = 0; i < f->nsubs; i++) { double o[FN_MAXIO]; func_eval(f->subs[i], x, 1, o); out[i] = o[0]; }
    return;
  case FT_EXP: {
    double t = pow(x[0], f->n);
    if (!(t == t)) t = 0;
    for (i = 0; i < f->nout; i++) out[i] = f->c0[i] + t * (f->c1[i] - f->c0[i]);
    break; }
  case FT_STITCH: {
    int k = 0;
    double lo, hi, e;
    while (k < f->nsubs - 1 && x[0] >= f->bounds[k]) k++;
    lo = k ? f->bounds[k - 1] : f->domain[0];
    hi = k < f->nsubs - 1 ? f->bounds[k] : f->domain[1];
    e = hi > lo ? f->enc3[k * 2] + (x[0] - lo) * (f->enc3[k * 2 + 1] - f->enc3[k * 2]) / (hi - lo) : f->enc3[k * 2];
    func_eval(f->subs[k], &e, 1, out);
    break; }
  case FT_SAMPLED: {
    /* multilinear interpolation between the samples around the point */
    double e[FN_MAXIO], fr[FN_MAXIO];
    int i0[FN_MAXIO], corner, j, nin2 = f->nin > 8 ? 8 : f->nin;
    for (i = 0; i < f->nin; i++) {
      double lo = f->domain[i * 2], hi = f->domain[i * 2 + 1];
      e[i] = hi > lo ? f->encode[i * 2] + (x[i] - lo) * (f->encode[i * 2 + 1] - f->encode[i * 2]) / (hi - lo) : f->encode[i * 2];
      e[i] = clampd(e[i], 0, f->size[i] - 1);
      i0[i] = (int)floor(e[i]);
      if (i0[i] >= f->size[i] - 1) i0[i] = f->size[i] > 1 ? f->size[i] - 2 : 0;
      fr[i] = f->size[i] > 1 ? e[i] - i0[i] : 0;
    }
    for (j = 0; j < f->nout; j++) {
      double v = 0;
      for (corner = 0; corner < (1 << nin2); corner++) {
        double w = 1;
        long idx = 0, mul = 1;
        for (i = 0; i < f->nin; i++) {
          int bit = i < nin2 ? (corner >> i) & 1 : 0, c = i0[i] + bit;
          if (c >= f->size[i]) c = f->size[i] - 1;
          w *= bit ? fr[i] : 1 - fr[i];
          idx += c * mul; mul *= f->size[i];
        }
        if (w > 0 && idx * f->nout + j < f->nsamples) v += w * f->samples[idx * f->nout + j];
      }
      out[j] = f->decode[j * 2] + v * (f->decode[j * 2 + 1] - f->decode[j * 2]);
    }
    break; }
  case FT_PS: {
    double st[100];
    int sp = 0;
    for (i = 0; i < f->nin; i++) st[sp++] = x[i];
    ps_run(f, st, &sp);
    for (i = 0; i < f->nout; i++) { int k = sp - f->nout + i; out[i] = k >= 0 ? st[k] : 0; }
    break; }
  }
  if (f->hasrange) for (i = 0; i < f->nout; i++) out[i] = clampd(out[i], f->range[i * 2], f->range[i * 2 + 1]);
}
