#include "enemy_types_menu.h"
#include "Archimedes.h"
#include "enemy.h"
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
#define EM_SCENE_MAIN_MENU 0

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
static int etype_modal_page = 0;
#define ETYPE_TRACK_COUNT 2
#define ETYPE_MODAL_PER_PAGE 3

// Track mapping: upgrades per enemy type (extensible)
static const ProgressUpgradeId_t etype_tracks[ENEMY_TYPE_COUNT][ETYPE_TRACK_COUNT] = {
  [ENEMY_TYPE_GRUNT]    = { PROG_GRUNT_DMG_RED,       PROG_GRUNT_EXPLOSION },
  [ENEMY_TYPE_DASHER]   = { PROG_DASHER_DMG_RED,      PROG_DASHER_STUN },
  [ENEMY_TYPE_BRUTE]    = { PROG_BRUTE_DMG_RED,       PROG_BRUTE_SPEED_DIM },
  [ENEMY_TYPE_SHAMAN]   = { PROG_SHAMAN_CORPSE_DECAY, PROG_SHAMAN_HEAL_RED },
  [ENEMY_TYPE_SNAKE]    = { PROG_SNAKE_DMG_RED,       PROG_SNAKE_SCALE_SHATTER },
  [ENEMY_TYPE_BEHOLDER] = { PROG_BEHOLDER_DMG_RED,    PROG_BEHOLDER_TELEGRAPH },
  [ENEMY_TYPE_MIMIC]    = { PROG_MIMIC_DMG_RED,         PROG_MIMIC_WEAPON_CD },
};

// Sorted track indices per enemy type (by cost, maxed last)
static int etype_tracks_sorted[ENEMY_TYPE_COUNT][ETYPE_TRACK_COUNT];
static int etype_tracks_sort_dirty = 1;

static void etype_rebuild_track_sort(void)
{
  for ( int e = 0; e < ENEMY_TYPE_COUNT; e++ )
  {
    for ( int i = 0; i < ETYPE_TRACK_COUNT; i++ ) etype_tracks_sorted[e][i] = i;

    for ( int i = 1; i < ETYPE_TRACK_COUNT; i++ )
    {
      int key = etype_tracks_sorted[e][i];
      int key_cost = progress_get_next_cost( etype_tracks[e][key] );
      if ( key_cost < 0 ) key_cost = 999999;

      int j = i - 1;
      while ( j >= 0 )
      {
        int jc = progress_get_next_cost( etype_tracks[e][ etype_tracks_sorted[e][j] ] );
        if ( jc < 0 ) jc = 999999;
        if ( jc <= key_cost ) break;
        etype_tracks_sorted[e][j + 1] = etype_tracks_sorted[e][j];
        j--;
      }
      etype_tracks_sorted[e][j + 1] = key;
    }
  }
  etype_tracks_sort_dirty = 0;
}

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

static const EnemyTypeCard_t mimic_card = {
  .name = "MIMIC",
  .desc = "Weapon thief",
  .behaviors = {
    "Steals one of your weapons on contact",
    "Uses your weapon's upgrades against you",
    "Must be killed to recover stolen weapon",
    "AI adapts to whichever weapon it steals"
  },
  .hp_line = "40 hits to kill"
};

#define point_in_rect  menu_point_in_rect
#define mouse_moved_this_frame  menu_mouse_moved()

