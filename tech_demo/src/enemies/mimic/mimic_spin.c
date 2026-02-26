#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include <stdlib.h>
#include <math.h>

// ============================================================================
// Spin AoE Attack — matches player spin (radius, upgrades, visuals)
// ============================================================================

// Sub-states for spin AI movement (strafe → dash-in → retreat cycle)
enum {
  SPIN_AI_STRAFE,       // Circle player at range, waiting for cooldown
  SPIN_AI_DASH_IN,      // Predict and rush toward player to fire
  SPIN_AI_RETREAT,      // Brief pullback after firing
};

#define MM_SPIN_STRAFE_PAD      30.0f
#define MM_SPIN_DASH_SPEED      280.0f
#define MM_SPIN_DASH_TIMEOUT    1.2f
#define MM_SPIN_PREDICT_TIME    0.4f
#define MM_SPIN_PREDICT_LEAD    40.0f

// Hostile linger zones (damages player)
#define MIMIC_LINGER_MAX 4
#define MIMIC_LINGER_TICK 0.3f
typedef struct {
  float x, y, radius, lifetime, max_lifetime, tick_timer;
  int active;
} MimicLingerZone_t;
static MimicLingerZone_t mm_linger[MIMIC_LINGER_MAX];

// Hostile aftershock wave
typedef struct {
  float x, y, dx, dy;
  float dist, max_dist, speed, width;
  int active;
} MimicAfterShock_t;
static MimicAfterShock_t mm_aftershock;
static float mm_aftershock_delay = 0.0f;
static int   mm_aftershock_pending = 0;
static float mm_aftershock_cx, mm_aftershock_cy; // origin

static float get_knockback_mult(void)
{
  return 1.0f + 0.2f * upgrades_get_tier(UPG_SPIN_KNOCKBACK);
}

static int mimic_fire_spin(float cx, float cy, float px, float py)
{
  float radius = weapons_get_spin_radius();
  float dx = px - cx;
  float dy = py - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  int hit = 0;

  if (dist < radius + MIMIC_PLAYER_RADIUS) {
    player_take_damage(MIMIC_CONTACT_DAMAGE, cx, cy, ENEMY_TYPE_MIMIC);
    hit = 1;

    int kb_tier = upgrades_get_tier(UPG_SPIN_KNOCKBACK);
    if (kb_tier > 0) {
      player_apply_knockback(cx, cy, 300.0f * get_knockback_mult());
    }
  }

  int slow_tier = upgrades_get_tier(UPG_SPIN_SLOW);
  if (slow_tier > 0) {
    static const float slow_vals[4] = { 1.0f, 0.80f, 0.70f, 0.60f };
    static const float slow_durs[4] = { 0.0f, 1.5f, 2.0f, 2.5f };
    if (dist < radius + MIMIC_PLAYER_RADIUS) {
      player_apply_slow(slow_vals[slow_tier], slow_durs[slow_tier]);
    }
  }
  return hit;
}

static void mimic_spawn_linger(float cx, float cy, float radius, float duration)
{
  for (int i = 0; i < MIMIC_LINGER_MAX; i++) {
    if (!mm_linger[i].active) {
      mm_linger[i] = (MimicLingerZone_t){
        .x = cx, .y = cy, .radius = radius,
        .lifetime = duration, .max_lifetime = duration,
        .tick_timer = 0.0f, .active = 1
      };
      return;
    }
  }
}

// ============================================================================
// Update
// ============================================================================

