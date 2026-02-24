#include <stdio.h>
#include <stdlib.h>

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
#include "pickups.h"
#include "game_audio.h"
#include "game_director.h"
#include "collision.h"
#include "game_hud.h"
#include "xp.h"
#include "upgrades.h"
#include "fire_particles.h"

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

// Weather sound
static aSoundEffect_t rain_sound;
static int rain_loaded = 0;

// Timer system (countdown from 15:00)
static float time_remaining = 15.0f * 60.0f;

#define MAX_ENEMIES 50
#define MAX_BLOOD_PARTICLES 500
#define MAX_FIRE_PARTICLES 500

// Level-up overlay state
static int level_up_active = 0;
static UpgradeId_t level_up_cards[3];
static int level_up_card_count = 0;
static int level_up_selected = 0;  // Currently highlighted card index
#define LEVELUP_CARD_W  180
#define LEVELUP_CARD_H  240
#define LEVELUP_CARD_GAP 20

static void level_up_logic( void );
static void level_up_draw( void );

// Pause menu state
static int game_paused = 0;
static int pause_selected = 0;  // 0 = Continue, 1 = Quit
#define PAUSE_BTN_W 160
#define PAUSE_BTN_H 40

static void pause_logic( void );
static void pause_draw( void );

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

  // Initialize game audio, director, and collision
  game_audio_init();
  director_init();
  collision_init(MAX_ENEMIES);

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

  // Initialize fire particles
  fire_particles_init(MAX_FIRE_PARTICLES);

  // Initialize weapons, drops, and pickups
  weapons_init();
  drops_init();
  pickups_init();

  // Initialize hotbar UI
  hotbar_init();

  // Initialize XP and upgrades
  xp_init();
  upgrades_init();
}