static void etype_draw_shape( int type_index, int cx, int cy, int size, aColor_t c )
{
  if ( type_index == ENEMY_TYPE_DASHER )
  {
    // Dasher -- triangle pointing up
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
    // Shaman -- cross/plus shape
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
    // Snake -- head square + 3 trailing body segments in zigzag, compact
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
    // Beholder -- matches in-game: tendrils + body circle + dark pupil
    int body_r = size / 2;

    // Tendrils: 4 at diagonal directions (pointing downward like in-game default)
    for ( int t = 0; t < 4; t++ )
    {
      float ta = ((float)t * 0.5f * 3.14159f) + 0.785f; // 45 deg offset, same as in-game
      for ( int seg = 0; seg < 3; seg++ )
      {
        float scale = (float)size / 28.0f; // 28 = in-game body diameter (radius 14 * 2)
        float ext = (6.0f + (float)seg * 5.0f) * scale;
        float base_off = 14.0f * scale;
        float tx = (float)cx + cosf(ta) * (base_off + ext);
        float ty = (float)cy + sinf(ta) * (base_off + ext);
        float seg_sz = 4.0f * scale;
        if ( seg_sz < 2.0f ) seg_sz = 2.0f;
        a_DrawFilledRect(
          (aRectf_t){tx - seg_sz / 2, ty - seg_sz / 2, seg_sz, seg_sz},
          c
        );
      }
    }

    // Body circle
    a_DrawFilledCircle( cx, cy, body_r, c );

    // Pupil -- small dark circle like in-game
    int pupil_r = body_r / 4;
    if ( pupil_r < 2 ) pupil_r = 2;
    a_DrawFilledCircle( cx, cy, pupil_r, (aColor_t){20, 20, 30, 255} );
  }
  else if ( type_index == ENEMY_TYPE_MIMIC )
  {
    // Mimic -- treasure chest shape (lid + base + clasp)
    float sz = (float)size;
    float fcx = (float)cx;
    float fcy = (float)cy;
    float base_h = sz * 0.55f;
    float lid_h = sz * 0.45f;
    float base_y = fcy + sz * 0.5f - base_h;
    float lid_gap = lid_h * 0.2f;
    float lid_y = base_y - lid_h - lid_gap;

    // Base (bottom half)
    a_DrawFilledRect(
      (aRectf_t){fcx - sz / 2.0f, base_y, sz, base_h},
      c
    );
    // Lid (top half)
    a_DrawFilledRect(
      (aRectf_t){fcx - sz / 2.0f, lid_y, sz, lid_h},
      c
    );
    // Lid top arc
    a_DrawFilledRect(
      (aRectf_t){fcx - sz * 0.4f, lid_y - 2, sz * 0.8f, 3},
      c
    );
    // Gold clasp
    a_DrawFilledRect(
      (aRectf_t){fcx - 2, base_y - 1, 4, 4},
      (aColor_t){200, 170, 50, 255}
    );
  }
  else
  {
    // Grunt / Brute -- square
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

void scene_enemy_types_logic( float dt )
{
  (void)dt;

  if ( etype_tracks_sort_dirty ) etype_rebuild_track_sort();

  // Modal logic
  if ( etype_modal_open )
  {
    int total_pages = (ETYPE_TRACK_COUNT + ETYPE_MODAL_PER_PAGE - 1) / ETYPE_MODAL_PER_PAGE;
    if ( etype_modal_page >= total_pages ) etype_modal_page = total_pages - 1;
    int page_start = etype_modal_page * ETYPE_MODAL_PER_PAGE;
    int page_end = page_start + ETYPE_MODAL_PER_PAGE;
    if ( page_end > ETYPE_TRACK_COUNT ) page_end = ETYPE_TRACK_COUNT;
    int visible = page_end - page_start;

    int pw = 500, ph = 80 + visible * 100 + 50;
    int epx = (SCREEN_WIDTH - pw) / 2;
    int epy = (SCREEN_HEIGHT - ph) / 2;

    // Click on BACK button
    {
      int bw = 100, bh = 24;
      int bx = (SCREEN_WIDTH - bw) / 2;
      int by = epy + ph - 32;
      if ( app.mouse.button == 1 && app.mouse.pressed &&
           point_in_rect( app.mouse.x, app.mouse.y, bx, by, bw, bh ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        etype_modal_open = 0;
        return;
      }
    }

    // Page navigation (A/D or Left/Right + mouse click on arrows)
    if ( total_pages > 1 )
    {
      int mnav_y = epy + 80 + visible * 100 + 2;
      int mnav_h = 20;
      int mleft_x = epx + 10, mleft_w = 80;
      int mright_x = epx + pw - 90, mright_w = 80;

      if ( app.keyboard[ SDL_SCANCODE_A ] == 1 || app.keyboard[ SDL_SCANCODE_LEFT ] == 1
           || ( app.mouse.button == 1 && app.mouse.pressed && etype_modal_page > 0
                && point_in_rect( app.mouse.x, app.mouse.y, mleft_x, mnav_y, mleft_w, mnav_h ) ) )
      {
        int was_mouse = (app.mouse.button == 1 && app.mouse.pressed);
        app.keyboard[ SDL_SCANCODE_A ] = 0;
        app.keyboard[ SDL_SCANCODE_LEFT ] = 0;
        if ( was_mouse ) app.mouse.button = 0;
        if ( etype_modal_page > 0 )
        {
          etype_modal_page--;
          etype_modal_track_sel = 0;
          play_menu_move();
        }
      }
      if ( app.keyboard[ SDL_SCANCODE_D ] == 1 || app.keyboard[ SDL_SCANCODE_RIGHT ] == 1
           || ( app.mouse.button == 1 && app.mouse.pressed && etype_modal_page < total_pages - 1
                && point_in_rect( app.mouse.x, app.mouse.y, mright_x, mnav_y, mright_w, mnav_h ) ) )
      {
        int was_mouse = (app.mouse.button == 1 && app.mouse.pressed);
        app.keyboard[ SDL_SCANCODE_D ] = 0;
        app.keyboard[ SDL_SCANCODE_RIGHT ] = 0;
        if ( was_mouse ) app.mouse.button = 0;
        if ( etype_modal_page < total_pages - 1 )
        {
          etype_modal_page++;
          etype_modal_track_sel = 0;
          play_menu_move();
        }
      }
      // Recalculate after page change
      page_start = etype_modal_page * ETYPE_MODAL_PER_PAGE;
      page_end = page_start + ETYPE_MODAL_PER_PAGE;
      if ( page_end > ETYPE_TRACK_COUNT ) page_end = ETYPE_TRACK_COUNT;
      visible = page_end - page_start;
    }

    if ( etype_modal_track_sel >= visible ) etype_modal_track_sel = visible - 1;

    static MenuRect_t em_rects[ETYPE_MODAL_PER_PAGE];
    for ( int t = 0; t < visible; t++ )
      em_rects[t] = (MenuRect_t){ epx + 20, epy + 80 + t * 100, pw - 40, 90 };

    static Menu_t em;
    em.selected = etype_modal_track_sel;
    em.count    = visible;
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
      int abs_sel = page_start + etype_modal_track_sel;
      int sorted_idx = etype_tracks_sorted[etype_modal_type][abs_sel];
      ProgressUpgradeId_t id = etype_tracks[etype_modal_type][sorted_idx];
      progress_purchase(id);
      etype_tracks_sort_dirty = 1;
    }
    return;
  }

  // ESC / Backspace / click Back button -> back to main menu
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
      current_scene = EM_SCENE_MAIN_MENU;
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
        etype_selected = progress_is_mimic_discovered() ? 2
                       : progress_is_beholder_discovered() ? 1 : 0;
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
      int max_slot = progress_is_mimic_discovered() ? 2
                   : progress_is_beholder_discovered() ? 1 : 0;
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
      etype_modal_page = 0;
    }
    else if ( etype_page == 1 && etype_selected == 0 && progress_is_snake_discovered() )
    {
      play_menu_click();
      etype_modal_open = 1;
      etype_modal_type = ENEMY_TYPE_SNAKE;
      etype_modal_track_sel = 0;
      etype_modal_page = 0;
    }
    else if ( etype_page == 1 && etype_selected == 1 && progress_is_beholder_discovered() )
    {
      play_menu_click();
      etype_modal_open = 1;
      etype_modal_type = ENEMY_TYPE_BEHOLDER;
      etype_modal_track_sel = 0;
      etype_modal_page = 0;
    }
    else if ( etype_page == 1 && etype_selected == 2 && progress_is_mimic_discovered() )
    {
      play_menu_click();
      etype_modal_open = 1;
      etype_modal_type = ENEMY_TYPE_MIMIC;
      etype_modal_track_sel = 0;
      etype_modal_page = 0;
    }
  }

  // Mouse hover and click
  int mx = app.mouse.x;
  int my = app.mouse.y;

  // Mouse: click card navigation arrows (< >) — page 0 only
  if ( etype_page == 0 )
  {
    int nav_total_w = ETYPE_CARD_COUNT * ETYPE_CARD_W + (ETYPE_CARD_COUNT - 1) * ETYPE_CARD_GAP;
    int nav_start_x = (SCREEN_WIDTH - nav_total_w) / 2;
    int card_yy = (SCREEN_HEIGHT - ETYPE_CARD_H) / 2;
    int nav_w = 40, nav_h = 40;
    int nav_y = card_yy + ETYPE_CARD_H / 2 - nav_h / 2;
    int left_nx = nav_start_x - 30 - nav_w;
    int right_nx = nav_start_x + nav_total_w + 30;

    if ( app.mouse.pressed && point_in_rect( mx, my, left_nx, nav_y, nav_w, nav_h ) )
    {
      app.mouse.pressed = 0;
      play_menu_move();
      if ( etype_selected > 0 )
        etype_selected--;
      else
      {
        etype_page = 1;
        etype_selected = progress_is_mimic_discovered() ? 2
                       : progress_is_beholder_discovered() ? 1 : 0;
      }
    }
    else if ( app.mouse.pressed && point_in_rect( mx, my, right_nx, nav_y, nav_w, nav_h ) )
    {
      app.mouse.pressed = 0;
      play_menu_move();
      if ( etype_selected < ETYPE_CARD_COUNT - 1 )
        etype_selected++;
      else
      {
        etype_page = 1;
        etype_selected = 0;
      }
    }
  }

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
          etype_modal_page = 0;
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
        etype_modal_page = 0;
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
        etype_modal_page = 0;
      }
    }

    // Mimic card (slot 2) is hoverable/clickable if discovered
    if ( progress_is_mimic_discovered() )
    {
      int mm_cx = start_x + 2 * (ETYPE_CARD_W + ETYPE_CARD_GAP);
      if ( mouse_moved_this_frame && etype_selected != 2 &&
           point_in_rect( mx, my, mm_cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        etype_selected = 2;
        play_menu_move();
      }

      if ( app.mouse.pressed &&
           point_in_rect( mx, my, mm_cx, card_y, ETYPE_CARD_W, ETYPE_CARD_H ) )
      {
        app.mouse.pressed = 0;
        play_menu_click();
        etype_modal_open = 1;
        etype_modal_type = ENEMY_TYPE_MIMIC;
        etype_modal_track_sel = 0;
        etype_modal_page = 0;
      }
    }
  }
}

