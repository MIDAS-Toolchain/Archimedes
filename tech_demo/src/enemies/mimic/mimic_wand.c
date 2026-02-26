#include "mimic_internal.h"
#include "upgrades.h"
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Wand Projectile Pool
// ============================================================================

typedef struct {
  float x, y, vx, vy;
  int active;
  int pierces_left;
} MimicWandBullet_t;

static MimicWandBullet_t bullets[MIMIC_WAND_MAX_BULLETS];
static aImage_t* bullet_img = NULL;

// Barrage burst state
static int   mm_shot_count = 0;
static int   mm_burst_remaining = 0;
static float mm_burst_timer = 0.0f;
static float mm_burst_cx, mm_burst_cy;   // stored aim origin for burst shots
static float mm_burst_px, mm_burst_py;   // stored player pos for burst shots
static float mm_burst_pvx, mm_burst_pvy; // stored player vel for burst shots

// Telegraph state
#define MIMIC_WAND_TELEGRAPH_TIME 0.3f
static int   mm_wand_telegraphing;
static float mm_wand_telegraph_timer;
static float mm_wand_tele_px, mm_wand_tele_py;
static float mm_wand_tele_pvx, mm_wand_tele_pvy;

// ============================================================================
// Init (load bullet image — same as player's)
// ============================================================================

void mimic_wand_init(void)
{
  if (!bullet_img) {
    bullet_img = a_ImageLoad("resources/assets/bullet.png");
  }
}

// ============================================================================
// Fire
// ============================================================================

static void fire_bullet(float ox, float oy, float dx, float dy)
{
  for (int i = 0; i < MIMIC_WAND_MAX_BULLETS; i++) {
    if (bullets[i].active) continue;
    bullets[i].active = 1;
    bullets[i].x = ox;
    bullets[i].y = oy;
    // Match player bullet speed exactly: 600 * speed_mult
    float speed = 600.0f * weapons_get_wand_bullet_speed_mult();
    float mag = sqrtf(dx * dx + dy * dy);
    if (mag < 0.1f) { dx = 0; dy = -1; mag = 1; }
    bullets[i].vx = (dx / mag) * speed;
    bullets[i].vy = (dy / mag) * speed;
    bullets[i].pierces_left = upgrades_get_tier(UPG_WAND_PIERCE);
    return;
  }
}

static void mimic_fire_volley(float cx, float cy,
                              float px, float py, float pvx, float pvy)
{
  float bullet_speed = 600.0f * weapons_get_wand_bullet_speed_mult();
  float raw_dx = px - cx;
  float raw_dy = py - cy;
  float raw_dist = sqrtf(raw_dx * raw_dx + raw_dy * raw_dy);
  float travel_time = (bullet_speed > 1.0f) ? raw_dist / bullet_speed : 0.0f;

  // Multishot (matches player upgrade tiers)
  static const int multishot_count[] = {1, 2, 3, 5};
  int tier = upgrades_get_tier(UPG_WAND_MULTISHOT);
  if (tier < 0) tier = 0;
  if (tier > 3) tier = 3;
  int count = multishot_count[tier];

  if (count <= 1) {
    // Single shot: full lead prediction
    float lead_x = px + pvx * travel_time;
    float lead_y = py + pvy * travel_time;
    fire_bullet(cx, cy, lead_x - cx, lead_y - cy);
  } else {
    // Multi-shot: spread bullets between direct aim and lead aim
    // This creates a "net" that's harder to dodge
    float direct_angle = atan2f(raw_dy, raw_dx);
    float lead_x = px + pvx * travel_time;
    float lead_y = py + pvy * travel_time;
    float lead_angle = atan2f(lead_y - cy, lead_x - cx);

    // Ensure angular difference is minimal (no wrap-around)
    float diff = lead_angle - direct_angle;
    while (diff > PI) diff -= 2.0f * (float)PI;
    while (diff < -PI) diff += 2.0f * (float)PI;

    // Center the fan between direct and lead, widen the spread
    float center_angle = direct_angle + diff * 0.5f;
    float half_spread = fabsf(diff) * 0.5f;
    // Minimum spread so fan is always visible
    float min_spread = 0.12f * (float)(count - 1);
    if (half_spread < min_spread) half_spread = min_spread;

    for (int i = 0; i < count; i++) {
      float t = (float)i / (float)(count - 1);
      float angle = center_angle - half_spread + t * half_spread * 2.0f;
      fire_bullet(cx, cy, cosf(angle), sinf(angle));
    }
  }
}

static void mimic_fire_wand(Enemy_t* e, float cx, float cy,
                             float px, float py, float pvx, float pvy)
{
  (void)e;
  mimic_fire_volley(cx, cy, px, py, pvx, pvy);

  // Barrage: check if this shot triggers a burst (matches player upgrade)
  int tier = upgrades_get_tier(UPG_WAND_BARRAGE);
  if (tier <= 0) return;

  mm_shot_count++;
  static const int every_n[4] = { 0, 3, 2, 1 };
  static const int burst[4]   = { 0, 2, 2, 4 };
  if (mm_shot_count % every_n[tier] == 0) {
    mm_burst_remaining = burst[tier] - 1;
    mm_burst_timer = 0.08f;
    mm_burst_cx = cx;
    mm_burst_cy = cy;
    mm_burst_px = px;
    mm_burst_py = py;
    mm_burst_pvx = pvx;
    mm_burst_pvy = pvy;
  }
}