static void spin_enter_retreat(Enemy_t* e, float px, float py)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  // Pick a flank point behind and to the side of the mimic (away from player)
  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  // Random angle offset for flanking
  float angle_offset = RANDF(-1.2f, 1.2f);
  float cos_a = cosf(angle_offset);
  float sin_a = sinf(angle_offset);
  float flank_dx = ndx * cos_a - ndy * sin_a;
  float flank_dy = ndx * sin_a + ndy * cos_a;

  float retreat_dist = RANDF(60.0f, 100.0f);
  e->mm_spin_flank_x = cx + flank_dx * retreat_dist;
  e->mm_spin_flank_y = cy + flank_dy * retreat_dist;

  // Clamp to screen
  float margin = 30.0f;
  if (e->mm_spin_flank_x < margin) e->mm_spin_flank_x = margin;
  if (e->mm_spin_flank_x > SCREEN_WIDTH - margin) e->mm_spin_flank_x = SCREEN_WIDTH - margin;
  if (e->mm_spin_flank_y < margin) e->mm_spin_flank_y = margin;
  if (e->mm_spin_flank_y > SCREEN_HEIGHT - margin) e->mm_spin_flank_y = SCREEN_HEIGHT - margin;

  e->mm_spin_ai_state = SPIN_AI_RETREAT;
  e->mm_spin_ai_timer = RANDF(0.4f, 0.6f);
}

