#include "achievements_menu.h"
#include "Archimedes.h"
#include "achievements.h"
#include "menu.h"
#include "main_menu.h"
#include <stdio.h>
#include <SDL2/SDL.h>

// ============================================================================
// Externs from main.c
// ============================================================================

extern int current_scene;
#define ACH_SCENE_MAIN_MENU 0

// ============================================================================
// Sound helpers (use shared menu sounds from main_menu.h)
// ============================================================================

static void ach_play_move(void)
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

static void ach_play_click(void)
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
// Filter state
// ============================================================================

static int ach_type_filter  = -1;  // -1=all, 0=general, 1=enemy, 2=weapon
static int ach_lock_filter  = 0;   // 0=all, 1=unlocked, 2=locked
static int ach_scroll       = 0;   // scroll offset (in items)
static int ach_filter_cursor = 0;  // 0-3 = type row, 4-6 = lock row

#define ACH_FILTER_BTN_W   120
#define ACH_FILTER_BTN_H    34
#define ACH_FILTER_BTN_GAP   8
#define ACH_FILTER_TOP_Y    70
#define ACH_LIST_ITEM_H     60
#define ACH_LIST_TOP_Y     170
#define ACH_LIST_VISIBLE     8

// Scrollbar drag state
static int ach_scrollbar_dragging = 0;
static int ach_scrollbar_drag_offset = 0;  // offset from top of thumb to mouse Y

// Build filtered list each frame
static int ach_filtered[ACH_COUNT];
static int ach_filtered_count = 0;

static void ach_rebuild_list(void)
{
  ach_filtered_count = 0;
  for (int i = 0; i < ACH_COUNT; i++)
  {
    const AchDef_t* def = achievements_get_def((AchId_t)i);
    if (!def || !def->name) continue;

    if (ach_type_filter >= 0 && (int)def->type != ach_type_filter)
      continue;

    int is_unlocked = achievements_is_unlocked((AchId_t)i);
    if (ach_lock_filter == 1 && !is_unlocked) continue;
    if (ach_lock_filter == 2 && is_unlocked) continue;

    ach_filtered[ach_filtered_count++] = i;
  }

  int max_scroll = ach_filtered_count - ACH_LIST_VISIBLE;
  if (max_scroll < 0) max_scroll = 0;
  if (ach_scroll > max_scroll) ach_scroll = max_scroll;
  if (ach_scroll < 0) ach_scroll = 0;
}

// ============================================================================
// Logic
// ============================================================================

