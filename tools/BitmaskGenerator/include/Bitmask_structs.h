#ifndef __BM_STRUCTS_H__
#define __BM_STRUCTS_H__

#include <Archimedes.h>

typedef struct
{
  uint8_t bit_neighbors[8];
  uint8_t bitmask;
  aPoint2i_t pos;
} TileMask_t;

#endif
