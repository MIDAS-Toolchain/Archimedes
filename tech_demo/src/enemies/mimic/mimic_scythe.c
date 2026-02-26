#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Scythe — dash in, wind-up telegraph, directional arc sweep
// ============================================================================

#define MM_SCYTHE_STRAFE_RANGE  160.0f
#define MM_SCYTHE_STRAFE_SPEED  90.0f
#define MM_SCYTHE_DASH_SPEED    300.0f
#define MM_SCYTHE_PREDICT_TIME  0.4f
#define MM_SCYTHE_PREDICT_LEAD  50.0f
#define MM_SCYTHE_ARRIVE_DIST   20.0f
#define MM_SCYTHE_DASH_TIMEOUT  1.2f
#define MM_SCYTHE_WINDUP_TIME   0.25f
#define MM_SCYTHE_SWEEP_VISUAL  0.15f
#define MM_SCYTHE_ZONE_MAX      4
#define MM_SCYTHE_ZONE_TICK     0.3f

enum {
  SCYTHE_AI_STRAFE,
  SCYTHE_AI_DASH_IN,
  SCYTHE_AI_SWING,
  SCYTHE_AI_RETREAT,
};

// --- Static state ---

static int   mm_scythe_swing_count = 0;
static float mm_scythe_sweep_timer = 0.0f;
static float mm_scythe_sweep_dir_x = 0.0f;
static float mm_scythe_sweep_dir_y = 0.0f;
static int   mm_scythe_is_whirlwind = 0;

// Sweep origin (where the swing happened, so visual stays in place during retreat)
static float mm_scythe_sweep_cx = 0.0f;
static float mm_scythe_sweep_cy = 0.0f;

// Harvest speed buff
static float mm_scythe_harvest_timer = 0.0f;
static float mm_scythe_harvest_mult = 1.0f;

// Arc-shaped hostile zones (slow + linger damage to player)
typedef struct {
  float cx, cy;
  float radius;
  float facing_angle;
  float arc_deg;
  float lifetime, max_lifetime;
  float slow_mult;    // < 1.0 = slow zone, 1.0 = linger only
  int   is_linger;
  float tick_timer;
  int   active;
} MmScytheZone_t;
static MmScytheZone_t mm_scythe_zones[MM_SCYTHE_ZONE_MAX];

// ============================================================================
// Helpers
// ============================================================================

static int angle_in_arc(float a, float start, float end)
{
  while (a < 0) a += 2.0f * (float)PI;
  while (a >= 2.0f * (float)PI) a -= 2.0f * (float)PI;
  while (start < 0) start += 2.0f * (float)PI;
  while (start >= 2.0f * (float)PI) start -= 2.0f * (float)PI;
  while (end < 0) end += 2.0f * (float)PI;
  while (end >= 2.0f * (float)PI) end -= 2.0f * (float)PI;

  if (start <= end) {
    return (a >= start && a <= end);
  } else {
    return (a >= start || a <= end);
  }
}

static float get_speed_mult(void)
{
  if (mm_scythe_harvest_timer > 0.0f) return mm_scythe_harvest_mult;
  return 1.0f;
}

static void scythe_enter_retreat(Enemy_t* e, float px, float py)
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

  float retreat_dist = RANDF(70.0f, 120.0f);
  e->mm_spin_flank_x = cx + flank_dx * retreat_dist;
  e->mm_spin_flank_y = cy + flank_dy * retreat_dist;

  float margin = 30.0f;
  if (e->mm_spin_flank_x < margin) e->mm_spin_flank_x = margin;
  if (e->mm_spin_flank_x > SCREEN_WIDTH - margin) e->mm_spin_flank_x = SCREEN_WIDTH - margin;
  if (e->mm_spin_flank_y < margin) e->mm_spin_flank_y = margin;
  if (e->mm_spin_flank_y > SCREEN_HEIGHT - margin) e->mm_spin_flank_y = SCREEN_HEIGHT - margin;

  e->mm_spin_ai_state = SCYTHE_AI_RETREAT;
  e->mm_spin_ai_timer = RANDF(0.4f, 0.7f);
}

