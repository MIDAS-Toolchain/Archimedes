/* 
 * @file src/aInitialize.c
 *
 * This file contains the Initialization for the Archimedes graphics engine
 * as well as the necessary functions to clean it up.
 *
 * The set of subsystems Initialized is:
 * - Audio
 * - Image
 * - Textures
 * - Fonts
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdlib.h>
#include <time.h>

#include "Archimedes.h"

aApp_t app;

typedef enum {
    INIT_SUCCESS = 0,
    INIT_ERROR_SDL = -1,
    INIT_ERROR_IMG = -2,
    INIT_ERROR_TTF = -3,
    INIT_ERROR_WINDOW = -4
} InitStatus_t;

static InitStatus_t a_ValidateSubsystems(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        return INIT_ERROR_SDL;
    }

    if (IMG_Init(IMG_INIT_PNG) == 0) {
        SDL_Quit();
        return INIT_ERROR_IMG;
    }

    if (TTF_Init() < 0) {
        IMG_Quit();
        SDL_Quit();
        return INIT_ERROR_TTF;
    }

    // Initialize SDL_mixer (support for OGG and MP3)
    int mix_flags = MIX_INIT_OGG | MIX_INIT_MP3;
    if ((Mix_Init(mix_flags) & mix_flags) != mix_flags) {
        // SDL_mixer init failed, but we can still use WAV files
        // Log warning but don't fail
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_mixer init warning: %s (WAV still supported)", Mix_GetError());
    }

    return INIT_SUCCESS;
}

static void a_CleanupSubsystems(void) {
    Mix_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

int a_Init( const int width, const int height, const char *title )
{
  InitStatus_t status = a_ValidateSubsystems();
  if (status != INIT_SUCCESS) {
    return status;
  }

  // Create window and renderer
  if (SDL_CreateWindowAndRenderer( width, height, 0, &app.window, &app.renderer ) < 0) {
    a_CleanupSubsystems();
    return INIT_ERROR_WINDOW;
  }

  // Set window title after window creation
  SDL_SetWindowTitle( app.window, title );

  // Initialize mouse state
  app.mouse = (aMouse_t){
    .x = 0,
    .y = 0,
    .state = 0,
    .pressed = 0,
    .button = 0,
    .wheel = 0,
    .clicks = 0 };

  // Initialize timing system
  app.time.frames  = 0;
  app.time.FPS_timer = a_TimerCreate();
  app.time.FPS_cap_timer = a_TimerCreate();
  a_TimerStart( app.time.FPS_timer );
  app.time.avg_FPS = 0.0f;

  // Initialize image cache
  app.img_cache = NULL;
  app.options.frame_cap = 1;
  app.options.scale_factor = 2;

  // Set app to running state
  app.running = 1;

  // Initialize internal Archimedes Systems
  a_AudioInit(16, 44100);  // 16 channels at 44.1kHz
  a_ImageInit();
  a_InitFonts();
 
  // Seed random number generator
  srand(time(NULL));

  return 0;
}

void a_Quit( void )
{
  // Call optional exit delegate first
  if ( app.delegate.onExit ) {
    app.delegate.onExit();
  }

  // Clean up Archimedes-level resources (before SDL shutdown)
  if ( app.time.FPS_timer ) {
    a_TimerFree( app.time.FPS_timer );
    app.time.FPS_timer = NULL;
  }
  if ( app.time.FPS_cap_timer ) {
    a_TimerFree( app.time.FPS_cap_timer );
    app.time.FPS_cap_timer = NULL;
  }

  if ( app.img_cache ) {
    a_ImageCacheCleanUp();
    free( app.img_cache );
    app.img_cache = NULL;
  }

  // Clean up audio system (before SDL shutdown)
  a_AudioQuit();

  // Destroy SDL resources (must come before subsystem shutdown)
  if ( app.renderer ) {
    SDL_DestroyRenderer( app.renderer );
    app.renderer = NULL;
  }

  if ( app.window ) {
    SDL_DestroyWindow( app.window );
    app.window = NULL;
  }

  // Shutdown SDL subsystems last (reverse order of init)
  a_CleanupSubsystems();

  // Reset app state
  app.running = 0;
}


char* log_level_strings[LOG_LEVEL_COUNT] =
{
  "NORMAL",
  "WARNING",
  "FATAL",
  "INFO",
  "DEBUG",
  "TRACE",
};

