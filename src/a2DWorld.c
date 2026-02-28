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
  
  new_world->rows = width;
  new_world->cols = height;
  new_world->tile_w = tile_w;
  new_world->tile_h = tile_h;
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

void a_2DWorldDraw( int x_off, int y_off, aWorld_t* world, aTileset_t* tile_set )
{
  for ( int i = 0; i < world->tile_count; i++ )
  {
    int row = i % world->cols;
    int col = i / world->cols;
    int x = (row * world->tile_w)+x_off;
    int y = (col * world->tile_h)+y_off;
    
    aTile_t img_index = world->map[i];
    aImage_t* tile_img = tile_set[img_index.tile].img;
    
    if ( app.g_viewport.x != 0 && app.g_viewport.y != 0
         && app.g_viewport.w != 0 && app.g_viewport.h != 0 )
    {
      a_ViewportBlit( tile_img, x, y );
    }
    
    else
    {
      a_Blit( tile_img, x, y );
    }
  }
}