// ============================================================================
// Fire scythe sweep — arc damage check against player + zone spawning
// ============================================================================

static void mimic_fire_scythe(Enemy_t* e, float px, float py)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  float radius = weapons_get_scythe_radius();
  float arc_deg = weapons_get_scythe_arc();

  // Whirlwind check
  int ww_tier = upgrades_get_tier(UPG_SCYTHE_WHIRLWIND);
  static const int whirlwind_every[4] = { 0, 4, 3, 2 };
  int is_whirlwind = 0;
  if (ww_tier > 0 && whirlwind_every[ww_tier] > 0 &&
      (mm_scythe_swing_count % whirlwind_every[ww_tier]) == 0) {
    arc_deg = 360.0f;
    is_whirlwind = 1;
  }

  // Facing toward player
  float fx = px - cx;
  float fy = py - cy;
  float flen = sqrtf(fx * fx + fy * fy);
  if (flen > 0.001f) { fx /= flen; fy /= flen; }
  else { fx = 0.0f; fy = -1.0f; }

  float facing_angle = atan2f(fy, fx);
  float half_arc = (arc_deg * (float)PI / 180.0f) / 2.0f;
  float arc_start = facing_angle - half_arc;
  float arc_end = facing_angle + half_arc;

  // Arc hit-check against player
  float dx = px - cx;
  float dy = py - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  int hit = 0;

  if (dist < radius + MIMIC_PLAYER_RADIUS) {
    if (arc_deg >= 360.0f) {
      hit = 1;
    } else {
      float player_angle = atan2f(dy, dx);
      hit = angle_in_arc(player_angle, arc_start, arc_end);
    }
  }

  if (hit) {
    player_take_damage(MIMIC_CONTACT_DAMAGE, cx, cy, ENEMY_TYPE_MIMIC);

    // Perpendicular knockback
    int kb_tier = upgrades_get_tier(UPG_SCYTHE_KNOCKBACK);
    if (kb_tier > 0 && dist > 0.1f) {
      float kb = weapons_get_scythe_knockback();
      float ndx = dx / dist;
      float ndy = dy / dist;
      // Perpendicular: rotate 90 degrees
      float kx = -ndy;
      float ky =  ndx;
      // Pick side based on cross product with facing direction
      float cross = fx * ndy - fy * ndx;
      if (cross < 0) { kx = -kx; ky = -ky; }
      player_apply_knockback(px - kx * kb, py - ky * kb, kb);
    }

    // Harvest: mimic gets speed buff after hitting player
    int harvest_tier = upgrades_get_tier(UPG_SCYTHE_HARVEST);
    if (harvest_tier > 0) {
      static const float h_speed[4] = { 1.0f, 1.15f, 1.20f, 1.25f };
      static const float h_dur[4]   = { 0.0f, 1.5f,  2.0f,  2.5f  };
      mm_scythe_harvest_timer = h_dur[harvest_tier];
      mm_scythe_harvest_mult = h_speed[harvest_tier];
    }
  }

  // Spawn slow zone
  int slow_tier = upgrades_get_tier(UPG_SCYTHE_SLOW_ZONE);
  if (slow_tier > 0) {
    static const float slow_mult[4] = { 1.0f, 0.80f, 0.70f, 0.60f };
    static const float slow_dur[4]  = { 0.0f, 1.0f,  1.5f,  2.0f  };
    for (int i = 0; i < MM_SCYTHE_ZONE_MAX; i++) {
      if (!mm_scythe_zones[i].active) {
        mm_scythe_zones[i] = (MmScytheZone_t){
          .cx = cx, .cy = cy,
          .radius = radius,
          .facing_angle = facing_angle,
          .arc_deg = arc_deg,
          .lifetime = slow_dur[slow_tier],
          .max_lifetime = slow_dur[slow_tier],
          .slow_mult = slow_mult[slow_tier],
          .is_linger = 0,
          .tick_timer = 0.0f,
          .active = 1
        };
        break;
      }
    }
  }

  // Spawn linger damage zone
  int linger_tier = upgrades_get_tier(UPG_SCYTHE_LINGER_TRAIL);
  if (linger_tier > 0) {
    static const float linger_dur[4] = { 0.0f, 1.0f, 1.5f, 2.0f };
    for (int i = 0; i < MM_SCYTHE_ZONE_MAX; i++) {
      if (!mm_scythe_zones[i].active) {
        mm_scythe_zones[i] = (MmScytheZone_t){
          .cx = cx, .cy = cy,
          .radius = radius,
          .facing_angle = facing_angle,
          .arc_deg = arc_deg,
          .lifetime = linger_dur[linger_tier],
          .max_lifetime = linger_dur[linger_tier],
          .slow_mult = 1.0f,
          .is_linger = 1,
          .tick_timer = 0.0f,
          .active = 1
        };
        break;
      }
    }
  }

  // Visual state — store position so sweep draws where the swing happened
  mm_scythe_sweep_timer = MM_SCYTHE_SWEEP_VISUAL;
  mm_scythe_sweep_cx = cx;
  mm_scythe_sweep_cy = cy;
  mm_scythe_sweep_dir_x = fx;
  mm_scythe_sweep_dir_y = fy;
  mm_scythe_is_whirlwind = is_whirlwind;

  // Cooldown starts on swing
  e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);

  mm_scythe_swing_count++;
}