static void aDoLoop( float dt )
{
  a_DoInput();

  // Dispatch to current scene
  switch ( current_scene )
  {
    case SCENE_GAME:
      // ESC to toggle pause
      if ( app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 )
      {
        app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
        if ( !level_up_active )
        {
          game_paused = !game_paused;
          pause_selected = 0;
        }
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
  // Pause menu pauses all game updates
  if ( game_paused )
  {
    pause_logic();
    return;
  }

  // Level-up overlay pauses all game updates
  if ( level_up_active )
  {
    level_up_logic();
    return;
  }

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

  // Director handles spawning, difficulty scaling, timed weapon drops, health drops
  director_update( dt );

  // Collision: snapshot alive state, check bullets, update enemies, resolve deaths
  collision_begin_death_tracking();
  collision_check_bullets();
  enemy_update( dt, player_get_x(), player_get_y(), player_get_vx(), player_get_vy() );
  collision_resolve_deaths();

  // Update weapons (auto-fire cooldowns) and drops (pickup check)
  weapons_update( dt );
  drops_update( dt );

  // Update pickups (lifetime, blink)
  pickups_update( dt );

  // Player collects power pickups on contact
  {
    float px = player_get_x();
    float py = player_get_y();
    int consumed = pickups_consume_nearest( px, py, 26.0f );
    if ( consumed >= 0 )
    {
      player_apply_buff( (PickupType_t)consumed );
    }
  }

  // Update player buffs (fire cone damage ticking, duration countdown)
  player_update_buffs( dt );

  // Update fire particles
  fire_particles_update( dt );

  // Update XP orbs (magnet pull, collection, level-up detection)
  xp_update( dt );

  // Check for level-up — pause game and show upgrade cards
  if ( xp_check_level_up() )
  {
    level_up_card_count = upgrades_roll_cards( level_up_cards );
    if ( level_up_card_count > 0 )
    {
      level_up_active = 1;
      level_up_selected = 0;
    }
  }

  // Pity timer: spawns pickups during dry spells
  pickups_check_pity( dt, player_get_x(), player_get_y() );

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

  // HUD (timer, health bar, dash indicator, shortcuts)
  hud_draw_game( time_remaining );

  // Draw entities
  enemy_draw();
  drops_draw();
  pickups_draw();
  xp_draw();
  weapons_draw();
  fire_particles_draw();
  player_draw_buffs();
  player_draw( bullet_img );

  // Level-up overlay (drawn on top of everything)
  if ( level_up_active )
  {
    level_up_draw();
  }

  // Pause overlay (drawn on top of everything)
  if ( game_paused )
  {
    pause_draw();
  }
}

// ============================================================================
// Utility
// ============================================================================

static int point_in_rect( int px, int py, int rx, int ry, int rw, int rh )
{
  return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

// ============================================================================
// Pause Menu
// ============================================================================

static FlexBox_t* pause_flex = NULL;

static void pause_begin( void )
{
  if ( pause_flex )
  {
    a_FlexBoxDestroy( &pause_flex );
  }

  int panel_w = 260;
  int panel_h = 160;
  pause_flex = a_FlexBoxCreate(
    (SCREEN_WIDTH - panel_w) / 2,
    (SCREEN_HEIGHT - panel_h) / 2,
    panel_w, panel_h
  );
  a_FlexConfigure( pause_flex, FLEX_DIR_COLUMN, FLEX_JUSTIFY_CENTER, 14 );
  a_FlexSetAlign( pause_flex, FLEX_ALIGN_CENTER );
  a_FlexSetPadding( pause_flex, 20 );
  a_FlexAddItem( pause_flex, PAUSE_BTN_W, PAUSE_BTN_H, NULL );  // Continue
  a_FlexAddItem( pause_flex, PAUSE_BTN_W, PAUSE_BTN_H, NULL );  // Quit
  a_FlexLayout( pause_flex );
}

static void pause_logic( void )
{
  if ( !pause_flex )
  {
    pause_begin();
  }

  // Arrow keys / W/S to navigate
  if ( app.keyboard[ SDL_SCANCODE_UP ] == 1 || app.keyboard[ SDL_SCANCODE_W ] == 1 )
  {
    pause_selected = 0;
    app.keyboard[ SDL_SCANCODE_UP ] = 0;
    app.keyboard[ SDL_SCANCODE_W ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_DOWN ] == 1 || app.keyboard[ SDL_SCANCODE_S ] == 1 )
  {
    pause_selected = 1;
    app.keyboard[ SDL_SCANCODE_DOWN ] = 0;
    app.keyboard[ SDL_SCANCODE_S ] = 0;
  }

  // Enter or Space to confirm
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 || app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;

    if ( pause_selected == 0 )
    {
      // Continue
      game_paused = 0;
      if ( pause_flex ) a_FlexBoxDestroy( &pause_flex );
    }
    else
    {
      // Quit
      app.running = 0;
    }
    return;
  }

  // Mouse click on buttons
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    int mx = app.mouse.x;
    int my = app.mouse.y;

    const FlexItem_t* cont_btn = a_FlexGetItem( pause_flex, 0 );
    if ( cont_btn && point_in_rect( mx, my, cont_btn->calc_x, cont_btn->calc_y, cont_btn->w, cont_btn->h ) )
    {
      app.mouse.pressed = 0;
      game_paused = 0;
      if ( pause_flex ) a_FlexBoxDestroy( &pause_flex );
      return;
    }

    const FlexItem_t* quit_btn = a_FlexGetItem( pause_flex, 1 );
    if ( quit_btn && point_in_rect( mx, my, quit_btn->calc_x, quit_btn->calc_y, quit_btn->w, quit_btn->h ) )
    {
      app.mouse.pressed = 0;
      app.running = 0;
      return;
    }

    app.mouse.pressed = 0;
  }
}

static void pause_draw( void )
{
  if ( !pause_flex ) return;

  // Dark overlay
  a_DrawFilledRect(
    (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
    (aColor_t){0, 0, 0, 160}
  );

  // Panel background
  a_DrawFilledRect(
    (aRectf_t){(float)pause_flex->x, (float)pause_flex->y,
               (float)pause_flex->w, (float)pause_flex->h},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){(float)pause_flex->x, (float)pause_flex->y,
               (float)pause_flex->w, (float)pause_flex->h},
    (aColor_t){150, 150, 200, 255}
  );

  // "GAME PAUSED" title above the panel
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.9f
  };
  a_DrawText( "GAME PAUSED", SCREEN_WIDTH / 2,
              pause_flex->y - 36, title_style );

  // Draw buttons
  const char* labels[2] = { "CONTINUE", "QUIT" };
  for ( int i = 0; i < 2; i++ )
  {
    const FlexItem_t* item = a_FlexGetItem( pause_flex, i );
    if ( !item ) continue;

    int hovered = ( i == pause_selected );

    // Also highlight on mouse hover
    if ( !hovered )
    {
      hovered = point_in_rect( app.mouse.x, app.mouse.y,
                               item->calc_x, item->calc_y, item->w, item->h );
    }

    aColor_t btn_bg = hovered ? (aColor_t){70, 70, 110, 255} : (aColor_t){45, 45, 65, 255};
    aColor_t btn_border = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){120, 120, 150, 200};

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
      .fg = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 180, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    a_DrawText( labels[i],
                item->calc_x + item->w / 2,
                item->calc_y + 10,
                btn_style );
  }
}

