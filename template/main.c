#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "Archimedes.h"
#include "test_text.h"
#include "player_actions.h"
#include "audio_test.h"
#include "enemy.h"
#include "hotbar.h"
#include "weapons.h"
#include "drops.h"

// Scene system
typedef enum {
  SCENE_GAME,
  SCENE_TEST_TEXT,
  SCENE_AUDIO_TEST,
  SCENE_GAME_OVER
} Scene_t;

static Scene_t current_scene = SCENE_GAME;

// Scene function declarations
static void scene_game_logic( float dt );
static void scene_game_draw( float dt );
static void scene_game_over_logic( float dt );
static void scene_game_over_draw( float dt );
static void game_reset( void );

static void aDoLoop( float );
static void aRenderLoop( float );

// Game over UI
static FlexBox_t* gameover_flex = NULL;
#define GAMEOVER_BTN_W 160
#define GAMEOVER_BTN_H 40
#define GAMEOVER_BTN_IDX_RETRY 0
#define GAMEOVER_BTN_IDX_QUIT  1

aImage_t* bullet_img;

// Hit sounds (exported for enemy.c)
#define HIT_SOUND_COUNT 5
aSoundEffect_t hit_sounds[HIT_SOUND_COUNT];
int hit_sounds_loaded = 0;

// Weather sound
static aSoundEffect_t rain_sound;
static int rain_loaded = 0;

// Death sound (exported for enemy.c)
aSoundEffect_t die_sound;
int die_loaded = 0;

// Timer system (countdown from 15:00)
static float time_remaining = 15.0f * 60.0f; // 15 minutes in seconds

// Enemy spawning
#define MAX_ENEMIES 50
#define MAX_BLOOD_PARTICLES 500
static float spawn_timer = 0.0f;
static float game_elapsed_timer = 0.0f;

// Difficulty scaling helpers
static float get_spawn_interval( void )
{
  float t = game_elapsed_timer;
  if ( t > 300.0f ) t = 300.0f;
  return 2.0f - (1.6f * (t / 300.0f));  // 2.0s -> 0.4s over 5 minutes
}

static float get_speed_mult( void )
{
  float t = game_elapsed_timer;
  if ( t > 600.0f ) t = 600.0f;
  return 1.0f + 0.5f * (t / 600.0f);  // 1.0x -> 1.5x over 10 minutes
}

static int get_hp_bonus( void )
{
  float t = game_elapsed_timer;
  if ( t > 900.0f ) t = 900.0f;
  return (int)(5.0f * (t / 900.0f));  // 0 -> +5 extra hits over 15 minutes
}

static int get_max_active_enemies( void )
{
  float t = game_elapsed_timer;
  if ( t > 300.0f ) t = 300.0f;
  return 15 + (int)(35.0f * (t / 300.0f));  // 15 -> 50 over 5 minutes
}

static EnemyType_t pick_enemy_type( void )
{
  float t = game_elapsed_timer;
  float grunt_w = 1.0f;
  float dasher_w = (t > 30.0f) ? fminf((t - 30.0f) / 60.0f, 0.4f) : 0.0f;
  float brute_w = (t > 90.0f) ? fminf((t - 90.0f) / 120.0f, 0.25f) : 0.0f;

  float total = grunt_w + dasher_w + brute_w;
  float roll = RANDF(0, total);

  if ( roll < brute_w ) return ENEMY_TYPE_BRUTE;
  if ( roll < brute_w + dasher_w ) return ENEMY_TYPE_DASHER;
  return ENEMY_TYPE_GRUNT;
}

// Health drop spawning
#define HEALTH_DROP_INTERVAL 10.0f
static float health_drop_timer = 0.0f;

// Timed weapon drops
#define WEAPON_DROP_TIME   20.0f
#define WEAPON_DROP_TIME_2 40.0f
#define WEAPON_DROP_TIME_3 60.0f
static int timed_weapon_dropped = 0;
static int timed_weapon_dropped_2 = 0;
static int timed_weapon_dropped_3 = 0;

