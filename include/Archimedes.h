/*
 *   Archimedes is a lightweight, cross-platform C game engine
 * built on SDL2 that provides essential 2D graphics, audio,
 * input handling, and UI widget functionality.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 ************************************************************************
 */

#ifndef __ARCHIMEDES_H__
#define __ARCHIMEDES_H__

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

/*
---------------------------------------------------------------
---                         Defines                         ---
---------------------------------------------------------------
*/

#define FPS_CAP 60
#define LOGIC_RATE ( 1000 / FPS_CAP )

#define MAX_KEYBOARD_KEYS 350
#define LOG_LEVEL_COUNT   6

#define SCREEN_WIDTH_16_9_1  1600
#define SCREEN_HEIGHT_16_9_1 900
#define SCREEN_WIDTH_16_9_0  1280
#define SCREEN_HEIGHT_16_9_0 720

#define SCREEN_WIDTH_5_4  1280
#define SCREEN_HEIGHT_5_4 1024

#define SCREEN_WIDTH_4_3_2  1024
#define SCREEN_HEIGHT_4_3_2 768
#define SCREEN_WIDTH_4_3_1  800
#define SCREEN_HEIGHT_4_3_1 600
#define SCREEN_WIDTH_4_3_0  640
#define SCREEN_HEIGHT_4_3_0 480

#define SCREEN_WIDTH  1280
#define SCREEN_HEIGHT 720

#define FONT_SIZE 32 
#define FONT_TEXTURE_SIZE 512
#define MAX_GLYPHS 400
#define MAX_GLYPH_SIZE 8
#define MAX_WORD_LENGTH 32
#define MAX_LINE_LENGTH 1024
#define MAX_WIDGET_IMAGE 4
#define MAX_WIDGET_COUNT 256

// Text system error codes
#define ARCH_TEXT_SUCCESS 0
#define ARCH_TEXT_ERROR_NULL_POINTER -1
#define ARCH_TEXT_ERROR_INVALID_FONT -2
#define ARCH_TEXT_ERROR_BUFFER_OVERFLOW -3
#define ARCH_TEXT_ERROR_INVALID_UTF8 -4

#ifndef __DAEDALUS_H__

#define MAGIC_NUMBER "CAFEBABE"
#define MAX_LINE_LENGTH        1024
#define MAX_FILENAME_LENGTH    256
#define MAX_NAME_LENGTH        32
#define MAX_DESCRIPTION_LENGTH 256
#define MAX_INPUT_LENGTH       16
#define PI 3.14159265

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define STRCPY( dest, src ) strncpy( dest, src, sizeof( dest ) ); dest[sizeof(dest) - 1] = '\0'
#define STRNCPY(dest, src, n) strncpy(dest, src, n); dest[n - 1] = '\0'
#define RANDF(lower, upper) (((float)rand() / (float)(RAND_MAX)) * (upper - lower)) + lower
#define MAP( value, start0, start1, end0, end1 ) ( ( value - start0 ) * ( ( end1 - end0 ) / ( start1 - start0 ) ) + end0 )
#define LOG( msg ) printf( "%s | File: %s, Line: %d\n", msg, __FILE__, __LINE__ )

#endif

enum
{
  FONT_CODE_PAGE_437,
  FONT_ENTER_COMMAND,
  FONT_LINUX,
  FONT_GAME,
  FONT_MAX
};

enum
{
  AUF_Invalid,
  AUF_False,
  AUF_True,
  AUF_NULL,
  AUF_Number,
  AUF_String,
  AUF_Array,
  AUF_Object,
  AUF_Raw
};

/*
---------------------------------------------------------------
---                         Structs                         ---
---------------------------------------------------------------
*/

/**
 * @brief Floating-point rectangle structure
 *
 * Represents a rectangle with floating-point precision for smooth
 * positioning and scaling. Used throughout Archimedes for entity
 * positions, UI layout, and collision detection.
 *
 * @param x X coordinate of the top-left corner
 * @param y Y coordinate of the top-left corner
 * @param w Width of the rectangle
 * @param h Height of the rectangle
 */
typedef struct
{
  float x, y;
  float w, h;
} aRectf_t;

/**
 * @brief Integer rectangle structure
 *
 * Represents a rectangle with integer precision for pixel-perfect
 * rendering and grid-aligned UI elements.
 *
 * @param x X coordinate of the top-left corner (pixels)
 * @param y Y coordinate of the top-left corner (pixels)
 * @param w Width of the rectangle (pixels)
 * @param h Height of the rectangle (pixels)
 */
typedef struct
{
  int x, y;
  int w, h;
} aRecti_t;

typedef struct
{
  float x, y;
} aPoint2f_t;

typedef struct
{
  int x, y;
} aPoint2i_t;

typedef struct
{
  float x, y, z;
} aPoint3f_t;

typedef struct
{
  int x, y, z;
} aPoint3i_t;

typedef struct
{
  SDL_Surface* surface;
  SDL_Texture* texture;
  char* filename;
} aImage_t;

typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} aColor_t;

typedef struct
{
  uint32_t start_ticks;
  uint32_t paused_ticks;
  uint8_t paused;
  uint8_t started;
} aTimer_t;

typedef struct
{
  char error_msg[MAX_LINE_LENGTH];
  int  error_type;
} aError_t;

typedef struct
{
  aImage_t * sprite_sheet;
  int frame_count;
  int frame_index;
  aRectf_t sprite_rect;
  uint32_t frame_duration;
  aTimer_t* animation_timer;
} aAnimation_t;

typedef struct _widget_t
{
  int type;
  char name[MAX_FILENAME_LENGTH];
  aRectf_t rect;
  char label[MAX_FILENAME_LENGTH];
  int toggle_label;
  int boxed;
  int hidden;
  int padding;
  int flex;
  aPoint2i_t grid_size; //row/col
  aPoint2f_t grid_pos;  //x,y
  int texture;
  aColor_t fg;
  aColor_t bg;
  aImage_t* images[MAX_WIDGET_IMAGE];
  int state;
  aPoint3f_t text_offset;
  struct _widget_t* next;
  struct _widget_t* prev;
  void (*action)( void );
  void (*data);
} aWidget_t;

typedef struct
{
  aRectf_t rect;
  int spacing;
  int num_components;
  aWidget_t* components;
} aContainerWidget_t;

typedef struct
{
  int num_options;
  char** options;
  aRectf_t rect;
  int value;
  char* text_name[MAX_NAME_LENGTH];
} aSelectWidget_t;

typedef struct
{
  aRectf_t rect;
  int value;
  int step;
  int wait_on_change;
} aSliderWidget_t;

typedef struct
{
  aRectf_t rect;
  int max_length;
  char* text;
} aInputWidget_t;

typedef struct
{
  int x, y;
  int value;
} aControlWidget_t;

typedef struct {
  char magic_number[8];
  uint8_t version;
  uint16_t num_widgets;
  char filename[MAX_FILENAME_LENGTH];
} aWidgetFileHeader_t;

typedef struct _aAUFNode_t {
  struct _aAUFNode_t* next;
  struct _aAUFNode_t* prev;

  struct _aAUFNode_t* child;

  int type;

  char* value_string;
  int value_int;
  double value_double;
  char* string;

} aAUFNode_t;

typedef struct _aAUF_t
{
  aAUFNode_t* head;
  aAUFNode_t* tail;
  int size;

} aAUF_t;

typedef struct
{
  uint8_t frame_cap;
  int scale_factor;
} aOptions_t;

typedef struct
{
  int x;
  int y;
  uint8_t pressed;
  uint8_t button;
  uint8_t state;
  uint8_t clicks;
  uint8_t motion;
  int8_t  wheel;
} aMouse_t;

typedef struct _aImageCacheNode_t
{
  aImage_t* image;
  struct _aImageCacheNode_t* next;
} aImageCacheNode_t;

typedef struct
{
  aImageCacheNode_t* head;
} aImageCache_t;

typedef struct
{
  uint32_t current_time; //Delta time
  uint32_t last_time;

  int frames;
  aTimer_t* FPS_timer;
  aTimer_t* FPS_cap_timer;
  float avg_FPS;
} aDeltaTime_t;

typedef struct
{
  void (*logic)( float delta_time );
  void (*draw)( float delta_time );
  void (*onExit)( void );
} aDelegate_t;

