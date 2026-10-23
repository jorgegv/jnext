#ifndef BANKED_H
#define BANKED_H
#include <stdint.h>
#ifdef USE_BANKED_KW
#define BANKED_FN __banked
#else
#define BANKED_FN
#endif
uint8_t banked_add(uint8_t a, uint8_t b) BANKED_FN;
#endif
