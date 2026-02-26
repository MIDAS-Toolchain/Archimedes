#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Orbit — dash to intercept, then chase with spinning orbs
// ============================================================================

#define MM_ORBIT_DASH_SPEED    350.0f
#define MM_ORBIT_CHASE_SPEED   180.0f
#define MM_ORBIT_RADIUS_BONUS  15.0f
#define MM_ORBIT_PREDICT_TIME  0.5f
#define MM_ORBIT_ARRIVE_DIST   30.0f
#define MM_ORBIT_DASH_TIMEOUT  1.5f
#define MM_ORBIT_HIT_COOLDOWN  0.3f
#define MM_ORBIT_LINGER_MAX    4
#define MM_ORBIT_LINGER_TICK   0.3f
#define MM_ORBIT_GW_MAX        8
#define MM_ORBIT_SHATTER_MAX   16
#define MM_ORBIT_SHATTER_SPEED 300.0f

enum {
  ORBIT_AI_STRAFE,
  ORBIT_AI_DASH_TO,
  ORBIT_AI_CHASE,
  ORBIT_AI_RETREAT,
};

// --- Orbit state ---

static int   mm_orbit_active = 0;
static float mm_orbit_angle = 0.0f;
static float mm_orbit_active_timer = 0.0f;
static float mm_orbit_max_duration = 0.0f;  // for growth factor
static float mm_orbit_player_cd = 0.0f;     // player hit cooldown

// Linger trail zones (damages player on tick)
typedef struct {
  float x, y, radius;
  float lifetime, max_lifetime, tick_timer;
  int active;
} MmOrbitLinger_t;
static MmOrbitLinger_t mm_orbit_linger[MM_ORBIT_LINGER_MAX];
static float mm_orbit_linger_timer = 0.0f;

// Gravity well slow zones
typedef struct {
  float x, y, radius;
  float lifetime;
  int active;
} MmOrbitGW_t;
static MmOrbitGW_t mm_orbit_gw[MM_ORBIT_GW_MAX];
static float mm_orbit_gw_timer = 0.0f;

// Shatter projectiles (fired at player on orb expiry)
typedef struct {
  float x, y, vx, vy;
  float lifetime;
  int active;
} MmOrbitShatter_t;
static MmOrbitShatter_t mm_orbit_shatter[MM_ORBIT_SHATTER_MAX];

// Ghost orbits (afterglow — linger after orbit expires)
#define MM_GHOST_ORBIT_MAX 2
typedef struct {
  float x, y, angle;
  float timer, max_timer;
  int orb_count;
  float radius;
  float player_cd;
  float trail_timer;
  int active;
} MmGhostOrbit_t;
static MmGhostOrbit_t mm_ghost_orbits[MM_GHOST_ORBIT_MAX];

// ============================================================================
// Helpers
// ============================================================================

static float get_growth_factor(void)
{
  int grow_tier = upgrades_get_tier(UPG_ORBIT_GROWING_ORB);
  if (grow_tier <= 0 || !mm_orbit_active) return 1.0f;
  static const float growth_mult[4] = { 1.0f, 1.5f, 2.0f, 3.0f };
  float elapsed = mm_orbit_max_duration - mm_orbit_active_timer;
  float t = elapsed / mm_orbit_max_duration;
  if (t > 1.0f) t = 1.0f;
  if (t < 0.0f) t = 0.0f;
  return 1.0f + (growth_mult[grow_tier] - 1.0f) * t;
}

static void draw_ring(float cx, float cy, float r, uint8_t red, uint8_t green,
                      uint8_t blue, uint8_t alpha)
{
  int segments = 24;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, red, green, blue, alpha);
  for (int i = 0; i < segments; i++) {
    float a1 = (float)i / segments * 2.0f * (float)PI;
    float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
    SDL_RenderDrawLine(app.renderer,
      (int)(cx + cosf(a1) * r), (int)(cy + sinf(a1) * r),
      (int)(cx + cosf(a2) * r), (int)(cy + sinf(a2) * r));
  }
}

