#ifndef ANGRY_INFLATE_H
#define ANGRY_INFLATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool inflate_raw(const uint8_t *src,size_t length,uint8_t *out,size_t capacity);
#endif