void mimic_spin_update(Enemy_t* e, int index, float dt,
                       float px, float py, float pvx, float pvy)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float sep_x, sep_y;
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);

  float sz = stats->size;
  float cx = e->x + sz / 2.0f;
  float cy = e->y + sz / 2.0f;
  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);

  float strafe_range = weapons_get_spin_radius() + MM_SPIN_STRAFE_PAD;

  // --- Movement: strafe → dash-in → fire → retreat cycle ---
  switch (e->mm_spin_ai_state) {
    case SPIN_AI_STRAFE: {
      // Circle player just outside spin radius, waiting for cooldown
      if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
      float ndx = dx / dist;
      float ndy = dy / dist;

      float steer_x = 0.0f, steer_y = 0.0f;

      // Radial: maintain strafe distance
      if (dist > strafe_range + 30.0f) {
        steer_x += -ndx * 0.5f;
        steer_y += -ndy * 0.5f;
      } else if (dist < strafe_range - 50.0f) {
        steer_x += ndx * 0.8f;
        steer_y += ndy * 0.8f;
      }

      // Perpendicular: orbit around player
      float perp_x = -ndy;
      float perp_y =  ndx;
      steer_x += perp_x * 0.6f;
      steer_y += perp_y * 0.6f;

      // Separation + boundary avoidance
      steer_x += sep_x;
      steer_y += sep_y;
      float margin = 30.0f;
      if (cx < margin)                 steer_x += (margin - cx) / margin;
      if (cx > SCREEN_WIDTH - margin)  steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
      if (cy < margin)                 steer_y += (margin - cy) / margin;
      if (cy > SCREEN_HEIGHT - margin) steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

      float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
      if (slen > 0.1f) {
        e->vx = (steer_x / slen) * MIMIC_SPEED_SPIN;
        e->vy = (steer_y / slen) * MIMIC_SPEED_SPIN;
      } else {
        e->vx = 0.0f;
        e->vy = 0.0f;
      }

      e->x += e->vx * dt;
      e->y += e->vy * dt;
      if (e->x < 0) e->x = 0;
      if (e->y < 0) e->y = 0;
      if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
      if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

      // Transition: cooldown expired → predict and dash in
      if (e->mm_weapon_cooldown <= 0.0f) {
        float pspd = sqrtf(pvx * pvx + pvy * pvy);

        if (pspd > 20.0f) {
          float pred_x = px + pvx * MM_SPIN_PREDICT_TIME;
          float pred_y = py + pvy * MM_SPIN_PREDICT_TIME;
          float move_nx = pvx / pspd;
          float move_ny = pvy / pspd;
          pred_x += move_nx * MM_SPIN_PREDICT_LEAD;
          pred_y += move_ny * MM_SPIN_PREDICT_LEAD;

          if (pred_x < 30.0f) pred_x = 30.0f;
          if (pred_x > SCREEN_WIDTH - 30.0f) pred_x = SCREEN_WIDTH - 30.0f;
          if (pred_y < 30.0f) pred_y = 30.0f;
          if (pred_y > SCREEN_HEIGHT - 30.0f) pred_y = SCREEN_HEIGHT - 30.0f;

          e->mm_spin_flank_x = pred_x;
          e->mm_spin_flank_y = pred_y;
        } else {
          // Player stationary: dash directly at them
          e->mm_spin_flank_x = px;
          e->mm_spin_flank_y = py;
        }

        e->mm_spin_ai_state = SPIN_AI_DASH_IN;
        e->mm_spin_ai_timer = MM_SPIN_DASH_TIMEOUT;
      }
      break;
    }

    case SPIN_AI_DASH_IN: {
      // Dash toward predicted player position
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     MM_SPIN_DASH_SPEED, sep_x, sep_y, 10.0f, dt);

      e->mm_spin_ai_timer -= dt;

      // Check if within spin range of player (not predicted pos)
      float spin_r = weapons_get_spin_radius();
      if (dist < spin_r || e->mm_spin_ai_timer <= 0.0f) {
        // Recalculate center after movement for accurate attack
        cx = e->x + sz / 2.0f;
        cy = e->y + sz / 2.0f;

        // --- Fire spin + all upgrade effects ---
        int hit_player = mimic_fire_spin(cx, cy, px, py);
        e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);
        e->mm_spin_pulse_timer = MIMIC_SPIN_PULSE_DURATION;
        game_audio_play_spin_attack();

        // Precision: if mimic missed the player, half cooldown
        int prec_tier = upgrades_get_tier(UPG_SPIN_PRECISION);
        if (prec_tier > 0 && !hit_player) {
          e->mm_weapon_cooldown *= 0.5f;
        }

        // Vacuum: slow player in outer ring
        int vacuum_tier = upgrades_get_tier(UPG_SPIN_VACUUM);
        if (vacuum_tier > 0) {
          float radius = weapons_get_spin_radius();
          static const float vacuum_mult[4] = { 1.0f, 1.3f, 1.5f, 1.8f };
          float outer_r = radius * vacuum_mult[vacuum_tier];
          float vdx = px - cx;
          float vdy = py - cy;
          float vdist = sqrtf(vdx * vdx + vdy * vdy);
          if (vdist > radius && vdist <= outer_r) {
            static const float vac_slow[4] = { 1.0f, 0.80f, 0.70f, 0.60f };
            player_apply_slow(vac_slow[vacuum_tier], 0.2f);
          }
        }

        // Linger zone
        int linger_tier = upgrades_get_tier(UPG_SPIN_LINGER_ZONE);
        if (linger_tier > 0) {
          static const float linger_dur[4] = { 0.0f, 1.2f, 1.8f, 2.4f };
          float radius = weapons_get_spin_radius();
          mimic_spawn_linger(cx, cy, radius * 0.8f, linger_dur[linger_tier]);
        }

        // Aftershock: schedule with delay
        int aftershock_tier = upgrades_get_tier(UPG_SPIN_AFTERSHOCK);
        if (aftershock_tier > 0) {
          mm_aftershock_pending = 1;
          mm_aftershock_delay = 0.3f;
          mm_aftershock_cx = cx;
          mm_aftershock_cy = cy;
        }

        // Always retreat after firing
        spin_enter_retreat(e, px, py);
      }
      break;
    }

    case SPIN_AI_RETREAT: {
      // Brief pullback to a flank point, then back to strafing
      float retreat_speed = MIMIC_SPEED_SPIN * 1.6f;
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     retreat_speed, sep_x, sep_y, 20.0f, dt);

      e->mm_spin_ai_timer -= dt;
      if (e->mm_spin_ai_timer <= 0.0f) {
        e->mm_spin_ai_state = SPIN_AI_STRAFE;
      }
      break;
    }
  }

  // --- Aftershock delay ---
  if (mm_aftershock_pending) {
    mm_aftershock_delay -= dt;
    if (mm_aftershock_delay <= 0.0f) {
      mm_aftershock_pending = 0;
      // Fire aftershock wave toward player
      float adx = px - mm_aftershock_cx;
      float ady = py - mm_aftershock_cy;
      float adist = sqrtf(adx * adx + ady * ady);
      if (adist < 0.1f) { adx = 0; ady = -1; adist = 1; }
      int tier = upgrades_get_tier(UPG_SPIN_AFTERSHOCK);
      static const float as_range[4] = { 0.0f, 135.0f, 198.0f, 270.0f };
      mm_aftershock = (MimicAfterShock_t){
        .x = mm_aftershock_cx, .y = mm_aftershock_cy,
        .dx = adx / adist, .dy = ady / adist,
        .dist = 0.0f, .max_dist = as_range[tier],
        .speed = 450.0f,
        .width = 40.0f,
        .active = 1
      };
    }
  }

  // --- Aftershock wave update ---
  if (mm_aftershock.active) {
    mm_aftershock.dist += mm_aftershock.speed * dt;
    if (mm_aftershock.dist >= mm_aftershock.max_dist) {
      mm_aftershock.active = 0;
    } else {
      // Check if wave hits player
      float wave_x = mm_aftershock.x + mm_aftershock.dx * mm_aftershock.dist;
      float wave_y = mm_aftershock.y + mm_aftershock.dy * mm_aftershock.dist;
      float wdx = px - wave_x;
      float wdy = py - wave_y;
      if (wdx * wdx + wdy * wdy < (mm_aftershock.width + MIMIC_PLAYER_RADIUS) *
                                    (mm_aftershock.width + MIMIC_PLAYER_RADIUS)) {
        player_take_damage(MIMIC_CONTACT_DAMAGE, wave_x, wave_y, ENEMY_TYPE_MIMIC);
        mm_aftershock.active = 0;
      }
    }
  }

  // --- Linger zone tick ---
  for (int i = 0; i < MIMIC_LINGER_MAX; i++) {
    if (!mm_linger[i].active) continue;
    mm_linger[i].lifetime -= dt;
    if (mm_linger[i].lifetime <= 0.0f) {
      mm_linger[i].active = 0;
      continue;
    }
    mm_linger[i].tick_timer -= dt;
    if (mm_linger[i].tick_timer <= 0.0f) {
      mm_linger[i].tick_timer = MIMIC_LINGER_TICK;
      // Damage player if inside
      float ldx = px - mm_linger[i].x;
      float ldy = py - mm_linger[i].y;
      if (ldx * ldx + ldy * ldy <
          (mm_linger[i].radius + MIMIC_PLAYER_RADIUS) *
          (mm_linger[i].radius + MIMIC_PLAYER_RADIUS)) {
        player_take_damage(MIMIC_CONTACT_DAMAGE / 2, mm_linger[i].x,
                           mm_linger[i].y, ENEMY_TYPE_MIMIC);
      }
    }
  }

  // Tick pulse timer for visual
  if (e->mm_spin_pulse_timer > 0.0f) {
    e->mm_spin_pulse_timer -= dt;
  }
}