void aInitGame( void )
{
  app.delegate.logic = aDoLoop;
  app.delegate.draw  = aRenderLoop;

  // Set background color (dark blue)
  app.background = (aColor_t){20, 20, 60, 255};

  bullet_img = a_ImageLoad( "resources/assets/bullet.png" );
  if ( bullet_img == NULL )
  {
    printf( "Failed to load bullet image\n" );
  }

  // Initialize player
  player_init();

  // Initialize enemy system
  enemy_init(MAX_ENEMIES, MAX_BLOOD_PARTICLES);

  // Load death sound
  if (a_AudioLoadSound("resources/soundEffects/die1.wav", &die_sound) == 0) {
    printf("Loaded die1.wav\n");
    die_loaded = 1;
  } else {
    printf("Failed to load die1.wav\n");
  }

  // Load hit sounds
  const char* hit_files[HIT_SOUND_COUNT] = {
    "resources/soundEffects/hit1.wav",
    "resources/soundEffects/hit2.wav",
    "resources/soundEffects/hit3.wav",
    "resources/soundEffects/hit4.wav",
    "resources/soundEffects/hit5.wav"
  };

  hit_sounds_loaded = 1;
  for (int i = 0; i < HIT_SOUND_COUNT; i++) {
    if (a_AudioLoadSound(hit_files[i], &hit_sounds[i]) < 0) {
      printf("Failed to load %s\n", hit_files[i]);
      hit_sounds_loaded = 0;
    }
  }
  if (hit_sounds_loaded) {
    printf("Loaded %d hit sounds\n", HIT_SOUND_COUNT);
  }

  // Load and play rain sound on weather channel
  if (a_AudioLoadSound("resources/music/rain_2.wav", &rain_sound) == 0) {
    printf("Loaded rain_2.wav\n");
    rain_loaded = 1;

    // Play on weather channel, loop forever, volume 32, interrupt mode
    aAudioOptions_t rain_opts = {
      .channel = AUDIO_CHANNEL_WEATHER,
      .volume = 16,           // 32 out of 128 (25% volume)
      .loops = -1,            // Loop forever
      .fade_ms = 0,           // No fade
      .interrupt = 1          // Interrupt any previous weather sound
    };
    a_AudioPlaySound(&rain_sound, &rain_opts);
    printf("Playing rain on channel %d at volume 32\n", AUDIO_CHANNEL_WEATHER);
  } else {
    printf("Failed to load rain_2.wav\n");
  }

  // Initialize audio test (loads calm BGM and starts playback)
  audio_test_init();

  // Initialize weapons and drops
  weapons_init();
  drops_init();

  // Initialize hotbar UI
  hotbar_init();
}

static void aDoLoop( float dt )
{
  a_DoInput();

  // Dispatch to current scene
  switch ( current_scene )
  {
    case SCENE_GAME:
      // ESC to quit from game
      if ( app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 )
      {
        app.running = 0;
      }
      scene_game_logic( dt );
      break;
    case SCENE_TEST_TEXT:
      if ( test_text_logic( dt ) )
      {
        current_scene = SCENE_GAME;
      }
      break;
    case SCENE_AUDIO_TEST:
      audio_test_update();
      if ( app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 )
      {
        current_scene = SCENE_GAME;
        app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
      }
      break;
    case SCENE_GAME_OVER:
      scene_game_over_logic( dt );
      break;
  }
}

