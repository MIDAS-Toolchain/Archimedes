/* 
 * @file src/a2DWorld.c
 *
 * This file defines the functions used to create, display, and manipulate
 * 2D Worlds.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "Archimedes.h"

aWorld_t* a_2DWorldCreate( int width, int height, int tile_w, int tile_h )
{
  aWorld_t* new_world = malloc( sizeof( aWorld_t ) );
  if ( new_world == NULL ) return NULL;
  
  new_world->rows = width  / tile_w;
  new_world->cols = height / tile_h;
  new_world->tile_count = new_world->rows * new_world->cols;
  
  new_world->map = malloc( sizeof( aTile_t ) * 
                           ( new_world->rows * new_world->cols ) );
  
  if ( new_world->map == NULL )
  {
    free( new_world );
    return NULL;
  }

  for ( int i = 0; i < ( new_world->rows * new_world->cols ); i++ )
  {
    new_world->map[i].solid = 1;
    new_world->map[i].tile  = 0;
  }

  return new_world;
}