typedef struct
{
  SDL_Window* window;
  SDL_Renderer* renderer;
  aDelegate_t delegate;
  aOptions_t options;
  aDeltaTime_t time;
  aColor_t background;
  aImageCache_t* img_cache;
  int keyboard[MAX_KEYBOARD_KEYS];
  aWidget_t* active_widget;
  double font_scale;
  int font_type;
  SDL_Rect glyphs[FONT_MAX][MAX_GLYPHS];
  TTF_Font* fonts[FONT_MAX];
  SDL_Texture* font_textures[FONT_MAX];
  uint8_t glyph_exists[FONT_MAX][MAX_GLYPHS];  // Track which glyphs are in atlas
  int fallback_glyph[FONT_MAX];                 // Index of '-' fallback for each font
  aMouse_t mouse;
  int running;
  char input_text[MAX_INPUT_LENGTH];
  int last_key_pressed;
  aRectf_t g_viewport;
  struct {
    int channel_count;       // Total number of mixing channels
    int reserved_channels;   // Channels reserved from auto-allocation
    int master_volume;       // Master volume for sound effects (0-128)
    int music_volume;        // Music volume (0-128)
  } audio;
} aApp_t;

typedef struct _textures{
  char name[MAX_FILENAME_LENGTH];
  SDL_Texture* texture;
  struct _textures* next;
} aTexture_t;

/*
---------------------------------------------------------------
---                          Audio                          ---
---------------------------------------------------------------
*/

/**
 * @brief Maximum volume level for audio playback
 *
 * This constant matches SDL_mixer's MIX_MAX_VOLUME (128).
 * Volume values range from 0 (silent) to 128 (maximum).
 */
#define AUDIO_MAX_VOLUME 128

/**
 * @brief Audio channel assignments for organized sound management
 *
 * Reserved channels prevent auto-allocation and provide dedicated
 * audio channels for specific sound types. This prevents UI sounds
 * from interfering with gameplay sounds.
 */
typedef enum {
  AUDIO_CHANNEL_AUTO = -1,     /**< Use any free channel (auto-allocation) */
  AUDIO_CHANNEL_UI_HOVER = 0,  /**< Reserved for UI hover sounds */
  AUDIO_CHANNEL_UI_CLICK = 1,  /**< Reserved for UI click sounds */
  AUDIO_CHANNEL_PLAYER = 2,    /**< Reserved for player action sounds */
  AUDIO_CHANNEL_ENEMY = 3,     /**< Reserved for enemy sounds */
  AUDIO_CHANNEL_WEATHER = 4,   /**< Reserved for weather/ambient sounds */
} aAudioChannel_t;

/**
 * @brief Sound effect handle
 *
 * Represents a loaded sound effect that can be played on any channel.
 * Wraps SDL_mixer's Mix_Chunk* for memory-efficient sound playback.
 *
 * @note Call a_AudioFreeSound() when done to free memory
 */
typedef struct {
  char filename[MAX_FILENAME_LENGTH];  /**< Path to the sound file */
  Mix_Chunk* chunk;                    /**< SDL_mixer sound chunk */
  int default_volume;                  /**< Default volume (0-128) */
} aSoundEffect_t;

/**
 * @brief Music handle
 *
 * Represents loaded background music. Only one music track can play
 * at a time. Supports streaming formats (OGG, MP3, FLAC, MOD, MID).
 *
 * @note Call a_AudioFreeMusic() when done to free memory
 */
typedef struct {
  char filename[MAX_FILENAME_LENGTH];  /**< Path to the music file */
  Mix_Music* music;                    /**< SDL_mixer music handle */
  int default_volume;                  /**< Default volume (0-128) */
} aMusic_t;

/**
 * @brief Playback options for sound effects
 *
 * Controls how a sound effect is played, including channel assignment,
 * volume, looping, fade-in, and whether to interrupt existing sounds.
 */
typedef struct {
  int channel;      /**< Channel to play on (-1 = auto-select free channel) */
  int volume;       /**< Volume override (0-128, -1 = use sound's default) */
  int loops;        /**< Loop count (0 = play once, -1 = loop forever) */
  int fade_ms;      /**< Fade-in duration in milliseconds (0 = no fade) */
  int interrupt;    /**< If true, halt channel before playing (prevents queuing) */
} aAudioOptions_t;

/**
 * @brief Initialize the SDL_mixer audio system
 *
 * Sets up the audio mixer with the specified number of channels and
 * sample rate. Must be called before any other audio functions.
 *
 * @param channels Number of mixing channels to allocate (recommended: 16)
 * @param frequency Sample rate in Hz (recommended: 44100 or 48000)
 * @return 0 on success, -1 on failure
 *
 * @note Call a_AudioQuit() during shutdown to clean up resources
 * @note More channels = more simultaneous sounds, but higher memory usage
 *
 * @code
 * // Initialize with 16 channels at 44.1kHz
 * if (a_AudioInit(16, 44100) < 0) {
 *     printf("Audio initialization failed\n");
 * }
 * @endcode
 */
int a_AudioInit(int channels, int frequency);

/**
 * @brief Clean up the audio system
 *
 * Frees all audio resources and shuts down SDL_mixer.
 * Call this before SDL_Quit() during program shutdown.
 *
 * @note Automatically halts all playing sounds and music
 */
void a_AudioQuit(void);

/**
 * @brief Reserve channels from auto-allocation
 *
 * Reserves the first N channels (0 to num_reserved-1) for manual
 * assignment. This prevents a_AudioPlaySound() with AUDIO_CHANNEL_AUTO
 * from using these channels.
 *
 * @param num_reserved Number of channels to reserve from the start
 *
 * @code
 * // Reserve channels 0-3 for UI sounds, leave 4-15 for auto-allocation
 * a_AudioReserveChannels(4);
 * @endcode
 */
void a_AudioReserveChannels(int num_reserved);

/**
 * @brief Load a sound effect from file
 *
 * Loads a sound effect into memory for playback. Supports WAV, OGG,
 * and other formats depending on SDL_mixer configuration.
 *
 * @param filename Path to the sound file
 * @param sound Output sound effect structure (will be populated)
 * @return 0 on success, -1 on failure
 *
 * @note Loaded sounds are cached in memory - call a_AudioFreeSound() when done
 * @note Default volume is set to AUDIO_MAX_VOLUME (128)
 *
 * @code
 * aSoundEffect_t laser_sound;
 * if (a_AudioLoadSound("resources/audio/laser.wav", &laser_sound) < 0) {
 *     printf("Failed to load laser sound\n");
 * }
 * @endcode
 */
int a_AudioLoadSound(const char* filename, aSoundEffect_t* sound);

/**
 * @brief Play a sound effect with options
 *
 * Plays a sound effect with the specified playback options. Use NULL
 * for default options (auto-channel, default volume, play once).
 *
 * @param sound Sound effect to play (must be loaded with a_AudioLoadSound)
 * @param options Playback options (NULL = use defaults)
 * @return Channel number playing the sound, or -1 on error
 *
 * @note If options->interrupt is true, halts the channel before playing
 * @note Returns the channel number for later control (halt, volume adjust)
 */
int a_AudioPlaySound(aSoundEffect_t* sound, aAudioOptions_t* options);

/**
 * @brief Free a sound effect and release memory
 *
 * Frees the memory used by a loaded sound effect. The sound
 * structure will be invalid after this call.
 *
 * @param sound Sound effect to free
 *
 * @note Safe to call multiple times (checks for NULL)
 */
void a_AudioFreeSound(aSoundEffect_t* sound);

/**
 * @brief Halt sound playback on a channel
 *
 * Immediately stops sound playback on the specified channel.
 *
 * @param channel Channel to halt (-1 = halt all channels)
 */
void a_AudioHaltChannel(int channel);

/**
 * @brief Check if a channel is currently playing
 *
 * @param channel Channel to check (-1 = check if any channel is playing)
 * @return 1 if playing, 0 if not
 *
 * @code
 * if (!a_AudioIsChannelPlaying(AUDIO_CHANNEL_PLAYER)) {
 *     // Player stopped moving, footsteps finished
 * }
 * @endcode
 */
int a_AudioIsChannelPlaying(int channel);