static void orbit_enter_retreat(Enemy_t* e, float px, float py)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  float angle_offset = RANDF(-1.2f, 1.2f);
  float cos_a = cosf(angle_offset);
  float sin_a = sinf(angle_offset);
  float flank_dx = ndx * cos_a - ndy * sin_a;
  float flank_dy = ndx * sin_a + ndy * cos_a;

  float retreat_dist = RANDF(60.0f, 100.0f);
  e->mm_spin_flank_x = cx + flank_dx * retreat_dist;
  e->mm_spin_flank_y = cy + flank_dy * retreat_dist;

  float margin = 30.0f;
  if (e->mm_spin_flank_x < margin) e->mm_spin_flank_x = margin;
  if (e->mm_spin_flank_x > SCREEN_WIDTH - margin) e->mm_spin_flank_x = SCREEN_WIDTH - margin;
  if (e->mm_spin_flank_y < margin) e->mm_spin_flank_y = margin;
  if (e->mm_spin_flank_y > SCREEN_HEIGHT - margin) e->mm_spin_flank_y = SCREEN_HEIGHT - margin;

  e->mm_spin_ai_state = ORBIT_AI_RETREAT;
  e->mm_spin_ai_timer = RANDF(0.3f, 0.5f);
}

static void fire_shatter(float cx, float cy, float px, float py)
{
  int tier = upgrades_get_tier(UPG_ORBIT_SHATTER);
  if (tier <= 0) return;

  static const int counts[4] = { 0, 4, 5, 7 };
  int count = counts[tier];
  int orb_count = weapons_get_orbit_orb_count();
  float radius = (weapons_get_orbit_radius() + MM_ORBIT_RADIUS_BONUS);

  for (int orb = 0; orb < orb_count; orb++) {
    float orb_angle = mm_orbit_angle +
                      (2.0f * (float)PI * (float)orb) / (float)orb_count;
    float ox = cx + cosf(orb_angle) * radius;
    float oy = cy + sinf(orb_angle) * radius;

    // Fire projectiles from each orb position toward player
    float base_angle = atan2f(py - oy, px - ox);
    float spread = 1.0472f; // 60 degrees
    float step = (count > 1) ? spread / (float)(count - 1) : 0.0f;
    float start = base_angle - spread * 0.5f;

    for (int s = 0; s < count; s++) {
      float angle = (count > 1) ? start + step * (float)s : base_angle;
      for (int b = 0; b < MM_ORBIT_SHATTER_MAX; b++) {
        if (mm_orbit_shatter[b].active) continue;
        mm_orbit_shatter[b] = (MmOrbitShatter_t){
          .x = ox, .y = oy,
          .vx = cosf(angle) * MM_ORBIT_SHATTER_SPEED,
          .vy = sinf(angle) * MM_ORBIT_SHATTER_SPEED,
          .lifetime = 0.65f,
          .active = 1
        };
        break;
      }
    }
  }
}

// ============================================================================
// AI State Machine + Update
// ============================================================================

