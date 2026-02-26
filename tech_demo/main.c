#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "Archimedes.h"
#include "player_actions.h"
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
#include "stats.h"
#include "progress.h"
#include "save_data.h"
#include "snake.h"
#include "menu.h"

// Menu sound effects
static aSoundEffect_t menu_move_sound;
static int menu_move_loaded = 0;
static aSoundEffect_t menu_click_sound;
static int menu_click_loaded = 0;

static void play_menu_move(void)
{
  if (menu_move_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 40,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(&menu_move_sound, &opts);
  }
}

static void play_menu_click(void)
{
  if (menu_click_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 120,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(&menu_click_sound, &opts);
  }
}

// Scene system
typedef enum {
  SCENE_MAIN_MENU,
  SCENE_ENEMY_TYPES,
  SCENE_WEAPONS,
  SCENE_UPGRADES,
  SCENE_GAME,
  SCENE_GAME_OVER,
  SCENE_SETTINGS
} Scene_t;

static Scene_t current_scene = SCENE_MAIN_MENU;

// FPS counter
static int show_fps = 0;
static float fps_accumulator = 0.0f;
static int fps_frame_count = 0;
static int fps_display = 0;

// Scene function declarations
static void scene_main_menu_logic( float dt );
static void scene_main_menu_draw( float dt );
static void scene_game_logic( float dt );
static void scene_game_draw( float dt );
static void scene_game_over_logic( float dt );
static void scene_game_over_draw( float dt );
static void scene_enemy_types_logic( float dt );
static void scene_enemy_types_draw( float dt );
static void scene_upgrades_logic( float dt );
static void scene_upgrades_draw( float dt );
static void game_reset( void );
static void scene_settings_logic( float dt );
static void scene_settings_draw( float dt );
static void scene_weapons_logic( float dt );
static void scene_weapons_draw( float dt );

static void draw_hotbar_tooltip( void );
static void aDoLoop( float );
static void aRenderLoop( float );

// Game over UI
static FlexBox_t* gameover_flex = NULL;
#define GAMEOVER_BTN_W 160
#define GAMEOVER_BTN_H 40
#define GAMEOVER_BTN_IDX_RETRY    0
#define GAMEOVER_BTN_IDX_MENU     1
#define GAMEOVER_BTN_IDX_SETTINGS 2
#define GAMEOVER_BTN_IDX_QUIT     3
#define GAMEOVER_BTN_COUNT        4
static int gameover_sel = 0; // keyboard-selected button index

// Main menu UI
static FlexBox_t* mainmenu_flex = NULL;
#define MAINMENU_BTN_W 160
#define MAINMENU_BTN_H 40
#define MAINMENU_BTN_IDX_PLAY     0
#define MAINMENU_BTN_IDX_ENEMIES  1
#define MAINMENU_BTN_IDX_WEAPONS  2
#define MAINMENU_BTN_IDX_UPGRADES 3
#define MAINMENU_BTN_IDX_SETTINGS 4
#define MAINMENU_BTN_IDX_QUIT     5
#define MAINMENU_BTN_COUNT         6
static int mainmenu_sel = 0;

// Settings scene state
static int settings_sel = 0;          // 0=SFX, 1=Music, 2=Delete
static int settings_confirm = 0;      // 0=off, 1=first confirm, 2=second confirm
static int settings_confirm_sel = 0;  // 0=YES, 1=NO
static Scene_t settings_return_scene; // where to go back on ESC

// Global upgrades scene
static int upgrades_sel = 0;
static int upgrades_sorted_dirty = 1;

// Frozen end-of-run stats (captured at death)
static int   go_score = 0;
static int   go_kills = 0;
static int   go_level = 0;
static float go_time  = 0.0f;
static int   go_best_score = 0;
static float go_best_time  = 0.0f;
static int   go_new_best = 0;
static int   go_new_best_score = 0;
static int   go_new_best_kills = 0;
static int   go_new_best_time  = 0;
static EnemyType_t go_killer_type = ENEMY_TYPE_GRUNT;
static int   go_killer_damage = 0;
static int   go_deaths_by_killer = 0;

// Death slowdown animation
static int   death_active = 0;
static float death_timer  = 0.0f;
#define DEATH_SLOWDOWN_DURATION 1.5f
#define DEATH_ANIM_START        1.0f
#define DEATH_ANIM_DURATION     0.5f

aImage_t* bullet_img;


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
static int free_upgrade_active = 0; // 1 = showing free weapon upgrade (not a level-up)
#define LEVELUP_CARD_W  200
#define LEVELUP_CARD_H  280
#define LEVELUP_CARD_GAP 20

static void level_up_logic( void );
static void level_up_draw( void );

// Pause menu state
static int game_paused = 0;
static int pause_selected = 0;  // 0 = Continue, 1 = Main Menu, 2 = Quit
static int pause_confirming = 0; // 1 = showing "are you sure?" for main menu
#define PAUSE_BTN_W 160
#define PAUSE_BTN_H 40
#define PAUSE_BTN_COUNT 4

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
  snake_init();

  // Initialize game audio, director, and collision
  game_audio_init();
  game_audio_start_music();
  director_init();
  collision_init(MAX_ENEMIES);


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
  save_data_init();
  stats_init();
  progress_init();

  // Load menu sounds
  if (a_AudioLoadSound("resources/soundEffects/menu_move.wav", &menu_move_sound) == 0)
    menu_move_loaded = 1;
  if (a_AudioLoadSound("resources/soundEffects/menu_click.wav", &menu_click_sound) == 0)
    menu_click_loaded = 1;

  // Restore saved volume settings
  app.audio.master_volume = save_data_get_int( "sfx_vol", 96 );
  app.audio.music_volume  = save_data_get_int( "music_vol", 64 );
  a_AudioSetChannelVolume( -1, app.audio.master_volume );
  a_AudioSetMusicVolume( app.audio.music_volume );
}

static void aDoLoop( float dt )
{
  a_DoInput();
  menu_update_mouse();

  // Ctrl+H to toggle FPS counter
  {
    static int ctrl_h_pressed = 0;
    if ( (app.keyboard[ SDL_SCANCODE_LCTRL ] || app.keyboard[ SDL_SCANCODE_RCTRL ]) &&
         app.keyboard[ SDL_SCANCODE_H ] == 1 && !ctrl_h_pressed )
    {
      show_fps = !show_fps;
      ctrl_h_pressed = 1;
      app.keyboard[ SDL_SCANCODE_H ] = 0;
    }
    if ( app.keyboard[ SDL_SCANCODE_H ] == 0 )
      ctrl_h_pressed = 0;
  }

  // Dispatch to current scene
  switch ( current_scene )
  {
    case SCENE_MAIN_MENU:
      scene_main_menu_logic( dt );
      break;
    case SCENE_ENEMY_TYPES:
      scene_enemy_types_logic( dt );
      break;
    case SCENE_WEAPONS:
      scene_weapons_logic( dt );
      break;
    case SCENE_UPGRADES:
      scene_upgrades_logic( dt );
      break;
    case SCENE_GAME:
      // ESC to toggle pause
      if ( app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 )
      {
        app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
        if ( !level_up_active )
        {
          game_paused = !game_paused;
          pause_selected = 0;
          if ( game_paused ) player_clear_screen_flashes();
        }
      }
      scene_game_logic( dt );
      break;
    case SCENE_GAME_OVER:
      scene_game_over_logic( dt );
      break;
    case SCENE_SETTINGS:
      scene_settings_logic( dt );
      break;
  }
}

static void scene_game_logic( float dt )
{
  // Pause menu pauses all game updates (blocked during death)
  if ( game_paused && !death_active )
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

  // Death slowdown — linear deceleration to 0% over 1.5s
  if ( death_active )
  {
    death_timer += dt; // real dt before slowdown

    if ( death_timer >= DEATH_SLOWDOWN_DURATION )
    {
      // Capture run stats
      go_score = stats_get_score();
      go_kills = stats_get_kills();
      go_level = xp_get_level();
      go_time  = director_get_elapsed();
      go_best_score = stats_get_best_score();
      go_best_time  = stats_get_best_time();
      go_new_best_score = (go_score > go_best_score);
      go_new_best_kills = (go_kills > stats_get_best_kills());
      go_new_best_time  = (go_time > go_best_time);
      go_new_best = (go_new_best_score || go_new_best_kills || go_new_best_time);
      stats_save_if_best(go_time);
      stats_add_run_time(go_time);

      // Capture killer info and record death
      go_killer_type = player_get_killer_type();
      go_killer_damage = player_get_killer_damage();
      stats_record_death(go_killer_type);
      go_deaths_by_killer = stats_get_deaths_by_type(go_killer_type);

      // Bank kills to meta-progression
      int run_kills[ENEMY_TYPE_COUNT];
      for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        run_kills[t] = stats_get_kills_by_type((EnemyType_t)t);
      progress_bank_run_kills(run_kills);

      // Bank weapon hits + kills to weapon meta-progression
      {
        int whits[WID_COUNT];
        int wkills[WID_COUNT];
        for (int w = 0; w < WID_COUNT; w++) {
          whits[w] = weapons_get_total_hits((WeaponType_t)(w + 1));
          wkills[w] = stats_get_weapon_kills_by_id(w);
        }
        progress_bank_weapon_hits(whits);
        progress_bank_weapon_kills(wkills);
      }

      int run_up = xp_get_pending_upgrade_points();
      if (run_up > 0) progress_bank_upgrade_points(run_up);

      // Create game over UI layout
      if ( gameover_flex )
      {
        a_FlexBoxDestroy( &gameover_flex );
      }
      int panel_w = 340;
      int panel_h = 480;
      gameover_flex = a_FlexBoxCreate(
        (SCREEN_WIDTH - panel_w) / 2,
        (SCREEN_HEIGHT - panel_h) / 2,
        panel_w, panel_h
      );
      a_FlexConfigure( gameover_flex, FLEX_DIR_COLUMN, FLEX_JUSTIFY_END, 16 );
      a_FlexSetAlign( gameover_flex, FLEX_ALIGN_CENTER );
      a_FlexSetPadding( gameover_flex, 20 );
      a_FlexAddItem( gameover_flex, GAMEOVER_BTN_W, GAMEOVER_BTN_H, NULL );
      a_FlexAddItem( gameover_flex, GAMEOVER_BTN_W, GAMEOVER_BTN_H, NULL );
      a_FlexAddItem( gameover_flex, GAMEOVER_BTN_W, GAMEOVER_BTN_H, NULL );
      a_FlexAddItem( gameover_flex, GAMEOVER_BTN_W, GAMEOVER_BTN_H, NULL );
      a_FlexLayout( gameover_flex );
      gameover_sel = GAMEOVER_BTN_IDX_RETRY;

      a_AudioStopMusic(0);

      death_active = 0;
      current_scene = SCENE_GAME_OVER;
      return;
    }

    // Slow game linearly to 0%
    float slow = 1.0f - (death_timer / DEATH_SLOWDOWN_DURATION);
    dt *= slow;

    // Ramp music volume down with the slowdown (use Mix directly to preserve saved volume)
    int death_vol = (int)((float)app.audio.music_volume * slow);
    if (death_vol < 0) death_vol = 0;
    Mix_VolumeMusic( death_vol );

    // Continue updating game at reduced speed (fall through below)
  }

  // Low-health game slow: 5% per danger tier (up to 15% at <=25% HP)
  if ( !death_active )
  {
    int danger = player_get_danger_tier();
    float hp_slow = 1.0f - 0.05f * danger;
    dt *= hp_slow;
  }

  // Update player movement and shooting
  player_update( dt );

  // Check for player death — start death slowdown
  if ( !player_is_alive() && !death_active )
  {
    death_active = 1;
    death_timer = 0.0f;
    player_clear_screen_flashes();
    player_start_death();
  }

  // Update timer (countdown)
  time_remaining -= dt;
  if ( time_remaining < 0.0f )
  {
    time_remaining = 0.0f;
  }

  // Director handles spawning, difficulty scaling, timed weapon drops, health drops
  director_update( dt );

  // Snake boss update
  snake_update( dt, player_get_x(), player_get_y(), player_get_vx(), player_get_vy() );
  snake_shatter_update( dt );

  // Collision: snapshot alive state, check bullets, update enemies, resolve deaths
  collision_begin_death_tracking();
  collision_check_bullets();
  collision_check_turret_bullets();
  enemy_update( dt, player_get_x(), player_get_y(), player_get_vx(), player_get_vy() );
  collision_resolve_deaths();

  // Update weapons (auto-fire cooldowns) and drops (pickup check)
  weapons_update( dt );
  drops_update( dt );

  // Check for weapon pickup → free upgrade (requires meta-progression purchase)
  {
    WeaponType_t picked = drops_get_last_weapon_picked();
    if ( picked != WEAPON_NONE )
    {
      drops_clear_last_weapon_picked();
      // WeaponType_t → WeaponId_t: WEAPON_WAND=1 → WID_WAND=0, etc.
      WeaponId_t wid = (WeaponId_t)(picked - 1);
      if ( wid >= 0 && wid < WID_COUNT && wprog_has_free_upgrade(wid) )
      {
        level_up_card_count = upgrades_roll_cards_for_weapon( picked, level_up_cards );
        if ( level_up_card_count > 0 )
        {
          level_up_active = 1;
          free_upgrade_active = 1;
          level_up_selected = 0;
          player_clear_screen_flashes();
        }
      }
    }
  }

  // Update pickups (lifetime, blink)
  pickups_update( dt );

  // Player collects power pickups on contact (not while dead)
  if ( player_is_alive() )
  {
    float px = player_get_x();
    float py = player_get_y();
    int consumed = pickups_consume_nearest( px, py, 34.0f );
    if ( consumed >= 0 )
    {
      player_apply_buff( (PickupType_t)consumed );
      stats_add_score(3);
    }
  }

  // Update player buffs (fire cone damage ticking, duration countdown)
  player_update_buffs( dt );

  // Update fire particles
  fire_particles_update( dt );

  // Update XP orbs (magnet pull, collection, level-up detection)
  xp_update( dt );

  // Check for level-up — pause game and show upgrade cards
  if ( !level_up_active && xp_check_level_up() )
  {
    level_up_card_count = upgrades_roll_cards( level_up_cards );
    if ( level_up_card_count > 0 )
    {
      level_up_active = 1;
      level_up_selected = 0;
      player_clear_screen_flashes();
    }
  }

  // Pity timer: spawns pickups during dry spells
  pickups_check_pity( dt, player_get_x(), player_get_y() );

  if ( app.mouse.wheel == 1 )
  {
    app.mouse.wheel = 0;
  }

  if ( app.mouse.wheel == -1 )
  {
    app.mouse.wheel = 0;
  }

}

static void aRenderLoop( float dt )
{
  // Dispatch to current scene
  switch ( current_scene )
  {
    case SCENE_MAIN_MENU:
      scene_main_menu_draw( dt );
      break;
    case SCENE_ENEMY_TYPES:
      scene_enemy_types_draw( dt );
      break;
    case SCENE_WEAPONS:
      scene_weapons_draw( dt );
      break;
    case SCENE_UPGRADES:
      scene_upgrades_draw( dt );
      break;
    case SCENE_GAME:
    {
      int sx, sy;
      player_get_screen_shake(&sx, &sy);
      if (sx || sy) {
        SDL_Rect vp = { sx, sy, SCREEN_WIDTH, SCREEN_HEIGHT };
        SDL_RenderSetViewport(app.renderer, &vp);
      }
      scene_game_draw( dt );
      if (sx || sy)
        SDL_RenderSetViewport(app.renderer, NULL);
      break;
    }
    case SCENE_GAME_OVER:
      scene_game_over_draw( dt );
      break;
    case SCENE_SETTINGS:
      scene_settings_draw( dt );
      break;
  }

  // Draw hotbar and screen flash (game scene only)
  if ( current_scene == SCENE_GAME )
  {
    hotbar_draw();
    draw_hotbar_tooltip();
    player_draw_screen_flash();

    // Death slowdown overlay — dark vignette + "GAME OVER" sliding in
    if ( death_active && death_timer >= DEATH_ANIM_START )
    {
      float anim_t = (death_timer - DEATH_ANIM_START) / DEATH_ANIM_DURATION;
      if (anim_t > 1.0f) anim_t = 1.0f;

      // Dark overlay fading in
      int overlay_alpha = (int)(180.0f * anim_t);
      a_DrawFilledRect(
        (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
        (aColor_t){0, 0, 0, (uint8_t)overlay_alpha}
      );

      // "GAME OVER" slides down from above
      int final_y = SCREEN_HEIGHT / 2 - 20;
      int start_y = -40;
      int text_y = start_y + (int)((float)(final_y - start_y) * anim_t);
      int text_alpha = (int)(255.0f * anim_t);

      aTextStyle_t death_title = {
        .type = FONT_ENTER_COMMAND,
        .fg = {220, 30, 30, (uint8_t)text_alpha},
        .align = TEXT_ALIGN_CENTER,
        .scale = 1.2f
      };
      a_DrawText( "GAME OVER", SCREEN_WIDTH / 2, text_y, death_title );
    }
  }

  // FPS counter (global, drawn on top of everything)
  if ( show_fps )
  {
    fps_frame_count++;
    fps_accumulator += dt;
    if ( fps_accumulator >= 0.5f )
    {
      fps_display = (int)( (float)fps_frame_count / fps_accumulator + 0.5f );
      fps_frame_count = 0;
      fps_accumulator = 0.0f;
    }
    char fps_text[16];
    snprintf( fps_text, sizeof(fps_text), "FPS: %d", fps_display );
    aTextStyle_t fps_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 0, 220},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.5f
    };
    a_DrawText( fps_text, 6, 4, fps_style );
  }
}

static void scene_game_draw( float dt )
{
  (void)dt;

  // HUD (timer, health bar, dash indicator, shortcuts)
  hud_draw_game( time_remaining );

  // Draw entities
  enemy_draw();
  snake_draw();
  snake_shatter_draw();
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

// Alias to menu utility (avoids renaming every call site)
#define point_in_rect  menu_point_in_rect
#define mouse_moved_this_frame  menu_mouse_moved()

// ============================================================================
// Main Menu Scene
// ============================================================================

static void mainmenu_begin( void )
{
  if ( mainmenu_flex )
  {
    a_FlexBoxDestroy( &mainmenu_flex );
  }

  int panel_w = 260;
  int panel_h = 380;
  mainmenu_flex = a_FlexBoxCreate(
    (SCREEN_WIDTH - panel_w) / 2,
    (SCREEN_HEIGHT - panel_h) / 2 + 40,
    panel_w, panel_h
  );
  a_FlexConfigure( mainmenu_flex, FLEX_DIR_COLUMN, FLEX_JUSTIFY_CENTER, 14 );
  a_FlexSetAlign( mainmenu_flex, FLEX_ALIGN_CENTER );
  a_FlexSetPadding( mainmenu_flex, 20 );
  a_FlexAddItem( mainmenu_flex, MAINMENU_BTN_W, MAINMENU_BTN_H, NULL );  // Play
  a_FlexAddItem( mainmenu_flex, MAINMENU_BTN_W, MAINMENU_BTN_H, NULL );  // Enemies
  a_FlexAddItem( mainmenu_flex, MAINMENU_BTN_W, MAINMENU_BTN_H, NULL );  // Weapons
  a_FlexAddItem( mainmenu_flex, MAINMENU_BTN_W, MAINMENU_BTN_H, NULL );  // Upgrades
  a_FlexAddItem( mainmenu_flex, MAINMENU_BTN_W, MAINMENU_BTN_H, NULL );  // Settings
  a_FlexAddItem( mainmenu_flex, MAINMENU_BTN_W, MAINMENU_BTN_H, NULL );  // Quit
  a_FlexLayout( mainmenu_flex );
}

static void scene_main_menu_logic( float dt )
{
  (void)dt;

  if ( !mainmenu_flex )
  {
    mainmenu_begin();
  }

  static Menu_t m;
  m.selected = mainmenu_sel;
  m.count    = MAINMENU_BTN_COUNT;
  m.axis     = MENU_AXIS_VERTICAL;
  m.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
  m.flex     = mainmenu_flex;
  m.rects    = NULL;

  MenuResult_t r = menu_update( &m );
  mainmenu_sel = m.selected;

  if ( r == MENU_CONFIRM )
  {
    if ( mainmenu_sel == MAINMENU_BTN_IDX_PLAY )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      game_reset();
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_ENEMIES )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = SCENE_ENEMY_TYPES;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_WEAPONS )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = SCENE_WEAPONS;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_UPGRADES )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      upgrades_sel = 0;
      upgrades_sorted_dirty = 1;
      current_scene = SCENE_UPGRADES;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_SETTINGS )
    {
      settings_return_scene = SCENE_MAIN_MENU;
      settings_sel = 0;
      settings_confirm = 0;
      current_scene = SCENE_SETTINGS;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_QUIT )
    {
      app.running = 0;
    }
    return;
  }
}

