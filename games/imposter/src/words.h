/* Imposter: the categories and their words (words.c). */
#ifndef IM_WORDS_H
#define IM_WORDS_H

#define N_CATS 30
#define CAT_MAX 64 /* words a category at most */
#define WORD_MAX 24 /* a word's letters at most, and its end */

typedef struct {
  const char *name;
  const char *words; /* separated by '|' */
} category_t;
extern const category_t categories[N_CATS];

int word_count(int cat);
/* word i of a category into out (cap bytes with its end); its length */
int word_get(int cat, int i, char *out, int cap);
#endif