void mimic_orbit_update(Enemy_t* e, int index, float dt,
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

  switch (e->mm_spin_ai_state) {
    case ORBIT_AI_STRAFE: {
      // Orbit player at ~200px range, waiting for cooldown
      if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
      float ndx = dx / dist;
      float ndy = dy / dist;

      float steer_x = 0.0f, steer_y = 0.0f;

      if (dist > MIMIC_RANGE_FAR + 30.0f) {
        steer_x += -ndx * 0.5f;
        steer_y += -ndy * 0.5f;
      } else if (dist < MIMIC_RANGE_FAR - 50.0f) {
        steer_x += ndx * 0.8f;
        steer_y += ndy * 0.8f;
      }

      float perp_x = -ndy;
      float perp_y =  ndx;
      steer_x += perp_x * 0.6f;
      steer_y += perp_y * 0.6f;

      steer_x += sep_x;
      steer_y += sep_y;
      float margin = 30.0f;
      if (cx < margin)                 steer_x += (margin - cx) / margin;
      if (cx > SCREEN_WIDTH - margin)  steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
      if (cy < margin)                 steer_y += (margin - cy) / margin;
      if (cy > SCREEN_HEIGHT - margin) steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

      float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
      if (slen > 0.1f) {
        e->vx = (steer_x / slen) * MIMIC_SPEED_ORBIT;
        e->vy = (steer_y / slen) * MIMIC_SPEED_ORBIT;
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

      // Transition: cooldown expired → predict and dash
      if (e->mm_weapon_cooldown <= 0.0f) {
        float pspd = sqrtf(pvx * pvx + pvy * pvy);

        if (pspd > 20.0f) {
          float pred_x = px + pvx * MM_ORBIT_PREDICT_TIME;
          float pred_y = py + pvy * MM_ORBIT_PREDICT_TIME;
          float move_nx = pvx / pspd;
          float move_ny = pvy / pspd;
          pred_x += move_nx * 60.0f;
          pred_y += move_ny * 60.0f;

          if (pred_x < 30.0f) pred_x = 30.0f;
          if (pred_x > SCREEN_WIDTH - 30.0f) pred_x = SCREEN_WIDTH - 30.0f;
          if (pred_y < 30.0f) pred_y = 30.0f;
          if (pred_y > SCREEN_HEIGHT - 30.0f) pred_y = SCREEN_HEIGHT - 30.0f;

          e->mm_spin_flank_x = pred_x;
          e->mm_spin_flank_y = pred_y;
        } else {
          float angle = RANDF(0.0f, 2.0f * (float)PI);
          e->mm_spin_flank_x = px + cosf(angle) * 80.0f;
          e->mm_spin_flank_y = py + sinf(angle) * 80.0f;

          if (e->mm_spin_flank_x < 30.0f) e->mm_spin_flank_x = 30.0f;
          if (e->mm_spin_flank_x > SCREEN_WIDTH - 30.0f)
            e->mm_spin_flank_x = SCREEN_WIDTH - 30.0f;
          if (e->mm_spin_flank_y < 30.0f) e->mm_spin_flank_y = 30.0f;
          if (e->mm_spin_flank_y > SCREEN_HEIGHT - 30.0f)
            e->mm_spin_flank_y = SCREEN_HEIGHT - 30.0f;
        }

        e->mm_spin_ai_state = ORBIT_AI_DASH_TO;
        e->mm_spin_ai_timer = MM_ORBIT_DASH_TIMEOUT;
      }
      break;
    }

    case ORBIT_AI_DASH_TO: {
      // Dash toward predicted player position
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     MM_ORBIT_DASH_SPEED, sep_x, sep_y, 10.0f, dt);

      e->mm_spin_ai_timer -= dt;

      float fdx = cx - e->mm_spin_flank_x;
      float fdy = cy - e->mm_spin_flank_y;
      if (fdx * fdx + fdy * fdy < MM_ORBIT_ARRIVE_DIST * MM_ORBIT_ARRIVE_DIST ||
          e->mm_spin_ai_timer <= 0.0f) {
        // Activate orbs and start chasing
        mm_orbit_active = 1;
        float dur = weapons_get_orbit_duration();
        mm_orbit_active_timer = dur;
        mm_orbit_max_duration = dur;
        mm_orbit_angle = 0.0f;
        mm_orbit_player_cd = 0.0f;
        mm_orbit_linger_timer = 0.0f;
        mm_orbit_gw_timer = 0.0f;

        // Cooldown starts on cast, not on expiry
        e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);

        e->mm_spin_ai_state = ORBIT_AI_CHASE;
      }
      break;
    }

    case ORBIT_AI_CHASE: {
      // Maintain orbit-radius distance so orbs sweep through the player
      float orbit_r = (weapons_get_orbit_radius() + MM_ORBIT_RADIUS_BONUS);
      if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
      float ndx = dx / dist;
      float ndy = dy / dist;

      float steer_x = 0.0f, steer_y = 0.0f;

      // Radial: push out if too close, pull in if too far
      float desired = orbit_r * 0.85f;
      if (dist < desired - 20.0f) {
        steer_x += ndx * 1.0f;
        steer_y += ndy * 1.0f;
      } else if (dist > desired + 30.0f) {
        steer_x += -ndx * 0.8f;
        steer_y += -ndy * 0.8f;
      }

      // Strafe perpendicular to keep moving around player
      float perp_x = -ndy;
      float perp_y =  ndx;
      steer_x += perp_x * 0.5f;
      steer_y += perp_y * 0.5f;

      // Lead the player's movement — offset toward where they're heading
      float pspd = sqrtf(pvx * pvx + pvy * pvy);
      if (pspd > 20.0f) {
        float lead = 0.4f;
        steer_x += (pvx / pspd) * lead;
        steer_y += (pvy / pspd) * lead;
      }

      steer_x += sep_x;
      steer_y += sep_y;

      float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
      if (slen > 0.1f) {
        e->vx = (steer_x / slen) * MM_ORBIT_CHASE_SPEED;
        e->vy = (steer_y / slen) * MM_ORBIT_CHASE_SPEED;
      }
      e->x += e->vx * dt;
      e->y += e->vy * dt;
      if (e->x < 0) e->x = 0;
      if (e->y < 0) e->y = 0;
      if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
      if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;
      break;
    }

    case ORBIT_AI_RETREAT: {
      // Brief pullback to flank position, then re-engage
      float retreat_speed = MM_ORBIT_CHASE_SPEED * 1.5f;
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     retreat_speed, sep_x, sep_y, 20.0f, dt);

      e->mm_spin_ai_timer -= dt;
      if (e->mm_spin_ai_timer <= 0.0f) {
        // Re-engage if orbs still active, otherwise back to strafe
        if (mm_orbit_active) {
          e->mm_spin_ai_state = ORBIT_AI_CHASE;
        } else {
          e->mm_spin_ai_state = ORBIT_AI_STRAFE;
        }
      }
      break;
    }
  }

  // Recalculate center after movement
  cx = e->x + sz / 2.0f;
  cy = e->y + sz / 2.0f;

  // ==========================================================================
  // Orb rotation + collision (while active)
  // ==========================================================================

  if (mm_orbit_active) {
    float speed_mult = weapons_get_orbit_speed_mult();
    mm_orbit_angle += (2.0f * (float)PI) * dt * speed_mult;
    mm_orbit_active_timer -= dt;

    // Check expiry
    if (mm_orbit_active_timer <= 0.0f) {
      // Afterglow: spawn ghost orbit before shatter
      int afterglow_tier = upgrades_get_tier(UPG_ORBIT_AFTERGLOW);
      if (afterglow_tier > 0) {
        static const float ag_dur[4] = { 0.0f, 1.0f, 2.0f, 3.0f };
        float dur = ag_dur[afterglow_tier];
        int orb_count = weapons_get_orbit_orb_count();
        float radius = (weapons_get_orbit_radius() + MM_ORBIT_RADIUS_BONUS);
        for (int g = 0; g < MM_GHOST_ORBIT_MAX; g++) {
          if (!mm_ghost_orbits[g].active) {
            mm_ghost_orbits[g] = (MmGhostOrbit_t){
              .x = cx, .y = cy,
              .angle = mm_orbit_angle,
              .timer = dur, .max_timer = dur,
              .orb_count = orb_count,
              .radius = radius,
              .player_cd = 0.0f,
              .trail_timer = 0.0f,
              .active = 1
            };
            break;
          }
        }
      }

      fire_shatter(cx, cy, px, py);
      mm_orbit_active = 0;
      if (e->mm_spin_ai_state == ORBIT_AI_CHASE ||
          e->mm_spin_ai_state == ORBIT_AI_RETREAT) {
        e->mm_spin_ai_state = ORBIT_AI_STRAFE;
      }
    }
  }

  // Orb → player collision
  if (mm_orbit_active) {
    int orb_count = weapons_get_orbit_orb_count();
    float radius = (weapons_get_orbit_radius() + MM_ORBIT_RADIUS_BONUS);
    float growth = get_growth_factor();
    float orb_size = ORBIT_ORB_SIZE * growth;

    // Growing Orb T3: reduce hit cooldown
    float hit_cd = MM_ORBIT_HIT_COOLDOWN;
    int grow_tier = upgrades_get_tier(UPG_ORBIT_GROWING_ORB);
    if (grow_tier >= 3 && growth > 1.0f) {
      hit_cd = MM_ORBIT_HIT_COOLDOWN / growth;
      if (hit_cd < 0.1f) hit_cd = 0.1f;
    }

    if (mm_orbit_player_cd > 0.0f)
      mm_orbit_player_cd -= dt;

    if (mm_orbit_player_cd <= 0.0f) {
      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = mm_orbit_angle +
                          (2.0f * (float)PI * (float)orb) / (float)orb_count;
        float ox = cx + cosf(orb_angle) * radius;
        float oy = cy + sinf(orb_angle) * radius;

        float odx = px - ox;
        float ody = py - oy;
        if (odx * odx + ody * ody <
            (orb_size + MIMIC_PLAYER_RADIUS) *
            (orb_size + MIMIC_PLAYER_RADIUS)) {
          player_take_damage(MIMIC_CONTACT_DAMAGE, ox, oy, ENEMY_TYPE_MIMIC);
          mm_orbit_player_cd = hit_cd;

          // Conductor: orb hits slow the player
          int cond_tier = upgrades_get_tier(UPG_ORBIT_CONDUCTOR);
          if (cond_tier > 0) {
            static const float slow_vals[4] = { 1.0f, 0.85f, 0.75f, 0.70f };
            player_apply_slow(slow_vals[cond_tier], 0.5f);
          }

          // Retreat after landing a hit (like spin AI)
          if (e->mm_spin_ai_state == ORBIT_AI_CHASE) {
            orbit_enter_retreat(e, px, py);
          }
          break;
        }
      }
    }

    // Linger Trail: leave fire zones behind orbs periodically
    int linger_tier = upgrades_get_tier(UPG_ORBIT_LINGER_TRAIL);
    if (linger_tier > 0) {
      mm_orbit_linger_timer += dt;
      if (mm_orbit_linger_timer >= 0.3f) {
        mm_orbit_linger_timer -= 0.3f;
        static const float trail_dur[4] = { 0.0f, 0.9f, 1.8f, 2.7f };
        for (int orb = 0; orb < orb_count; orb++) {
          float orb_angle = mm_orbit_angle +
                            (2.0f * (float)PI * (float)orb) / (float)orb_count;
          float ox = cx + cosf(orb_angle) * radius;
          float oy = cy + sinf(orb_angle) * radius;
          for (int i = 0; i < MM_ORBIT_LINGER_MAX; i++) {
            if (!mm_orbit_linger[i].active) {
              mm_orbit_linger[i] = (MmOrbitLinger_t){
                .x = ox, .y = oy, .radius = 24.0f,
                .lifetime = trail_dur[linger_tier],
                .max_lifetime = trail_dur[linger_tier],
                .tick_timer = 0.0f, .active = 1
              };
              break;
            }
          }
        }
      }
    }

    // Gravity Well: leave slow zones behind orbs
    int gw_tier = upgrades_get_tier(UPG_ORBIT_GRAVITY_WELL);
    if (gw_tier > 0) {
      static const float gw_interval[4] = { 0.0f, 0.4f, 0.3f, 0.25f };
      static const float gw_dur[4]      = { 0.0f, 2.1f, 2.8f, 3.5f };
      static const float gw_radius_t[4] = { 0.0f, 25.0f, 31.0f, 39.0f };

      mm_orbit_gw_timer += dt;
      if (mm_orbit_gw_timer >= gw_interval[gw_tier]) {
        mm_orbit_gw_timer -= gw_interval[gw_tier];
        for (int orb = 0; orb < orb_count; orb++) {
          float orb_angle = mm_orbit_angle +
                            (2.0f * (float)PI * (float)orb) / (float)orb_count;
          float ox = cx + cosf(orb_angle) * radius;
          float oy = cy + sinf(orb_angle) * radius;
          for (int i = 0; i < MM_ORBIT_GW_MAX; i++) {
            if (!mm_orbit_gw[i].active) {
              mm_orbit_gw[i] = (MmOrbitGW_t){
                .x = ox, .y = oy,
                .radius = gw_radius_t[gw_tier],
                .lifetime = gw_dur[gw_tier],
                .active = 1
              };
              break;
            }
          }
        }
      }
    }
  }

  // ==========================================================================
  // Update linger zones (tick damage to player)
  // ==========================================================================

  for (int i = 0; i < MM_ORBIT_LINGER_MAX; i++) {
    if (!mm_orbit_linger[i].active) continue;
    mm_orbit_linger[i].lifetime -= dt;
    if (mm_orbit_linger[i].lifetime <= 0.0f) {
      mm_orbit_linger[i].active = 0;
      continue;
    }
    mm_orbit_linger[i].tick_timer -= dt;
    if (mm_orbit_linger[i].tick_timer <= 0.0f) {
      mm_orbit_linger[i].tick_timer = MM_ORBIT_LINGER_TICK;
      float ldx = px - mm_orbit_linger[i].x;
      float ldy = py - mm_orbit_linger[i].y;
      float lr = mm_orbit_linger[i].radius + MIMIC_PLAYER_RADIUS;
      if (ldx * ldx + ldy * ldy < lr * lr) {
        player_take_damage(MIMIC_CONTACT_DAMAGE / 2,
                           mm_orbit_linger[i].x, mm_orbit_linger[i].y,
                           ENEMY_TYPE_MIMIC);
      }
    }
  }

  // ==========================================================================
  // Update gravity well zones (slow player)
  // ==========================================================================

  for (int i = 0; i < MM_ORBIT_GW_MAX; i++) {
    if (!mm_orbit_gw[i].active) continue;
    mm_orbit_gw[i].lifetime -= dt;
    if (mm_orbit_gw[i].lifetime <= 0.0f) {
      mm_orbit_gw[i].active = 0;
      continue;
    }
    float gdx = px - mm_orbit_gw[i].x;
    float gdy = py - mm_orbit_gw[i].y;
    float gr = mm_orbit_gw[i].radius + MIMIC_PLAYER_RADIUS;
    if (gdx * gdx + gdy * gdy < gr * gr) {
      int gw_tier = upgrades_get_tier(UPG_ORBIT_GRAVITY_WELL);
      float slow_mult = (gw_tier >= 3) ? 0.7f : 0.85f;
      player_apply_slow(slow_mult, 0.2f);
    }
  }

  // ==========================================================================
  // Update shatter projectiles
  // ==========================================================================

  for (int i = 0; i < MM_ORBIT_SHATTER_MAX; i++) {
    if (!mm_orbit_shatter[i].active) continue;

    // Soft homing toward player
    float bul_angle = atan2f(mm_orbit_shatter[i].vy, mm_orbit_shatter[i].vx);
    float target_angle = atan2f(py - mm_orbit_shatter[i].y,
                                px - mm_orbit_shatter[i].x);
    float diff = target_angle - bul_angle;
    while (diff > PI) diff -= 2.0f * (float)PI;
    while (diff < -PI) diff += 2.0f * (float)PI;
    float max_turn = 3.0f * dt;
    if (diff > max_turn) diff = max_turn;
    else if (diff < -max_turn) diff = -max_turn;
    float new_angle = bul_angle + diff;
    mm_orbit_shatter[i].vx = cosf(new_angle) * MM_ORBIT_SHATTER_SPEED;
    mm_orbit_shatter[i].vy = sinf(new_angle) * MM_ORBIT_SHATTER_SPEED;

    mm_orbit_shatter[i].x += mm_orbit_shatter[i].vx * dt;
    mm_orbit_shatter[i].y += mm_orbit_shatter[i].vy * dt;

    mm_orbit_shatter[i].lifetime -= dt;
    if (mm_orbit_shatter[i].lifetime <= 0.0f) {
      mm_orbit_shatter[i].active = 0;
      continue;
    }

    // Off-screen
    if (mm_orbit_shatter[i].x < -20 || mm_orbit_shatter[i].x > SCREEN_WIDTH + 20 ||
        mm_orbit_shatter[i].y < -20 || mm_orbit_shatter[i].y > SCREEN_HEIGHT + 20) {
      mm_orbit_shatter[i].active = 0;
      continue;
    }

    // Player collision
    float sdx = mm_orbit_shatter[i].x - px;
    float sdy = mm_orbit_shatter[i].y - py;
    if (sdx * sdx + sdy * sdy < MIMIC_PLAYER_RADIUS * MIMIC_PLAYER_RADIUS) {
      player_take_damage(MIMIC_CONTACT_DAMAGE, mm_orbit_shatter[i].x,
                         mm_orbit_shatter[i].y, ENEMY_TYPE_MIMIC);
      // T3: pierce (don't deactivate)
      if (upgrades_get_tier(UPG_ORBIT_SHATTER) < 3) {
        mm_orbit_shatter[i].active = 0;
      }
    }
  }

  // ==========================================================================
  // Update ghost orbits (afterglow)
  // ==========================================================================

  for (int g = 0; g < MM_GHOST_ORBIT_MAX; g++) {
    if (!mm_ghost_orbits[g].active) continue;

    mm_ghost_orbits[g].timer -= dt;
    if (mm_ghost_orbits[g].timer <= 0.0f) {
      // Ghost expires — fire shatter from ghost position
      fire_shatter(mm_ghost_orbits[g].x, mm_ghost_orbits[g].y, px, py);
      mm_ghost_orbits[g].active = 0;
      continue;
    }

    // Advance angle
    float speed_mult = weapons_get_orbit_speed_mult();
    mm_ghost_orbits[g].angle += (2.0f * (float)PI) * dt * speed_mult;

    // Linger trail from ghost orbs
    int linger_tier = upgrades_get_tier(UPG_ORBIT_LINGER_TRAIL);
    if (linger_tier > 0) {
      mm_ghost_orbits[g].trail_timer += dt;
      if (mm_ghost_orbits[g].trail_timer >= 0.3f) {
        mm_ghost_orbits[g].trail_timer -= 0.3f;
        static const float trail_dur[4] = { 0.0f, 0.9f, 1.8f, 2.7f };
        float dur = trail_dur[linger_tier] * 0.6f;
        for (int orb = 0; orb < mm_ghost_orbits[g].orb_count; orb++) {
          float orb_angle = mm_ghost_orbits[g].angle +
            (2.0f * (float)PI * orb) / mm_ghost_orbits[g].orb_count;
          float ox = mm_ghost_orbits[g].x + cosf(orb_angle) * mm_ghost_orbits[g].radius;
          float oy = mm_ghost_orbits[g].y + sinf(orb_angle) * mm_ghost_orbits[g].radius;
          for (int i = 0; i < MM_ORBIT_LINGER_MAX; i++) {
            if (!mm_orbit_linger[i].active) {
              mm_orbit_linger[i] = (MmOrbitLinger_t){
                .x = ox, .y = oy, .radius = 20.0f,
                .lifetime = dur, .max_lifetime = dur,
                .tick_timer = 0.0f, .active = 1
              };
              break;
            }
          }
        }
      }
    }

    // Player collision
    if (mm_ghost_orbits[g].player_cd > 0.0f)
      mm_ghost_orbits[g].player_cd -= dt;

    if (mm_ghost_orbits[g].player_cd <= 0.0f) {
      float orb_sz = ORBIT_ORB_SIZE;
      for (int orb = 0; orb < mm_ghost_orbits[g].orb_count; orb++) {
        float orb_angle = mm_ghost_orbits[g].angle +
          (2.0f * (float)PI * orb) / mm_ghost_orbits[g].orb_count;
        float ox = mm_ghost_orbits[g].x + cosf(orb_angle) * mm_ghost_orbits[g].radius;
        float oy = mm_ghost_orbits[g].y + sinf(orb_angle) * mm_ghost_orbits[g].radius;

        float odx = px - ox;
        float ody = py - oy;
        if (odx * odx + ody * ody <
            (orb_sz + MIMIC_PLAYER_RADIUS) * (orb_sz + MIMIC_PLAYER_RADIUS)) {
          player_take_damage(MIMIC_CONTACT_DAMAGE / 2, ox, oy, ENEMY_TYPE_MIMIC);
          mm_ghost_orbits[g].player_cd = MM_ORBIT_HIT_COOLDOWN * 2.0f;
          break;
        }
      }
    }
  }
}