static void scene_game_logic( float dt )
{
  // Update player movement and shooting
  player_update( dt );

  // Check for player death — switch to game over screen
  if ( !player_is_alive() )
  {
    // Create game over UI layout
    if ( gameover_flex )
    {
      a_FlexBoxDestroy( &gameover_flex );
    }
    int panel_w = 300;
    int panel_h = 200;
    gameover_flex = a_FlexBoxCreate(
      (SCREEN_WIDTH - panel_w) / 2,
      (SCREEN_HEIGHT - panel_h) / 2,
      panel_w, panel_h
    );
    a_FlexConfigure( gameover_flex, FLEX_DIR_COLUMN, FLEX_JUSTIFY_CENTER, 16 );
    a_FlexSetAlign( gameover_flex, FLEX_ALIGN_CENTER );
    a_FlexSetPadding( gameover_flex, 20 );
    a_FlexAddItem( gameover_flex, GAMEOVER_BTN_W, GAMEOVER_BTN_H, NULL ); // Try Again
    a_FlexAddItem( gameover_flex, GAMEOVER_BTN_W, GAMEOVER_BTN_H, NULL ); // Quit
    a_FlexLayout( gameover_flex );

    current_scene = SCENE_GAME_OVER;
    return;
  }

  // Update timer (countdown)
  time_remaining -= dt;
  if ( time_remaining < 0.0f )
  {
    time_remaining = 0.0f;
  }

  // Spawn enemies (dynamic difficulty)
  int current_enemy_count = enemy_get_count();
  int max_active = get_max_active_enemies();
  if ( current_enemy_count < max_active )
  {
    spawn_timer += dt;
  }

  float current_spawn_interval = get_spawn_interval();
  if ( spawn_timer >= current_spawn_interval && current_enemy_count < max_active )
  {
    spawn_timer = 0.0f;

    // Random spawn position outside screen
    int side = rand() % 4; // 0=top, 1=right, 2=bottom, 3=left
    float spawn_x, spawn_y;

    if ( side == 0 ) // Top
    {
      spawn_x = RANDF( 0, SCREEN_WIDTH );
      spawn_y = -16;
    }
    else if ( side == 1 ) // Right
    {
      spawn_x = SCREEN_WIDTH + 16;
      spawn_y = RANDF( 0, SCREEN_HEIGHT );
    }
    else if ( side == 2 ) // Bottom
    {
      spawn_x = RANDF( 0, SCREEN_WIDTH );
      spawn_y = SCREEN_HEIGHT + 16;
    }
    else // Left
    {
      spawn_x = -16;
      spawn_y = RANDF( 0, SCREEN_HEIGHT );
    }

    enemy_spawn( spawn_x, spawn_y, pick_enemy_type(), get_speed_mult(), get_hp_bonus() );
  }

  // Check bullet collisions with enemies (alive or repositioning, not corpses)
  for ( int j = 0; j < MAX_ENEMIES; j++ )
  {
    EnemyState_t state = enemy_get_state( j );
    if ( enemy_is_active( j ) && state != ENEMY_STATE_CORPSE && state != ENEMY_STATE_HIT_KNOCKBACK )
    {
      float enemy_x, enemy_y;
      enemy_get_position( j, &enemy_x, &enemy_y );
      int bullet_hit = player_check_bullet_collision( enemy_x, enemy_y, enemy_get_radius( j ) );

      if ( bullet_hit >= 0 )
      {
        // Get bullet velocity BEFORE destroying it
        float bullet_vx, bullet_vy;
        player_get_bullet_velocity( bullet_hit, &bullet_vx, &bullet_vy );

        // Apply hit with knockback
        enemy_hit( j, bullet_vx, bullet_vy );

        // Destroy the bullet
        player_destroy_bullet( bullet_hit );
      }
    }
  }

  // Update all enemies (AI, physics, blood particles)
  enemy_update( dt, player_get_x(), player_get_y(), player_get_vx(), player_get_vy() );

  // Update weapons (auto-fire cooldowns) and drops (pickup check)
  weapons_update( dt );
  drops_update( dt );

  // Spawn health drops on a timer
  health_drop_timer += dt;
  if ( health_drop_timer >= HEALTH_DROP_INTERVAL )
  {
    health_drop_timer = 0.0f;
    float hx = RANDF( 40, SCREEN_WIDTH - 40 );
    float hy = RANDF( 40, SCREEN_HEIGHT - 40 );
    drops_spawn( hx, hy, DROP_HEALTH );
  }

  // Timed weapon drop at 20 seconds — random weapon the player doesn't have
  game_elapsed_timer += dt;
  if ( !timed_weapon_dropped && game_elapsed_timer >= WEAPON_DROP_TIME )
  {
    timed_weapon_dropped = 1;
    health_drop_timer = 0.0f;  // Reset health timer so they don't overlap
    WeaponType_t pool[5];
    int pool_count = 0;
    if ( !weapons_has( WEAPON_WAND )  && !drops_has_type( WEAPON_WAND ) )  pool[pool_count++] = WEAPON_WAND;
    if ( !weapons_has( WEAPON_SPIN )  && !drops_has_type( WEAPON_SPIN ) )  pool[pool_count++] = WEAPON_SPIN;
    if ( !weapons_has( WEAPON_CHAIN ) && !drops_has_type( WEAPON_CHAIN ) ) pool[pool_count++] = WEAPON_CHAIN;
    if ( !weapons_has( WEAPON_ORBIT ) && !drops_has_type( WEAPON_ORBIT ) ) pool[pool_count++] = WEAPON_ORBIT;
    if ( !weapons_has( WEAPON_BOMB )  && !drops_has_type( WEAPON_BOMB ) )  pool[pool_count++] = WEAPON_BOMB;
    if ( pool_count > 0 )
    {
      WeaponType_t chosen = pool[rand() % pool_count];
      // Pick a random spot away from the player (at least 150px)
      float wx, wy;
      float px = player_get_x();
      float py = player_get_y();
      do {
        wx = RANDF( 40, SCREEN_WIDTH - 40 );
        wy = RANDF( 40, SCREEN_HEIGHT - 40 );
      } while ( sqrtf( (wx - px) * (wx - px) + (wy - py) * (wy - py) ) < 150.0f );
      printf( "20s weapon drop: type=%d at (%.0f, %.0f) pool_count=%d\n", chosen, wx, wy, pool_count );
      drops_spawn( wx, wy, chosen );
    }
    else
    {
      printf( "20s weapon drop: pool empty, player already has all weapons\n" );
    }
  }

  // Timed weapon drop at 40 seconds — random weapon the player doesn't have (no health)
  if ( !timed_weapon_dropped_2 && game_elapsed_timer >= WEAPON_DROP_TIME_2 )
  {
    timed_weapon_dropped_2 = 1;
    health_drop_timer = 0.0f;
    WeaponType_t pool2[5];
    int pool2_count = 0;
    if ( !weapons_has( WEAPON_WAND )  && !drops_has_type( WEAPON_WAND ) )  pool2[pool2_count++] = WEAPON_WAND;
    if ( !weapons_has( WEAPON_SPIN )  && !drops_has_type( WEAPON_SPIN ) )  pool2[pool2_count++] = WEAPON_SPIN;
    if ( !weapons_has( WEAPON_CHAIN ) && !drops_has_type( WEAPON_CHAIN ) ) pool2[pool2_count++] = WEAPON_CHAIN;
    if ( !weapons_has( WEAPON_ORBIT ) && !drops_has_type( WEAPON_ORBIT ) ) pool2[pool2_count++] = WEAPON_ORBIT;
    if ( !weapons_has( WEAPON_BOMB )  && !drops_has_type( WEAPON_BOMB ) )  pool2[pool2_count++] = WEAPON_BOMB;
    if ( pool2_count > 0 )
    {
      WeaponType_t chosen2 = pool2[rand() % pool2_count];
      float wx, wy;
      float px = player_get_x();
      float py = player_get_y();
      do {
        wx = RANDF( 40, SCREEN_WIDTH - 40 );
        wy = RANDF( 40, SCREEN_HEIGHT - 40 );
      } while ( sqrtf( (wx - px) * (wx - px) + (wy - py) * (wy - py) ) < 150.0f );
      printf( "40s weapon drop: type=%d at (%.0f, %.0f) pool_count=%d\n", chosen2, wx, wy, pool2_count );
      drops_spawn( wx, wy, chosen2 );
    }
    else
    {
      printf( "40s weapon drop: pool empty, player already has all weapons\n" );
    }
  }

  // Timed weapon drop at 60 seconds — final drop
  if ( !timed_weapon_dropped_3 && game_elapsed_timer >= WEAPON_DROP_TIME_3 )
  {
    timed_weapon_dropped_3 = 1;
    health_drop_timer = 0.0f;
    WeaponType_t pool3[5];
    int pool3_count = 0;
    if ( !weapons_has( WEAPON_WAND )  && !drops_has_type( WEAPON_WAND ) )  pool3[pool3_count++] = WEAPON_WAND;
    if ( !weapons_has( WEAPON_SPIN )  && !drops_has_type( WEAPON_SPIN ) )  pool3[pool3_count++] = WEAPON_SPIN;
    if ( !weapons_has( WEAPON_CHAIN ) && !drops_has_type( WEAPON_CHAIN ) ) pool3[pool3_count++] = WEAPON_CHAIN;
    if ( !weapons_has( WEAPON_ORBIT ) && !drops_has_type( WEAPON_ORBIT ) ) pool3[pool3_count++] = WEAPON_ORBIT;
    if ( !weapons_has( WEAPON_BOMB )  && !drops_has_type( WEAPON_BOMB ) )  pool3[pool3_count++] = WEAPON_BOMB;
    if ( pool3_count > 0 )
    {
      WeaponType_t chosen3 = pool3[rand() % pool3_count];
      float wx, wy;
      float px = player_get_x();
      float py = player_get_y();
      do {
        wx = RANDF( 40, SCREEN_WIDTH - 40 );
        wy = RANDF( 40, SCREEN_HEIGHT - 40 );
      } while ( sqrtf( (wx - px) * (wx - px) + (wy - py) * (wy - py) ) < 150.0f );
      printf( "60s weapon drop: type=%d at (%.0f, %.0f) pool_count=%d\n", chosen3, wx, wy, pool3_count );
      drops_spawn( wx, wy, chosen3 );
    }
    else
    {
      printf( "60s weapon drop: pool empty, player already has all weapons\n" );
    }
  }

  if ( app.mouse.wheel == 1 )
  {
    printf( "scroll up\n" );
    app.mouse.wheel = 0;
  }

  if ( app.mouse.wheel == -1 )
  {
    printf( "scroll down\n" );
    app.mouse.wheel = 0;
  }

  // Ctrl+T to switch to test text scene
  static int ctrl_t_pressed = 0;
  if ( (app.keyboard[ SDL_SCANCODE_LCTRL ] || app.keyboard[ SDL_SCANCODE_RCTRL ]) &&
       app.keyboard[ SDL_SCANCODE_T ] == 1 && !ctrl_t_pressed )
  {
    current_scene = SCENE_TEST_TEXT;
    ctrl_t_pressed = 1;
    app.keyboard[ SDL_SCANCODE_T ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_T ] == 0 )
  {
    ctrl_t_pressed = 0;
  }

  // Ctrl+M to switch to audio test scene
  static int ctrl_m_pressed = 0;
  if ( (app.keyboard[ SDL_SCANCODE_LCTRL ] || app.keyboard[ SDL_SCANCODE_RCTRL ]) &&
       app.keyboard[ SDL_SCANCODE_M ] == 1 && !ctrl_m_pressed )
  {
    current_scene = SCENE_AUDIO_TEST;
    ctrl_m_pressed = 1;
    app.keyboard[ SDL_SCANCODE_M ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_M ] == 0 )
  {
    ctrl_m_pressed = 0;
  }
}

