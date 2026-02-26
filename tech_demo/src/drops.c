#include <math.h>
#include "Archimedes.h"
#include "drops.h"
#include "weapons.h"
#include "hotbar.h"
#include "player_actions.h"
#include "stats.h"
#include "progress.h"

#define MAX_DROPS      16
#define DROP_SIZE       30.0f
#define PICKUP_RADIUS  56.0f
#define DROP_BOB_SPEED 3.0f
#define DROP_BOB_AMP   3.0f
#define ARROW_BOB_SPEED 4.0f
#define ARROW_BOB_AMP  6.0f
#define GLOW_PULSE_SPEED 5.0f

typedef struct {
  float x, y;
  WeaponType_t type;
  int active;
  float age;
} Drop_t;

static Drop_t drops[MAX_DROPS];
static aSoundEffect_t heal_sound;
static int heal_sound_loaded = 0;
static WeaponType_t last_weapon_picked = WEAPON_NONE;

WeaponType_t drops_get_last_weapon_picked(void) { return last_weapon_picked; }
void drops_clear_last_weapon_picked(void) { last_weapon_picked = WEAPON_NONE; }

void drops_init(void)
{
  for (int i = 0; i < MAX_DROPS; i++) {
    drops[i].active = 0;
  }
  last_weapon_picked = WEAPON_NONE;

  if (a_AudioLoadSound("resources/soundEffects/heal.wav", &heal_sound) == 0) {
    heal_sound_loaded = 1;
  }
}

int drops_has_type(WeaponType_t type)
{
  for (int i = 0; i < MAX_DROPS; i++) {
    if (drops[i].active && drops[i].type == type) return 1;
  }
  return 0;
}

int drops_find_nearest_health(float x, float y, float search_radius, float* out_x, float* out_y)
{
  float best_dist = search_radius + 1.0f;
  int found = 0;
  for (int i = 0; i < MAX_DROPS; i++) {
    if (!drops[i].active || drops[i].type != DROP_HEALTH) continue;
    float dx = drops[i].x - x;
    float dy = drops[i].y - y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= search_radius && dist < best_dist) {
      best_dist = dist;
      *out_x = drops[i].x;
      *out_y = drops[i].y;
      found = 1;
    }
  }
  return found;
}

void drops_consume_nearest_health(float x, float y, float radius)
{
  float best_dist = radius + 1.0f;
  int best_idx = -1;
  for (int i = 0; i < MAX_DROPS; i++) {
    if (!drops[i].active || drops[i].type != DROP_HEALTH) continue;
    float dx = drops[i].x - x;
    float dy = drops[i].y - y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= radius && dist < best_dist) {
      best_dist = dist;
      best_idx = i;
    }
  }
  if (best_idx >= 0) {
    drops[best_idx].active = 0;
  }
}

void drops_spawn(float x, float y, WeaponType_t type)
{
  for (int i = 0; i < MAX_DROPS; i++) {
    if (!drops[i].active) {
      drops[i] = (Drop_t){
        .x = x, .y = y,
        .type = type,
        .active = 1,
        .age = 0.0f
      };
      return;
    }
  }
}

void drops_update(float dt)
{
  float px = player_get_x();
  float py = player_get_y();

  for (int i = 0; i < MAX_DROPS; i++) {
    if (!drops[i].active) continue;

    drops[i].age += dt;

    float dx = (drops[i].x + DROP_SIZE / 2) - px;
    float dy = (drops[i].y + DROP_SIZE / 2) - py;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < PICKUP_RADIUS && player_is_alive()) {
      if (drops[i].type == DROP_HEALTH) {
        player_heal(10 + global_get_health_pickup_bonus());
        stats_add_score(2);
        drops[i].active = 0;
        if (heal_sound_loaded) {
          aAudioOptions_t opts = {
            .channel = AUDIO_CHANNEL_AUTO,
            .volume = 80,
            .loops = 0,
            .fade_ms = 0,
            .interrupt = 0
          };
          a_AudioPlaySound(&heal_sound, &opts);
        }
      } else {
        int slot = weapons_add(drops[i].type);
        if (slot >= 0) {
          hotbar_add_slot();
          last_weapon_picked = drops[i].type;
          drops[i].active = 0;
        }
      }
    }
  }
}