/**
 * @brief Set volume for a specific channel
 *
 * Adjusts the volume for a channel. Affects currently playing sound
 * and future sounds on that channel.
 *
 * @param channel Channel to adjust (-1 = all channels)
 * @param volume Volume level (0 = silent, 128 = max)
 *
 * @code
 * // Reduce volume of UI sounds
 * a_AudioSetChannelVolume(AUDIO_CHANNEL_UI_HOVER, 64); // 50% volume
 * @endcode
 */
void a_AudioSetChannelVolume(int channel, int volume);

/**
 * @brief Load music from file
 *
 * Loads background music for streaming playback. Supports OGG, MP3,
 * WAV, FLAC, MOD, MID, and other formats.
 *
 * @param filename Path to the music file
 * @param music Output music structure (will be populated)
 * @return 0 on success, -1 on failure
 *
 * @note Music is streamed, not fully loaded into memory (efficient for large files)
 * @note Only one music track can play at a time
 * @note Default volume is set to AUDIO_MAX_VOLUME (128)
 *
 * @code
 * aMusic_t menu_music;
 * if (a_AudioLoadMusic("resources/music/menu_theme.ogg", &menu_music) < 0) {
 *     printf("Failed to load menu music\n");
 * }
 * @endcode
 */
int a_AudioLoadMusic(const char* filename, aMusic_t* music);

/**
 * @brief Play music with optional fade-in and looping
 *
 * Starts music playback. Only one music track can play at a time;
 * calling this stops any currently playing music.
 *
 * @param music Music to play (must be loaded with a_AudioLoadMusic)
 * @param loops Number of times to loop (-1 = loop forever, 0 = play once)
 * @param fade_ms Fade-in duration in milliseconds (0 = no fade)
 * @return 0 on success, -1 on failure
 *
 * @code
 * // Play menu music on loop with 2-second fade-in
 * a_AudioPlayMusic(&menu_music, -1, 2000);
 *
 * // Play boss music once (no loop, no fade)
 * a_AudioPlayMusic(&boss_music, 0, 0);
 * @endcode
 */
int a_AudioPlayMusic(aMusic_t* music, int loops, int fade_ms);

/**
 * @brief Stop music with optional fade-out
 *
 * Stops currently playing music, optionally fading out over time.
 *
 * @param fade_ms Fade-out duration in milliseconds (0 = stop immediately)
 *
 * @code
 * // Fade out music over 1 second
 * a_AudioStopMusic(1000);
 * @endcode
 */
void a_AudioStopMusic(int fade_ms);

/**
 * @brief Pause music playback
 *
 * Pauses the currently playing music. Resume with a_AudioResumeMusic().
 *
 * @note Does not reset playback position - resumes from where it paused
 */
void a_AudioPauseMusic(void);

/**
 * @brief Resume paused music
 *
 * Resumes music that was paused with a_AudioPauseMusic().
 */
void a_AudioResumeMusic(void);

/**
 * @brief Set music volume (independent from sound effects)
 *
 * Adjusts the volume for music playback only. Does not affect
 * sound effect volumes.
 *
 * @param volume Volume level (0 = silent, 128 = max)
 *
 * @code
 * // Set music to 50% volume
 * a_AudioSetMusicVolume(64);
 * @endcode
 */
void a_AudioSetMusicVolume(int volume);

/**
 * @brief Check if music is currently playing
 *
 * @return 1 if music is playing, 0 if not
 *
 * @code
 * if (!a_AudioIsMusicPlaying()) {
 *     // Music ended, start next track
 *     a_AudioPlayMusic(&next_track, -1, 1000);
 * }
 * @endcode
 */
int a_AudioIsMusicPlaying(void);

/**
 * @brief Free music and release resources
 *
 * Frees the memory used by loaded music. The music structure
 * will be invalid after this call.
 *
 * @param music Music to free
 *
 * @note Safe to call multiple times (checks for NULL)
 * @note Automatically stops music if currently playing
 */
void a_AudioFreeMusic(aMusic_t* music);

/**
 * @brief Create default playback options
 *
 * Returns an aAudioOptions_t structure with sensible defaults:
 * - Auto-select channel
 * - Use sound's default volume
 * - Play once (no loop)
 * - No fade-in
 * - Don't interrupt existing sound
 *
 * @return Default options structure
 *
 * @code
 * aAudioOptions_t opts = a_AudioDefaultOptions();
 * opts.interrupt = 1;  // Customize: interrupt existing sound
 * a_AudioPlaySound(&ui_click, &opts);
 * @endcode
 */
static inline aAudioOptions_t a_AudioDefaultOptions(void) {
  return (aAudioOptions_t){
    .channel = AUDIO_CHANNEL_AUTO,
    .volume = -1,
    .loops = 0,
    .fade_ms = 0,
    .interrupt = 0
  };
}

/**
 * @brief Quick play helper (simple wrapper for common case)
 *
 * Plays a sound effect with default options. Equivalent to
 * a_AudioPlaySound(sound, NULL).
 *
 * @param sound Sound effect to play
 * @return Channel number playing the sound, or -1 on error
 *
 * @code
 * // Quick play explosion sound
 * a_AudioQuickPlay(&explosion);
 * @endcode
 */
static inline int a_AudioQuickPlay(aSoundEffect_t* sound) {
  return a_AudioPlaySound(sound, NULL);
}

/*
---------------------------------------------------------------
---         DEPRECATED: Old Audio API (Backward Compat)     ---
---------------------------------------------------------------
*/

/**
 * @brief DEPRECATED: Old audio clip structure
 *
 * @deprecated Use aSoundEffect_t instead
 * @note This type is kept for backward compatibility only
 */
typedef struct {
  char filename[MAX_FILENAME_LENGTH];
  SDL_AudioSpec spec;
  uint32_t length;
  uint8_t* buffer;
} aAudioClip_t;

/**
 * @brief DEPRECATED: Initialize audio (old API)
 *
 * @deprecated Use a_AudioInit(channels, frequency) instead
 * @return 0 on success, -1 on failure
 */
int a_InitAudio(void);

/**
 * @brief DEPRECATED: Load sound (old API)
 *
 * @deprecated Use a_AudioLoadSound() instead
 * @param filename Path to WAV file
 * @param clip Output audio clip structure
 */
void a_LoadSounds(const char *filename, aAudioClip_t *clip);

/**
 * @brief DEPRECATED: Play sound effect (old API)
 *
 * @deprecated Use a_AudioPlaySound() instead
 * @param clip Audio clip to play
 *
 * @warning This function uses SDL_QueueAudio which causes sound queuing
 *          issues. Use the new API for proper channel-based playback.
 */
void a_AudioPlayEffect(aAudioClip_t *clip);

/*
---------------------------------------------------------------
---                      Delta Time                         ---
---------------------------------------------------------------
*/

float a_GetDeltaTime( void );
void a_GetFPS( void );

/*
---------------------------------------------------------------
---                        Draw                             ---
---------------------------------------------------------------
*/

/**
 * @brief Prepare the scene for rendering
 * 
 * Initializes the render target and clears the screen with the current background color.
 * This function must be called at the beginning of each frame before any drawing operations.
 * 
 * @note Sets the render draw color to white (255,255,255,255) for subsequent operations
 * @note Must be paired with a_PresentScene() to display the rendered frame
 */
void a_PrepareScene( void );

/**
 * @brief Present the rendered scene to the screen
 * 
 * Displays all drawing operations performed since a_PrepareScene() was called.
 * This completes the render cycle and should be called once per frame after all
 * drawing operations are complete.
 * 
 * @note Uses double-buffered rendering: swaps front and back buffers
 * @note Call this after all drawing operations in a frame
 */
void a_PresentScene( void );

/**
 * @brief Draw a single point at specified coordinates
 * 
 * Renders a single pixel at the given screen coordinates using the specified color.
 * 
 * @param x Horizontal position in pixels
 * @param y Vertical position in pixels
 * @param color RGBA color structure for the point
 * 
 */
void a_DrawPoint( const int x, const int y, const aColor_t color );

/**
 * @brief Draw a line between two points
 * 
 * Renders a straight line from the starting point to the ending point using
 * the specified color. Uses SDL's line drawing algorithm for optimal performance.
 * 
 * @param x1 Starting X coordinate
 * @param y1 Starting Y coordinate
 * @param x2 Ending X coordinate
 * @param y2 Ending Y coordinate
 * @param color RGBA color structure for the line
 * 
 */
