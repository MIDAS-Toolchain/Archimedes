#include "upgrades_menu.h"
#include "Archimedes.h"
#include "progress.h"
#include "menu.h"
#include "main_menu.h"
#include <stdio.h>
#include <SDL2/SDL.h>

// ============================================================================
// Externs from main.c
// ============================================================================

extern int current_scene;
#define UM_SCENE_MAIN_MENU 0

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
// Global Upgrades Scene
// ============================================================================

#define UPGRADES_PANEL_W 560
#define UPGRADES_PANEL_H 530
#define UPGRADES_ROW_H   68
#define UPGRADES_PER_PAGE 6

static int upgrades_page = 0;
static int upgrades_sel = 0;
static int upgrades_sorted_dirty = 1;
static int upgrades_sorted[GLOBAL_COUNT];

#define point_in_rect  menu_point_in_rect
#define mouse_moved_this_frame  menu_mouse_moved()

static void upgrades_rebuild_sort( void )
{
  // Initialize with identity
  for ( int i = 0; i < GLOBAL_COUNT; i++ ) upgrades_sorted[i] = i;

  // Insertion sort by next cost (maxed = INT_MAX, so they go last)
  for ( int i = 1; i < GLOBAL_COUNT; i++ )
  {
    int key = upgrades_sorted[i];
    int key_cost = global_get_next_cost( (GlobalUpgradeId_t)key );
    if ( key_cost < 0 ) key_cost = 999999;  // maxed -> sort last

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

void upgrades_menu_enter(void)
{
  upgrades_sel = 0;
  upgrades_sorted_dirty = 1;
}

void scene_upgrades_logic( float dt )
{
  (void)dt;

  if ( upgrades_sorted_dirty ) upgrades_rebuild_sort();

  int total_pages = (GLOBAL_COUNT + UPGRADES_PER_PAGE - 1) / UPGRADES_PER_PAGE;
  int page_start = upgrades_page * UPGRADES_PER_PAGE;
  int page_end = page_start + UPGRADES_PER_PAGE;
  if ( page_end > GLOBAL_COUNT ) page_end = GLOBAL_COUNT;

  // ESC / Backspace / click Back button -> back to main menu
  {
    int upy = (SCREEN_HEIGHT - UPGRADES_PANEL_H) / 2;
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = upy + UPGRADES_PANEL_H + 10;
    int esc = app.keyboard[ SDL_SCANCODE_ESCAPE ] == 1 || app.keyboard[ SDL_SCANCODE_BACKSPACE ] == 1;
    int clicked = app.mouse.button == 1 && app.mouse.pressed &&
                  point_in_rect( app.mouse.x, app.mouse.y, back_x, back_y, back_w, back_h );
    if ( esc || clicked )
    {
      app.keyboard[ SDL_SCANCODE_ESCAPE ] = 0;
      app.keyboard[ SDL_SCANCODE_BACKSPACE ] = 0;
      if ( clicked ) app.mouse.button = 0;
      current_scene = UM_SCENE_MAIN_MENU;
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

  // Hover on rows (current page only) -- only when mouse has moved
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

void scene_upgrades_draw( float dt )
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

  // "BACK" button (below the panel)
  {
    int back_w = 100, back_h = 28;
    int back_x = SCREEN_WIDTH / 2 - back_w / 2;
    int back_y = py + UPGRADES_PANEL_H + 10;
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
    int help_w = 260;
    int help_h = 170;
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

    int hy = help_y + 14;
    aTextStyle_t help_title = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 215, 0, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.55f
    };
    a_DrawText( "HOW TO EARN POINTS", help_x + 14, hy, help_title );
    hy += 34;

    aTextStyle_t help_body = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 200, 220},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.45f
    };
    a_DrawText( "Level up during a run", help_x + 14, hy, help_body );
    hy += 22;
    a_DrawText( "to earn global points.", help_x + 14, hy, help_body );
    hy += 32;
    a_DrawText( "Each level-up grants", help_x + 14, hy, help_body );
    hy += 22;
    a_DrawText( "points equal to your", help_x + 14, hy, help_body );
    hy += 22;
    a_DrawText( "current level.", help_x + 14, hy, help_body );
  }
}
