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
#include "main_menu.h"
#include "achievements.h"
#include "achievements_menu.h"
#include "weapons_menu.h"
#include "enemy_types_menu.h"
#include "upgrades_menu.h"

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
  SCENE_SETTINGS,
  SCENE_ACHIEVEMENTS,
  SCENE_STATS_DEEP_DIVE,
  SCENE_CHALLENGES
} Scene_t;

Scene_t current_scene = SCENE_MAIN_MENU;

// FPS counter
static int show_fps = 0;
static float fps_accumulator = 0.0f;
static int fps_frame_count = 0;
static int fps_display = 0;

// Scene function declarations
static void scene_game_logic( float dt );
static void scene_game_draw( float dt );
static void scene_game_over_logic( float dt );
static void scene_game_over_draw( float dt );
void game_reset( void );
static void scene_settings_logic( float dt );
static void scene_settings_draw( float dt );
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

// Settings scene state
int settings_sel = 0;          // 0=SFX, 1=Music, 2=Delete
int settings_confirm = 0;      // 0=off, 1=first confirm, 2=second confirm
static int settings_confirm_sel = 0;  // 0=YES, 1=NO
Scene_t settings_return_scene; // where to go back on ESC
int settings_dragging = 0;     // -1=none, 0=SFX bar, 1=Music bar


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
static UpgradeId_t level_up_cards[MAX_LEVEL_UP_CARDS];
static int level_up_card_count = 0;
static int level_up_selected = 0;  // Currently highlighted card index
static int free_upgrade_active = 0; // 1 = showing free weapon upgrade (not a level-up)
static int free_upgrade_weapon = 0; // WeaponType_t of the free upgrade (for reroll)
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
  achievements_init();

  // Load menu sounds
  main_menu_init();

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
          if ( game_paused ) {
            player_clear_screen_flashes();
            enemy_stop_beholder_sounds();
          }
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
    case SCENE_ACHIEVEMENTS:
      scene_achievements_logic( dt );
      break;
    case SCENE_STATS_DEEP_DIVE:
    case SCENE_CHALLENGES:
    {
      // ESC / Backspace -> back
      if ( app.keyboard[SDL_SCANCODE_ESCAPE] == 1 || app.keyboard[SDL_SCANCODE_BACKSPACE] == 1 ) {
        app.keyboard[SDL_SCANCODE_ESCAPE] = 0;
        app.keyboard[SDL_SCANCODE_BACKSPACE] = 0;
        current_scene = SCENE_MAIN_MENU;
        mainmenu_begin();
      }
      // Mouse click on back button
      if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT ) {
        int back_w = 120, back_h = 34;
        int back_x = SCREEN_WIDTH / 2 - back_w / 2;
        int back_y = SCREEN_HEIGHT - 46;
        if ( menu_point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h ) ) {
          app.mouse.pressed = 0;
          current_scene = SCENE_MAIN_MENU;
          mainmenu_begin();
        }
      }
      break;
    }
  }

  // Achievement popup (always ticks regardless of scene)
  achievements_update_popup( dt );
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
      progress_bank_source_hits(
        weapons_get_total_hits(SOURCE_FIRE_CONE),
        weapons_get_total_hits(SOURCE_CONDUCTOR)
      );

      int run_up = xp_get_pending_upgrade_points();
      if (run_up > 0) progress_bank_upgrade_points(run_up);

      // Check achievements (after kills/hits are banked)
      achievements_check_all();

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
      enemy_stop_beholder_sounds();

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
  // Player keeps full speed; everything else slows down
  float player_dt = dt;
  if ( !death_active )
  {
    int danger = player_get_danger_tier();
    float hp_slow = 1.0f - 0.05f * danger;
    dt *= hp_slow;
  }

  // Update player movement and shooting (unslowed)
  player_update( player_dt );

  // Check for player death — start death slowdown
  if ( !player_is_alive() && !death_active )
  {
    death_active = 1;
    death_timer = 0.0f;
    player_clear_screen_flashes();
    player_start_death();
    enemy_stop_beholder_sounds();
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
          free_upgrade_weapon = picked;
          level_up_selected = 0;
          player_clear_screen_flashes();
          enemy_stop_beholder_sounds();
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
    int consumed = pickups_consume_nearest( px, py, pickups_get_collect_dist() );
    if ( consumed >= 0 )
    {
      player_apply_buff( (PickupType_t)consumed );
      stats_record_pickup_collected(consumed);
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
      enemy_stop_beholder_sounds();
    }
  }

  // Check achievements mid-game (survival milestones, etc.)
  achievements_check_all();

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
    case SCENE_ACHIEVEMENTS:
      scene_achievements_draw( dt );
      break;
    case SCENE_STATS_DEEP_DIVE:
    case SCENE_CHALLENGES:
    {
      a_DrawFilledRect(
        (aRectf_t){0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT},
        (aColor_t){20, 20, 40, 255}
      );
      const char* cs_title = (current_scene == SCENE_STATS_DEEP_DIVE)
                              ? "STATS DEEP DIVE" : "CHALLENGES";
      aColor_t cs_color = (current_scene == SCENE_STATS_DEEP_DIVE)
                          ? (aColor_t){255, 220, 80, 255}
                          : (aColor_t){220, 80, 80, 255};
      aTextStyle_t title_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = cs_color,
        .align = TEXT_ALIGN_CENTER,
        .scale = 1.2f
      };
      a_DrawText( cs_title, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 40, title_st );
      aTextStyle_t sub_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = {160, 160, 180, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.6f
      };
      a_DrawText( "coming soon", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 10, sub_st );
      // Back button
      int back_w = 120, back_h = 34;
      int back_x = SCREEN_WIDTH / 2 - back_w / 2;
      int back_y = SCREEN_HEIGHT - 46;
      int bk_hov = menu_point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
      aColor_t bk_bg = bk_hov ? (aColor_t){70, 70, 110, 255} : (aColor_t){45, 45, 65, 255};
      aColor_t bk_bd = bk_hov ? (aColor_t){255, 255, 255, 255} : (aColor_t){120, 120, 150, 200};
      a_DrawFilledRect( (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h}, bk_bg );
      a_DrawRect( (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h}, bk_bd );
      aTextStyle_t bk_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = bk_hov ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 180, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.65f
      };
      a_DrawText( "BACK", back_x + back_w / 2, back_y + 6, bk_st );
      break;
    }
  }

  // Draw hotbar and screen flash (game scene only)
  if ( current_scene == SCENE_GAME )
  {
    hotbar_draw();
    hotbar_draw_tooltip();
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

  // Achievement popup (drawn on top of everything)
  achievements_draw_popup();

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

// Main Menu Scene — see src/main_menu.c
// Hotbar Tooltip — see src/hotbar.c

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
      settings_dragging = -1;
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
  WeaponType_t bonus_sources_pre[] = { SOURCE_FIRE_CONE, SOURCE_CONDUCTOR, SOURCE_GRUNT_EXPLOSION, SOURCE_SNAKE_POP };
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
  WeaponType_t bonus_sources[] = { SOURCE_FIRE_CONE, SOURCE_CONDUCTOR, SOURCE_GRUNT_EXPLOSION, SOURCE_SNAKE_POP };
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

  hotbar_draw_tooltip();

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
  if ( app.keyboard[ SDL_SCANCODE_4 ] == 1 && level_up_card_count >= 4 )
  {
    play_menu_click();
    level_up_select( 3 );
    app.keyboard[ SDL_SCANCODE_4 ] = 0;
    return;
  }
  if ( app.keyboard[ SDL_SCANCODE_5 ] == 1 && level_up_card_count >= 5 )
  {
    play_menu_click();
    level_up_select( 4 );
    app.keyboard[ SDL_SCANCODE_5 ] = 0;
    return;
  }

  // R key to reroll
  if ( app.keyboard[ SDL_SCANCODE_R ] == 1 && upgrades_get_rerolls() > 0 )
  {
    app.keyboard[ SDL_SCANCODE_R ] = 0;
    upgrades_use_reroll();
    if ( free_upgrade_active )
      level_up_card_count = upgrades_roll_cards_for_weapon( free_upgrade_weapon, level_up_cards );
    else
      level_up_card_count = upgrades_roll_cards( level_up_cards );
    level_up_selected = 0;
    if ( levelup_flex ) { a_FlexBoxDestroy( &levelup_flex ); }
    level_up_begin();
  }

  // Mouse hover on cards (only when mouse actually moved)
  if ( menu_mouse_moved() && levelup_flex )
  {
    int mx = app.mouse.x;
    int my = app.mouse.y;
    for ( int i = 0; i < level_up_card_count; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( levelup_flex, i );
      if ( item && point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h ) )
      {
        if ( level_up_selected != i )
        {
          level_up_selected = i;
          play_menu_move();
        }
        break;
      }
    }
  }

  // Mouse click on card (or reroll button)
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    int mx = app.mouse.x;
    int my = app.mouse.y;

    // Check reroll button click
    if ( upgrades_get_rerolls() > 0 && levelup_flex )
    {
      int btn_w = 140, btn_h = 32;
      int btn_x = SCREEN_WIDTH / 2 - btn_w / 2;
      int btn_y = levelup_flex->y + levelup_flex->h + 12;
      if ( point_in_rect( mx, my, btn_x, btn_y, btn_w, btn_h ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        upgrades_use_reroll();
        if ( free_upgrade_active )
          level_up_card_count = upgrades_roll_cards_for_weapon( free_upgrade_weapon, level_up_cards );
        else
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
                levelup_flex->y - 76, title_style );

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

  // Reroll button
  if ( upgrades_get_rerolls() > 0 )
  {
    int btn_w = 140, btn_h = 32;
    int btn_x = SCREEN_WIDTH / 2 - btn_w / 2;
    int btn_y = levelup_flex->y + levelup_flex->h + 12;
    int hover = point_in_rect( app.mouse.x, app.mouse.y, btn_x, btn_y, btn_w, btn_h );

    // Button background
    a_DrawFilledRect(
      (aRectf_t){(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
      hover ? (aColor_t){70, 70, 110, 240} : (aColor_t){50, 50, 80, 230}
    );
    a_DrawRect(
      (aRectf_t){(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
      hover ? (aColor_t){220, 220, 255, 255} : (aColor_t){180, 180, 255, 200}
    );

    // Button text
    char reroll_text[32];
    snprintf( reroll_text, sizeof(reroll_text), "REROLL (%d)  [R]", upgrades_get_rerolls() );
    aTextStyle_t reroll_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hover ? (aColor_t){220, 220, 255, 255} : (aColor_t){180, 180, 255, 255},
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
      settings_dragging = -1;
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
      case ENEMY_TYPE_BEHOLDER: {
        // Matches in-game: tendrils + body circle + dark pupil
        float bcx_kb = (float)kb_mid;
        float bcy_kb = (float)kb_ty + (float)shape_size / 2.0f;
        int body_r = shape_size / 2;
        float scale = (float)shape_size / 28.0f;
        // Tendrils
        for (int t = 0; t < 4; t++) {
          float ta_kb = ((float)t * 0.5f * 3.14159f) + 0.785f;
          for (int seg = 0; seg < 3; seg++) {
            float ext = (6.0f + (float)seg * 5.0f) * scale;
            float base_off = 14.0f * scale;
            float tx = bcx_kb + cosf(ta_kb) * (base_off + ext);
            float ty = bcy_kb + sinf(ta_kb) * (base_off + ext);
            float seg_sz = 4.0f * scale;
            if (seg_sz < 2.0f) seg_sz = 2.0f;
            a_DrawFilledRect(
              (aRectf_t){tx - seg_sz / 2, ty - seg_sz / 2, seg_sz, seg_sz},
              shape_c
            );
          }
        }
        // Body
        a_DrawFilledCircle((int)bcx_kb, (int)bcy_kb, body_r, shape_c);
        // Pupil
        int pupil_r = body_r / 4;
        if (pupil_r < 2) pupil_r = 2;
        a_DrawFilledCircle((int)bcx_kb, (int)bcy_kb, pupil_r, (aColor_t){20, 20, 30, 255});
        break;
      }
      case ENEMY_TYPE_MIMIC: {
        // Chest: base, lid with mouth gap, rounded top, gold clasp
        float sz = (float)shape_size;
        float mcx = (float)kb_mid;
        float base_h = sz * 0.55f;
        float lid_h  = sz * 0.45f;
        float mouth_gap = lid_h * 0.35f;
        float base_y = (float)kb_ty + lid_h + mouth_gap;
        float lid_y  = (float)kb_ty;

        // Lid
        a_DrawFilledRect(
          (aRectf_t){mcx - sz / 2.0f, lid_y, sz, lid_h},
          shape_c
        );
        // Lid top arc
        a_DrawFilledRect(
          (aRectf_t){mcx - sz * 0.4f, lid_y - 3, sz * 0.8f, 4},
          shape_c
        );
        // Base
        a_DrawFilledRect(
          (aRectf_t){mcx - sz / 2.0f, base_y, sz, base_h},
          shape_c
        );
        // Gold clasp
        a_DrawFilledRect(
          (aRectf_t){mcx - 2, base_y - 1, 4, 4},
          (aColor_t){200, 170, 50, 255}
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
      [ENEMY_TYPE_GRUNT]    = "GRUNT",
      [ENEMY_TYPE_DASHER]   = "DASHER",
      [ENEMY_TYPE_BRUTE]    = "BRUTE",
      [ENEMY_TYPE_SHAMAN]   = "SHAMAN",
      [ENEMY_TYPE_SNAKE]    = "SNAKE",
      [ENEMY_TYPE_BEHOLDER] = "BEHOLDER",
      [ENEMY_TYPE_MIMIC]    = "MIMIC",
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
          achievements_init();
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
    sm.count    = (settings_return_scene == SCENE_MAIN_MENU) ? SETTINGS_ROW_COUNT : 2;
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
      else if ( settings_sel == 0 || settings_sel == 1 )
      {
        // Start dragging the volume bar
        settings_dragging = settings_sel;
      }
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

  // Click-and-drag on volume bars
  {
    int panel_w = 560, panel_h = 280;
    int px = (SCREEN_WIDTH - panel_w) / 2;
    int py = (SCREEN_HEIGHT - panel_h) / 2;
    int row_x = px + 20;
    int row_y_base = py + 60;

    int bar_x = row_x + 160;
    int bar_w = 320;
    int bar_h = 20;
    int sfx_bar_y   = row_y_base + 4;
    int music_bar_y  = row_y_base + 60 + 4;

    // Start drag on click
    if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
    {
      int mx = app.mouse.x, my = app.mouse.y;
      if ( point_in_rect( mx, my, bar_x, sfx_bar_y, bar_w, bar_h ) )
      {
        settings_dragging = 0;
        settings_sel = 0;
      }
      else if ( point_in_rect( mx, my, bar_x, music_bar_y, bar_w, bar_h ) )
      {
        settings_dragging = 1;
        settings_sel = 1;
      }
    }

    // Stop drag on release
    if ( app.mouse.state == SDL_RELEASED && settings_dragging >= 0 )
    {
      save_data_flush();
      settings_dragging = -1;
    }

    // Scrub while dragging (or on initial click)
    if ( settings_dragging >= 0 )
    {
      int vol = ((app.mouse.x - bar_x) * 128) / bar_w;
      if ( vol < 0 ) vol = 0;
      if ( vol > 128 ) vol = 128;

      if ( settings_dragging == 0 )
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
    }
  }

  // Mouse click on Back button
  if ( !settings_confirm && app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT )
  {
    int panel_h = 280;
    int py = (SCREEN_HEIGHT - panel_h) / 2;
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = py + panel_h - 36;
    if ( point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h ) )
    {
      app.mouse.pressed = 0;
      play_menu_click();
      if ( settings_return_scene == SCENE_GAME )
      {
        game_paused = 1;
        pause_selected = 2;
      }
      current_scene = settings_return_scene;
      return;
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

  // Row 2: Delete Save Data (only from main menu)
  if ( settings_return_scene == SCENE_MAIN_MENU )
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

  // Back button
  if ( !settings_confirm )
  {
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = py + panel_h - 36;
    int bmx = app.mouse.x, bmy = app.mouse.y;
    int back_hover = point_in_rect( bmx, bmy, back_x, back_y, back_w, back_h );

    aColor_t back_bg = back_hover ? (aColor_t){60, 60, 90, 255} : (aColor_t){40, 40, 60, 200};
    aColor_t back_border = back_hover ? (aColor_t){180, 180, 220, 255} : (aColor_t){100, 100, 140, 200};
    a_DrawFilledRect(
      (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
      back_bg
    );
    a_DrawRect(
      (aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h},
      back_border
    );
    aTextStyle_t back_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = back_hover ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 200, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    a_DrawText( "BACK", SCREEN_WIDTH / 2, back_y + 5, back_style );
  }

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
// Game Reset
// ============================================================================

void game_reset( void )
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
  achievements_clear_popups();
  level_up_active = 0;
  free_upgrade_active = 0;
  game_paused = 0;
  death_active = 0;
  death_timer = 0.0f;

  // Free starting weapon upgrade on game start (requires meta-progression purchase)
  {
    const Weapon_t* start_w = weapons_get_slot(0);
    if (start_w && start_w->type >= WEAPON_WAND && start_w->type <= WEAPON_TRAIL) {
      WeaponId_t wid = (WeaponId_t)(start_w->type - 1);
      if (wprog_has_free_upgrade(wid)) {
        level_up_card_count = upgrades_roll_cards_for_weapon(start_w->type, level_up_cards);
        if (level_up_card_count > 0) {
          level_up_active = 1;
          free_upgrade_active = 1;
          free_upgrade_weapon = start_w->type;
          level_up_selected = 0;
        }
      }
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
