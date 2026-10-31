/* Temple Run: what is kept (subway.sav), and its copy in a Python script. */
#ifndef SS_SAVE_H
#define SS_SAVE_H
#include <stdbool.h>
void save_load(void);    /* the progress, or a new one; brought back from the copy if the file is gone */
bool save_write(void);
#endif