// ============================================================================
// Level-Up Overlay
// ============================================================================

static FlexBox_t* levelup_flex = NULL;

static void level_up_begin( void )
{
  // Destroy any previous layout
  if ( levelup_flex )
  {
    a_FlexBoxDestroy( &levelup_flex );
  }

  int total_w = level_up_card_count * LEVELUP_CARD_W + (level_up_card_count - 1) * LEVELUP_CARD_GAP + 40;
  int total_h = LEVELUP_CARD_H + 20;
  levelup_flex = a_FlexBoxCreate(
    (SCREEN_WIDTH - total_w) / 2,
    (SCREEN_HEIGHT - total_h) / 2 + 20,
    total_w, total_h
  );
  a_FlexConfigure( levelup_flex, FLEX_DIR_ROW, FLEX_JUSTIFY_CENTER, LEVELUP_CARD_GAP );
  a_FlexSetAlign( levelup_flex, FLEX_ALIGN_CENTER );
  a_FlexSetPadding( levelup_flex, 10 );

  for ( int i = 0; i < level_up_card_count; i++ )
  {
    a_FlexAddItem( levelup_flex, LEVELUP_CARD_W, LEVELUP_CARD_H, NULL );
  }
  a_FlexLayout( levelup_flex );
}

static void level_up_select( int card_index )
{
  if ( card_index < 0 || card_index >= level_up_card_count ) return;

  upgrades_apply( level_up_cards[card_index] );
  level_up_active = 0;

  if ( levelup_flex )
  {
    a_FlexBoxDestroy( &levelup_flex );
  }
}

static void level_up_logic( void )
{
  if ( !levelup_flex )
  {
    level_up_begin();
  }

  // Arrow keys to navigate
  if ( app.keyboard[ SDL_SCANCODE_LEFT ] == 1 || app.keyboard[ SDL_SCANCODE_A ] == 1 )
  {
    level_up_selected--;
    if ( level_up_selected < 0 ) level_up_selected = level_up_card_count - 1;
    app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
    app.keyboard[ SDL_SCANCODE_A ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_RIGHT ] == 1 || app.keyboard[ SDL_SCANCODE_D ] == 1 )
  {
    level_up_selected++;
    if ( level_up_selected >= level_up_card_count ) level_up_selected = 0;
    app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
    app.keyboard[ SDL_SCANCODE_D ] = 0;
  }

  // Enter or Space to confirm selection
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 || app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    level_up_select( level_up_selected );
    return;
  }

  // Keyboard: 1, 2, 3 (direct select)
  if ( app.keyboard[ SDL_SCANCODE_1 ] == 1 && level_up_card_count >= 1 )
  {
    level_up_select( 0 );
    app.keyboard[ SDL_SCANCODE_1 ] = 0;
    return;
  }
  if ( app.keyboard[ SDL_SCANCODE_2 ] == 1 && level_up_card_count >= 2 )
  {
    level_up_select( 1 );
    app.keyboard[ SDL_SCANCODE_2 ] = 0;
    return;
  }
  if ( app.keyboard[ SDL_SCANCODE_3 ] == 1 && level_up_card_count >= 3 )
  {
    level_up_select( 2 );
    app.keyboard[ SDL_SCANCODE_3 ] = 0;
    return;
  }

  // Mouse click on card
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    int mx = app.mouse.x;
    int my = app.mouse.y;

    for ( int i = 0; i < level_up_card_count; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( levelup_flex, i );
      if ( item && point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h ) )
      {
        app.mouse.pressed = 0;
        level_up_select( i );
        return;
      }
    }
    app.mouse.pressed = 0;
  }
}

