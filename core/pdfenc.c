/*
 * pdfenc.c - encodings and glyph names (see pdfenc.h). The tables were generated from the PDF reference's
 * encodings, the Adobe Glyph List and Adobe's Symbol and ZapfDingbats mapping files.
 */
#include <stdlib.h>
#include <string.h>
#include "pdfenc.h"

typedef struct { const char *n; u16 u; } AglEnt;

const char *const enc_standard[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 18 */
  "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quoteright",  /* 20 */
  "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",  /* 28 */
  "zero", "one", "two", "three", "four", "five", "six", "seven",  /* 30 */
  "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question",  /* 38 */
  "at", "A", "B", "C", "D", "E", "F", "G",  /* 40 */
  "H", "I", "J", "K", "L", "M", "N", "O",  /* 48 */
  "P", "Q", "R", "S", "T", "U", "V", "W",  /* 50 */
  "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",  /* 58 */
  "quoteleft", "a", "b", "c", "d", "e", "f", "g",  /* 60 */
  "h", "i", "j", "k", "l", "m", "n", "o",  /* 68 */
  "p", "q", "r", "s", "t", "u", "v", "w",  /* 70 */
  "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", 0,  /* 78 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 80 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 88 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 90 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 98 */
  0, "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section",  /* A0 */
  "currency", "quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl",  /* A8 */
  0, "endash", "dagger", "daggerdbl", "periodcentered", 0, "paragraph", "bullet",  /* B0 */
  "quotesinglbase", "quotedblbase", "quotedblright", "guillemotright", "ellipsis", "perthousand", 0, "questiondown",  /* B8 */
  0, "grave", "acute", "circumflex", "tilde", "macron", "breve", "dotaccent",  /* C0 */
  "dieresis", 0, "ring", "cedilla", 0, "hungarumlaut", "ogonek", "caron",  /* C8 */
  "emdash", 0, 0, 0, 0, 0, 0, 0,  /* D0 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* D8 */
  0, "AE", 0, "ordfeminine", 0, 0, 0, 0,  /* E0 */
  "Lslash", "Oslash", "OE", "ordmasculine", 0, 0, 0, 0,  /* E8 */
  0, "ae", 0, 0, 0, "dotlessi", 0, 0,  /* F0 */
  "lslash", "oslash", "oe", "germandbls", 0, 0, 0, 0,  /* F8 */
};
const char *const enc_winansi[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 18 */
  "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",  /* 20 */
  "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",  /* 28 */
  "zero", "one", "two", "three", "four", "five", "six", "seven",  /* 30 */
  "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question",  /* 38 */
  "at", "A", "B", "C", "D", "E", "F", "G",  /* 40 */
  "H", "I", "J", "K", "L", "M", "N", "O",  /* 48 */
  "P", "Q", "R", "S", "T", "U", "V", "W",  /* 50 */
  "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",  /* 58 */
  "grave", "a", "b", "c", "d", "e", "f", "g",  /* 60 */
  "h", "i", "j", "k", "l", "m", "n", "o",  /* 68 */
  "p", "q", "r", "s", "t", "u", "v", "w",  /* 70 */
  "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", "bullet",  /* 78 */
  "Euro", "bullet", "quotesinglbase", "florin", "quotedblbase", "ellipsis", "dagger", "daggerdbl",  /* 80 */
  "circumflex", "perthousand", "Scaron", "guilsinglleft", "OE", "bullet", "Zcaron", "bullet",  /* 88 */
  "bullet", "quoteleft", "quoteright", "quotedblleft", "quotedblright", "bullet", "endash", "emdash",  /* 90 */
  "tilde", "trademark", "scaron", "guilsinglright", "oe", "bullet", "zcaron", "Ydieresis",  /* 98 */
  "space", "exclamdown", "cent", "sterling", "currency", "yen", "brokenbar", "section",  /* A0 */
  "dieresis", "copyright", "ordfeminine", "guillemotleft", "logicalnot", "hyphen", "registered", "macron",  /* A8 */
  "degree", "plusminus", "twosuperior", "threesuperior", "acute", "mu", "paragraph", "periodcentered",  /* B0 */
  "cedilla", "onesuperior", "ordmasculine", "guillemotright", "onequarter", "onehalf", "threequarters", "questiondown",  /* B8 */
  "Agrave", "Aacute", "Acircumflex", "Atilde", "Adieresis", "Aring", "AE", "Ccedilla",  /* C0 */
  "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave", "Iacute", "Icircumflex", "Idieresis",  /* C8 */
  "Eth", "Ntilde", "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis", "multiply",  /* D0 */
  "Oslash", "Ugrave", "Uacute", "Ucircumflex", "Udieresis", "Yacute", "Thorn", "germandbls",  /* D8 */
  "agrave", "aacute", "acircumflex", "atilde", "adieresis", "aring", "ae", "ccedilla",  /* E0 */
  "egrave", "eacute", "ecircumflex", "edieresis", "igrave", "iacute", "icircumflex", "idieresis",  /* E8 */
  "eth", "ntilde", "ograve", "oacute", "ocircumflex", "otilde", "odieresis", "divide",  /* F0 */
  "oslash", "ugrave", "uacute", "ucircumflex", "udieresis", "yacute", "thorn", "ydieresis",  /* F8 */
};
const char *const enc_macroman[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 18 */
  "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",  /* 20 */
  "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",  /* 28 */
  "zero", "one", "two", "three", "four", "five", "six", "seven",  /* 30 */
  "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question",  /* 38 */
  "at", "A", "B", "C", "D", "E", "F", "G",  /* 40 */
  "H", "I", "J", "K", "L", "M", "N", "O",  /* 48 */
  "P", "Q", "R", "S", "T", "U", "V", "W",  /* 50 */
  "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",  /* 58 */
  "grave", "a", "b", "c", "d", "e", "f", "g",  /* 60 */
  "h", "i", "j", "k", "l", "m", "n", "o",  /* 68 */
  "p", "q", "r", "s", "t", "u", "v", "w",  /* 70 */
  "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", 0,  /* 78 */
  "Adieresis", "Aring", "Ccedilla", "Eacute", "Ntilde", "Odieresis", "Udieresis", "aacute",  /* 80 */
  "agrave", "acircumflex", "adieresis", "atilde", "aring", "ccedilla", "eacute", "egrave",  /* 88 */
  "ecircumflex", "edieresis", "iacute", "igrave", "icircumflex", "idieresis", "ntilde", "oacute",  /* 90 */
  "ograve", "ocircumflex", "odieresis", "otilde", "uacute", "ugrave", "ucircumflex", "udieresis",  /* 98 */
  "dagger", "degree", "cent", "sterling", "section", "bullet", "paragraph", "germandbls",  /* A0 */
  "registered", "copyright", "trademark", "acute", "dieresis", 0, "AE", "Oslash",  /* A8 */
  0, "plusminus", 0, 0, "yen", "mu", 0, 0,  /* B0 */
  0, 0, 0, "ordfeminine", "ordmasculine", 0, "ae", "oslash",  /* B8 */
  "questiondown", "exclamdown", "logicalnot", 0, "florin", 0, 0, "guillemotleft",  /* C0 */
  "guillemotright", "ellipsis", "space", "Agrave", "Atilde", "Otilde", "OE", "oe",  /* C8 */
  "endash", "emdash", "quotedblleft", "quotedblright", "quoteleft", "quoteright", "divide", 0,  /* D0 */
  "ydieresis", "Ydieresis", "fraction", "currency", "guilsinglleft", "guilsinglright", "fi", "fl",  /* D8 */
  "daggerdbl", "periodcentered", "quotesinglbase", "quotedblbase", "perthousand", "Acircumflex", "Ecircumflex", "Aacute",  /* E0 */
  "Edieresis", "Egrave", "Iacute", "Icircumflex", "Idieresis", "Igrave", "Oacute", "Ocircumflex",  /* E8 */
  0, "Ograve", "Uacute", "Ucircumflex", "Ugrave", "dotlessi", "circumflex", "tilde",  /* F0 */
  "macron", "breve", "dotaccent", "ring", "cedilla", "hungarumlaut", "ogonek", "caron",  /* F8 */
};
const char *const enc_macexpert[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 18 */
  "space", "exclamsmall", "Hungarumlautsmall", "centoldstyle", "dollaroldstyle", "dollarsuperior", "ampersandsmall", "Acutesmall",  /* 20 */
  "parenleftsuperior", "parenrightsuperior", "twodotenleader", "onedotenleader", "comma", "hyphen", "period", "fraction",  /* 28 */
  "zerooldstyle", "oneoldstyle", "twooldstyle", "threeoldstyle", "fouroldstyle", "fiveoldstyle", "sixoldstyle", "sevenoldstyle",  /* 30 */
  "eightoldstyle", "nineoldstyle", "colon", "semicolon", 0, "threequartersemdash", 0, "questionsmall",  /* 38 */
  0, 0, 0, 0, "Ethsmall", 0, 0, "onequarter",  /* 40 */
  "onehalf", "threequarters", "oneeighth", "threeeighths", "fiveeighths", "seveneighths", "onethird", "twothirds",  /* 48 */
  0, 0, 0, 0, 0, 0, "ff", "fi",  /* 50 */
  "fl", "ffi", "ffl", "parenleftinferior", 0, "parenrightinferior", "Circumflexsmall", "hypheninferior",  /* 58 */
  "Gravesmall", "Asmall", "Bsmall", "Csmall", "Dsmall", "Esmall", "Fsmall", "Gsmall",  /* 60 */
  "Hsmall", "Ismall", "Jsmall", "Ksmall", "Lsmall", "Msmall", "Nsmall", "Osmall",  /* 68 */
  "Psmall", "Qsmall", "Rsmall", "Ssmall", "Tsmall", "Usmall", "Vsmall", "Wsmall",  /* 70 */
  "Xsmall", "Ysmall", "Zsmall", "colonmonetary", "onefitted", "rupiah", "Tildesmall", 0,  /* 78 */
  0, "asuperior", "centsuperior", 0, 0, 0, 0, "Aacutesmall",  /* 80 */
  "Agravesmall", "Acircumflexsmall", "Adieresissmall", "Atildesmall", "Aringsmall", "Ccedillasmall", "Eacutesmall", "Egravesmall",  /* 88 */
  "Ecircumflexsmall", "Edieresissmall", "Iacutesmall", "Igravesmall", "Icircumflexsmall", "Idieresissmall", "Ntildesmall", "Oacutesmall",  /* 90 */
  "Ogravesmall", "Ocircumflexsmall", "Odieresissmall", "Otildesmall", "Uacutesmall", "Ugravesmall", "Ucircumflexsmall", "Udieresissmall",  /* 98 */
  0, "eightsuperior", "fourinferior", "threeinferior", "sixinferior", "eightinferior", "seveninferior", "Scaronsmall",  /* A0 */
  0, "centinferior", "twoinferior", 0, "Dieresissmall", 0, "Caronsmall", "osuperior",  /* A8 */
  "fiveinferior", 0, "commainferior", "periodinferior", "Yacutesmall", 0, "dollarinferior", 0,  /* B0 */
  0, "Thornsmall", 0, "nineinferior", "zeroinferior", "Zcaronsmall", "AEsmall", "Oslashsmall",  /* B8 */
  "questiondownsmall", "oneinferior", "Lslashsmall", 0, 0, 0, 0, 0,  /* C0 */
  0, "Cedillasmall", 0, 0, 0, 0, 0, "OEsmall",  /* C8 */
  "figuredash", "hyphensuperior", 0, 0, 0, 0, "exclamdownsmall", 0,  /* D0 */
  "Ydieresissmall", 0, "onesuperior", "twosuperior", "threesuperior", "foursuperior", "fivesuperior", "sixsuperior",  /* D8 */
  "sevensuperior", "ninesuperior", "zerosuperior", 0, "esuperior", "rsuperior", "tsuperior", 0,  /* E0 */
  0, "isuperior", "ssuperior", "dsuperior", 0, 0, 0, 0,  /* E8 */
  0, "lsuperior", "Ogoneksmall", "Brevesmall", "Macronsmall", "bsuperior", "nsuperior", "msuperior",  /* F0 */
  "commasuperior", "periodsuperior", "Dotaccentsmall", "Ringsmall", 0, 0, 0, 0,  /* F8 */
};
const char *const enc_pdfdoc[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  "breve", "caron", "circumflex", "dotaccent", "hungarumlaut", "ogonek", "ring", "tilde",  /* 18 */
  "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle",  /* 20 */
  "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",  /* 28 */
  "zero", "one", "two", "three", "four", "five", "six", "seven",  /* 30 */
  "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question",  /* 38 */
  "at", "A", "B", "C", "D", "E", "F", "G",  /* 40 */
  "H", "I", "J", "K", "L", "M", "N", "O",  /* 48 */
  "P", "Q", "R", "S", "T", "U", "V", "W",  /* 50 */
  "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum", "underscore",  /* 58 */
  "grave", "a", "b", "c", "d", "e", "f", "g",  /* 60 */
  "h", "i", "j", "k", "l", "m", "n", "o",  /* 68 */
  "p", "q", "r", "s", "t", "u", "v", "w",  /* 70 */
  "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", 0,  /* 78 */
  "bullet", "dagger", "daggerdbl", "ellipsis", "emdash", "endash", "florin", "fraction",  /* 80 */
  "guilsinglleft", "guilsinglright", "minus", "perthousand", "quotedblbase", "quotedblleft", "quotedblright", "quoteleft",  /* 88 */
  "quoteright", "quotesinglbase", "trademark", "fi", "fl", "Lslash", "OE", "Scaron",  /* 90 */
  "Ydieresis", "Zcaron", "dotlessi", "lslash", "oe", "scaron", "zcaron", 0,  /* 98 */
  "Euro", "exclamdown", "cent", "sterling", "currency", "yen", "brokenbar", "section",  /* A0 */
  "dieresis", "copyright", "ordfeminine", "guillemotleft", "logicalnot", 0, "registered", "macron",  /* A8 */
  "degree", "plusminus", "twosuperior", "threesuperior", "acute", "mu", "paragraph", "periodcentered",  /* B0 */
  "cedilla", "onesuperior", "ordmasculine", "guillemotright", "onequarter", "onehalf", "threequarters", "questiondown",  /* B8 */
  "Agrave", "Aacute", "Acircumflex", "Atilde", "Adieresis", "Aring", "AE", "Ccedilla",  /* C0 */
  "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave", "Iacute", "Icircumflex", "Idieresis",  /* C8 */
  "Eth", "Ntilde", "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis", "multiply",  /* D0 */
  "Oslash", "Ugrave", "Uacute", "Ucircumflex", "Udieresis", "Yacute", "Thorn", "germandbls",  /* D8 */
  "agrave", "aacute", "acircumflex", "atilde", "adieresis", "aring", "ae", "ccedilla",  /* E0 */
  "egrave", "eacute", "ecircumflex", "edieresis", "igrave", "iacute", "icircumflex", "idieresis",  /* E8 */
  "eth", "ntilde", "ograve", "oacute", "ocircumflex", "otilde", "odieresis", "divide",  /* F0 */
  "oslash", "ugrave", "uacute", "ucircumflex", "udieresis", "yacute", "thorn", "ydieresis",  /* F8 */
};
const char *const enc_symbol[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 18 */
  "space", "exclam", "universal", "numbersign", "existential", "percent", "ampersand", "suchthat",  /* 20 */
  "parenleft", "parenright", "asteriskmath", "plus", "comma", "minus", "period", "slash",  /* 28 */
  "zero", "one", "two", "three", "four", "five", "six", "seven",  /* 30 */
  "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question",  /* 38 */
  "congruent", "Alpha", "Beta", "Chi", "Delta", "Epsilon", "Phi", "Gamma",  /* 40 */
  "Eta", "Iota", "theta1", "Kappa", "Lambda", "Mu", "Nu", "Omicron",  /* 48 */
  "Pi", "Theta", "Rho", "Sigma", "Tau", "Upsilon", "sigma1", "Omega",  /* 50 */
  "Xi", "Psi", "Zeta", "bracketleft", "therefore", "bracketright", "perpendicular", "underscore",  /* 58 */
  "radicalex", "alpha", "beta", "chi", "delta", "epsilon", "phi", "gamma",  /* 60 */
  "eta", "iota", "phi1", "kappa", "lambda", "mu", "nu", "omicron",  /* 68 */
  "pi", "theta", "rho", "sigma", "tau", "upsilon", "omega1", "omega",  /* 70 */
  "xi", "psi", "zeta", "braceleft", "bar", "braceright", "similar", 0,  /* 78 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 80 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 88 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 90 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 98 */
  "Euro", "Upsilon1", "minute", "lessequal", "fraction", "infinity", "florin", "club",  /* A0 */
  "diamond", "heart", "spade", "arrowboth", "arrowleft", "arrowup", "arrowright", "arrowdown",  /* A8 */
  "degree", "plusminus", "second", "greaterequal", "multiply", "proportional", "partialdiff", "bullet",  /* B0 */
  "divide", "notequal", "equivalence", "approxequal", "ellipsis", "arrowvertex", "arrowhorizex", "carriagereturn",  /* B8 */
  "aleph", "Ifraktur", "Rfraktur", "weierstrass", "circlemultiply", "circleplus", "emptyset", "intersection",  /* C0 */
  "union", "propersuperset", "reflexsuperset", "notsubset", "propersubset", "reflexsubset", "element", "notelement",  /* C8 */
  "angle", "gradient", "registerserif", "copyrightserif", "trademarkserif", "product", "radical", "dotmath",  /* D0 */
  "logicalnot", "logicaland", "logicalor", "arrowdblboth", "arrowdblleft", "arrowdblup", "arrowdblright", "arrowdbldown",  /* D8 */
  "lozenge", "angleleft", "registersans", "copyrightsans", "trademarksans", "summation", "parenlefttp", "parenleftex",  /* E0 */
  "parenleftbt", "bracketlefttp", "bracketleftex", "bracketleftbt", "bracelefttp", "braceleftmid", "braceleftbt", "braceex",  /* E8 */
  0, "angleright", "integral", "integraltp", "integralex", "integralbt", "parenrighttp", "parenrightex",  /* F0 */
  "parenrightbt", "bracketrighttp", "bracketrightex", "bracketrightbt", "bracerighttp", "bracerightmid", "bracerightbt", 0,  /* F8 */
};
const char *const enc_zapf[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,  /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 08 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 10 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 18 */
  "space", "a1", "a2", "a202", "a3", "a4", "a5", "a119",  /* 20 */
  "a118", "a117", "a11", "a12", "a13", "a14", "a15", "a16",  /* 28 */
  "a105", "a17", "a18", "a19", "a20", "a21", "a22", "a23",  /* 30 */
  "a24", "a25", "a26", "a27", "a28", "a6", "a7", "a8",  /* 38 */
  "a9", "a10", "a29", "a30", "a31", "a32", "a33", "a34",  /* 40 */
  "a35", "a36", "a37", "a38", "a39", "a40", "a41", "a42",  /* 48 */
  "a43", "a44", "a45", "a46", "a47", "a48", "a49", "a50",  /* 50 */
  "a51", "a52", "a53", "a54", "a55", "a56", "a57", "a58",  /* 58 */
  "a59", "a60", "a61", "a62", "a63", "a64", "a65", "a66",  /* 60 */
  "a67", "a68", "a69", "a70", "a71", "a72", "a73", "a74",  /* 68 */
  "a203", "a75", "a204", "a76", "a77", "a78", "a79", "a81",  /* 70 */
  "a82", "a83", "a84", "a97", "a98", "a99", "a100", 0,  /* 78 */
  "a89", "a90", "a93", "a94", "a91", "a92", "a205", "a85",  /* 80 */
  "a206", "a86", "a87", "a88", "a95", "a96", 0, 0,  /* 88 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 90 */
  0, 0, 0, 0, 0, 0, 0, 0,  /* 98 */
  0, "a101", "a102", "a103", "a104", "a106", "a107", "a108",  /* A0 */
  "a112", "a111", "a110", "a109", "a120", "a121", "a122", "a123",  /* A8 */
  "a124", "a125", "a126", "a127", "a128", "a129", "a130", "a131",  /* B0 */
  "a132", "a133", "a134", "a135", "a136", "a137", "a138", "a139",  /* B8 */
  "a140", "a141", "a142", "a143", "a144", "a145", "a146", "a147",  /* C0 */
  "a148", "a149", "a150", "a151", "a152", "a153", "a154", "a155",  /* C8 */
  "a156", "a157", "a158", "a159", "a160", "a161", "a163", "a164",  /* D0 */
  "a196", "a165", "a192", "a166", "a167", "a168", "a169", "a170",  /* D8 */
  "a171", "a172", "a173", "a162", "a174", "a175", "a176", "a177",  /* E0 */
  "a178", "a179", "a193", "a180", "a199", "a181", "a200", "a182",  /* E8 */
  0, "a201", "a183", "a184", "a197", "a185", "a194", "a198",  /* F0 */
  "a186", "a195", "a187", "a188", "a189", "a190", "a191", 0,  /* F8 */
};

