#include "weapons_menu.h"
#include "Archimedes.h"
#include "weapons.h"
#include "progress.h"
#include "menu.h"
#include "main_menu.h"
#include "stats.h"
#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>

// ============================================================================
// Externs from main.c
// ============================================================================

extern int current_scene;
#define WM_SCENE_MAIN_MENU 0

// ============================================================================
// Sound helpers (use shared menu sounds from main_menu.h)
// ============================================================================

static void play_menu_move(void)
{
  if (menu_move_loaded)
  {
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
  if (menu_click_loaded)
  {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 120,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(&menu_click_sound, &opts);
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
static int wtype_modal_page = 0;
#define WTYPE_MODAL_PER_PAGE 3

// Page 1: 4 cards (Wand, Spin, Chain, Orbit)
// Page 2: 4 cards (Bomb, Turret, Trail, Scythe)
static const WeaponId_t wtype_page1[] = { WID_WAND, WID_SPIN, WID_CHAIN, WID_ORBIT };
static const WeaponId_t wtype_page2[] = { WID_BOMB, WID_TURRET, WID_TRAIL, WID_SCYTHE };
#define WTYPE_PAGE1_COUNT 4
#define WTYPE_PAGE2_COUNT 4

// Track mapping: up to 6 upgrades per weapon
#define WTYPE_MAX_TRACKS 6
static const int wtype_track_count[WID_COUNT] = { 5, 5, 5, 6, 5, 6, 5, 5 };
static const WeaponProgressId_t wtype_tracks[WID_COUNT][WTYPE_MAX_TRACKS] = {
  { WPROG_WAND_RARITY,   WPROG_WAND_COOLDOWN,   WPROG_WAND_REACH,   WPROG_WAND_FREE_UPG,   WPROG_WAND_EXTRA_CHOICE },
  { WPROG_SPIN_RARITY,   WPROG_SPIN_COOLDOWN,   WPROG_SPIN_REACH,   WPROG_SPIN_FREE_UPG,   WPROG_SPIN_EXTRA_CHOICE },
  { WPROG_CHAIN_RARITY,  WPROG_CHAIN_COOLDOWN,  WPROG_CHAIN_REACH,  WPROG_CHAIN_FREE_UPG,  WPROG_CHAIN_EXTRA_CHOICE },
  { WPROG_ORBIT_RARITY,  WPROG_ORBIT_COOLDOWN,  WPROG_ORBIT_REACH,  WPROG_ORBIT_FREE_UPG,  WPROG_ORBIT_EXTRA_CHOICE,  WPROG_ORBIT_SHATTER },
  { WPROG_BOMB_RARITY,   WPROG_BOMB_COOLDOWN,   WPROG_BOMB_REACH,   WPROG_BOMB_FREE_UPG,   WPROG_BOMB_EXTRA_CHOICE },
  { WPROG_TURRET_RARITY, WPROG_TURRET_COOLDOWN, WPROG_TURRET_REACH, WPROG_TURRET_FREE_UPG, WPROG_TURRET_EXTRA_CHOICE, WPROG_TURRET_DURATION },
  { WPROG_TRAIL_RARITY,  WPROG_TRAIL_COOLDOWN,  WPROG_TRAIL_REACH,  WPROG_TRAIL_FREE_UPG,  WPROG_TRAIL_EXTRA_CHOICE },
  { WPROG_SCYTHE_RARITY, WPROG_SCYTHE_COOLDOWN, WPROG_SCYTHE_REACH, WPROG_SCYTHE_FREE_UPG, WPROG_SCYTHE_EXTRA_CHOICE },
};

// Sorted track indices per weapon (by cost, maxed last)
static int wtype_tracks_sorted[WID_COUNT][WTYPE_MAX_TRACKS];
static int wtype_tracks_sort_dirty = 1;

static void wtype_rebuild_track_sort(void)
{
  for ( int w = 0; w < WID_COUNT; w++ )
  {
    int count = wtype_track_count[w];
    for ( int i = 0; i < count; i++ ) wtype_tracks_sorted[w][i] = i;

    // Insertion sort by next cost (maxed = 999999 -> last)
    for ( int i = 1; i < count; i++ )
    {
      int key = wtype_tracks_sorted[w][i];
      int key_cost = wprog_get_next_cost( wtype_tracks[w][key] );
      if ( key_cost < 0 ) key_cost = 999999;

      int j = i - 1;
      while ( j >= 0 )
      {
        int jc = wprog_get_next_cost( wtype_tracks[w][ wtype_tracks_sorted[w][j] ] );
        if ( jc < 0 ) jc = 999999;
        if ( jc <= key_cost ) break;
        wtype_tracks_sorted[w][j + 1] = wtype_tracks_sorted[w][j];
        j--;
      }
      wtype_tracks_sorted[w][j + 1] = key;
    }
  }
  wtype_tracks_sort_dirty = 0;
}

static const char* wtype_descs[WID_COUNT] = {
  "Fires bullets in the aim direction",
  "Damages nearby enemies in a circle",
  "Lightning arcs between nearby enemies",
  "Shields orbit the player",
  "Throws bombs that explode on impact",
  "Stationary turret that auto-fires",
  "Leaves a damaging trail behind you",
  "Sweeps an arc in your facing direction",
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
  [WID_SCYTHE] = {180, 255, 180, 255},
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
  [WID_SCYTHE] = "SCTH",
};

// Draw weapon icon -- colored square with label text (matches in-game drops)
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

#define point_in_rect  menu_point_in_rect
#define mouse_moved_this_frame  menu_mouse_moved()

void scene_weapons_logic( float dt )
{
  (void)dt;

  if ( wtype_tracks_sort_dirty ) wtype_rebuild_track_sort();

  // Get current page data
  const WeaponId_t* page_items = (wtype_page == 0) ? wtype_page1 : wtype_page2;
  int page_count = (wtype_page == 0) ? WTYPE_PAGE1_COUNT : WTYPE_PAGE2_COUNT;

  // Modal logic
  if ( wtype_modal_open )
  {
    int wt_count = wtype_track_count[wtype_modal_wid];
    int total_pages = (wt_count + WTYPE_MODAL_PER_PAGE - 1) / WTYPE_MODAL_PER_PAGE;
    if ( wtype_modal_page >= total_pages ) wtype_modal_page = total_pages - 1;
    int page_start = wtype_modal_page * WTYPE_MODAL_PER_PAGE;
    int page_end = page_start + WTYPE_MODAL_PER_PAGE;
    if ( page_end > wt_count ) page_end = wt_count;
    int visible = page_end - page_start;

    int pw = 500, ph = 80 + visible * 80 + 50;
    int mpx = (SCREEN_WIDTH - pw) / 2;
    int mpy = (SCREEN_HEIGHT - ph) / 2;

    // Click on BACK button
    {
      int bw = 100, bh = 24;
      int bx = (SCREEN_WIDTH - bw) / 2;
      int by = mpy + ph - 32;
      if ( app.mouse.button == 1 && app.mouse.pressed &&
           point_in_rect( app.mouse.x, app.mouse.y, bx, by, bw, bh ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        wtype_modal_open = 0;
        return;
      }
    }

    // Page navigation (A/D or Left/Right + mouse click on arrows)
    if ( total_pages > 1 )
    {
      int mnav_y = mpy + 80 + visible * 80 + 2;
      int mnav_h = 20;
      int mleft_x = mpx + 10, mleft_w = 80;
      int mright_x = mpx + pw - 90, mright_w = 80;

      if ( app.keyboard[ SDL_SCANCODE_A ] == 1 || app.keyboard[ SDL_SCANCODE_LEFT ] == 1
           || ( app.mouse.button == 1 && app.mouse.pressed && wtype_modal_page > 0
                && point_in_rect( app.mouse.x, app.mouse.y, mleft_x, mnav_y, mleft_w, mnav_h ) ) )
      {
        int was_mouse = (app.mouse.button == 1 && app.mouse.pressed);
        app.keyboard[ SDL_SCANCODE_A ] = 0;
        app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
        if ( was_mouse ) app.mouse.button = 0;
        if ( wtype_modal_page > 0 )
        {
          wtype_modal_page--;
          wtype_modal_track_sel = 0;
          play_menu_move();
        }
      }
      if ( app.keyboard[ SDL_SCANCODE_D ] == 1 || app.keyboard[ SDL_SCANCODE_RIGHT ] == 1
           || ( app.mouse.button == 1 && app.mouse.pressed && wtype_modal_page < total_pages - 1
                && point_in_rect( app.mouse.x, app.mouse.y, mright_x, mnav_y, mright_w, mnav_h ) ) )
      {
        int was_mouse = (app.mouse.button == 1 && app.mouse.pressed);
        app.keyboard[ SDL_SCANCODE_D ] = 0;
        app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
        if ( was_mouse ) app.mouse.button = 0;
        if ( wtype_modal_page < total_pages - 1 )
        {
          wtype_modal_page++;
          wtype_modal_track_sel = 0;
          play_menu_move();
        }
      }
      // Recalculate after page change
      page_start = wtype_modal_page * WTYPE_MODAL_PER_PAGE;
      page_end = page_start + WTYPE_MODAL_PER_PAGE;
      if ( page_end > wt_count ) page_end = wt_count;
      visible = page_end - page_start;
    }

    if ( wtype_modal_track_sel >= visible ) wtype_modal_track_sel = visible - 1;

    static MenuRect_t wm_rects[WTYPE_MODAL_PER_PAGE];
    for ( int t = 0; t < visible; t++ )
      wm_rects[t] = (MenuRect_t){ mpx + 20, mpy + 80 + t * 80, pw - 40, 72 };

    static Menu_t wm;
    wm.selected = wtype_modal_track_sel;
    wm.count    = visible;
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
      int abs_sel = page_start + wtype_modal_track_sel;
      int sorted_idx = wtype_tracks_sorted[wtype_modal_wid][abs_sel];
      WeaponProgressId_t id = wtype_tracks[wtype_modal_wid][sorted_idx];
      if ( wprog_can_afford(id) )
      {
        wprog_purchase(id);
        wtype_tracks_sort_dirty = 1;
      }
    }
    return;
  }

  // ESC / Backspace / click Back button -> return to main menu
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
      current_scene = WM_SCENE_MAIN_MENU;
      return;
    }
  }

  // Left/Right/A/D -- navigate cards
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

  // Enter/Space -- open modal
  if ( app.keyboard[ SDL_SCANCODE_RETURN ] == 1 || app.keyboard[ SDL_SCANCODE_SPACE ] == 1 )
  {
    app.keyboard[ SDL_SCANCODE_RETURN ] = 0;
    app.keyboard[ SDL_SCANCODE_SPACE ] = 0;
    WeaponId_t wid = page_items[wtype_selected];
    wtype_modal_open = 1;
    wtype_modal_wid = wid;
    wtype_modal_track_sel = 0;
    wtype_modal_page = 0;
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

  // Mouse: click card navigation arrows (< >)
  if ( app.mouse.button == 1 && app.mouse.pressed )
  {
    int mx = app.mouse.x, my = app.mouse.y;
    int card_y = (SCREEN_HEIGHT - WTYPE_CARD_H) / 2;
    int total_w = page_count * WTYPE_CARD_W + (page_count - 1) * WTYPE_CARD_GAP;
    int start_x = (SCREEN_WIDTH - total_w) / 2;
    int nav_w = 40, nav_h = 40;
    int nav_y = card_y + WTYPE_CARD_H / 2 - nav_h / 2;
    int left_nx = start_x - 30 - nav_w;
    int right_nx = start_x + total_w + 30;

    int can_left = (wtype_selected > 0 || wtype_page > 0);
    int can_right = (wtype_selected < page_count - 1 || wtype_page < WTYPE_PAGE_COUNT - 1);

    if ( can_left && point_in_rect( mx, my, left_nx, nav_y, nav_w, nav_h ) )
    {
      app.mouse.button = 0;
      play_menu_move();
      if ( wtype_selected > 0 )
        wtype_selected--;
      else if ( wtype_page > 0 )
      {
        wtype_page--;
        int prev_count = (wtype_page == 0) ? WTYPE_PAGE1_COUNT : WTYPE_PAGE2_COUNT;
        wtype_selected = prev_count - 1;
      }
    }
    else if ( can_right && point_in_rect( mx, my, right_nx, nav_y, nav_w, nav_h ) )
    {
      app.mouse.button = 0;
      play_menu_move();
      if ( wtype_selected < page_count - 1 )
        wtype_selected++;
      else if ( wtype_page < WTYPE_PAGE_COUNT - 1 )
      {
        wtype_page++;
        wtype_selected = 0;
      }
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
          wtype_modal_page = 0;
          play_menu_click();
        }
        break;
      }
    }
  }
}

