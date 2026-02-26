#include "main_menu.h"

#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>

#include "Archimedes.h"
#include "enemy.h"
#include "stats.h"
#include "progress.h"
#include "upgrades.h"
#include "menu.h"
#include "upgrades_menu.h"

// ============================================================================
// Externs from main.c
// ============================================================================

extern int current_scene;
extern void game_reset(void);
extern int settings_sel;
extern int settings_confirm;
extern int settings_dragging;
extern int settings_return_scene;
// Scene enum values (must match main.c)
#define MM_SCENE_MAIN_MENU  0
#define MM_SCENE_ENEMY_TYPES 1
#define MM_SCENE_WEAPONS     2
#define MM_SCENE_UPGRADES    3
#define MM_SCENE_SETTINGS    6
#define MM_SCENE_ACHIEVEMENTS 7
#define MM_SCENE_STATS_DEEP_DIVE 8
#define MM_SCENE_CHALLENGES 9

// ============================================================================
// Menu sounds (shared by all menus)
// ============================================================================

aSoundEffect_t menu_move_sound;
int             menu_move_loaded = 0;
aSoundEffect_t menu_click_sound;
int             menu_click_loaded = 0;

// ============================================================================
// Main menu state
// ============================================================================

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

// Icon buttons below the menu panel
#define MAINMENU_ICON_0           6
#define MAINMENU_ICON_1           7
#define MAINMENU_ICON_ACHIEVE     8
#define MAINMENU_TOTAL_COUNT       9

#define MAINMENU_ICON_SIZE  40
#define MAINMENU_ICON_GAP   12

static int mainmenu_sel = 0;

// ============================================================================
// Initialization
// ============================================================================

void main_menu_init(void)
{
  if (a_AudioLoadSound("resources/soundEffects/menu_move.wav", &menu_move_sound) == 0)
    menu_move_loaded = 1;
  if (a_AudioLoadSound("resources/soundEffects/menu_click.wav", &menu_click_sound) == 0)
    menu_click_loaded = 1;
}

// ============================================================================
// Layout
// ============================================================================

void mainmenu_begin(void)
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

// ============================================================================
// Controls panel (shared with pause menu)
// ============================================================================

void draw_hotkeys( int cx, int top_y )
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

// ============================================================================
// Logic
// ============================================================================

// Helper: get icon button rect
static void mainmenu_icon_rect( int ib, int* ox, int* oy, int* ow, int* oh )
{
  if ( !mainmenu_flex ) return;
  int total_w = MAINMENU_ICON_SIZE * 3 + MAINMENU_ICON_GAP * 2;
  int base_x = (SCREEN_WIDTH - total_w) / 2;
  int base_y = mainmenu_flex->y + mainmenu_flex->h + 10;
  *ox = base_x + ib * (MAINMENU_ICON_SIZE + MAINMENU_ICON_GAP);
  *oy = base_y;
  *ow = MAINMENU_ICON_SIZE;
  *oh = MAINMENU_ICON_SIZE;
}

static void mainmenu_play_move(void)
{
  if ( menu_move_loaded ) {
    aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_AUTO, .volume = 40, .loops = 0, .fade_ms = 0, .interrupt = 0 };
    a_AudioPlaySound( &menu_move_sound, &opts );
  }
}

static void mainmenu_play_click(void)
{
  if ( menu_click_loaded ) {
    aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_AUTO, .volume = 120, .loops = 0, .fade_ms = 0, .interrupt = 0 };
    a_AudioPlaySound( &menu_click_sound, &opts );
  }
}