/* the Adobe Glyph List (the part a PDF viewer meets: Latin, Greek, Cyrillic, punctuation, math, shapes,
   the Symbol and ZapfDingbats fonts' names), sorted by name for bsearch */
static const AglEnt agl_tab[1955] = {
  {"A", 0x0041}, {"AE", 0x00C6}, {"AEacute", 0x01FC}, {"AEmacron", 0x01E2}, {"AEsmall", 0xF7E6}, {"Aacute", 0x00C1},
  {"Aacutesmall", 0xF7E1}, {"Abreve", 0x0102}, {"Abreveacute", 0x1EAE}, {"Abrevecyrillic", 0x04D0}, {"Abrevedotbelow", 0x1EB6},
  {"Abrevegrave", 0x1EB0}, {"Abrevehookabove", 0x1EB2}, {"Abrevetilde", 0x1EB4}, {"Acaron", 0x01CD}, {"Acircumflex", 0x00C2},
  {"Acircumflexacute", 0x1EA4}, {"Acircumflexdotbelow", 0x1EAC}, {"Acircumflexgrave", 0x1EA6}, {"Acircumflexhookabove", 0x1EA8},
  {"Acircumflexsmall", 0xF7E2}, {"Acircumflextilde", 0x1EAA}, {"Acutesmall", 0xF7B4}, {"Acyrillic", 0x0410}, {"Adblgrave", 0x0200},
  {"Adieresis", 0x00C4}, {"Adieresiscyrillic", 0x04D2}, {"Adieresismacron", 0x01DE}, {"Adieresissmall", 0xF7E4}, {"Adotbelow", 0x1EA0},
  {"Adotmacron", 0x01E0}, {"Agrave", 0x00C0}, {"Agravesmall", 0xF7E0}, {"Ahookabove", 0x1EA2}, {"Aiecyrillic", 0x04D4},
  {"Ainvertedbreve", 0x0202}, {"Alpha", 0x0391}, {"Alphatonos", 0x0386}, {"Amacron", 0x0100}, {"Aogonek", 0x0104}, {"Aring", 0x00C5},
  {"Aringacute", 0x01FA}, {"Aringsmall", 0xF7E5}, {"Asmall", 0xF761}, {"Atilde", 0x00C3}, {"Atildesmall", 0xF7E3}, {"B", 0x0042},
  {"Becyrillic", 0x0411}, {"Beta", 0x0392}, {"Bhook", 0x0181}, {"Brevesmall", 0xF6F4}, {"Bsmall", 0xF762}, {"Btopbar", 0x0182},
  {"C", 0x0043}, {"Cacute", 0x0106}, {"Caronsmall", 0xF6F5}, {"Ccaron", 0x010C}, {"Ccedilla", 0x00C7}, {"Ccedillasmall", 0xF7E7},
  {"Ccircumflex", 0x0108}, {"Cdot", 0x010A}, {"Cdotaccent", 0x010A}, {"Cedillasmall", 0xF7B8}, {"Cheabkhasiancyrillic", 0x04BC},
  {"Checyrillic", 0x0427}, {"Chedescenderabkhasiancyrillic", 0x04BE}, {"Chedescendercyrillic", 0x04B6}, {"Chedieresiscyrillic", 0x04F4},
  {"Chekhakassiancyrillic", 0x04CB}, {"Cheverticalstrokecyrillic", 0x04B8}, {"Chi", 0x03A7}, {"Chook", 0x0187}, {"Circumflexsmall", 0xF6F6},
  {"Csmall", 0xF763}, {"D", 0x0044}, {"DZ", 0x01F1}, {"DZcaron", 0x01C4}, {"Dafrican", 0x0189}, {"Dcaron", 0x010E}, {"Dcroat", 0x0110},
  {"Decyrillic", 0x0414}, {"Deicoptic", 0x03EE}, {"Delta", 0x0394}, {"Deltagreek", 0x0394}, {"Dhook", 0x018A}, {"Dieresissmall", 0xF7A8},
  {"Digammagreek", 0x03DC}, {"Djecyrillic", 0x0402}, {"Dotaccentsmall", 0xF6F7}, {"Dslash", 0x0110}, {"Dsmall", 0xF764},
  {"Dtopbar", 0x018B}, {"Dz", 0x01F2}, {"Dzcaron", 0x01C5}, {"Dzeabkhasiancyrillic", 0x04E0}, {"Dzecyrillic", 0x0405},
  {"Dzhecyrillic", 0x040F}, {"E", 0x0045}, {"Eacute", 0x00C9}, {"Eacutesmall", 0xF7E9}, {"Ebreve", 0x0114}, {"Ecaron", 0x011A},
  {"Ecircumflex", 0x00CA}, {"Ecircumflexacute", 0x1EBE}, {"Ecircumflexdotbelow", 0x1EC6}, {"Ecircumflexgrave", 0x1EC0},
  {"Ecircumflexhookabove", 0x1EC2}, {"Ecircumflexsmall", 0xF7EA}, {"Ecircumflextilde", 0x1EC4}, {"Ecyrillic", 0x0404},
  {"Edblgrave", 0x0204}, {"Edieresis", 0x00CB}, {"Edieresissmall", 0xF7EB}, {"Edot", 0x0116}, {"Edotaccent", 0x0116}, {"Edotbelow", 0x1EB8},
  {"Efcyrillic", 0x0424}, {"Egrave", 0x00C8}, {"Egravesmall", 0xF7E8}, {"Ehookabove", 0x1EBA}, {"Eightroman", 0x2167},
  {"Einvertedbreve", 0x0206}, {"Eiotifiedcyrillic", 0x0464}, {"Elcyrillic", 0x041B}, {"Elevenroman", 0x216A}, {"Emacron", 0x0112},
  {"Emcyrillic", 0x041C}, {"Encyrillic", 0x041D}, {"Endescendercyrillic", 0x04A2}, {"Eng", 0x014A}, {"Enghecyrillic", 0x04A4},
  {"Enhookcyrillic", 0x04C7}, {"Eogonek", 0x0118}, {"Eopen", 0x0190}, {"Epsilon", 0x0395}, {"Epsilontonos", 0x0388}, {"Ercyrillic", 0x0420},
  {"Ereversed", 0x018E}, {"Ereversedcyrillic", 0x042D}, {"Escyrillic", 0x0421}, {"Esdescendercyrillic", 0x04AA}, {"Esh", 0x01A9},
  {"Esmall", 0xF765}, {"Eta", 0x0397}, {"Etatonos", 0x0389}, {"Eth", 0x00D0}, {"Ethsmall", 0xF7F0}, {"Etilde", 0x1EBC}, {"Euro", 0x20AC},
  {"Ezh", 0x01B7}, {"Ezhcaron", 0x01EE}, {"Ezhreversed", 0x01B8}, {"F", 0x0046}, {"Feicoptic", 0x03E4}, {"Fhook", 0x0191},
  {"Fitacyrillic", 0x0472}, {"Fiveroman", 0x2164}, {"Fourroman", 0x2163}, {"Fsmall", 0xF766}, {"G", 0x0047}, {"Gacute", 0x01F4},
  {"Gamma", 0x0393}, {"Gammaafrican", 0x0194}, {"Gangiacoptic", 0x03EA}, {"Gbreve", 0x011E}, {"Gcaron", 0x01E6}, {"Gcedilla", 0x0122},
  {"Gcircumflex", 0x011C}, {"Gcommaaccent", 0x0122}, {"Gdot", 0x0120}, {"Gdotaccent", 0x0120}, {"Gecyrillic", 0x0413},
  {"Ghemiddlehookcyrillic", 0x0494}, {"Ghestrokecyrillic", 0x0492}, {"Gheupturncyrillic", 0x0490}, {"Ghook", 0x0193},
  {"Gjecyrillic", 0x0403}, {"Gravesmall", 0xF760}, {"Gsmall", 0xF767}, {"Gstroke", 0x01E4}, {"H", 0x0048}, {"H18533", 0x25CF},
  {"H18543", 0x25AA}, {"H18551", 0x25AB}, {"H22073", 0x25A1}, {"Haabkhasiancyrillic", 0x04A8}, {"Hadescendercyrillic", 0x04B2},
  {"Hardsigncyrillic", 0x042A}, {"Hbar", 0x0126}, {"Hcircumflex", 0x0124}, {"Horicoptic", 0x03E8}, {"Hsmall", 0xF768},
  {"Hungarumlautsmall", 0xF6F8}, {"I", 0x0049}, {"IAcyrillic", 0x042F}, {"IJ", 0x0132}, {"IUcyrillic", 0x042E}, {"Iacute", 0x00CD},
  {"Iacutesmall", 0xF7ED}, {"Ibreve", 0x012C}, {"Icaron", 0x01CF}, {"Icircumflex", 0x00CE}, {"Icircumflexsmall", 0xF7EE},
  {"Icyrillic", 0x0406}, {"Idblgrave", 0x0208}, {"Idieresis", 0x00CF}, {"Idieresiscyrillic", 0x04E4}, {"Idieresissmall", 0xF7EF},
  {"Idot", 0x0130}, {"Idotaccent", 0x0130}, {"Idotbelow", 0x1ECA}, {"Iebrevecyrillic", 0x04D6}, {"Iecyrillic", 0x0415},
  {"Ifraktur", 0x2111}, {"Igrave", 0x00CC}, {"Igravesmall", 0xF7EC}, {"Ihookabove", 0x1EC8}, {"Iicyrillic", 0x0418},
  {"Iinvertedbreve", 0x020A}, {"Iishortcyrillic", 0x0419}, {"Imacron", 0x012A}, {"Imacroncyrillic", 0x04E2}, {"Iocyrillic", 0x0401},
  {"Iogonek", 0x012E}, {"Iota", 0x0399}, {"Iotaafrican", 0x0196}, {"Iotadieresis", 0x03AA}, {"Iotatonos", 0x038A}, {"Ismall", 0xF769},
  {"Istroke", 0x0197}, {"Itilde", 0x0128}, {"Izhitsacyrillic", 0x0474}, {"Izhitsadblgravecyrillic", 0x0476}, {"J", 0x004A},
  {"Jcircumflex", 0x0134}, {"Jecyrillic", 0x0408}, {"Jsmall", 0xF76A}, {"K", 0x004B}, {"Kabashkircyrillic", 0x04A0}, {"Kacyrillic", 0x041A},
  {"Kadescendercyrillic", 0x049A}, {"Kahookcyrillic", 0x04C3}, {"Kappa", 0x039A}, {"Kastrokecyrillic", 0x049E},
  {"Kaverticalstrokecyrillic", 0x049C}, {"Kcaron", 0x01E8}, {"Kcedilla", 0x0136}, {"Kcommaaccent", 0x0136}, {"Khacyrillic", 0x0425},
  {"Kheicoptic", 0x03E6}, {"Khook", 0x0198}, {"Kjecyrillic", 0x040C}, {"Koppacyrillic", 0x0480}, {"Koppagreek", 0x03DE},
  {"Ksicyrillic", 0x046E}, {"Ksmall", 0xF76B}, {"L", 0x004C}, {"LJ", 0x01C7}, {"Lacute", 0x0139}, {"Lambda", 0x039B}, {"Lcaron", 0x013D},
  {"Lcedilla", 0x013B}, {"Lcommaaccent", 0x013B}, {"Ldot", 0x013F}, {"Ldotaccent", 0x013F}, {"Lj", 0x01C8}, {"Ljecyrillic", 0x0409},
  {"Lslash", 0x0141}, {"Lslashsmall", 0xF6F9}, {"Lsmall", 0xF76C}, {"M", 0x004D}, {"Macronsmall", 0xF7AF}, {"Msmall", 0xF76D},
  {"Mturned", 0x019C}, {"Mu", 0x039C}, {"N", 0x004E}, {"NJ", 0x01CA}, {"Nacute", 0x0143}, {"Ncaron", 0x0147}, {"Ncedilla", 0x0145},
  {"Ncommaaccent", 0x0145}, {"Nhookleft", 0x019D}, {"Nineroman", 0x2168}, {"Nj", 0x01CB}, {"Njecyrillic", 0x040A}, {"Nsmall", 0xF76E},
  {"Ntilde", 0x00D1}, {"Ntildesmall", 0xF7F1}, {"Nu", 0x039D}, {"O", 0x004F}, {"OE", 0x0152}, {"OEsmall", 0xF6FA}, {"Oacute", 0x00D3},
  {"Oacutesmall", 0xF7F3}, {"Obarredcyrillic", 0x04E8}, {"Obarreddieresiscyrillic", 0x04EA}, {"Obreve", 0x014E}, {"Ocaron", 0x01D1},
  {"Ocenteredtilde", 0x019F}, {"Ocircumflex", 0x00D4}, {"Ocircumflexacute", 0x1ED0}, {"Ocircumflexdotbelow", 0x1ED8},
  {"Ocircumflexgrave", 0x1ED2}, {"Ocircumflexhookabove", 0x1ED4}, {"Ocircumflexsmall", 0xF7F4}, {"Ocircumflextilde", 0x1ED6},
  {"Ocyrillic", 0x041E}, {"Odblacute", 0x0150}, {"Odblgrave", 0x020C}, {"Odieresis", 0x00D6}, {"Odieresiscyrillic", 0x04E6},
  {"Odieresissmall", 0xF7F6}, {"Odotbelow", 0x1ECC}, {"Ogoneksmall", 0xF6FB}, {"Ograve", 0x00D2}, {"Ogravesmall", 0xF7F2}, {"Ohm", 0x2126},
  {"Ohookabove", 0x1ECE}, {"Ohorn", 0x01A0}, {"Ohornacute", 0x1EDA}, {"Ohorndotbelow", 0x1EE2}, {"Ohorngrave", 0x1EDC},
  {"Ohornhookabove", 0x1EDE}, {"Ohorntilde", 0x1EE0}, {"Ohungarumlaut", 0x0150}, {"Oi", 0x01A2}, {"Oinvertedbreve", 0x020E},
  {"Omacron", 0x014C}, {"Omega", 0x03A9}, {"Omegacyrillic", 0x0460}, {"Omegagreek", 0x03A9}, {"Omegaroundcyrillic", 0x047A},
  {"Omegatitlocyrillic", 0x047C}, {"Omegatonos", 0x038F}, {"Omicron", 0x039F}, {"Omicrontonos", 0x038C}, {"Oneroman", 0x2160},
  {"Oogonek", 0x01EA}, {"Oogonekmacron", 0x01EC}, {"Oopen", 0x0186}, {"Oslash", 0x00D8}, {"Oslashacute", 0x01FE}, {"Oslashsmall", 0xF7F8},
  {"Osmall", 0xF76F}, {"Ostrokeacute", 0x01FE}, {"Otcyrillic", 0x047E}, {"Otilde", 0x00D5}, {"Otildesmall", 0xF7F5}, {"P", 0x0050},
  {"Pecyrillic", 0x041F}, {"Pemiddlehookcyrillic", 0x04A6}, {"Phi", 0x03A6}, {"Phook", 0x01A4}, {"Pi", 0x03A0}, {"Psi", 0x03A8},
  {"Psicyrillic", 0x0470}, {"Psmall", 0xF770}, {"Q", 0x0051}, {"Qsmall", 0xF771}, {"R", 0x0052}, {"Racute", 0x0154}, {"Rcaron", 0x0158},
  {"Rcedilla", 0x0156}, {"Rcommaaccent", 0x0156}, {"Rdblgrave", 0x0210}, {"Rfraktur", 0x211C}, {"Rho", 0x03A1}, {"Ringsmall", 0xF6FC},
  {"Rinvertedbreve", 0x0212}, {"Rsmall", 0xF772}, {"Rsmallinvertedsuperior", 0x02B6}, {"S", 0x0053}, {"SF010000", 0x250C},
  {"SF020000", 0x2514}, {"SF030000", 0x2510}, {"SF040000", 0x2518}, {"SF050000", 0x253C}, {"SF060000", 0x252C}, {"SF070000", 0x2534},
  {"SF080000", 0x251C}, {"SF090000", 0x2524}, {"SF100000", 0x2500}, {"SF110000", 0x2502}, {"SF190000", 0x2561}, {"SF200000", 0x2562},
  {"SF210000", 0x2556}, {"SF220000", 0x2555}, {"SF230000", 0x2563}, {"SF240000", 0x2551}, {"SF250000", 0x2557}, {"SF260000", 0x255D},
  {"SF270000", 0x255C}, {"SF280000", 0x255B}, {"SF360000", 0x255E}, {"SF370000", 0x255F}, {"SF380000", 0x255A}, {"SF390000", 0x2554},
  {"SF400000", 0x2569}, {"SF410000", 0x2566}, {"SF420000", 0x2560}, {"SF430000", 0x2550}, {"SF440000", 0x256C}, {"SF450000", 0x2567},
  {"SF460000", 0x2568}, {"SF470000", 0x2564}, {"SF480000", 0x2565}, {"SF490000", 0x2559}, {"SF500000", 0x2558}, {"SF510000", 0x2552},
  {"SF520000", 0x2553}, {"SF530000", 0x256B}, {"SF540000", 0x256A}, {"Sacute", 0x015A}, {"Sampigreek", 0x03E0}, {"Scaron", 0x0160},
  {"Scaronsmall", 0xF6FD}, {"Scedilla", 0x015E}, {"Schwa", 0x018F}, {"Schwacyrillic", 0x04D8}, {"Schwadieresiscyrillic", 0x04DA},
  {"Scircumflex", 0x015C}, {"Scommaaccent", 0x0218}, {"Sevenroman", 0x2166}, {"Shacyrillic", 0x0428}, {"Shchacyrillic", 0x0429},
  {"Sheicoptic", 0x03E2}, {"Shhacyrillic", 0x04BA}, {"Shimacoptic", 0x03EC}, {"Sigma", 0x03A3}, {"Sixroman", 0x2165},
  {"Softsigncyrillic", 0x042C}, {"Ssmall", 0xF773}, {"Stigmagreek", 0x03DA}, {"T", 0x0054}, {"Tau", 0x03A4}, {"Tbar", 0x0166},
  {"Tcaron", 0x0164}, {"Tcedilla", 0x0162}, {"Tcommaaccent", 0x0162}, {"Tecyrillic", 0x0422}, {"Tedescendercyrillic", 0x04AC},
  {"Tenroman", 0x2169}, {"Tetsecyrillic", 0x04B4}, {"Theta", 0x0398}, {"Thook", 0x01AC}, {"Thorn", 0x00DE}, {"Thornsmall", 0xF7FE},
  {"Threeroman", 0x2162}, {"Tildesmall", 0xF6FE}, {"Tonefive", 0x01BC}, {"Tonesix", 0x0184}, {"Tonetwo", 0x01A7},
  {"Tretroflexhook", 0x01AE}, {"Tsecyrillic", 0x0426}, {"Tshecyrillic", 0x040B}, {"Tsmall", 0xF774}, {"Twelveroman", 0x216B},
  {"Tworoman", 0x2161}, {"U", 0x0055}, {"Uacute", 0x00DA}, {"Uacutesmall", 0xF7FA}, {"Ubreve", 0x016C}, {"Ucaron", 0x01D3},
  {"Ucircumflex", 0x00DB}, {"Ucircumflexsmall", 0xF7FB}, {"Ucyrillic", 0x0423}, {"Udblacute", 0x0170}, {"Udblgrave", 0x0214},
  {"Udieresis", 0x00DC}, {"Udieresisacute", 0x01D7}, {"Udieresiscaron", 0x01D9}, {"Udieresiscyrillic", 0x04F0}, {"Udieresisgrave", 0x01DB},
  {"Udieresismacron", 0x01D5}, {"Udieresissmall", 0xF7FC}, {"Udotbelow", 0x1EE4}, {"Ugrave", 0x00D9}, {"Ugravesmall", 0xF7F9},
  {"Uhookabove", 0x1EE6}, {"Uhorn", 0x01AF}, {"Uhornacute", 0x1EE8}, {"Uhorndotbelow", 0x1EF0}, {"Uhorngrave", 0x1EEA},
  {"Uhornhookabove", 0x1EEC}, {"Uhorntilde", 0x1EEE}, {"Uhungarumlaut", 0x0170}, {"Uhungarumlautcyrillic", 0x04F2},
  {"Uinvertedbreve", 0x0216}, {"Ukcyrillic", 0x0478}, {"Umacron", 0x016A}, {"Umacroncyrillic", 0x04EE}, {"Uogonek", 0x0172},
  {"Upsilon", 0x03A5}, {"Upsilon1", 0x03D2}, {"Upsilonacutehooksymbolgreek", 0x03D3}, {"Upsilonafrican", 0x01B1},
  {"Upsilondieresis", 0x03AB}, {"Upsilondieresishooksymbolgreek", 0x03D4}, {"Upsilonhooksymbol", 0x03D2}, {"Upsilontonos", 0x038E},
  {"Uring", 0x016E}, {"Ushortcyrillic", 0x040E}, {"Usmall", 0xF775}, {"Ustraightcyrillic", 0x04AE}, {"Ustraightstrokecyrillic", 0x04B0},
  {"Utilde", 0x0168}, {"V", 0x0056}, {"Vecyrillic", 0x0412}, {"Vhook", 0x01B2}, {"Vsmall", 0xF776}, {"W", 0x0057}, {"Wacute", 0x1E82},
  {"Wcircumflex", 0x0174}, {"Wdieresis", 0x1E84}, {"Wdotaccent", 0x1E86}, {"Wdotbelow", 0x1E88}, {"Wgrave", 0x1E80}, {"Wsmall", 0xF777},
  {"X", 0x0058}, {"Xdieresis", 0x1E8C}, {"Xdotaccent", 0x1E8A}, {"Xi", 0x039E}, {"Xsmall", 0xF778}, {"Y", 0x0059}, {"Yacute", 0x00DD},
  {"Yacutesmall", 0xF7FD}, {"Yatcyrillic", 0x0462}, {"Ycircumflex", 0x0176}, {"Ydieresis", 0x0178}, {"Ydieresissmall", 0xF7FF},
  {"Ydotaccent", 0x1E8E}, {"Yericyrillic", 0x042B}, {"Yerudieresiscyrillic", 0x04F8}, {"Ygrave", 0x1EF2}, {"Yhook", 0x01B3},
  {"Yicyrillic", 0x0407}, {"Ysmall", 0xF779}, {"Yusbigcyrillic", 0x046A}, {"Yusbigiotifiedcyrillic", 0x046C}, {"Yuslittlecyrillic", 0x0466},
  {"Yuslittleiotifiedcyrillic", 0x0468}, {"Z", 0x005A}, {"Zacute", 0x0179}, {"Zcaron", 0x017D}, {"Zcaronsmall", 0xF6FF},
  {"Zcircumflex", 0x1E90}, {"Zdot", 0x017B}, {"Zdotaccent", 0x017B}, {"Zdotbelow", 0x1E92}, {"Zecyrillic", 0x0417},
  {"Zedescendercyrillic", 0x0498}, {"Zedieresiscyrillic", 0x04DE}, {"Zeta", 0x0396}, {"Zhebrevecyrillic", 0x04C1}, {"Zhecyrillic", 0x0416},
  {"Zhedescendercyrillic", 0x0496}, {"Zhedieresiscyrillic", 0x04DC}, {"Zlinebelow", 0x1E94}, {"Zsmall", 0xF77A}, {"Zstroke", 0x01B5},
  {"a", 0x0061}, {"a1", 0x2701}, {"a10", 0x2721}, {"a100", 0x275E}, {"a101", 0x2761}, {"a102", 0x2762}, {"a103", 0x2763}, {"a104", 0x2764},
  {"a105", 0x2710}, {"a106", 0x2765}, {"a107", 0x2766}, {"a108", 0x2767}, {"a109", 0x2660}, {"a11", 0x261B}, {"a110", 0x2665},
  {"a111", 0x2666}, {"a112", 0x2663}, {"a117", 0x2709}, {"a118", 0x2708}, {"a119", 0x2707}, {"a12", 0x261E}, {"a120", 0x2460},
  {"a121", 0x2461}, {"a122", 0x2462}, {"a123", 0x2463}, {"a124", 0x2464}, {"a125", 0x2465}, {"a126", 0x2466}, {"a127", 0x2467},
  {"a128", 0x2468}, {"a129", 0x2469}, {"a13", 0x270C}, {"a130", 0x2776}, {"a131", 0x2777}, {"a132", 0x2778}, {"a133", 0x2779},
  {"a134", 0x277A}, {"a135", 0x277B}, {"a136", 0x277C}, {"a137", 0x277D}, {"a138", 0x277E}, {"a139", 0x277F}, {"a14", 0x270D},
  {"a140", 0x2780}, {"a141", 0x2781}, {"a142", 0x2782}, {"a143", 0x2783}, {"a144", 0x2784}, {"a145", 0x2785}, {"a146", 0x2786},
  {"a147", 0x2787}, {"a148", 0x2788}, {"a149", 0x2789}, {"a15", 0x270E}, {"a150", 0x278A}, {"a151", 0x278B}, {"a152", 0x278C},
  {"a153", 0x278D}, {"a154", 0x278E}, {"a155", 0x278F}, {"a156", 0x2790}, {"a157", 0x2791}, {"a158", 0x2792}, {"a159", 0x2793},
  {"a16", 0x270F}, {"a160", 0x2794}, {"a161", 0x2192}, {"a162", 0x27A3}, {"a163", 0x2194}, {"a164", 0x2195}, {"a165", 0x2799},
  {"a166", 0x279B}, {"a167", 0x279C}, {"a168", 0x279D}, {"a169", 0x279E}, {"a17", 0x2711}, {"a170", 0x279F}, {"a171", 0x27A0},
  {"a172", 0x27A1}, {"a173", 0x27A2}, {"a174", 0x27A4}, {"a175", 0x27A5}, {"a176", 0x27A6}, {"a177", 0x27A7}, {"a178", 0x27A8},
  {"a179", 0x27A9}, {"a18", 0x2712}, {"a180", 0x27AB}, {"a181", 0x27AD}, {"a182", 0x27AF}, {"a183", 0x27B2}, {"a184", 0x27B3},
  {"a185", 0x27B5}, {"a186", 0x27B8}, {"a187", 0x27BA}, {"a188", 0x27BB}, {"a189", 0x27BC}, {"a19", 0x2713}, {"a190", 0x27BD},
  {"a191", 0x27BE}, {"a192", 0x279A}, {"a193", 0x27AA}, {"a194", 0x27B6}, {"a195", 0x27B9}, {"a196", 0x2798}, {"a197", 0x27B4},
  {"a198", 0x27B7}, {"a199", 0x27AC}, {"a2", 0x2702}, {"a20", 0x2714}, {"a200", 0x27AE}, {"a201", 0x27B1}, {"a202", 0x2703},
  {"a203", 0x2750}, {"a204", 0x2752}, {"a205", 0x276E}, {"a205 (CUS)", 0xF8DD}, {"a206", 0x2770}, {"a206 (CUS)", 0xF8DF}, {"a21", 0x2715},
  {"a22", 0x2716}, {"a23", 0x2717}, {"a24", 0x2718}, {"a25", 0x2719}, {"a26", 0x271A}, {"a27", 0x271B}, {"a28", 0x271C}, {"a29", 0x2722},
  {"a3", 0x2704}, {"a30", 0x2723}, {"a31", 0x2724}, {"a32", 0x2725}, {"a33", 0x2726}, {"a34", 0x2727}, {"a35", 0x2605}, {"a36", 0x2729},
  {"a37", 0x272A}, {"a38", 0x272B}, {"a39", 0x272C}, {"a4", 0x260E}, {"a40", 0x272D}, {"a41", 0x272E}, {"a42", 0x272F}, {"a43", 0x2730},
  {"a44", 0x2731}, {"a45", 0x2732}, {"a46", 0x2733}, {"a47", 0x2734}, {"a48", 0x2735}, {"a49", 0x2736}, {"a5", 0x2706}, {"a50", 0x2737},
  {"a51", 0x2738}, {"a52", 0x2739}, {"a53", 0x273A}, {"a54", 0x273B}, {"a55", 0x273C}, {"a56", 0x273D}, {"a57", 0x273E}, {"a58", 0x273F},
  {"a59", 0x2740}, {"a6", 0x271D}, {"a60", 0x2741}, {"a61", 0x2742}, {"a62", 0x2743}, {"a63", 0x2744}, {"a64", 0x2745}, {"a65", 0x2746},
  {"a66", 0x2747}, {"a67", 0x2748}, {"a68", 0x2749}, {"a69", 0x274A}, {"a7", 0x271E}, {"a70", 0x274B}, {"a71", 0x25CF}, {"a72", 0x274D},
  {"a73", 0x25A0}, {"a74", 0x274F}, {"a75", 0x2751}, {"a76", 0x25B2}, {"a77", 0x25BC}, {"a78", 0x25C6}, {"a79", 0x2756}, {"a8", 0x271F},
  {"a81", 0x25D7}, {"a82", 0x2758}, {"a83", 0x2759}, {"a84", 0x275A}, {"a85", 0x276F}, {"a85 (CUS)", 0xF8DE}, {"a86", 0x2771},
  {"a86 (CUS)", 0xF8E0}, {"a87", 0x2772}, {"a87 (CUS)", 0xF8E1}, {"a88", 0x2773}, {"a88 (CUS)", 0xF8E2}, {"a89", 0x2768},
  {"a89 (CUS)", 0xF8D7}, {"a9", 0x2720}, {"a90", 0x2769}, {"a90 (CUS)", 0xF8D8}, {"a91", 0x276C}, {"a91 (CUS)", 0xF8DB}, {"a92", 0x276D},
  {"a92 (CUS)", 0xF8DC}, {"a93", 0x276A}, {"a93 (CUS)", 0xF8D9}, {"a94", 0x276B}, {"a94 (CUS)", 0xF8DA}, {"a95", 0x2774},
  {"a95 (CUS)", 0xF8E3}, {"a96", 0x2775}, {"a96 (CUS)", 0xF8E4}, {"a97", 0x275B}, {"a98", 0x275C}, {"a99", 0x275D}, {"aacute", 0x00E1},
  {"abreve", 0x0103}, {"abreveacute", 0x1EAF}, {"abrevecyrillic", 0x04D1}, {"abrevedotbelow", 0x1EB7}, {"abrevegrave", 0x1EB1},
  {"abrevehookabove", 0x1EB3}, {"abrevetilde", 0x1EB5}, {"acaron", 0x01CE}, {"acircumflex", 0x00E2}, {"acircumflexacute", 0x1EA5},
  {"acircumflexdotbelow", 0x1EAD}, {"acircumflexgrave", 0x1EA7}, {"acircumflexhookabove", 0x1EA9}, {"acircumflextilde", 0x1EAB},
  {"acute", 0x00B4}, {"acutelowmod", 0x02CF}, {"acyrillic", 0x0430}, {"adblgrave", 0x0201}, {"adieresis", 0x00E4},
  {"adieresiscyrillic", 0x04D3}, {"adieresismacron", 0x01DF}, {"adotbelow", 0x1EA1}, {"adotmacron", 0x01E1}, {"ae", 0x00E6},
  {"aeacute", 0x01FD}, {"aemacron", 0x01E3}, {"afii00208", 0x2015}, {"afii08941", 0x20A4}, {"afii10017", 0x0410}, {"afii10018", 0x0411},
  {"afii10019", 0x0412}, {"afii10020", 0x0413}, {"afii10021", 0x0414}, {"afii10022", 0x0415}, {"afii10023", 0x0401}, {"afii10024", 0x0416},
  {"afii10025", 0x0417}, {"afii10026", 0x0418}, {"afii10027", 0x0419}, {"afii10028", 0x041A}, {"afii10029", 0x041B}, {"afii10030", 0x041C},
  {"afii10031", 0x041D}, {"afii10032", 0x041E}, {"afii10033", 0x041F}, {"afii10034", 0x0420}, {"afii10035", 0x0421}, {"afii10036", 0x0422},
  {"afii10037", 0x0423}, {"afii10038", 0x0424}, {"afii10039", 0x0425}, {"afii10040", 0x0426}, {"afii10041", 0x0427}, {"afii10042", 0x0428},
  {"afii10043", 0x0429}, {"afii10044", 0x042A}, {"afii10045", 0x042B}, {"afii10046", 0x042C}, {"afii10047", 0x042D}, {"afii10048", 0x042E},
  {"afii10049", 0x042F}, {"afii10050", 0x0490}, {"afii10051", 0x0402}, {"afii10052", 0x0403}, {"afii10053", 0x0404}, {"afii10054", 0x0405},
  {"afii10055", 0x0406}, {"afii10056", 0x0407}, {"afii10057", 0x0408}, {"afii10058", 0x0409}, {"afii10059", 0x040A}, {"afii10060", 0x040B},
  {"afii10061", 0x040C}, {"afii10062", 0x040E}, {"afii10065", 0x0430}, {"afii10066", 0x0431}, {"afii10067", 0x0432}, {"afii10068", 0x0433},
  {"afii10069", 0x0434}, {"afii10070", 0x0435}, {"afii10071", 0x0451}, {"afii10072", 0x0436}, {"afii10073", 0x0437}, {"afii10074", 0x0438},
  {"afii10075", 0x0439}, {"afii10076", 0x043A}, {"afii10077", 0x043B}, {"afii10078", 0x043C}, {"afii10079", 0x043D}, {"afii10080", 0x043E},
  {"afii10081", 0x043F}, {"afii10082", 0x0440}, {"afii10083", 0x0441}, {"afii10084", 0x0442}, {"afii10085", 0x0443}, {"afii10086", 0x0444},
  {"afii10087", 0x0445}, {"afii10088", 0x0446}, {"afii10089", 0x0447}, {"afii10090", 0x0448}, {"afii10091", 0x0449}, {"afii10092", 0x044A},
  {"afii10093", 0x044B}, {"afii10094", 0x044C}, {"afii10095", 0x044D}, {"afii10096", 0x044E}, {"afii10097", 0x044F}, {"afii10098", 0x0491},
  {"afii10099", 0x0452}, {"afii10100", 0x0453}, {"afii10101", 0x0454}, {"afii10102", 0x0455}, {"afii10103", 0x0456}, {"afii10104", 0x0457},
  {"afii10105", 0x0458}, {"afii10106", 0x0459}, {"afii10107", 0x045A}, {"afii10108", 0x045B}, {"afii10109", 0x045C}, {"afii10110", 0x045E},
  {"afii10145", 0x040F}, {"afii10146", 0x0462}, {"afii10147", 0x0472}, {"afii10148", 0x0474}, {"afii10193", 0x045F}, {"afii10194", 0x0463},
  {"afii10195", 0x0473}, {"afii10196", 0x0475}, {"afii10846", 0x04D9}, {"afii299", 0x200E}, {"afii300", 0x200F}, {"afii301", 0x200D},
  {"afii57636", 0x20AA}, {"afii57929", 0x02BC}, {"afii61248", 0x2105}, {"afii61289", 0x2113}, {"afii61352", 0x2116}, {"afii61573", 0x202C},
  {"afii61574", 0x202D}, {"afii61575", 0x202E}, {"afii61664", 0x200C}, {"afii64937", 0x02BD}, {"agrave", 0x00E0}, {"ahookabove", 0x1EA3},
  {"aiecyrillic", 0x04D5}, {"ainvertedbreve", 0x0203}, {"aleph", 0x2135}, {"allequal", 0x224C}, {"alpha", 0x03B1}, {"alphatonos", 0x03AC},
  {"amacron", 0x0101}, {"ampersand", 0x0026}, {"ampersandsmall", 0xF726}, {"angle", 0x2220}, {"angleleft", 0x2329}, {"angleright", 0x232A},
  {"angstrom", 0x212B}, {"anoteleia", 0x0387}, {"aogonek", 0x0105}, {"apostrophemod", 0x02BC}, {"approaches", 0x2250},
  {"approxequal", 0x2248}, {"approxequalorimage", 0x2252}, {"approximatelyequal", 0x2245}, {"arc", 0x2312}, {"arighthalfring", 0x1E9A},
  {"aring", 0x00E5}, {"aringacute", 0x01FB}, {"arrowboth", 0x2194}, {"arrowdashdown", 0x21E3}, {"arrowdashleft", 0x21E0},
  {"arrowdashright", 0x21E2}, {"arrowdashup", 0x21E1}, {"arrowdblboth", 0x21D4}, {"arrowdbldown", 0x21D3}, {"arrowdblleft", 0x21D0},
  {"arrowdblright", 0x21D2}, {"arrowdblup", 0x21D1}, {"arrowdown", 0x2193}, {"arrowdownleft", 0x2199}, {"arrowdownright", 0x2198},
  {"arrowdownwhite", 0x21E9}, {"arrowheaddownmod", 0x02C5}, {"arrowheadleftmod", 0x02C2}, {"arrowheadrightmod", 0x02C3},
  {"arrowheadupmod", 0x02C4}, {"arrowhorizex", 0xF8E7}, {"arrowhorizex (CUS)", 0xF8E7}, {"arrowleft", 0x2190}, {"arrowleftdbl", 0x21D0},
  {"arrowleftdblstroke", 0x21CD}, {"arrowleftoverright", 0x21C6}, {"arrowleftwhite", 0x21E6}, {"arrowright", 0x2192},
  {"arrowrightdblstroke", 0x21CF}, {"arrowrightoverleft", 0x21C4}, {"arrowrightwhite", 0x21E8}, {"arrowtableft", 0x21E4},
  {"arrowtabright", 0x21E5}, {"arrowup", 0x2191}, {"arrowupdn", 0x2195}, {"arrowupdnbse", 0x21A8}, {"arrowupdownbase", 0x21A8},
  {"arrowupleft", 0x2196}, {"arrowupleftofdown", 0x21C5}, {"arrowupright", 0x2197}, {"arrowupwhite", 0x21E7}, {"arrowvertex", 0xF8E6},
  {"arrowvertex (CUS)", 0xF8E6}, {"asciicircum", 0x005E}, {"asciitilde", 0x007E}, {"asterisk", 0x002A}, {"asteriskmath", 0x2217},
  {"asterism", 0x2042}, {"asuperior", 0xF6E9}, {"asymptoticallyequal", 0x2243}, {"at", 0x0040}, {"atilde", 0x00E3}, {"b", 0x0062},
  {"backslash", 0x005C}, {"bar", 0x007C}, {"beamedsixteenthnotes", 0x266C}, {"because", 0x2235}, {"becyrillic", 0x0431}, {"beta", 0x03B2},
  {"betasymbolgreek", 0x03D0}, {"blackcircle", 0x25CF}, {"blackdiamond", 0x25C6}, {"blackdownpointingtriangle", 0x25BC},
  {"blackleftpointingpointer", 0x25C4}, {"blackleftpointingtriangle", 0x25C0}, {"blacklowerlefttriangle", 0x25E3},
  {"blacklowerrighttriangle", 0x25E2}, {"blackrectangle", 0x25AC}, {"blackrightpointingpointer", 0x25BA},
  {"blackrightpointingtriangle", 0x25B6}, {"blacksmallsquare", 0x25AA}, {"blacksmilingface", 0x263B}, {"blacksquare", 0x25A0},
  {"blackstar", 0x2605}, {"blackupperlefttriangle", 0x25E4}, {"blackupperrighttriangle", 0x25E5}, {"blackuppointingsmalltriangle", 0x25B4},
  {"blackuppointingtriangle", 0x25B2}, {"block", 0x2588}, {"braceex", 0xF8F4}, {"braceex (CUS)", 0xF8F4}, {"braceleft", 0x007B},
  {"braceleftbt", 0xF8F3}, {"braceleftbt (CUS)", 0xF8F3}, {"braceleftmid", 0xF8F2}, {"braceleftmid (CUS)", 0xF8F2}, {"bracelefttp", 0xF8F1},
  {"bracelefttp (CUS)", 0xF8F1}, {"braceright", 0x007D}, {"bracerightbt", 0xF8FE}, {"bracerightbt (CUS)", 0xF8FE},
  {"bracerightmid", 0xF8FD}, {"bracerightmid (CUS)", 0xF8FD}, {"bracerighttp", 0xF8FC}, {"bracerighttp (CUS)", 0xF8FC},
  {"bracketleft", 0x005B}, {"bracketleftbt", 0xF8F0}, {"bracketleftbt (CUS)", 0xF8F0}, {"bracketleftex", 0xF8EF},
  {"bracketleftex (CUS)", 0xF8EF}, {"bracketlefttp", 0xF8EE}, {"bracketlefttp (CUS)", 0xF8EE}, {"bracketright", 0x005D},
  {"bracketrightbt", 0xF8FB}, {"bracketrightbt (CUS)", 0xF8FB}, {"bracketrightex", 0xF8FA}, {"bracketrightex (CUS)", 0xF8FA},
  {"bracketrighttp", 0xF8F9}, {"bracketrighttp (CUS)", 0xF8F9}, {"breve", 0x02D8}, {"brokenbar", 0x00A6}, {"bstroke", 0x0180},
  {"bsuperior", 0xF6EA}, {"btopbar", 0x0183}, {"bullet", 0x2022}, {"bulletinverse", 0x25D8}, {"bulletoperator", 0x2219},
  {"bullseye", 0x25CE}, {"c", 0x0063}, {"cacute", 0x0107}, {"capslock", 0x21EA}, {"careof", 0x2105}, {"caron", 0x02C7},
  {"carriagereturn", 0x21B5}, {"ccaron", 0x010D}, {"ccedilla", 0x00E7}, {"ccircumflex", 0x0109}, {"cdot", 0x010B}, {"cdotaccent", 0x010B},
  {"cedilla", 0x00B8}, {"cent", 0x00A2}, {"centigrade", 0x2103}, {"centinferior", 0xF6DF}, {"centoldstyle", 0xF7A2},
  {"centsuperior", 0xF6E0}, {"cheabkhasiancyrillic", 0x04BD}, {"checyrillic", 0x0447}, {"chedescenderabkhasiancyrillic", 0x04BF},
  {"chedescendercyrillic", 0x04B7}, {"chedieresiscyrillic", 0x04F5}, {"chekhakassiancyrillic", 0x04CC},
  {"cheverticalstrokecyrillic", 0x04B9}, {"chi", 0x03C7}, {"chook", 0x0188}, {"circle", 0x25CB}, {"circlemultiply", 0x2297},
  {"circleot", 0x2299}, {"circleplus", 0x2295}, {"circlewithlefthalfblack", 0x25D0}, {"circlewithrighthalfblack", 0x25D1},
  {"circumflex", 0x02C6}, {"clear", 0x2327}, {"clickalveolar", 0x01C2}, {"clickdental", 0x01C0}, {"clicklateral", 0x01C1},
  {"clickretroflex", 0x01C3}, {"club", 0x2663}, {"clubsuitblack", 0x2663}, {"clubsuitwhite", 0x2667}, {"colon", 0x003A},
  {"colonmonetary", 0x20A1}, {"colonsign", 0x20A1}, {"colontriangularhalfmod", 0x02D1}, {"colontriangularmod", 0x02D0}, {"comma", 0x002C},
  {"commainferior", 0xF6E1}, {"commareversedmod", 0x02BD}, {"commasuperior", 0xF6E2}, {"commaturnedmod", 0x02BB}, {"compass", 0x263C},
  {"congruent", 0x2245}, {"contourintegral", 0x222E}, {"control", 0x2303}, {"controlDEL", 0x007F}, {"copyright", 0x00A9},
  {"copyrightsans", 0xF8E9}, {"copyrightsans (CUS)", 0xF8E9}, {"copyrightserif", 0xF6D9}, {"copyrightserif (CUS)", 0xF6D9},
  {"cruzeiro", 0x20A2}, {"curlyand", 0x22CF}, {"curlyor", 0x22CE}, {"currency", 0x00A4}, {"d", 0x0064}, {"dagger", 0x2020},
  {"daggerdbl", 0x2021}, {"dasiapneumatacyrilliccmb", 0x0485}, {"dblarrowleft", 0x21D4}, {"dblarrowright", 0x21D2}, {"dblintegral", 0x222C},
  {"dbllowline", 0x2017}, {"dblprimemod", 0x02BA}, {"dblverticalbar", 0x2016}, {"dcaron", 0x010F}, {"dcroat", 0x0111},
  {"decyrillic", 0x0434}, {"degree", 0x00B0}, {"deicoptic", 0x03EF}, {"deleteleft", 0x232B}, {"deleteright", 0x2326}, {"delta", 0x03B4},
  {"deltaturned", 0x018D}, {"dialytikatonos", 0x0385}, {"diamond", 0x2666}, {"diamondsuitwhite", 0x2662}, {"dieresis", 0x00A8},
  {"dieresistonos", 0x0385}, {"divide", 0x00F7}, {"divides", 0x2223}, {"divisionslash", 0x2215}, {"djecyrillic", 0x0452},
  {"dkshade", 0x2593}, {"dmacron", 0x0111}, {"dnblock", 0x2584}, {"dollar", 0x0024}, {"dollarinferior", 0xF6E3}, {"dollaroldstyle", 0xF724},
  {"dollarsuperior", 0xF6E4}, {"dong", 0x20AB}, {"dotaccent", 0x02D9}, {"dotlessi", 0x0131}, {"dotmath", 0x22C5}, {"dottedcircle", 0x25CC},
  {"downtackmod", 0x02D5}, {"dsuperior", 0xF6EB}, {"dtopbar", 0x018C}, {"dz", 0x01F3}, {"dzcaron", 0x01C6},
  {"dzeabkhasiancyrillic", 0x04E1}, {"dzecyrillic", 0x0455}, {"dzhecyrillic", 0x045F}, {"e", 0x0065}, {"eacute", 0x00E9}, {"earth", 0x2641},
  {"ebreve", 0x0115}, {"ecaron", 0x011B}, {"ecircumflex", 0x00EA}, {"ecircumflexacute", 0x1EBF}, {"ecircumflexdotbelow", 0x1EC7},
  {"ecircumflexgrave", 0x1EC1}, {"ecircumflexhookabove", 0x1EC3}, {"ecircumflextilde", 0x1EC5}, {"ecyrillic", 0x0454},
  {"edblgrave", 0x0205}, {"edieresis", 0x00EB}, {"edot", 0x0117}, {"edotaccent", 0x0117}, {"edotbelow", 0x1EB9}, {"efcyrillic", 0x0444},
  {"egrave", 0x00E8}, {"ehookabove", 0x1EBB}, {"eight", 0x0038}, {"eighthnotebeamed", 0x266B}, {"eightinferior", 0x2088},
  {"eightoldstyle", 0xF738}, {"eightroman", 0x2177}, {"eightsuperior", 0x2078}, {"einvertedbreve", 0x0207}, {"eiotifiedcyrillic", 0x0465},
  {"elcyrillic", 0x043B}, {"element", 0x2208}, {"elevenroman", 0x217A}, {"ellipsis", 0x2026}, {"ellipsisvertical", 0x22EE},
  {"emacron", 0x0113}, {"emcyrillic", 0x043C}, {"emdash", 0x2014}, {"emptyset", 0x2205}, {"encyrillic", 0x043D}, {"endash", 0x2013},
  {"endescendercyrillic", 0x04A3}, {"eng", 0x014B}, {"enghecyrillic", 0x04A5}, {"enhookcyrillic", 0x04C8}, {"enspace", 0x2002},
  {"eogonek", 0x0119}, {"epsilon", 0x03B5}, {"epsilontonos", 0x03AD}, {"equal", 0x003D}, {"equalsuperior", 0x207C}, {"equivalence", 0x2261},
  {"ercyrillic", 0x0440}, {"ereversedcyrillic", 0x044D}, {"escyrillic", 0x0441}, {"esdescendercyrillic", 0x04AB},
  {"eshreversedloop", 0x01AA}, {"estimated", 0x212E}, {"esuperior", 0xF6EC}, {"eta", 0x03B7}, {"etatonos", 0x03AE}, {"eth", 0x00F0},
  {"etilde", 0x1EBD}, {"eturned", 0x01DD}, {"euro", 0x20AC}, {"exclam", 0x0021}, {"exclamdbl", 0x203C}, {"exclamdown", 0x00A1},
  {"exclamdownsmall", 0xF7A1}, {"exclamsmall", 0xF721}, {"existential", 0x2203}, {"ezhcaron", 0x01EF}, {"ezhreversed", 0x01B9},
  {"ezhtail", 0x01BA}, {"f", 0x0066}, {"fahrenheit", 0x2109}, {"feicoptic", 0x03E5}, {"female", 0x2640}, {"ff", 0xFB00}, {"ffi", 0xFB03},
  {"ffl", 0xFB04}, {"fi", 0xFB01}, {"figuredash", 0x2012}, {"filledbox", 0x25A0}, {"filledrect", 0x25AC}, {"firsttonechinese", 0x02C9},
  {"fisheye", 0x25C9}, {"fitacyrillic", 0x0473}, {"five", 0x0035}, {"fiveeighths", 0x215D}, {"fiveinferior", 0x2085},
  {"fiveoldstyle", 0xF735}, {"fiveroman", 0x2174}, {"fivesuperior", 0x2075}, {"fl", 0xFB02}, {"florin", 0x0192}, {"forall", 0x2200},
  {"four", 0x0034}, {"fourinferior", 0x2084}, {"fouroldstyle", 0xF734}, {"fourroman", 0x2173}, {"foursuperior", 0x2074},
  {"fourthtonechinese", 0x02CB}, {"fraction", 0x2044}, {"franc", 0x20A3}, {"g", 0x0067}, {"gacute", 0x01F5}, {"gamma", 0x03B3},
  {"gammasuperior", 0x02E0}, {"gangiacoptic", 0x03EB}, {"gbreve", 0x011F}, {"gcaron", 0x01E7}, {"gcedilla", 0x0123},
  {"gcircumflex", 0x011D}, {"gcommaaccent", 0x0123}, {"gdot", 0x0121}, {"gdotaccent", 0x0121}, {"gecyrillic", 0x0433},
  {"geometricallyequal", 0x2251}, {"germandbls", 0x00DF}, {"ghemiddlehookcyrillic", 0x0495}, {"ghestrokecyrillic", 0x0493},
  {"gheupturncyrillic", 0x0491}, {"gjecyrillic", 0x0453}, {"glottalinvertedstroke", 0x01BE}, {"glottalstopmod", 0x02C0},
  {"glottalstopreversedmod", 0x02C1}, {"glottalstopreversedsuperior", 0x02E4}, {"gradient", 0x2207}, {"grave", 0x0060},
  {"gravelowmod", 0x02CE}, {"greater", 0x003E}, {"greaterequal", 0x2265}, {"greaterequalorless", 0x22DB}, {"greaterorequivalent", 0x2273},
  {"greaterorless", 0x2277}, {"greateroverequal", 0x2267}, {"gstroke", 0x01E5}, {"guillemotleft", 0x00AB}, {"guillemotright", 0x00BB},
  {"guilsinglleft", 0x2039}, {"guilsinglright", 0x203A}, {"h", 0x0068}, {"haabkhasiancyrillic", 0x04A9}, {"hadescendercyrillic", 0x04B3},
  {"hardsigncyrillic", 0x044A}, {"harpoonleftbarbup", 0x21BC}, {"harpoonrightbarbup", 0x21C0}, {"hbar", 0x0127}, {"hcircumflex", 0x0125},
  {"heart", 0x2665}, {"heartsuitblack", 0x2665}, {"heartsuitwhite", 0x2661}, {"hhooksuperior", 0x02B1}, {"hlinebelow", 0x1E96},
  {"horicoptic", 0x03E9}, {"horizontalbar", 0x2015}, {"hotsprings", 0x2668}, {"house", 0x2302}, {"hsuperior", 0x02B0},
  {"hungarumlaut", 0x02DD}, {"hv", 0x0195}, {"hyphen", 0x002D}, {"hypheninferior", 0xF6E5}, {"hyphensuperior", 0xF6E6},
  {"hyphentwo", 0x2010}, {"i", 0x0069}, {"iacute", 0x00ED}, {"iacyrillic", 0x044F}, {"ibreve", 0x012D}, {"icaron", 0x01D0},
  {"icircumflex", 0x00EE}, {"icyrillic", 0x0456}, {"idblgrave", 0x0209}, {"idieresis", 0x00EF}, {"idieresiscyrillic", 0x04E5},
  {"idotbelow", 0x1ECB}, {"iebrevecyrillic", 0x04D7}, {"iecyrillic", 0x0435}, {"igrave", 0x00EC}, {"ihookabove", 0x1EC9},
  {"iicyrillic", 0x0438}, {"iinvertedbreve", 0x020B}, {"iishortcyrillic", 0x0439}, {"ij", 0x0133}, {"ilde", 0x02DC}, {"imacron", 0x012B},
  {"imacroncyrillic", 0x04E3}, {"imageorapproximatelyequal", 0x2253}, {"increment", 0x2206}, {"infinity", 0x221E}, {"integral", 0x222B},
  {"integralbottom", 0x2321}, {"integralbt", 0x2321}, {"integralex", 0xF8F5}, {"integralex (CUS)", 0xF8F5}, {"integraltop", 0x2320},
  {"integraltp", 0x2320}, {"intersection", 0x2229}, {"invbullet", 0x25D8}, {"invcircle", 0x25D9}, {"invsmileface", 0x263B},
  {"iocyrillic", 0x0451}, {"iogonek", 0x012F}, {"iota", 0x03B9}, {"iotadieresis", 0x03CA}, {"iotadieresistonos", 0x0390},
  {"iotatonos", 0x03AF}, {"isuperior", 0xF6ED}, {"itilde", 0x0129}, {"iucyrillic", 0x044E}, {"izhitsacyrillic", 0x0475},
  {"izhitsadblgravecyrillic", 0x0477}, {"j", 0x006A}, {"jcaron", 0x01F0}, {"jcircumflex", 0x0135}, {"jecyrillic", 0x0458},
  {"jsuperior", 0x02B2}, {"k", 0x006B}, {"kabashkircyrillic", 0x04A1}, {"kacyrillic", 0x043A}, {"kadescendercyrillic", 0x049B},
  {"kahookcyrillic", 0x04C4}, {"kappa", 0x03BA}, {"kappasymbolgreek", 0x03F0}, {"kastrokecyrillic", 0x049F},
  {"kaverticalstrokecyrillic", 0x049D}, {"kcaron", 0x01E9}, {"kcedilla", 0x0137}, {"kcommaaccent", 0x0137}, {"kgreenlandic", 0x0138},
  {"khacyrillic", 0x0445}, {"kheicoptic", 0x03E7}, {"khook", 0x0199}, {"kjecyrillic", 0x045C}, {"koppacyrillic", 0x0481},
  {"ksicyrillic", 0x046F}, {"l", 0x006C}, {"lacute", 0x013A}, {"lambda", 0x03BB}, {"lambdastroke", 0x019B}, {"largecircle", 0x25EF},
  {"lbar", 0x019A}, {"lcaron", 0x013E}, {"lcedilla", 0x013C}, {"lcommaaccent", 0x013C}, {"ldot", 0x0140}, {"ldotaccent", 0x0140},
  {"less", 0x003C}, {"lessequal", 0x2264}, {"lessequalorgreater", 0x22DA}, {"lessorequivalent", 0x2272}, {"lessorgreater", 0x2276},
  {"lessoverequal", 0x2266}, {"lfblock", 0x258C}, {"lira", 0x20A4}, {"lj", 0x01C9}, {"ljecyrillic", 0x0459}, {"logicaland", 0x2227},
  {"logicalnot", 0x00AC}, {"logicalnotreversed", 0x2310}, {"logicalor", 0x2228}, {"longs", 0x017F}, {"lozenge", 0x25CA}, {"lslash", 0x0142},
  {"lsquare", 0x2113}, {"lsuperior", 0xF6EE}, {"ltshade", 0x2591}, {"m", 0x006D}, {"macron", 0x00AF}, {"macronlowmod", 0x02CD},
  {"male", 0x2642}, {"mars", 0x2642}, {"middot", 0x00B7}, {"minus", 0x2212}, {"minuscircle", 0x2296}, {"minusmod", 0x02D7},
  {"minusplus", 0x2213}, {"minute", 0x2032}, {"msuperior", 0xF6EF}, {"mu", 0x00B5}, {"mu1", 0x00B5}, {"muchgreater", 0x226B},
  {"muchless", 0x226A}, {"mugreek", 0x03BC}, {"multiply", 0x00D7}, {"musicalnote", 0x266A}, {"musicalnotedbl", 0x266B},
  {"musicflatsign", 0x266D}, {"musicsharpsign", 0x266F}, {"n", 0x006E}, {"nabla", 0x2207}, {"nacute", 0x0144}, {"napostrophe", 0x0149},
  {"nbspace", 0x00A0}, {"ncaron", 0x0148}, {"ncedilla", 0x0146}, {"ncommaaccent", 0x0146}, {"newsheqelsign", 0x20AA}, {"nine", 0x0039},
  {"nineinferior", 0x2089}, {"nineoldstyle", 0xF739}, {"nineroman", 0x2178}, {"ninesuperior", 0x2079}, {"nj", 0x01CC},
  {"njecyrillic", 0x045A}, {"nlegrightlong", 0x019E}, {"nonbreakingspace", 0x00A0}, {"notcontains", 0x220C}, {"notelement", 0x2209},
  {"notelementof", 0x2209}, {"notequal", 0x2260}, {"notgreater", 0x226F}, {"notgreaternorequal", 0x2271}, {"notgreaternorless", 0x2279},
  {"notidentical", 0x2262}, {"notless", 0x226E}, {"notlessnorequal", 0x2270}, {"notparallel", 0x2226}, {"notprecedes", 0x2280},
  {"notsubset", 0x2284}, {"notsucceeds", 0x2281}, {"notsuperset", 0x2285}, {"nsuperior", 0x207F}, {"ntilde", 0x00F1}, {"nu", 0x03BD},
  {"numbersign", 0x0023}, {"numeralsigngreek", 0x0374}, {"numeralsignlowergreek", 0x0375}, {"numero", 0x2116}, {"o", 0x006F},
  {"oacute", 0x00F3}, {"obarredcyrillic", 0x04E9}, {"obarreddieresiscyrillic", 0x04EB}, {"obreve", 0x014F}, {"ocaron", 0x01D2},
  {"ocircumflex", 0x00F4}, {"ocircumflexacute", 0x1ED1}, {"ocircumflexdotbelow", 0x1ED9}, {"ocircumflexgrave", 0x1ED3},
  {"ocircumflexhookabove", 0x1ED5}, {"ocircumflextilde", 0x1ED7}, {"ocyrillic", 0x043E}, {"odblacute", 0x0151}, {"odblgrave", 0x020D},
  {"odieresis", 0x00F6}, {"odieresiscyrillic", 0x04E7}, {"odotbelow", 0x1ECD}, {"oe", 0x0153}, {"ogonek", 0x02DB}, {"ograve", 0x00F2},
  {"ohookabove", 0x1ECF}, {"ohorn", 0x01A1}, {"ohornacute", 0x1EDB}, {"ohorndotbelow", 0x1EE3}, {"ohorngrave", 0x1EDD},
  {"ohornhookabove", 0x1EDF}, {"ohorntilde", 0x1EE1}, {"ohungarumlaut", 0x0151}, {"oi", 0x01A3}, {"oinvertedbreve", 0x020F},
  {"omacron", 0x014D}, {"omega", 0x03C9}, {"omega1", 0x03D6}, {"omegacyrillic", 0x0461}, {"omegaroundcyrillic", 0x047B},
  {"omegatitlocyrillic", 0x047D}, {"omegatonos", 0x03CE}, {"omicron", 0x03BF}, {"omicrontonos", 0x03CC}, {"one", 0x0031},
  {"onedotenleader", 0x2024}, {"oneeighth", 0x215B}, {"onefitted", 0xF6DC}, {"onehalf", 0x00BD}, {"oneinferior", 0x2081},
  {"oneoldstyle", 0xF731}, {"onequarter", 0x00BC}, {"oneroman", 0x2170}, {"onesuperior", 0x00B9}, {"onethird", 0x2153}, {"oogonek", 0x01EB},
  {"oogonekmacron", 0x01ED}, {"openbullet", 0x25E6}, {"option", 0x2325}, {"ordfeminine", 0x00AA}, {"ordmasculine", 0x00BA},
  {"orthogonal", 0x221F}, {"oslash", 0x00F8}, {"oslashacute", 0x01FF}, {"ostrokeacute", 0x01FF}, {"osuperior", 0xF6F0},
  {"otcyrillic", 0x047F}, {"otilde", 0x00F5}, {"overline", 0x203E}, {"overscore", 0x00AF}, {"p", 0x0070}, {"pagedown", 0x21DF},
  {"pageup", 0x21DE}, {"palatalizationcyrilliccmb", 0x0484}, {"palochkacyrillic", 0x04C0}, {"paragraph", 0x00B6}, {"parallel", 0x2225},
  {"parenleft", 0x0028}, {"parenleftbt", 0xF8ED}, {"parenleftbt (CUS)", 0xF8ED}, {"parenleftex", 0xF8EC}, {"parenleftex (CUS)", 0xF8EC},
  {"parenleftinferior", 0x208D}, {"parenleftsuperior", 0x207D}, {"parenlefttp", 0xF8EB}, {"parenlefttp (CUS)", 0xF8EB},
  {"parenright", 0x0029}, {"parenrightbt", 0xF8F8}, {"parenrightbt (CUS)", 0xF8F8}, {"parenrightex", 0xF8F7},
  {"parenrightex (CUS)", 0xF8F7}, {"parenrightinferior", 0x208E}, {"parenrightsuperior", 0x207E}, {"parenrighttp", 0xF8F6},
  {"parenrighttp (CUS)", 0xF8F6}, {"partialdiff", 0x2202}, {"pecyrillic", 0x043F}, {"pemiddlehookcyrillic", 0x04A7}, {"percent", 0x0025},
  {"period", 0x002E}, {"periodcentered", 0x00B7}, {"periodinferior", 0xF6E7}, {"periodsuperior", 0xF6E8}, {"perpendicular", 0x22A5},
  {"perthousand", 0x2030}, {"peseta", 0x20A7}, {"phi", 0x03C6}, {"phi1", 0x03D5}, {"phisymbolgreek", 0x03D5}, {"phook", 0x01A5},
  {"pi", 0x03C0}, {"pisymbolgreek", 0x03D6}, {"plus", 0x002B}, {"pluscircle", 0x2295}, {"plusminus", 0x00B1}, {"plusmod", 0x02D6},
  {"plussuperior", 0x207A}, {"pointingindexdownwhite", 0x261F}, {"pointingindexleftwhite", 0x261C}, {"pointingindexrightwhite", 0x261E},
  {"pointingindexupwhite", 0x261D}, {"precedes", 0x227A}, {"prescription", 0x211E}, {"primemod", 0x02B9}, {"primereversed", 0x2035},
  {"product", 0x220F}, {"projective", 0x2305}, {"propellor", 0x2318}, {"propersubset", 0x2282}, {"propersuperset", 0x2283},
  {"proportion", 0x2237}, {"proportional", 0x221D}, {"psi", 0x03C8}, {"psicyrillic", 0x0471}, {"psilipneumatacyrilliccmb", 0x0486},
  {"q", 0x0071}, {"quarternote", 0x2669}, {"question", 0x003F}, {"questiondown", 0x00BF}, {"questiondownsmall", 0xF7BF},
  {"questiongreek", 0x037E}, {"questionsmall", 0xF73F}, {"quotedbl", 0x0022}, {"quotedblbase", 0x201E}, {"quotedblleft", 0x201C},
  {"quotedblright", 0x201D}, {"quoteleft", 0x2018}, {"quoteleftreversed", 0x201B}, {"quotereversed", 0x201B}, {"quoteright", 0x2019},
  {"quoterightn", 0x0149}, {"quotesinglbase", 0x201A}, {"quotesingle", 0x0027}, {"r", 0x0072}, {"racute", 0x0155}, {"radical", 0x221A},
  {"radicalex", 0xF8E5}, {"radicalex (CUS)", 0xF8E5}, {"ratio", 0x2236}, {"rcaron", 0x0159}, {"rcedilla", 0x0157}, {"rcommaaccent", 0x0157},
  {"rdblgrave", 0x0211}, {"referencemark", 0x203B}, {"reflexsubset", 0x2286}, {"reflexsuperset", 0x2287}, {"registered", 0x00AE},
  {"registersans", 0xF8E8}, {"registersans (CUS)", 0xF8E8}, {"registerserif", 0xF6DA}, {"registerserif (CUS)", 0xF6DA},
  {"reversedtilde", 0x223D}, {"revlogicalnot", 0x2310}, {"rho", 0x03C1}, {"rhookturnedsuperior", 0x02B5}, {"rhosymbolgreek", 0x03F1},
  {"rhotichookmod", 0x02DE}, {"rightangle", 0x221F}, {"righttriangle", 0x22BF}, {"ring", 0x02DA}, {"ringhalfleft", 0x02BF},
  {"ringhalfleftcentered", 0x02D3}, {"ringhalfright", 0x02BE}, {"ringhalfrightcentered", 0x02D2}, {"rinvertedbreve", 0x0213},
  {"rsuperior", 0xF6F1}, {"rtblock", 0x2590}, {"rturnedsuperior", 0x02B4}, {"rupiah", 0xF6DD}, {"s", 0x0073}, {"sacute", 0x015B},
  {"scaron", 0x0161}, {"scedilla", 0x015F}, {"schwacyrillic", 0x04D9}, {"schwadieresiscyrillic", 0x04DB}, {"scircumflex", 0x015D},
  {"scommaaccent", 0x0219}, {"second", 0x2033}, {"secondtonechinese", 0x02CA}, {"section", 0x00A7}, {"semicolon", 0x003B},
  {"seven", 0x0037}, {"seveneighths", 0x215E}, {"seveninferior", 0x2087}, {"sevenoldstyle", 0xF737}, {"sevenroman", 0x2176},
  {"sevensuperior", 0x2077}, {"sfthyphen", 0x00AD}, {"shacyrillic", 0x0448}, {"shade", 0x2592}, {"shadedark", 0x2593},
  {"shadelight", 0x2591}, {"shademedium", 0x2592}, {"shchacyrillic", 0x0449}, {"sheicoptic", 0x03E3}, {"sheqel", 0x20AA},
  {"sheqelhebrew", 0x20AA}, {"shhacyrillic", 0x04BB}, {"shimacoptic", 0x03ED}, {"sigma", 0x03C3}, {"sigma1", 0x03C2},
  {"sigmafinal", 0x03C2}, {"sigmalunatesymbolgreek", 0x03F2}, {"similar", 0x223C}, {"six", 0x0036}, {"sixinferior", 0x2086},
  {"sixoldstyle", 0xF736}, {"sixroman", 0x2175}, {"sixsuperior", 0x2076}, {"slash", 0x002F}, {"slong", 0x017F}, {"slongdotaccent", 0x1E9B},
  {"smileface", 0x263A}, {"softhyphen", 0x00AD}, {"softsigncyrillic", 0x044C}, {"space", 0x0020}, {"spacehackarabic", 0x0020},
  {"spade", 0x2660}, {"spadesuitblack", 0x2660}, {"spadesuitwhite", 0x2664}, {"squarediagonalcrosshatchfill", 0x25A9},
  {"squarehorizontalfill", 0x25A4}, {"squareorthogonalcrosshatchfill", 0x25A6}, {"squareupperlefttolowerrightfill", 0x25A7},
  {"squareupperrighttolowerleftfill", 0x25A8}, {"squareverticalfill", 0x25A5}, {"squarewhitewithsmallblack", 0x25A3}, {"ssuperior", 0xF6F2},
  {"sterling", 0x00A3}, {"subset", 0x2282}, {"subsetnotequal", 0x228A}, {"subsetorequal", 0x2286}, {"succeeds", 0x227B},
  {"suchthat", 0x220B}, {"summation", 0x2211}, {"sun", 0x263C}, {"superset", 0x2283}, {"supersetnotequal", 0x228B},
  {"supersetorequal", 0x2287}, {"t", 0x0074}, {"tackdown", 0x22A4}, {"tackleft", 0x22A3}, {"tau", 0x03C4}, {"tbar", 0x0167},
  {"tcaron", 0x0165}, {"tcedilla", 0x0163}, {"tcommaaccent", 0x0163}, {"tdieresis", 0x1E97}, {"tecyrillic", 0x0442},
  {"tedescendercyrillic", 0x04AD}, {"telephone", 0x2121}, {"telephoneblack", 0x260E}, {"tenroman", 0x2179}, {"tetsecyrillic", 0x04B5},
  {"thereexists", 0x2203}, {"therefore", 0x2234}, {"theta", 0x03B8}, {"theta1", 0x03D1}, {"thetasymbolgreek", 0x03D1}, {"thook", 0x01AD},
  {"thorn", 0x00FE}, {"thousandcyrillic", 0x0482}, {"three", 0x0033}, {"threeeighths", 0x215C}, {"threeinferior", 0x2083},
  {"threeoldstyle", 0xF733}, {"threequarters", 0x00BE}, {"threequartersemdash", 0xF6DE}, {"threeroman", 0x2172}, {"threesuperior", 0x00B3},
  {"tilde", 0x02DC}, {"tildeoperator", 0x223C}, {"timescircle", 0x2297}, {"titlocyrilliccmb", 0x0483}, {"tonebarextrahighmod", 0x02E5},
  {"tonebarextralowmod", 0x02E9}, {"tonebarhighmod", 0x02E6}, {"tonebarlowmod", 0x02E8}, {"tonebarmidmod", 0x02E7}, {"tonefive", 0x01BD},
  {"tonesix", 0x0185}, {"tonetwo", 0x01A8}, {"tonos", 0x0384}, {"tpalatalhook", 0x01AB}, {"trademark", 0x2122}, {"trademarksans", 0xF8EA},
  {"trademarksans (CUS)", 0xF8EA}, {"trademarkserif", 0xF6DB}, {"trademarkserif (CUS)", 0xF6DB}, {"triagdn", 0x25BC}, {"triaglf", 0x25C4},
  {"triagrt", 0x25BA}, {"triagup", 0x25B2}, {"tsecyrillic", 0x0446}, {"tshecyrillic", 0x045B}, {"tsuperior", 0xF6F3},
  {"twelveroman", 0x217B}, {"two", 0x0032}, {"twodotenleader", 0x2025}, {"twodotleader", 0x2025}, {"twoinferior", 0x2082},
  {"twooldstyle", 0xF732}, {"tworoman", 0x2171}, {"twostroke", 0x01BB}, {"twosuperior", 0x00B2}, {"twothirds", 0x2154}, {"u", 0x0075},
  {"uacute", 0x00FA}, {"ubreve", 0x016D}, {"ucaron", 0x01D4}, {"ucircumflex", 0x00FB}, {"ucyrillic", 0x0443}, {"udblacute", 0x0171},
  {"udblgrave", 0x0215}, {"udieresis", 0x00FC}, {"udieresisacute", 0x01D8}, {"udieresiscaron", 0x01DA}, {"udieresiscyrillic", 0x04F1},
  {"udieresisgrave", 0x01DC}, {"udieresismacron", 0x01D6}, {"udotbelow", 0x1EE5}, {"ugrave", 0x00F9}, {"uhookabove", 0x1EE7},
  {"uhorn", 0x01B0}, {"uhornacute", 0x1EE9}, {"uhorndotbelow", 0x1EF1}, {"uhorngrave", 0x1EEB}, {"uhornhookabove", 0x1EED},
  {"uhorntilde", 0x1EEF}, {"uhungarumlaut", 0x0171}, {"uhungarumlautcyrillic", 0x04F3}, {"uinvertedbreve", 0x0217}, {"ukcyrillic", 0x0479},
  {"umacron", 0x016B}, {"umacroncyrillic", 0x04EF}, {"underscore", 0x005F}, {"underscoredbl", 0x2017}, {"union", 0x222A},
  {"universal", 0x2200}, {"uogonek", 0x0173}, {"upblock", 0x2580}, {"upsilon", 0x03C5}, {"upsilondieresis", 0x03CB},
  {"upsilondieresistonos", 0x03B0}, {"upsilontonos", 0x03CD}, {"uptackmod", 0x02D4}, {"uring", 0x016F}, {"ushortcyrillic", 0x045E},
  {"ustraightcyrillic", 0x04AF}, {"ustraightstrokecyrillic", 0x04B1}, {"utilde", 0x0169}, {"v", 0x0076}, {"vecyrillic", 0x0432},
  {"venus", 0x2640}, {"verticalbar", 0x007C}, {"verticallinelowmod", 0x02CC}, {"verticallinemod", 0x02C8}, {"w", 0x0077},
  {"wacute", 0x1E83}, {"wcircumflex", 0x0175}, {"wdieresis", 0x1E85}, {"wdotaccent", 0x1E87}, {"wdotbelow", 0x1E89},
  {"weierstrass", 0x2118}, {"wgrave", 0x1E81}, {"whitebullet", 0x25E6}, {"whitecircle", 0x25CB}, {"whitecircleinverse", 0x25D9},
  {"whitediamond", 0x25C7}, {"whitediamondcontainingblacksmalldiamond", 0x25C8}, {"whitedownpointingsmalltriangle", 0x25BF},
  {"whitedownpointingtriangle", 0x25BD}, {"whiteleftpointingsmalltriangle", 0x25C3}, {"whiteleftpointingtriangle", 0x25C1},
  {"whiterightpointingsmalltriangle", 0x25B9}, {"whiterightpointingtriangle", 0x25B7}, {"whitesmallsquare", 0x25AB},
  {"whitesmilingface", 0x263A}, {"whitesquare", 0x25A1}, {"whitestar", 0x2606}, {"whitetelephone", 0x260F},
  {"whiteuppointingsmalltriangle", 0x25B5}, {"whiteuppointingtriangle", 0x25B3}, {"won", 0x20A9}, {"wring", 0x1E98}, {"wsuperior", 0x02B7},
  {"wynn", 0x01BF}, {"x", 0x0078}, {"xdieresis", 0x1E8D}, {"xdotaccent", 0x1E8B}, {"xi", 0x03BE}, {"xsuperior", 0x02E3}, {"y", 0x0079},
  {"yacute", 0x00FD}, {"yatcyrillic", 0x0463}, {"ycircumflex", 0x0177}, {"ydieresis", 0x00FF}, {"ydotaccent", 0x1E8F}, {"yen", 0x00A5},
  {"yericyrillic", 0x044B}, {"yerudieresiscyrillic", 0x04F9}, {"ygrave", 0x1EF3}, {"yhook", 0x01B4}, {"yicyrillic", 0x0457},
  {"yinyang", 0x262F}, {"yotgreek", 0x03F3}, {"ypogegrammeni", 0x037A}, {"yr", 0x01A6}, {"yring", 0x1E99}, {"ysuperior", 0x02B8},
  {"yusbigcyrillic", 0x046B}, {"yusbigiotifiedcyrillic", 0x046D}, {"yuslittlecyrillic", 0x0467}, {"yuslittleiotifiedcyrillic", 0x0469},
  {"z", 0x007A}, {"zacute", 0x017A}, {"zcaron", 0x017E}, {"zcircumflex", 0x1E91}, {"zdot", 0x017C}, {"zdotaccent", 0x017C},
  {"zdotbelow", 0x1E93}, {"zecyrillic", 0x0437}, {"zedescendercyrillic", 0x0499}, {"zedieresiscyrillic", 0x04DF}, {"zero", 0x0030},
  {"zeroinferior", 0x2080}, {"zerooldstyle", 0xF730}, {"zerosuperior", 0x2070}, {"zerowidthnonjoiner", 0x200C}, {"zerowidthspace", 0x200B},
  {"zeta", 0x03B6}, {"zhebrevecyrillic", 0x04C2}, {"zhecyrillic", 0x0436}, {"zhedescendercyrillic", 0x0497},
  {"zhedieresiscyrillic", 0x04DD}, {"zlinebelow", 0x1E95}, {"zstroke", 0x01B6},
};
/* the names again, one per code point (the usual one), as indexes into agl_tab sorted by code point */
static const u16 agl_rev[1699] = {
  1733, 1237, 1640, 1498, 1157, 1596, 928, 1650, 1575, 1584, 988, 1610, 1107, 1334, 1597, 1727, 1942, 1542, 1815, 1780, 1269, 1260, 1722,
  1699, 1194, 1471, 1102, 1698, 1420, 1221, 1303, 1635, 993, 0, 46, 53, 74, 97, 152, 159, 180, 193, 233, 237, 256, 270, 275, 289, 348, 357,
  359, 371, 433, 458, 506, 510, 518, 523, 541, 1038, 996, 1045, 986, 1853, 1301, 560, 995, 1061, 1126, 1174, 1246, 1277, 1314, 1338, 1385,
  1390, 1409, 1440, 1462, 1502, 1568, 1633, 1651, 1687, 1757, 1824, 1869, 1875, 1905, 1910, 1932, 1024, 997, 1031, 987, 1116, 1466, 1239,
  1073, 1746, 1125, 1916, 1053, 1697, 1148, 1117, 1557, 1310, 1431, 1705, 1664, 1441, 1139, 1612, 1822, 1787, 791, 1452, 1573, 1598, 1072,
  1551, 1558, 1311, 1549, 1546, 1784, 1636, 31, 5, 15, 44, 25, 40, 1, 57, 117, 98, 102, 111, 214, 197, 201, 205, 145, 286, 314, 292, 299,
  346, 309, 1457, 340, 476, 459, 463, 468, 524, 445, 1291, 919, 776, 785, 994, 795, 943, 800, 1068, 1192, 1175, 1179, 1187, 1351, 1339,
  1343, 1346, 1233, 1496, 1522, 1503, 1508, 1565, 1517, 1150, 1560, 1839, 1825, 1828, 1832, 1911, 1778, 1914, 38, 927, 7, 777, 39, 935, 54,
  1062, 59, 1069, 61, 1071, 56, 1067, 78, 1136, 79, 1137, 125, 1207, 100, 1177, 114, 1189, 132, 1218, 101, 1178, 167, 1285, 164, 1282, 170,
  1288, 166, 1284, 189, 1321, 188, 1320, 230, 1381, 220, 1358, 199, 1341, 223, 1375, 209, 1163, 195, 1356, 234, 1387, 246, 1400, 1402, 258,
  1410, 261, 1416, 260, 1415, 263, 1418, 267, 1436, 277, 1464, 279, 1468, 278, 1467, 1465, 129, 1214, 327, 1533, 296, 1506, 324, 1530, 290,
  1520, 360, 1652, 362, 1658, 361, 1657, 412, 1688, 420, 1693, 416, 1690, 414, 1689, 437, 1763, 436, 1762, 435, 1761, 505, 1868, 489, 1851,
  461, 1826, 500, 1864, 485, 1847, 491, 1857, 512, 1877, 527, 1913, 528, 542, 1933, 547, 1937, 543, 1934, 1434, 1054, 49, 52, 1056, 450,
  1798, 339, 71, 1086, 77, 84, 91, 1168, 1144, 137, 417, 133, 154, 1267, 175, 162, 1333, 225, 229, 250, 1405, 1414, 1412, 273, 281, 1478,
  298, 318, 1524, 325, 1531, 352, 1607, 1925, 451, 1799, 141, 1228, 1801, 444, 1777, 452, 479, 1841, 495, 508, 534, 1920, 559, 1954, 149,
  151, 1244, 1245, 1821, 449, 1797, 1296, 1904, 1096, 1097, 1095, 1098, 76, 93, 1170, 257, 265, 1428, 276, 283, 1476, 14, 784, 200, 1342,
  297, 1507, 462, 1827, 473, 1837, 469, 1833, 470, 1834, 472, 1836, 1235, 27, 797, 30, 799, 3, 802, 179, 1309, 165, 1283, 245, 1399, 337,
  1553, 338, 1554, 150, 1243, 1386, 75, 92, 1169, 160, 1278, 41, 944, 2, 801, 341, 1561, 24, 794, 35, 922, 110, 1186, 121, 1200, 204, 1345,
  218, 1354, 308, 1516, 326, 1532, 364, 1660, 368, 1682, 467, 1831, 487, 1849, 421, 1694, 1331, 1325, 1389, 1685, 1672, 370, 1903, 1927,
  1621, 1134, 1111, 910, 918, 1680, 1678, 1297, 1298, 960, 961, 962, 959, 1093, 1065, 1874, 1257, 1696, 1274, 1873, 1442, 1302, 792, 1106,
  1105, 1681, 1679, 1863, 1166, 1613, 1448, 1052, 1162, 1677, 1521, 1788, 1332, 1674, 1280, 1909, 1299, 1792, 1794, 1796, 1795, 1793, 1499,
  1500, 1924, 1638, 1800, 1149, 37, 934, 135, 144, 227, 335, 499, 333, 1378, 36, 48, 161, 82, 134, 552, 143, 443, 224, 242, 259, 274, 288,
  521, 334, 353, 366, 428, 434, 492, 351, 70, 354, 328, 226, 496, 926, 1220, 1232, 1379, 1861, 925, 1001, 1279, 1143, 1219, 1948, 1231,
  1774, 1376, 1395, 1411, 1456, 1497, 1908, 1540, 1608, 1671, 1718, 1717, 1760, 1859, 1604, 1085, 1630, 1534, 1377, 1860, 1541, 1862, 1539,
  1002, 1775, 493, 494, 497, 1605, 1535, 432, 86, 253, 413, 425, 1712, 153, 1248, 249, 1404, 190, 1327, 163, 1281, 427, 1716, 81, 1140,
  1396, 1673, 1720, 1923, 811, 839, 840, 109, 842, 203, 844, 845, 846, 847, 848, 849, 850, 897, 23, 806, 807, 808, 809, 810, 812, 813, 814,
  815, 816, 817, 818, 819, 306, 821, 822, 823, 824, 465, 826, 827, 828, 829, 830, 831, 832, 833, 834, 835, 836, 837, 793, 852, 853, 854,
  855, 856, 858, 859, 860, 861, 862, 863, 864, 865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880, 881, 882,
  883, 857, 885, 886, 887, 888, 889, 890, 891, 892, 893, 894, 895, 896, 901, 329, 1536, 898, 902, 122, 1201, 539, 1930, 540, 1931, 537,
  1928, 538, 1929, 254, 1408, 355, 1631, 899, 903, 900, 904, 232, 1384, 488, 1850, 331, 1537, 332, 1538, 345, 1564, 252, 1407, 1779, 1791,
  1571, 1129, 1632, 838, 884, 173, 1293, 172, 1292, 555, 1951, 550, 1940, 240, 1393, 244, 1398, 243, 1397, 238, 1391, 128, 1213, 130, 1215,
  350, 1595, 185, 1315, 140, 1227, 440, 1767, 503, 1866, 504, 1867, 186, 1316, 442, 1771, 66, 1081, 69, 1084, 426, 1715, 63, 1078, 65, 1080,
  1572, 553, 1949, 241, 1394, 131, 1216, 68, 1083, 9, 779, 26, 796, 34, 921, 211, 1349, 418, 905, 419, 1692, 556, 1952, 551, 1941, 94, 1171,
  221, 1359, 206, 1347, 310, 1518, 294, 1504, 295, 1505, 490, 1852, 471, 1835, 486, 1848, 67, 1082, 532, 1918, 516, 1882, 511, 1876, 513,
  1878, 514, 1879, 515, 1880, 520, 1907, 519, 1906, 530, 1915, 545, 1935, 548, 1938, 557, 1953, 1326, 1765, 1902, 1926, 942, 1729, 29, 798,
  33, 920, 16, 786, 18, 788, 19, 789, 21, 790, 17, 787, 8, 778, 11, 781, 12, 782, 13, 783, 10, 780, 115, 1190, 119, 1193, 147, 1234, 103,
  1180, 105, 1182, 106, 1183, 108, 1184, 104, 1181, 216, 1352, 210, 1348, 312, 1519, 317, 1523, 300, 1509, 302, 1511, 303, 1512, 305, 1513,
  301, 1510, 319, 1525, 321, 1527, 322, 1528, 323, 1529, 320, 1526, 475, 1838, 478, 1840, 480, 1842, 482, 1844, 483, 1845, 484, 1846, 481,
  1843, 533, 1919, 1217, 1947, 917, 908, 906, 907, 1337, 1254, 1212, 1209, 803, 1135, 1854, 1644, 1647, 1649, 1646, 1642, 1643, 1641, 1127,
  1128, 1057, 1543, 1816, 1205, 914, 915, 916, 1602, 1450, 1695, 1622, 1312, 1313, 1661, 1238, 1566, 990, 1275, 1945, 1273, 1265, 1726,
  1704, 1199, 1475, 1614, 1222, 1581, 1590, 1495, 1943, 1547, 1818, 1782, 1270, 1262, 1723, 1701, 1196, 1472, 1580, 1589, 1103, 1122, 1276,
  1427, 1603, 1901, 1713, 1161, 148, 1074, 1064, 1247, 213, 1437, 1501, 1881, 365, 1620, 1768, 1802, 316, 933, 1229, 923, 1552, 1823, 1544,
  1781, 1261, 1700, 336, 457, 447, 157, 156, 429, 422, 120, 282, 441, 124, 456, 1550, 1820, 1786, 1272, 1264, 1725, 1703, 1198, 1474, 1770,
  1204, 1814, 965, 976, 970, 955, 945, 977, 980, 982, 957, 956, 978, 1066, 1318, 1319, 972, 981, 968, 967, 971, 952, 954, 953, 951, 950,
  1570, 1569, 947, 949, 948, 946, 974, 975, 969, 983, 973, 958, 1063, 1856, 1593, 1242, 1210, 1361, 1300, 1203, 1481, 1751, 1480, 1623,
  1752, 1446, 1449, 1152, 989, 1059, 1653, 1629, 1362, 1559, 930, 1151, 1574, 1490, 1430, 1433, 1370, 1855, 1363, 1132, 1114, 1773, 999,
  1656, 1628, 1721, 1669, 992, 1113, 938, 924, 937, 1290, 939, 1360, 1483, 1223, 1487, 1421, 1304, 1425, 1308, 1455, 1454, 1488, 1484, 1489,
  1485, 1423, 1306, 1424, 1307, 1486, 1619, 1750, 1491, 1493, 1626, 1627, 1492, 1494, 1662, 1663, 1748, 1755, 1090, 1447, 1088, 1089, 1759,
  1758, 1601, 1676, 1164, 1124, 1123, 1422, 1305, 1206, 1330, 1115, 1624, 1670, 941, 1625, 1369, 1365, 1556, 1142, 1094, 931, 932, 1141,
  581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 381, 382, 372, 374, 373, 375, 379, 380, 377, 378, 376, 400, 388, 408, 409, 396, 386,
  385, 389, 407, 406, 395, 392, 391, 390, 393, 394, 399, 383, 384, 387, 404, 405, 398, 402, 403, 397, 411, 410, 401, 1858, 1156, 1021, 1426,
  1684, 1439, 1707, 1154, 1255, 184, 1744, 1739, 1743, 1740, 1741, 1742, 1738, 182, 183, 1256, 1810, 1900, 1019, 1899, 1012, 1893, 1892,
  1809, 1807, 1889, 1888, 1007, 1891, 1890, 1808, 741, 1886, 1887, 1258, 1435, 1087, 1165, 1060, 181, 1091, 1092, 744, 1371, 1372, 1009,
  1008, 1017, 1018, 1555, 1413, 694, 1897, 699, 1898, 573, 1616, 1618, 580, 1615, 1922, 1730, 1373, 1753, 1249, 1176, 1443, 1735, 1324,
  1147, 1099, 1737, 1322, 1146, 1101, 1329, 1634, 1458, 1459, 998, 1460, 1461, 561, 668, 672, 688, 710, 579, 578, 577, 591, 602, 613, 624,
  568, 635, 646, 657, 669, 679, 680, 681, 682, 683, 684, 685, 686, 721, 732, 743, 758, 562, 687, 689, 690, 691, 692, 693, 695, 696, 697,
  698, 700, 701, 702, 703, 704, 705, 706, 707, 708, 709, 711, 712, 713, 714, 715, 716, 717, 718, 719, 720, 722, 723, 724, 725, 726, 727,
  728, 729, 730, 731, 733, 735, 737, 673, 738, 674, 742, 745, 746, 747, 773, 774, 775, 563, 564, 565, 566, 567, 569, 570, 571, 756, 759,
  765, 767, 761, 763, 675, 748, 677, 750, 752, 754, 769, 771, 592, 593, 594, 595, 596, 597, 598, 599, 600, 601, 603, 604, 605, 606, 607,
  608, 609, 610, 611, 612, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 625, 664, 630, 660, 631, 632, 633, 634, 636, 637, 638, 639,
  627, 640, 641, 642, 643, 644, 645, 661, 647, 667, 648, 670, 649, 671, 650, 651, 665, 652, 662, 666, 653, 663, 654, 655, 656, 658, 659,
  1120, 1667, 1805, 1545, 1686, 1785, 1075, 1077, 1108, 1110, 1158, 1160, 1335, 1336, 1599, 1600, 991, 1055, 1167, 1230, 1380, 1438, 1451,
  1563, 1683, 1745, 1813, 50, 55, 72, 88, 192, 268, 291, 313, 367, 415, 448, 544, 1241, 1159, 929, 1944, 1548, 1819, 1783, 1271, 1263, 1724,
  1702, 1197, 1473, 1639, 177, 43, 51, 73, 90, 142, 158, 178, 191, 228, 236, 255, 269, 272, 285, 343, 356, 358, 369, 431, 455, 502, 509,
  517, 522, 536, 558, 1240, 1076, 85, 271, 22, 62, 1637, 32, 6, 20, 45, 28, 42, 4, 58, 118, 99, 107, 112, 215, 198, 202, 207, 146, 287, 315,
  293, 304, 347, 311, 342, 477, 460, 464, 474, 525, 446, 529, 757, 760, 766, 768, 762, 764, 676, 749, 678, 751, 753, 755, 770, 772, 1654,
  984, 963, 1665, 1118, 1803, 1582, 1578, 1576, 1043, 1041, 1039, 1029, 1027, 1025, 1022, 1366, 1591, 1587, 1585, 1050, 1048, 1046, 1036,
  1034, 1032, 1250, 1253, 1266, 1251, 1252,
};