// ============================================================================
// AI State Machine + Update
// ============================================================================

void mimic_scythe_update(Enemy_t* e, int index, float dt,
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

  float spd_mult = get_speed_mult();

  // Tick harvest buff
  if (mm_scythe_harvest_timer > 0.0f) {
    mm_scythe_harvest_timer -= dt;
    if (mm_scythe_harvest_timer <= 0.0f) {
      mm_scythe_harvest_timer = 0.0f;
      mm_scythe_harvest_mult = 1.0f;
    }
  }

  switch (e->mm_spin_ai_state) {
    case SCYTHE_AI_STRAFE: {
      // Circle player at mid range, waiting for cooldown
      if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
      float ndx = dx / dist;
      float ndy = dy / dist;

      float steer_x = 0.0f, steer_y = 0.0f;

      if (dist > MM_SCYTHE_STRAFE_RANGE + 30.0f) {
        steer_x += -ndx * 0.5f;
        steer_y += -ndy * 0.5f;
      } else if (dist < MM_SCYTHE_STRAFE_RANGE - 50.0f) {
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
        float spd = MM_SCYTHE_STRAFE_SPEED * spd_mult;
        e->vx = (steer_x / slen) * spd;
        e->vy = (steer_y / slen) * spd;
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
          float pred_x = px + pvx * MM_SCYTHE_PREDICT_TIME;
          float pred_y = py + pvy * MM_SCYTHE_PREDICT_TIME;
          float move_nx = pvx / pspd;
          float move_ny = pvy / pspd;
          pred_x += move_nx * MM_SCYTHE_PREDICT_LEAD;
          pred_y += move_ny * MM_SCYTHE_PREDICT_LEAD;

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

        e->mm_spin_ai_state = SCYTHE_AI_DASH_IN;
        e->mm_spin_ai_timer = MM_SCYTHE_DASH_TIMEOUT;
      }
      break;
    }

    case SCYTHE_AI_DASH_IN: {
      // Dash toward predicted player position
      float spd = MM_SCYTHE_DASH_SPEED * spd_mult;
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     spd, sep_x, sep_y, 10.0f, dt);

      e->mm_spin_ai_timer -= dt;

      // Check if within sweep range of player (not predicted position)
      float scythe_r = weapons_get_scythe_radius();
      float to_player = sqrtf(dx * dx + dy * dy);
      if (to_player < scythe_r - MM_SCYTHE_ARRIVE_DIST ||
          e->mm_spin_ai_timer <= 0.0f) {
        // Enter swing wind-up
        e->mm_spin_ai_state = SCYTHE_AI_SWING;
        e->mm_spin_ai_timer = MM_SCYTHE_WINDUP_TIME;
      }
      break;
    }

    case SCYTHE_AI_SWING: {
      // Wind-up: slow approach while tracking player
      float chase_spd = 80.0f * spd_mult;
      if (dist > 0.1f) {
        float ndx2 = -dx / dist;
        float ndy2 = -dy / dist;
        e->vx = ndx2 * chase_spd;
        e->vy = ndy2 * chase_spd;
      }
      e->x += e->vx * dt;
      e->y += e->vy * dt;
      if (e->x < 0) e->x = 0;
      if (e->y < 0) e->y = 0;
      if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
      if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

      e->mm_spin_ai_timer -= dt;
      if (e->mm_spin_ai_timer <= 0.0f) {
        // Fire the sweep
        mimic_fire_scythe(e, px, py);
        // Always retreat after swinging
        scythe_enter_retreat(e, px, py);
      }
      break;
    }

    case SCYTHE_AI_RETREAT: {
      float retreat_spd = 180.0f * spd_mult;
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     retreat_spd, sep_x, sep_y, 20.0f, dt);

      e->mm_spin_ai_timer -= dt;
      if (e->mm_spin_ai_timer <= 0.0f) {
        e->mm_spin_ai_state = SCYTHE_AI_STRAFE;
      }
      break;
    }
  }

  // Tick sweep visual timer
  if (mm_scythe_sweep_timer > 0.0f)
    mm_scythe_sweep_timer -= dt;

  // ==========================================================================
  // Update hostile arc zones
  // ==========================================================================

  for (int i = 0; i < MM_SCYTHE_ZONE_MAX; i++) {
    if (!mm_scythe_zones[i].active) continue;
    mm_scythe_zones[i].lifetime -= dt;
    if (mm_scythe_zones[i].lifetime <= 0.0f) {
      mm_scythe_zones[i].active = 0;
      continue;
    }

    // Slow zone: apply slow to player if inside
    if (mm_scythe_zones[i].slow_mult < 1.0f) {
      float zdx = px - mm_scythe_zones[i].cx;
      float zdy = py - mm_scythe_zones[i].cy;
      float zdist = sqrtf(zdx * zdx + zdy * zdy);
      if (zdist < mm_scythe_zones[i].radius + MIMIC_PLAYER_RADIUS) {
        int in_arc = 1;
        if (mm_scythe_zones[i].arc_deg < 360.0f) {
          float pa = atan2f(zdy, zdx);
          float z_half = (mm_scythe_zones[i].arc_deg * (float)PI / 180.0f) / 2.0f;
          float z_start = mm_scythe_zones[i].facing_angle - z_half;
          float z_end = mm_scythe_zones[i].facing_angle + z_half;
          in_arc = angle_in_arc(pa, z_start, z_end);
        }
        if (in_arc) {
          player_apply_slow(mm_scythe_zones[i].slow_mult, 0.15f);
        }
      }
    }

    // Linger zone: tick damage to player
    if (mm_scythe_zones[i].is_linger) {
      mm_scythe_zones[i].tick_timer += dt;
      if (mm_scythe_zones[i].tick_timer >= MM_SCYTHE_ZONE_TICK) {
        mm_scythe_zones[i].tick_timer -= MM_SCYTHE_ZONE_TICK;
        float zdx = px - mm_scythe_zones[i].cx;
        float zdy = py - mm_scythe_zones[i].cy;
        float zdist = sqrtf(zdx * zdx + zdy * zdy);
        if (zdist < mm_scythe_zones[i].radius + MIMIC_PLAYER_RADIUS) {
          int in_arc = 1;
          if (mm_scythe_zones[i].arc_deg < 360.0f) {
            float pa = atan2f(zdy, zdx);
            float z_half = (mm_scythe_zones[i].arc_deg * (float)PI / 180.0f) / 2.0f;
            float z_start = mm_scythe_zones[i].facing_angle - z_half;
            float z_end = mm_scythe_zones[i].facing_angle + z_half;
            in_arc = angle_in_arc(pa, z_start, z_end);
          }
          if (in_arc) {
            player_take_damage(MIMIC_CONTACT_DAMAGE / 2,
                               mm_scythe_zones[i].cx, mm_scythe_zones[i].cy,
                               ENEMY_TYPE_MIMIC);
          }
        }
      }
    }
  }
}