void scene_achievements_logic(float dt)
{
  (void)dt;

  ach_rebuild_list();

  // --- ESC / Backspace -> back ---
  if (app.keyboard[SDL_SCANCODE_ESCAPE] == 1 || app.keyboard[SDL_SCANCODE_BACKSPACE] == 1)
  {
    app.keyboard[SDL_SCANCODE_ESCAPE] = 0;
    app.keyboard[SDL_SCANCODE_BACKSPACE] = 0;
    ach_play_click();
    current_scene = ACH_SCENE_MAIN_MENU;
    return;
  }

  // --- Filter cursor with left/right/A/D ---
  if (app.keyboard[SDL_SCANCODE_RIGHT] == 1 || app.keyboard[SDL_SCANCODE_D] == 1)
  {
    app.keyboard[SDL_SCANCODE_RIGHT] = 0;
    app.keyboard[SDL_SCANCODE_D] = 0;
    ach_filter_cursor++;
    if (ach_filter_cursor > 6) ach_filter_cursor = 0;  // wrap to start
    ach_play_move();
  }
  if (app.keyboard[SDL_SCANCODE_LEFT] == 1 || app.keyboard[SDL_SCANCODE_A] == 1)
  {
    app.keyboard[SDL_SCANCODE_LEFT] = 0;
    app.keyboard[SDL_SCANCODE_A] = 0;
    ach_filter_cursor--;
    if (ach_filter_cursor < 0) ach_filter_cursor = 6;  // wrap to end
    ach_play_move();
  }

  // --- Toggle filter with Enter/Space ---
  if (app.keyboard[SDL_SCANCODE_RETURN] == 1 || app.keyboard[SDL_SCANCODE_SPACE] == 1)
  {
    app.keyboard[SDL_SCANCODE_RETURN] = 0;
    app.keyboard[SDL_SCANCODE_SPACE] = 0;
    if (ach_filter_cursor <= 3)
    {
      int new_val = ach_filter_cursor - 1;
      if (ach_type_filter != new_val) { ach_type_filter = new_val; ach_scroll = 0; ach_play_click(); }
    }
    else
    {
      int new_val = ach_filter_cursor - 4;
      if (ach_lock_filter != new_val) { ach_lock_filter = new_val; ach_scroll = 0; ach_play_click(); }
    }
  }

  // --- Scroll with up/down/W/S ---
  if (app.keyboard[SDL_SCANCODE_DOWN] == 1 || app.keyboard[SDL_SCANCODE_S] == 1)
  {
    app.keyboard[SDL_SCANCODE_DOWN] = 0;
    app.keyboard[SDL_SCANCODE_S] = 0;
    int max_scroll = ach_filtered_count - ACH_LIST_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (ach_scroll < max_scroll) { ach_scroll++; ach_play_move(); }
  }
  if (app.keyboard[SDL_SCANCODE_UP] == 1 || app.keyboard[SDL_SCANCODE_W] == 1)
  {
    app.keyboard[SDL_SCANCODE_UP] = 0;
    app.keyboard[SDL_SCANCODE_W] = 0;
    if (ach_scroll > 0) { ach_scroll--; ach_play_move(); }
  }

  // --- Scrollbar drag: compute geometry (shared by press + drag) ---
  int sb_list_w = 560;
  int sb_list_x = SCREEN_WIDTH / 2 - sb_list_w / 2;
  int sb_bar_x = sb_list_x + sb_list_w + 6;
  int sb_bar_y = ACH_LIST_TOP_Y;
  int sb_bar_h = ACH_LIST_VISIBLE * ACH_LIST_ITEM_H;
  int sb_max_scroll = ach_filtered_count - ACH_LIST_VISIBLE;
  if (sb_max_scroll < 0) sb_max_scroll = 0;
  float sb_ratio = (ach_filtered_count > 0) ? (float)ACH_LIST_VISIBLE / (float)ach_filtered_count : 1.0f;
  int sb_thumb_h = (int)(sb_ratio * (float)sb_bar_h);
  if (sb_thumb_h < 12) sb_thumb_h = 12;

  // --- Scrollbar drag: handle release ---
  if (ach_scrollbar_dragging && !(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK))
    ach_scrollbar_dragging = 0;

  // --- Scrollbar drag: update position while dragging ---
  if (ach_scrollbar_dragging && sb_max_scroll > 0)
  {
    int track_range = sb_bar_h - sb_thumb_h;
    int target_y = app.mouse.y - ach_scrollbar_drag_offset - sb_bar_y;
    if (target_y < 0) target_y = 0;
    if (target_y > track_range) target_y = track_range;
    ach_scroll = (int)((float)target_y / (float)track_range * (float)sb_max_scroll + 0.5f);
    if (ach_scroll < 0) ach_scroll = 0;
    if (ach_scroll > sb_max_scroll) ach_scroll = sb_max_scroll;
  }

  // --- Mouse click on filter buttons ---
  if (app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT)
  {
    int mx = app.mouse.x, my = app.mouse.y;
    int cx = SCREEN_WIDTH / 2;

    // Scrollbar click (check first — wider hit area for usability)
    if (ach_filtered_count > ACH_LIST_VISIBLE)
    {
      int sb_hit_w = 16;  // wider than visual 4px for easier clicking
      int sb_hit_x = sb_bar_x - 6;
      if (menu_point_in_rect(mx, my, sb_hit_x, sb_bar_y, sb_hit_w, sb_bar_h))
      {
        // Calculate current thumb position
        float pos_ratio = (sb_max_scroll > 0) ? (float)ach_scroll / (float)sb_max_scroll : 0.0f;
        int thumb_y = sb_bar_y + (int)(pos_ratio * (float)(sb_bar_h - sb_thumb_h));

        if (my >= thumb_y && my <= thumb_y + sb_thumb_h)
        {
          // Clicked on thumb — start dragging with offset
          ach_scrollbar_dragging = 1;
          ach_scrollbar_drag_offset = my - thumb_y;
        }
        else
        {
          // Clicked on track — jump to position, then start dragging from center
          int track_range = sb_bar_h - sb_thumb_h;
          int target_y = my - sb_bar_y - sb_thumb_h / 2;
          if (target_y < 0) target_y = 0;
          if (target_y > track_range) target_y = track_range;
          ach_scroll = (int)((float)target_y / (float)track_range * (float)sb_max_scroll + 0.5f);
          if (ach_scroll < 0) ach_scroll = 0;
          if (ach_scroll > sb_max_scroll) ach_scroll = sb_max_scroll;
          ach_scrollbar_dragging = 1;
          ach_scrollbar_drag_offset = sb_thumb_h / 2;
        }
        app.mouse.pressed = 0;
      }
    }

    // Row 0: type filter
    {
      int total_w = ACH_FILTER_BTN_W * 4 + ACH_FILTER_BTN_GAP * 3;
      int base_x = cx - total_w / 2;
      int row_y = ACH_FILTER_TOP_Y;
      for (int i = 0; i < 4; i++)
      {
        int bx = base_x + i * (ACH_FILTER_BTN_W + ACH_FILTER_BTN_GAP);
        if (menu_point_in_rect(mx, my, bx, row_y, ACH_FILTER_BTN_W, ACH_FILTER_BTN_H))
        {
          int new_val = i - 1;
          if (ach_type_filter != new_val) { ach_type_filter = new_val; ach_scroll = 0; ach_play_click(); }
          app.mouse.pressed = 0;
        }
      }
    }

    // Row 1: lock filter
    {
      int total_w = ACH_FILTER_BTN_W * 3 + ACH_FILTER_BTN_GAP * 2;
      int base_x = cx - total_w / 2;
      int row_y = ACH_FILTER_TOP_Y + ACH_FILTER_BTN_H + ACH_FILTER_BTN_GAP;
      for (int i = 0; i < 3; i++)
      {
        int bx = base_x + i * (ACH_FILTER_BTN_W + ACH_FILTER_BTN_GAP);
        if (menu_point_in_rect(mx, my, bx, row_y, ACH_FILTER_BTN_W, ACH_FILTER_BTN_H))
        {
          if (ach_lock_filter != i) { ach_lock_filter = i; ach_scroll = 0; ach_play_click(); }
          app.mouse.pressed = 0;
        }
      }
    }

    // Back button
    {
      int back_w = 120, back_h = 34;
      int back_x = SCREEN_WIDTH / 2 - back_w / 2;
      int back_y = SCREEN_HEIGHT - 46;
      if (menu_point_in_rect(mx, my, back_x, back_y, back_w, back_h))
      {
        app.mouse.pressed = 0;
        ach_play_click();
        current_scene = ACH_SCENE_MAIN_MENU;
        return;
      }
    }

    app.mouse.pressed = 0;
  }

  // --- Mouse wheel scroll ---
  if (app.mouse.wheel != 0)
  {
    ach_scroll -= app.mouse.wheel;
    app.mouse.wheel = 0;
    int max_scroll = ach_filtered_count - ACH_LIST_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (ach_scroll < 0) ach_scroll = 0;
    if (ach_scroll > max_scroll) ach_scroll = max_scroll;
  }
}