void a_DrawLine( const int x1, const int y1, const int x2, const int y2, const aColor_t color );

/**
 * @brief Draw a horizontal line
 * 
 * Renders a horizontal line between two X coordinates at a fixed Y position.
 * 
 * @param x1 Starting X coordinate
 * @param x2 Ending X coordinate
 * @param y Y coordinate (same for both endpoints)
 * @param color RGBA color structure for the line
 * 
 * @note Optimized for horizontal lines
 * @note Order of x1 and x2 doesn't matter (automatically handles swapping)
 */
void a_DrawHorizontalLine( const int x1, const int x2, const int y, const aColor_t color );

/**
 * @brief Draw a vertical line
 * 
 * Renders a vertical line between two Y coordinates at a fixed X position.
 * 
 * @param y1 Starting Y coordinate
 * @param y2 Ending Y coordinate
 * @param x X coordinate (same for both endpoints)
 * @param color RGBA color structure for the line
 * 
 * @note Order of y1 and y2 doesn't matter (automatically handles swapping)
 */
void a_DrawVerticalLine( const int y1, const int y2, const int x, const aColor_t color );

/**
 * @brief Draw a circle outline
 * 
 * Renders the outline of a circle at the specified center position with the given radius.
 * Uses Bresenham's circle algorithm for efficient pixel-perfect circle drawing.
 * 
 * @param posX Center X coordinate
 * @param posY Center Y coordinate
 * @param radius Circle radius in pixels
 * @param color RGBA color structure for the circle outline
 */
void a_DrawCircle( const int posX, const int posY, const int radius, const aColor_t color );

/**
 * @brief Draw a filled circle
 * 
 * Renders a completely filled circle at the specified center position with the given radius.
 * Uses an optimized scan-line filling algorithm for smooth, solid circles.
 * 
 * @param posX Center X coordinate
 * @param posY Center Y coordinate
 * @param radius Circle radius in pixels
 * @param color RGBA color structure for the filled circle
 * 
 */
void a_DrawFilledCircle( const int posX, const int posY, const int radius, const aColor_t color );

/**
 * @brief Draw a triangle outline
 * 
 * Renders the outline of a triangle by drawing three lines connecting the specified vertices.
 * 
 * @param x0 First vertex X coordinate
 * @param y0 First vertex Y coordinate
 * @param x1 Second vertex X coordinate
 * @param y1 Second vertex Y coordinate
 * @param x2 Third vertex X coordinate
 * @param y2 Third vertex Y coordinate
 * @param color RGBA color structure for the triangle outline
 * 
 * @note Vertices can be specified in any order (clockwise or counter-clockwise)
 */
void a_DrawTriangle( const int x0, const int y0, const int x1, const int y1, const int x2,
                     const int y2, const aColor_t color );

/**
 * @brief Draw a filled triangle
 * 
 * Renders a completely filled triangle using the three specified vertices.
 * The implementation uses barycentric coordinate filling for accurate results.
 * 
 * @param x0 First vertex X coordinate
 * @param y0 First vertex Y coordinate
 * @param x1 Second vertex X coordinate
 * @param y1 Second vertex Y coordinate
 * @param x2 Third vertex X coordinate
 * @param y2 Third vertex Y coordinate
 * @param color RGBA color structure for the filled triangle
 * 
 * @note Currently implementation is disabled
 */
void a_DrawFilledTriangle( const int x0, const int y0, const int x1, const int y1,
                           const int x2, const int y2, const aColor_t color );

/**
 * @brief Draw a rectangle outline
 * 
 * Renders the outline of a rectangle at the specified position and dimensions.
 * 
 * @param rect the rectangle to be drawn
 * @param color (RGBA)
 * 
 */
void a_DrawRect( const aRectf_t rect, const aColor_t color );

/**
 * @brief Draw a filled rectangle
 * 
 * Renders a completely filled rectangle at the specified position and dimensions.
 * 
 * @param rect the rectangle to be drawn
 * @param color (RGBA)
 * 
 */
void a_DrawFilledRect( const aRectf_t rect, const aColor_t color );

/**
 * @brief Render texture at position with original dimensions
 *
 * Renders texture at original size. For scaling, use a_BlitTextureRect().
 *
 * @param texture Texture to render
 * @param x Screen X position
 * @param y Screen Y position
 *
 * @note For scaling or clipping, use a_BlitTextureRect()
 */
void a_Blit( aImage_t* img, int x, int y );

/**
 * @brief Render texture with scaling
 *
 * Renders texture at specified position and size with scaling factor.
 * Matches the signature of a_BlitSurfaceRect() for consistency.
 *
 * @param texture Texture to render
 * @param rect Destination rectangle (x, y, w, h)
 * @param scale Scaling factor for the destination size
 */
void a_BlitRect( aImage_t* img, aRectf_t* src, aRectf_t* dest, const float scale );

/**
 * @brief Blit a rectangular region of a surface to another surface
 *
 * @param surf Source SDL surface to draw from
 * @param dest Destination SDL surface to draw to
 * @param dest_rect Destination rect
 * @param scale Scaling factor for the destination size
 *
 */
void a_BlitSurfaceToSurfaceScaled( aImage_t* src, aImage_t* dest,
                                   aRectf_t dest_rect, int scale );

/**
 * Update the window title text
 *
 * Changes the text displayed in the window's title bar
 *
 * @param title Source of new Title
 *
 */
void a_UpdateTitle( const char *title );

void a_SetPixel( SDL_Surface *surface, int x, int y, aColor_t c );

/*
---------------------------------------------------------------
---                          Image                          ---
---------------------------------------------------------------
*/

/**
 * @brief Initialize the image cache system
 *
 * Allocates memory for the image cache and prepares it for use.
 * Must be called before any image loading operations.
 *
 * @return 0 on success, 1 on failure
 */
int a_ImageInit( void );

/**
 * @brief Load an image from disk with automatic caching
 *
 * Loads an SDL_Surface from the specified file path. If the image
 * has been loaded before, returns the cached version instead of
 * reloading from disk.
 *
 * @param filename Path to the image file to load
 * @return Pointer to SDL_Surface, or NULL on failure
 */
aImage_t* a_ImageLoad( const char *filename );

/**
 * @brief Clean up and free all cached images
 *
 * Frees all SDL_Surfaces stored in the image cache and resets
 * the cache structure. Should be called during shutdown.
 *
 * @return 0 on success, 1 if cache is NULL
 */
int a_ImageCacheCleanUp( void );

/**
 * @brief Capture the current renderer contents to a PNG file
 *
 * Reads pixels from the renderer and saves them as a PNG image
 * to the specified filename.
 *
 * @param renderer The SDL renderer to capture from
 * @param filename Path where the PNG file should be saved
 * @return 1 on success, 0 on failure
 */
int a_ScreenshotSave( SDL_Renderer *renderer, const char *filename );

/*
---------------------------------------------------------------
---                       Initialize                        ---
---------------------------------------------------------------
*/

extern aApp_t app;

/**
 * @brief Initialize the Archimedes framework and SDL subsystems
 * 
 * Sets up SDL (video, audio, timer), IMG (PNG support), TTF (font rendering),
 * creates the window and renderer, and initializes the global app state including
 * mouse tracking, timing system, image cache, and RNG seeding.
 * 
 * Must be called before any other Archimedes functions.
 * 
 * @param width Window width in pixels (must be > 0)
 * @param height Window height in pixels (must be > 0)
 * @param title Window title string (must not be NULL)
 * @return InitStatus_t error code:
 *         - INIT_SUCCESS (0): Initialization successful
 *         - INIT_ERROR_SDL (-1): SDL subsystem initialization failed
 *         - INIT_ERROR_IMG (-2): SDL_image initialization failed
 *         - INIT_ERROR_TTF (-3): SDL_ttf initialization failed
 *         - INIT_ERROR_WINDOW (-4): Window/renderer creation failed
 * 
 * @note Populates global `app` structure with initialized state
 * @note Automatically cleans up subsystems on failure
 * @see a_Quit()
 */
int a_Init( const int width, const int height, const char *title );

