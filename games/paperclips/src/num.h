/* Numbers as the web game has them: JavaScript's doubles, worked out the same way on the calculator and on every
 * computer (num.c has its own exp, log, pow and sin, so no library can round differently), and printed the way the
 * game's pages print them (Number's toString and toFixed, toLocaleString, and the game's formatWithCommas, spellf,
 * numberCruncher and timeCruncher). */
#ifndef PC_NUM_H
#define PC_NUM_H
#include <stdbool.h>
#include <stdint.h>

#define JS_E 2.718281828459045

double js_floor(double x);
double js_ceil(double x);
double js_round(double x);             /* Math.round: halves go up */
double js_exp(double x);
double js_log(double x);
double js_log10(double x);
double js_pow(double x, double y);
double js_sin(double x);
bool js_finite(double x);

/* Every function writes into `out` (room for `n` bytes, always ended) and returns it. */
char *js_tostring(double x, char *out, int n);             /* Number.prototype.toString() */
char *js_tofixed(double x, int digits, char *out, int n);  /* toFixed(digits) */
char *js_locale(double x, char *out, int n);               /* toLocaleString() in English */
char *js_commas(double x, int decimals, char *out, int n); /* the game's formatWithCommas(num, decimals) */
char *js_spellf(double x, char *out, int n);               /* spellf(): "1.2 million" */
char *js_cruncher(double x, int decimals, char *out, int n); /* numberCruncher(): "1.23 million" */
char *js_time(double ticks, char *out, int n);             /* timeCruncher(): "1 hour 2 minutes 3 seconds" */
#endif