/* bsearch's comparison: a name against an entry of agl_tab */
static int agl_cmp(const void *key, const void *ent)
{
  return strcmp((const char *)key, ((const AglEnt *)ent)->n);
}
/* the code point of exactly this name in the list; 0 if it isn't there */
static u32 agl_find(const char *name)
{
  const AglEnt *e = (const AglEnt *)bsearch(name, agl_tab, sizeof agl_tab / sizeof agl_tab[0], sizeof agl_tab[0], agl_cmp);
  return e ? e->u : 0;
}
/* n hex digits at s as a number; -1 if any isn't one */
static long hexn(const char *s, int n)
{
  long v = 0;
  int i;
  for (i = 0; i < n; i++) {
    int c = s[i], d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else return -1;
    v = v * 16 + d;
  }
  return v;
}
/* one component of a name (no '.' or '_'): the list, then uniXXXX (the first of a sequence) and uXXXX to uXXXXXX */
static u32 agl_part(const char *s)
{
  size_t n = strlen(s);
  long v;
  u32 u;
  if (!n) return 0;
  if ((u = agl_find(s)) != 0) return u;
  if (n >= 7 && !strncmp(s, "uni", 3) && (n - 3) % 4 == 0 && (v = hexn(s + 3, 4)) >= 0) {
    if (v >= 0xD800 && v <= 0xDFFF) return 0;
    return (u32)v;
  }
  if (n >= 5 && n <= 7 && s[0] == 'u' && (v = hexn(s + 1, (int)n - 1)) >= 0) {
    if ((v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF) return 0;
    return (u32)v;
  }
  return 0;     /* (gNN, cidNN, glyphNN and anything else unknown) */
}
/* a glyph name's code point: the base name before any '.', then the name as it is, a ligature's components joined
   (f_i is fi) or else its first component */
u32 agl_unicode(const char *name)
{
  char b[64], j[64];
  const char *dot;
  size_t n, i, k;
  u32 u;
  if (!name) return 0;
  dot = strchr(name, '.');
  n = dot ? (size_t)(dot - name) : strlen(name);
  if (!n || n >= sizeof b) return 0;
  memcpy(b, name, n); b[n] = 0;
  if (!strchr(b, '_')) return agl_part(b);
  for (i = k = 0; i < n; i++) if (b[i] != '_') j[k++] = b[i];
  j[k] = 0;
  if ((u = agl_find(j)) != 0) return u;
  *strchr(b, '_') = 0;
  return agl_part(b);
}
/* the usual name of a code point: a binary search of agl_rev */
const char *agl_name(u32 unicode)
{
  int lo = 0, hi = (int)(sizeof agl_rev / sizeof agl_rev[0]) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    u32 u = agl_tab[agl_rev[mid]].u;
    if (u == unicode) return agl_tab[agl_rev[mid]].n;
    if (u < unicode) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}
