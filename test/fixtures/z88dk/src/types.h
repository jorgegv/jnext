#ifndef TYPES_H
#define TYPES_H
#include <stdint.h>
struct point { int16_t x; int16_t y; uint8_t tag; };
struct rect { struct point a; struct point b; char name[4]; };
union word { uint16_t w; uint8_t b[2]; };
typedef struct node { struct node *next; int32_t val; } node_t;
struct flags { unsigned lo:3; unsigned mid:5; unsigned hi:8; };
enum colour { RED, GREEN = 5, BLUE };
typedef uint8_t byte_t;
int16_t other(int16_t q);
#endif