// ============================================================================
// Draw
// ============================================================================

void scene_achievements_draw(float dt)
{
  (void)dt;

  int cx = SCREEN_WIDTH / 2;
  int mx = app.mouse.x, my = app.mouse.y;

  // --- Title ---
  aTextStyle_t title_st = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 200, 50, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 1.4f
  };
  a_DrawText("ACHIEVEMENTS", cx, 6, title_st);

  // --- Counter ---
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d / %d",
             achievements_get_unlocked_count(), achievements_get_total_count());
    aTextStyle_t count_st = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 180, 200, 200},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    a_DrawText(buf, cx, 48, count_st);
  }

  // --- Filter row 0: type filter ---
  {
    const char* labels[] = { "ALL", "GENERAL", "ENEMY", "WEAPON" };
    aColor_t type_colors[] = {
      {180, 180, 200, 255},
      {255, 220, 50, 255},
      {80, 220, 80, 255},
      {100, 180, 255, 255},
    };
    int total_w = ACH_FILTER_BTN_W * 4 + ACH_FILTER_BTN_GAP * 3;
    int base_x = cx - total_w / 2;
    int row_y = ACH_FILTER_TOP_Y;

    for (int i = 0; i < 4; i++)
    {
      int bx = base_x + i * (ACH_FILTER_BTN_W + ACH_FILTER_BTN_GAP);
      int sel = (ach_type_filter == i - 1);
      int hovered = menu_point_in_rect(mx, my, bx, row_y, ACH_FILTER_BTN_W, ACH_FILTER_BTN_H);
      int kb_cur = (ach_filter_cursor == i);
      int highlight = sel || hovered || kb_cur;

      aColor_t bg = sel ? (aColor_t){60, 60, 100, 255} : highlight ? (aColor_t){50, 50, 80, 255} : (aColor_t){35, 35, 55, 220};
      aColor_t border = sel ? type_colors[i] : kb_cur ? (aColor_t){255, 255, 255, 255} : highlight ? (aColor_t){150, 150, 180, 255} : (aColor_t){80, 80, 110, 180};

      a_DrawFilledRect((aRectf_t){(float)bx, (float)row_y, (float)ACH_FILTER_BTN_W, (float)ACH_FILTER_BTN_H}, bg);
      a_DrawRect((aRectf_t){(float)bx, (float)row_y, (float)ACH_FILTER_BTN_W, (float)ACH_FILTER_BTN_H}, border);

      aTextStyle_t btn_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = sel ? type_colors[i] : highlight ? (aColor_t){220, 220, 240, 255} : (aColor_t){140, 140, 160, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.55f
      };
      a_DrawText(labels[i], bx + ACH_FILTER_BTN_W / 2, row_y + 6, btn_st);
    }
  }

  // --- Filter row 1: lock filter ---
  {
    const char* labels[] = { "ALL", "UNLOCKED", "LOCKED" };
    int total_w = ACH_FILTER_BTN_W * 3 + ACH_FILTER_BTN_GAP * 2;
    int base_x = cx - total_w / 2;
    int row_y = ACH_FILTER_TOP_Y + ACH_FILTER_BTN_H + ACH_FILTER_BTN_GAP;

    for (int i = 0; i < 3; i++)
    {
      int bx = base_x + i * (ACH_FILTER_BTN_W + ACH_FILTER_BTN_GAP);
      int sel = (ach_lock_filter == i);
      int hovered = menu_point_in_rect(mx, my, bx, row_y, ACH_FILTER_BTN_W, ACH_FILTER_BTN_H);
      int kb_cur = (ach_filter_cursor == i + 4);
      int highlight = sel || hovered || kb_cur;

      aColor_t bg = sel ? (aColor_t){60, 60, 100, 255} : highlight ? (aColor_t){50, 50, 80, 255} : (aColor_t){35, 35, 55, 220};
      aColor_t border = sel ? (aColor_t){200, 200, 255, 255} : kb_cur ? (aColor_t){255, 255, 255, 255} : highlight ? (aColor_t){150, 150, 180, 255} : (aColor_t){80, 80, 110, 180};

      a_DrawFilledRect((aRectf_t){(float)bx, (float)row_y, (float)ACH_FILTER_BTN_W, (float)ACH_FILTER_BTN_H}, bg);
      a_DrawRect((aRectf_t){(float)bx, (float)row_y, (float)ACH_FILTER_BTN_W, (float)ACH_FILTER_BTN_H}, border);

      aTextStyle_t btn_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = sel ? (aColor_t){220, 220, 255, 255} : highlight ? (aColor_t){220, 220, 240, 255} : (aColor_t){140, 140, 160, 200},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.55f
      };
      a_DrawText(labels[i], bx + ACH_FILTER_BTN_W / 2, row_y + 6, btn_st);
    }
  }

  // --- Achievement list ---
  {
    int list_w = 560;
    int list_x = cx - list_w / 2;
    int list_y = ACH_LIST_TOP_Y;
    int visible = ACH_LIST_VISIBLE;
    if (ach_filtered_count < visible) visible = ach_filtered_count;

    aColor_t type_badge_colors[] = {
      {255, 220, 50, 255},
      {80, 220, 80, 255},
      {100, 180, 255, 255},
    };

    if (ach_filtered_count == 0)
    {
      aTextStyle_t empty_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = {120, 120, 140, 180},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.6f
      };
      a_DrawText("No achievements match this filter", cx, list_y + 40, empty_st);
    }

    for (int vi = 0; vi < visible; vi++)
    {
      int idx = ach_filtered[ach_scroll + vi];
      const AchDef_t* def = achievements_get_def((AchId_t)idx);
      if (!def) continue;
      int is_unlocked = achievements_is_unlocked((AchId_t)idx);

      int iy = list_y + vi * ACH_LIST_ITEM_H;

      // Item background
      aColor_t item_bg = is_unlocked ? (aColor_t){40, 45, 55, 230} : (aColor_t){30, 30, 40, 200};
      aColor_t ach_color = (def->reward <= 0) ? (aColor_t){220, 60, 60, 255} : type_badge_colors[def->type];
      aColor_t item_border = is_unlocked ? ach_color : (aColor_t){60, 60, 80, 150};
      if (is_unlocked)
        item_border = (aColor_t){item_border.r / 2, item_border.g / 2, item_border.b / 2, 200};

      a_DrawFilledRect((aRectf_t){(float)list_x, (float)iy, (float)list_w, (float)(ACH_LIST_ITEM_H - 4)}, item_bg);
      a_DrawRect((aRectf_t){(float)list_x, (float)iy, (float)list_w, (float)(ACH_LIST_ITEM_H - 4)}, item_border);

      // Lock/check icon
      int icon_x = list_x + 8;
      int icon_cy = iy + (ACH_LIST_ITEM_H - 4) / 2;
      if (is_unlocked)
      {
        aColor_t check = {80, 220, 80, 255};
        a_DrawLine(icon_x + 2, icon_cy, icon_x + 6, icon_cy + 4, check);
        a_DrawLine(icon_x + 6, icon_cy + 4, icon_x + 14, icon_cy - 4, check);
      }
      else
      {
        aColor_t lock_col = {100, 100, 120, 180};
        a_DrawFilledRect((aRectf_t){(float)(icon_x + 3), (float)(icon_cy - 1), 10.0f, 8.0f}, lock_col);
        a_DrawRect((aRectf_t){(float)(icon_x + 5), (float)(icon_cy - 6), 6.0f, 6.0f}, lock_col);
      }

      // Name
      int text_x = list_x + 28;
      aColor_t name_col = is_unlocked ? (aColor_t){240, 240, 255, 255} : (aColor_t){140, 140, 160, 200};
      aTextStyle_t name_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = name_col,
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.6f
      };
      a_DrawText(def->name, text_x, iy + 4, name_st);

      // Description
      aColor_t desc_col = is_unlocked ? (aColor_t){180, 180, 200, 200} : (aColor_t){110, 110, 130, 160};
      aTextStyle_t desc_st = {
        .type = FONT_ENTER_COMMAND,
        .fg = desc_col,
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.48f
      };
      a_DrawText(def->desc, text_x, iy + 28, desc_st);

      // Reward badge
      {
        char reward_buf[32];
        snprintf(reward_buf, sizeof(reward_buf), "%s%d", def->reward > 0 ? "+" : "", def->reward);
        int badge_x = list_x + list_w - 55;
        aColor_t badge_col = is_unlocked ? (aColor_t){ach_color.r, ach_color.g, ach_color.b, 200}
                                         : (aColor_t){100, 100, 120, 140};
        aTextStyle_t reward_st = {
          .type = FONT_ENTER_COMMAND,
          .fg = badge_col,
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.6f
        };
        a_DrawText(reward_buf, badge_x + 40, iy + 4, reward_st);

        const char* type_labels[] = { "GP", "EP", "WP" };
        aTextStyle_t type_st = {
          .type = FONT_ENTER_COMMAND,
          .fg = badge_col,
          .align = TEXT_ALIGN_RIGHT,
          .scale = 0.42f
        };
        a_DrawText(type_labels[def->type], badge_x + 40, iy + 28, type_st);
      }
    }

    // Scroll indicator
    if (ach_filtered_count > ACH_LIST_VISIBLE)
    {
      int bar_x = list_x + list_w + 6;
      int bar_h = ACH_LIST_VISIBLE * ACH_LIST_ITEM_H;
      int bar_y = list_y;

      a_DrawFilledRect((aRectf_t){(float)bar_x, (float)bar_y, 4.0f, (float)bar_h}, (aColor_t){40, 40, 60, 150});

      int max_scroll = ach_filtered_count - ACH_LIST_VISIBLE;
      float ratio = (float)ACH_LIST_VISIBLE / (float)ach_filtered_count;
      int thumb_h = (int)(ratio * (float)bar_h);
      if (thumb_h < 12) thumb_h = 12;
      float pos_ratio = (max_scroll > 0) ? (float)ach_scroll / (float)max_scroll : 0.0f;
      int thumb_y = bar_y + (int)(pos_ratio * (float)(bar_h - thumb_h));
      a_DrawFilledRect((aRectf_t){(float)bar_x, (float)thumb_y, 4.0f, (float)thumb_h}, (aColor_t){120, 120, 160, 200});
    }
  }

  // --- Legend panel (right side) ---
  {
    int list_w = 560;
    int list_x = cx - list_w / 2;
    int legend_x = list_x + list_w + 24;
    int legend_y = ACH_LIST_TOP_Y;
    int legend_w = 260;
    int legend_h = 210;

    // Panel background
    a_DrawFilledRect((aRectf_t){(float)legend_x, (float)legend_y, (float)legend_w, (float)legend_h},
                     (aColor_t){30, 30, 45, 220});
    a_DrawRect((aRectf_t){(float)legend_x, (float)legend_y, (float)legend_w, (float)legend_h},
               (aColor_t){80, 80, 110, 180});

    aTextStyle_t legend_title = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 200, 220, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.55f
    };
    a_DrawText("REWARD TYPES", legend_x + 10, legend_y + 8, legend_title);

    int ly = legend_y + 40;
    aTextStyle_t abbr_st = { .type = FONT_ENTER_COMMAND, .align = TEXT_ALIGN_LEFT, .scale = 0.52f };
    aTextStyle_t desc_st = { .type = FONT_ENTER_COMMAND, .align = TEXT_ALIGN_LEFT, .scale = 0.42f };

    // GP
    abbr_st.fg = (aColor_t){255, 220, 50, 255};
    a_DrawText("GP", legend_x + 10, ly, abbr_st);
    desc_st.fg = (aColor_t){160, 160, 180, 200};
    a_DrawText("Global Points", legend_x + 40, ly + 2, desc_st);
    desc_st.fg = (aColor_t){130, 130, 150, 170};
    a_DrawText("for global upgrades", legend_x + 40, ly + 20, desc_st);
    ly += 48;

    // EP
    abbr_st.fg = (aColor_t){80, 220, 80, 255};
    a_DrawText("EP", legend_x + 10, ly, abbr_st);
    desc_st.fg = (aColor_t){160, 160, 180, 200};
    a_DrawText("Enemy Points", legend_x + 40, ly + 2, desc_st);
    desc_st.fg = (aColor_t){130, 130, 150, 170};
    a_DrawText("for that enemy's upgrades", legend_x + 40, ly + 20, desc_st);
    ly += 48;

    // WP
    abbr_st.fg = (aColor_t){100, 180, 255, 255};
    a_DrawText("WP", legend_x + 10, ly, abbr_st);
    desc_st.fg = (aColor_t){160, 160, 180, 200};
    a_DrawText("Weapon Points", legend_x + 40, ly + 2, desc_st);
    desc_st.fg = (aColor_t){130, 130, 150, 170};
    a_DrawText("for that weapon's upgrades", legend_x + 40, ly + 20, desc_st);
  }

  // --- Back button ---
  {
    int back_w = 120, back_h = 34;
    int back_x = cx - back_w / 2;
    int back_y = SCREEN_HEIGHT - 46;
    int hovered = menu_point_in_rect(mx, my, back_x, back_y, back_w, back_h);
    aColor_t bg = hovered ? (aColor_t){70, 70, 110, 255} : (aColor_t){45, 45, 65, 255};
    aColor_t border = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){120, 120, 150, 200};
    a_DrawFilledRect((aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h}, bg);
    a_DrawRect((aRectf_t){(float)back_x, (float)back_y, (float)back_w, (float)back_h}, border);
    aTextStyle_t btn_st = {
      .type = FONT_ENTER_COMMAND,
      .fg = hovered ? (aColor_t){255, 255, 255, 255} : (aColor_t){180, 180, 180, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.65f
    };
    a_DrawText("BACK", back_x + back_w / 2, back_y + 6, btn_st);
  }
}