static void draw_hotbar_tooltip( void )
{
  int hslot = hotbar_get_hovered_slot( app.mouse.x, app.mouse.y );
  if ( hslot < 0 ) return;

  const Weapon_t* w = weapons_get_slot( hslot );
  if ( !w || w->type == WEAPON_NONE ) return;

  int sx, sy, sw, sh;
  hotbar_get_slot_rect( hslot, &sx, &sy, &sw, &sh );

  UpgradeId_t upg_ids[UPG_COUNT];
  int upg_count = upgrades_get_for_weapon( (int)w->type, upg_ids, UPG_COUNT );

  int active_count = 0;
  for ( int u = 0; u < upg_count; u++ )
  {
    if ( upgrades_get_tier( upg_ids[u] ) > 0 ) active_count++;
  }

  int tt_w = 220;
  int tt_h = 56 + active_count * 20;
  int tt_x = sx + sw / 2 - tt_w / 2;
  int tt_y = sy - tt_h - 8;

  if ( tt_x < 4 ) tt_x = 4;
  if ( tt_x + tt_w > SCREEN_WIDTH - 4 ) tt_x = SCREEN_WIDTH - 4 - tt_w;
  if ( tt_y < 4 ) tt_y = 4;

  a_DrawFilledRect(
    (aRectf_t){(float)tt_x, (float)tt_y, (float)tt_w, (float)tt_h},
    (aColor_t){20, 20, 40, 230}
  );
  a_DrawRect(
    (aRectf_t){(float)tt_x, (float)tt_y, (float)tt_w, (float)tt_h},
    (aColor_t){180, 180, 220, 255}
  );

  int cx = tt_x + tt_w / 2;
  int ty = tt_y + 6;

  aTextStyle_t tt_name = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.6f
  };
  a_DrawText( w->label, cx, ty, tt_name );
  ty += 18;

  char cd_text[32];
  float cd = weapons_get_cooldown_progress( hslot );
  snprintf( cd_text, sizeof(cd_text), "Cooldown: %.1fs", w->cooldown );
  aTextStyle_t tt_cd = {
    .type = FONT_ENTER_COMMAND,
    .fg = cd >= 1.0f ? (aColor_t){100, 255, 100, 255} : (aColor_t){255, 200, 100, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.45f
  };
  a_DrawText( cd_text, cx, ty, tt_cd );
  ty += 20;

  for ( int u = 0; u < upg_count; u++ )
  {
    int tier = upgrades_get_tier( upg_ids[u] );
    if ( tier <= 0 ) continue;

    const UpgradeInfo_t* info = upgrades_get_info( upg_ids[u] );

    char line[64];
    const char* pips[3] = { "I", "I I", "I I I" };
    snprintf( line, sizeof(line), "%s %s", info->upgrade_name,
              tier <= 3 ? pips[tier - 1] : "MAX" );

    aColor_t upg_color;
    switch ( info->rarity )
    {
      case RARITY_UNCOMMON: upg_color = (aColor_t){100, 230, 100, 255}; break;
      case RARITY_RARE:     upg_color = (aColor_t){255, 210, 60, 255}; break;
      default:              upg_color = (aColor_t){200, 200, 200, 255}; break;
    }

    aTextStyle_t tt_upg = {
      .type = FONT_ENTER_COMMAND,
      .fg = upg_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.4f
    };
    a_DrawText( line, cx, ty, tt_upg );
    ty += 18;
  }
}

static void draw_hotkeys( int cx, int top_y )
{
  aTextStyle_t header_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {200, 200, 220, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.6f
  };
  aTextStyle_t key_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {160, 160, 180, 220},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.45f
  };

  a_DrawText( "CONTROLS", cx, top_y, header_style );

  const char* lines[] = {
    "WASD / ARROWS - MOVE",
    "SHIFT / SPACE - DASH",
    "1  2  3 - SELECT CARD",
    "ENTER / SPACE - CONFIRM",
    "ESC - PAUSE",
  };
  int line_h = 24;
  for ( int i = 0; i < 5; i++ )
  {
    a_DrawText( lines[i], cx, top_y + 34 + i * line_h, key_style );
  }
}

