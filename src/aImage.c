/* 
 * @file src/aImage.c
 
 * This file manages the loading, caching, and retrieval of all SDL_Surface
 * image assets to prevent redundant disk access. It ensures images are loaded
 * only once and handles memory cleanup upon shutdown. It also provides utility
 * functions, such as a viewport screenshot capture
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <SDL2/SDL_surface.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL_image.h>

#include "Archimedes.h"

static aImageCache_t  img_head;
static aImageCache_t* img_tail;

static int a_CacheImage( char* filename, aImage_t* img );
static aImage_t* a_GetImage( const char* filename );

int a_ImageInit( void )
{
  memset( &img_head, 0, sizeof( aImageCache_t ) );
  img_tail = &img_head;

  return 0;
}

aImage_t* a_ImageLoad( const char *filename )
{
  aImage_t *img = NULL;

  img = a_GetImage( filename );
  
  if ( img == NULL )
  {
    img = malloc( sizeof( aImage_t ) );
    if ( img == NULL )
    {
      LOG( "Failed to allocate memory for img" );
    }
    
    //SDL_LogMessage( SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, "Loading %s", filename );
    img->filename = strndup( filename, MAX_FILENAME_LENGTH );
    img->surface = IMG_Load( filename );
    img->texture = SDL_CreateTextureFromSurface( app.renderer, img->surface );
    
    img->rect = (aRectf_t){
      .x = 0,
      .y = 0,
      .w = (int)img->surface->w,
      .h = (int)img->surface->h,
    };

    if ( img->surface == NULL )
    {
      aError_t new_error;
      new_error.error_type = FATAL;
      snprintf( new_error.error_msg, MAX_LINE_LENGTH, "%s: Failed to load image: %s, %s",
               log_level_strings[new_error.error_type], filename, SDL_GetError() );
      LOG( new_error.error_msg );

      return NULL;
    }

    a_CacheImage( img->filename, img );
  }

  return img;
}

static int a_CacheImage( char* filename, aImage_t* img )
{
  aImageCache_t* new_cache = malloc( sizeof( aImageCache_t ) );
  if ( new_cache == NULL ) return 1;
  memset( new_cache, 0, sizeof( aImageCache_t ) );
  img_tail->next = new_cache;
  img_tail = new_cache;
  STRNCPY( filename, img->filename, MAX_FILENAME_LENGTH );
  new_cache->image = img;

  return 0;
}

static aImage_t* a_GetImage( const char* filename )
{
  aImageCache_t* temp;

  for ( temp = img_head.next; temp != NULL; temp = temp->next )
  {
    if ( strcmp( temp->image->filename, filename ) == 0 )
    {
      return temp->image;
    }
  }

  return NULL;
}

int a_ImageCacheCleanUp( void )
{
  aImageCache_t* current = img_head.next;
  aImageCache_t* next_node;
  
  while ( current != NULL )
  {
    next_node = current->next;
    if ( current->image != NULL )
    {
      a_ImageFree( current->image );
    }

    free( current );

    current = next_node;
  }

  img_head.next = NULL;
  img_tail = &img_head;
  
  return 0;
}

aImage_t* a_ImageCreate( int width, int height )
{
  aImage_t* img = malloc( sizeof( aImage_t ) );
  if ( img == NULL )
  {
    LOG( "Failed to allocate memory for img" );
  }

  img->surface = SDL_CreateRGBSurface( 0, width, height, 32,
                                       0x00FF0000,
                                       0x0000FF00,
                                       0x000000FF,
                                       0xFF000000);
  if ( img->surface == NULL )
  {
    free( img );
    return NULL;
  }

  img->texture = NULL;
  img->filename = NULL;
  img->color_modulate = 0;
  img->color = white;
  img->rect = (aRectf_t){ 0, 0, width, height };

  return img;
}

void a_ImageFree( aImage_t* img )
{
  if ( !img ) return;

  if ( img->filename )
  {
    free( img->filename );
    img->filename = NULL;
  }

  if ( img->surface )
  {
    SDL_FreeSurface( img->surface );
    img->surface = NULL; 
  }

  if ( img->texture )
  {
    SDL_DestroyTexture( img->texture );
    img->texture = NULL;
  }
  
  free( img );
}

int a_ScreenshotSave( SDL_Renderer *renderer, const char *filename )
{
  SDL_Rect aViewport;
  SDL_Surface *aSurface = NULL;

  SDL_RenderGetViewport( renderer, &aViewport );

  aSurface = SDL_CreateRGBSurface( 0, aViewport.w, aViewport.h, 32, 0, 0, 0, 0 );

  if ( aSurface == NULL )
  {
    aError_t new_error;
    new_error.error_type = WARNING;
    snprintf( new_error.error_msg, MAX_LINE_LENGTH, "%s: Failed to create surface %s",
             log_level_strings[new_error.error_type], SDL_GetError() );
    LOG( new_error.error_msg );

    return 0;
  }

  if ( SDL_RenderReadPixels( renderer, NULL, aSurface->format->format, aSurface->pixels, aSurface->pitch ) != 0 )
  {
    aError_t new_error;
    new_error.error_type = WARNING;
    snprintf( new_error.error_msg, MAX_LINE_LENGTH, "%s: Failed to read pixels from renderer: %s",
             log_level_strings[new_error.error_type], SDL_GetError() );
    LOG( new_error.error_msg );

    SDL_FreeSurface( aSurface );
    return 0;
  }

  if ( IMG_SavePNG( aSurface, filename ) != 0 )
  {
    aError_t new_error;
    new_error.error_type = WARNING;
    snprintf( new_error.error_msg, MAX_LINE_LENGTH, "%s: Failed to save surface as png: %s, %s",
             log_level_strings[new_error.error_type], filename, SDL_GetError() );
    LOG( new_error.error_msg );

    SDL_FreeSurface( aSurface );
    return 0;
  }

  SDL_FreeSurface( aSurface );
  return 1;
}