/**
 * @brief Cleans up all Archimedes resources and shuts down SDL.
 *
 * This function performs complete cleanup of the Archimedes framework by
 * calling the optional onExit delegate callback, destroying the renderer
 * and window, cleaning up the image cache, and shutting down SDL subsystems.
 * Should be called before program termination to ensure proper resource cleanup.
 *
 * The cleanup sequence includes:
 * - Calling the optional onExit delegate callback if set
 * - Destroying the SDL renderer
 * - Destroying the SDL window
 * - Cleaning up the image cache and freeing memory
 * - Shutting down TTF, IMG, and SDL subsystems
 * - Resetting the app running state
 */
void a_Quit( void );


/*
---------------------------------------------------------------
---                          Input                          ---
---------------------------------------------------------------
*/

/**
 * @brief Processes all pending SDL events and dispatches them to appropriate handlers.
 *
 * This function continuously polls for SDL events (keyboard, mouse, quit, text input)
 * using `SDL_PollEvent` and dispatches them to internal static functions (`a_DoKeyDown`,
 * `a_DoKeyUp`, `a_DoMouseDown`, `a_DoMouseUp`, `a_DoMouseWheel`, `a_DoMouseMotion`).
 * It updates the global `app` state based on these events, managing the game's input.
 * Unknown event types are silently ignored.
 *
 */
void a_DoInput( void );

/*
---------------------------------------------------------------
---                          Text                           ---
---------------------------------------------------------------
*/

enum
{
  TEXT_ALIGN_LEFT,    /**< Align text to the left of X coordinate */
  TEXT_ALIGN_CENTER,  /**< Center text horizontally at X coordinate */
  TEXT_ALIGN_RIGHT    /**< Align text to the right of X coordinate */
};

/** @brief Font configuration for styled text rendering */
typedef struct
{
  int type;          // Font type (FONT_ENTER_COMMAND, FONT_GAME, etc.)
  aColor_t fg;       // Foreground/text color
  aColor_t bg;       // Background color (alpha 0 = no background)
  int align;         // Text alignment (TEXT_ALIGN_LEFT/CENTER/RIGHT)
  int wrap_width;    // Word wrap width (0 = no wrap)
  float scale;       // Font scale multiplier (1.0 = default)
  int padding;       // Padding around text (expands background)
} aTextStyle_t;

/**
 * @brief Calculate the height of text with word wrapping
 *
 * Measures how tall the text will be when rendered with word wrapping enabled.
 * Useful for laying out text containers or calculating scroll regions.
 *
 * @param text Text string to measure (must not be NULL)
 * @param font_type Font type to use (FONT_ENTER_COMMAND, FONT_GAME, etc.)
 * @param max_width Maximum line width in pixels before wrapping
 * @return Height in pixels, or 0 on error (NULL text, invalid font, invalid width)
 */
int a_GetWrappedTextHeight( char* text, int font_type, int max_width );

/**
 * @brief Calculate text dimensions without rendering
 *
 * Measures the width and height of text as it would be rendered.
 * Does not wrap - calculates single-line dimensions.
 *
 * @param text Text string to measure (must not be NULL)
 * @param font_type Font type to use (FONT_ENTER_COMMAND, FONT_GAME, etc.)
 * @param w Pointer to store calculated width (set to 0 on error)
 * @param h Pointer to store calculated height (set to 0 on error)
 */
void a_CalcTextDimensions( const char* text, int font_type, float* w, float* h );

/**
 * @brief Draw text with styled configuration
 *
 * Renders text at the specified position using a font configuration structure.
 * Supports alignment, wrapping, scaling, and color. This is the recommended
 * text drawing function.
 *
 * @param content Text string to render (must not be NULL)
 * @param x X coordinate (meaning depends on alignment)
 * @param y Y coordinate (top of text baseline)
 * @param style Font configuration (use a_default_text_style for defaults)
 */
void a_DrawText( const char* content, int x, int y, aTextStyle_t style );

/**
 * @brief Create an SDL texture from text
 *
 * Renders text to a new SDL texture using the specified font.
 * The caller is responsible for destroying the texture with SDL_DestroyTexture().
 *
 * @param text Text string to render (must not be NULL)
 * @param font_type Font type to use (FONT_ENTER_COMMAND, FONT_GAME, etc.)
 * @return New SDL_Texture containing rendered text, or NULL on error
 */
SDL_Texture* a_GetTextTexture( char* text, int font_type );

/**
 * @brief Initializes the font system with TTF fonts.
 *
 * This function loads the required TTF fonts and creates glyph atlases for
 * efficient text rendering. 
 *
 * The function expects font files at:
 * - resources/fonts/EnterCommand.ttf (48pt)
 * - resources/fonts/JetBrains.ttf (32pt)
 * - resources/fonts/CodePage437.png (32pt)
 * - resources/fonts/font.png (32pt)
 *
 * If font loading fails, the program will exit with an error message.
 */
void a_InitFonts( void );

/** @brief Default font config (white, left-aligned, FONT_GAME, no wrap, scale 1.0) */
extern aTextStyle_t a_default_text_style;

/**
 * @brief Check if a glyph exists in the font atlas
 *
 * @param font_type Font type to check (FONT_ENTER_COMMAND, FONT_GAME, etc.)
 * @param codepoint Unicode codepoint to check
 * @return 1 if glyph exists, 0 otherwise
 */
int a_GlyphExists(int font_type, unsigned int codepoint);

/**
 * @brief Get glyph index, returning fallback if glyph doesn't exist
 *
 * If the requested codepoint doesn't exist in the font atlas, returns
 * the fallback glyph (typically '-') instead. Logs a warning once per
 * missing glyph type.
 *
 * @param font_type Font type to use
 * @param codepoint Unicode codepoint to look up
 * @return Glyph index to use (either requested or fallback)
 */
int a_GetGlyphOrFallback(int font_type, unsigned int codepoint);

/*
---------------------------------------------------------------
---                       Textures                          ---
---------------------------------------------------------------
*/

SDL_Texture* a_TextureLoad( const char* filename );
SDL_Texture* a_SurfaceToTexture( SDL_Surface* surf, int destroy );
void a_TexturesInit( void );

/*
---------------------------------------------------------------
---                        Widgets                          ---
---------------------------------------------------------------
*/

enum
{
  WT_UNKNOWN,
  WT_BUTTON,
  WT_SELECT,
  WT_SLIDER,
  WT_INPUT,
  WT_CONTROL,
  WT_CONTAINER,
};

enum
{
  WI_BACKGROUND = 0,
  WI_PRESSED,
  WI_HOVERING,
  WI_DISABLED,
};

/**
 * @brief Draws all visible widgets.
 *
 * This function iterates through the linked list of widgets, starting from `widget_head.next`,
 * and calls the appropriate drawing function for each widget based on its type.
 * Hidden widgets are skipped as they are not drawn.
 */
void a_DrawWidgets( void );

/**
 * @brief Handles the logic and interactions for all active widgets.
 *
 * This function is the main update loop for the widget system. It processes mouse clicks,
 * keyboard inputs (left, right, space, return), and manages the active widget state.
 * It also handles transitions to specific input or control modes if an appropriate
 * widget is activated.
 *
 * It checks for mouse clicks to activate widgets based on their coordinates.
 * For container widgets, it iterates through their components to find the clicked one.
 * Keyboard inputs are handled for navigating widgets (though up/down are commented out)
 * and triggering actions or entering specific widget interaction modes (input/control).
 */
void a_DoWidget( void );

/**
 * @brief Retrieves a widget by its name.
 *
 * This function searches through the linked list of widgets (starting from `widget_head.next`)
 * to find a widget with a matching name. If a widget with the given name is found,
 * a pointer to that widget is returned. If no widget is found, an SDL warning
 * message is logged, and `NULL` is returned.
 *
 * @param name The string name of the widget to retrieve.
 * @return A pointer to the `aWidget_t` if found, otherwise `NULL`.
 */
aWidget_t* a_GetWidget( const char* name );

/**
 * @brief Retrieves a container widget by its name and casts it to `aContainerWidget_t`.
 *
 * This function first calls `a_GetWidget` to locate a widget by the given name.
 * If the widget is found, it attempts to cast its `data` member to an `aContainerWidget_t*`.
 * Error messages are printed to stdout if the widget is not found or if the cast results in NULL.
 *
 * @param name The string name of the container widget to retrieve.
 * @return A pointer to the `aContainerWidget_t` if successful, otherwise `NULL`.
 */