// ============================================================================
// Update
// ============================================================================

void mimic_wand_update(Enemy_t* e, int index, float dt,
                       float px, float py, float pvx, float pvy)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  // --- Movement: beholder-style strafe at preferred range ---
  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  float steer_x = 0.0f, steer_y = 0.0f;

  // Radial spring: strong push toward preferred range (beholder-style)
  if (dist > MIMIC_RANGE_FAR + 30.0f) {
    steer_x += -ndx * 0.5f;
    steer_y += -ndy * 0.5f;
  } else if (dist < MIMIC_RANGE_FAR - 30.0f) {
    steer_x += ndx * 0.8f;
    steer_y += ndy * 0.8f;
  }

  // Perpendicular strafe (orbit around player)
  float perp_x = -ndy;
  float perp_y =  ndx;
  steer_x += perp_x;
  steer_y += perp_y;

  // Separation from other enemies
  float sep_x, sep_y;
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);
  steer_x += sep_x;
  steer_y += sep_y;

  // Screen edge repulsion (beholder-style smooth gradient)
  float margin = 30.0f;
  if (cx < margin)                  steer_x += (margin - cx) / margin;
  if (cx > SCREEN_WIDTH - margin)   steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
  if (cy < margin)                  steer_y += (margin - cy) / margin;
  if (cy > SCREEN_HEIGHT - margin)  steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

  float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
  if (slen > 0.1f) {
    e->vx = (steer_x / slen) * MIMIC_SPEED_STRAFE;
    e->vy = (steer_y / slen) * MIMIC_SPEED_STRAFE;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  e->x += e->vx * dt;
  e->y += e->vy * dt;

  // Clamp to screen
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - stats->size) e->x = SCREEN_WIDTH - stats->size;
  if (e->y > SCREEN_HEIGHT - stats->size) e->y = SCREEN_HEIGHT - stats->size;

  // --- Attack: telegraph then fire ---
  if (e->mm_weapon_cooldown <= 0.0f && !mm_wand_telegraphing) {
    mm_wand_tele_px = px;
    mm_wand_tele_py = py;
    mm_wand_tele_pvx = pvx;
    mm_wand_tele_pvy = pvy;
    mm_wand_telegraphing = 1;
    mm_wand_telegraph_timer = MIMIC_WAND_TELEGRAPH_TIME;
  }

  if (mm_wand_telegraphing) {
    mm_wand_telegraph_timer -= dt;
    if (mm_wand_telegraph_timer <= 0.0f) {
      mm_wand_telegraphing = 0;
      cx = e->x + stats->size / 2.0f;
      cy = e->y + stats->size / 2.0f;
      mimic_fire_wand(e, cx, cy, mm_wand_tele_px, mm_wand_tele_py,
                      mm_wand_tele_pvx, mm_wand_tele_pvy);
      e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);
    }
  }

  // --- Barrage burst timer ---
  if (mm_burst_remaining > 0) {
    mm_burst_timer -= dt;
    if (mm_burst_timer <= 0.0f) {
      // Use current mimic position for burst shots
      cx = e->x + stats->size / 2.0f;
      cy = e->y + stats->size / 2.0f;
      mimic_fire_volley(cx, cy, mm_burst_px, mm_burst_py,
                        mm_burst_pvx, mm_burst_pvy);
      mm_burst_remaining--;
      mm_burst_timer = 0.08f;
    }
  }

  // --- Update bullets ---
  for (int i = 0; i < MIMIC_WAND_MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;
    bullets[i].x += bullets[i].vx * dt;
    bullets[i].y += bullets[i].vy * dt;

    // Off-screen check
    if (bullets[i].x < -20 || bullets[i].x > SCREEN_WIDTH + 20 ||
        bullets[i].y < -20 || bullets[i].y > SCREEN_HEIGHT + 20) {
      bullets[i].active = 0;
      continue;
    }

    // Player collision
    float bdx = bullets[i].x - px;
    float bdy = bullets[i].y - py;
    if (bdx * bdx + bdy * bdy < MIMIC_PLAYER_RADIUS * MIMIC_PLAYER_RADIUS) {
      player_take_damage(MIMIC_CONTACT_DAMAGE, bullets[i].x, bullets[i].y, ENEMY_TYPE_MIMIC);
      if (bullets[i].pierces_left > 0) {
        bullets[i].pierces_left--;
      } else {
        bullets[i].active = 0;
      }
    }
  }
}

// ============================================================================
// Draw — uses same bullet image as player at same scale
// ============================================================================

