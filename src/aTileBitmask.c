/* 
 * @file src/aTileBitmask.c
 *
 * This file defines the functions used to create a tile bitmask for autotiling.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "Archimedes.h"

#define NEIGHTBOR_COUNT 8

static uint8_t get_bitmask( aWorld_t* world, int index );

const aPoint2i_t neighbor_offsets[] = {
  {.x = -1, .y =  1},
  {.x =  0, .y =  1},
  {.x =  1, .y =  1},
  {.x =  1, .y =  0},
  {.x =  1, .y = -1},
  {.x =  0, .y = -1},
  {.x = -1, .y = -1},
  {.x = -1, .y =  0}
};

aTileBitmask_t* a_TileBitmaskCreate( aWorld_t* world )
{

  return NULL;
}

static uint8_t get_bitmask( aWorld_t* world, int index )
{

  return 0;
}