aContainerWidget_t* a_GetContainerFromWidget( const char* name );

/**
 * @brief Initializes the widget system from a configuration file.
 *
 * This function sets up the initial state of the widget system.
 * It clears the widget head, sets the tail, loads widgets from the specified
 * filename.
 *
 * @param filename The path to the file containing widget configuration data.
 */
void a_WidgetsInit( const char* filename );
int a_WidgetCacheFree( void );
aWidget_t a_WidgetGetHeadWidget( void );

/*
---------------------------------------------------------------
---                      Widget Parser                      ---
---------------------------------------------------------------
*/

aAUF_t* a_AUFParser( const char* filename );
int a_AUFSaveWidgets( const char* filename );
int a_FreeLine( char** line, const int nl_count );

aAUF_t* a_AUFCreation( void );
aAUFNode_t* a_AUFNodeCreation( void );
int a_AUFAddNode( aAUF_t* root, aAUFNode_t* node );
int a_AUFNodeAddNode( aAUFNode_t* root, aAUFNode_t* node );
int a_AUFNodeAddChild( aAUFNode_t* root, aAUFNode_t* node );
int a_AUFNodeFree( aAUFNode_t* head );
int a_AUFFree( aAUF_t* root );
aAUFNode_t* a_AUFGetObjectItem( aAUFNode_t* node, char* object_str );
void a_PrintAUFTree( aAUFNode_t* node, int depth );

/*
---------------------------------------------------------------
---                         Colors                          ---
---------------------------------------------------------------
*/

extern aColor_t black;
extern aColor_t blue;
extern aColor_t green;
extern aColor_t cyan;
extern aColor_t red;
extern aColor_t magenta;
extern aColor_t yellow;
extern aColor_t white;
extern aColor_t shit0;
extern aColor_t shit1;
extern aColor_t shit2;
extern aColor_t shit3;

extern aColor_t gray9;
extern aColor_t gray8;
extern aColor_t gray7;
extern aColor_t gray6;
extern aColor_t gray5;
extern aColor_t gray4;
extern aColor_t gray3;
extern aColor_t gray2;
extern aColor_t gray1;
extern aColor_t gray0;

/*
---------------------------------------------------------------
---                         Errors                          ---
---------------------------------------------------------------
*/

typedef enum
{
  NORMAL,
  WARNING,
  FATAL,
  INFO,
  DEBUG,
  TRACE,
} aLogLevel_t;

extern char* log_level_strings[LOG_LEVEL_COUNT];

/*
---------------------------------------------------------------
---                         Utils                           ---
---------------------------------------------------------------
*/

char* a_STR_NDUP( const char* str, size_t len );
char* a_ParseString( char delimiter, char* str, int str_len );
char* a_ReadFile( const char* filename, int* file_size );
char* a_ParseStringDoubleDelim( char delimiter1, char delimiter2, char* str,
                                int str_len );
int a_CountNewLines( const char* file_string, const int file_size );
char** a_ParseLinesInFile( const char* file_string, const int file_size,
                           const int nl_count );

/*
---------------------------------------------------------------
---                      Viewport                           ---
---------------------------------------------------------------
*/


aPoint2f_t a_ViewportCalculateScale( void );
uint8_t a_ViewportIsRectVisible( aRectf_t rect );
uint8_t a_ViewportIsPointVisible( aPoint2f_t point );

void a_ViewportDrawPoint( aPoint3f_t p, aColor_t color );
void a_ViewportDrawRect( aRectf_t rect, aColor_t color );

/*
---------------------------------------------------------------
---                         Defines                         ---
---------------------------------------------------------------
*/

aTimer_t* a_TimerCreate( void );
void a_TimerFree( aTimer_t* timer );
void a_TimerStart( aTimer_t* timer );
void a_TimerStop( aTimer_t* timer );
void a_TimerPause( aTimer_t* timer );
void a_TimerUnpause( aTimer_t* timer );
uint32_t a_TimerGetTicks( aTimer_t* timer );
uint8_t a_TimerStarted( aTimer_t* timer );
uint8_t a_TimerPaused( aTimer_t* timer );
int a_TimerOneshot( aTimer_t* timer, const uint32_t wait_ms );

/*
---------------------------------------------------------------
---                     Input Enums                         ---
---------------------------------------------------------------
*/

enum
{
  //From USB HID Usage Tables
  //Keyboard/Keypad Page (0x07) and SDL2
  A_UNKOWN = 0,
  A_A = SDL_SCANCODE_A,
  A_B = 5,
  A_C = 6,
  A_D = 7,
  A_E = 8,
  A_F = 9,
  A_G = 10,
  A_H = 11,
  A_I = 12,
  A_J = 13,
  A_K = 14,
  A_L = 15,
  A_M = 16,
  A_N = 17,
  A_O = 18,
  A_P = 19,
  A_Q = 20,
  A_R = 21,
  A_S = 22,
  A_T = 23,
  A_U = 24,
  A_V = 25,
  A_W = 26,
  A_Y = 27,
  A_X = 28,
  A_Z = 29,
  A_1 = 30,
  A_2 = 31,
  A_3 = 32,
  A_4 = 33,
  A_5 = 34,
  A_6 = 35,
  A_7 = 36,
  A_8 = 37,
  A_9 = 38,
  A_0 = 39,
  A_RETURN = 40,
  A_ESCAPE = 41,
  A_DELETE = 42,
  A_TAB = 43,
  A_SPACEBAR = 44,
  A_MINUS = 45,
  A_EQUALS = 46,
  A_LEFTBRACKET = 47,
  A_RIGHTBRACKET = 48,
  A_BACKSLASH = 49,
  A_NONUSHASH = 50,
  A_SEMICOLON = 51,
  A_APOSTROPHE = 52,
  A_GRAVE = 53,
  A_COMMA = 54,
  A_PERIOD = 55,
  A_FORWARDSLASH = 56,
  A_CAPSLOCK = 57,
  A_F1 = 58,
  A_F2 = 59,
  A_F3 = 60,
  A_F4 = 61,
  A_F5 = 62,
  A_F6 = 63,
  A_F7 = 64,
  A_F8 = 65,
  A_F9 = 66,
  A_F10 = 67,
  A_F11 = 68,
  A_F12 = 69,
  A_PRINTSCREEN = 70,
  A_SCROLLLOCK = 71,
  A_PAUSE = 72,
  A_INSERT = 73,
  A_HOME = 74,
  A_PAGEUP = 75,
  A_DELETEFORWARD = 76,
  A_END = 77,
  A_PAGEDOWN = 78,
  A_RIGHT = 79,
  A_LEFT = 80,
  A_DOWN = 81,
  A_UP = 82,
  A_KP_NUMLOCK = 83,
  A_KP_DIVIDE = 84,
  A_KP_MULTIPLY = 85,
  A_KP_MINUS = 86,
  A_KP_PLUS = 87,
  A_KP_ENTER = 88,
  A_KP_1 = 89,
  A_KP_2 = 90,
  A_KP_3 = 91,
  A_KP_4 = 92,
  A_KP_5 = 93,
  A_KP_6 = 94,
  A_KP_7 = 95,
  A_KP_8 = 96,
  A_KP_9 = 97,
  A_KP_0 = 98,
  A_KP_PERIOD = 99,
  A_NONUSBACKSLASH = 100,
  A_APPLICATION = 101,
  A_POWER = 102,
  A_KP_EQUALS = 103,
  A_F13 = 104,
  A_F14 = 105,
  A_F15 = 106,
  A_F16 = 107,
  A_F17 = 108,
  A_F18 = 109,
  A_F19 = 110,
  A_F20 = 111,
  A_F21 = 112,
  A_F22 = 113,
  A_F23 = 114,
  A_F24 = 115,
  A_EXECUTE = 116,
  A_HELP = 117,
  A_MENU = 118,
  A_SELECT = 119,
  A_STOP = 120,
  A_AGAIN = 121,
  A_UNDO = 122,
  A_CUT = 123,
  A_COPY = 124,
  A_PASTE = 125,
  A_FIND = 126,
  A_MUTE = 127,
  A_VOLUMEUP = 128,
  A_VOLUMEDOWN = 129,
  A_LOCKINGCAPSLOCK = 130,
  A_LOCKINGNUMLOCK = 131,
  A_LOCKINGSCROLLLOCK = 132,
  A_KP_COMMA = 133,
  A_KP_EQUALSSIGN = 134,
  A_INTERNATIONAL1 = 135,
  A_INTERNATIONAL2 = 136,
  A_INTERNATIONAL3 = 137,
  A_INTERNATIONAL4 = 138,
  A_INTERNATIONAL5 = 139,
  A_INTERNATIONAL6 = 140,
  A_INTERNATIONAL7 = 141,
  A_INTERNATIONAL8 = 142,
  A_INTERNATIONAL9 = 143,
  A_LANG1 = 144,
  A_LANG2 = 145,
  A_LANG3 = 146,
  A_LANG4 = 147,
  A_LANG5 = 148,
  A_LANG6 = 149,
  A_LANG7 = 150,
  A_LANG8 = 151,
  A_LANG9 = 152,
  A_ALTERASE = 153,
  A_SYSREQ = 154,
  A_CANCEL = 155,
  A_CLEAR = 156,
  A_PRIOR = 157,
  A_RETURN2 = 158,
  A_SEPARATOR = 159,
  A_OUT = 160,
  A_OPER = 161,
  A_CLEARAGAIN = 162,
  A_CRSEL = 163,
  A_EXSEL = 164,
  A_KP_00 = 176,
  A_KP_000 = 177,
  A_THOUSANDSSPEARATOR = 178,
  A_DECIMALSPEARATOR = 179,
  A_CURRENCYUNIT = 180,
  A_CURRENCYSUBUNIT = 181,
  A_KP_LEFTPAREN = 182,
  A_KP_RIGHTPAREN = 183,
  A_KP_LEFTBRACE = 184,
  A_KP_RIGHTBRACE = 185,
  A_KP_TAB = 186,
  A_KP_BACKSPACE = 187,
  A_KP_A = 188,
  A_KP_B = 189,
  A_KP_C = 190,
  A_KP_D = 191,
  A_KP_E = 192,
  A_KP_F = 193,
  A_KP_XOR = 194,
  A_KP_POWER = 195,
  A_KP_PRECENT = 196,
  A_KP_LESSTHAN = 197,
  A_KP_GREATERTHAN = 198,
  A_KP_AMPERSAND = 199,
  A_KP_DBLAPERSAND = 200,
  A_KP_VERTBAR = 201,
  A_KP_DBLVERTBAR = 202,
  A_KP_COLON = 203,
  A_KP_HASH = 204,
  A_KP_SPACE = 205,
  A_KP_AT = 206,
  A_KP_EXCLAM = 207,
  A_KP_MEMSTORE = 208,
  A_KP_MEMRECALL = 209,
  A_KP_MEMCLEAR = 210,
  A_KP_MEMADD = 211,
  A_KP_MEMSUB = 212,
  A_KP_MEMMULTIPLY = 213,
  A_KP_MEMDIVIDE = 214,
  A_KP_PLUSMINUS = 215,
  A_KP_CLEAR = 216,
  A_KP_CLEARENTRY = 217,
  A_KP_BINARY = 218,
  A_KP_OCTAL = 219,
  A_KP_DECIMAL = 220,
  A_KP_HEXADECIMAL = 221,
  A_LCTRL = 224,
  A_LSHIFT = 225,
  A_LALT = 226,
  A_LGUI = 227,
  A_RCTRL = 228,
  A_RSHIFT = 229,
  A_RALT = 230,
  A_RGUI = 231,
  A_KEYS = 512
};