static void scene_main_menu_draw( float dt )
{
  (void)dt;

  if ( !mainmenu_flex ) return;

  // Title "ARCHIMEDES" centered near the top
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.6f
  };
  a_DrawText( "ARCHIMEDES", SCREEN_WIDTH / 2,
              mainmenu_flex->y - 90, title_style );

  aTextStyle_t subtitle_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {180, 180, 200, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.8f
  };
  a_DrawText( "survivors", SCREEN_WIDTH / 2,
              mainmenu_flex->y - 35, subtitle_style );

  aTextStyle_t version_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {120, 120, 140, 180},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.4f
  };
  a_DrawText( "early-access-v-0.2", SCREEN_WIDTH / 2,
              mainmenu_flex->y + mainmenu_flex->h + 6, version_style );

  // Panel background
  a_DrawFilledRect(
    (aRectf_t){(float)mainmenu_flex->x, (float)mainmenu_flex->y,
               (float)mainmenu_flex->w, (float)mainmenu_flex->h},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){(float)mainmenu_flex->x, (float)mainmenu_flex->y,
               (float)mainmenu_flex->w, (float)mainmenu_flex->h},
    (aColor_t){150, 150, 200, 255}
  );

  // Draw buttons
  const char* labels[6] = { "PLAY", "ENEMIES", "WEAPONS", "UPGRADES", "SETTINGS", "QUIT" };
  int mx = app.mouse.x;
  int my = app.mouse.y;
  int has_enemy_upgrades = progress_has_affordable_upgrade();
  int has_weapon_upgrades = wprog_has_affordable();
  int has_global_upgrades = global_has_affordable();

  for ( int i = 0; i < MAINMENU_BTN_COUNT; i++ )
  {
    const FlexItem_t* item = a_FlexGetItem( mainmenu_flex, i );
    if ( !item ) continue;

    int hovered = point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h );
    int selected = ( i == mainmenu_sel );
    int highlight = hovered || selected;

    aColor_t btn_bg = highlight ? (aColor_t){70, 70, 110, 255} : (aColor_t){45, 45, 65, 255};
    aColor_t btn_border = highlight ? (aColor_t){255, 255, 255, 255} : (aColor_t){120, 120, 150, 200};

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
      .fg = highlight ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 180, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    a_DrawText( labels[i],
                item->calc_x + item->w / 2,
                item->calc_y + 10,
                btn_style );

    // Upgrade indicator next to ENEMIES button
    if ( i == MAINMENU_BTN_IDX_ENEMIES && has_enemy_upgrades )
    {
      float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
      int arrow_x = item->calc_x + item->w + 12;
      int arrow_y = item->calc_y + item->h / 2 + (int)bob;
      a_DrawFilledTriangle(
        arrow_x, arrow_y,
        arrow_x + 16, arrow_y - 10,
        arrow_x + 16, arrow_y + 10,
        (aColor_t){80, 220, 80, 255}
      );
      aTextStyle_t upg_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {80, 220, 80, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.45f
      };
      a_DrawText( "UPGRADES", arrow_x + 22, arrow_y - 10, upg_style );
      a_DrawText( "AVAILABLE!", arrow_x + 22, arrow_y + 3, upg_style );
    }

    // Upgrade indicator next to WEAPONS button
    if ( i == MAINMENU_BTN_IDX_WEAPONS && has_weapon_upgrades )
    {
      float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
      int arrow_x = item->calc_x + item->w + 12;
      int arrow_y = item->calc_y + item->h / 2 + (int)bob;
      a_DrawFilledTriangle(
        arrow_x, arrow_y,
        arrow_x + 16, arrow_y - 10,
        arrow_x + 16, arrow_y + 10,
        (aColor_t){100, 180, 255, 255}
      );
      aTextStyle_t upg_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {100, 180, 255, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.45f
      };
      a_DrawText( "UPGRADES", arrow_x + 22, arrow_y - 10, upg_style );
      a_DrawText( "AVAILABLE!", arrow_x + 22, arrow_y + 3, upg_style );
    }

    // Points indicator next to UPGRADES button (only when affordable)
    if ( i == MAINMENU_BTN_IDX_UPGRADES && has_global_upgrades )
    {
      float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
      int arrow_x = item->calc_x + item->w + 12;
      int arrow_y = item->calc_y + item->h / 2 + (int)bob;
      a_DrawFilledTriangle(
        arrow_x, arrow_y,
        arrow_x + 16, arrow_y - 10,
        arrow_x + 16, arrow_y + 10,
        (aColor_t){80, 220, 80, 255}
      );
      aTextStyle_t pts_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {80, 220, 80, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.45f
      };
      a_DrawText( "UPGRADES", arrow_x + 22, arrow_y - 10, pts_style );
      a_DrawText( "AVAILABLE!", arrow_x + 22, arrow_y + 3, pts_style );
    }
  }

  // Hotkeys to the left of the menu panel
  draw_hotkeys( mainmenu_flex->x / 2,
                mainmenu_flex->y + (mainmenu_flex->h - 160) / 2 );

  // Stats panel to the right of the menu panel
  {
    int right_edge = mainmenu_flex->x + mainmenu_flex->w;
    int stats_x = right_edge + (SCREEN_WIDTH - right_edge) / 2;
    int stats_cy = mainmenu_flex->y + mainmenu_flex->h / 2;

    aTextStyle_t title_st = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 220, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    aTextStyle_t sub_best = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 200, 60, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    aTextStyle_t sub_avg = {
      .type = FONT_ENTER_COMMAND,
      .fg = {80, 180, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    aTextStyle_t sl_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {160, 160, 180, 220},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.55f
    };
    aTextStyle_t best_val = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 220, 80, 255},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.55f
    };
    aTextStyle_t avg_val = {
      .type = FONT_ENTER_COMMAND,
      .fg = {100, 200, 255, 255},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.55f
    };

    int line_h = 26;
    int col_w = 220;
    int lx = stats_x - col_w / 2;
    int rx = stats_x + col_w / 2;
    char buf[64];

    // Gather data
    int best_sc = stats_get_best_score();
    float bt = stats_get_best_time();
    int bt_m = (int)bt / 60;
    int bt_s = (int)bt % 60;
    int best_k = stats_get_best_kills();
    int lifetime_kills = 0;
    for ( int t = 0; t < ENEMY_TYPE_COUNT; t++ )
      lifetime_kills += progress_get_lifetime_kills( (EnemyType_t)t );
    int runs = stats_get_total_runs();
    float total_t = stats_get_total_time();
    int tt_m = (int)total_t / 60;
    int tt_s = (int)total_t % 60;
    float avg_kills = (runs > 0) ? (float)lifetime_kills / runs : 0;
    float avg_time  = (runs > 0) ? total_t / runs : 0;
    int at_m = (int)avg_time / 60;
    int at_s = (int)avg_time % 60;
    float kpm = (total_t > 0) ? (lifetime_kills * 60.0f) / total_t : 0;

    int sy = stats_cy - 170;

    // --- BEST section ---
    a_DrawText( "BEST", stats_x, sy, sub_best );
    sy += 26;

    a_DrawText( "Score", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d", best_sc );
    a_DrawText( buf, rx, sy, best_val );
    sy += line_h;

    a_DrawText( "Kills", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d", best_k );
    a_DrawText( buf, rx, sy, best_val );
    sy += line_h;

    a_DrawText( "Time", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d:%02d", bt_m, bt_s );
    a_DrawText( buf, rx, sy, best_val );
    sy += line_h;

    // Divider
    sy += 4;
    a_DrawFilledRect(
      (aRectf_t){(float)lx, (float)sy, (float)col_w, 1},
      (aColor_t){100, 100, 120, 150}
    );
    sy += 10;

    // --- TOTALS section ---
    a_DrawText( "TOTALS", stats_x, sy, title_st );
    sy += 26;

    a_DrawText( "Kills", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d", lifetime_kills );
    a_DrawText( buf, rx, sy, best_val );
    sy += line_h;

    a_DrawText( "Runs", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d", runs );
    a_DrawText( buf, rx, sy, best_val );
    sy += line_h;

    a_DrawText( "Time", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d:%02d", tt_m, tt_s );
    a_DrawText( buf, rx, sy, best_val );
    sy += line_h;

    // Divider
    sy += 4;
    a_DrawFilledRect(
      (aRectf_t){(float)lx, (float)sy, (float)col_w, 1},
      (aColor_t){100, 100, 120, 150}
    );
    sy += 10;

    // --- AVERAGES section ---
    a_DrawText( "AVERAGES", stats_x, sy, sub_avg );
    sy += 26;

    a_DrawText( "Kills/Run", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%.1f", avg_kills );
    a_DrawText( buf, rx, sy, avg_val );
    sy += line_h;

    a_DrawText( "Time/Run", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%d:%02d", at_m, at_s );
    a_DrawText( buf, rx, sy, avg_val );
    sy += line_h;

    a_DrawText( "Kills/Min", lx, sy, sl_style );
    snprintf( buf, sizeof(buf), "%.1f", kpm );
    a_DrawText( buf, rx, sy, avg_val );
  }
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
  int panel_h = 274;
  pause_flex = a_FlexBoxCreate(
    (SCREEN_WIDTH - panel_w) / 2,
    (SCREEN_HEIGHT - panel_h) / 2,
    panel_w, panel_h
  );
  a_FlexConfigure( pause_flex, FLEX_DIR_COLUMN, FLEX_JUSTIFY_CENTER, 14 );
  a_FlexSetAlign( pause_flex, FLEX_ALIGN_CENTER );
  a_FlexSetPadding( pause_flex, 20 );
  a_FlexAddItem( pause_flex, PAUSE_BTN_W, PAUSE_BTN_H, NULL );  // Continue
  a_FlexAddItem( pause_flex, PAUSE_BTN_W, PAUSE_BTN_H, NULL );  // Main Menu
  a_FlexAddItem( pause_flex, PAUSE_BTN_W, PAUSE_BTN_H, NULL );  // Settings
  a_FlexAddItem( pause_flex, PAUSE_BTN_W, PAUSE_BTN_H, NULL );  // Quit
  a_FlexLayout( pause_flex );
}

static void pause_do_main_menu(void)
{
  if ( pause_flex ) a_FlexBoxDestroy( &pause_flex );
  game_paused = 0;
  pause_confirming = 0;
  mainmenu_begin();
  current_scene = SCENE_MAIN_MENU;
}

static void pause_logic( void )
{
  if ( !pause_flex )
  {
    pause_begin();
    pause_confirming = 0;
  }

  // Escape: cancel confirmation, or unpause (custom — not in menu_update)
  if ( app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
    if ( pause_confirming )
    {
      pause_confirming = 0;
      pause_selected = 1;
    }
    else
    {
      game_paused = 0;
      if ( pause_flex ) a_FlexBoxDestroy( &pause_flex );
    }
    return;
  }

  // Confirmation sub-state: YES (0) / NO (1)
  if ( pause_confirming )
  {
    // Build rects from FlexBox items with +30 Y offset
    static MenuRect_t confirm_rects[2];
    const FlexItem_t* b0 = a_FlexGetItem( pause_flex, 0 );
    const FlexItem_t* b1 = a_FlexGetItem( pause_flex, 1 );
    if ( b0 ) confirm_rects[0] = (MenuRect_t){ b0->calc_x, b0->calc_y + 30, b0->w, b0->h };
    if ( b1 ) confirm_rects[1] = (MenuRect_t){ b1->calc_x, b1->calc_y + 30, b1->w, b1->h };

    static Menu_t cm;
    cm.selected = pause_selected;
    cm.count    = 2;
    cm.axis     = MENU_AXIS_VERTICAL;
    cm.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
    cm.flex     = NULL;
    cm.rects    = confirm_rects;

    MenuResult_t cr = menu_update( &cm );
    pause_selected = cm.selected;

    if ( cr == MENU_CONFIRM )
    {
      if ( pause_selected == 0 )
        pause_do_main_menu();
      else
      {
        pause_confirming = 0;
        pause_selected = 1;
      }
    }
    else if ( cr == MENU_BACK )
    {
      pause_confirming = 0;
      pause_selected = 1;
    }
    return;
  }

  // Normal pause menu
  static Menu_t pm;
  pm.selected = pause_selected;
  pm.count    = PAUSE_BTN_COUNT;
  pm.axis     = MENU_AXIS_VERTICAL;
  pm.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
  pm.flex     = pause_flex;
  pm.rects    = NULL;

  MenuResult_t r = menu_update( &pm );
  pause_selected = pm.selected;

  if ( r == MENU_CONFIRM )
  {
    if ( pause_selected == 0 )
    {
      game_paused = 0;
      if ( pause_flex ) a_FlexBoxDestroy( &pause_flex );
    }
    else if ( pause_selected == 1 )
    {
      pause_confirming = 1;
      pause_selected = 1;
    }
    else if ( pause_selected == 2 )
    {
      if ( pause_flex ) a_FlexBoxDestroy( &pause_flex );
      game_paused = 0;
      settings_return_scene = SCENE_GAME;
      settings_sel = 0;
      settings_confirm = 0;
      current_scene = SCENE_SETTINGS;
    }
    else
    {
      app.running = 0;
    }
    return;
  }
}

// Draws the weapon stats panel at the given anchor position
static void draw_weapon_stats_panel(int anchor_x, int anchor_y, int anchor_h)
{
  int wcount = weapons_get_count();
  int wp_w = 240;

  // Calculate full height including bonus sources
  int wp_h = 36 + wcount * 56;
  WeaponType_t bonus_sources_pre[] = { SOURCE_FIRE_CONE, SOURCE_CONDUCTOR, SOURCE_GRUNT_EXPLOSION };
  int num_bonus_pre = sizeof(bonus_sources_pre) / sizeof(bonus_sources_pre[0]);
  int bonus_count = 0;
  for (int i = 0; i < num_bonus_pre; i++) {
    if (weapons_get_total_hits(bonus_sources_pre[i]) > 0) bonus_count++;
  }
  if (bonus_count > 0) {
    wp_h += 14 + 24 + bonus_count * 56;
  }

  (void)anchor_h;
  int wp_x = anchor_x;
  int wp_y = anchor_y;

  // Shift up if panel would overflow bottom of screen
  if (wp_y + wp_h > SCREEN_HEIGHT - 10) {
    wp_y = SCREEN_HEIGHT - 10 - wp_h;
  }

  // Panel background
  a_DrawFilledRect(
    (aRectf_t){(float)wp_x, (float)wp_y, (float)wp_w, (float)wp_h},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){(float)wp_x, (float)wp_y, (float)wp_w, (float)wp_h},
    (aColor_t){150, 150, 200, 255}
  );

  // Title
  aTextStyle_t wt_title = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.6f
  };
  a_DrawText( "WEAPON STATS", wp_x + wp_w / 2, wp_y + 10, wt_title );

  int wy = wp_y + 36;
  for ( int i = 0; i < wcount; i++ )
  {
    const Weapon_t* slot = weapons_get_slot(i);
    if ( !slot || slot->type == WEAPON_NONE ) continue;

    WeaponType_t wtype = slot->type;
    const char* wname = weapons_get_name(wtype);
    int total = weapons_get_total_hits(wtype);
    float dps = weapons_get_dps(wtype);

    aTextStyle_t wn_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {220, 220, 240, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.5f
    };
    a_DrawText( wname, wp_x + 12, wy, wn_style );

    char hits_buf[48];
    snprintf( hits_buf, sizeof(hits_buf), "Hits: %d", total );
    aTextStyle_t wv_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {160, 160, 180, 220},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.45f
    };
    a_DrawText( hits_buf, wp_x + 12, wy + 18, wv_style );

    char dps_buf[48];
    snprintf( dps_buf, sizeof(dps_buf), "HPS: %.1f", dps );
    aTextStyle_t dps_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {80, 200, 255, 255},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.55f
    };
    a_DrawText( dps_buf, wp_x + wp_w - 12, wy + 16, dps_style );

    if ( i < wcount - 1 )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)(wp_x + 10), (float)(wy + 44),
                   (float)(wp_w - 20), 1},
        (aColor_t){100, 100, 130, 100}
      );
    }

    wy += 56;
  }

  // Non-weapon damage sources
  WeaponType_t bonus_sources[] = { SOURCE_FIRE_CONE, SOURCE_CONDUCTOR, SOURCE_GRUNT_EXPLOSION };
  int num_bonus = sizeof(bonus_sources) / sizeof(bonus_sources[0]);
  int has_bonus = 0;
  for (int i = 0; i < num_bonus; i++) {
    if (weapons_get_total_hits(bonus_sources[i]) > 0) { has_bonus = 1; break; }
  }
  if (has_bonus) {
    a_DrawFilledRect(
      (aRectf_t){(float)(wp_x + 10), (float)(wy + 4),
                 (float)(wp_w - 20), 1},
      (aColor_t){100, 100, 130, 100}
    );
    wy += 14;

    aTextStyle_t bonus_hdr = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 180, 80, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( "BONUS", wp_x + wp_w / 2, wy, bonus_hdr );
    wy += 24;

    for (int i = 0; i < num_bonus; i++) {
      int total = weapons_get_total_hits(bonus_sources[i]);
      if (total <= 0) continue;
      float dps = weapons_get_dps(bonus_sources[i]);
      const char* bname = weapons_get_name(bonus_sources[i]);

      aTextStyle_t bn_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {220, 220, 240, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.5f
      };
      a_DrawText( bname, wp_x + 12, wy, bn_style );

      char bhits_buf[48];
      snprintf( bhits_buf, sizeof(bhits_buf), "Hits: %d", total );
      aTextStyle_t bv_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {160, 160, 180, 220},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.45f
      };
      a_DrawText( bhits_buf, wp_x + 12, wy + 18, bv_style );

      char bdps_buf[48];
      snprintf( bdps_buf, sizeof(bdps_buf), "HPS: %.1f", dps );
      aTextStyle_t bdps_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {80, 200, 255, 255},
        .align = TEXT_ALIGN_RIGHT,
        .scale = 0.55f
      };
      a_DrawText( bdps_buf, wp_x + wp_w - 12, wy + 16, bdps_style );

      wy += 56;
    }
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

  // Title above the panel
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.9f
  };
  a_DrawText( pause_confirming ? "ARE YOU SURE?" : "GAME PAUSED",
              SCREEN_WIDTH / 2, pause_flex->y - 36, title_style );

  if ( pause_confirming )
  {
    // Warning text
    aTextStyle_t warn_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 100, 100, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText( "You will lose this run!",
                SCREEN_WIDTH / 2, pause_flex->y + 10, warn_style );

    // Draw only first two flex items as YES / NO
    const char* confirm_labels[2] = { "YES, QUIT RUN", "NO, GO BACK" };
    for ( int i = 0; i < 2; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( pause_flex, i );
      if ( !item ) continue;

      // Offset buttons down to make room for warning text
      int draw_y = item->calc_y + 30;

      int hovered = ( i == pause_selected );
      if ( !hovered )
        hovered = point_in_rect( app.mouse.x, app.mouse.y,
                                 item->calc_x, draw_y, item->w, item->h );

      aColor_t btn_bg = hovered
        ? (i == 0 ? (aColor_t){110, 50, 50, 255} : (aColor_t){70, 70, 110, 255})
        : (aColor_t){45, 45, 65, 255};
      aColor_t btn_border = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){120, 120, 150, 200};

      a_DrawFilledRect(
        (aRectf_t){(float)item->calc_x, (float)draw_y,
                   (float)item->w, (float)item->h},
        btn_bg
      );
      a_DrawRect(
        (aRectf_t){(float)item->calc_x, (float)draw_y,
                   (float)item->w, (float)item->h},
        btn_border
      );

      aTextStyle_t btn_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 180, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.55f
      };
      a_DrawText( confirm_labels[i],
                  item->calc_x + item->w / 2,
                  draw_y + 10,
                  btn_style );
    }
  }
  else
  {
    // Normal pause buttons
    const char* labels[4] = { "CONTINUE", "MAIN MENU", "SETTINGS", "QUIT" };
    int has_upgrades = progress_has_affordable_upgrade() || global_has_affordable();

    for ( int i = 0; i < PAUSE_BTN_COUNT; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( pause_flex, i );
      if ( !item ) continue;

      int hovered = ( i == pause_selected );
      if ( !hovered )
        hovered = point_in_rect( app.mouse.x, app.mouse.y,
                                 item->calc_x, item->calc_y, item->w, item->h );

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

      // Upgrade indicator to the left of MAIN MENU button
      if ( i == 1 && has_upgrades )
      {
        float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
        int arrow_x = item->calc_x - 12;
        int arrow_y = item->calc_y + item->h / 2 + (int)bob;
        a_DrawFilledTriangle(
          arrow_x, arrow_y,
          arrow_x - 16, arrow_y - 10,
          arrow_x - 16, arrow_y + 10,
          (aColor_t){80, 220, 80, 255}
        );
        aTextStyle_t upg_style = {
          .type = FONT_ENTER_COMMAND,
          .fg = {80, 220, 80, 255},
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.45f
        };
        a_DrawText( "UPGRADES", arrow_x - 22, arrow_y - 10, upg_style );
        a_DrawText( "AVAILABLE!", arrow_x - 22, arrow_y + 3, upg_style );
      }
    }
  }

  draw_hotbar_tooltip();

  // Hotkeys to the left of the pause panel
  draw_hotkeys( pause_flex->x / 2,
                pause_flex->y + (pause_flex->h - 160) / 2 );

  // Weapon stats panel to the right of pause panel
  draw_weapon_stats_panel(
    pause_flex->x + pause_flex->w + 30,
    pause_flex->y,
    pause_flex->h );
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
  free_upgrade_active = 0;

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
    play_menu_move();
    app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
    app.keyboard[ SDL_SCANCODE_A ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_RIGHT ] == 1 || app.keyboard[ SDL_SCANCODE_D ] == 1 )
  {
    level_up_selected++;
    if ( level_up_selected >= level_up_card_count ) level_up_selected = 0;
    play_menu_move();
    app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
    app.keyboard[ SDL_SCANCODE_D ] = 0;
  }

  // Enter or Space to confirm selection
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 || app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    play_menu_click();
    level_up_select( level_up_selected );
    return;
  }

  // Keyboard: 1, 2, 3 (direct select)
  if ( app.keyboard[ SDL_SCANCODE_1 ] == 1 && level_up_card_count >= 1 )
  {
    play_menu_click();
    level_up_select( 0 );
    app.keyboard[ SDL_SCANCODE_1 ] = 0;
    return;
  }
  if ( app.keyboard[ SDL_SCANCODE_2 ] == 1 && level_up_card_count >= 2 )
  {
    play_menu_click();
    level_up_select( 1 );
    app.keyboard[ SDL_SCANCODE_2 ] = 0;
    return;
  }
  if ( app.keyboard[ SDL_SCANCODE_3 ] == 1 && level_up_card_count >= 3 )
  {
    play_menu_click();
    level_up_select( 2 );
    app.keyboard[ SDL_SCANCODE_3 ] = 0;
    return;
  }

  // R key to reroll
  if ( app.keyboard[ SDL_SCANCODE_R ] == 1 && upgrades_get_rerolls() > 0 && !free_upgrade_active )
  {
    app.keyboard[ SDL_SCANCODE_R ] = 0;
    upgrades_use_reroll();
    level_up_card_count = upgrades_roll_cards( level_up_cards );
    level_up_selected = 0;
    if ( levelup_flex ) { a_FlexBoxDestroy( &levelup_flex ); }
    level_up_begin();
  }

  // Mouse click on card (or reroll button)
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    int mx = app.mouse.x;
    int my = app.mouse.y;

    // Check reroll button click
    if ( upgrades_get_rerolls() > 0 && levelup_flex && !free_upgrade_active )
    {
      int btn_w = 140, btn_h = 32;
      int btn_x = SCREEN_WIDTH / 2 - btn_w / 2;
      int btn_y = levelup_flex->y + levelup_flex->h + 12;
      if ( point_in_rect( mx, my, btn_x, btn_y, btn_w, btn_h ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        upgrades_use_reroll();
        level_up_card_count = upgrades_roll_cards( level_up_cards );
        level_up_selected = 0;
        if ( levelup_flex ) { a_FlexBoxDestroy( &levelup_flex ); }
        level_up_begin();
        return;
      }
    }

    for ( int i = 0; i < level_up_card_count; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( levelup_flex, i );
      if ( item && point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
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

  if ( free_upgrade_active )
  {
    // "FREE UPGRADE!" title (green)
    aTextStyle_t title_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {100, 255, 100, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 1.2f
    };
    a_DrawText( "FREE UPGRADE!", SCREEN_WIDTH / 2,
                levelup_flex->y - 62, title_style );

    // Weapon name subtitle
    const UpgradeInfo_t* info = upgrades_get_info( level_up_cards[0] );
    char sub_text[64];
    snprintf( sub_text, sizeof(sub_text), "New Weapon: %s", info ? info->weapon_name : "?" );
    aTextStyle_t sub_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 200, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( sub_text, SCREEN_WIDTH / 2,
                levelup_flex->y - 34, sub_style );
  }
  else
  {
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
  }

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
      .scale = 0.5f
    };
    a_DrawText( info->weapon_name, cx, top + 14, weapon_style );

    // Upgrade name
    aTextStyle_t name_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.7f
    };
    a_DrawText( info->upgrade_name, cx, top + 38, name_style );

    // Tier label ("I", "II", "III")
    const char* tier_labels[] = { "I", "II", "III" };
    const char* tier_str = (tier < 3) ? tier_labels[tier] : "MAX";
    aTextStyle_t tier_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = border_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    a_DrawText( tier_str, cx, top + 68, tier_style );

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
      .scale = 0.4f
    };
    a_DrawText( rarity_str, cx, top + 92, rarity_style );

    // Separator line
    a_DrawLine(
      item->calc_x + 10, top + 112,
      item->calc_x + item->w - 10, top + 112,
      (aColor_t){255, 255, 255, 40}
    );

    // Description (wrapped)
    if ( tier < UPG_MAX_TIER && info->descriptions[tier] )
    {
      aTextStyle_t desc_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 200, 220},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.45f,
        .wrap_width = item->w - 20
      };
      a_DrawText( info->descriptions[tier], cx, top + 122, desc_style );
    }

    // Key hint at bottom
    char key_text[16];
    snprintf( key_text, sizeof(key_text), "[%d]", i + 1 );
    aTextStyle_t key_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = selected ? (aColor_t){255, 255, 255, 255} : (aColor_t){150, 150, 150, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    a_DrawText( key_text, cx, top + item->h - 32, key_style );
  }

  // Reroll button (not during free upgrade)
  if ( upgrades_get_rerolls() > 0 && !free_upgrade_active )
  {
    int btn_w = 140, btn_h = 32;
    int btn_x = SCREEN_WIDTH / 2 - btn_w / 2;
    int btn_y = levelup_flex->y + levelup_flex->h + 12;

    // Button background
    a_DrawFilledRect(
      (aRectf_t){(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
      (aColor_t){50, 50, 80, 230}
    );
    a_DrawRect(
      (aRectf_t){(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
      (aColor_t){180, 180, 255, 200}
    );

    // Button text
    char reroll_text[32];
    snprintf( reroll_text, sizeof(reroll_text), "REROLL (%d)  [R]", upgrades_get_rerolls() );
    aTextStyle_t reroll_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( reroll_text, SCREEN_WIDTH / 2, btn_y + btn_h / 2 - 2, reroll_style );
  }
}

// ============================================================================
// Game Over Scene
// ============================================================================

static void scene_game_over_logic( float dt )
{
  (void)dt;

  if ( !gameover_flex ) return;

  static Menu_t m;
  m.selected = gameover_sel;
  m.count    = GAMEOVER_BTN_COUNT;
  m.axis     = MENU_AXIS_VERTICAL;
  m.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
  m.flex     = gameover_flex;
  m.rects    = NULL;

  MenuResult_t r = menu_update( &m );
  gameover_sel = m.selected;

  if ( r == MENU_CONFIRM )
  {
    if ( gameover_sel == GAMEOVER_BTN_IDX_RETRY )
    {
      game_reset();
      return;
    }
    else if ( gameover_sel == GAMEOVER_BTN_IDX_MENU )
    {
      if ( gameover_flex ) a_FlexBoxDestroy( &gameover_flex );
      game_audio_restart_music();
      mainmenu_begin();
      current_scene = SCENE_MAIN_MENU;
      return;
    }
    else if ( gameover_sel == GAMEOVER_BTN_IDX_SETTINGS )
    {
      settings_return_scene = SCENE_GAME_OVER;
      settings_sel = 0;
      settings_confirm = 0;
      current_scene = SCENE_SETTINGS;
      return;
    }
    else if ( gameover_sel == GAMEOVER_BTN_IDX_QUIT )
    {
      app.running = 0;
      return;
    }
  }
}

static void scene_game_over_draw( float dt )
{
  (void)dt;

  if ( !gameover_flex ) return;

  // Dark overlay
  a_DrawFilledRect(
    (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
    (aColor_t){0, 0, 0, 180}
  );

  // Panel background
  float panel_x = (float)gameover_flex->x;
  float panel_y = (float)gameover_flex->y;
  float panel_w = (float)gameover_flex->w;
  float panel_h = (float)gameover_flex->h;

  a_DrawFilledRect(
    (aRectf_t){panel_x, panel_y, panel_w, panel_h},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){panel_x, panel_y, panel_w, panel_h},
    (aColor_t){200, 30, 30, 255}
  );

  // "GAME OVER" title
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {220, 30, 30, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.2f
  };
  a_DrawText( "GAME OVER", SCREEN_WIDTH / 2,
              (int)panel_y + 14, title_style );

  // Stats section
  aTextStyle_t label_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {160, 160, 180, 255},
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.45f
  };
  aTextStyle_t value_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_RIGHT,
    .scale = 0.45f
  };

  int left_x  = (int)panel_x + 24;
  int right_x = (int)(panel_x + panel_w) - 24;
  int stat_y  = (int)panel_y + 52;
  int line_h  = 22;

  char buf[64];

  // Time survived
  int mins = (int)go_time / 60;
  int secs = (int)go_time % 60;
  a_DrawText( "Time", left_x, stat_y, label_style );
  snprintf( buf, sizeof(buf), "%d:%02d", mins, secs );
  a_DrawText( buf, right_x, stat_y, value_style );
  stat_y += line_h;

  // Score
  a_DrawText( "Score", left_x, stat_y, label_style );
  snprintf( buf, sizeof(buf), "%d", go_score );
  a_DrawText( buf, right_x, stat_y, value_style );
  stat_y += line_h;

  // Kills
  a_DrawText( "Kills", left_x, stat_y, label_style );
  snprintf( buf, sizeof(buf), "%d", go_kills );
  a_DrawText( buf, right_x, stat_y, value_style );
  stat_y += line_h;

  // Level
  a_DrawText( "Level", left_x, stat_y, label_style );
  snprintf( buf, sizeof(buf), "%d", go_level );
  a_DrawText( buf, right_x, stat_y, value_style );
  stat_y += line_h + 6;

  // Divider line
  a_DrawFilledRect(
    (aRectf_t){panel_x + 20, (float)stat_y, panel_w - 40, 1},
    (aColor_t){100, 100, 120, 200}
  );
  stat_y += 8;

  // Best score
  aTextStyle_t best_label_style = label_style;
  best_label_style.fg = (aColor_t){200, 180, 80, 255};
  aTextStyle_t best_value_style = value_style;
  best_value_style.fg = (aColor_t){255, 220, 80, 255};

  int best_score_now = stats_get_best_score();
  int best_kills_now = stats_get_best_kills();
  float best_time_now = stats_get_best_time();
  int best_mins = (int)best_time_now / 60;
  int best_secs = (int)best_time_now % 60;

  aTextStyle_t new_best_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 220, 50, 255},
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.35f
  };

  a_DrawText( "Best Score", left_x, stat_y, best_label_style );
  snprintf( buf, sizeof(buf), "%d", best_score_now );
  a_DrawText( buf, right_x, stat_y, best_value_style );
  if ( go_new_best_score ) a_DrawText( "NEW!", right_x + 6, stat_y, new_best_style );
  stat_y += line_h;

  a_DrawText( "Best Kills", left_x, stat_y, best_label_style );
  snprintf( buf, sizeof(buf), "%d", best_kills_now );
  a_DrawText( buf, right_x, stat_y, best_value_style );
  if ( go_new_best_kills ) a_DrawText( "NEW!", right_x + 6, stat_y, new_best_style );
  stat_y += line_h;

  a_DrawText( "Best Time", left_x, stat_y, best_label_style );
  snprintf( buf, sizeof(buf), "%d:%02d", best_mins, best_secs );
  a_DrawText( buf, right_x, stat_y, best_value_style );
  if ( go_new_best_time ) a_DrawText( "NEW!", right_x + 6, stat_y, new_best_style );
  stat_y += line_h;

  // Draw buttons
  int mx = app.mouse.x;
  int my = app.mouse.y;
  int has_upgrades = progress_has_affordable_upgrade() || global_has_affordable();

  const char* labels[4] = { "TRY AGAIN", "MAIN MENU", "SETTINGS", "QUIT" };
  for ( int i = 0; i < GAMEOVER_BTN_COUNT; i++ )
  {
    const FlexItem_t* item = a_FlexGetItem( gameover_flex, i );
    if ( !item ) continue;

    int hovered = point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h );
    int selected = (i == gameover_sel);
    int highlight = hovered || selected;

    aColor_t btn_bg = highlight ? (aColor_t){80, 80, 120, 255} : (aColor_t){50, 50, 70, 255};
    aColor_t btn_border = highlight ? (aColor_t){255, 255, 255, 255} : (aColor_t){150, 150, 150, 200};

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
      .fg = highlight ? (aColor_t){255, 255, 255, 255} : (aColor_t){200, 200, 200, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    a_DrawText( labels[i],
                item->calc_x + item->w / 2,
                item->calc_y + 10,
                btn_style );

    // Upgrade indicator arrow + text next to MAIN MENU button
    if ( i == GAMEOVER_BTN_IDX_MENU && has_upgrades )
    {
      float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
      int arrow_x = item->calc_x + item->w + 12;
      int arrow_y = item->calc_y + item->h / 2 + (int)bob;
      // Big triangle arrow pointing at button
      a_DrawFilledTriangle(
        arrow_x, arrow_y,
        arrow_x + 16, arrow_y - 10,
        arrow_x + 16, arrow_y + 10,
        (aColor_t){80, 220, 80, 255}
      );
      aTextStyle_t upg_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {80, 220, 80, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.5f
      };
      a_DrawText( "UPGRADES", arrow_x + 22, arrow_y - 12, upg_style );
      a_DrawText( "AVAILABLE!", arrow_x + 22, arrow_y + 2, upg_style );
    }
  }

  // Weapon stats panel (right of game over panel)
  draw_weapon_stats_panel(
    gameover_flex->x + gameover_flex->w + 30,
    gameover_flex->y,
    gameover_flex->h
  );

  // "KILLED BY" panel (left of game over panel)
  {
    int kb_w = 180;
    int kb_h = 220;
    float kb_x = (float)(gameover_flex->x - kb_w - 30);
    float kb_y = (float)gameover_flex->y;

    // Panel background
    a_DrawFilledRect(
      (aRectf_t){kb_x, kb_y, (float)kb_w, (float)kb_h},
      (aColor_t){30, 30, 50, 240}
    );
    a_DrawRect(
      (aRectf_t){kb_x, kb_y, (float)kb_w, (float)kb_h},
      (aColor_t){200, 30, 30, 255}
    );

    int kb_mid = (int)kb_x + kb_w / 2;
    int kb_ty = (int)kb_y + 14;

    // "KILLED BY" header
    aTextStyle_t kb_title = {
      .type = FONT_ENTER_COMMAND,
      .fg = {220, 30, 30, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.7f
    };
    a_DrawText( "KILLED BY", kb_mid, kb_ty, kb_title );
    kb_ty += 30;

    // Enemy shape swatch
    aColor_t shape_c = {0, 255, 0, 255};
    int shape_size = 30;

    switch (go_killer_type) {
      case ENEMY_TYPE_DASHER: {
        int half = shape_size / 2;
        a_DrawFilledTriangle(
          kb_mid, kb_ty,
          kb_mid - half, kb_ty + shape_size,
          kb_mid + half, kb_ty + shape_size,
          shape_c
        );
        break;
      }
      case ENEMY_TYPE_SHAMAN: {
        float arm_w = (float)shape_size * 0.3f;
        float arm_l = (float)shape_size / 2.0f;
        float fcx = (float)kb_mid;
        float fcy = (float)kb_ty + arm_l;
        a_DrawFilledRect(
          (aRectf_t){fcx - arm_w / 2.0f, fcy - arm_l, arm_w, (float)shape_size},
          shape_c
        );
        a_DrawFilledRect(
          (aRectf_t){fcx - arm_l, fcy - arm_w / 2.0f, (float)shape_size, arm_w},
          shape_c
        );
        break;
      }
      case ENEMY_TYPE_SNAKE: {
        // Square head like in-game snake
        int half = shape_size / 2;
        float hx = (float)(kb_mid - half);
        float hy = (float)kb_ty;
        a_DrawFilledRect(
          (aRectf_t){hx, hy, (float)shape_size, (float)shape_size},
          shape_c
        );
        // Eyes — two black squares facing downward
        float eye_sz = 4.0f;
        float eye_fwd = (float)shape_size * 0.35f;
        float eye_lat = (float)shape_size * 0.2f;
        float cx_s = hx + (float)half;
        float cy_s = hy + (float)half;
        a_DrawFilledRect(
          (aRectf_t){cx_s - eye_lat - eye_sz / 2, cy_s + eye_fwd - eye_sz / 2, eye_sz, eye_sz},
          (aColor_t){0, 0, 0, 255}
        );
        a_DrawFilledRect(
          (aRectf_t){cx_s + eye_lat - eye_sz / 2, cy_s + eye_fwd - eye_sz / 2, eye_sz, eye_sz},
          (aColor_t){0, 0, 0, 255}
        );
        break;
      }
      case ENEMY_TYPE_BRUTE: {
        int half = shape_size / 2;
        float bx = (float)(kb_mid - half);
        float by = (float)kb_ty;
        float sz = (float)shape_size;
        a_DrawFilledRect(
          (aRectf_t){bx, by, sz, sz},
          shape_c
        );
        int horn_h = (int)(sz * 0.4f);
        int horn_w = (int)(sz * 0.25f);
        a_DrawFilledTriangle(
          (int)(bx + sz * 0.2f), (int)by,
          (int)(bx - horn_w * 0.3f), (int)(by - horn_h),
          (int)bx, (int)by,
          shape_c
        );
        a_DrawFilledTriangle(
          (int)(bx + sz * 0.8f), (int)by,
          (int)(bx + sz + horn_w * 0.3f), (int)(by - horn_h),
          (int)(bx + sz), (int)by,
          shape_c
        );
        break;
      }
      default: {
        // Grunt — square
        int half = shape_size / 2;
        a_DrawFilledRect(
          (aRectf_t){(float)(kb_mid - half), (float)kb_ty, (float)shape_size, (float)shape_size},
          shape_c
        );
        break;
      }
    }
    kb_ty += shape_size + 14;

    // Enemy name
    static const char* killer_names[ENEMY_TYPE_COUNT] = {
      "GRUNT", "DASHER", "BRUTE", "SHAMAN", "SNAKE"
    };
    aTextStyle_t kb_name = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.7f
    };
    a_DrawText( killer_names[go_killer_type], kb_mid, kb_ty, kb_name );
    kb_ty += 26;

    // Damage line
    char kb_buf[64];
    aTextStyle_t kb_stat = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 220, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    snprintf( kb_buf, sizeof(kb_buf), "Damage: %d", go_killer_damage );
    a_DrawText( kb_buf, kb_mid, kb_ty, kb_stat );
    kb_ty += 22;

    // Divider
    a_DrawFilledRect(
      (aRectf_t){kb_x + 16, (float)kb_ty, (float)(kb_w - 32), 1},
      (aColor_t){100, 100, 120, 200}
    );
    kb_ty += 12;

    // Death count
    aTextStyle_t kb_death = {
      .type = FONT_ENTER_COMMAND,
      .fg = {220, 80, 80, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    snprintf( kb_buf, sizeof(kb_buf), "Killed you %d time%s",
              go_deaths_by_killer, go_deaths_by_killer == 1 ? "" : "s" );
    a_DrawText( kb_buf, kb_mid, kb_ty, kb_death );
  }
}

// ============================================================================
// Weapons Scene
// ============================================================================

#define WTYPE_CARD_W     290
#define WTYPE_CARD_H     560
#define WTYPE_CARD_GAP   14
#define WTYPE_PAGE_COUNT 2
#define WTYPE_ACCENT     ((aColor_t){100, 180, 255, 255})

static int wtype_selected = 0;
static int wtype_page = 0;
static int wtype_modal_open = 0;
static int wtype_modal_wid = 0;
static int wtype_modal_track_sel = 0;

// Page 1: 4 cards (Wand, Spin, Chain, Orbit)
// Page 2: 3 cards (Bomb, Turret, Trail)
static const WeaponId_t wtype_page1[] = { WID_WAND, WID_SPIN, WID_CHAIN, WID_ORBIT };
static const WeaponId_t wtype_page2[] = { WID_BOMB, WID_TURRET, WID_TRAIL };
#define WTYPE_PAGE1_COUNT 4
#define WTYPE_PAGE2_COUNT 3

// Track mapping: 3 upgrades per weapon (cooldown, reach, free upgrade)
#define WTYPE_TRACK_COUNT 3
static const WeaponProgressId_t wtype_tracks[WID_COUNT][WTYPE_TRACK_COUNT] = {
  { WPROG_WAND_COOLDOWN,   WPROG_WAND_REACH,   WPROG_WAND_FREE_UPG },
  { WPROG_SPIN_COOLDOWN,   WPROG_SPIN_REACH,   WPROG_SPIN_FREE_UPG },
  { WPROG_CHAIN_COOLDOWN,  WPROG_CHAIN_REACH,  WPROG_CHAIN_FREE_UPG },
  { WPROG_ORBIT_COOLDOWN,  WPROG_ORBIT_REACH,  WPROG_ORBIT_FREE_UPG },
  { WPROG_BOMB_COOLDOWN,   WPROG_BOMB_REACH,   WPROG_BOMB_FREE_UPG },
  { WPROG_TURRET_COOLDOWN, WPROG_TURRET_REACH, WPROG_TURRET_FREE_UPG },
  { WPROG_TRAIL_COOLDOWN,  WPROG_TRAIL_REACH,  WPROG_TRAIL_FREE_UPG },
};

static const char* wtype_descs[WID_COUNT] = {
  "Fires bullets in the aim direction",
  "Damages nearby enemies in a circle",
  "Lightning arcs between nearby enemies",
  "Shields orbit the player",
  "Throws bombs that explode on impact",
  "Stationary turret that auto-fires",
  "Leaves a damaging trail behind you",
};

// Weapon colors (same as drops.c)
static const aColor_t wtype_colors[WID_COUNT] = {
  [WID_WAND]   = {100, 150, 255, 255},
  [WID_SPIN]   = {255, 200, 50,  255},
  [WID_CHAIN]  = {200, 220, 255, 255},
  [WID_ORBIT]  = {255, 160, 50,  255},
  [WID_BOMB]   = {255, 180, 50,  255},
  [WID_TURRET] = {255, 180, 50,  255},
  [WID_TRAIL]  = {255, 120, 30,  255},
};

// Weapon labels (same as drops.c)
static const char* wtype_labels[WID_COUNT] = {
  [WID_WAND]   = "WAND",
  [WID_SPIN]   = "SPIN",
  [WID_CHAIN]  = "BOLT",
  [WID_ORBIT]  = "ORB",
  [WID_BOMB]   = "BOMB",
  [WID_TURRET] = "TRRT",
  [WID_TRAIL]  = "FIRE",
};

// Draw weapon icon — colored square with label text (matches in-game drops)
static void wtype_draw_icon( WeaponId_t wid, int cx, int cy, int size )
{
  int half = size / 2;
  float bx = (float)(cx - half);
  float by = (float)(cy - half);
  float sz = (float)size;
  aColor_t col = wtype_colors[wid];

  // Glow behind
  a_DrawFilledRect(
    (aRectf_t){bx - 3.0f, by - 3.0f, sz + 6.0f, sz + 6.0f},
    (aColor_t){col.r, col.g, col.b, 80}
  );

  // Filled square
  a_DrawFilledRect( (aRectf_t){bx, by, sz, sz}, col );
  a_DrawRect( (aRectf_t){bx, by, sz, sz}, (aColor_t){255, 255, 255, 220} );

  // Label text (black, centered)
  aTextStyle_t label_s = {
    .type = FONT_ENTER_COMMAND,
    .fg = {0, 0, 0, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.55f
  };
  a_DrawText( wtype_labels[wid], cx, cy - 6, label_s );
}

static void scene_weapons_logic( float dt )
{
  (void)dt;

  // Get current page data
  const WeaponId_t* page_items = (wtype_page == 0) ? wtype_page1 : wtype_page2;
  int page_count = (wtype_page == 0) ? WTYPE_PAGE1_COUNT : WTYPE_PAGE2_COUNT;

  // Modal logic
  if ( wtype_modal_open )
  {
    int pw = 500, ph = 420;
    int mpx = (SCREEN_WIDTH - pw) / 2;
    int mpy = (SCREEN_HEIGHT - ph) / 2;

    static MenuRect_t wm_rects[WTYPE_TRACK_COUNT];
    for ( int t = 0; t < WTYPE_TRACK_COUNT; t++ )
      wm_rects[t] = (MenuRect_t){ mpx + 20, mpy + 80 + t * 80, pw - 40, 72 };

    static Menu_t wm;
    wm.selected = wtype_modal_track_sel;
    wm.count    = WTYPE_TRACK_COUNT;
    wm.axis     = MENU_AXIS_VERTICAL;
    wm.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
    wm.flex     = NULL;
    wm.rects    = wm_rects;

    MenuResult_t mr = menu_update( &wm );
    wtype_modal_track_sel = wm.selected;

    if ( mr == MENU_BACK )
    {
      wtype_modal_open = 0;
      return;
    }
    if ( mr == MENU_CONFIRM )
    {
      WeaponProgressId_t id = wtype_tracks[wtype_modal_wid][wtype_modal_track_sel];
      if ( wprog_can_afford(id) )
      {
        wprog_purchase(id);
      }
    }
    return;
  }

  // ESC / Backspace / click Back button → return to main menu
  {
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = SCREEN_HEIGHT - 40;
    int esc = app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 || app.keyboard[ SDL_SCANCODE_BACKSPACE ] == 1;
    int clicked = app.mouse.button == 1 && app.mouse.pressed &&
                  point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( esc || clicked )
    {
      app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
      app.keyboard[ SDL_SCANCODE_BACKSPACE ] = 0;
      if ( clicked ) app.mouse.button = 0;
      play_menu_click();
      current_scene = SCENE_MAIN_MENU;
      return;
    }
  }

  // Left/Right/A/D — navigate cards
  if ( app.keyboard[ SDL_SCANCODE_A ] == 1 || app.keyboard[ SDL_SCANCODE_LEFT ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_A ] = 0;
    app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
    if ( wtype_selected > 0 )
    {
      wtype_selected--;
      play_menu_move();
    }
    else if ( wtype_page > 0 )
    {
      wtype_page--;
      int prev_count = (wtype_page == 0) ? WTYPE_PAGE1_COUNT : WTYPE_PAGE2_COUNT;
      wtype_selected = prev_count - 1;
      play_menu_move();
    }
  }
  if ( app.keyboard[ SDL_SCANCODE_D ] == 1 || app.keyboard[ SDL_SCANCODE_RIGHT ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_D ] = 0;
    app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
    if ( wtype_selected < page_count - 1 )
    {
      wtype_selected++;
      play_menu_move();
    }
    else if ( wtype_page < WTYPE_PAGE_COUNT - 1 )
    {
      wtype_page++;
      wtype_selected = 0;
      play_menu_move();
    }
  }

  // Enter/Space — open modal
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 || app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    WeaponId_t wid = page_items[wtype_selected];
    wtype_modal_open = 1;
    wtype_modal_wid = wid;
    wtype_modal_track_sel = 0;
    play_menu_click();
  }

  // Mouse: click page arrows
  if ( app.mouse.button == 1 && app.mouse.pressed )
  {
    int mx = app.mouse.x, my = app.mouse.y;
    int page_y = SCREEN_HEIGHT - 60;
    int arrow_w = 40, arrow_h = 30;
    int left_ax = SCREEN_WIDTH / 2 - 100 - arrow_w / 2;
    int right_ax = SCREEN_WIDTH / 2 + 100 - arrow_w / 2;
    int arrow_ay = page_y - 4;

    if ( wtype_page > 0 && point_in_rect( mx, my, left_ax, arrow_ay, arrow_w, arrow_h ) )
    {
      app.mouse.button = 0;
      wtype_page--;
      wtype_selected = 0;
      play_menu_click();
    }
    else if ( wtype_page < WTYPE_PAGE_COUNT - 1 && point_in_rect( mx, my, right_ax, arrow_ay, arrow_w, arrow_h ) )
    {
      app.mouse.button = 0;
      wtype_page++;
      wtype_selected = 0;
      play_menu_click();
    }
  }

  // Mouse: hover + click cards
  {
    int mx = app.mouse.x, my = app.mouse.y;
    int card_y = (SCREEN_HEIGHT - WTYPE_CARD_H) / 2;
    int total_w = page_count * WTYPE_CARD_W + (page_count - 1) * WTYPE_CARD_GAP;
    int start_x = (SCREEN_WIDTH - total_w) / 2;

    for ( int i = 0; i < page_count; i++ )
    {
      int cx = start_x + i * (WTYPE_CARD_W + WTYPE_CARD_GAP);
      if ( point_in_rect( mx, my, cx, card_y, WTYPE_CARD_W, WTYPE_CARD_H ) )
      {
        if ( mouse_moved_this_frame && wtype_selected != i )
        {
          wtype_selected = i;
          play_menu_move();
        }
        if ( app.mouse.button == 1 && app.mouse.pressed )
        {
          app.mouse.button = 0;
          wtype_selected = i;
          WeaponId_t wid = page_items[i];
          wtype_modal_open = 1;
          wtype_modal_wid = wid;
          wtype_modal_track_sel = 0;
          play_menu_click();
        }
        break;
      }
    }
  }
}

static void scene_weapons_draw( float dt )
{
  (void)dt;

  // Title
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.2f
  };
  a_DrawText( "WEAPONS", SCREEN_WIDTH / 2, 24, title_style );

  // Current page data
  const WeaponId_t* page_items = (wtype_page == 0) ? wtype_page1 : wtype_page2;
  int page_count = (wtype_page == 0) ? WTYPE_PAGE1_COUNT : WTYPE_PAGE2_COUNT;

  int card_y = (SCREEN_HEIGHT - WTYPE_CARD_H) / 2;
  int total_w = page_count * WTYPE_CARD_W + (page_count - 1) * WTYPE_CARD_GAP;
  int start_x = (SCREEN_WIDTH - total_w) / 2;

  for ( int i = 0; i < page_count; i++ )
  {
    WeaponId_t wid = page_items[i];
    int cx = start_x + i * (WTYPE_CARD_W + WTYPE_CARD_GAP);
    int selected = (i == wtype_selected);

    // Card background
    aColor_t bg = selected ? (aColor_t){40, 50, 80, 240} : (aColor_t){25, 30, 50, 240};
    a_DrawFilledRect(
      (aRectf_t){(float)cx, (float)card_y, (float)WTYPE_CARD_W, (float)WTYPE_CARD_H},
      bg
    );

    // Border
    aColor_t border = selected ? WTYPE_ACCENT : (aColor_t){80, 100, 140, 200};
    a_DrawRect(
      (aRectf_t){(float)cx, (float)card_y, (float)WTYPE_CARD_W, (float)WTYPE_CARD_H},
      border
    );
    if ( selected )
    {
      a_DrawRect(
        (aRectf_t){(float)(cx + 1), (float)(card_y + 1),
                   (float)(WTYPE_CARD_W - 2), (float)(WTYPE_CARD_H - 2)},
        border
      );
    }

    int mid_x = cx + WTYPE_CARD_W / 2;
    int ty = card_y + 20;

    // Weapon name
    aTextStyle_t name_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.8f
    };
    a_DrawText( wprog_get_weapon_name(wid), mid_x, ty, name_style );
    ty += 40;

    // Shape icon
    wtype_draw_icon( wid, mid_x, ty + 13, 30 );
    ty += 26 + 22;

    // Description
    aTextStyle_t desc_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 200, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f,
      .wrap_width = WTYPE_CARD_W - 30
    };
    a_DrawText( wtype_descs[wid], mid_x, ty, desc_style );
    ty += 48;

    // Separator
    a_DrawFilledRect(
      (aRectf_t){(float)(cx + 16), (float)ty, (float)(WTYPE_CARD_W - 32), 1},
      (aColor_t){255, 255, 255, 40}
    );
    ty += 16;

    // "UPGRADE TRACKS" subheader
    aTextStyle_t sub_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = WTYPE_ACCENT,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.6f
    };
    a_DrawText( "UPGRADE TRACKS", mid_x, ty, sub_style );
    ty += 34;

    // Show upgrade tracks with current tier pips
    for ( int t = 0; t < WTYPE_TRACK_COUNT; t++ )
    {
      WeaponProgressId_t pid = wtype_tracks[wid][t];
      int tier = wprog_get_tier(pid);
      int max_t = wprog_get_max_tier(pid);

      aTextStyle_t track_name = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 220, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.5f
      };
      a_DrawText( wprog_get_name(pid), mid_x, ty, track_name );
      ty += 20;

      // Tier pips centered
      int pip_total = max_t * 16 + (max_t - 1) * 4;
      int pip_sx = mid_x - pip_total / 2;
      for ( int p = 0; p < max_t; p++ )
      {
        aColor_t pip_c = (p < tier)
          ? WTYPE_ACCENT
          : (aColor_t){50, 55, 70, 255};
        a_DrawFilledRect(
          (aRectf_t){(float)(pip_sx + p * 20), (float)ty, 16, 12},
          pip_c
        );
        a_DrawRect(
          (aRectf_t){(float)(pip_sx + p * 20), (float)ty, 16, 12},
          (aColor_t){80, 100, 140, 200}
        );
      }
      ty += 24;
    }

    // Stats at bottom of card
    int kills = progress_get_weapon_lifetime_kills(wid);
    int pts   = progress_get_weapon_available_points(wid);
    char kills_buf[64], pts_buf[64];
    snprintf( kills_buf, sizeof(kills_buf), "KILLS: %d", kills );
    snprintf( pts_buf, sizeof(pts_buf), "POINTS: %d", pts );

    aTextStyle_t stat_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 220, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    int bottom_y = card_y + WTYPE_CARD_H - 78;
    a_DrawText( kills_buf, mid_x, bottom_y, stat_style );
    a_DrawText( pts_buf, mid_x, bottom_y + 18, stat_style );

    int has_upg = wprog_has_affordable_for(wid);
    if ( has_upg )
    {
      float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
      aTextStyle_t upg_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = WTYPE_ACCENT,
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.5f
      };
      a_DrawText( "UPGRADE AVAILABLE", mid_x, bottom_y + 40 + (int)bob, upg_style );
    }
    else
    {
      aTextStyle_t select_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = selected ? WTYPE_ACCENT : (aColor_t){100, 110, 140, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.45f
      };
      a_DrawText( "[SELECT TO UPGRADE]", mid_x, bottom_y + 42, select_style );
    }
  }

  // Page indicator at bottom
  {
    int page_y = SCREEN_HEIGHT - 60;
    char page_buf[32];
    snprintf( page_buf, sizeof(page_buf), "Page %d / %d", wtype_page + 1, WTYPE_PAGE_COUNT );
    aTextStyle_t page_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.7f
    };
    a_DrawText( page_buf, SCREEN_WIDTH / 2, page_y, page_style );

    // Page arrows
    int mx = app.mouse.x;
    int my = app.mouse.y;
    int arrow_w = 40, arrow_h = 30;
    int left_ax = SCREEN_WIDTH / 2 - 100 - arrow_w / 2;
    int right_ax = SCREEN_WIDTH / 2 + 100 - arrow_w / 2;
    int arrow_ay = page_y - 4;

    int can_left = (wtype_page > 0);
    int can_right = (wtype_page < WTYPE_PAGE_COUNT - 1);
    int hover_left = can_left && point_in_rect( mx, my, left_ax, arrow_ay, arrow_w, arrow_h );
    int hover_right = can_right && point_in_rect( mx, my, right_ax, arrow_ay, arrow_w, arrow_h );

    aColor_t left_color = !can_left ? (aColor_t){50, 60, 80, 150}
                         : hover_left ? (aColor_t){160, 210, 255, 255}
                         : WTYPE_ACCENT;
    if ( hover_left ) {
      a_DrawFilledRect(
        (aRectf_t){(float)left_ax, (float)arrow_ay, (float)arrow_w, (float)arrow_h},
        (aColor_t){100, 180, 255, 40}
      );
    }
    aTextStyle_t left_style = {
      .type = FONT_ENTER_COMMAND, .fg = left_color,
      .align = TEXT_ALIGN_CENTER, .scale = 0.9f
    };
    a_DrawText( "<<", SCREEN_WIDTH / 2 - 100, page_y - 2, left_style );

    aColor_t right_color = !can_right ? (aColor_t){50, 60, 80, 150}
                          : hover_right ? (aColor_t){160, 210, 255, 255}
                          : WTYPE_ACCENT;
    if ( hover_right ) {
      a_DrawFilledRect(
        (aRectf_t){(float)right_ax, (float)arrow_ay, (float)arrow_w, (float)arrow_h},
        (aColor_t){100, 180, 255, 40}
      );
    }
    aTextStyle_t right_style = {
      .type = FONT_ENTER_COMMAND, .fg = right_color,
      .align = TEXT_ALIGN_CENTER, .scale = 0.9f
    };
    a_DrawText( ">>", SCREEN_WIDTH / 2 + 100, page_y - 2, right_style );
  }

  // "BACK" button
  {
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = SCREEN_HEIGHT - 40;
    int hover = point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( hover )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
        (aColor_t){100, 180, 255, 40}
      );
    }
    a_DrawRect(
      (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
      hover ? (aColor_t){160, 210, 255, 255} : (aColor_t){80, 80, 110, 180}
    );
    aTextStyle_t back_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hover ? (aColor_t){220, 240, 255, 255} : (aColor_t){140, 140, 160, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText( "BACK [ESC]", SCREEN_WIDTH / 2, back_y + 6, back_style );
  }

  // Upgrade modal overlay
  if ( wtype_modal_open )
  {
    // Dark overlay
    a_DrawFilledRect(
      (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
      (aColor_t){0, 0, 0, 160}
    );

    int pw = 500, ph = 420;
    int px = (SCREEN_WIDTH - pw) / 2;
    int py = (SCREEN_HEIGHT - ph) / 2;

    // Panel background
    a_DrawFilledRect(
      (aRectf_t){(float)px, (float)py, (float)pw, (float)ph},
      (aColor_t){20, 25, 50, 245}
    );
    a_DrawRect(
      (aRectf_t){(float)px, (float)py, (float)pw, (float)ph},
      WTYPE_ACCENT
    );

    // Weapon name + available points
    int mcx = px + pw / 2;
    int mty = py + 16;
    aTextStyle_t modal_title = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.8f
    };
    a_DrawText( wprog_get_weapon_name((WeaponId_t)wtype_modal_wid), mcx, mty, modal_title );
    mty += 28;

    char pts_line[64];
    int avail = progress_get_weapon_available_points((WeaponId_t)wtype_modal_wid);
    snprintf( pts_line, sizeof(pts_line), "Available Points: %d", avail );
    aTextStyle_t pts_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = WTYPE_ACCENT,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    a_DrawText( pts_line, mcx, mty, pts_style );

    // Track rows
    for ( int t = 0; t < WTYPE_TRACK_COUNT; t++ )
    {
      WeaponProgressId_t id = wtype_tracks[wtype_modal_wid][t];
      int tier = wprog_get_tier(id);
      int max_tier = wprog_get_max_tier(id);
      int cost = wprog_get_next_cost(id);
      int can_buy = wprog_can_afford(id);
      int sel = (t == wtype_modal_track_sel);

      int row_y = py + 80 + t * 80;

      // Row background
      aColor_t row_bg = sel ? (aColor_t){35, 40, 70, 255} : (aColor_t){25, 30, 55, 255};
      a_DrawFilledRect(
        (aRectf_t){(float)(px + 20), (float)row_y, (float)(pw - 40), 72},
        row_bg
      );
      if ( sel )
      {
        a_DrawRect(
          (aRectf_t){(float)(px + 20), (float)row_y, (float)(pw - 40), 72},
          WTYPE_ACCENT
        );
      }

      // Upgrade name
      aTextStyle_t name_s = {
        .type = FONT_ENTER_COMMAND,
        .fg = {255, 255, 255, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.55f
      };
      a_DrawText( wprog_get_name(id), px + 30, row_y + 6, name_s );

      // Tier pips
      int pip_x = px + 30;
      int pip_y = row_y + 28;
      for ( int p = 0; p < max_tier; p++ )
      {
        aColor_t pip_color = (p < tier)
          ? WTYPE_ACCENT
          : (aColor_t){50, 55, 80, 255};
        a_DrawFilledRect(
          (aRectf_t){(float)pip_x, (float)pip_y, 14, 14},
          pip_color
        );
        a_DrawRect(
          (aRectf_t){(float)pip_x, (float)pip_y, 14, 14},
          (aColor_t){80, 100, 140, 200}
        );
        pip_x += 20;
      }

      // Cost or MAXED label
      aTextStyle_t cost_s = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 220, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.45f
      };
      if ( cost < 0 )
      {
        cost_s.fg = WTYPE_ACCENT;
        a_DrawText( "MAXED", pip_x + 8, pip_y + 1, cost_s );
      }
      else
      {
        char cost_buf[32];
        snprintf( cost_buf, sizeof(cost_buf), "Cost: %d", cost );
        a_DrawText( cost_buf, pip_x + 8, pip_y + 1, cost_s );
      }

      // Tier description
      if ( tier > 0 )
      {
        const char* desc = wprog_get_tier_desc(id, tier - 1);
        aTextStyle_t desc_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = {160, 160, 180, 200},
          .align = TEXT_ALIGN_LEFT,
          .scale = 0.4f
        };
        a_DrawText( desc, px + 30, row_y + 50, desc_s );
      }

      // BUY indicator (right side)
      if ( can_buy && sel )
      {
        aTextStyle_t buy_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = WTYPE_ACCENT,
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.55f
        };
        a_DrawText( "BUY", px + pw - 40, row_y + 28, buy_s );
      }
    }

    // ESC hint
    aTextStyle_t esc_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {140, 140, 160, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( "ESC - CLOSE", mcx, py + ph - 24, esc_style );

    // Info box to the right of the modal for the selected track
    {
      WeaponProgressId_t sel_prog = wtype_tracks[wtype_modal_wid][wtype_modal_track_sel];
      int s_tier = wprog_get_tier( sel_prog );
      int s_max = wprog_get_max_tier( sel_prog );

      int iw = 240;
      int ih = 80 + s_max * 26;
      int ix = px + pw + 14;
      int sel_ry = py + 80 + wtype_modal_track_sel * 80;
      int iy = sel_ry + 45 - ih / 2;

      // Clamp to screen
      if ( iy < 4 ) iy = 4;
      if ( iy + ih > SCREEN_HEIGHT - 4 ) iy = SCREEN_HEIGHT - 4 - ih;
      if ( ix + iw > SCREEN_WIDTH - 4 ) ix = px - iw - 14;

      // Background
      a_DrawFilledRect(
        (aRectf_t){(float)ix, (float)iy, (float)iw, (float)ih},
        (aColor_t){20, 25, 45, 240}
      );
      a_DrawRect(
        (aRectf_t){(float)ix, (float)iy, (float)iw, (float)ih},
        (aColor_t){100, 180, 255, 180}
      );

      int info_ty = iy + 10;

      // Detail text
      aTextStyle_t det_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 220, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.42f,
        .wrap_width = iw - 20
      };
      a_DrawText( wprog_get_detail( sel_prog ), ix + 10, info_ty, det_style );
      info_ty += 52;

      // Separator
      a_DrawFilledRect(
        (aRectf_t){(float)(ix + 10), (float)info_ty, (float)(iw - 20), 1},
        (aColor_t){255, 255, 255, 40}
      );
      info_ty += 8;

      // Tier breakdown
      for ( int tb = 0; tb < s_max; tb++ )
      {
        int is_current = (tb == s_tier - 1);
        int is_purchased = (tb < s_tier);
        int is_next = (tb == s_tier);

        char tline[64];
        snprintf( tline, sizeof(tline), "%s %d: %s",
                  is_next ? ">" : " ", tb + 1,
                  wprog_get_tier_desc( sel_prog, tb ) );

        aColor_t tc;
        if ( is_current )
          tc = (aColor_t){120, 190, 255, 255};
        else if ( is_purchased )
          tc = (aColor_t){100, 180, 255, 180};
        else if ( is_next )
          tc = (aColor_t){255, 255, 255, 255};
        else
          tc = (aColor_t){100, 100, 120, 150};

        aTextStyle_t ts = {
          .type = FONT_ENTER_COMMAND,
          .fg = tc,
          .align = TEXT_ALIGN_LEFT,
          .scale = 0.42f
        };
        a_DrawText( tline, ix + 12, info_ty, ts );
        info_ty += 24;
      }
    }
  }
}