static void draw_dashed_line(float x1, float y1, float x2, float y2,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  float ux = dx / len;
  float uy = dy / len;

  float dash = 8.0f, gap = 6.0f;
  float pos = 0.0f;
  SDL_SetRenderDrawColor(app.renderer, r, g, b, a);
  while (pos < len) {
    float end = pos + dash;
    if (end > len) end = len;
    SDL_RenderDrawLine(app.renderer,
      (int)(x1 + ux * pos), (int)(y1 + uy * pos),
      (int)(x1 + ux * end), (int)(y1 + uy * end));
    pos = end + gap;
  }
}

void mimic_wand_draw(Enemy_t* e)
{
  // Telegraph — dashed white lines showing aim direction(s)
  if (mm_wand_telegraphing && e) {
    const EnemyStats_t* stats = ei_get_stats(e);
    float cx = e->x + stats->size / 2.0f;
    float cy = e->y + stats->size / 2.0f;

    float pct = mm_wand_telegraph_timer / MIMIC_WAND_TELEGRAPH_TIME;
    float flicker_rate = 8.0f + (1.0f - pct) * 24.0f;
    float flicker = sinf(mm_wand_telegraph_timer * flicker_rate * PI);
    int alpha = (int)(80.0f + 100.0f * (0.5f + 0.5f * flicker));
    if (alpha > 255) alpha = 255;

    // Compute aim angles (same logic as mimic_fire_volley)
    float bullet_speed = 600.0f * weapons_get_wand_bullet_speed_mult();
    float raw_dx = mm_wand_tele_px - cx;
    float raw_dy = mm_wand_tele_py - cy;
    float raw_dist = sqrtf(raw_dx * raw_dx + raw_dy * raw_dy);
    float travel_time = (bullet_speed > 1.0f) ? raw_dist / bullet_speed : 0.0f;

    static const int multishot_count[] = {1, 2, 3, 5};
    int tier = upgrades_get_tier(UPG_WAND_MULTISHOT);
    if (tier < 0) tier = 0;
    if (tier > 3) tier = 3;
    int count = multishot_count[tier];

    float line_len = 300.0f;
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    if (count <= 1) {
      float lead_x = mm_wand_tele_px + mm_wand_tele_pvx * travel_time;
      float lead_y = mm_wand_tele_py + mm_wand_tele_pvy * travel_time;
      float ang = atan2f(lead_y - cy, lead_x - cx);
      draw_dashed_line(cx, cy, cx + cosf(ang) * line_len,
                       cy + sinf(ang) * line_len,
                       255, 255, 255, (uint8_t)alpha);
    } else {
      float direct_angle = atan2f(raw_dy, raw_dx);
      float lead_x = mm_wand_tele_px + mm_wand_tele_pvx * travel_time;
      float lead_y = mm_wand_tele_py + mm_wand_tele_pvy * travel_time;
      float lead_angle = atan2f(lead_y - cy, lead_x - cx);
      float diff = lead_angle - direct_angle;
      while (diff > PI) diff -= 2.0f * PI;
      while (diff < -PI) diff += 2.0f * PI;
      float center_angle = direct_angle + diff * 0.5f;
      float half_spread = fabsf(diff) * 0.5f;
      float min_spread = 0.12f * (float)(count - 1);
      if (half_spread < min_spread) half_spread = min_spread;

      for (int i = 0; i < count; i++) {
        float t = (float)i / (float)(count - 1);
        float ang = center_angle - half_spread + t * half_spread * 2.0f;
        draw_dashed_line(cx, cy, cx + cosf(ang) * line_len,
                         cy + sinf(ang) * line_len,
                         255, 255, 255, (uint8_t)alpha);
      }
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  if (bullet_img != NULL && bullet_img->surface != NULL) {
    float img_w = (float)bullet_img->surface->w;
    float img_h = (float)bullet_img->surface->h;
    float proj_scale = 0.25f * weapons_get_wand_proj_size_mult();
    float scaled_w = img_w * proj_scale;
    float scaled_h = img_h * proj_scale;

    for (int i = 0; i < MIMIC_WAND_MAX_BULLETS; i++) {
      if (!bullets[i].active) continue;
      aRectf_t src = {0, 0, img_w, img_h};
      aRectf_t dest = {
        bullets[i].x - scaled_w / 2.0f,
        bullets[i].y - scaled_h / 2.0f,
        scaled_w, scaled_h
      };
      a_BlitRect(bullet_img, &src, &dest, 1);
    }
  } else {
    // Fallback: colored squares if image failed to load
    aColor_t bullet_color = {255, 255, 100, 220};
    for (int i = 0; i < MIMIC_WAND_MAX_BULLETS; i++) {
      if (!bullets[i].active) continue;
      a_DrawFilledRect(
        (aRectf_t){bullets[i].x - 3, bullets[i].y - 3, 6, 6},
        bullet_color
      );
    }
  }
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_wand_cleanup(void)
{
  memset(bullets, 0, sizeof(bullets));
  mm_shot_count = 0;
  mm_burst_remaining = 0;
  mm_burst_timer = 0.0f;
  mm_wand_telegraphing = 0;
  mm_wand_telegraph_timer = 0.0f;
}