/*
---------------------------------------------------------------
---                         Layout                          ---
---------------------------------------------------------------
*/

/**
 * FlexBox Layout System
 * CSS-inspired automatic layout engine for UI positioning
 *
 * The FlexBox system provides automatic positioning of UI elements using
 * CSS flexbox-like rules. Items can be arranged horizontally or vertically
 * with various alignment and spacing options.
 *
 */

/**
 * @brief Layout direction for FlexBox containers
 */
typedef enum {
    FLEX_DIR_ROW,      /**< Horizontal layout (left to right) */
    FLEX_DIR_COLUMN    /**< Vertical layout (top to bottom) */
} FlexDirection_t;

/**
 * @brief Main-axis justification modes
 */
typedef enum {
    FLEX_JUSTIFY_START,          /**< Align to start (left/top) */
    FLEX_JUSTIFY_CENTER,         /**< Center items */
    FLEX_JUSTIFY_END,            /**< Align to end (right/bottom) */
    FLEX_JUSTIFY_SPACE_BETWEEN,  /**< Equal spacing, edges aligned */
    FLEX_JUSTIFY_SPACE_AROUND    /**< Equal spacing including edges */
} FlexJustify_t;

/**
 * @brief Cross-axis alignment modes
 */
typedef enum {
    FLEX_ALIGN_START,   /**< Cross-axis start */
    FLEX_ALIGN_CENTER,  /**< Cross-axis center */
    FLEX_ALIGN_END      /**< Cross-axis end */
} FlexAlign_t;

/**
 * @brief Individual item in a FlexBox container
 */
typedef struct {
    int w, h;                /**< Item dimensions */
    int calc_x, calc_y;      /**< Calculated position (set by a_FlexLayout) */
    void* user_data;         /**< Custom data pointer (not managed by FlexBox) */
} FlexItem_t;

/**
 * @brief FlexBox container for automatic layout
 */
typedef struct {
    int x, y;                    /**< Container position */
    int w, h;                    /**< Container dimensions */
    FlexDirection_t direction;   /**< Layout direction */
    FlexJustify_t justify;       /**< Main-axis alignment */
    FlexAlign_t align;           /**< Cross-axis alignment */
    int gap;                     /**< Spacing between items */
    int padding;                 /**< Internal padding */
    void* children;              /**< Internal array of FlexItem_t */
    int dirty;                   /**< Layout needs recalculation */
} FlexBox_t;

/**
 * @brief Create a new FlexBox layout container
 *
 * Creates a FlexBox container at the specified position and dimensions.
 * The container will automatically position child items based on flexbox
 * layout rules (direction, justify, align).
 *
 * @param x Top-left X coordinate of the container
 * @param y Top-left Y coordinate of the container
 * @param w Width of the container in pixels
 * @param h Height of the container in pixels
 * @return Pointer to newly created FlexBox_t, or NULL on allocation failure
 *
 * @note Default direction: FLEX_DIR_ROW (horizontal)
 * @note Default justify: FLEX_JUSTIFY_START (left/top aligned)
 * @note Default align: FLEX_ALIGN_START (cross-axis start)
 * @note Must call a_DestroyFlexBox() when done to free memory
 * @see a_DestroyFlexBox()
 */
FlexBox_t* a_FlexBoxCreate(int x, int y, int w, int h);

/**
 * @brief Destroy a FlexBox and free all memory
 *
 * Destroys the FlexBox container and frees all internal allocations.
 * The pointer is set to NULL after destruction.
 *
 * @param box Pointer to FlexBox pointer (will be set to NULL)
 *
 * @note Safe to call with NULL pointer
 * @note Does NOT free user_data pointers stored in items
 * @see a_CreateFlexBox()
 */
void a_FlexBoxDestroy(FlexBox_t** box);

/**
 * @brief Set the layout direction (row or column)
 *
 * Controls whether items are arranged horizontally (ROW) or vertically (COLUMN).
 * Marks the layout as dirty, requiring recalculation on next a_FlexLayout() call.
 *
 * @param box FlexBox container to configure
 * @param direction FLEX_DIR_ROW (horizontal) or FLEX_DIR_COLUMN (vertical)
 *
 * @note ROW: items flow left-to-right
 * @note COLUMN: items flow top-to-bottom
 * @see FlexDirection_t
 */
void a_FlexSetDirection(FlexBox_t* box, FlexDirection_t direction);

/**
 * @brief Set main-axis justification (how items are spaced)
 *
 * Controls how items are distributed along the main axis (horizontal for ROW,
 * vertical for COLUMN).
 *
 * @param box FlexBox container to configure
 * @param justify Justification mode (START/CENTER/END/SPACE_BETWEEN/SPACE_AROUND)
 *
 * @note START: items align to left/top
 * @note CENTER: items centered
 * @note END: items align to right/bottom
 * @note SPACE_BETWEEN: equal spacing, edges aligned to container
 * @note SPACE_AROUND: equal spacing including edges
 * @see FlexJustify_t
 */