void scene_enemy_types_draw( float dt )
{
  (void)dt;

  if ( etype_tracks_sort_dirty ) etype_rebuild_track_sort();

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
    // Page 1 -- 4 standard enemy type cards
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
    // Page 2 -- Snake card + Beholder + Mimic cards
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
          // Unlocked snake card -- full content like page 1 cards
          const EnemyTypeCard_t* card = &snake_card;

          aTextStyle_t name_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {255, 255, 255, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( card->name, mid_x, ty, name_style );
          ty += 34;

          // Snake shape -- compact, matching page 1 swatch size
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
          a_DrawText( "Regens shield when broken", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Must get close to burst down", mid_x, ty, desc_style );
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
        // Mimic card (slot 2)
        int mm_discovered = progress_is_mimic_discovered();
        int mm_selected = (etype_selected == 2 && mm_discovered);

        aColor_t bg = mm_discovered
          ? (mm_selected ? (aColor_t){50, 50, 80, 240} : (aColor_t){30, 30, 50, 240})
          : (aColor_t){25, 25, 35, 240};
        a_DrawFilledRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          bg
        );

        aColor_t border = mm_discovered
          ? (mm_selected ? ETYPE_ACCENT_COLOR : (aColor_t){100, 100, 130, 200})
          : (aColor_t){60, 60, 70, 200};
        a_DrawRect(
          (aRectf_t){(float)cx, (float)card_y, (float)ETYPE_CARD_W, (float)ETYPE_CARD_H},
          border
        );
        if ( mm_selected )
        {
          a_DrawRect(
            (aRectf_t){(float)(cx + 1), (float)(card_y + 1),
                       (float)(ETYPE_CARD_W - 2), (float)(ETYPE_CARD_H - 2)},
            border
          );
        }

        int ty = card_y + 20;

        if ( !mm_discovered )
        {
          aTextStyle_t lock_name = {
            .type = FONT_ENTER_COMMAND,
            .fg = {80, 80, 90, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( "???", mid_x, ty, lock_name );
          ty += 60;

          // Grey chest silhouette
          etype_draw_shape( ENEMY_TYPE_MIMIC, mid_x, ty + 13, 26, (aColor_t){60, 60, 70, 255} );
          ty += 60;

          aTextStyle_t hint_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {100, 100, 120, 200},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.5f
          };
          a_DrawText( "Survive 3:00 to", mid_x, ty, hint_style );
          a_DrawText( "discover this enemy", mid_x, ty + 18, hint_style );
        }
        else
        {
          const EnemyTypeCard_t* card = &mimic_card;

          aTextStyle_t name_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {255, 255, 255, 255},
            .align = TEXT_ALIGN_CENTER,
            .scale = 0.8f
          };
          a_DrawText( card->name, mid_x, ty, name_style );
          ty += 34;

          etype_draw_shape( ENEMY_TYPE_MIMIC, mid_x, ty + 13, 26, ETYPE_SWATCH_COLOR );
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
          a_DrawText( "Steals a weapon on contact", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Uses your upgrades against you", mid_x, ty, desc_style );
          ty += 18;
          a_DrawText( "Kill to recover weapon", mid_x, ty, desc_style );
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

          int kills = progress_get_lifetime_kills(ENEMY_TYPE_MIMIC);
          int pts   = progress_get_available_points(ENEMY_TYPE_MIMIC);
          char kills_buf[64], deaths_buf[64], pts_buf[64];
          snprintf( kills_buf, sizeof(kills_buf), "KILLS: %d", kills );
          snprintf( deaths_buf, sizeof(deaths_buf), "DEATHS: %d",
                    stats_get_deaths_by_type(ENEMY_TYPE_MIMIC) );
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

          int type_has_upg = progress_has_affordable_upgrade_for(ENEMY_TYPE_MIMIC);
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
              .fg = mm_selected ? ETYPE_ACCENT_COLOR : (aColor_t){120, 120, 140, 200},
              .align = TEXT_ALIGN_CENTER,
              .scale = 0.45f
            };
            a_DrawText( "[SELECT TO UPGRADE]", mid_x, bottom_y + 58, select_style );
          }
        }
      }
    }
  }

  // Card navigation arrows (< >) — page 0 only
  if ( etype_page == 0 )
  {
    int nav_total_w = ETYPE_CARD_COUNT * ETYPE_CARD_W + (ETYPE_CARD_COUNT - 1) * ETYPE_CARD_GAP;
    int nav_start_x = (SCREEN_WIDTH - nav_total_w) / 2;
    int nav_w = 40, nav_h = 40;
    int nav_y = card_y + ETYPE_CARD_H / 2 - nav_h / 2;
    int left_nx = nav_start_x - 30 - nav_w;
    int right_nx = nav_start_x + nav_total_w + 30;
    int nmx = app.mouse.x, nmy = app.mouse.y;

    int hover_left = point_in_rect( nmx, nmy, left_nx, nav_y, nav_w, nav_h );
    int hover_right = point_in_rect( nmx, nmy, right_nx, nav_y, nav_w, nav_h );

    aColor_t lc = hover_left ? (aColor_t){180, 255, 180, 255} : (aColor_t){80, 220, 80, 255};
    if ( hover_left )
      a_DrawFilledRect( (aRectf_t){(float)left_nx, (float)nav_y, (float)nav_w, (float)nav_h},
                        (aColor_t){80, 220, 80, 40} );
    aTextStyle_t nls = { .type = FONT_ENTER_COMMAND, .fg = lc, .align = TEXT_ALIGN_CENTER, .scale = 1.0f };
    a_DrawText( "<", left_nx + nav_w / 2, nav_y + nav_h / 2 - 8, nls );

    aColor_t rc = hover_right ? (aColor_t){180, 255, 180, 255} : (aColor_t){80, 220, 80, 255};
    if ( hover_right )
      a_DrawFilledRect( (aRectf_t){(float)right_nx, (float)nav_y, (float)nav_w, (float)nav_h},
                        (aColor_t){80, 220, 80, 40} );
    aTextStyle_t nrs = { .type = FONT_ENTER_COMMAND, .fg = rc, .align = TEXT_ALIGN_CENTER, .scale = 1.0f };
    a_DrawText( ">", right_nx + nav_w / 2, nav_y + nav_h / 2 - 8, nrs );
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

    int total_pages = (ETYPE_TRACK_COUNT + ETYPE_MODAL_PER_PAGE - 1) / ETYPE_MODAL_PER_PAGE;
    int page_start = etype_modal_page * ETYPE_MODAL_PER_PAGE;
    int page_end = page_start + ETYPE_MODAL_PER_PAGE;
    if ( page_end > ETYPE_TRACK_COUNT ) page_end = ETYPE_TRACK_COUNT;
    int visible = page_end - page_start;

    int pw = 500, ph = 80 + visible * 100 + 50;
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
    const char* modal_name;
    if ( etype_modal_type < ETYPE_CARD_COUNT )
      modal_name = etype_cards[etype_modal_type].name;
    else if ( etype_modal_type == ENEMY_TYPE_SNAKE )
      modal_name = snake_card.name;
    else if ( etype_modal_type == ENEMY_TYPE_BEHOLDER )
      modal_name = beholder_card.name;
    else
      modal_name = mimic_card.name;
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

    // Track rows (current page only, sorted by cost)
    for ( int vi = 0; vi < visible; vi++ )
    {
      int t = page_start + vi;
      int sorted_idx = etype_tracks_sorted[etype_modal_type][t];
      ProgressUpgradeId_t id = etype_tracks[etype_modal_type][sorted_idx];
      int tier = progress_get_tier(id);
      int max_tier = progress_get_max_tier(id);
      int cost = progress_get_next_cost(id);
      int can_buy = progress_can_afford(id);
      int selected = (vi == etype_modal_track_sel);

      int row_y = py + 80 + vi * 100;

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

    // Page navigation arrows (below rows, above back button)
    if ( total_pages > 1 )
    {
      int nav_y = py + 80 + visible * 100 + 2;
      int mnav_h = 20;
      int mleft_x = px + 10, mleft_w = 80;
      int mright_x = px + pw - 90, mright_w = 80;
      int amx = app.mouse.x, amy = app.mouse.y;

      if ( etype_modal_page > 0 )
      {
        int lhover = point_in_rect( amx, amy, mleft_x, nav_y, mleft_w, mnav_h );
        if ( lhover )
          a_DrawFilledRect( (aRectf_t){(float)mleft_x, (float)nav_y, (float)mleft_w, (float)mnav_h},
                            (aColor_t){80, 220, 80, 40} );
        aTextStyle_t arr_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = lhover ? (aColor_t){220, 255, 220, 255} : (aColor_t){180, 220, 180, 255},
          .align = TEXT_ALIGN_LEFT,
          .scale = 0.45f
        };
        a_DrawText( "< [A]", px + 30, nav_y, arr_s );
      }
      if ( etype_modal_page < total_pages - 1 )
      {
        int rhover = point_in_rect( amx, amy, mright_x, nav_y, mright_w, mnav_h );
        if ( rhover )
          a_DrawFilledRect( (aRectf_t){(float)mright_x, (float)nav_y, (float)mright_w, (float)mnav_h},
                            (aColor_t){80, 220, 80, 40} );
        aTextStyle_t arr_s = {
          .type = FONT_ENTER_COMMAND,
          .fg = rhover ? (aColor_t){220, 255, 220, 255} : (aColor_t){180, 220, 180, 255},
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.45f
        };
        a_DrawText( "[D] >", px + pw - 30, nav_y, arr_s );
      }

      char pg_buf[16];
      snprintf( pg_buf, sizeof(pg_buf), "%d / %d", etype_modal_page + 1, total_pages );
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
      int abs_sel = page_start + etype_modal_track_sel;
      int sel_sorted_idx = etype_tracks_sorted[etype_modal_type][abs_sel];
      ProgressUpgradeId_t sel_prog = etype_tracks[etype_modal_type][sel_sorted_idx];
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