// ============================================================================
// Settings Scene
// ============================================================================

#define SETTINGS_ROW_COUNT 3  // SFX, Music, Delete Save

static void scene_settings_logic( float dt )
{
  (void)dt;

  // ESC or Backspace → return to previous scene
  if ( app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 || app.keyboard[ SDL_SCANCODE_BACKSPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
    app.keyboard[ SDL_SCANCODE_BACKSPACE ] = 0;
    if ( settings_confirm )
    {
      settings_confirm = 0;
    }
    else
    {
      // Returning to game means re-pause
      if ( settings_return_scene == SCENE_GAME )
      {
        game_paused = 1;
        pause_selected = 2;
      }
      current_scene = settings_return_scene;
    }
    return;
  }

  // Delete confirmation sub-state
  if ( settings_confirm )
  {
    int box_h = 120;
    int by = (SCREEN_HEIGHT - box_h) / 2;
    int cbtn_w = 100, cbtn_h = 34, gap = 30;
    int cbtn_y = by + 60;
    static MenuRect_t sc_rects[2];
    sc_rects[0] = (MenuRect_t){ SCREEN_WIDTH / 2 - cbtn_w - gap / 2, cbtn_y, cbtn_w, cbtn_h };
    sc_rects[1] = (MenuRect_t){ SCREEN_WIDTH / 2 + gap / 2, cbtn_y, cbtn_w, cbtn_h };

    static Menu_t scm;
    scm.selected = settings_confirm_sel;
    scm.count    = 2;
    scm.axis     = MENU_AXIS_HORIZONTAL;
    scm.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
    scm.flex     = NULL;
    scm.rects    = sc_rects;

    MenuResult_t cr = menu_update( &scm );
    settings_confirm_sel = scm.selected;

    if ( cr == MENU_CONFIRM )
    {
      if ( settings_confirm_sel == 0 )
      {
        if ( settings_confirm == 1 )
        {
          settings_confirm = 2;
          settings_confirm_sel = 1;
        }
        else
        {
          save_data_clear_all();
          progress_init();
          stats_init();
          settings_confirm = 0;
        }
      }
      else
      {
        settings_confirm = 0;
      }
    }
    else if ( cr == MENU_BACK )
    {
      settings_confirm = 0;
    }
    return;
  }

  // Settings row rects for hover
  {
    int panel_w = 560, panel_h = 280;
    int px = (SCREEN_WIDTH - panel_w) / 2;
    int py = (SCREEN_HEIGHT - panel_h) / 2;
    int row_y_base = py + 60;

    static MenuRect_t sr[3];
    sr[0] = (MenuRect_t){ px + 4, row_y_base - 4, panel_w - 8, 30 };
    sr[1] = (MenuRect_t){ px + 4, row_y_base + 56, panel_w - 8, 30 };
    sr[2] = (MenuRect_t){ (SCREEN_WIDTH - 200) / 2, row_y_base + 130, 200, 30 };

    // Up/Down nav + hover only (no confirm/back — those are custom)
    static Menu_t sm;
    sm.selected = settings_sel;
    sm.count    = SETTINGS_ROW_COUNT;
    sm.axis     = MENU_AXIS_VERTICAL;
    sm.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
    sm.flex     = NULL;
    sm.rects    = sr;

    // Consume ESC before menu_update (handled above already)
    // Consume Enter/Space before menu_update — we handle them manually per-row
    int enter_pressed = (app.keyboard[ SDL_SCANCODE_RETURN ] == 1 || app.keyboard[ SDL_SCANCODE_SPACE ] == 1);
    if ( enter_pressed )
    {
      app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
      app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    }

    MenuResult_t r = menu_update( &sm );
    settings_sel = sm.selected;

    if ( r == MENU_BACK )
    {
      if ( settings_return_scene == SCENE_GAME )
      {
        game_paused = 1;
        pause_selected = 2;
      }
      current_scene = settings_return_scene;
      return;
    }

    // Mouse click confirm on rows (from menu_update) → handle per-row
    if ( r == MENU_CONFIRM )
    {
      if ( settings_sel == 2 )
      {
        settings_confirm = 1;
        settings_confirm_sel = 1;
      }
      // volume row clicks fall through to bar scrubbing below
    }

    // Keyboard Enter/Space confirm (manual, only on delete row)
    if ( enter_pressed && settings_sel == 2 )
    {
      play_menu_click();
      settings_confirm = 1;
      settings_confirm_sel = 1;
    }
  }

  // Left/Right on volume rows
  if ( settings_sel == 0 || settings_sel == 1 )
  {
    int vol = (settings_sel == 0)
      ? app.audio.master_volume
      : app.audio.music_volume;
    int changed = 0;

    if ( app.keyboard[ SDL_SCANCODE_LEFT ] == 1 || app.keyboard[ SDL_SCANCODE_A ] == 1 )
    {
      vol -= 10;
      if ( vol < 0 ) vol = 0;
      changed = 1;
      app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
      app.keyboard[ SDL_SCANCODE_A ] = 0;
    }
    if ( app.keyboard[ SDL_SCANCODE_RIGHT ] == 1 || app.keyboard[ SDL_SCANCODE_D ] == 1 )
    {
      vol += 10;
      if ( vol > 128 ) vol = 128;
      changed = 1;
      app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
      app.keyboard[ SDL_SCANCODE_D ] = 0;
    }

    if ( changed )
    {
      if ( settings_sel == 0 )
      {
        app.audio.master_volume = vol;
        a_AudioSetChannelVolume( -1, vol );
        save_data_set_int( "sfx_vol", vol );
      }
      else
      {
        app.audio.music_volume = vol;
        a_AudioSetMusicVolume( vol );
        save_data_set_int( "music_vol", vol );
      }
      save_data_flush();
    }
  }

  // Mouse click on volume bars (custom — not part of menu_update)
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    app.mouse.pressed = 0;
    int panel_w = 560, panel_h = 280;
    int px = (SCREEN_WIDTH - panel_w) / 2;
    int py = (SCREEN_HEIGHT - panel_h) / 2;
    int row_x = px + 20;
    int row_y_base = py + 60;

    int bar_x = row_x + 160;
    int bar_y = row_y_base + 4;
    int bar_w = 320;
    int bar_h = 20;
    if ( point_in_rect( app.mouse.x, app.mouse.y, bar_x, bar_y, bar_w, bar_h ) )
    {
      int vol = ((app.mouse.x - bar_x) * 128) / bar_w;
      if ( vol < 0 ) vol = 0;
      if ( vol > 128 ) vol = 128;
      app.audio.master_volume = vol;
      a_AudioSetChannelVolume( -1, vol );
      save_data_set_int( "sfx_vol", vol );
      save_data_flush();
      settings_sel = 0;
    }

    bar_y = row_y_base + 60 + 4;
    if ( point_in_rect( app.mouse.x, app.mouse.y, bar_x, bar_y, 320, bar_h ) )
    {
      int vol = ((app.mouse.x - bar_x) * 128) / 320;
      if ( vol < 0 ) vol = 0;
      if ( vol > 128 ) vol = 128;
      app.audio.music_volume = vol;
      a_AudioSetMusicVolume( vol );
      save_data_set_int( "music_vol", vol );
      save_data_flush();
      settings_sel = 1;
    }
  }
}

