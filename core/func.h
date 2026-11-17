/*
 * func.h - PDF functions (types 0 sampled, 2 exponential, 3 stitching, 4 PostScript calculator),
 * for shadings and the tint transforms of Separation and DeviceN colours.
 */
#ifndef FOLIO_FUNC_H
#define FOLIO_FUNC_H

#include "pdf.h"

#define FN_MAXIO 32
typedef struct PFunc PFunc;
/* a function object (dictionary or stream), or an array of 1-in-1-out functions (one per output);
   NULL if it can't be used */
PFunc *func_load(PDoc *d, PObj *o);
void   func_free(PFunc *f);
int    func_nout(const PFunc *f);
/* in: nin inputs; out: the outputs (up to FN_MAXIO) */
void   func_eval(const PFunc *f, const double *in, int nin, double *out);
#endif
