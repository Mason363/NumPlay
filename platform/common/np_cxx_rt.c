/* The one piece of the C++ runtime the web builds of games with C++ files need (classes with pure virtual functions) */
#include <stdlib.h>
void __cxa_pure_virtual(void) { abort(); }