void scene_weapons_draw( float dt )
{
  (void)dt;

  if ( wtype_tracks_sort_dirty ) wtype_rebuild_track_sort();

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

    // Show upgrade tracks with current tier pips (sorted by cost)
    for ( int t = 0; t < wtype_track_count[wid]; t++ )
    {
      WeaponProgressId_t pid = wtype_tracks[wid][ wtype_tracks_sorted[wid][t] ];
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
    int hits  = progress_get_weapon_lifetime_hits(wid);
    int kills = progress_get_weapon_lifetime_kills(wid);
    int pts   = progress_get_weapon_available_points(wid);
    char hits_buf[64], kills_buf[64], pts_buf[64];
    snprintf( hits_buf, sizeof(hits_buf), "HITS: %d", hits );
    snprintf( kills_buf, sizeof(kills_buf), "KILLS: %d", kills );
    snprintf( pts_buf, sizeof(pts_buf), "POINTS: %d", pts );

    aTextStyle_t grey_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {140, 140, 160, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    aTextStyle_t pts_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 220, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.5f
    };
    int bottom_y = card_y + WTYPE_CARD_H - 78;
    a_DrawText( hits_buf, mid_x, bottom_y, grey_style );
    a_DrawText( kills_buf, mid_x, bottom_y + 18, grey_style );
    a_DrawText( pts_buf, mid_x, bottom_y + 36, pts_style );

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
      a_DrawText( "UPGRADE AVAILABLE", mid_x, bottom_y + 58 + (int)bob, upg_style );
    }
    else
    {
      aTextStyle_t select_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = selected ? WTYPE_ACCENT : (aColor_t){100, 110, 140, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.45f
      };
      a_DrawText( "[SELECT TO UPGRADE]", mid_x, bottom_y + 60, select_style );
    }
  }

  // Card navigation arrows (< >)
  {
    int nav_w = 40, nav_h = 40;
    int nav_y = card_y + WTYPE_CARD_H / 2 - nav_h / 2;
    int left_nx = start_x - 30 - nav_w;
    int right_nx = start_x + total_w + 30;
    int nmx = app.mouse.x, nmy = app.mouse.y;

    int can_left = (wtype_selected > 0 || wtype_page > 0);
    int can_right = (wtype_selected < page_count - 1 || wtype_page < WTYPE_PAGE_COUNT - 1);
    int hover_left = can_left && point_in_rect( nmx, nmy, left_nx, nav_y, nav_w, nav_h );
    int hover_right = can_right && point_in_rect( nmx, nmy, right_nx, nav_y, nav_w, nav_h );

    aColor_t lc = !can_left ? (aColor_t){50, 60, 80, 150}
                 : hover_left ? (aColor_t){160, 210, 255, 255}
                 : WTYPE_ACCENT;
    if ( hover_left )
      a_DrawFilledRect( (aRectf_t){(float)left_nx, (float)nav_y, (float)nav_w, (float)nav_h},
                        (aColor_t){100, 180, 255, 40} );
    aTextStyle_t ls = { .type = FONT_ENTER_COMMAND, .fg = lc, .align = TEXT_ALIGN_CENTER, .scale = 1.0f };
    a_DrawText( "<", left_nx + nav_w / 2, nav_y + nav_h / 2 - 8, ls );

    aColor_t rc = !can_right ? (aColor_t){50, 60, 80, 150}
                 : hover_right ? (aColor_t){160, 210, 255, 255}
                 : WTYPE_ACCENT;
    if ( hover_right )
      a_DrawFilledRect( (aRectf_t){(float)right_nx, (float)nav_y, (float)nav_w, (float)nav_h},
                        (aColor_t){100, 180, 255, 40} );
    aTextStyle_t rs = { .type = FONT_ENTER_COMMAND, .fg = rc, .align = TEXT_ALIGN_CENTER, .scale = 1.0f };
    a_DrawText( ">", right_nx + nav_w / 2, nav_y + nav_h / 2 - 8, rs );
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

    int wt_count = wtype_track_count[wtype_modal_wid];
    int total_pages = (wt_count + WTYPE_MODAL_PER_PAGE - 1) / WTYPE_MODAL_PER_PAGE;
    int page_start = wtype_modal_page * WTYPE_MODAL_PER_PAGE;
    int page_end = page_start + WTYPE_MODAL_PER_PAGE;
    if ( page_end > wt_count ) page_end = wt_count;
    int visible = page_end - page_start;

    int pw = 500, ph = 80 + visible * 80 + 50;
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

    // Track rows (current page only, sorted by cost)
    for ( int vi = 0; vi < visible; vi++ )
    {
      int t = page_start + vi;
      int sorted_idx = wtype_tracks_sorted[wtype_modal_wid][t];
      WeaponProgressId_t id = wtype_tracks[wtype_modal_wid][sorted_idx];
      int tier = wprog_get_tier(id);
      int max_tier = wprog_get_max_tier(id);
      int cost = wprog_get_next_cost(id);
      int can_buy = wprog_can_afford(id);
      int sel = (vi == wtype_modal_track_sel);

      int row_y = py + 80 + vi * 80;

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

    // Page navigation arrows (below rows, above back button)
    if ( total_pages > 1 )
    {
      int nav_y = py + 80 + visible * 80 + 2;
      int mnav_h = 20;
      int mleft_x = px + 10, mleft_w = 80;
      int mright_x = px + pw - 90, mright_w = 80;
      int amx = app.mouse.x, amy = app.mouse.y;

      if ( wtype_modal_page > 0 )
      {
        int lhover = point_in_rect( amx, amy, mleft_x, nav_y, mleft_w, mnav_h );
        if ( lhover )
          a_DrawFilledRect( (aRectf_t){(float)mleft_x, (float)nav_y, (float)mleft_w, (float)mnav_h},
                            (aColor_t){100, 180, 255, 40} );
        aTextStyle_t arr_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = lhover ? (aColor_t){220, 220, 255, 255} : (aColor_t){180, 180, 255, 255},
          .align = TEXT_ALIGN_LEFT,
          .scale = 0.45f
        };
        a_DrawText( "< [A]", px + 30, nav_y, arr_s );
      }
      if ( wtype_modal_page < total_pages - 1 )
      {
        int rhover = point_in_rect( amx, amy, mright_x, nav_y, mright_w, mnav_h );
        if ( rhover )
          a_DrawFilledRect( (aRectf_t){(float)mright_x, (float)nav_y, (float)mright_w, (float)mnav_h},
                            (aColor_t){100, 180, 255, 40} );
        aTextStyle_t arr_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = rhover ? (aColor_t){220, 220, 255, 255} : (aColor_t){180, 180, 255, 255},
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.45f
        };
        a_DrawText( "[D] >", px + pw - 30, nav_y, arr_s );
      }

      char pg_buf[32];
      snprintf( pg_buf, sizeof(pg_buf), "%d / %d", wtype_modal_page + 1, total_pages );
      aTextStyle_t pg_s = {
        .type = FONT_ENTER_COMMAND,
        .fg = {150, 150, 170, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.4f
      };
      a_DrawText( pg_buf, mcx, nav_y, pg_s );
    }

    // BACK [ESC] button
    {
      int bw = 100, bh = 24;
      int bx = (SCREEN_WIDTH - bw) / 2;
      int by = py + ph - 32;
      int bhover = point_in_rect( app.mouse.x, app.mouse.y, bx, by, bw, bh );
      if ( bhover )
      {
        a_DrawFilledRect(
          (aRectf_t){(float)bx, (float)by, (float)bw, (float)bh},
          (aColor_t){100, 180, 255, 40}
        );
      }
      a_DrawRect(
        (aRectf_t){(float)bx, (float)by, (float)bw, (float)bh},
        bhover ? (aColor_t){160, 210, 255, 255} : (aColor_t){80, 80, 110, 180}
      );
      aTextStyle_t back_s = {
        .type = FONT_ENTER_COMMAND,
        .fg = bhover ? (aColor_t){220, 240, 255, 255} : (aColor_t){140, 140, 160, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.45f
      };
      a_DrawText( "BACK [ESC]", mcx, by + 4, back_s );
    }

    // Info box to the right of the modal for the selected track
    {
      int abs_sel = page_start + wtype_modal_track_sel;
      int sel_sorted_idx = wtype_tracks_sorted[wtype_modal_wid][abs_sel];
      WeaponProgressId_t sel_prog = wtype_tracks[wtype_modal_wid][sel_sorted_idx];
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