// ============================================================================
// Draw — matches player spin visual (yellow expanding ring)
// ============================================================================

static void draw_ring(float cx, float cy, float r, uint8_t red, uint8_t green,
                      uint8_t blue, uint8_t alpha)
{
  int segments = 32;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, red, green, blue, alpha);
  for (int i = 0; i < segments; i++) {
    float a1 = (float)i / segments * 2.0f * (float)PI;
    float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
    int x1 = (int)(cx + cosf(a1) * r);
    int y1 = (int)(cy + sinf(a1) * r);
    int x2 = (int)(cx + cosf(a2) * r);
    int y2 = (int)(cy + sinf(a2) * r);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
  }
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

void mimic_spin_draw(Enemy_t* e)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;
  float radius = weapons_get_spin_radius();

  // Spin pulse ring
  if (e->mm_spin_pulse_timer > 0.0f) {
    float alpha_pct = e->mm_spin_pulse_timer / MIMIC_SPIN_PULSE_DURATION;
    int alpha = (int)(200.0f * alpha_pct);
    if (alpha > 255) alpha = 255;
    float ring_progress = 1.0f - alpha_pct;
    float r = radius * (0.5f + 0.5f * ring_progress);
    draw_ring(cx, cy, r, 255, 200, 50, (uint8_t)alpha);
  }

  // Linger zones (yellow-orange pulsing circles)
  for (int i = 0; i < MIMIC_LINGER_MAX; i++) {
    if (!mm_linger[i].active) continue;
    float pct = mm_linger[i].lifetime / mm_linger[i].max_lifetime;
    int a = (int)(80.0f * pct);
    if (a > 255) a = 255;
    aColor_t lc = {255, 200, 50, (uint8_t)a};
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    a_DrawFilledCircle((int)mm_linger[i].x, (int)mm_linger[i].y,
                       (int)mm_linger[i].radius, lc);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Aftershock wave (filled beam matching player's aftershock visual)
  if (mm_aftershock.active) {
    float progress = mm_aftershock.dist / mm_aftershock.max_dist;
    int alpha = (int)(200.0f * (1.0f - progress * 0.6f));
    if (alpha > 255) alpha = 255;
    if (alpha < 0) alpha = 0;

    float dx = mm_aftershock.dx;
    float dy = mm_aftershock.dy;
    float perp_x = -dy;
    float perp_y =  dx;
    float w = mm_aftershock.width;
    float ox = mm_aftershock.x;
    float oy = mm_aftershock.y;
    float head = mm_aftershock.dist;

    float tail = head - 60.0f;
    if (tail < 0.0f) tail = 0.0f;

    // Four corners of the beam rectangle
    float x1 = ox + dx * tail + perp_x * w;
    float y1 = oy + dy * tail + perp_y * w;
    float x2 = ox + dx * tail - perp_x * w;
    float y2 = oy + dy * tail - perp_y * w;
    float x3 = ox + dx * head - perp_x * w;
    float y3 = oy + dy * head - perp_y * w;
    float x4 = ox + dx * head + perp_x * w;
    float y4 = oy + dy * head + perp_y * w;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    // Filled beam: draw lines between edges
    int steps = (int)(head - tail);
    if (steps < 1) steps = 1;
    if (steps > 80) steps = 80;
    for (int s = 0; s <= steps; s++) {
      float t = (float)s / (float)steps;
      float dist_along = tail + t * (head - tail);
      float fade = 1.0f - (head - dist_along) / 60.0f;
      if (fade < 0.2f) fade = 0.2f;
      if (fade > 1.0f) fade = 1.0f;
      int a = (int)((float)alpha * fade);

      float lx = ox + dx * dist_along + perp_x * w;
      float ly = oy + dy * dist_along + perp_y * w;
      float rx = ox + dx * dist_along - perp_x * w;
      float ry = oy + dy * dist_along - perp_y * w;

      SDL_SetRenderDrawColor(app.renderer, 255, 220, 80, (uint8_t)a);
      SDL_RenderDrawLine(app.renderer, (int)lx, (int)ly, (int)rx, (int)ry);
    }

    // Outline edges
    int edge_alpha = alpha * 3 / 4;
    SDL_SetRenderDrawColor(app.renderer, 255, 240, 120, (uint8_t)edge_alpha);
    SDL_RenderDrawLine(app.renderer, (int)x1, (int)y1, (int)x4, (int)y4);
    SDL_RenderDrawLine(app.renderer, (int)x2, (int)y2, (int)x3, (int)y3);
    // Head cap
    SDL_SetRenderDrawColor(app.renderer, 255, 255, 180, (uint8_t)alpha);
    SDL_RenderDrawLine(app.renderer, (int)x3, (int)y3, (int)x4, (int)y4);

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }
}