static void aRenderLoop( float dt )
{
  // Dispatch to current scene
  switch ( current_scene )
  {
    case SCENE_GAME:
      scene_game_draw( dt );
      break;
    case SCENE_TEST_TEXT:
      test_text_draw( dt );
      break;
    case SCENE_AUDIO_TEST:
      audio_test_draw();
      break;
    case SCENE_GAME_OVER:
      scene_game_over_draw( dt );
      break;
  }

  // Draw hotbar and screen flash (game scene only)
  if ( current_scene == SCENE_GAME )
  {
    hotbar_draw();
    player_draw_screen_flash();
  }
}

static void scene_game_draw( float dt )
{
  (void)dt;

  // Draw countdown timer
  int minutes = (int)(time_remaining / 60.0f);
  int seconds = (int)time_remaining % 60;
  char timer_text[32];
  snprintf( timer_text, sizeof(timer_text), "%02d:%02d", minutes, seconds );

  aTextStyle_t timer_config = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .wrap_width = 0,
    .scale = 0.8f
  };
  a_DrawText( timer_text, SCREEN_WIDTH / 2, 25, timer_config );

  // Draw enemy count and spawn timer (top right)
  int current_enemy_count = enemy_get_count();
  char enemy_count_text[32];
  snprintf( enemy_count_text, sizeof(enemy_count_text), "Enemies: %d/%d", current_enemy_count, MAX_ENEMIES );

  aTextStyle_t enemy_count_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_RIGHT,
    .wrap_width = 0,
    .scale = 0.6f
  };
  a_DrawText( enemy_count_text, SCREEN_WIDTH - 20, 15, enemy_count_style );

  // Draw spawn timer (pauses at 0:00 if at max capacity)
  char spawn_timer_text[32];
  if ( current_enemy_count >= get_max_active_enemies() )
  {
    snprintf( spawn_timer_text, sizeof(spawn_timer_text), "Next spawn: --" );
  }
  else
  {
    float time_until_spawn = get_spawn_interval() - spawn_timer;
    if ( time_until_spawn < 0.0f ) time_until_spawn = 0.0f;
    int spawn_seconds = (int)time_until_spawn;
    int spawn_hundredths = (int)((time_until_spawn - spawn_seconds) * 100);
    snprintf( spawn_timer_text, sizeof(spawn_timer_text), "Next spawn: %d:%02d", spawn_seconds, spawn_hundredths );
  }

  a_DrawText( spawn_timer_text, SCREEN_WIDTH - 20, 40, enemy_count_style );

  // Draw player health bar (top center)
  {
    int bar_w = 200;
    int bar_h = 12;
    int bar_x = (SCREEN_WIDTH - bar_w) / 2;
    int bar_y = 8;

    float hp_pct = (float)player_get_hp() / (float)player_get_max_hp();
    int fill_w = (int)(bar_w * hp_pct);

    // Background
    a_DrawFilledRect(
      (aRectf_t){(float)bar_x, (float)bar_y, (float)bar_w, (float)bar_h},
      (aColor_t){40, 40, 40, 200}
    );

    // Red health fill
    if ( fill_w > 0 )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)bar_x, (float)bar_y, (float)fill_w, (float)bar_h},
        (aColor_t){200, 30, 30, 255}
      );
    }

    // Heal pulse — green glow overlay on the health fill
    float heal_pct = player_get_heal_flash_progress();
    if ( heal_pct > 0.0f && fill_w > 0 )
    {
      int heal_alpha = (int)(180.0f * heal_pct);
      // Expand the bar slightly for a "pulse" effect
      int pulse_pad = (int)(3.0f * heal_pct);
      a_DrawFilledRect(
        (aRectf_t){(float)(bar_x - pulse_pad), (float)(bar_y - pulse_pad),
                   (float)(fill_w + pulse_pad * 2), (float)(bar_h + pulse_pad * 2)},
        (aColor_t){40, 255, 40, (uint8_t)heal_alpha}
      );
    }

    // Border — green during heal, white otherwise
    aColor_t border_color = (heal_pct > 0.0f)
      ? (aColor_t){40, 255, 40, (uint8_t)(150 + (int)(105.0f * heal_pct))}
      : (aColor_t){255, 255, 255, 150};
    a_DrawRect(
      (aRectf_t){(float)bar_x, (float)bar_y, (float)bar_w, (float)bar_h},
      border_color
    );

    // HP text
    char hp_text[16];
    snprintf( hp_text, sizeof(hp_text), "%d/%d", player_get_hp(), player_get_max_hp() );
    aColor_t hp_text_color = (heal_pct > 0.0f)
      ? (aColor_t){40, 255, 40, 255}
      : (aColor_t){255, 255, 255, 255};
    aTextStyle_t hp_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hp_text_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.4f
    };
    a_DrawText( hp_text, bar_x + bar_w / 2, bar_y - 1, hp_style );
  }

  // Draw enemies (includes blood particles)
  enemy_draw();

  // Draw drops on ground and weapon effects
  drops_draw();
  weapons_draw();

  // Draw player and bullets
  player_draw( bullet_img );

  // Draw dash indicator (bottom left)
  {
    int dash_size = 44;
    int dash_x = 16;
    int dash_y = SCREEN_HEIGHT - dash_size - 16;
    float dash_progress = player_get_dash_cooldown_progress();
    int dash_ready = (dash_progress >= 1.0f);

    aRectf_t dash_rect = {(float)dash_x, (float)dash_y, (float)dash_size, (float)dash_size};

    // Background
    aColor_t dash_bg = dash_ready ? (aColor_t){40, 80, 120, 220} : (aColor_t){40, 40, 40, 180};
    a_DrawFilledRect(dash_rect, dash_bg);
    aColor_t dash_border = dash_ready ? (aColor_t){100, 200, 255, 200} : (aColor_t){255, 255, 255, 60};
    a_DrawRect(dash_rect, dash_border);

    // "DASH" label
    aTextStyle_t dash_label_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = dash_ready ? (aColor_t){100, 200, 255, 255} : (aColor_t){150, 150, 150, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText("DASH", dash_x + dash_size / 2, dash_y + 6, dash_label_style);

    // "SHIFT" key hint below
    aTextStyle_t key_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = dash_ready ? (aColor_t){255, 255, 255, 255} : (aColor_t){100, 100, 100, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.35f
    };
    a_DrawText("SHIFT", dash_x + dash_size / 2, dash_y + 22, key_style);

    // Cooldown pie overlay (reuse same logic as hotbar)
    if (!dash_ready) {
      float remaining = 1.0f - dash_progress;
      float threshold = remaining * 2.0f * (float)PI;
      int cx = dash_x + dash_size / 2;
      int cy = dash_y + dash_size / 2;
      int r = dash_size / 2 - 2;
      int r2 = r * r;

      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 140);

      for (int pdy = -r; pdy <= r; pdy++) {
        for (int pdx = -r; pdx <= r; pdx++) {
          if (pdx * pdx + pdy * pdy > r2) continue;
          float angle = atan2f((float)pdx, (float)(-pdy));
          if (angle < 0.0f) angle += 2.0f * (float)PI;
          if (angle < threshold) {
            SDL_RenderDrawPoint(app.renderer, cx + pdx, cy + pdy);
          }
        }
      }

      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
    }
  }

  // Draw keyboard shortcuts (bottom right, 30% opacity white)
  aTextStyle_t shortcuts_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 77},  // 30% opacity (255 * 0.3 = 77)
    .align = TEXT_ALIGN_RIGHT,
    .wrap_width = 0,
    .scale = 0.5f
  };

  int y_offset = SCREEN_HEIGHT - 80;
  a_DrawText("Ctrl+T - Text Test", SCREEN_WIDTH - 20, y_offset, shortcuts_style);
  y_offset += 20;
  a_DrawText("Ctrl+M - Audio Test", SCREEN_WIDTH - 20, y_offset, shortcuts_style);
  y_offset += 20;
  a_DrawText("ESC - Quit", SCREEN_WIDTH - 20, y_offset, shortcuts_style);
}

