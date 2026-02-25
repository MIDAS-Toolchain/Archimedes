/* 
 * src/aViewport.c
 *
 * This file defines the functions used to draw basic constructs
 * to the screen with respect to a viewport.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include "Archimedes.h"

static int prev_mouse_x = 0;
static int prev_mouse_y = 0;
static int panning = 0;
static aPoint2f_t scale = {0};

aPoint2f_t a_ViewportCalculateScale( void )
{
  aPoint2f_t scale = { .x = (float)SCREEN_WIDTH  / ( app.g_viewport.w * 2.0f ),
                    .y = (float)SCREEN_HEIGHT / ( app.g_viewport.h * 2.0f ) };
  return scale;
}

uint8_t a_ViewportIsRectVisible( aRectf_t rect )
{
  if ( rect.x + rect.w < app.g_viewport.x - app.g_viewport.w ||
       rect.x - rect.w > app.g_viewport.x + app.g_viewport.w || 
       rect.y + rect.h < app.g_viewport.y - app.g_viewport.h ||
       rect.y - rect.h > app.g_viewport.y + app.g_viewport.h )
  {
    return 0;
  }

  return 1;
}

uint8_t a_ViewportIsPointVisible( aPoint2f_t point )
{
  if ( point.x < app.g_viewport.x - app.g_viewport.w ||
       point.x > app.g_viewport.x + app.g_viewport.w || 
       point.y < app.g_viewport.y - app.g_viewport.h ||
       point.y > app.g_viewport.y + app.g_viewport.h )
  {
    return 0;
  }

  return 1;
}

void a_ViewportDrawPoint( aPoint3f_t p, aColor_t color )
{
  aPoint2f_t current_scale = a_ViewportCalculateScale();

  float viewport_x1 = app.g_viewport.x - app.g_viewport.w;
  float viewport_y1 = app.g_viewport.y - app.g_viewport.h;

  int x = (int)( ( p.x - viewport_x1 ) * current_scale.x );
  int y = (int)( ( p.y - viewport_y1 ) * current_scale.y );

  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawPoint(app.renderer, x, y);
  // Reset the renderer color to white
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_ViewportDrawRect( aRectf_t rect, aColor_t color )
{
  aPoint2f_t current_scale = a_ViewportCalculateScale();

  float viewport_x1 = app.g_viewport.x - app.g_viewport.w;
  float viewport_y1 = app.g_viewport.y - app.g_viewport.h;

  float world_x1 = rect.x - rect.w;
  float world_y1 = rect.y - rect.h;

  float world_width  = rect.w * 2.0f;
  float world_height = rect.h * 2.0f;

  SDL_Rect r = {
    (int)( ( world_x1 - viewport_x1 ) * current_scale.x ),
    (int)( ( world_y1 - viewport_y1 ) * current_scale.y ),
    (int)( world_width  * current_scale.x ),
    (int)( world_height * current_scale.y )
  };
  
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_BLEND );
  SDL_SetRenderDrawColor( app.renderer, color.r, color.g, color.b, color.a );
 
  SDL_RenderDrawRect( app.renderer, &r );
  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode( app.renderer, SDL_BLENDMODE_NONE );
}

void a_ViewportBlit( aImage_t* img, float x, float y )
{
  aPoint2f_t current_scale = a_ViewportCalculateScale();

  float viewport_x1 = app.g_viewport.x - app.g_viewport.w;
  float viewport_y1 = app.g_viewport.y - app.g_viewport.h;

  float world_x1 = x - img->rect.w;
  float world_y1 = y - img->rect.h;

  float world_width  = img->rect.w;
  float world_height = img->rect.h;

  SDL_FRect r = {
    ( ( world_x1 - viewport_x1 ) * current_scale.x ),
    ( ( world_y1 - viewport_y1 ) * current_scale.y ),
    ( world_width  * current_scale.y ),
    ( world_height * current_scale.y )
  };
  
  if ( img->color_modulate )
  {
    SDL_SetTextureColorMod( img->texture,
                            img->color.r, img->color.g, img->color.b);
    SDL_SetTextureAlphaMod( img->texture, img->color.a );
  }
  
  SDL_RenderCopyF( app.renderer, img->texture, NULL, &r );
  
  if ( img->color_modulate )
  {
    SDL_SetTextureColorMod( img->texture,
                            255, 255, 255);
    SDL_SetTextureAlphaMod( img->texture, 255 );
  }
}

void a_ViewportInput( aRectf_t* viewport, float width, float height )
{
  scale = a_ViewportCalculateScale();

  float pan_dist_x = viewport->w * PAN_FACTOR;
  float pan_dist_y = viewport->h * PAN_FACTOR;
  
  if ( app.keyboard[A_W] == 1 )
  {
    app.keyboard[A_W] = 0;
    viewport->y -= pan_dist_y;
  }
  
  if ( app.keyboard[A_A] == 1 )
  {
    app.keyboard[A_A] = 0;
    viewport->x -= pan_dist_x;

  }
  
  if ( app.keyboard[A_S] == 1 )
  {
    app.keyboard[A_S] = 0;
    viewport->y += pan_dist_y;

  }
  
  if ( app.keyboard[A_D] == 1 )
  {
    app.keyboard[A_D] = 0;
    viewport->x += pan_dist_x;

  }
  
  if ( app.mouse.button == 2 )
  {
    if ( !panning )
    {
      prev_mouse_x = app.mouse.x;
      prev_mouse_y = app.mouse.y;

    }
    panning = 1;
  }
  
  if ( app.mouse.state == 0 )
  {
    panning = 0;
    app.mouse.button = 0;
  }

  if ( app.mouse.wheel == 1 )
  {
    app.mouse.wheel = 0;
    viewport->w *= ZOOM_FACTOR;
    viewport->h *= ZOOM_FACTOR;

    if ( viewport->w < 1.0f ) viewport->w = 1.0f;
    if ( viewport->h < 1.0f ) viewport->h = 1.0f;

  }
 
  if ( app.mouse.wheel == -1 )
  {
    app.mouse.wheel = 0;
    viewport->w /= ZOOM_FACTOR;
    viewport->h /= ZOOM_FACTOR;

    if ( viewport->w > width / 2.0f ) viewport->w = width / 2.0f;
    if ( viewport->h > height / 2.0f ) viewport->h = height / 2.0f;
    
  }
  
  if ( app.mouse.motion == 1 )
  {
    if ( panning == 1 )
    {
      int delta_x = app.mouse.x - prev_mouse_x;
      int delta_y = app.mouse.y - prev_mouse_y;

      float delta_wx = (float)delta_x / scale.x;
      float delta_wy = (float)delta_y / scale.y;

      viewport->x -= delta_wx;
      viewport->y -= delta_wy;

      prev_mouse_x = app.mouse.x;
      prev_mouse_y = app.mouse.y;

    }
  }

  if ( viewport->x < viewport->w ) viewport->x = viewport->w;
  if ( viewport->x > width - viewport->w ) viewport->x = width - viewport->w;
  if ( viewport->y < viewport->h ) viewport->y = viewport->h;
  if ( viewport->y > height - viewport->h ) viewport->y = height - viewport->h;

}

