/* 
 * @file src/aTile.c
 *
 * This file defines the functions used to create tile arrays.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "Archimedes.h"

aTileset_t* a_TilesetCreate( const char* filename,
                             const int tile_w, const int tile_h )
{
  aSpriteSheet_t* temp_sheet = a_SpriteSheetCreate( filename, tile_w, tile_h );
  
  aTileset_t* new_set = malloc( sizeof( aTileset_t ) * temp_sheet->img_count );
  if ( new_set == NULL ) return NULL;

  for ( int i = 0; i < temp_sheet->img_count; i++ )
  {
    int row = i / temp_sheet->v_count;
    int col = i % temp_sheet->v_count;
    new_set[i].img = a_ImageFromSpriteSheet( temp_sheet, row, col );
  }

  return new_set;
}

