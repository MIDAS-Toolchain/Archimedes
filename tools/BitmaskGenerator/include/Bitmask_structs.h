#ifndef __BM_STRUCTS_H__
#define __BM_STRUCTS_H__

#include <Archimedes.h>
#include <Daedalus.h>

typedef struct lut
{
  uint8_t table[MAX_UINT8];
}LUT_t; //Look Up Table

typedef struct bitmask_array
{
  uint8_t mask;
  uint8_t img_index;
} BitmaskArray_t;

typedef struct bitmask
{
  BitmaskArray_t arr[MAX_UINT8];
  aSpriteSheet_t* sheet;
} Bitmask_t;

typedef struct
{
  uint8_t bitmask;
  aPoint2i_t pos;
} TileMask_t;

#endif
