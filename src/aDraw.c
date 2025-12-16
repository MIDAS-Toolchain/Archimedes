/**
 * @file src/aDraw.c
 * @brief Drawing system implementation for the Archimedes engine
 * 
 * This file contains the implementation of all 2D drawing functions for the Archimedes
 * graphics engine. It provides a comprehensive set of drawing primitives including:
 * - Scene management (prepare/present)
 * - Basic primitives (points, lines, shapes)
 * - Filled and outlined shapes
 * - Surface and texture blitting operations
 * - Color management and render state handling
 * 
 * All drawing functions use SDL2 as the underlying graphics backend.
 * 
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
#include <stdio.h>
#include <stdlib.h>

#include "Archimedes.h"

void a_PrepareScene( void )
{
  SDL_SetRenderDrawColor(app.renderer, app.background.r, app.background.g, app.background.b, app.background.a);
  SDL_RenderClear(app.renderer);
  // Reset the renderer color to white
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
}

void a_PresentScene( void )
{
  SDL_RenderPresent(app.renderer);
}

void a_DrawPoint( const int x, const int y, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawPoint(app.renderer, x, y);
  // Reset the renderer color to white
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_DrawLine( const int x1, const int y1, const int x2, const int y2, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
  // Reset the renderer color to white
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_DrawHorizontalLine( const int x1, const int x2, const int y, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(app.renderer, x1, y, x2, y);
  // Reset the renderer color to white
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_DrawVerticalLine( const int y1, const int y2, const int x, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(app.renderer, x, y1, x, y2);
  // Reset the renderer color to white
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_DrawCircle( const int posX, const int posY, const int radius, const aColor_t color )
{
  int x = 0;
  int y = radius;
  int decision = 5 - ( 4 * radius );

  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor( app.renderer, color.r, color.g, color.b, color.a );
  while ( x <= y )
  {
    SDL_RenderDrawPoint( app.renderer, posX + x, posY - y );
    SDL_RenderDrawPoint( app.renderer, posX + x, posY + y );
    SDL_RenderDrawPoint( app.renderer, posX - x, posY - y );
    SDL_RenderDrawPoint( app.renderer, posX - x, posY + y );
    SDL_RenderDrawPoint( app.renderer, posX + y, posY - x );
    SDL_RenderDrawPoint( app.renderer, posX + y, posY + x );
    SDL_RenderDrawPoint( app.renderer, posX - y, posY - x );
    SDL_RenderDrawPoint( app.renderer, posX - y, posY + x );
    SDL_SetRenderDrawColor( app.renderer, 255, 255, 255, 255 );

    if ( decision > 0 )
    {
      y--;
      decision -= 8 *  y;
    }

    x++;

    decision += 8 * x + 4;
  }
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_DrawFilledCircle( const int posX, const int posY, const int radius, const aColor_t color )
{
  int x = 0;
  int y = radius;
  int decision = 5 - ( 4 * radius );

  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  while ( x <= y )
  {
    SDL_RenderDrawLine(app.renderer, posX - x, posY - y, posX + x, posY - y);
    SDL_RenderDrawLine(app.renderer, posX - y, posY - x, posX + y, posY - x);
    SDL_RenderDrawLine(app.renderer, posX - y, posY + x, posX + y, posY + x);
    SDL_RenderDrawLine(app.renderer, posX - x, posY + y, posX + x, posY + y);

    if ( decision > 0 )
    {
      y--;
      decision -= 8 *  y;
    }

    x++;

    decision += 8 * x + 4;
  }
  // Reset color to white after drawing is complete
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_DrawTriangle( const int x0, const int y0, const int x1, const int y1,
                     const int x2, const int y2, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(app.renderer, x0, y0, x1, y1);
  SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
  SDL_RenderDrawLine(app.renderer, x2, y2, x0, y0);
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

/*void a_DrawFilledTriangle( const int x0, const int y0, const int x1, const int y1,
                           const int x2, const int y2, const aColor_t color )
{
  int maxX = MAX(x0, MAX(x1, x2));
  int minX = MIN(x0, MIN(x1, x2));
  int maxY = MAX(y0, MAX(y1, y2));
  int minY = MIN(y0, MIN(y1, y2));

  dVec2_t v1 = {x1 - x0, y1 - y0};
  dVec2_t v2 = {x2 - x0, y2 - y0};

  for (int x = minX; x <= maxX; x++)
  {
    for (int y = minY; y <= maxY; y++)
    {
      dVec2_t q = {x - x0, y - y0};

      float s = d_CrossProductVec2f(q, v2) / d_CrossProductVec2f(v1, v2);
      float t = d_CrossProductVec2f(v1, q) / d_CrossProductVec2f(v1, v2);

      if ( (s >= 0) && (t >= 0) && (s + t <= 1)) {
        SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
        SDL_RenderDrawPoint(app.renderer, x, y);
      }
    }
  }
}*/

void a_DrawRect( const aRectf_t rect, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor( app.renderer, color.r, color.g, color.b, color.a );
  SDL_Rect sdl_rect = (SDL_Rect){ rect.x, rect.y, rect.w, rect.h };
  SDL_RenderDrawRect( app.renderer, &sdl_rect );
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}


