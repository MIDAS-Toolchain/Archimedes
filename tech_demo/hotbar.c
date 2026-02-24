#include <math.h>
#include "Archimedes.h"
#include "hotbar.h"
#include "weapons.h"

#define SLOT_SIZE       40
#define HOTBAR_GAP      6
#define HOTBAR_PADDING  4
#define PIE_RADIUS      18
#define HOTBAR_BOTTOM_MARGIN 8

static FlexBox_t* hotbar_flex;
static int slot_count = 0;

static void recalc_bounds(void)
{
  int total_w = HOTBAR_PADDING * 2 + SLOT_SIZE * slot_count + HOTBAR_GAP * (slot_count > 1 ? slot_count - 1 : 0);
  int total_h = HOTBAR_PADDING * 2 + SLOT_SIZE;
  int x = (SCREEN_WIDTH - total_w) / 2;
  int y = SCREEN_HEIGHT - total_h - HOTBAR_BOTTOM_MARGIN;

  a_FlexSetBounds(hotbar_flex, x, y, total_w, total_h);
  a_FlexLayout(hotbar_flex);
}

void hotbar_init(void)
{
  hotbar_flex = a_FlexBoxCreate(0, 0, 0, 0);
  a_FlexConfigure(hotbar_flex, FLEX_DIR_ROW, FLEX_JUSTIFY_CENTER, HOTBAR_GAP);
  a_FlexSetAlign(hotbar_flex, FLEX_ALIGN_CENTER);
  a_FlexSetPadding(hotbar_flex, HOTBAR_PADDING);

  // Start with 1 slot for the wand
  a_FlexAddItem(hotbar_flex, SLOT_SIZE, SLOT_SIZE, NULL);
  slot_count = 1;

  recalc_bounds();
}

void hotbar_cleanup(void)
{
  if (hotbar_flex) {
    a_FlexBoxDestroy(&hotbar_flex);
  }
  slot_count = 0;
}

void hotbar_add_slot(void)
{
  a_FlexAddItem(hotbar_flex, SLOT_SIZE, SLOT_SIZE, NULL);
  slot_count++;
  recalc_bounds();
}

/**
 * Draw a pie-chart cooldown overlay on a slot rect.
 * progress: 0.0 = just fired (full overlay), 1.0 = ready (no overlay)
 */
static void draw_cooldown_pie(aRectf_t rect, float progress)
{
  if (progress >= 1.0f) return;

  float remaining = 1.0f - progress;
  float threshold = remaining * 2.0f * (float)PI;

  int cx = (int)(rect.x + rect.w / 2.0f);
  int cy = (int)(rect.y + rect.h / 2.0f);
  int r = PIE_RADIUS;
  int r2 = r * r;

  aColor_t overlay = {0, 0, 0, 140};

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, overlay.r, overlay.g, overlay.b, overlay.a);

  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy > r2) continue;

      float angle = atan2f((float)dx, (float)(-dy));
      if (angle < 0.0f) angle += 2.0f * (float)PI;

      if (angle < threshold) {
        SDL_RenderDrawPoint(app.renderer, cx + dx, cy + dy);
      }
    }
  }

  SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

int hotbar_get_slot_count(void)
{
  return slot_count;
}

int hotbar_get_hovered_slot(int mx, int my)
{
  if (!hotbar_flex) return -1;
  for (int i = 0; i < slot_count; i++) {
    const FlexItem_t* item = a_FlexGetItem(hotbar_flex, i);
    if (!item) continue;
    if (mx >= item->calc_x && mx < item->calc_x + item->w &&
        my >= item->calc_y && my < item->calc_y + item->h)
      return i;
  }
  return -1;
}

int hotbar_get_slot_rect(int slot, int* out_x, int* out_y, int* out_w, int* out_h)
{
  if (!hotbar_flex || slot < 0 || slot >= slot_count) return -1;
  const FlexItem_t* item = a_FlexGetItem(hotbar_flex, slot);
  if (!item) return -1;
  *out_x = item->calc_x;
  *out_y = item->calc_y;
  *out_w = item->w;
  *out_h = item->h;
  return 0;
}

void hotbar_draw(void)
{
  if (!hotbar_flex) return;

  /* Container background */
  aRectf_t bg_rect = {
    (float)hotbar_flex->x, (float)hotbar_flex->y,
    (float)hotbar_flex->w, (float)hotbar_flex->h
  };
  a_DrawFilledRect(bg_rect, (aColor_t){20, 20, 20, 180});
  a_DrawRect(bg_rect, (aColor_t){255, 255, 255, 100});

  aTextStyle_t label_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.6f
  };

  for (int i = 0; i < slot_count; i++) {
    const FlexItem_t* item = a_FlexGetItem(hotbar_flex, i);
    if (!item) continue;

    aRectf_t r = {
      (float)item->calc_x, (float)item->calc_y,
      (float)item->w, (float)item->h
    };

    const Weapon_t* w = weapons_get_slot(i);

    /* Slot background */
    a_DrawFilledRect(r, (aColor_t){60, 60, 80, 220});
    a_DrawRect(r, (aColor_t){255, 255, 255, 60});

    /* Label */
    if (w) {
      int tx = (int)(r.x + r.w / 2);
      int ty = (int)(r.y + 10);
      a_DrawText(w->label, tx, ty, label_style);
    }

    /* Cooldown pie overlay */
    float progress = weapons_get_cooldown_progress(i);
    draw_cooldown_pie(r, progress);
  }
}