static void level_up_draw( void )
{
  if ( !levelup_flex ) return;

  // Dark overlay
  a_DrawFilledRect(
    (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
    (aColor_t){0, 0, 0, 160}
  );

  // "LEVEL UP!" title
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 215, 0, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.2f
  };
  a_DrawText( "LEVEL UP!", SCREEN_WIDTH / 2,
              levelup_flex->y - 50, title_style );

  // Level label
  char lv_text[32];
  snprintf( lv_text, sizeof(lv_text), "Level %d", xp_get_level() );
  aTextStyle_t lv_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {200, 200, 200, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.5f
  };
  a_DrawText( lv_text, SCREEN_WIDTH / 2,
              levelup_flex->y - 22, lv_style );

  for ( int i = 0; i < level_up_card_count; i++ )
  {
    const FlexItem_t* item = a_FlexGetItem( levelup_flex, i );
    if ( !item ) continue;

    const UpgradeInfo_t* info = upgrades_get_info( level_up_cards[i] );
    if ( !info ) continue;

    int tier = upgrades_get_tier( level_up_cards[i] );
    int selected = ( i == level_up_selected );

    // Rarity border color
    aColor_t border_color;
    switch ( info->rarity )
    {
      case RARITY_UNCOMMON: border_color = (aColor_t){80, 220, 80, 255}; break;
      case RARITY_RARE:     border_color = (aColor_t){255, 200, 50, 255}; break;
      default:              border_color = (aColor_t){200, 200, 200, 255}; break;
    }

    // Card background
    aColor_t card_bg = selected ? (aColor_t){60, 60, 90, 240} : (aColor_t){35, 35, 55, 240};
    a_DrawFilledRect(
      (aRectf_t){(float)item->calc_x, (float)item->calc_y,
                 (float)item->w, (float)item->h},
      card_bg
    );

    // Border (thicker when hovered — draw two rects)
    a_DrawRect(
      (aRectf_t){(float)item->calc_x, (float)item->calc_y,
                 (float)item->w, (float)item->h},
      border_color
    );
    if ( selected )
    {
      a_DrawRect(
        (aRectf_t){(float)(item->calc_x + 1), (float)(item->calc_y + 1),
                   (float)(item->w - 2), (float)(item->h - 2)},
        border_color
      );
    }

    int cx = item->calc_x + item->w / 2;
    int top = item->calc_y;

    // Weapon name
    aTextStyle_t weapon_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 220, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.4f
    };
    a_DrawText( info->weapon_name, cx, top + 12, weapon_style );

    // Upgrade name
    aTextStyle_t name_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    a_DrawText( info->upgrade_name, cx, top + 32, name_style );

    // Tier label ("I", "II", "III")
    const char* tier_labels[] = { "I", "II", "III" };
    const char* tier_str = (tier < 3) ? tier_labels[tier] : "MAX";
    aTextStyle_t tier_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = border_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText( tier_str, cx, top + 56, tier_style );

    // Rarity label
    const char* rarity_str;
    switch ( info->rarity )
    {
      case RARITY_UNCOMMON: rarity_str = "Uncommon"; break;
      case RARITY_RARE:     rarity_str = "Rare"; break;
      default:              rarity_str = "Common"; break;
    }
    aTextStyle_t rarity_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {border_color.r, border_color.g, border_color.b, 160},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.3f
    };
    a_DrawText( rarity_str, cx, top + 76, rarity_style );

    // Separator line
    a_DrawLine(
      item->calc_x + 10, top + 92,
      item->calc_x + item->w - 10, top + 92,
      (aColor_t){255, 255, 255, 40}
    );

    // Description (wrapped)
    if ( tier < UPG_MAX_TIER && info->descriptions[tier] )
    {
      aTextStyle_t desc_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 200, 220},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.35f,
        .wrap_width = item->w - 20
      };
      a_DrawText( info->descriptions[tier], cx, top + 102, desc_style );
    }

    // Key hint at bottom
    char key_text[16];
    snprintf( key_text, sizeof(key_text), "[%d]", i + 1 );
    aTextStyle_t key_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = selected ? (aColor_t){255, 255, 255, 255} : (aColor_t){150, 150, 150, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( key_text, cx, top + item->h - 28, key_style );
  }
}

// ============================================================================
// Game Over Scene
// ============================================================================

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
  // Clean up UI
  if ( gameover_flex )
  {
    a_FlexBoxDestroy( &gameover_flex );
  }
  if ( levelup_flex )
  {
    a_FlexBoxDestroy( &levelup_flex );
  }
  if ( pause_flex )
  {
    a_FlexBoxDestroy( &pause_flex );
  }

  // Clean up game systems
  enemy_cleanup();
  collision_cleanup();
  hotbar_cleanup();
  fire_particles_cleanup();

  // Reset main.c timers
  time_remaining = 15.0f * 60.0f;
  director_reset();

  // Re-initialize all game systems
  player_init();
  enemy_init( MAX_ENEMIES, MAX_BLOOD_PARTICLES );
  fire_particles_init( MAX_FIRE_PARTICLES );
  collision_init( MAX_ENEMIES );
  weapons_init();
  drops_init();
  pickups_init();
  hotbar_init();
  xp_reset();
  upgrades_reset();
  level_up_active = 0;
  game_paused = 0;

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