/** Draw a filled heart shape centered at (cx, cy) with given half-size */
static void draw_heart(int cx, int cy, int size, aColor_t color)
{
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);

  // Two filled circles for the top bumps
  int r = size / 2;
  // Left bump center
  int lx = cx - r;
  int ly = cy;
  // Right bump center
  int rx = cx + r;
  int ry = cy;

  // Scanline circles for left and right bumps
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    SDL_Rect left_row  = { lx - dx, ly + dy, dx * 2 + 1, 1 };
    SDL_Rect right_row = { rx - dx, ry + dy, dx * 2 + 1, 1 };
    SDL_RenderFillRect(app.renderer, &left_row);
    SDL_RenderFillRect(app.renderer, &right_row);
  }

  // Triangle: from circle centers down to a point
  int tri_top = cy;
  int tri_bot = cy + size + r;
  int tri_half_w = 2 * r;

  for (int y = tri_top; y <= tri_bot; y++) {
    float t = (float)(y - tri_top) / (float)(tri_bot - tri_top);
    int half_w = (int)(tri_half_w * (1.0f - t));
    SDL_Rect tri_row = { cx - half_w, y, half_w * 2 + 1, 1 };
    SDL_RenderFillRect(app.renderer, &tri_row);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

static void draw_arrow_down(int cx, int cy, aColor_t color)
{
  // Arrow shaft (vertical line)
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(app.renderer, cx, cy - 12, cx, cy);

  // Arrow head (V shape)
  SDL_RenderDrawLine(app.renderer, cx - 5, cy - 5, cx, cy);
  SDL_RenderDrawLine(app.renderer, cx + 5, cy - 5, cx, cy);
  // Thicken it
  SDL_RenderDrawLine(app.renderer, cx - 1, cy - 12, cx - 1, cy);
  SDL_RenderDrawLine(app.renderer, cx + 1, cy - 12, cx + 1, cy);
  SDL_RenderDrawLine(app.renderer, cx - 5, cy - 6, cx, cy - 1);
  SDL_RenderDrawLine(app.renderer, cx + 5, cy - 6, cx, cy - 1);

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

void drops_draw(void)
{
  for (int i = 0; i < MAX_DROPS; i++) {
    if (!drops[i].active) continue;

    float bob = sinf(drops[i].age * DROP_BOB_SPEED) * DROP_BOB_AMP;
    float glow_alpha = 0.5f + 0.5f * sinf(drops[i].age * GLOW_PULSE_SPEED);
    int glow_a = (int)(120.0f * glow_alpha);

    aColor_t color;
    aColor_t glow_color;
    const char* label;
    int is_health = (drops[i].type == DROP_HEALTH);
    switch (drops[i].type) {
      case WEAPON_SPIN:
        color = (aColor_t){255, 200, 50, 255};
        glow_color = (aColor_t){255, 200, 50, (uint8_t)glow_a};
        label = "SPIN";
        break;
      case WEAPON_WAND:
        color = (aColor_t){100, 150, 255, 255};
        glow_color = (aColor_t){100, 150, 255, (uint8_t)glow_a};
        label = "WAND";
        break;
      case WEAPON_CHAIN:
        color = (aColor_t){200, 220, 255, 255};
        glow_color = (aColor_t){200, 220, 255, (uint8_t)glow_a};
        label = "BOLT";
        break;
      case WEAPON_ORBIT:
        color = (aColor_t){255, 160, 50, 255};
        glow_color = (aColor_t){255, 160, 50, (uint8_t)glow_a};
        label = "ORB";
        break;
      case WEAPON_BOMB:
        color = (aColor_t){255, 180, 50, 255};
        glow_color = (aColor_t){255, 180, 50, (uint8_t)glow_a};
        label = "BOMB";
        break;
      case WEAPON_TURRET:
        color = (aColor_t){255, 180, 50, 255};
        glow_color = (aColor_t){255, 180, 50, (uint8_t)glow_a};
        label = "TRRT";
        break;
      case WEAPON_TRAIL:
        color = (aColor_t){255, 120, 30, 255};
        glow_color = (aColor_t){255, 120, 30, (uint8_t)glow_a};
        label = "FIRE";
        break;
      case WEAPON_SCYTHE:
        color = (aColor_t){180, 255, 180, 255};
        glow_color = (aColor_t){180, 255, 180, (uint8_t)glow_a};
        label = "SCTH";
        break;
      case DROP_HEALTH:
        color = (aColor_t){220, 30, 30, 255};
        glow_color = (aColor_t){220, 30, 30, (uint8_t)glow_a};
        label = "";
        break;
      default:
        color = (aColor_t){200, 200, 200, 255};
        glow_color = (aColor_t){200, 200, 200, (uint8_t)glow_a};
        label = "?";
        break;
    }

    float draw_x = drops[i].x - DROP_SIZE / 2;
    float draw_y = drops[i].y - DROP_SIZE / 2 + bob;

    // Neon glow — concentric layers radiating outward
    float neon_pulse = 0.7f + 0.3f * sinf(drops[i].age * GLOW_PULSE_SPEED);

    aRectf_t r;
    if (is_health) {
      int hcx = (int)(drops[i].x);
      int hcy = (int)(drops[i].y + bob);
      // Heart-shaped neon glow
      for (int g = 3; g >= 1; g--) {
        int na = (int)(25.0f * neon_pulse / (float)g);
        aColor_t nc = { color.r, color.g, color.b, (uint8_t)na };
        draw_heart(hcx, hcy, 6 + g * 5, nc);
      }
      draw_heart(hcx, hcy, 9, glow_color);  // glow heart behind
      draw_heart(hcx, hcy, 7, color);        // solid heart
      r = (aRectf_t){ draw_x, draw_y, DROP_SIZE, DROP_SIZE };
    } else {
      // Rect neon glow for weapons
      for (int g = 3; g >= 1; g--) {
        float pad = (float)g * 6.0f;
        int na = (int)(25.0f * neon_pulse / (float)g);
        aColor_t nc = { color.r, color.g, color.b, (uint8_t)na };
        a_DrawFilledRect(
          (aRectf_t){ draw_x - pad, draw_y - pad, DROP_SIZE + pad * 2, DROP_SIZE + pad * 2 },
          nc
        );
      }
      // Pulsing glow behind the drop (larger rect)
      aRectf_t glow_rect = { draw_x - 4, draw_y - 4, DROP_SIZE + 8, DROP_SIZE + 8 };
      a_DrawFilledRect(glow_rect, glow_color);

      // Drop icon
      r = (aRectf_t){ draw_x, draw_y, DROP_SIZE, DROP_SIZE };
      a_DrawFilledRect(r, color);
      a_DrawRect(r, (aColor_t){255, 255, 255, 220});

      // Label on the drop
      aTextStyle_t icon_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {0, 0, 0, 255},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.45f
      };
      a_DrawText(label, (int)(r.x + DROP_SIZE / 2), (int)(r.y + 5), icon_style);
    }

    // Bouncing arrow above the drop (green for health, red for weapons)
    float arrow_bob = sinf(drops[i].age * ARROW_BOB_SPEED) * ARROW_BOB_AMP;
    int arrow_x = (int)(drops[i].x);
    int arrow_y = (int)(draw_y - 22 + arrow_bob);
    aColor_t arrow_color = is_health ? (aColor_t){40, 220, 40, 255} : (aColor_t){255, 40, 40, 255};
    draw_arrow_down(arrow_x, arrow_y, arrow_color);

    // "PICK UP" / "HEAL" text well above the arrow
    aTextStyle_t pickup_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = arrow_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText(is_health ? "HEAL" : "WEAPON", arrow_x, arrow_y - 28, pickup_style);
  }
}