void a_DrawFilledRect( const aRectf_t rect, const aColor_t color )
{
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor( app.renderer, color.r, color.g, color.b, color.a );
  SDL_Rect sdl_rect = (SDL_Rect){ rect.x, rect.y, rect.w, rect.h };
  SDL_RenderFillRect( app.renderer, &sdl_rect );
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_Blit( aImage_t* img, float x, float y )
{
  if ( !img ) return;
  
  // Query texture for its original dimensions
  int temp_w, temp_h;
  SDL_QueryTexture( img->texture, NULL, NULL, &temp_w, &temp_h );

  SDL_FRect dest;
  dest.x = x;
  dest.y = y;
  dest.w = temp_w;
  dest.h = temp_h;
  
  SDL_RenderCopyF( app.renderer, img->texture, NULL, &dest );
}

void a_BlitRect( aImage_t* img, aRectf_t* src, aRectf_t* dest, const float scale )
{
  SDL_FRect temp_dest = {0};
  SDL_Rect temp_src = {0};

  if ( !img ) return;

  if ( dest != NULL )
  {
    temp_dest = (SDL_FRect){ .x = dest->x,
      .y = dest->y,
      .w = dest->w * scale,
      .h = dest->h * scale
    };
  }
  
  else
  {
    int temp_w, temp_h;
    SDL_QueryTexture( img->texture, NULL, NULL, &temp_w, &temp_h );
    temp_dest.w = temp_w;
    temp_dest.h = temp_h;
  }

  if ( src != NULL )
  {
    temp_src = (SDL_Rect){ .x = src->x,
      .y = src->y,
      .w = src->w,
      .h = src->h
    };
  }
  
  else
  {
    SDL_QueryTexture( img->texture, NULL, NULL, &temp_src.w, &temp_src.h );
  }

  SDL_RenderCopyF( app.renderer, img->texture, &temp_src, &temp_dest );
}

void a_BlitSurfaceToSurfaceScaled( aImage_t* src, aRectf_t* src_rect,
                                   aImage_t* dest, aRectf_t* dest_rect,
                                   float scale )
{
  if ( !src ) return;
  if ( !dest ) return;

  SDL_Rect temp_src_rect = {0};
  SDL_Rect temp_dest_rect = (SDL_Rect){
    .x = dest_rect->x,
    .y = dest_rect->y,
    .w = dest_rect->w * scale,
    .h = dest_rect->h * scale,
  };

  if ( src_rect != NULL )
  {
    temp_src_rect = (SDL_Rect){
      .x = src_rect->x,
      .y = src_rect->y,
      .w = src_rect->w,
      .h = src_rect->h,
    };
  }
  
  else
  {
    SDL_QueryTexture( src->texture, NULL, NULL,
                      &temp_src_rect.w, &temp_src_rect.h );
  }

  SDL_BlitSurface( src->surface, &temp_src_rect, dest->surface, &temp_dest_rect );
  dest->texture = SDL_CreateTextureFromSurface( app.renderer, dest->surface );
}

void a_UpdateTitle( const char *title )
{
  SDL_SetWindowTitle( app.window, title );
}

void a_SetPixel( SDL_Surface *surface, int x, int y, aColor_t c )
{
  if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) {
    return;
  }
  
  uint32_t color;
  color = ( c.a << 24 ) | ( c.r << 16 ) | ( c.g << 8 ) | c.b;

  if (SDL_MUSTLOCK(surface)) {
    if (SDL_LockSurface(surface) < 0) {
      return;
    }
  }

  Uint8 *pixel_address = (Uint8 *)surface->pixels + y * surface->pitch 
                         + x * surface->format->BytesPerPixel;

  *(Uint32 *)pixel_address = color;

  if (SDL_MUSTLOCK(surface)) {
    SDL_UnlockSurface(surface);
  }
}

/**
 * These color constants provide convenient access to commonly used colors
 * without needing to construct aColor_t structures manually. All colors
 * use full alpha (255) for complete opacity.
 */

// Basic color palette
aColor_t black   = {   0,   0,   0, 255 };  ///< Pure black
aColor_t blue    = {   0,   0, 255, 255 };  ///< Pure blue
aColor_t green   = {   0, 255,   0, 255 };  ///< Pure green
aColor_t cyan    = {   0, 255, 255, 255 };  ///< Blue + Green
aColor_t red     = { 255,   0,   0, 255 };  ///< Pure red
aColor_t magenta = { 255,   0, 255, 255 };  ///< Red + Blue
aColor_t yellow  = { 255, 255,   0, 255 };  ///< Red + Green
aColor_t white   = { 255, 255, 255, 255 };  ///< Pure white

// Custom color palette
aColor_t shit0   = { 128, 128, 128, 255 };  ///< Medium gray
aColor_t shit1   = { 128, 255, 255, 255 };  ///< Light cyan
aColor_t shit2   = { 128, 128, 255, 255 };  ///< Light blue
aColor_t shit3   = {   0, 255, 128, 255 };  ///< Light green

// Grayscale palette (gray0 = darkest, gray9 = lightest)
aColor_t gray9   = { 235, 235, 235, 255 };  ///< Very light gray
aColor_t gray8   = { 215, 215, 215, 255 };  ///< Light gray
aColor_t gray7   = { 195, 195, 195, 255 };  ///< Light-medium gray
aColor_t gray6   = { 175, 175, 175, 255 };  ///< Medium-light gray
aColor_t gray5   = { 155, 155, 155, 255 };  ///< Medium gray
aColor_t gray4   = { 135, 135, 135, 255 };  ///< Medium-dark gray
aColor_t gray3   = { 115, 115, 115, 255 };  ///< Dark-medium gray
aColor_t gray2   = {  95,  95,  95, 255 };  ///< Dark gray
aColor_t gray1   = {  55,  55,  55, 255 };  ///< Very dark gray
aColor_t gray0   = {  35,  35,  35, 255 };  ///< Near black