static void scene_settings_draw( float dt )
{
  (void)dt;

  // Dark overlay (only when coming from game/game over)
  if ( settings_return_scene == SCENE_GAME || settings_return_scene == SCENE_GAME_OVER )
  {
    a_DrawFilledRect(
      (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
      (aColor_t){0, 0, 0, 160}
    );
  }

  int panel_w = 560;
  int panel_h = 280;
  int px = (SCREEN_WIDTH - panel_w) / 2;
  int py = (SCREEN_HEIGHT - panel_h) / 2;

  // Panel background
  a_DrawFilledRect(
    (aRectf_t){(float)px, (float)py, (float)panel_w, (float)panel_h},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){(float)px, (float)py, (float)panel_w, (float)panel_h},
    (aColor_t){150, 150, 200, 255}
  );

  // Title
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.9f
  };
  a_DrawText( "SETTINGS", SCREEN_WIDTH / 2, py + 14, title_style );

  int row_x = px + 20;
  int row_y = py + 60;
  int bar_w = 320;
  int bar_h = 20;
  int bar_x = row_x + 160;

  aTextStyle_t label_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {180, 180, 200, 255},
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.5f
  };
  aTextStyle_t label_sel_style = label_style;
  label_sel_style.fg = (aColor_t){255, 255, 255, 255};

  aTextStyle_t pct_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.45f
  };

  char buf[32];

  // Row 0: SFX Volume
  {
    int sel = (settings_sel == 0 && !settings_confirm);
    int y = row_y;

    // Selection indicator
    if ( sel )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)(px + 4), (float)(y - 4), (float)(panel_w - 8), 30.0f},
        (aColor_t){50, 50, 80, 200}
      );
    }

    a_DrawText( "SFX Volume", row_x, y, sel ? label_sel_style : label_style );

    // Bar background
    a_DrawFilledRect(
      (aRectf_t){(float)bar_x, (float)(y + 4), (float)bar_w, (float)bar_h},
      (aColor_t){20, 20, 30, 255}
    );
    // Bar fill (green)
    int fill_w = (app.audio.master_volume * bar_w) / 128;
    if ( fill_w > 0 )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)bar_x, (float)(y + 4), (float)fill_w, (float)bar_h},
        (aColor_t){60, 200, 60, 255}
      );
    }
    // Bar border
    a_DrawRect(
      (aRectf_t){(float)bar_x, (float)(y + 4), (float)bar_w, (float)bar_h},
      (aColor_t){120, 120, 150, 200}
    );

    int pct = (app.audio.master_volume * 100) / 128;
    snprintf( buf, sizeof(buf), "%d%%", pct );
    a_DrawText( buf, bar_x + bar_w + 10, y + 4, pct_style );
  }

  // Row 1: Music Volume
  {
    int sel = (settings_sel == 1 && !settings_confirm);
    int y = row_y + 60;

    if ( sel )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)(px + 4), (float)(y - 4), (float)(panel_w - 8), 30.0f},
        (aColor_t){50, 50, 80, 200}
      );
    }

    a_DrawText( "Music Volume", row_x, y, sel ? label_sel_style : label_style );

    // Bar background
    a_DrawFilledRect(
      (aRectf_t){(float)bar_x, (float)(y + 4), (float)bar_w, (float)bar_h},
      (aColor_t){20, 20, 30, 255}
    );
    // Bar fill (blue)
    int fill_w = (app.audio.music_volume * bar_w) / 128;
    if ( fill_w > 0 )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)bar_x, (float)(y + 4), (float)fill_w, (float)bar_h},
        (aColor_t){60, 100, 220, 255}
      );
    }
    a_DrawRect(
      (aRectf_t){(float)bar_x, (float)(y + 4), (float)bar_w, (float)bar_h},
      (aColor_t){120, 120, 150, 200}
    );

    int pct = (app.audio.music_volume * 100) / 128;
    snprintf( buf, sizeof(buf), "%d%%", pct );
    a_DrawText( buf, bar_x + bar_w + 10, y + 4, pct_style );
  }

  // Row 2: Delete Save Data
  {
    int sel = (settings_sel == 2 && !settings_confirm);
    int y = row_y + 130;
    int del_w = 200;
    int del_h = 30;
    int del_x = (SCREEN_WIDTH - del_w) / 2;

    aColor_t btn_bg = sel ? (aColor_t){100, 40, 40, 255} : (aColor_t){60, 30, 30, 255};
    aColor_t btn_border = sel ? (aColor_t){255, 100, 100, 255} : (aColor_t){150, 80, 80, 200};

    a_DrawFilledRect(
      (aRectf_t){(float)del_x, (float)y, (float)del_w, (float)del_h},
      btn_bg
    );
    a_DrawRect(
      (aRectf_t){(float)del_x, (float)y, (float)del_w, (float)del_h},
      btn_border
    );

    aTextStyle_t del_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = sel ? (aColor_t){255, 100, 100, 255} : (aColor_t){200, 80, 80, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( "DELETE SAVE DATA", SCREEN_WIDTH / 2, y + 6, del_style );

    // Save data size below button
    long save_bytes = save_data_get_size();
    char size_buf[64];
    if (save_bytes >= 1024 * 1024)
      snprintf( size_buf, sizeof(size_buf), "Save data: %.1f MB", (double)save_bytes / (1024.0 * 1024.0) );
    else if (save_bytes >= 1024)
      snprintf( size_buf, sizeof(size_buf), "Save data: %.1f KB", (double)save_bytes / 1024.0 );
    else
      snprintf( size_buf, sizeof(size_buf), "Save data: %ld bytes", save_bytes );
    aTextStyle_t size_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {120, 120, 140, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.4f
    };
    a_DrawText( size_buf, SCREEN_WIDTH / 2, y + del_h + 8, size_style );
  }

  // ESC hint
  aTextStyle_t hint_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {120, 120, 140, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.4f
  };
  a_DrawText( "ESC - Back", SCREEN_WIDTH / 2, py + panel_h - 24, hint_style );

  // Confirmation overlay
  if ( settings_confirm )
  {
    // Dark overlay on top of settings panel
    a_DrawFilledRect(
      (aRectf_t){(float)px, (float)py, (float)panel_w, (float)panel_h},
      (aColor_t){0, 0, 0, 180}
    );

    int box_w = 320;
    int box_h = 120;
    int bx = (SCREEN_WIDTH - box_w) / 2;
    int by = (SCREEN_HEIGHT - box_h) / 2;

    a_DrawFilledRect(
      (aRectf_t){(float)bx, (float)by, (float)box_w, (float)box_h},
      (aColor_t){40, 20, 20, 250}
    );
    a_DrawRect(
      (aRectf_t){(float)bx, (float)by, (float)box_w, (float)box_h},
      (aColor_t){255, 80, 80, 255}
    );

    aTextStyle_t warn_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 80, 80, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    if ( settings_confirm == 2 )
    {
      a_DrawText( "Like, actually?", SCREEN_WIDTH / 2, by + 10, warn_style );
      aTextStyle_t sub_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {180, 180, 200, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.4f
      };
      a_DrawText( "You will restart from zero", SCREEN_WIDTH / 2, by + 34, sub_style );
    }
    else
    {
      a_DrawText( "DELETE ALL PROGRESS?", SCREEN_WIDTH / 2, by + 16, warn_style );
    }

    // YES / NO buttons
    int cbtn_w = 100;
    int cbtn_h = 34;
    int gap = 30;
    int yes_x = SCREEN_WIDTH / 2 - cbtn_w - gap / 2;
    int no_x  = SCREEN_WIDTH / 2 + gap / 2;
    int cbtn_y = by + 60;

    for ( int i = 0; i < 2; i++ )
    {
      int cx = (i == 0) ? yes_x : no_x;
      int is_sel = (settings_confirm_sel == i);

      aColor_t cbg = is_sel
        ? (i == 0 ? (aColor_t){140, 40, 40, 255} : (aColor_t){40, 80, 40, 255})
        : (aColor_t){50, 50, 65, 255};
      aColor_t cborder = is_sel ? (aColor_t){255, 255, 255, 255} : (aColor_t){120, 120, 150, 200};

      a_DrawFilledRect(
        (aRectf_t){(float)cx, (float)cbtn_y, (float)cbtn_w, (float)cbtn_h},
        cbg
      );
      a_DrawRect(
        (aRectf_t){(float)cx, (float)cbtn_y, (float)cbtn_w, (float)cbtn_h},
        cborder
      );

      aTextStyle_t cbtn_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = is_sel ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 180, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.55f
      };
      a_DrawText( i == 0 ? "YES" : "NO",
                  cx + cbtn_w / 2, cbtn_y + 8, cbtn_style );
    }
  }
}

// ============================================================================
// Enemy Types Scene
// ============================================================================

#define ETYPE_CARD_COUNT 4   // cards per page on page 1
#define ETYPE_CARD_W     290
#define ETYPE_CARD_H     560
#define ETYPE_CARD_GAP   14
#define ETYPE_PAGE_COUNT 2

static int etype_selected = 0;  // highlighted card on current page
static int etype_page = 0;      // 0 = page 1 (4 types), 1 = page 2 (snake + coming soon)