// ============================================================================
// Draw
// ============================================================================

void mimic_scythe_draw(Enemy_t* e)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;
  float px = player_get_x();
  float py = player_get_y();

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  // Wind-up telegraph: pulsing orange arc showing where sweep will land
  if (e->mm_spin_ai_state == SCYTHE_AI_SWING && e->mm_spin_ai_timer > 0.0f) {
    float radius = weapons_get_scythe_radius();
    float arc_deg = weapons_get_scythe_arc();

    // Check whirlwind for this swing
    int ww_tier = upgrades_get_tier(UPG_SCYTHE_WHIRLWIND);
    static const int ww_every[4] = { 0, 4, 3, 2 };
    if (ww_tier > 0 && ww_every[ww_tier] > 0 &&
        (mm_scythe_swing_count % ww_every[ww_tier]) == 0) {
      arc_deg = 360.0f;
    }

    float facing = atan2f(py - cy, px - cx);
    float half_arc = (arc_deg * (float)PI / 180.0f) / 2.0f;
    float arc_start = facing - half_arc;

    // Pulsing alpha
    float pulse = 0.6f + 0.4f * sinf(e->mm_spin_ai_timer * 20.0f);
    int alpha = (int)(50.0f * pulse);

    if (arc_deg >= 360.0f) {
      // Full circle telegraph
      SDL_SetRenderDrawColor(app.renderer, 255, 120, 50, (uint8_t)alpha);
      int segs = 24;
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
          (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius));
      }
    } else {
      // Arc telegraph
      int segs = (int)(arc_deg / 8.0f);
      if (segs < 6) segs = 6;
      float arc_end_a = arc_start + arc_deg * (float)PI / 180.0f;
      for (int s = 0; s < segs; s++) {
        float a1 = arc_start + (arc_end_a - arc_start) * (float)s / segs;
        float a2 = arc_start + (arc_end_a - arc_start) * (float)(s + 1) / segs;
        aColor_t col = {255, 120, 50, (uint8_t)(alpha / 2)};
        a_DrawFilledTriangle((int)cx, (int)cy,
          (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
          (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius), col);
      }
      // Edge lines
      SDL_SetRenderDrawColor(app.renderer, 255, 120, 50, (uint8_t)alpha);
      SDL_RenderDrawLine(app.renderer, (int)cx, (int)cy,
        (int)(cx + cosf(arc_start) * radius), (int)(cy + sinf(arc_start) * radius));
      SDL_RenderDrawLine(app.renderer, (int)cx, (int)cy,
        (int)(cx + cosf(arc_end_a) * radius), (int)(cy + sinf(arc_end_a) * radius));
    }
  }

  // Sweep flash visual (drawn at stored swing position, not current mimic pos)
  if (mm_scythe_sweep_timer > 0.0f) {
    float scx = mm_scythe_sweep_cx;
    float scy = mm_scythe_sweep_cy;
    float alpha_pct = mm_scythe_sweep_timer / MM_SCYTHE_SWEEP_VISUAL;
    int alpha = (int)(220.0f * alpha_pct);

    float radius = weapons_get_scythe_radius();
    float arc_deg = mm_scythe_is_whirlwind ? 360.0f : weapons_get_scythe_arc();
    float facing = atan2f(mm_scythe_sweep_dir_y, mm_scythe_sweep_dir_x);
    float half_arc = (arc_deg * (float)PI / 180.0f) / 2.0f;
    float arc_start = facing - half_arc;

    if (arc_deg >= 360.0f) {
      // Whirlwind: animated 360° filled sweep (matches player style)
      int segments = 48;
      float sweep_progress = 1.0f - alpha_pct;
      float draw_end = arc_start + 2.0f * (float)PI * sweep_progress;

      // Filled arc triangles
      for (int s = 0; s < segments; s++) {
        float a1 = arc_start + (draw_end - arc_start) * (float)s / segments;
        float a2 = arc_start + (draw_end - arc_start) * (float)(s + 1) / segments;
        aColor_t col = {255, 220, 180, (uint8_t)(alpha / 2)};
        a_DrawFilledTriangle((int)scx, (int)scy,
          (int)(scx + cosf(a1) * radius), (int)(scy + sinf(a1) * radius),
          (int)(scx + cosf(a2) * radius), (int)(scy + sinf(a2) * radius), col);
      }

      // Bright edge at sweep front
      float edge_x = scx + cosf(draw_end) * radius;
      float edge_y = scy + sinf(draw_end) * radius;
      SDL_SetRenderDrawColor(app.renderer, 255, 255, 240, (uint8_t)alpha);
      SDL_RenderDrawLine(app.renderer, (int)scx, (int)scy, (int)edge_x, (int)edge_y);

      // Outer arc outline
      for (int s = 0; s < segments; s++) {
        float a1 = arc_start + (draw_end - arc_start) * (float)s / segments;
        float a2 = arc_start + (draw_end - arc_start) * (float)(s + 1) / segments;
        SDL_RenderDrawLine(app.renderer,
          (int)(scx + cosf(a1) * radius), (int)(scy + sinf(a1) * radius),
          (int)(scx + cosf(a2) * radius), (int)(scy + sinf(a2) * radius));
      }
    } else {
      // Arc sweep flash — bright white-orange filled arc
      int segs = (int)(arc_deg / 5.0f);
      if (segs < 8) segs = 8;
      if (segs > 48) segs = 48;
      float arc_end_a = arc_start + arc_deg * (float)PI / 180.0f;
      for (int s = 0; s < segs; s++) {
        float a1 = arc_start + (arc_end_a - arc_start) * (float)s / segs;
        float a2 = arc_start + (arc_end_a - arc_start) * (float)(s + 1) / segs;
        aColor_t col = {255, 220, 180, (uint8_t)(alpha * 2 / 3)};
        a_DrawFilledTriangle((int)scx, (int)scy,
          (int)(scx + cosf(a1) * radius), (int)(scy + sinf(a1) * radius),
          (int)(scx + cosf(a2) * radius), (int)(scy + sinf(a2) * radius), col);
      }
      // Bright white edge at sweep front
      SDL_SetRenderDrawColor(app.renderer, 255, 255, 240, (uint8_t)alpha);
      for (int s = 0; s < segs; s++) {
        float a1 = arc_start + (arc_end_a - arc_start) * (float)s / segs;
        float a2 = arc_start + (arc_end_a - arc_start) * (float)(s + 1) / segs;
        SDL_RenderDrawLine(app.renderer,
          (int)(scx + cosf(a1) * radius), (int)(scy + sinf(a1) * radius),
          (int)(scx + cosf(a2) * radius), (int)(scy + sinf(a2) * radius));
      }
      SDL_RenderDrawLine(app.renderer, (int)scx, (int)scy,
        (int)(scx + cosf(arc_start) * radius), (int)(scy + sinf(arc_start) * radius));
      SDL_RenderDrawLine(app.renderer, (int)scx, (int)scy,
        (int)(scx + cosf(arc_end_a) * radius), (int)(scy + sinf(arc_end_a) * radius));
    }
  }

  // Arc zones (slow + linger) — same LingerZone_t visual style
  for (int i = 0; i < MM_SCYTHE_ZONE_MAX; i++) {
    if (!mm_scythe_zones[i].active) continue;
    float fade = mm_scythe_zones[i].lifetime / mm_scythe_zones[i].max_lifetime;

    aColor_t zc;
    if (mm_scythe_zones[i].is_linger) { zc = (aColor_t){255, 140, 100, 200}; }
    else { zc = (aColor_t){255, 140, 80, 200}; }

    int za = (int)(fade * (float)zc.a * 0.4f);
    if (za < 0) za = 0;
    if (za > 255) za = 255;

    float zcx = mm_scythe_zones[i].cx;
    float zcy = mm_scythe_zones[i].cy;
    float zr = mm_scythe_zones[i].radius;
    float z_arc = mm_scythe_zones[i].arc_deg;
    float z_facing = mm_scythe_zones[i].facing_angle;
    float z_half = (z_arc * (float)PI / 180.0f) / 2.0f;
    float z_start = z_facing - z_half;

    if (z_arc >= 360.0f) {
      SDL_SetRenderDrawColor(app.renderer, zc.r, zc.g, zc.b, (uint8_t)za);
      // Filled circle via scanlines
      int ir = (int)zr;
      for (int row = -ir; row <= ir; row++) {
        int half_w = (int)sqrtf((float)(ir * ir - row * row));
        SDL_Rect line = { (int)zcx - half_w, (int)zcy + row, half_w * 2, 1 };
        SDL_RenderFillRect(app.renderer, &line);
      }
      // Ring outline
      int ring_a = za + 40; if (ring_a > 255) ring_a = 255;
      SDL_SetRenderDrawColor(app.renderer, zc.r, zc.g, zc.b, (uint8_t)ring_a);
      int segs = 24;
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          (int)(zcx + cosf(a1) * zr), (int)(zcy + sinf(a1) * zr),
          (int)(zcx + cosf(a2) * zr), (int)(zcy + sinf(a2) * zr));
      }
    } else {
      // Arc-shaped zone: triangle fan
      int segs = (int)(z_arc / 5.0f);
      if (segs < 8) segs = 8;
      if (segs > 48) segs = 48;
      float z_end = z_start + z_arc * (float)PI / 180.0f;
      for (int s = 0; s < segs; s++) {
        float a1 = z_start + (z_end - z_start) * (float)s / segs;
        float a2 = z_start + (z_end - z_start) * (float)(s + 1) / segs;
        aColor_t col = {zc.r, zc.g, zc.b, (uint8_t)za};
        a_DrawFilledTriangle((int)zcx, (int)zcy,
          (int)(zcx + cosf(a1) * zr), (int)(zcy + sinf(a1) * zr),
          (int)(zcx + cosf(a2) * zr), (int)(zcy + sinf(a2) * zr), col);
      }
      // Arc outline
      int ring_a = za + 40; if (ring_a > 255) ring_a = 255;
      SDL_SetRenderDrawColor(app.renderer, zc.r, zc.g, zc.b, (uint8_t)ring_a);
      for (int s = 0; s < segs; s++) {
        float a1 = z_start + (z_end - z_start) * (float)s / segs;
        float a2 = z_start + (z_end - z_start) * (float)(s + 1) / segs;
        SDL_RenderDrawLine(app.renderer,
          (int)(zcx + cosf(a1) * zr), (int)(zcy + sinf(a1) * zr),
          (int)(zcx + cosf(a2) * zr), (int)(zcy + sinf(a2) * zr));
      }
      // Edge lines
      SDL_RenderDrawLine(app.renderer, (int)zcx, (int)zcy,
        (int)(zcx + cosf(z_start) * zr), (int)(zcy + sinf(z_start) * zr));
      SDL_RenderDrawLine(app.renderer, (int)zcx, (int)zcy,
        (int)(zcx + cosf(z_end) * zr), (int)(zcy + sinf(z_end) * zr));
    }
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_scythe_cleanup(void)
{
  mm_scythe_swing_count = 0;
  mm_scythe_sweep_timer = 0.0f;
  mm_scythe_sweep_cx = 0.0f;
  mm_scythe_sweep_cy = 0.0f;
  mm_scythe_sweep_dir_x = 0.0f;
  mm_scythe_sweep_dir_y = 0.0f;
  mm_scythe_is_whirlwind = 0;
  mm_scythe_harvest_timer = 0.0f;
  mm_scythe_harvest_mult = 1.0f;
  memset(mm_scythe_zones, 0, sizeof(mm_scythe_zones));
}