void a_FlexSetJustify(FlexBox_t* box, FlexJustify_t justify);

/**
 * @brief Set cross-axis alignment
 *
 * Controls how items are aligned perpendicular to the main axis.
 * For ROW: controls vertical alignment. For COLUMN: controls horizontal alignment.
 *
 * @param box FlexBox container to configure
 * @param align Alignment mode (START/CENTER/END)
 *
 * @note START: align to top (ROW) or left (COLUMN)
 * @note CENTER: center items on cross-axis
 * @note END: align to bottom (ROW) or right (COLUMN)
 * @see FlexAlign_t
 */
void a_FlexSetAlign(FlexBox_t* box, FlexAlign_t align);

/**
 * @brief Set spacing between items
 *
 * Sets the gap (in pixels) between adjacent items in the layout.
 *
 * @param box FlexBox container to configure
 * @param gap Spacing between items in pixels (0 for no gap)
 *
 * @note Gap is applied between items, not at edges
 * @note Negative values are allowed but not recommended
 */
void a_FlexSetGap(FlexBox_t* box, int gap);

/**
 * @brief Configure FlexBox direction, justification, and gap in one call
 *
 * Convenience function to set the three most common FlexBox properties.
 * Equivalent to calling a_FlexSetDirection(), a_FlexSetJustify(), and
 * a_FlexSetGap() in sequence.
 *
 * @param box FlexBox container to configure
 * @param direction Layout direction (FLEX_DIR_ROW or FLEX_DIR_COLUMN)
 * @param justify Main-axis justification (START/CENTER/END/SPACE_BETWEEN/SPACE_AROUND)
 * @param gap Spacing between items in pixels
 *
 * @note This is a convenience wrapper - individual set functions still work
 * @note Reduces FlexBox configuration from 3 calls to 1
 * @see a_FlexSetDirection()
 * @see a_FlexSetJustify()
 * @see a_FlexSetGap()
 */
void a_FlexConfigure(FlexBox_t* box, FlexDirection_t direction, FlexJustify_t justify, int gap);

/**
 * @brief Set internal padding of the container
 *
 * Sets uniform padding on all sides of the container's inner area.
 * Items are positioned within the padded area.
 *
 * @param box FlexBox container to configure
 * @param padding Padding in pixels (applied to all sides)
 *
 * @note Reduces available space for items by 2*padding on each axis
 * @note Padding is included in container bounds visualization
 */
void a_FlexSetPadding(FlexBox_t* box, int padding);

/**
 * @brief Update the container's position and dimensions
 *
 * Changes the FlexBox container's absolute position and size.
 * Marks layout as dirty, requiring recalculation.
 *
 * @param box FlexBox container to update
 * @param x New top-left X coordinate
 * @param y New top-left Y coordinate
 * @param w New width in pixels
 * @param h New height in pixels
 *
 * @note Use this to reposition containers dynamically (e.g., from parent FlexBox)
 * @note Common use: `a_FlexSetBounds(row, 0, parent_y + 10, SCREEN_WIDTH, 50)`
 */
void a_FlexSetBounds(FlexBox_t* box, int x, int y, int w, int h);

/**
 * @brief Add an item to the FlexBox
 *
 * Adds a new item with specified dimensions to the container.
 * Position is calculated automatically by a_FlexLayout().
 *
 * @param box FlexBox container
 * @param w Item width in pixels
 * @param h Item height in pixels
 * @param user_data Optional custom data pointer (can be NULL)
 * @return Index of added item, or -1 on failure
 *
 * @note user_data is NOT freed by FlexBox - manage externally
 * @note Item position (calc_x, calc_y) is set by a_FlexLayout()
 * @see a_FlexLayout()
 * @see a_FlexGetItemX()
 * @see a_FlexGetItemY()
 */
int a_FlexAddItem(FlexBox_t* box, int w, int h, void* user_data);

/**
 * @brief Remove an item from the FlexBox
 *
 * Marks an item as removed by setting its dimensions to zero.
 * Does not actually free memory (simple array implementation limitation).
 *
 * @param box FlexBox container
 * @param index Index of item to remove
 *
 * @note Item is marked as 0×0 size, not truly removed
 * @note For full cleanup, use a_FlexClearItems()
 */
void a_FlexRemoveItem(FlexBox_t* box, int index);

/**
 * @brief Clear all items from the FlexBox
 *
 * Removes all items and frees the internal item array.
 * A new empty array is allocated.
 *
 * @param box FlexBox container
 *
 * @note Does NOT free user_data pointers - manage externally
 */
void a_FlexClearItems(FlexBox_t* box);

/**
 * @brief Get the number of items in the FlexBox
 *
 * @param box FlexBox container
 * @return Number of items, or 0 if box is NULL
 */
int a_FlexGetItemCount(const FlexBox_t* box);

/**
 * @brief Get a specific item from the FlexBox
 *
 * Returns a const pointer to the item's data.
 * Access calc_x and calc_y for the item's calculated position.
 *
 * @param box FlexBox container
 * @param index Index of item to retrieve
 * @return Const pointer to FlexItem_t, or NULL if invalid index
 *
 * @note Use a_FlexGetItemX() / a_FlexGetItemY() for convenience
 * @see FlexItem_t
 */
const FlexItem_t* a_FlexGetItem(const FlexBox_t* box, int index);

/**
 * @brief Update an item's dimensions
 *
 * Changes the width and height of an existing item.
 * Marks layout as dirty for recalculation.
 *
 * @param box FlexBox container
 * @param index Index of item to update
 * @param w New width in pixels
 * @param h New height in pixels
 */
void a_FlexUpdateItem(FlexBox_t* box, int index, int w, int h);

/**
 * @brief Calculate and apply FlexBox layout
 *
 * Recalculates item positions based on container settings.
 * Updates calc_x and calc_y for all items.
 *
 * @param box FlexBox container
 * @return 0 on success, -1 on error
 *
 * @note Only recalculates if layout is dirty
 * @note Sets dirty flag to 0 after calculation
 * @note Call this before reading item positions with a_FlexGetItemX/Y()
 */
int a_FlexLayout(FlexBox_t* box);

/**
 * @brief Check if layout needs recalculation
 *
 * @param box FlexBox container
 * @return 1 if layout is dirty (needs recalc), 0 otherwise
 */
int a_FlexIsDirty(const FlexBox_t* box);

/**
 * @brief Get the calculated X position of an item
 *
 * Returns the item's final X coordinate after layout calculation.
 *
 * @param box FlexBox container
 * @param index Index of item
 * @return Calculated X position in pixels, or 0 if invalid
 *
 * @note Call a_FlexLayout() first to ensure position is current
 * @see a_FlexLayout()
 */
int a_FlexGetItemX(const FlexBox_t* box, int index);

/**
 * @brief Get the calculated Y position of an item
 *
 * Returns the item's final Y coordinate after layout calculation.
 *
 * @param box FlexBox container
 * @param index Index of item
 * @return Calculated Y position in pixels, or 0 if invalid
 *
 * @note Call a_FlexLayout() first to ensure position is current
 * @see a_FlexLayout()
 */
int a_FlexGetItemY(const FlexBox_t* box, int index);

/**
 * @brief Render debug visualization of the FlexBox
 *
 * Draws container bounds (white), padding area (yellow), and
 * item rectangles (cyan) for debugging layout issues.
 *
 * @param box FlexBox container
 *
 * @note For debugging only - renders directly to screen
 * @note Container: white outline
 * @note Padding area: yellow outline
 * @note Items: cyan outlines
 */
void a_FlexDebugRender(const FlexBox_t* box);

/*
---------------------------------------------------------------
---                       Animations                        ---
---------------------------------------------------------------
*/

aAnimation_t* a_AnimationCreate( const char* filename, const float w,
                                 const float h, const int frame_count,
                                 const uint32_t frame_duration );
void a_AnimationFree( aAnimation_t* animation );
void a_AnimationPlay( aPoint2f_t pos, aAnimation_t* animtion );

#endif