// ============================================================================
// Game Over Scene
// ============================================================================

static int point_in_rect( int px, int py, int rx, int ry, int rw, int rh )
{
  return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static void scene_game_over_logic( float dt )
{
  (void)dt;

  if ( !gameover_flex ) return;

  // Check mouse click on buttons
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    int mx = app.mouse.x;
    int my = app.mouse.y;

    // Try Again button
    const FlexItem_t* retry = a_FlexGetItem( gameover_flex, GAMEOVER_BTN_IDX_RETRY );
    if ( retry && point_in_rect( mx, my, retry->calc_x, retry->calc_y, retry->w, retry->h ) )
    {
      app.mouse.pressed = 0;
      game_reset();
      return;
    }

    // Quit button
    const FlexItem_t* quit = a_FlexGetItem( gameover_flex, GAMEOVER_BTN_IDX_QUIT );
    if ( quit && point_in_rect( mx, my, quit->calc_x, quit->calc_y, quit->w, quit->h ) )
    {
      app.mouse.pressed = 0;
      app.running = 0;
      return;
    }

    app.mouse.pressed = 0;
  }
}

static void scene_game_over_draw( float dt )
{
  (void)dt;

  if ( !gameover_flex ) return;

  // Dark overlay over the whole screen
  a_DrawFilledRect(
    (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
    (aColor_t){0, 0, 0, 180}
  );

  // Panel background
  a_DrawFilledRect(
    (aRectf_t){(float)gameover_flex->x, (float)gameover_flex->y,
               (float)gameover_flex->w, (float)gameover_flex->h},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){(float)gameover_flex->x, (float)gameover_flex->y,
               (float)gameover_flex->w, (float)gameover_flex->h},
    (aColor_t){200, 30, 30, 255}
  );

  // "GAME OVER" title above the panel
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {220, 30, 30, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.2f
  };
  a_DrawText( "GAME OVER", SCREEN_WIDTH / 2,
              gameover_flex->y - 40, title_style );

  // Draw buttons
  int mx = app.mouse.x;
  int my = app.mouse.y;

  const char* labels[2] = { "TRY AGAIN", "QUIT" };
  for ( int i = 0; i < 2; i++ )
  {
    const FlexItem_t* item = a_FlexGetItem( gameover_flex, i );
    if ( !item ) continue;

    int hovered = point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h );

    aColor_t btn_bg = hovered ? (aColor_t){80, 80, 120, 255} : (aColor_t){50, 50, 70, 255};
    aColor_t btn_border = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){150, 150, 150, 200};

    a_DrawFilledRect(
      (aRectf_t){(float)item->calc_x, (float)item->calc_y,
                 (float)item->w, (float)item->h},
      btn_bg
    );
    a_DrawRect(
      (aRectf_t){(float)item->calc_x, (float)item->calc_y,
                 (float)item->w, (float)item->h},
      btn_border
    );

    aTextStyle_t btn_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){200, 200, 200, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    a_DrawText( labels[i],
                item->calc_x + item->w / 2,
                item->calc_y + 10,
                btn_style );
  }
}

