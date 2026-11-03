#ifndef UTIL_H
#define UTIL_H
#include <stdint.h>
#define UTIL_CLAMP(v, m) ((v) > (m) ? (m) : (v))
uint8_t util_twice(uint8_t v);
#ifdef __SDCC
static inline uint8_t util_inc(uint8_t v) { return v + 1; }
#else
#define util_inc(v) ((v) + 1)
#endif
#endif
