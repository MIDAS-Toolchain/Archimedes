#include <math.h>
#include <stdlib.h>
#include "Archimedes.h"
#include "pickups.h"
#include "player_actions.h"
#include "enemy.h"

#define MAX_PICKUPS       30
#define PICKUP_LIFETIME   10.0f
#define PICKUP_BLINK_AT   7.0f    // Start blinking at 7s age (3s remaining)
#define PICKUP_BLINK_RATE 0.15f
#define PICKUP_COLLECT_DIST 34.0f
#define PICKUP_DIAMOND_SIZE 14
#define PICKUP_BOB_SPEED  3.0f
#define PICKUP_BOB_AMP    2.0f

// Pity timer
#define PITY_TIMER_DEFAULT   15.0f
#define PITY_TIMER_MAX       30.0f
#define PITY_KILL_NO_DROP    1.0f   // Subtract 1s per no-drop kill
#define PITY_KILL_WITH_DROP  5.0f   // Add 5s when a drop occurs
#define PITY_SPAWN_MIN_DIST  50.0f
#define PITY_SPAWN_MAX_DIST 150.0f
#define PICKUP_MIN_PLAYER_DIST 60.0f
#define PICKUP_MIN_BRUTE_DIST  60.0f

static Pickup_t pickups[MAX_PICKUPS];
static float pity_timer = PITY_TIMER_DEFAULT;

void pickups_init(void)
{
  for (int i = 0; i < MAX_PICKUPS; i++) {
    pickups[i].active = 0;
  }
  pity_timer = PITY_TIMER_DEFAULT;
}

void pickups_cleanup(void)
{
  for (int i = 0; i < MAX_PICKUPS; i++) {
    pickups[i].active = 0;
  }
  pity_timer = PITY_TIMER_DEFAULT;
}

static int pickup_spot_valid(float x, float y, float px, float py)
{
  float dx = x - px;
  float dy = y - py;
  if (sqrtf(dx * dx + dy * dy) < PICKUP_MIN_PLAYER_DIST) return 0;
  int max_e = enemy_get_max_count();
  for (int i = 0; i < max_e; i++) {
    if (!enemy_is_alive(i)) continue;
    if (enemy_get_type(i) != ENEMY_TYPE_BRUTE) continue;
    float ex, ey;
    enemy_get_position(i, &ex, &ey);
    float er = enemy_get_radius(i);
    dx = x - (ex + er);
    dy = y - (ey + er);
    if (sqrtf(dx * dx + dy * dy) < PICKUP_MIN_BRUTE_DIST) return 0;
  }
  return 1;
}

static void pickup_find_safe_spot(float* out_x, float* out_y)
{
  float px = player_get_x() + 16.0f;
  float py = player_get_y() + 16.0f;
  float best_x = *out_x, best_y = *out_y;
  float best_score = 99999.0f;
  int found = 0;

  for (int attempt = 0; attempt < 20; attempt++) {
    float angle = RANDF(0, 2.0f * (float)PI);
    float dist = RANDF(PICKUP_MIN_PLAYER_DIST, PICKUP_MIN_PLAYER_DIST + 120.0f);
    float cx = px + cosf(angle) * dist;
    float cy = py + sinf(angle) * dist;
    if (cx < 20.0f) cx = 20.0f;
    if (cy < 20.0f) cy = 20.0f;
    if (cx > (float)SCREEN_WIDTH - 20.0f) cx = (float)SCREEN_WIDTH - 20.0f;
    if (cy > (float)SCREEN_HEIGHT - 20.0f) cy = (float)SCREEN_HEIGHT - 20.0f;

    if (!pickup_spot_valid(cx, cy, px, py)) continue;

    // Prefer spots closer to player
    if (dist < best_score) {
      best_score = dist;
      best_x = cx;
      best_y = cy;
      found = 1;
    }
  }

  if (!found) {
    // Fallback: just push away from player in random direction
    float angle = RANDF(0, 2.0f * (float)PI);
    best_x = px + cosf(angle) * PICKUP_MIN_PLAYER_DIST;
    best_y = py + sinf(angle) * PICKUP_MIN_PLAYER_DIST;
    if (best_x < 20.0f) best_x = 20.0f;
    if (best_y < 20.0f) best_y = 20.0f;
    if (best_x > (float)SCREEN_WIDTH - 20.0f) best_x = (float)SCREEN_WIDTH - 20.0f;
    if (best_y > (float)SCREEN_HEIGHT - 20.0f) best_y = (float)SCREEN_HEIGHT - 20.0f;
  }

  *out_x = best_x;
  *out_y = best_y;
}

void pickups_spawn(float x, float y, PickupType_t type)
{
  float px = player_get_x() + 16.0f;
  float py = player_get_y() + 16.0f;
  if (!pickup_spot_valid(x, y, px, py)) {
    pickup_find_safe_spot(&x, &y);
  }

  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) {
      pickups[i] = (Pickup_t){
        .x = x, .y = y,
        .type = type,
        .lifetime = PICKUP_LIFETIME,
        .active = 1,
        .age = 0.0f
      };
      return;
    }
  }
}

void pickups_spawn_random(float x, float y)
{
  PickupType_t type = (PickupType_t)(rand() % PICKUP_TYPE_COUNT);
  pickups_spawn(x, y, type);
}

void pickups_update(float dt)
{
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) continue;

    pickups[i].age += dt;
  }
}

// Get color for a pickup type
static void pickup_color(PickupType_t type, aColor_t* out)
{
  switch (type) {
    case PICKUP_FIRE_CONE: *out = (aColor_t){255, 130, 30, 255};  break;
    case PICKUP_SPEED:     *out = (aColor_t){255, 230, 50, 255};  break;
    case PICKUP_SHIELD:    *out = (aColor_t){50, 150, 255, 255};  break;
    case PICKUP_SLOW_AURA: *out = (aColor_t){100, 200, 255, 255}; break;
    default:               *out = (aColor_t){200, 200, 200, 255}; break;
  }
}