// ============================================================================
// Game Reset
// ============================================================================

static void game_reset( void )
{
  // Clean up game over UI
  if ( gameover_flex )
  {
    a_FlexBoxDestroy( &gameover_flex );
  }

  // Clean up game systems
  enemy_cleanup();
  hotbar_cleanup();

  // Reset main.c timers
  time_remaining = 15.0f * 60.0f;
  spawn_timer = 0.0f;
  health_drop_timer = 0.0f;
  game_elapsed_timer = 0.0f;
  timed_weapon_dropped = 0;
  timed_weapon_dropped_2 = 0;
  timed_weapon_dropped_3 = 0;

  // Re-initialize all game systems
  player_init();
  enemy_init( MAX_ENEMIES, MAX_BLOOD_PARTICLES );
  weapons_init();
  drops_init();
  hotbar_init();

  current_scene = SCENE_GAME;
}

void aMainloop( void )
{
  a_PrepareScene();

  float dt = a_GetDeltaTime();
  app.delegate.logic( dt );
  app.delegate.draw( dt );

  a_PresentScene();
}

int main( void )
{
  a_Init( SCREEN_WIDTH, SCREEN_HEIGHT, "Archimedes" );

  aInitGame();

  #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop( aMainloop, 0, 1 );
  #endif

  #ifndef __EMSCRIPTEN__
    while( app.running ) {
      aMainloop();
    }
  #endif

  a_Quit();

  return 0;
}
