/*
 * restest.c - core/respath.c: where the shared Fonts, TrueType and Dict drawers are looked for
 * (the program's own, then the Bureau suite's, then FONTS: for screen fonts) and Amiga path joins.
 */
#include <stdio.h>
#include <string.h>
#include "respath.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* a pretend disk: the dirs listed in ud exist (a NULL-ended list); counts the questions asked */
static int asked;
static int exists_in(const char *dir, void *ud)
{
  const char *const *d = (const char *const *)ud;
  asked++;
  for (; *d; d++) if (!strcmp(*d, dir)) return 1;
  return 0;
}
static const char *pick(int kind, const char *const *have) { asked = 0; return res_pick(kind, exists_in, (void *)have); }
static int same(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static void test_pick(void)
{
  static const char *const all[] = { "PROGDIR:Fonts", "PROGDIR:TrueType", "PROGDIR:Dict", "Bureau:Fonts", "Bureau:TrueType", "Bureau:Dict", "FONTS:", NULL };
  static const char *const suite[] = { "Bureau:Fonts", "Bureau:TrueType", "Bureau:Dict", NULL };
  static const char *const suite_sysfonts[] = { "Bureau:TrueType", "Bureau:Dict", NULL };   /* fonts went to FONTS: */
  static const char *const none[] = { NULL };                                                  /* no Bureau: either */
  static const char *const own_tt[] = { "PROGDIR:TrueType", "Bureau:Fonts", "Bureau:Dict", NULL };
  /* 1st: the program's own drawers win */
  CHECK(same(pick(RES_FONTS, all), "PROGDIR:Fonts")); CHECK(asked == 1);
  CHECK(same(pick(RES_TRUETYPE, all), "PROGDIR:TrueType"));
  CHECK(same(pick(RES_DICT, all), "PROGDIR:Dict"));
  /* 2nd: the suite's */
  CHECK(same(pick(RES_FONTS, suite), "Bureau:Fonts")); CHECK(asked == 2);
  CHECK(same(pick(RES_TRUETYPE, suite), "Bureau:TrueType"));
  CHECK(same(pick(RES_DICT, suite), "Bureau:Dict"));
  /* 3rd: screen fonts fall to FONTS: without asking about it */
  CHECK(same(pick(RES_FONTS, suite_sysfonts), "FONTS:")); CHECK(asked == 2);
  /* no Bureau: and nothing beside the program */
  CHECK(same(pick(RES_FONTS, none), "FONTS:"));
  CHECK(pick(RES_TRUETYPE, none) == NULL); CHECK(asked == 2);
  CHECK(pick(RES_DICT, none) == NULL);
  /* each kind on its own */
  CHECK(same(pick(RES_FONTS, own_tt), "Bureau:Fonts"));
  CHECK(same(pick(RES_TRUETYPE, own_tt), "PROGDIR:TrueType"));
  CHECK(same(pick(RES_DICT, own_tt), "Bureau:Dict"));
  /* a kind that isn't one */
  CHECK(pick(-1, all) == NULL); CHECK(pick(3, all) == NULL); CHECK(asked == 0);
}

static void test_join(void)
{
  char out[32];
  CHECK(res_join(out, sizeof out, "FONTS:", "x.font") && !strcmp(out, "FONTS:x.font"));
  CHECK(res_join(out, sizeof out, "Bureau:TrueType", "a.ttf") && !strcmp(out, "Bureau:TrueType/a.ttf"));
  CHECK(res_join(out, sizeof out, "Work:Bureau/", "Dict") && !strcmp(out, "Work:Bureau/Dict"));
  CHECK(res_join(out, sizeof out, "Fonts", "QuillSans.font") && !strcmp(out, "Fonts/QuillSans.font"));
  CHECK(res_join(out, sizeof out, "", "plain") && !strcmp(out, "plain"));
  CHECK(res_join(out, sizeof out, "PROGDIR:Fonts", "QuillSans/13") && !strcmp(out, "PROGDIR:Fonts/QuillSans/13"));
  /* truncation: exactly fitting, one too long, no room at all */
  CHECK(res_join(out, 8, "AB:", "cdef") && !strcmp(out, "AB:cdef"));           /* 7 + NUL */
  strcpy(out, "junk");
  CHECK(!res_join(out, 8, "AB:", "cdefg") && out[0] == 0);
  strcpy(out, "junk");
  CHECK(!res_join(out, 8, "ABC", "defg") && out[0] == 0);                       /* the '/' counts */
  CHECK(res_join(out, 9, "ABC", "defg") && !strcmp(out, "ABC/defg"));
  strcpy(out, "junk");
  CHECK(!res_join(out, 0, "A:", "b") && !strcmp(out, "junk"));                  /* n 0: out untouched */
}

int main(void)
{
  test_pick();
  test_join();
  printf("respath: %d checks, %d failures\n", checks, fails);
  return fails != 0;
}
