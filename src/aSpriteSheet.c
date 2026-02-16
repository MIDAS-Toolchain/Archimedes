/* 
 * @file src/aSpriteSheet.c
 *
 * This file defines the functions used to display sprite sheets, and retrieve
 * single images from them.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "Archimedes.h"

aSpriteSheet_t* a_SpriteSheetCreate( const char* filename,
                                     int sprite_w, int sprite_h )
{
  aSpriteSheet_t* new_sheet = malloc( sizeof( aSpriteSheet_t ) );
  if ( new_sheet == NULL ) return NULL;

  new_sheet->sheet      = a_ImageLoad( filename );
  new_sheet->s_width    = sprite_w;
  new_sheet->s_height   = sprite_h;
  new_sheet->img_width  = new_sheet->sheet->surface->w;
  new_sheet->img_height = new_sheet->sheet->surface->h;
  new_sheet->v_count    = new_sheet->img_width  / new_sheet->s_width;
  new_sheet->h_count    = new_sheet->img_height / new_sheet->s_height;
  new_sheet->img_count  = new_sheet->v_count * new_sheet->h_count; 

  return new_sheet;
}

aImage_t* a_ImageFromSpriteSheet( aSpriteSheet_t* sheet, int x, int y )
{

  return NULL;
}