void scene_main_menu_logic( float dt )
{
  (void)dt;

  if ( !mainmenu_flex )
  {
    mainmenu_begin();
  }

  int in_icons = mainmenu_sel >= MAINMENU_ICON_0;

  // --- Keyboard navigation ---
  int up   = app.keyboard[SDL_SCANCODE_UP]   == 1 || app.keyboard[SDL_SCANCODE_W] == 1;
  int down = app.keyboard[SDL_SCANCODE_DOWN] == 1 || app.keyboard[SDL_SCANCODE_S] == 1;
  int left = app.keyboard[SDL_SCANCODE_LEFT] == 1 || app.keyboard[SDL_SCANCODE_A] == 1;
  int right= app.keyboard[SDL_SCANCODE_RIGHT]== 1 || app.keyboard[SDL_SCANCODE_D] == 1;
  if (up)   { app.keyboard[SDL_SCANCODE_UP]=0;   app.keyboard[SDL_SCANCODE_W]=0; }
  if (down) { app.keyboard[SDL_SCANCODE_DOWN]=0; app.keyboard[SDL_SCANCODE_S]=0; }
  if (left) { app.keyboard[SDL_SCANCODE_LEFT]=0; app.keyboard[SDL_SCANCODE_A]=0; }
  if (right){ app.keyboard[SDL_SCANCODE_RIGHT]=0;app.keyboard[SDL_SCANCODE_D]=0; }

  if ( in_icons )
  {
    // Icon row: left/right navigate between icons, up goes to QUIT
    if ( left ) {
      int icon_idx = mainmenu_sel - MAINMENU_ICON_0;
      icon_idx = (icon_idx - 1 + 3) % 3;
      mainmenu_sel = MAINMENU_ICON_0 + icon_idx;
      mainmenu_play_move();
    } else if ( right ) {
      int icon_idx = mainmenu_sel - MAINMENU_ICON_0;
      icon_idx = (icon_idx + 1) % 3;
      mainmenu_sel = MAINMENU_ICON_0 + icon_idx;
      mainmenu_play_move();
    } else if ( up ) {
      mainmenu_sel = MAINMENU_BTN_IDX_QUIT;
      mainmenu_play_move();
    } else if ( down ) {
      mainmenu_sel = MAINMENU_BTN_IDX_PLAY;
      mainmenu_play_move();
    }
  }
  else
  {
    // Column: up/down navigate, down from QUIT goes to middle icon
    if ( up ) {
      if ( mainmenu_sel == 0 )
        mainmenu_sel = MAINMENU_ICON_1; // wrap to middle icon
      else
        mainmenu_sel--;
      mainmenu_play_move();
    } else if ( down ) {
      if ( mainmenu_sel == MAINMENU_BTN_IDX_QUIT )
        mainmenu_sel = MAINMENU_ICON_1; // go to middle icon
      else
        mainmenu_sel++;
      mainmenu_play_move();
    }
  }

  // --- Mouse hover ---
  if ( menu_mouse_moved() && mainmenu_flex )
  {
    int mx = app.mouse.x, my = app.mouse.y;
    // Check flex buttons
    for ( int i = 0; i < MAINMENU_BTN_COUNT; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( mainmenu_flex, i );
      if ( item && menu_point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h ) )
      {
        if ( mainmenu_sel != i ) { mainmenu_sel = i; mainmenu_play_move(); }
        break;
      }
    }
    // Check icon buttons
    for ( int ib = 0; ib < 3; ib++ )
    {
      int bx, by, bw, bh;
      mainmenu_icon_rect( ib, &bx, &by, &bw, &bh );
      if ( menu_point_in_rect( mx, my, bx, by, bw, bh ) )
      {
        int idx = MAINMENU_ICON_0 + ib;
        if ( mainmenu_sel != idx ) { mainmenu_sel = idx; mainmenu_play_move(); }
        break;
      }
    }
  }

  // --- Confirm (Enter/Space or mouse click) ---
  int confirm = 0;
  if ( app.keyboard[SDL_SCANCODE_RETURN] == 1 || app.keyboard[SDL_SCANCODE_SPACE] == 1 )
  {
    app.keyboard[SDL_SCANCODE_RETURN] = 0;
    app.keyboard[SDL_SCANCODE_SPACE]  = 0;
    confirm = 1;
  }
  if ( app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT && mainmenu_flex )
  {
    int mx = app.mouse.x, my = app.mouse.y;
    for ( int i = 0; i < MAINMENU_BTN_COUNT; i++ )
    {
      const FlexItem_t* item = a_FlexGetItem( mainmenu_flex, i );
      if ( item && menu_point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h ) )
      {
        mainmenu_sel = i;
        confirm = 1;
        break;
      }
    }
    for ( int ib = 0; ib < 3; ib++ )
    {
      int bx, by, bw, bh;
      mainmenu_icon_rect( ib, &bx, &by, &bw, &bh );
      if ( menu_point_in_rect( mx, my, bx, by, bw, bh ) )
      {
        mainmenu_sel = MAINMENU_ICON_0 + ib;
        confirm = 1;
        break;
      }
    }
    app.mouse.pressed = 0;
  }

  if ( confirm )
  {
    mainmenu_play_click();

    if ( mainmenu_sel == MAINMENU_BTN_IDX_PLAY )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      game_reset();
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_ENEMIES )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = MM_SCENE_ENEMY_TYPES;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_WEAPONS )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = MM_SCENE_WEAPONS;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_UPGRADES )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      upgrades_menu_enter();
      current_scene = MM_SCENE_UPGRADES;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_SETTINGS )
    {
      settings_return_scene = MM_SCENE_MAIN_MENU;
      settings_sel = 0;
      settings_confirm = 0;
      settings_dragging = -1;
      current_scene = MM_SCENE_SETTINGS;
    }
    else if ( mainmenu_sel == MAINMENU_BTN_IDX_QUIT )
    {
      app.running = 0;
    }
    else if ( mainmenu_sel == MAINMENU_ICON_ACHIEVE )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = MM_SCENE_ACHIEVEMENTS;
    }
    else if ( mainmenu_sel == MAINMENU_ICON_0 )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = MM_SCENE_CHALLENGES;
    }
    else if ( mainmenu_sel == MAINMENU_ICON_1 )
    {
      if ( mainmenu_flex ) a_FlexBoxDestroy( &mainmenu_flex );
      current_scene = MM_SCENE_STATS_DEEP_DIVE;
    }
  }
}