static const char* pickup_label(PickupType_t type)
{
  switch (type) {
    case PICKUP_FIRE_CONE: return "F";
    case PICKUP_SPEED:     return "Z";
    case PICKUP_SHIELD:    return "S";
    case PICKUP_SLOW_AURA: return "C";
    default:               return "?";
  }
}

// Draw a filled diamond (rotated square) centered at (cx, cy) with half-size
static void draw_diamond(int cx, int cy, int half, aColor_t color)
{
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, color.r, color.g, color.b, color.a);

  for (int dy = -half; dy <= half; dy++) {
    int width = half - abs(dy);
    SDL_Rect row = { cx - width, cy + dy, width * 2 + 1, 1 };
    SDL_RenderFillRect(app.renderer, &row);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

void pickups_draw(void)
{
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) continue;

    // Blink when 3s or less remaining
    if (pickups[i].lifetime <= (PICKUP_LIFETIME - PICKUP_BLINK_AT)) {
      int blink_phase = (int)(pickups[i].age / PICKUP_BLINK_RATE);
      if (blink_phase % 2 == 1) continue; // skip drawing on odd phases
    }

    float bob = sinf(pickups[i].age * PICKUP_BOB_SPEED) * PICKUP_BOB_AMP;
    int cx = (int)pickups[i].x;
    int cy = (int)(pickups[i].y + bob);

    aColor_t color;
    pickup_color(pickups[i].type, &color);

    // Neon glow — concentric diamond layers radiating outward
    float neon_pulse = 0.7f + 0.3f * sinf(pickups[i].age * PICKUP_BOB_SPEED);
    for (int g = 3; g >= 1; g--) {
      int pad = g * 6;
      int na = (int)(25.0f * neon_pulse / (float)g);
      aColor_t nc = { color.r, color.g, color.b, (uint8_t)na };
      draw_diamond(cx, cy, PICKUP_DIAMOND_SIZE + pad, nc);
    }

    // Glow diamond (larger, translucent)
    aColor_t glow = color;
    glow.a = 60;
    draw_diamond(cx, cy, PICKUP_DIAMOND_SIZE + 4, glow);

    // Outline diamond (white border)
    draw_diamond(cx, cy, PICKUP_DIAMOND_SIZE + 1, (aColor_t){255, 255, 255, 180});

    // Solid diamond
    draw_diamond(cx, cy, PICKUP_DIAMOND_SIZE, color);

    // Letter label — black outline for readability, then white on top
    const char* label = pickup_label(pickups[i].type);
    aTextStyle_t shadow_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {0, 0, 0, 220},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    aTextStyle_t label_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.55f
    };
    // Shadow outline (4 directions)
    a_DrawText(label, cx - 1, cy - 10, shadow_style);
    a_DrawText(label, cx + 1, cy - 10, shadow_style);
    a_DrawText(label, cx, cy - 11, shadow_style);
    a_DrawText(label, cx, cy - 9, shadow_style);
    // White label
    a_DrawText(label, cx, cy - 10, label_style);

  }
}

int pickups_find_nearest(float x, float y, float radius,
                         PickupType_t* out_type, float* out_x, float* out_y)
{
  float best_dist = radius + 1.0f;
  int found = 0;
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) continue;
    float dx = pickups[i].x - x;
    float dy = pickups[i].y - y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= radius && dist < best_dist) {
      best_dist = dist;
      *out_type = pickups[i].type;
      *out_x = pickups[i].x;
      *out_y = pickups[i].y;
      found = 1;
    }
  }
  return found;
}

int pickups_consume_nearest(float x, float y, float radius)
{
  float best_dist = radius + 1.0f;
  int best_idx = -1;
  for (int i = 0; i < MAX_PICKUPS; i++) {
    if (!pickups[i].active) continue;
    float dx = pickups[i].x - x;
    float dy = pickups[i].y - y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= radius && dist < best_dist) {
      best_dist = dist;
      best_idx = i;
    }
  }
  if (best_idx >= 0) {
    PickupType_t type = pickups[best_idx].type;
    pickups[best_idx].active = 0;
    return (int)type;
  }
  return -1;
}

void pickups_notify_kill(int dropped)
{
  if (dropped) {
    pity_timer += PITY_KILL_WITH_DROP;
    if (pity_timer > PITY_TIMER_MAX) pity_timer = PITY_TIMER_MAX;
  } else {
    pity_timer -= PITY_KILL_NO_DROP;
  }
}

void pickups_check_pity(float dt, float player_x, float player_y)
{
  pity_timer -= dt;
  if (pity_timer <= 0.0f) {
    // Spawn random pickup 50-150px from player
    float angle = RANDF(0, 2.0f * (float)PI);
    float dist = RANDF(PITY_SPAWN_MIN_DIST, PITY_SPAWN_MAX_DIST);
    float sx = player_x + cosf(angle) * dist;
    float sy = player_y + sinf(angle) * dist;

    // Clamp to screen
    if (sx < 20.0f) sx = 20.0f;
    if (sy < 20.0f) sy = 20.0f;
    if (sx > (float)SCREEN_WIDTH - 20.0f) sx = (float)SCREEN_WIDTH - 20.0f;
    if (sy > (float)SCREEN_HEIGHT - 20.0f) sy = (float)SCREEN_HEIGHT - 20.0f;

    pickups_spawn_random(sx, sy);
    pity_timer = PITY_TIMER_DEFAULT;
  }
}

float pickups_get_collect_dist(void)
{
  return PICKUP_COLLECT_DIST;
}