// ============================================================================
// Draw
// ============================================================================

void mimic_orbit_draw(Enemy_t* e)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  // Draw orbs (orange, matching player orbit color)
  if (mm_orbit_active) {
    int orb_count = weapons_get_orbit_orb_count();
    float radius = (weapons_get_orbit_radius() + MM_ORBIT_RADIUS_BONUS);
    float growth = get_growth_factor();
    float orb_sz = ORBIT_ORB_SIZE * growth;

    // Orbit radius ring (faint)
    draw_ring(cx, cy, radius, 255, 160, 50, 25);

    for (int orb = 0; orb < orb_count; orb++) {
      float orb_angle = mm_orbit_angle +
                        (2.0f * (float)PI * (float)orb) / (float)orb_count;
      float ox = cx + cosf(orb_angle) * radius - orb_sz / 2.0f;
      float oy = cy + sinf(orb_angle) * radius - orb_sz / 2.0f;

      // Glow behind orb
      a_DrawFilledRect(
        (aRectf_t){ox - 2, oy - 2, orb_sz + 4, orb_sz + 4},
        (aColor_t){255, 160, 50, 80}
      );
      // Solid orb
      a_DrawFilledRect(
        (aRectf_t){ox, oy, orb_sz, orb_sz},
        (aColor_t){255, 160, 50, 255}
      );
    }
  }

  // Ghost orbits (afterglow — translucent, fading)
  for (int g = 0; g < MM_GHOST_ORBIT_MAX; g++) {
    if (!mm_ghost_orbits[g].active) continue;
    float fade = mm_ghost_orbits[g].timer / mm_ghost_orbits[g].max_timer;
    if (fade > 1.0f) fade = 1.0f;
    int base_alpha = (int)(140.0f * fade);
    float orb_sz = ORBIT_ORB_SIZE;

    // Faint radius ring
    draw_ring(mm_ghost_orbits[g].x, mm_ghost_orbits[g].y,
              mm_ghost_orbits[g].radius, 200, 140, 255, (uint8_t)(base_alpha / 6));

    for (int orb = 0; orb < mm_ghost_orbits[g].orb_count; orb++) {
      float orb_angle = mm_ghost_orbits[g].angle +
        (2.0f * (float)PI * orb) / mm_ghost_orbits[g].orb_count;
      float ox = mm_ghost_orbits[g].x + cosf(orb_angle) * mm_ghost_orbits[g].radius
                 - orb_sz / 2.0f;
      float oy = mm_ghost_orbits[g].y + sinf(orb_angle) * mm_ghost_orbits[g].radius
                 - orb_sz / 2.0f;

      // Ghost glow
      a_DrawFilledRect(
        (aRectf_t){ox - 2, oy - 2, orb_sz + 4, orb_sz + 4},
        (aColor_t){200, 140, 255, (uint8_t)(base_alpha / 3)}
      );
      // Ghost orb
      a_DrawFilledRect(
        (aRectf_t){ox, oy, orb_sz, orb_sz},
        (aColor_t){200, 140, 255, (uint8_t)base_alpha}
      );
    }
  }

  // Linger trail zones (orange pulsing circles)
  for (int i = 0; i < MM_ORBIT_LINGER_MAX; i++) {
    if (!mm_orbit_linger[i].active) continue;
    float pct = mm_orbit_linger[i].lifetime / mm_orbit_linger[i].max_lifetime;
    int a = (int)(80.0f * pct);
    if (a > 255) a = 255;
    a_DrawFilledCircle((int)mm_orbit_linger[i].x, (int)mm_orbit_linger[i].y,
                       (int)mm_orbit_linger[i].radius,
                       (aColor_t){255, 160, 50, (uint8_t)a});
  }

  // Gravity well zones (faint orange rings)
  for (int i = 0; i < MM_ORBIT_GW_MAX; i++) {
    if (!mm_orbit_gw[i].active) continue;
    float pct = mm_orbit_gw[i].lifetime / 3.5f;
    if (pct > 1.0f) pct = 1.0f;
    int a = (int)(40.0f * pct);
    if (a < 5) continue;
    draw_ring(mm_orbit_gw[i].x, mm_orbit_gw[i].y, mm_orbit_gw[i].radius,
              200, 130, 30, (uint8_t)a);
  }

  // Shatter projectiles (small orange squares)
  for (int i = 0; i < MM_ORBIT_SHATTER_MAX; i++) {
    if (!mm_orbit_shatter[i].active) continue;
    int bx = (int)mm_orbit_shatter[i].x;
    int by = (int)mm_orbit_shatter[i].y;
    // Glow (6x6)
    SDL_SetRenderDrawColor(app.renderer, 255, 160, 50, 40);
    { SDL_Rect glow = { bx - 3, by - 3, 6, 6 };
      SDL_RenderFillRect(app.renderer, &glow); }
    // Core (4x4)
    SDL_SetRenderDrawColor(app.renderer, 255, 200, 80, 255);
    { SDL_Rect core = { bx - 2, by - 2, 4, 4 };
      SDL_RenderFillRect(app.renderer, &core); }
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_orbit_cleanup(void)
{
  mm_orbit_active = 0;
  mm_orbit_angle = 0.0f;
  mm_orbit_active_timer = 0.0f;
  mm_orbit_max_duration = 0.0f;
  mm_orbit_player_cd = 0.0f;
  mm_orbit_linger_timer = 0.0f;
  mm_orbit_gw_timer = 0.0f;
  memset(mm_orbit_linger, 0, sizeof(mm_orbit_linger));
  memset(mm_orbit_gw, 0, sizeof(mm_orbit_gw));
  memset(mm_orbit_shatter, 0, sizeof(mm_orbit_shatter));
  memset(mm_ghost_orbits, 0, sizeof(mm_ghost_orbits));
}