// ============================================================================
// Draw
// ============================================================================

void scene_main_menu_draw( float dt )
{
  (void)dt;

  if ( !mainmenu_flex ) return;

  float anim_t = (float)SDL_GetTicks() / 1000.0f;

  // --- Background: subtle vertical gradient (lighter center, darker top/bottom) ---
  {
    int steps = 24;
    float step_h = (float)SCREEN_HEIGHT / (float)steps;
    for ( int i = 0; i < steps; i++ )
    {
      float t = (float)i / (float)( steps - 1 );           // 0 at top, 1 at bottom
      float center_dist = fabsf( t - 0.45f ) * 2.0f;       // 0 at center, ~1 at edges
      if ( center_dist > 1.0f ) center_dist = 1.0f;
      // Lighten the center band slightly, darken the edges
      int r = 20 + (int)( 12.0f * ( 1.0f - center_dist ) );
      int g = 20 + (int)( 10.0f * ( 1.0f - center_dist ) );
      int b = 60 + (int)( 18.0f * ( 1.0f - center_dist ) );
      a_DrawFilledRect(
        (aRectf_t){ 0, (float)i * step_h, (float)SCREEN_WIDTH, step_h + 1.0f },
        (aColor_t){ (Uint8)r, (Uint8)g, (Uint8)b, 255 }
      );
    }
  }

  // --- Vignette: darken edges and corners ---
  {
    // Top edge
    for ( int i = 0; i < 6; i++ )
    {
      int a = 40 - i * 7;
      if ( a < 0 ) a = 0;
      float y = (float)i * 20.0f;
      a_DrawFilledRect(
        (aRectf_t){ 0, y, (float)SCREEN_WIDTH, 20.0f },
        (aColor_t){ 0, 0, 10, (Uint8)a }
      );
    }
    // Bottom edge
    for ( int i = 0; i < 6; i++ )
    {
      int a = 40 - i * 7;
      if ( a < 0 ) a = 0;
      float y = (float)SCREEN_HEIGHT - (float)( i + 1 ) * 20.0f;
      a_DrawFilledRect(
        (aRectf_t){ 0, y, (float)SCREEN_WIDTH, 20.0f },
        (aColor_t){ 0, 0, 10, (Uint8)a }
      );
    }
    // Left edge
    for ( int i = 0; i < 6; i++ )
    {
      int a = 30 - i * 5;
      if ( a < 0 ) a = 0;
      float x = (float)i * 24.0f;
      a_DrawFilledRect(
        (aRectf_t){ x, 0, 24.0f, (float)SCREEN_HEIGHT },
        (aColor_t){ 0, 0, 10, (Uint8)a }
      );
    }
    // Right edge
    for ( int i = 0; i < 6; i++ )
    {
      int a = 30 - i * 5;
      if ( a < 0 ) a = 0;
      float x = (float)SCREEN_WIDTH - (float)( i + 1 ) * 24.0f;
      a_DrawFilledRect(
        (aRectf_t){ x, 0, 24.0f, (float)SCREEN_HEIGHT },
        (aColor_t){ 0, 0, 10, (Uint8)a }
      );
    }
  }

  // --- Background: floating enemy silhouettes ---
  {
    // 7 enemy types, 6 particles each = 42 total
    #define BG_PER_TYPE 6
    #define BG_TOTAL    (7 * BG_PER_TYPE)

    for ( int i = 0; i < BG_TOTAL; i++ )
    {
      int etype = i / BG_PER_TYPE; // 0-6: grunt,dasher,brute,shaman,snake,beholder,mimic
      float seed = (float)( i + 1 ) * 137.508f;
      float sx = 4.0f + fmodf( seed * 3.7f, 8.0f );
      float sy = 1.0f + fmodf( seed * 5.3f, 3.0f );
      float ddx = ( i % 2 == 0 ) ? 1.0f : -1.0f;
      float px = fmodf( fmodf( seed * 7.3f, (float)SCREEN_WIDTH )
                   + anim_t * sx * ddx + (float)SCREEN_WIDTH * 4.0f,
                   (float)SCREEN_WIDTH );
      float py = fmodf( fmodf( seed * 13.7f, (float)SCREEN_HEIGHT )
                   + anim_t * sy + (float)SCREEN_HEIGHT * 4.0f,
                   (float)SCREEN_HEIGHT );
      float pulse = sinf( anim_t * 0.7f + seed * 0.1f );
      int alpha = 40 + (int)( pulse * 25.0f );
      if ( alpha < 20 ) alpha = 20;
      if ( alpha > 75 ) alpha = 75;

      int ix = (int)px, iy = (int)py;
      Uint8 a = (Uint8)alpha;

      switch ( etype )
      {
        case 0: // Grunt — small red square
        {
          aColor_t c = { 200, 80, 80, a };
          a_DrawFilledRect( (aRectf_t){ px, py, 6.0f, 6.0f }, c );
          break;
        }
        case 1: // Dasher — small triangle pointing right/left
        {
          aColor_t c = { 200, 120, 60, a };
          int tip = ( i % 2 == 0 ) ? 7 : -7;
          a_DrawFilledTriangle( ix + tip, iy + 3,
                                ix - tip / 2, iy,
                                ix - tip / 2, iy + 6, c );
          break;
        }
        case 2: // Brute — square with two horn triangles
        {
          aColor_t c = { 180, 60, 60, a };
          a_DrawFilledRect( (aRectf_t){ px, py + 2, 8.0f, 8.0f }, c );
          aColor_t h = { 180, 60, 60, (Uint8)( alpha * 3 / 4 ) };
          a_DrawFilledTriangle( ix + 1, iy + 2, ix, iy - 2, ix + 3, iy + 2, h );
          a_DrawFilledTriangle( ix + 5, iy + 2, ix + 8, iy - 2, ix + 7, iy + 2, h );
          break;
        }
        case 3: // Shaman — square with cross/staff
        {
          aColor_t c = { 100, 60, 160, a };
          a_DrawFilledRect( (aRectf_t){ px, py + 2, 6.0f, 6.0f }, c );
          aColor_t s = { 140, 100, 200, a };
          a_DrawFilledRect( (aRectf_t){ px + 2, py - 2, 2.0f, 4.0f }, s );
          a_DrawFilledRect( (aRectf_t){ px, py - 1, 6.0f, 2.0f }, s );
          break;
        }
        case 4: // Snake — chain of 4 small rects
        {
          float ang = anim_t * 1.5f + seed * 0.05f;
          for ( int seg = 0; seg < 4; seg++ )
          {
            float wave = sinf( ang + (float)seg * 0.8f ) * 2.0f;
            float segx = px + (float)seg * 5.0f;
            float segy = py + wave;
            Uint8 sg = (Uint8)( 160 + seg * 25 );
            aColor_t c = { sg, (Uint8)( sg / 2 ), 0, a };
            float sz = ( seg == 0 ) ? 4.0f : 3.0f;
            a_DrawFilledRect( (aRectf_t){ segx, segy, sz, sz }, c );
          }
          break;
        }
        case 5: // Beholder — small circle with tendrils
        {
          aColor_t c = { 180, 180, 220, a };
          a_DrawFilledCircle( ix + 4, iy + 4, 4, c );
          aColor_t t = { 150, 150, 200, (Uint8)( alpha * 2 / 3 ) };
          for ( int tn = 0; tn < 3; tn++ )
          {
            float ta = (float)tn * 2.09f + anim_t * 0.8f + seed * 0.02f;
            int tx = ix + 4 + (int)( cosf( ta ) * 7.0f );
            int ty = iy + 4 + (int)( sinf( ta ) * 7.0f );
            a_DrawFilledRect( (aRectf_t){ (float)tx, (float)ty, 2.0f, 2.0f }, t );
          }
          // Eye dot
          aColor_t eye = { 255, 80, 80, a };
          a_DrawFilledRect( (aRectf_t){ px + 3, py + 3, 2.0f, 2.0f }, eye );
          break;
        }
        case 6: // Mimic — small chest (base + lid)
        {
          aColor_t base = { 140, 100, 50, a };
          aColor_t lid  = { 160, 120, 60, a };
          aColor_t clasp = { 200, 170, 50, a };
          a_DrawFilledRect( (aRectf_t){ px, py + 4, 8.0f, 4.0f }, base );
          float bob = sinf( anim_t * 2.0f + seed * 0.1f ) * 1.0f;
          a_DrawFilledRect( (aRectf_t){ px, py + bob, 8.0f, 3.0f }, lid );
          a_DrawFilledRect( (aRectf_t){ px + 3, py + 3 + bob, 2.0f, 2.0f }, clasp );
          break;
        }
      }
    }
  }

  // --- Title "ARCHIMEDES" with logo decorations ---
  {
    int title_cx = SCREEN_WIDTH / 2;
    int title_y  = mainmenu_flex->y - 150;

    // Soft glow behind title
    float glow_pulse = 0.4f + 0.35f * sinf( anim_t * 1.2f );
    int glow_alpha = (int)( glow_pulse * 60.0f );
    if ( glow_alpha < 8 ) glow_alpha = 8;
    a_DrawFilledRect(
      (aRectf_t){ (float)( title_cx - 170 ), (float)( title_y + 14 ),
                  340.0f, 36.0f },
      (aColor_t){ 180, 150, 60, (Uint8)glow_alpha }
    );

    // Drop shadow
    aTextStyle_t shadow_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = { 20, 15, 40, 100 },
      .align = TEXT_ALIGN_CENTER,
      .scale = 1.6f
    };
    a_DrawText( "ARCHIMEDES", title_cx + 2, title_y + 6, shadow_style );

    // Main title — warm gold with breathing
    float bp = 0.75f + 0.25f * sinf( anim_t * 1.5f );
    aTextStyle_t title_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = { (Uint8)( 255.0f * bp ), (Uint8)( 215.0f * bp ),
              (Uint8)( 120.0f * bp ), 255 },
      .align = TEXT_ALIGN_CENTER,
      .scale = 1.6f
    };
    a_DrawText( "ARCHIMEDES", title_cx, title_y, title_style );

    // Decorative horizontal lines flanking title
    int line_y  = title_y + 30;
    int gap     = 46;
    int extent  = 180;
    aColor_t line_col = { 180, 160, 100, 100 };
    a_DrawLine( title_cx - gap, line_y, title_cx - extent, line_y, line_col );
    a_DrawLine( title_cx + gap, line_y, title_cx + extent, line_y, line_col );

    // Diamond endpoints
    int dl = title_cx - extent;
    int dr = title_cx + extent;
    aColor_t diamond_col = { 200, 180, 100, 140 };
    a_DrawFilledTriangle( dl, line_y, dl - 5, line_y - 4,
                          dl - 5, line_y + 4, diamond_col );
    a_DrawFilledTriangle( dr, line_y, dr + 5, line_y - 4,
                          dr + 5, line_y + 4, diamond_col );

    // Small inner dots at the gap edges
    aColor_t dot_col = { 200, 180, 100, 80 };
    a_DrawFilledRect( (aRectf_t){ (float)( title_cx - gap - 1 ),
                      (float)( line_y - 1 ), 2.0f, 2.0f }, dot_col );
    a_DrawFilledRect( (aRectf_t){ (float)( title_cx + gap - 1 ),
                      (float)( line_y - 1 ), 2.0f, 2.0f }, dot_col );
  }

  // Subtitle "survivor"
  {
    float sub_pulse = 0.85f + 0.15f * sinf( anim_t * 0.8f + 1.0f );
    aTextStyle_t subtitle_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = { (Uint8)( 160.0f * sub_pulse ), (Uint8)( 170.0f * sub_pulse ),
              (Uint8)( 220.0f * sub_pulse ), 255 },
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.8f
    };
    a_DrawText( "survivor", SCREEN_WIDTH / 2,
                mainmenu_flex->y - 90, subtitle_style );
  }

  // Version (pushed down below icon buttons)
  aTextStyle_t version_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {120, 120, 140, 180},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.4f
  };
  a_DrawText( "early-access-v-0.5", SCREEN_WIDTH / 2,
              mainmenu_flex->y + mainmenu_flex->h + 58, version_style );

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

    int hovered = menu_point_in_rect( mx, my, item->calc_x, item->calc_y, item->w, item->h );
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
        (aColor_t){255, 220, 50, 255}
      );
      aTextStyle_t pts_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {255, 220, 50, 255},
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

  // --- Icon buttons below menu panel (? / ? / Achievements) ---
  {
    int icon_size = MAINMENU_ICON_SIZE;
    int icon_gap = MAINMENU_ICON_GAP;
    int total_w = icon_size * 3 + icon_gap * 2;
    int icon_base_x = (SCREEN_WIDTH - total_w) / 2;
    int icon_y = mainmenu_flex->y + mainmenu_flex->h + 10;
    int mx = app.mouse.x;
    int my = app.mouse.y;

    for ( int ib = 0; ib < 3; ib++ )
    {
      int bx = icon_base_x + ib * (icon_size + icon_gap);
      int selected = ( mainmenu_sel == MAINMENU_ICON_0 + ib );
      int hovered = selected || menu_point_in_rect( mx, my, bx, icon_y, icon_size, icon_size );

      // Button background
      aColor_t bg = hovered ? (aColor_t){70, 70, 110, 255} : (aColor_t){35, 35, 55, 220};
      aColor_t border_col = hovered ? (aColor_t){255, 220, 50, 255} : (aColor_t){120, 120, 150, 180};
      a_DrawFilledRect(
        (aRectf_t){(float)bx, (float)icon_y, (float)icon_size, (float)icon_size}, bg
      );
      a_DrawRect(
        (aRectf_t){(float)bx, (float)icon_y, (float)icon_size, (float)icon_size}, border_col
      );

      float cx = (float)bx + (float)icon_size / 2.0f;
      float cy = (float)icon_y + (float)icon_size / 2.0f;

      if ( ib == 0 )
      {
        // Skull icon (challenges)
        float bright = hovered ? 1.0f : 0.6f;
        Uint8 bone = (Uint8)(220.0f * bright);
        Uint8 bone_d = (Uint8)(180.0f * bright);
        aColor_t sk = { bone, bone, bone, 255 };
        aColor_t sk_d = { bone_d, bone_d, bone_d, 255 };
        aColor_t eye_c = { (Uint8)(40*bright), (Uint8)(20*bright), (Uint8)(30*bright), 255 };

        float sx = cx, sy = cy - 2.0f;
        // Cranium (rounded-ish: wide middle, narrower top)
        a_DrawFilledRect( (aRectf_t){ sx - 7, sy - 9, 14, 3 }, sk_d );  // top
        a_DrawFilledRect( (aRectf_t){ sx - 9, sy - 6, 18, 8 }, sk );    // mid
        a_DrawFilledRect( (aRectf_t){ sx - 7, sy + 2, 14, 3 }, sk_d );  // lower
        // Eye sockets
        a_DrawFilledRect( (aRectf_t){ sx - 6, sy - 4, 4, 4 }, eye_c );
        a_DrawFilledRect( (aRectf_t){ sx + 2, sy - 4, 4, 4 }, eye_c );
        // Nose
        a_DrawFilledRect( (aRectf_t){ sx - 1, sy + 1, 2, 2 }, eye_c );
        // Jaw / teeth
        a_DrawFilledRect( (aRectf_t){ sx - 6, sy + 5, 12, 3 }, sk_d );
        // Tooth gaps
        a_DrawFilledRect( (aRectf_t){ sx - 3, sy + 5, 1, 3 }, eye_c );
        a_DrawFilledRect( (aRectf_t){ sx,     sy + 5, 1, 3 }, eye_c );
        a_DrawFilledRect( (aRectf_t){ sx + 3, sy + 5, 1, 3 }, eye_c );
        // Crossbones behind
        a_DrawFilledRect( (aRectf_t){ sx - 12, sy + 10, 24, 2 }, sk_d );
        a_DrawFilledRect( (aRectf_t){ sx - 12, sy + 13, 24, 2 }, sk_d );
      }
      else if ( ib == 1 )
      {
        // Stats bar graph icon — 3 colored bars
        float bright = hovered ? 1.0f : 0.6f;
        float bar_base_y = cy + 10.0f;
        float bar_w = 6.0f;
        float gap = 3.0f;
        float left_x = cx - (bar_w * 3.0f + gap * 2.0f) / 2.0f;

        // Short bar (blue)
        float h1 = 10.0f;
        a_DrawFilledRect(
          (aRectf_t){ left_x, bar_base_y - h1, bar_w, h1 },
          (aColor_t){ (Uint8)(80*bright), (Uint8)(160*bright), (Uint8)(255*bright), 255 }
        );
        // Tall bar (green)
        float h2 = 20.0f;
        a_DrawFilledRect(
          (aRectf_t){ left_x + bar_w + gap, bar_base_y - h2, bar_w, h2 },
          (aColor_t){ (Uint8)(80*bright), (Uint8)(220*bright), (Uint8)(100*bright), 255 }
        );
        // Medium bar (gold)
        float h3 = 15.0f;
        a_DrawFilledRect(
          (aRectf_t){ left_x + (bar_w + gap) * 2.0f, bar_base_y - h3, bar_w, h3 },
          (aColor_t){ (Uint8)(255*bright), (Uint8)(200*bright), (Uint8)(50*bright), 255 }
        );
        // Baseline
        a_DrawFilledRect(
          (aRectf_t){ left_x - 1.0f, bar_base_y, bar_w * 3.0f + gap * 2.0f + 2.0f, 1.0f },
          (aColor_t){ (Uint8)(180*bright), (Uint8)(180*bright), (Uint8)(200*bright), 200 }
        );
      }
      else
      {
        // Trophy icon (achievements)
        float tcx = cx;
        float tcy = cy - 2.0f;
        float bright = hovered ? 1.0f : 0.7f;
        Uint8 gr = (Uint8)(255.0f * bright);
        Uint8 gg = (Uint8)(200.0f * bright);
        Uint8 gb = (Uint8)(50.0f * bright);
        aColor_t gold = {gr, gg, gb, 255};
        aColor_t dark_gold = {(Uint8)(180.0f * bright), (Uint8)(140.0f * bright), (Uint8)(30.0f * bright), 255};
        aColor_t shine = {255, 255, (Uint8)(180.0f * bright), (Uint8)(hovered ? 200 : 140)};

        // Cup body (wide at top, narrower at bottom)
        a_DrawFilledRect( (aRectf_t){tcx - 8, tcy - 8, 16, 3}, gold );
        a_DrawFilledRect( (aRectf_t){tcx - 7, tcy - 5, 14, 8}, gold );
        a_DrawFilledRect( (aRectf_t){tcx - 5, tcy + 3, 10, 3}, gold );
        a_DrawFilledRect( (aRectf_t){tcx - 3, tcy + 6, 6, 2}, gold );

        // Left handle
        a_DrawFilledRect( (aRectf_t){tcx - 11, tcy - 5, 3, 2}, dark_gold );
        a_DrawFilledRect( (aRectf_t){tcx - 12, tcy - 3, 2, 5}, dark_gold );
        a_DrawFilledRect( (aRectf_t){tcx - 11, tcy + 2, 3, 2}, dark_gold );

        // Right handle
        a_DrawFilledRect( (aRectf_t){tcx + 8, tcy - 5, 3, 2}, dark_gold );
        a_DrawFilledRect( (aRectf_t){tcx + 10, tcy - 3, 2, 5}, dark_gold );
        a_DrawFilledRect( (aRectf_t){tcx + 8, tcy + 2, 3, 2}, dark_gold );

        // Stem
        a_DrawFilledRect( (aRectf_t){tcx - 1.5f, tcy + 8, 3, 4}, dark_gold );

        // Base
        a_DrawFilledRect( (aRectf_t){tcx - 6, tcy + 12, 12, 3}, gold );

        // Star/shine on cup face
        a_DrawFilledRect( (aRectf_t){tcx - 0.5f, tcy - 4, 1, 5}, shine );
        a_DrawFilledRect( (aRectf_t){tcx - 2, tcy - 1.5f, 4, 1}, shine );
      }
    }
  }
}
