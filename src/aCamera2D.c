/*
 * aCamera2D.c:
 *
 * Copyright (c) 2026 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 ************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>

#include "Archimedes.h"

static int prev_mouse_x = 0;
static int prev_mouse_y = 0;
static int panning = 0;

aCamera2D_t a_Camera2DInit( const int screen_width, const int screen_height )
{
  aCamera2D_t new_camera = 
    {
      .x = 0, .y = 0,
      .w = screen_width,
      .h = screen_height,
      .zoom = 1.0f,
      .zoom_speed = 0.05f,
      .offset_x = 0, .offset_y = 0,
      .shake = { 0, 0 }
    };

  return new_camera;
}

void a_CameraUpdate( aCamera2D_t* cam, aRectf_t* rect,
                     const int MAX_WIDTH, const int MAX_HEIGHT )
{
  float vis_w = (float)SCREEN_WIDTH  / cam->zoom;
  float vis_h = (float)SCREEN_HEIGHT / cam->zoom;

  cam->x = rect->x - ( vis_w / 2.0f ) + cam->offset_x;
  cam->y = rect->y - ( vis_h / 2.0f ) + cam->offset_y;

  if ( cam->x < 0 ) cam->x = 0;
  if ( cam->y < 0 ) cam->y = 0;
  if ( cam->x > MAX_WIDTH - vis_w )
  {
    cam->x = MAX_WIDTH - vis_w;
  }
  
  if ( cam->y > MAX_HEIGHT - vis_h )
  {
    cam->y = MAX_HEIGHT - vis_h;
  }

  if ( cam->shake.duration > 0 )
  {
    cam->x += ( rand() % (int)cam->shake.intensity ) - ( cam->shake.intensity / 2.0f );
    cam->y += ( rand() % (int)cam->shake.intensity ) - ( cam->shake.intensity / 2.0f );
    cam->shake.duration--;
  }
}

void a_CameraInput( aCamera2D_t* cam,
                    const int MAX_WIDTH, const int MAX_HEIGHT )
{
  if ( app.mouse.button == 2 )
  {
    if ( !panning )
    {
      panning = 1;
      prev_mouse_x = app.mouse.x;
      prev_mouse_y = app.mouse.y;

    }
  } 

  else
  {
    panning = 0;  
  }
  
  if ( app.mouse.state == 0 )
  {
    panning = 0;
    app.mouse.button = 0;
  }

  if ( app.mouse.wheel != 0 )
  {
    if ( app.mouse.wheel > 0 )
    {
      app.mouse.wheel = 0;
      cam->zoom *= ( 1.0f + cam->zoom_speed );
    }
    
    else if ( app.mouse.wheel < 0 )
    {
      app.mouse.wheel = 0;
      cam->zoom /= ( 1.0f + cam->zoom_speed );
    }

    if ( cam->zoom < 0.25f ) cam->zoom = 0.25f;
    if ( cam->zoom > 5.0f ) cam->zoom = 5.0f;
  }
  
  if ( app.mouse.motion == 1 )
  {
    if ( panning == 1 )
    {
      int delta_x = app.mouse.x - prev_mouse_x;
      int delta_y = app.mouse.y - prev_mouse_y;

      cam->offset_x -= delta_x / cam->zoom;
      cam->offset_y -= delta_y / cam->zoom;

      prev_mouse_x = app.mouse.x;
      prev_mouse_y = app.mouse.y;
    }
  }

  float vis_w = (float)SCREEN_WIDTH  / cam->zoom;
  float vis_h = (float)SCREEN_HEIGHT / cam->zoom;

  if ( cam->x < 0 ) cam->x = 0;
  if ( cam->x > (MAX_WIDTH - vis_w) ) cam->x = MAX_WIDTH - vis_w;
  if ( cam->y < 0 ) cam->y = 0;
  if ( cam->y > (MAX_HEIGHT - vis_h) ) cam->y = MAX_HEIGHT - vis_h;
}

void a_CameraShake( aCamera2D_t* cam,
                    const int intensity, const int duration )
{
  if ( cam == NULL ) return;

  cam->shake.duration  = duration;
  cam->shake.intensity = intensity;
}

uint8_t a_IsOnCamera( aCamera2D_t* cam, aRectf_t* obj )
{
  return ( obj->x + obj->w > cam->x &&
           obj->x < cam->x + cam->w &&
           obj->y + obj->h > cam->y &&
           obj->y < cam->y + cam->h );
}

void a_WorldToCamera( aCamera2D_t* cam,
                      const int Wx, const int Wy, int* Sx, int* Sy )
{
  *Sx = ( Wx - (int)cam->x ) * cam->zoom;
  *Sy = ( Wy - (int)cam->y ) * cam->zoom;
}

void a_WorldRectToCameraRect( aCamera2D_t* cam,
                              const aRectf_t wrect, aRectf_t* srect )
{
  *srect = (aRectf_t){
    .x = ( wrect.x - cam->x ) * cam->zoom,
    .y = ( wrect.y - cam->y ) * cam->zoom,
    .w = wrect.w * cam->zoom,
    .h = wrect.h * cam->zoom
  };
}

