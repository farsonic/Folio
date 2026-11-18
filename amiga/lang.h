/*
 * lang.h - the languages Folio's interface speaks. Its words are written in English in the code;
 * tr() gives them in the language chosen (View > Language, kept in Folio.prefs; by default the first
 * of Locale prefs' languages Folio has), from a table built in (lang_table.h, made by tools/mklang.py
 * from lang/strings.txt). The custom parts (tabs, toolbars, panes, status bar) translate as they
 * draw, so a new language shows at once; the menus take theirs when Folio starts. Latin-1 in and out.
 */
#ifndef FOLIO_LANG_H
#define FOLIO_LANG_H

enum { LANG_EN, LANG_DE, LANG_FR, LANG_ES, LANG_IT, UI_NLANG };
extern const char *const ui_languages[UI_NLANG];   /* each language's name in itself */

const char *tr(const char *english);                /* the words in the language chosen (or as given) */
void lang_set(int lang);                            /* the interface in language lang (LANG_) */
int  lang_current(void);                            /* the language chosen */
int  lang_system(void);                             /* the language Locale prefs prefer, if Folio has it */
const char *lang_code(int lang);                    /* "en", "de" ... */
int  lang_by_code(const char *code);                /* -1: not known */

#endif