// Upgrade modal state
static int etype_modal_open = 0;
static int etype_modal_type = 0;
static int etype_modal_track_sel = 0;

// Track mapping: 2 upgrades per enemy type
static const ProgressUpgradeId_t etype_tracks[ENEMY_TYPE_COUNT][2] = {
  { PROG_GRUNT_DMG_RED,       PROG_GRUNT_EXPLOSION },
  { PROG_DASHER_DMG_RED,      PROG_DASHER_STUN },
  { PROG_BRUTE_DMG_RED,       PROG_BRUTE_SPEED_DIM },
  { PROG_SHAMAN_CORPSE_DECAY, PROG_SHAMAN_HEAL_RED },
  { PROG_SNAKE_DMG_RED,       PROG_SNAKE_SCALE_SHATTER },
  { PROG_BEHOLDER_DMG_RED,    PROG_BEHOLDER_TELEGRAPH },
};

typedef struct {
  const char* name;
  const char* desc;
  const char* behaviors[4];
  const char* hp_line;
} EnemyTypeCard_t;

// All enemies are green in-game (healthy color)
#define ETYPE_SWATCH_COLOR ((aColor_t){0, 255, 0, 255})
#define ETYPE_ACCENT_COLOR ((aColor_t){80, 220, 80, 255})

static const EnemyTypeCard_t etype_cards[ETYPE_CARD_COUNT] = {
  {
    .name = "GRUNT",
    .desc = "Standard melee fighter",
    .behaviors = {
      "Approaches and flanks the player",
      "Charges at 2x speed when in range",
      "Retreats after hitting, then re-engages",
      NULL
    },
    .hp_line = "5 hits to kill"
  },
  {
    .name = "DASHER",
    .desc = "Fast duelist with telegraphed charges",
    .behaviors = {
      "Winds up with a red indicator line",
      "Locks direction then charges at 3x speed",
      "Bounces off screen edges during charge",
      NULL
    },
    .hp_line = "4 hits to kill"
  },
  {
    .name = "BRUTE",
    .desc = "Slow tank that enrages when damaged",
    .behaviors = {
      "Gets faster as HP drops",
      "Steals health and power pickups",
      "Gains buffs from pickups (fire, speed, shield, slow)",
      "Cannot be knocked back"
    },
    .hp_line = "12 hits to kill"
  },
  {
    .name = "SHAMAN",
    .desc = "Support healer, no direct damage",
    .behaviors = {
      "Eats enemy corpses to store heal energy",
      "Channels heal beam on damaged allies",
      "Flees when player gets too close",
      "Orbits at safe distance"
    },
    .hp_line = "2 hits to kill"
  }
};

static const EnemyTypeCard_t snake_card = {
  .name = "SNAKE",
  .desc = "Boss enemy",
  .behaviors = {
    "Charges in straight lines, 3 per cycle",
    "Gets faster each cycle and per lost segment",
    "Shamans can heal and regrow segments",
    "Head exposed after all segments destroyed"
  },
  .hp_line = "3 HP per segment + 6 HP head"
};

static const EnemyTypeCard_t beholder_card = {
  .name = "BEHOLDER",
  .desc = "Ranged mini-boss",
  .behaviors = {
    "Maintains distance and fires sweeping beams",
    "Regens shield when broken - must burst down",
    "Ramps fire rate over time",
    "Shamans can heal HP but NOT shield"
  },
  .hp_line = "8 HP + scaling shield"
};

