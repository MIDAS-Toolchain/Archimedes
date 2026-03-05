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

void a_SpriteSheetDestroy( aSpriteSheet_t* sheet )
{
  if ( sheet == NULL ) return;

  //a_ImageFree( sheet->sheet );
  free( sheet );
}

aImage_t* a_ImageFromSpriteSheet( aSpriteSheet_t* sheet, int x, int y )
{
  aImage_t* sprite  = a_ImageCreate( sheet->s_width, sheet->s_height );
  aRectf_t src_rect = (aRectf_t){ .x = x * sheet->s_width,
                                  .y = y * sheet->s_height,
                                  .w = sheet->s_width,
                                  .h = sheet->s_height };
  
  a_BlitSurfaceToSurfaceScaled( sheet->sheet, &src_rect, sprite, NULL, 1 );
  
  return sprite;
}