static void scene_enemy_types_logic( float dt )
{
  (void)dt;

  // Modal logic
  if ( etype_modal_open )
  {
    int pw = 500, ph = 360;
    int epx = (SCREEN_WIDTH - pw) / 2;
    int epy = (SCREEN_HEIGHT - ph) / 2;

    static MenuRect_t em_rects[2];
    for ( int t = 0; t < 2; t++ )
      em_rects[t] = (MenuRect_t){ epx + 20, epy + 80 + t * 100, pw - 40, 90 };

    static Menu_t em;
    em.selected = etype_modal_track_sel;
    em.count    = 2;
    em.axis     = MENU_AXIS_VERTICAL;
    em.sounds   = menu_sounds( &menu_move_sound, 40, &menu_click_sound, 120 );
    em.flex     = NULL;
    em.rects    = em_rects;

    MenuResult_t mr = menu_update( &em );
    etype_modal_track_sel = em.selected;

    if ( mr == MENU_BACK )
    {
      etype_modal_open = 0;
      return;
    }
    if ( mr == MENU_CONFIRM )
    {
      ProgressUpgradeId_t id = etype_tracks[etype_modal_type][etype_modal_track_sel];
      progress_purchase(id);
    }
    return;
  }

  // ESC / Backspace / click Back button → back to main menu
  {
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = SCREEN_HEIGHT - 40;
    int esc = app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 || app.keyboard[ SDL_SCANCODE_BACKSPACE ] == 1;
    int clicked = app.mouse.button == 1 && app.mouse.pressed &&
                  point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( esc || clicked )
    {
      app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
      app.keyboard[ SDL_SCANCODE_BACKSPACE ] = 0;
      if ( clicked ) app.mouse.button = 0;
      current_scene = SCENE_MAIN_MENU;
      return;
    }
  }

  // Left/Right or A/D to navigate cards (with page switching)
  if ( app.keyboard[ SDL_SCANCODE_LEFT ] == 1 || app.keyboard[ SDL_SCANCODE_A ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
    app.keyboard[ SDL_SCANCODE_A ] = 0;
    if ( etype_page == 0 )
    {
      if ( etype_selected > 0 )
        etype_selected--;
      else
      {
        etype_page = 1;
        etype_selected = progress_is_beholder_discovered() ? 1 : 0;
      }
    }
    else
    {
      if ( etype_selected > 0 )
        etype_selected--;
      else
      {
        etype_page = 0;
        etype_selected = ETYPE_CARD_COUNT - 1;
      }
    }
    play_menu_move();
  }
  if ( app.keyboard[ SDL_SCANCODE_RIGHT ] == 1 || app.keyboard[ SDL_SCANCODE_D ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
    app.keyboard[ SDL_SCANCODE_D ] = 0;
    if ( etype_page == 0 )
    {
      if ( etype_selected < ETYPE_CARD_COUNT - 1 )
        etype_selected++;
      else
      {
        etype_page = 1;
        etype_selected = 0;
      }
    }
    else
    {
      int max_slot = progress_is_beholder_discovered() ? 1 : 0;
      if ( etype_selected < max_slot )
        etype_selected++;
      else
      {
        etype_page = 0;
        etype_selected = 0;
      }
    }
    play_menu_move();
  }

  // Enter/Space opens upgrade modal
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 ||
       app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    if ( etype_page == 0 )
    {
      play_menu_click();
      etype_modal_open = 1;
      etype_modal_type = etype_selected;
      etype_modal_track_sel = 0;
    }
    else if ( etype_page == 1 && etype_selected == 0 && progress_is_snake_discovered() )
    {
      play_menu_click();
      etype_modal_open = 1;
      etype_modal_type = ENEMY_TYPE_SNAKE;
      etype_modal_track_sel = 0;
    }
    else if ( etype_page == 1 && etype_selected == 1 && progress_is_beholder_discovered() )
    {
      play_menu_click();
      etype_modal_open = 1;
      etype_modal_type = ENEMY_TYPE_BEHOLDER;
      etype_modal_track_sel = 0;
    }
  }

  // Mouse hover and click
  int mx = app.mouse.x;
  int my = app.mouse.y;

  // Arrow click handling for page navigation
  {
    int page_y = SCREEN_HEIGHT - 60;
    int arrow_w = 40, arrow_h = 30;
    int left_ax = SCREEN_WIDTH / 2 - 100 - arrow_w / 2;
    int right_ax = SCREEN_WIDTH / 2 + 100 - arrow_w / 2;
    int arrow_ay = page_y - 4;

    if ( app.mouse.pressed )
    {
      if ( etype_page > 0 && point_in_rect( mx, my, left_ax, arrow_ay, arrow_w, arrow_h ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        etype_page--;
        etype_selected = 0;
      }
      else if ( etype_page < ETYPE_PAGE_COUNT - 1 && point_in_rect( mx, my, right_ax, arrow_ay, arrow_w, arrow_h ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        etype_page++;
        etype_selected = 0;
      }
    }
  }

  if ( etype_page == 0 )
  {
    int total_w = ETYPE_CARD_COUNT * ETYPE_CARD_W + (ETYPE_CARD_COUNT - 1) * ETYPE_CARD_GAP;
    int start_x = (SCREEN_WIDTH - total_w) / 2;
    int card_y  = (SCREEN_HEIGHT - ETYPE_CARD_H) / 2;

    for ( int i = 0; i < ETYPE_CARD_COUNT; i++ )
    {
      int cx = start_x + i * (ETYPE_CARD_W + ETYPE_CARD_GAP);
      if ( point_in_rect( mx, my, cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        if ( mouse_moved_this_frame && etype_selected != i )
        {
          etype_selected = i;
          play_menu_move();
        }
        break;
      }
    }

    if ( app.mouse.pressed )
    {
      for ( int i = 0; i < ETYPE_CARD_COUNT; i++ )
      {
        int cx = start_x + i * (ETYPE_CARD_W + ETYPE_CARD_GAP);
        if ( point_in_rect( mx, my, cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
        {
          app.mouse.pressed = 0;
          play_menu_click();
          etype_modal_open = 1;
          etype_modal_type = i;
          etype_modal_track_sel = 0;
          break;
        }
      }
    }
  }
  else
  {
    // Page 2 mouse handling: 3 card slots centered
    int p2_cards = 3;
    int total_w = p2_cards * ETYPE_CARD_W + (p2_cards - 1) * ETYPE_CARD_GAP;
    int start_x = (SCREEN_WIDTH - total_w) / 2;
    int card_y  = (SCREEN_HEIGHT - ETYPE_CARD_H) / 2;

    // Only snake card (slot 0) is hoverable/clickable if discovered
    if ( progress_is_snake_discovered() )
    {
      int cx = start_x;
      if ( mouse_moved_this_frame && etype_selected != 0 &&
           point_in_rect( mx, my, cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        etype_selected = 0;
        play_menu_move();
      }

      if ( app.mouse.pressed &&
           point_in_rect( mx, my, cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        etype_modal_open = 1;
        etype_modal_type = ENEMY_TYPE_SNAKE;
        etype_modal_track_sel = 0;
      }
    }

    // Beholder card (slot 1) is hoverable/clickable if discovered
    if ( progress_is_beholder_discovered() )
    {
      int bh_cx = start_x + 1 * (ETYPE_CARD_W + ETYPE_CARD_GAP);
      if ( mouse_moved_this_frame && etype_selected != 1 &&
           point_in_rect( mx, my, bh_cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        etype_selected = 1;
        play_menu_move();
      }

      if ( app.mouse.pressed &&
           point_in_rect( mx, my, bh_cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        etype_modal_open = 1;
        etype_modal_type = ENEMY_TYPE_BEHOLDER;
        etype_modal_track_sel = 0;
      }
    }
  }
}

static void etype_draw_shape( int type_index, int cx, int cy, int size, aColor_t c )
{
  if ( type_index == ENEMY_TYPE_DASHER )
  {
    // Dasher — triangle pointing up
    int half = size / 2;
    a_DrawFilledTriangle(
      cx, cy - half,
      cx - half, cy + half,
      cx + half, cy + half,
      c
    );
  }
  else if ( type_index == ENEMY_TYPE_SHAMAN )
  {
    // Shaman — cross/plus shape
    float arm_w = (float)size * 0.3f;
    float arm_l = (float)size / 2.0f;
    float fcx = (float)cx;
    float fcy = (float)cy;
    a_DrawFilledRect(
      (aRectf_t){fcx - arm_w / 2.0f, fcy - arm_l, arm_w, (float)size},
      c
    );
    a_DrawFilledRect(
      (aRectf_t){fcx - arm_l, fcy - arm_w / 2.0f, (float)size, arm_w},
      c
    );
  }
  else if ( type_index == ENEMY_TYPE_SNAKE )
  {
    // Snake — head square + 3 trailing body segments in zigzag, compact
    int head_sz = (int)((float)size * 0.7f);
    int seg_sz = (int)((float)size * 0.5f);
    if ( head_sz < 4 ) head_sz = 4;
    if ( seg_sz < 3 ) seg_sz = 3;

    // Head at top-center
    int hx = cx - head_sz / 2;
    int hy = cy - size / 2;
    a_DrawFilledRect(
      (aRectf_t){(float)hx, (float)hy, (float)head_sz, (float)head_sz},
      c
    );

    // Eyes
    int eye_off = head_sz / 4;
    int eye_sz = head_sz > 12 ? 3 : 2;
    a_DrawFilledRect(
      (aRectf_t){(float)(cx - eye_off - eye_sz / 2), (float)(hy + 2),
                 (float)eye_sz, (float)eye_sz},
      (aColor_t){0, 0, 0, 255}
    );
    a_DrawFilledRect(
      (aRectf_t){(float)(cx + eye_off - eye_sz / 2), (float)(hy + 2),
                 (float)eye_sz, (float)eye_sz},
      (aColor_t){0, 0, 0, 255}
    );

    // 3 body segments zigzagging below head
    int seg_gap = seg_sz + 2;
    int base_y = hy + head_sz + 2;
    for ( int s = 0; s < 3; s++ )
    {
      int sx = cx + ((s % 2 == 0) ? -(seg_sz / 2 + 2) : (seg_sz / 2 + 2));
      int sy = base_y + s * seg_gap;
      a_DrawFilledRect(
        (aRectf_t){(float)(sx - seg_sz / 2), (float)sy, (float)seg_sz, (float)seg_sz},
        c
      );
    }
  }
  else if ( type_index == ENEMY_TYPE_BEHOLDER )
  {
    // Beholder — circle body + eye
    int body_r = size / 2;
    a_DrawFilledCircle( cx, cy, body_r, c );
    // Sclera
    a_DrawFilledCircle( cx, cy, body_r * 2 / 3, (aColor_t){220, 220, 230, 255} );
    // Pupil
    a_DrawFilledCircle( cx, cy - 1, body_r / 3, (aColor_t){20, 20, 30, 255} );
  }
  else
  {
    // Grunt / Brute — square
    int half = size / 2;
    float bx = (float)(cx - half);
    float by = (float)(cy - half);
    float sz = (float)size;
    a_DrawFilledRect(
      (aRectf_t){bx, by, sz, sz},
      c
    );

    // Brute gets devil horns (matches in-game drawing)
    if ( type_index == ENEMY_TYPE_BRUTE )
    {
      int horn_h = (int)(sz * 0.4f);
      int horn_w = (int)(sz * 0.25f);
      // Left horn
      a_DrawFilledTriangle(
        (int)(bx + sz * 0.2f), (int)by,
        (int)(bx - horn_w * 0.3f), (int)(by - horn_h),
        (int)bx, (int)by,
        c
      );
      // Right horn
      a_DrawFilledTriangle(
        (int)(bx + sz * 0.8f), (int)by,
        (int)(bx + sz + horn_w * 0.3f), (int)(by - horn_h),
        (int)(bx + sz), (int)by,
        c
      );
    }
  }
}

static void scene_enemy_types_draw( float dt )
{
  (void)dt;

  // Title
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.2f
  };
  a_DrawText( "ENEMY TYPES", SCREEN_WIDTH / 2, 24, title_style );

  // (page indicator drawn at bottom after cards)

  int card_y  = (SCREEN_HEIGHT - ETYPE_CARD_H) / 2;

  if ( etype_page == 0 )
  {
    // ================================================================
    // Page 1 — 4 standard enemy type cards
    // ================================================================
    int total_w = ETYPE_CARD_COUNT * ETYPE_CARD_W + (ETYPE_CARD_COUNT - 1) * ETYPE_CARD_GAP;
    int start_x = (SCREEN_WIDTH - total_w) / 2;

    for ( int i = 0; i < ETYPE_CARD_COUNT; i++ )
    {
      const EnemyTypeCard_t* card = &etype_cards[i];
      int cx = start_x + i * (ETYPE_CARD_W + ETYPE_CARD_GAP);
      int selected = (i == etype_selected);

      // Card background
      aColor_t bg = selected ? (aColor_t){50, 50, 80, 240} : (aColor_t){30, 30, 50, 240};
      a_DrawFilledRect(
        (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
        bg
      );

      // Border
      aColor_t border = selected ? ETYPE_ACCENT_COLOR : (aColor_t){100, 100, 130, 200};
      a_DrawRect(
        (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
        border
      );
      if ( selected )
      {
        a_DrawRect(
          (aRectf_t){(float)(cx + 1), (float)(card_y + 1),
                     (float)(ETYPE_CARD_W - 2), (float)(ETYPE_CARD_H - 2)},
          border
        );
      }

      int mid_x = cx + ETYPE_CARD_W / 2;
      int ty = card_y + 20;

      // Enemy name
      aTextStyle_t name_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {255, 255, 255, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.8f
      };
      a_DrawText( card->name, mid_x, ty, name_style );
      ty += 40;

      // Shape swatch
      int shape_size = (i == ENEMY_TYPE_BRUTE) ? 36 : 26;
      etype_draw_shape( i, mid_x, ty + shape_size / 2, shape_size, ETYPE_SWATCH_COLOR );
      ty += shape_size + 22;

      // Short description
      aTextStyle_t desc_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {180, 180, 200, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.55f,
        .wrap_width = ETYPE_CARD_W - 30
      };
      a_DrawText( card->desc, mid_x, ty, desc_style );
      ty += 48;

      // HP line
      aTextStyle_t hp_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = ETYPE_ACCENT_COLOR,
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.55f
      };
      a_DrawText( card->hp_line, mid_x, ty, hp_style );
      ty += 36;

      // Separator
      a_DrawFilledRect(
        (aRectf_t){(float)(cx + 16), (float)ty, (float)(ETYPE_CARD_W - 32), 1},
        (aColor_t){255, 255, 255, 40}
      );
      ty += 16;

      // "AI BEHAVIOR" subheader
      aTextStyle_t sub_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = ETYPE_ACCENT_COLOR,
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.6f
      };
      a_DrawText( "AI BEHAVIOR", mid_x, ty, sub_style );
      ty += 34;

      // Behavior bullet points
      aTextStyle_t bullet_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {160, 160, 180, 220},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.5f,
        .wrap_width = ETYPE_CARD_W - 30
      };
      for ( int b = 0; b < 4 && card->behaviors[b]; b++ )
      {
        a_DrawText( card->behaviors[b], mid_x, ty, bullet_style );
        ty += 46;
      }

      // Kill/Death/Point display at bottom of card
      int kills = progress_get_lifetime_kills((EnemyType_t)i);
      int pts   = progress_get_available_points((EnemyType_t)i);
      char kills_buf[64], mid_buf[64], pts_buf[64];
      snprintf( kills_buf, sizeof(kills_buf), "KILLS: %d", kills );
      if (i == ENEMY_TYPE_SHAMAN)
        snprintf( mid_buf, sizeof(mid_buf), "HEALED: %d", stats_get_shaman_total_healed() );
      else
        snprintf( mid_buf, sizeof(mid_buf), "DEATHS: %d", stats_get_deaths_by_type((EnemyType_t)i) );
      snprintf( pts_buf, sizeof(pts_buf), "POINTS: %d", pts );

      aTextStyle_t kill_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 220, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.5f
      };
      aColor_t mid_color = (i == ENEMY_TYPE_SHAMAN)
        ? (aColor_t){80, 220, 80, 255}
        : (aColor_t){220, 80, 80, 255};
      aTextStyle_t mid_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = mid_color,
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.5f
      };
      int bottom_y = card_y + ETYPE_CARD_H - 88;
      a_DrawText( kills_buf, mid_x, bottom_y, kill_style );
      a_DrawText( mid_buf, mid_x, bottom_y + 18, mid_style );
      a_DrawText( pts_buf, mid_x, bottom_y + 36, kill_style );

      int type_has_upg = progress_has_affordable_upgrade_for((EnemyType_t)i);
      if ( type_has_upg )
      {
        float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
        aTextStyle_t upg_style = {
          .type = FONT_ENTER_COMMAND,
          .fg = {80, 220, 80, 255},
          .align = TEXT_ALIGN_CENTER,
          .scale = 0.5f
        };
        a_DrawText( "UPGRADE AVAILABLE", mid_x, bottom_y + 56 + (int)bob, upg_style );
      }
      else
      {
        aTextStyle_t select_style = {
          .type = FONT_ENTER_COMMAND,
          .fg = selected ? ETYPE_ACCENT_COLOR : (aColor_t){120, 120, 140, 200},
          .align = TEXT_ALIGN_CENTER,
          .scale = 0.45f
        };
        a_DrawText( "[SELECT TO UPGRADE]", mid_x, bottom_y + 58, select_style );
      }
    }
  }
  else
  {
    // ================================================================
    // Page 2 — Snake card + 2 "Coming Soon" cards
    // ================================================================
    int p2_cards = 3;
    int total_w = p2_cards * ETYPE_CARD_W + (p2_cards - 1) * ETYPE_CARD_GAP;
    int start_x = (SCREEN_WIDTH - total_w) / 2;
    int discovered = progress_is_snake_discovered();

    for ( int slot = 0; slot < p2_cards; slot++ )
    {
      int cx = start_x + slot * (ETYPE_CARD_W + ETYPE_CARD_GAP);
      int mid_x = cx + ETYPE_CARD_W / 2;

      if ( slot == 0 )
      {
        // Snake card
        int selected = (etype_selected == 0 && discovered);

        aColor_t bg = discovered
          ? (selected ? (aColor_t){50, 50, 80, 240} : (aColor_t){30, 30, 50, 240})
          : (aColor_t){25, 25, 35, 240};
        a_DrawFilledRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          bg
        );

        aColor_t border = discovered
          ? (selected ? ETYPE_ACCENT_COLOR : (aColor_t){100, 100, 130, 200})
          : (aColor_t){60, 60, 70, 200};
        a_DrawRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          border
        );
        if ( selected )
        {
          a_DrawRect(
            (aRectf_t){(float)(cx + 1), (float)(card_y + 1),
                       (float)(ETYPE_CARD_W - 2), (float)(ETYPE_CARD_H - 2)},
            border
          );
        }

        int ty = card_y + 20;

        if ( !discovered )
        {
          // Locked snake card
          aTextStyle_t lock_name = {
            .type = FONT_ENTER_COMMAND,
            .fg = {80, 80, 90, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( "???", mid_x, ty, lock_name );
          ty += 120;

          aTextStyle_t hint_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {100, 100, 120, 200},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          a_DrawText( "Survive 2:00 to", mid_x, ty, hint_style );
          a_DrawText( "discover this enemy", mid_x, ty + 18, hint_style );
        }
        else
        {
          // Unlocked snake card — full content like page 1 cards
          const EnemyTypeCard_t* card = &snake_card;

          aTextStyle_t name_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {255, 255, 255, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( card->name, mid_x, ty, name_style );
          ty += 34;

          // Snake shape — compact, matching page 1 swatch size
          etype_draw_shape( ENEMY_TYPE_SNAKE, mid_x, ty + 13, 26, ETYPE_SWATCH_COLOR );
          ty += 26 + 22;

          ty += 10;
          aTextStyle_t desc_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {180, 180, 200, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.55f
          };
          a_DrawText( card->desc, mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Zones the screen with", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "its trailing body", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Damage hits the tail", mid_x, ty, desc_style );
          ty += 24;

          aTextStyle_t hp_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = ETYPE_ACCENT_COLOR,
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.55f
          };
          a_DrawText( card->hp_line, mid_x, ty, hp_style );
          ty += 36;

          a_DrawFilledRect(
            (aRectf_t){(float)(cx + 16), (float)ty, (float)(ETYPE_CARD_W - 32), 1},
            (aColor_t){255, 255, 255, 40}
          );
          ty += 16;

          aTextStyle_t sub_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = ETYPE_ACCENT_COLOR,
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.6f
          };
          a_DrawText( "AI BEHAVIOR", mid_x, ty, sub_style );
          ty += 34;

          aTextStyle_t bullet_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {160, 160, 180, 220},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f,
            .wrap_width = ETYPE_CARD_W - 30
          };
          for ( int b = 0; b < 4 && card->behaviors[b]; b++ )
          {
            a_DrawText( card->behaviors[b], mid_x, ty, bullet_style );
            ty += 46;
          }

          // Stats at bottom
          int kills = progress_get_lifetime_kills(ENEMY_TYPE_SNAKE);
          int pts   = progress_get_available_points(ENEMY_TYPE_SNAKE);
          char kills_buf[64], deaths_buf[64], pts_buf[64];
          snprintf( kills_buf, sizeof(kills_buf), "KILLS: %d", kills );
          snprintf( deaths_buf, sizeof(deaths_buf), "DEATHS: %d",
                    stats_get_deaths_by_type(ENEMY_TYPE_SNAKE) );
          snprintf( pts_buf, sizeof(pts_buf), "POINTS: %d", pts );

          aTextStyle_t kill_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {200, 200, 220, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          aTextStyle_t death_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {220, 80, 80, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          int bottom_y = card_y + ETYPE_CARD_H - 88;
          a_DrawText( kills_buf, mid_x, bottom_y, kill_style );
          a_DrawText( deaths_buf, mid_x, bottom_y + 18, death_style );
          a_DrawText( pts_buf, mid_x, bottom_y + 36, kill_style );

          int type_has_upg = progress_has_affordable_upgrade_for(ENEMY_TYPE_SNAKE);
          if ( type_has_upg )
          {
            float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
            aTextStyle_t upg_style = {
              .type = FONT_ENTER_COMMAND,
              .fg = {80, 220, 80, 255},
              .align = TEXT_ALIGN_CENTER,
              .scale = 0.5f
            };
            a_DrawText( "UPGRADE AVAILABLE", mid_x, bottom_y + 56 + (int)bob, upg_style );
          }
          else
          {
            aTextStyle_t select_style = {
              .type = FONT_ENTER_COMMAND,
              .fg = selected ? ETYPE_ACCENT_COLOR : (aColor_t){120, 120, 140, 200},
              .align = TEXT_ALIGN_CENTER,
              .scale = 0.45f
            };
            a_DrawText( "[SELECT TO UPGRADE]", mid_x, bottom_y + 58, select_style );
          }
        }
      }
      else if ( slot == 1 )
      {
        // Beholder card
        int bh_discovered = progress_is_beholder_discovered();
        int bh_selected = (etype_selected == 1 && bh_discovered);

        aColor_t bg = bh_discovered
          ? (bh_selected ? (aColor_t){50, 50, 80, 240} : (aColor_t){30, 30, 50, 240})
          : (aColor_t){25, 25, 35, 240};
        a_DrawFilledRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          bg
        );

        aColor_t border = bh_discovered
          ? (bh_selected ? ETYPE_ACCENT_COLOR : (aColor_t){100, 100, 130, 200})
          : (aColor_t){60, 60, 70, 200};
        a_DrawRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          border
        );
        if ( bh_selected )
        {
          a_DrawRect(
            (aRectf_t){(float)(cx + 1), (float)(card_y + 1),
                       (float)(ETYPE_CARD_W - 2), (float)(ETYPE_CARD_H - 2)},
            border
          );
        }

        int ty = card_y + 20;

        if ( !bh_discovered )
        {
          aTextStyle_t lock_name = {
            .type = FONT_ENTER_COMMAND,
            .fg = {80, 80, 90, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( "???", mid_x, ty, lock_name );
          ty += 60;

          // Grey circle silhouette with closed eye
          a_DrawFilledCircle( mid_x, ty + 13, 13, (aColor_t){60, 60, 70, 255} );
          SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(app.renderer, 40, 40, 50, 255);
          SDL_RenderDrawLine(app.renderer, mid_x - 5, ty + 13, mid_x + 5, ty + 13);
          SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
          ty += 60;

          aTextStyle_t hint_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {100, 100, 120, 200},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          a_DrawText( "Survive 2:30 to", mid_x, ty, hint_style );
          a_DrawText( "discover this enemy", mid_x, ty + 18, hint_style );
        }
        else
        {
          const EnemyTypeCard_t* card = &beholder_card;

          aTextStyle_t name_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {255, 255, 255, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( card->name, mid_x, ty, name_style );
          ty += 34;

          etype_draw_shape( ENEMY_TYPE_BEHOLDER, mid_x, ty + 13, 26, ETYPE_SWATCH_COLOR );
          ty += 26 + 22;

          ty += 10;
          aTextStyle_t desc_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {180, 180, 200, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.55f
          };
          a_DrawText( card->desc, mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Fires sweeping beam attacks", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Regenerates shield when broken", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Must close distance to burst down", mid_x, ty, desc_style );
          ty += 24;

          aTextStyle_t hp_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = ETYPE_ACCENT_COLOR,
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.55f
          };
          a_DrawText( card->hp_line, mid_x, ty, hp_style );
          ty += 36;

          a_DrawFilledRect(
            (aRectf_t){(float)(cx + 16), (float)ty, (float)(ETYPE_CARD_W - 32), 1},
            (aColor_t){255, 255, 255, 40}
          );
          ty += 16;

          aTextStyle_t sub_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = ETYPE_ACCENT_COLOR,
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.6f
          };
          a_DrawText( "AI BEHAVIOR", mid_x, ty, sub_style );
          ty += 34;

          aTextStyle_t bullet_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {160, 160, 180, 220},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f,
            .wrap_width = ETYPE_CARD_W - 30
          };
          for ( int bi = 0; bi < 4 && card->behaviors[bi]; bi++ )
          {
            a_DrawText( card->behaviors[bi], mid_x, ty, bullet_style );
            ty += 46;
          }

          int kills = progress_get_lifetime_kills(ENEMY_TYPE_BEHOLDER);
          int pts   = progress_get_available_points(ENEMY_TYPE_BEHOLDER);
          char kills_buf[64], deaths_buf[64], pts_buf[64];
          snprintf( kills_buf, sizeof(kills_buf), "KILLS: %d", kills );
          snprintf( deaths_buf, sizeof(deaths_buf), "DEATHS: %d",
                    stats_get_deaths_by_type(ENEMY_TYPE_BEHOLDER) );
          snprintf( pts_buf, sizeof(pts_buf), "POINTS: %d", pts );

          aTextStyle_t kill_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {200, 200, 220, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          aTextStyle_t death_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {220, 80, 80, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          int bottom_y = card_y + ETYPE_CARD_H - 88;
          a_DrawText( kills_buf, mid_x, bottom_y, kill_style );
          a_DrawText( deaths_buf, mid_x, bottom_y + 18, death_style );
          a_DrawText( pts_buf, mid_x, bottom_y + 36, kill_style );

          int type_has_upg = progress_has_affordable_upgrade_for(ENEMY_TYPE_BEHOLDER);
          if ( type_has_upg )
          {
            float bob = sinf( (float)SDL_GetTicks() / 500.0f ) * 3.0f;
            aTextStyle_t upg_style = {
              .type = FONT_ENTER_COMMAND,
              .fg = {80, 220, 80, 255},
              .align = TEXT_ALIGN_CENTER,
              .scale = 0.5f
            };
            a_DrawText( "UPGRADE AVAILABLE", mid_x, bottom_y + 56 + (int)bob, upg_style );
          }
          else
          {
            aTextStyle_t select_style = {
              .type = FONT_ENTER_COMMAND,
              .fg = bh_selected ? ETYPE_ACCENT_COLOR : (aColor_t){120, 120, 140, 200},
              .align = TEXT_ALIGN_CENTER,
              .scale = 0.45f
            };
            a_DrawText( "[SELECT TO UPGRADE]", mid_x, bottom_y + 58, select_style );
          }
        }
      }
      else
      {
        // "Coming Soon" placeholder card
        a_DrawFilledRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          (aColor_t){20, 20, 30, 200}
        );
        a_DrawRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          (aColor_t){50, 50, 60, 180}
        );

        int ty = card_y + 20;
        aTextStyle_t cs_name = {
          .type = FONT_ENTER_COMMAND,
          .fg = {70, 70, 80, 255},
          .align = TEXT_ALIGN_CENTER,
          .scale = 0.8f
        };
        a_DrawText( "COMING SOON", mid_x, ty, cs_name );
        ty += 50;

        aTextStyle_t cs_q = {
          .type = FONT_ENTER_COMMAND,
          .fg = {50, 50, 60, 200},
          .align = TEXT_ALIGN_CENTER,
          .scale = 1.2f
        };
        a_DrawText( "???", mid_x, ty, cs_q );
        ty += 60;

        aTextStyle_t cs_hint = {
          .type = FONT_ENTER_COMMAND,
          .fg = {60, 60, 70, 180},
          .align = TEXT_ALIGN_CENTER,
          .scale = 0.5f,
          .wrap_width = ETYPE_CARD_W - 40
        };
        a_DrawText( "A new threat approaches...", mid_x, ty, cs_hint );
      }
    }
  }

  // Page indicator at bottom
  {
    int page_y = SCREEN_HEIGHT - 60;
    char page_buf[32];
    snprintf( page_buf, sizeof(page_buf), "Page %d / %d", etype_page + 1, ETYPE_PAGE_COUNT );
    aTextStyle_t page_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.7f
    };
    a_DrawText( page_buf, SCREEN_WIDTH / 2, page_y, page_style );

    // Page arrows with greyed/hover states
    int mx = app.mouse.x;
    int my = app.mouse.y;
    int arrow_w = 40, arrow_h = 30;
    int left_ax = SCREEN_WIDTH / 2 - 100 - arrow_w / 2;
    int right_ax = SCREEN_WIDTH / 2 + 100 - arrow_w / 2;
    int arrow_ay = page_y - 4;

    int can_left = (etype_page > 0);
    int can_right = (etype_page < ETYPE_PAGE_COUNT - 1);
    int hover_left = can_left && point_in_rect( mx, my, left_ax, arrow_ay, arrow_w, arrow_h );
    int hover_right = can_right && point_in_rect( mx, my, right_ax, arrow_ay, arrow_w, arrow_h );

    // Left arrow
    aColor_t left_color = !can_left ? (aColor_t){60, 60, 70, 150}
                         : hover_left ? (aColor_t){180, 255, 180, 255}
                         : (aColor_t){80, 220, 80, 255};
    if ( hover_left ) {
      a_DrawFilledRect(
        (aRectf_t){(float)left_ax, (float)arrow_ay, (float)arrow_w, (float)arrow_h},
        (aColor_t){80, 220, 80, 40}
      );
    }
    aTextStyle_t left_style = {
      .type = FONT_ENTER_COMMAND, .fg = left_color,
      .align = TEXT_ALIGN_CENTER, .scale = 0.9f
    };
    a_DrawText( "<<", SCREEN_WIDTH / 2 - 100, page_y - 2, left_style );

    // Right arrow
    aColor_t right_color = !can_right ? (aColor_t){60, 60, 70, 150}
                          : hover_right ? (aColor_t){180, 255, 180, 255}
                          : (aColor_t){80, 220, 80, 255};
    if ( hover_right ) {
      a_DrawFilledRect(
        (aRectf_t){(float)right_ax, (float)arrow_ay, (float)arrow_w, (float)arrow_h},
        (aColor_t){80, 220, 80, 40}
      );
    }
    aTextStyle_t right_style = {
      .type = FONT_ENTER_COMMAND, .fg = right_color,
      .align = TEXT_ALIGN_CENTER, .scale = 0.9f
    };
    a_DrawText( ">>", SCREEN_WIDTH / 2 + 100, page_y - 2, right_style );
  }

  // "BACK" button
  {
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = SCREEN_HEIGHT - 40;
    int hover = point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( hover )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
        (aColor_t){100, 180, 255, 40}
      );
    }
    a_DrawRect(
      (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
      hover ? (aColor_t){160, 210, 255, 255} : (aColor_t){80, 80, 110, 180}
    );
    aTextStyle_t back_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hover ? (aColor_t){220, 240, 255, 255} : (aColor_t){140, 140, 160, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText( "BACK [ESC]", SCREEN_WIDTH / 2, back_y + 6, back_style );
  }

  // Upgrade modal overlay
  if ( etype_modal_open )
  {
    // Dark overlay
    a_DrawFilledRect(
      (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
      (aColor_t){0, 0, 0, 160}
    );

    int pw = 500, ph = 360;
    int px = (SCREEN_WIDTH - pw) / 2;
    int py = (SCREEN_HEIGHT - ph) / 2;

    // Panel background
    a_DrawFilledRect(
      (aRectf_t){(float)px, (float)py, (float)pw, (float)ph},
      (aColor_t){25, 25, 50, 245}
    );
    a_DrawRect(
      (aRectf_t){(float)px, (float)py, (float)pw, (float)ph},
      (aColor_t){80, 220, 80, 255}
    );

    // Enemy name + available points
    int mcx = px + pw / 2;
    int mty = py + 16;
    aTextStyle_t modal_title = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.8f
    };
    const char* modal_name = (etype_modal_type < ETYPE_CARD_COUNT)
      ? etype_cards[etype_modal_type].name : snake_card.name;
    a_DrawText( modal_name, mcx, mty, modal_title );
    mty += 28;

    char pts_line[64];
    int avail = progress_get_available_points((EnemyType_t)etype_modal_type);
    snprintf( pts_line, sizeof(pts_line), "Available Points: %d", avail );
    aTextStyle_t pts_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {80, 220, 80, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    a_DrawText( pts_line, mcx, mty, pts_style );

    // Two track rows
    for ( int t = 0; t < 2; t++ )
    {
      ProgressUpgradeId_t id = etype_tracks[etype_modal_type][t];
      int tier = progress_get_tier(id);
      int max_tier = progress_get_max_tier(id);
      int cost = progress_get_next_cost(id);
      int can_buy = progress_can_afford(id);
      int selected = (t == etype_modal_track_sel);

      int row_y = py + 80 + t * 100;

      // Row background
      aColor_t row_bg = selected ? (aColor_t){40, 40, 70, 255} : (aColor_t){30, 30, 55, 255};
      a_DrawFilledRect(
        (aRectf_t){(float)(px + 20), (float)row_y, (float)(pw - 40), 90},
        row_bg
      );
      if ( selected )
      {
        a_DrawRect(
          (aRectf_t){(float)(px + 20), (float)row_y, (float)(pw - 40), 90},
          (aColor_t){80, 220, 80, 200}
        );
      }

      // Upgrade name
      aTextStyle_t name_s = {
        .type = FONT_ENTER_COMMAND,
        .fg = {255, 255, 255, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.55f
      };
      a_DrawText( progress_get_upgrade_name(id), px + 30, row_y + 8, name_s );

      // Tier pips
      int pip_x = px + 30;
      int pip_y = row_y + 34;
      for ( int p = 0; p < max_tier; p++ )
      {
        aColor_t pip_color = (p < tier)
          ? (aColor_t){80, 220, 80, 255}
          : (aColor_t){60, 60, 80, 255};
        a_DrawFilledRect(
          (aRectf_t){(float)pip_x, (float)pip_y, 16, 16},
          pip_color
        );
        a_DrawRect(
          (aRectf_t){(float)pip_x, (float)pip_y, 16, 16},
          (aColor_t){100, 100, 130, 200}
        );
        pip_x += 22;
      }

      // Cost or MAXED label
      aTextStyle_t cost_s = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 220, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.5f
      };
      if ( cost < 0 )
      {
        cost_s.fg = (aColor_t){80, 220, 80, 255};
        a_DrawText( "MAXED", pip_x + 10, pip_y + 2, cost_s );
      }
      else
      {
        char cost_buf[32];
        snprintf( cost_buf, sizeof(cost_buf), "Cost: %d", cost );
        a_DrawText( cost_buf, pip_x + 10, pip_y + 2, cost_s );
      }

      // Tier description
      if ( tier > 0 )
      {
        const char* desc = progress_get_tier_desc(id, tier - 1);
        aTextStyle_t desc_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = {160, 160, 180, 200},
          .align = TEXT_ALIGN_LEFT,
          .scale = 0.45f
        };
        a_DrawText( desc, px + 30, row_y + 60, desc_s );
      }

      // BUY indicator (right side)
      if ( can_buy && selected )
      {
        aTextStyle_t buy_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = {80, 220, 80, 255},
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.55f
        };
        a_DrawText( "BUY", px + pw - 40, row_y + 34, buy_s );
      }
    }

    // ESC hint
    aTextStyle_t esc_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {140, 140, 160, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( "ESC - CLOSE", mcx, py + ph - 24, esc_style );

    // Info box to the right of the modal for the selected track
    {
      ProgressUpgradeId_t sel_prog = etype_tracks[etype_modal_type][etype_modal_track_sel];
      int s_tier = progress_get_tier( sel_prog );
      int s_max = progress_get_max_tier( sel_prog );

      int iw = 240;
      int ih = 80 + s_max * 26;
      int ix = px + pw + 14;
      int sel_ry = py + 80 + etype_modal_track_sel * 100;
      int iy = sel_ry + 45 - ih / 2;

      // Clamp to screen
      if ( iy < 4 ) iy = 4;
      if ( iy + ih > SCREEN_HEIGHT - 4 ) iy = SCREEN_HEIGHT - 4 - ih;
      if ( ix + iw > SCREEN_WIDTH - 4 ) ix = px - iw - 14; // flip to left if no room

      // Background
      a_DrawFilledRect(
        (aRectf_t){(float)ix, (float)iy, (float)iw, (float)ih},
        (aColor_t){25, 25, 45, 240}
      );
      a_DrawRect(
        (aRectf_t){(float)ix, (float)iy, (float)iw, (float)ih},
        (aColor_t){80, 220, 80, 180}
      );

      int ty = iy + 10;

      // Detail text
      aTextStyle_t det_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {200, 200, 220, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.42f,
        .wrap_width = iw - 20
      };
      a_DrawText( progress_get_upgrade_detail( sel_prog ), ix + 10, ty, det_style );
      ty += 52;

      // Separator
      a_DrawFilledRect(
        (aRectf_t){(float)(ix + 10), (float)ty, (float)(iw - 20), 1},
        (aColor_t){255, 255, 255, 40}
      );
      ty += 8;

      // Tier breakdown
      for ( int t = 0; t < s_max; t++ )
      {
        int is_current = (t == s_tier - 1);
        int is_purchased = (t < s_tier);
        int is_next = (t == s_tier);

        char tline[64];
        snprintf( tline, sizeof(tline), "%s %d: %s",
                  is_next ? ">" : " ", t + 1,
                  progress_get_tier_desc( sel_prog, t ) );

        aColor_t tc;
        if ( is_current )
          tc = (aColor_t){120, 220, 120, 255};
        else if ( is_purchased )
          tc = (aColor_t){80, 220, 80, 180};
        else if ( is_next )
          tc = (aColor_t){255, 255, 255, 255};
        else
          tc = (aColor_t){100, 100, 120, 150};

        aTextStyle_t ts = {
          .type = FONT_ENTER_COMMAND,
          .fg = tc,
          .align = TEXT_ALIGN_LEFT,
          .scale = 0.42f
        };
        a_DrawText( tline, ix + 12, ty, ts );
        ty += 24;
      }
    }
  }
}

// ============================================================================
// Global Upgrades Scene
// ============================================================================

#define UPGRADES_PANEL_W 560
#define UPGRADES_PANEL_H 530
#define UPGRADES_ROW_H   68
#define UPGRADES_PER_PAGE 6

static int upgrades_page = 0;
static int upgrades_sorted[GLOBAL_COUNT];  // Indices sorted by cost (cheapest first)

static void upgrades_rebuild_sort( void )
{
  // Initialize with identity
  for ( int i = 0; i < GLOBAL_COUNT; i++ ) upgrades_sorted[i] = i;

  // Insertion sort by next cost (maxed = INT_MAX, so they go last)
  for ( int i = 1; i < GLOBAL_COUNT; i++ )
  {
    int key = upgrades_sorted[i];
    int key_cost = global_get_next_cost( (GlobalUpgradeId_t)key );
    if ( key_cost < 0 ) key_cost = 999999;  // maxed → sort last

    int j = i - 1;
    while ( j >= 0 )
    {
      int jc = global_get_next_cost( (GlobalUpgradeId_t)upgrades_sorted[j] );
      if ( jc < 0 ) jc = 999999;
      if ( jc <= key_cost ) break;
      upgrades_sorted[j + 1] = upgrades_sorted[j];
      j--;
    }
    upgrades_sorted[j + 1] = key;
  }
  upgrades_sorted_dirty = 0;
}

static void scene_upgrades_logic( float dt )
{
  (void)dt;

  if ( upgrades_sorted_dirty ) upgrades_rebuild_sort();

  int total_pages = (GLOBAL_COUNT + UPGRADES_PER_PAGE - 1) / UPGRADES_PER_PAGE;
  int page_start = upgrades_page * UPGRADES_PER_PAGE;
  int page_end = page_start + UPGRADES_PER_PAGE;
  if ( page_end > GLOBAL_COUNT ) page_end = GLOBAL_COUNT;

  // ESC / Backspace / click Back button → back to main menu
  {
    int upx = (SCREEN_WIDTH - UPGRADES_PANEL_W) / 2;
    int upy = (SCREEN_HEIGHT - UPGRADES_PANEL_H) / 2;
    int back_w = 100, back_h = 24;
    int back_x = upx + 8;
    int back_y = upy + UPGRADES_PANEL_H - back_h - 6;
    int esc = app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 || app.keyboard[ SDL_SCANCODE_BACKSPACE ] == 1;
    int clicked = app.mouse.button == 1 && app.mouse.pressed &&
                  point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( esc || clicked )
    {
      app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
      app.keyboard[ SDL_SCANCODE_BACKSPACE ] = 0;
      if ( clicked ) app.mouse.button = 0;
      current_scene = SCENE_MAIN_MENU;
      return;
    }
  }

  // Up/Down to navigate (wraps and auto-pages)
  if ( app.keyboard[ SDL_SCANCODE_UP ] == 1 || app.keyboard[ SDL_SCANCODE_W ] == 1 )
  {
    upgrades_sel = (upgrades_sel - 1 + GLOBAL_COUNT) % GLOBAL_COUNT;
    upgrades_page = upgrades_sel / UPGRADES_PER_PAGE;
    play_menu_move();
    app.keyboard[ SDL_SCANCODE_UP ] = 0;
    app.keyboard[ SDL_SCANCODE_W ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_DOWN ] == 1 || app.keyboard[ SDL_SCANCODE_S ] == 1 )
  {
    upgrades_sel = (upgrades_sel + 1) % GLOBAL_COUNT;
    upgrades_page = upgrades_sel / UPGRADES_PER_PAGE;
    play_menu_move();
    app.keyboard[ SDL_SCANCODE_DOWN ] = 0;
    app.keyboard[ SDL_SCANCODE_S ] = 0;
  }

  // Left/Right to switch pages
  if ( app.keyboard[ SDL_SCANCODE_LEFT ] == 1 || app.keyboard[ SDL_SCANCODE_A ] == 1 )
  {
    if ( upgrades_page > 0 )
    {
      upgrades_page--;
      upgrades_sel = upgrades_page * UPGRADES_PER_PAGE;
      play_menu_move();
    }
    app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
    app.keyboard[ SDL_SCANCODE_A ] = 0;
  }
  if ( app.keyboard[ SDL_SCANCODE_RIGHT ] == 1 || app.keyboard[ SDL_SCANCODE_D ] == 1 )
  {
    if ( upgrades_page < total_pages - 1 )
    {
      upgrades_page++;
      upgrades_sel = upgrades_page * UPGRADES_PER_PAGE;
      play_menu_move();
    }
    app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
    app.keyboard[ SDL_SCANCODE_D ] = 0;
  }

  // Enter/Space to purchase
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 ||
       app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    play_menu_click();
    if ( global_purchase( (GlobalUpgradeId_t)upgrades_sorted[upgrades_sel] ) )
      upgrades_sorted_dirty = 1;
  }

  // Mouse hover and click
  int mx = app.mouse.x;
  int my = app.mouse.y;
  int px = (SCREEN_WIDTH - UPGRADES_PANEL_W) / 2;
  int py = (SCREEN_HEIGHT - UPGRADES_PANEL_H) / 2;
  int row_start_y = py + 70;

  // Hover on rows (current page only) — only when mouse has moved
  if ( mouse_moved_this_frame )
  {
    for ( int i = page_start; i < page_end; i++ )
    {
      int row_idx = i - page_start;
      int ry = row_start_y + row_idx * UPGRADES_ROW_H;
      if ( point_in_rect( mx, my, px + 10, ry, UPGRADES_PANEL_W - 20, UPGRADES_ROW_H - 4 ) )
      {
        if ( upgrades_sel != i )
        {
          upgrades_sel = i;
          play_menu_move();
        }
        break;
      }
    }
  }

  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    // Click on rows
    for ( int i = page_start; i < page_end; i++ )
    {
      int row_idx = i - page_start;
      int ry = row_start_y + row_idx * UPGRADES_ROW_H;
      if ( point_in_rect( mx, my, px + 10, ry, UPGRADES_PANEL_W - 20, UPGRADES_ROW_H - 4 ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        if ( global_purchase( (GlobalUpgradeId_t)upgrades_sorted[i] ) )
          upgrades_sorted_dirty = 1;
        return;
      }
    }

    // Click on Prev button
    if ( upgrades_page > 0 )
    {
      int btn_y = py + UPGRADES_PANEL_H - 42;
      int prev_x = px + 12;
      if ( point_in_rect( mx, my, prev_x, btn_y, 100, 28 ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        upgrades_page--;
        upgrades_sel = upgrades_page * UPGRADES_PER_PAGE;
        return;
      }
    }

    // Click on Next button
    if ( upgrades_page < total_pages - 1 )
    {
      int btn_y = py + UPGRADES_PANEL_H - 42;
      int next_x = px + UPGRADES_PANEL_W - 112;
      if ( point_in_rect( mx, my, next_x, btn_y, 100, 28 ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        upgrades_page++;
        upgrades_sel = upgrades_page * UPGRADES_PER_PAGE;
        return;
      }
    }

    app.mouse.pressed = 0;
  }
}

static void scene_upgrades_draw( float dt )
{
  (void)dt;

  int px = (SCREEN_WIDTH - UPGRADES_PANEL_W) / 2;
  int py = (SCREEN_HEIGHT - UPGRADES_PANEL_H) / 2;

  // Panel background
  a_DrawFilledRect(
    (aRectf_t){(float)px, (float)py, (float)UPGRADES_PANEL_W, (float)UPGRADES_PANEL_H},
    (aColor_t){30, 30, 50, 240}
  );
  a_DrawRect(
    (aRectf_t){(float)px, (float)py, (float)UPGRADES_PANEL_W, (float)UPGRADES_PANEL_H},
    (aColor_t){150, 150, 200, 255}
  );

  int cx = px + UPGRADES_PANEL_W / 2;

  // Title
  aTextStyle_t title_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.9f
  };
  a_DrawText( "GLOBAL UPGRADES", cx, py + 12, title_style );

  // Subtitle: points
  char pts_buf[48];
  int pts = progress_get_upgrade_points();
  snprintf( pts_buf, sizeof(pts_buf), "Upgrade Points: %d", pts );
  aColor_t pts_color = global_has_affordable()
    ? (aColor_t){80, 220, 80, 255}
    : (aColor_t){255, 215, 0, 255};
  aTextStyle_t pts_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = pts_color,
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.5f
  };
  a_DrawText( pts_buf, cx, py + 40, pts_style );

  int total_pages = (GLOBAL_COUNT + UPGRADES_PER_PAGE - 1) / UPGRADES_PER_PAGE;
  int page_start = upgrades_page * UPGRADES_PER_PAGE;
  int page_end = page_start + UPGRADES_PER_PAGE;
  if ( page_end > GLOBAL_COUNT ) page_end = GLOBAL_COUNT;
  // Navigation buttons and hints at bottom
  int nav_y = py + UPGRADES_PANEL_H - 42;

  if ( upgrades_page > 0 )
  {
    // Prev button
    a_DrawFilledRect(
      (aRectf_t){(float)(px + 12), (float)nav_y, 100, 28},
      (aColor_t){50, 50, 80, 220}
    );
    a_DrawRect(
      (aRectf_t){(float)(px + 12), (float)nav_y, 100, 28},
      (aColor_t){180, 180, 255, 200}
    );
    aTextStyle_t prev_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText( "< PREV [A]", px + 62, nav_y + 8, prev_style );
  }

  if ( upgrades_page < total_pages - 1 )
  {
    // Next button
    int next_x = px + UPGRADES_PANEL_W - 112;
    a_DrawFilledRect(
      (aRectf_t){(float)next_x, (float)nav_y, 100, 28},
      (aColor_t){50, 50, 80, 220}
    );
    a_DrawRect(
      (aRectf_t){(float)next_x, (float)nav_y, 100, 28},
      (aColor_t){180, 180, 255, 200}
    );
    aTextStyle_t next_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText( "[D] NEXT >", next_x + 50, nav_y + 8, next_style );
  }

  // Page indicator
  if ( total_pages > 1 )
  {
    char page_buf[16];
    snprintf( page_buf, sizeof(page_buf), "%d / %d", upgrades_page + 1, total_pages );
    aTextStyle_t page_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {150, 150, 170, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.4f
    };
    a_DrawText( page_buf, cx, nav_y + 8, page_style );
  }

  // "BACK" button
  {
    int back_w = 100, back_h = 24;
    int back_x = px + 8;
    int back_y = py + UPGRADES_PANEL_H - back_h - 6;
    int hover = point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( hover )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
        (aColor_t){100, 180, 255, 40}
      );
    }
    a_DrawRect(
      (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
      hover ? (aColor_t){160, 210, 255, 255} : (aColor_t){80, 80, 110, 180}
    );
    aTextStyle_t back_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hover ? (aColor_t){220, 240, 255, 255} : (aColor_t){150, 150, 170, 180},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.38f
    };
    a_DrawText( "BACK [ESC]", back_x + back_w / 2, back_y + 5, back_style );
  }

  // Rows (current page only)
  int row_start_y = py + 70;

  for ( int i = page_start; i < page_end; i++ )
  {
    GlobalUpgradeId_t id = (GlobalUpgradeId_t)upgrades_sorted[i];
    int row_idx = i - page_start;
    int ry = row_start_y + row_idx * UPGRADES_ROW_H;
    int selected = (i == upgrades_sel);
    int tier = global_get_tier( id );
    int max_tier = global_get_max_tier( id );
    int maxed = (tier >= max_tier);
    int affordable = global_can_afford( id );

    // Row background
    aColor_t row_bg = selected
      ? (aColor_t){50, 50, 80, 220}
      : (aColor_t){35, 35, 55, 180};
    a_DrawFilledRect(
      (aRectf_t){(float)(px + 10), (float)ry,
                 (float)(UPGRADES_PANEL_W - 20), (float)(UPGRADES_ROW_H - 4)},
      row_bg
    );

    // Row border (highlight if selected)
    if ( selected )
    {
      a_DrawRect(
        (aRectf_t){(float)(px + 10), (float)ry,
                   (float)(UPGRADES_PANEL_W - 20), (float)(UPGRADES_ROW_H - 4)},
        (aColor_t){200, 200, 255, 200}
      );
    }

    int lx = px + 22; // left text x
    int text_y = ry + 6;

    // Upgrade name
    aTextStyle_t name_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.55f
    };
    a_DrawText( global_get_name( id ), lx, text_y, name_style );

    // Short description
    aTextStyle_t desc_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {160, 160, 180, 200},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.35f
    };
    a_DrawText( global_get_desc( id ), lx, text_y + 20, desc_style );

    // Current tier effect text
    if ( tier > 0 )
    {
      aTextStyle_t effect_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {120, 220, 120, 220},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.35f
      };
      a_DrawText( global_get_tier_desc( id, tier - 1 ), lx, text_y + 36, effect_style );
    }

    // Tier pips (centered-ish, after the name area)
    int pip_x = px + 300;
    int pip_y = ry + 10;
    int pip_sz = 10;
    int pip_gap = 5;
    for ( int p = 0; p < max_tier; p++ )
    {
      aColor_t pip_color = (p < tier)
        ? (aColor_t){255, 215, 0, 255}    // gold filled
        : (aColor_t){80, 80, 100, 150};   // gray empty
      a_DrawFilledRect(
        (aRectf_t){(float)(pip_x + p * (pip_sz + pip_gap)), (float)pip_y,
                   (float)pip_sz, (float)pip_sz},
        pip_color
      );
      a_DrawRect(
        (aRectf_t){(float)(pip_x + p * (pip_sz + pip_gap)), (float)pip_y,
                   (float)pip_sz, (float)pip_sz},
        (aColor_t){255, 255, 255, 80}
      );
    }

    // Cost or MAXED (right side)
    int right_x = px + UPGRADES_PANEL_W - 30;
    if ( maxed )
    {
      aTextStyle_t maxed_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {255, 215, 0, 200},
        .align = TEXT_ALIGN_RIGHT,
        .scale = 0.5f
      };
      a_DrawText( "MAXED", right_x, ry + 12, maxed_style );
    }
    else
    {
      char cost_buf[32];
      snprintf( cost_buf, sizeof(cost_buf), "%d pts", global_get_next_cost( id ) );
      aColor_t cost_color = affordable
        ? (aColor_t){255, 255, 255, 255}
        : (aColor_t){120, 120, 120, 200};
      aTextStyle_t cost_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = cost_color,
        .align = TEXT_ALIGN_RIGHT,
        .scale = 0.45f
      };
      a_DrawText( cost_buf, right_x, ry + 8, cost_style );

      // "BUY" indicator when affordable + selected
      if ( affordable && selected )
      {
        aTextStyle_t buy_style = {
          .type = FONT_ENTER_COMMAND,
          .fg = {80, 220, 80, 255},
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.45f
        };
        a_DrawText( "BUY", right_x, ry + 30, buy_style );
      }
      else if ( !affordable )
      {
        aTextStyle_t locked_style = {
          .type = FONT_ENTER_COMMAND,
          .fg = {100, 100, 100, 150},
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.35f
        };
        a_DrawText( "insufficient", right_x, ry + 30, locked_style );
      }
    }
  }

  // Info box to the left of the panel for the selected upgrade
  {
    GlobalUpgradeId_t sel_id = (GlobalUpgradeId_t)upgrades_sorted[upgrades_sel];
    int sel_tier = global_get_tier( sel_id );
    int sel_max = global_get_max_tier( sel_id );

    int info_w = 240;
    int info_h = 80 + sel_max * 26;
    int info_x = px - info_w - 14;
    int sel_row_y = row_start_y + (upgrades_sel - page_start) * UPGRADES_ROW_H;
    int info_y = sel_row_y + (UPGRADES_ROW_H - 4) / 2 - info_h / 2;

    // Clamp to screen
    if ( info_y < 4 ) info_y = 4;
    if ( info_y + info_h > SCREEN_HEIGHT - 4 ) info_y = SCREEN_HEIGHT - 4 - info_h;
    if ( info_x < 4 ) info_x = 4;

    // Background
    a_DrawFilledRect(
      (aRectf_t){(float)info_x, (float)info_y, (float)info_w, (float)info_h},
      (aColor_t){25, 25, 45, 240}
    );
    a_DrawRect(
      (aRectf_t){(float)info_x, (float)info_y, (float)info_w, (float)info_h},
      (aColor_t){200, 200, 255, 180}
    );

    int iy = info_y + 10;

    // Detail text (wrapping description)
    aTextStyle_t detail_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 220, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.42f,
      .wrap_width = info_w - 20
    };
    a_DrawText( global_get_detail( sel_id ), info_x + 10, iy, detail_style );
    iy += 52;

    // Separator
    a_DrawFilledRect(
      (aRectf_t){(float)(info_x + 10), (float)iy, (float)(info_w - 20), 1},
      (aColor_t){255, 255, 255, 40}
    );
    iy += 8;

    // Tier breakdown
    for ( int t = 0; t < sel_max; t++ )
    {
      int is_current = (t == sel_tier - 1);
      int is_purchased = (t < sel_tier);
      int is_next = (t == sel_tier);

      char tier_line[64];
      snprintf( tier_line, sizeof(tier_line), "%s %d: %s",
                is_next ? ">" : " ", t + 1,
                global_get_tier_desc( sel_id, t ) );

      aColor_t tier_color;
      if ( is_current )
        tier_color = (aColor_t){120, 220, 120, 255};  // green - active
      else if ( is_purchased )
        tier_color = (aColor_t){255, 215, 0, 180};    // gold - done
      else if ( is_next )
        tier_color = (aColor_t){255, 255, 255, 255};  // white - next
      else
        tier_color = (aColor_t){100, 100, 120, 150};  // dim - locked

      aTextStyle_t tier_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = tier_color,
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.42f
      };
      a_DrawText( tier_line, info_x + 12, iy, tier_style );
      iy += 24;
    }
  }

  // "How to earn points" help box on the right
  {
    int help_w = 210;
    int help_h = 130;
    int help_x = px + UPGRADES_PANEL_W + 14;
    int help_y = py + (UPGRADES_PANEL_H - help_h) / 2;

    a_DrawFilledRect(
      (aRectf_t){(float)help_x, (float)help_y, (float)help_w, (float)help_h},
      (aColor_t){25, 25, 45, 240}
    );
    a_DrawRect(
      (aRectf_t){(float)help_x, (float)help_y, (float)help_w, (float)help_h},
      (aColor_t){150, 150, 200, 120}
    );

    int hy = help_y + 10;
    aTextStyle_t help_title = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 215, 0, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.45f
    };
    a_DrawText( "HOW TO EARN POINTS", help_x + 10, hy, help_title );
    hy += 28;

    aTextStyle_t help_body = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 200, 220},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.36f
    };
    a_DrawText( "Level up during a run", help_x + 10, hy, help_body );
    hy += 18;
    a_DrawText( "to earn global points.", help_x + 10, hy, help_body );
    hy += 28;
    a_DrawText( "Each level-up grants", help_x + 10, hy, help_body );
    hy += 18;
    a_DrawText( "points equal to your", help_x + 10, hy, help_body );
    hy += 18;
    a_DrawText( "current level.", help_x + 10, hy, help_body );
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
  snake_cleanup();
  collision_cleanup();
  hotbar_cleanup();
  fire_particles_cleanup();

  // Reset main.c timers
  time_remaining = 15.0f * 60.0f;
  director_reset();

  // Re-initialize all game systems
  player_init();
  enemy_init( MAX_ENEMIES, MAX_BLOOD_PARTICLES );
  snake_init();
  fire_particles_init( MAX_FIRE_PARTICLES );
  collision_init( MAX_ENEMIES );
  weapons_init();
  drops_init();
  pickups_init();
  hotbar_init();
  xp_reset();
  upgrades_reset();
  upgrades_reset_rerolls();
  stats_reset();
  stats_increment_runs();
  level_up_active = 0;
  free_upgrade_active = 0;
  game_paused = 0;
  death_active = 0;
  death_timer = 0.0f;

  // Free wand upgrade on game start (requires meta-progression purchase)
  if ( wprog_has_free_upgrade(WID_WAND) )
  {
    level_up_card_count = upgrades_roll_cards_for_weapon(WEAPON_WAND, level_up_cards);
    if (level_up_card_count > 0) {
      level_up_active = 1;
      free_upgrade_active = 1;
      level_up_selected = 0;
    }
  }

  game_audio_restart_music();

  current_scene = SCENE_GAME;
}

void aMainloop( void )
{
  Uint32 frame_start = SDL_GetTicks();

  a_PrepareScene();

  float dt = a_GetDeltaTime();
  app.delegate.logic( dt );
  app.delegate.draw( dt );

  a_PresentScene();

  Uint32 frame_time = SDL_GetTicks() - frame_start;
  if ( frame_time < (Uint32)LOGIC_RATE )
  {
    SDL_Delay( (Uint32)LOGIC_RATE - frame_time );
  }
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
