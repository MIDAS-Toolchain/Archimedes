#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include "fire_particles.h"
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Trail — dash ahead of player path, sweep perpendicular laying fire
// ============================================================================

#define MM_TRAIL_MAX_SEGMENTS  80
#define MM_TRAIL_EMBER_MAX     8
#define MM_TRAIL_DASH_SPEED    350.0f
#define MM_TRAIL_SWEEP_SPEED   280.0f
#define MM_TRAIL_PREDICT_TIME  0.6f
#define MM_TRAIL_SWEEP_DIST    250.0f
#define MM_TRAIL_ARRIVE_DIST   30.0f
#define MM_TRAIL_DASH_TIMEOUT  1.5f
#define MM_TRAIL_TICK_RATE     1.0f
#define MM_TRAIL_DROP_DISTANCE 8.0f

enum {
  TRAIL_AI_STRAFE,
  TRAIL_AI_DASH_AHEAD,
  TRAIL_AI_TRAIL_ACTIVE,
};

// --- Hostile trail segment pool ---

static TrailSegment_t mm_trail_segs[MM_TRAIL_MAX_SEGMENTS];
static TrailEmber_t   mm_trail_embers[MM_TRAIL_EMBER_MAX];

// Inferno Wake chain detonation state
#define MM_INFERNO_MAX_QUEUE 80
typedef struct { float x, y; } MmInfernoPos_t;
static MmInfernoPos_t mm_inferno_queue[MM_INFERNO_MAX_QUEUE];
static int   mm_inferno_queue_count = 0;
static int   mm_inferno_active = 0;
static int   mm_inferno_index = 0;
static float mm_inferno_timer = 0.0f;

#define MM_INFERNO_VISUAL_MAX 8
typedef struct {
  float x, y, radius;
  float timer;
  int active;
} MmInfernoBlast_t;
static MmInfernoBlast_t mm_inferno_blasts[MM_INFERNO_VISUAL_MAX];

// Trail laying state (static — only one mimic uses trail at a time)
static int   mm_trail_laying = 0;
static float mm_trail_active_timer = 0.0f;
static float mm_trail_last_x = 0.0f;
static float mm_trail_last_y = 0.0f;
static float mm_trail_player_cd = 0.0f;
static float mm_trail_sweep_traveled = 0.0f;

// ============================================================================
// Helpers
// ============================================================================

static void spawn_segment(float x, float y, float dx, float dy)
{
  for (int i = 0; i < MM_TRAIL_MAX_SEGMENTS; i++) {
    if (!mm_trail_segs[i].active) {
      float persist = weapons_get_trail_persist();
      mm_trail_segs[i] = (TrailSegment_t){
        .x = x, .y = y,
        .dir_x = dx, .dir_y = dy,
        .lifetime = persist, .max_lifetime = persist,
        .active = 1
      };
      return;
    }
  }
}

static void spawn_ember(float x, float y)
{
  int tier = upgrades_get_tier(UPG_TRAIL_EMBER_BURST);
  if (tier <= 0) return;
  static const float radii[4] = { 0.0f, 20.0f, 30.0f, 40.0f };
  for (int i = 0; i < MM_TRAIL_EMBER_MAX; i++) {
    if (!mm_trail_embers[i].active) {
      mm_trail_embers[i] = (TrailEmber_t){
        .x = x, .y = y,
        .radius = radii[tier],
        .timer = 0.2f,
        .has_damage = (tier >= 3) ? 1 : 0,
        .active = 1
      };
      return;
    }
  }
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
    int x1 = (int)(cx + cosf(a1) * r);
    int y1 = (int)(cy + sinf(a1) * r);
    int x2 = (int)(cx + cosf(a2) * r);
    int y2 = (int)(cy + sinf(a2) * r);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
  }
}

// ============================================================================
// AI State Machine + Update
// ============================================================================

void mimic_trail_update(Enemy_t* e, int index, float dt,
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
    case TRAIL_AI_STRAFE: {
      // Orbit player at ~250px range
      if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
      float ndx = dx / dist;
      float ndy = dy / dist;

      float steer_x = 0.0f, steer_y = 0.0f;

      // Radial spring toward preferred range
      if (dist > MIMIC_RANGE_FAR + 30.0f) {
        steer_x += -ndx * 0.5f;
        steer_y += -ndy * 0.5f;
      } else if (dist < MIMIC_RANGE_FAR - 30.0f) {
        steer_x += ndx * 0.8f;
        steer_y += ndy * 0.8f;
      }

      // Perpendicular strafe
      float perp_x = -ndy;
      float perp_y =  ndx;
      steer_x += perp_x * 0.6f;
      steer_y += perp_y * 0.6f;

      // Separation + screen edge
      steer_x += sep_x;
      steer_y += sep_y;
      float margin = 30.0f;
      if (cx < margin)                 steer_x += (margin - cx) / margin;
      if (cx > SCREEN_WIDTH - margin)  steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
      if (cy < margin)                 steer_y += (margin - cy) / margin;
      if (cy > SCREEN_HEIGHT - margin) steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

      float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
      if (slen > 0.1f) {
        e->vx = (steer_x / slen) * MIMIC_SPEED_TRAIL;
        e->vy = (steer_y / slen) * MIMIC_SPEED_TRAIL;
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

      // Transition: cooldown expired → predict path and dash ahead
      if (e->mm_weapon_cooldown <= 0.0f) {
        float pspd = sqrtf(pvx * pvx + pvy * pvy);

        if (pspd > 20.0f) {
          // Player moving: predict position ahead
          float pred_x = px + pvx * MM_TRAIL_PREDICT_TIME;
          float pred_y = py + pvy * MM_TRAIL_PREDICT_TIME;

          // Offset further along movement direction
          float move_nx = pvx / pspd;
          float move_ny = pvy / pspd;
          pred_x += move_nx * 80.0f;
          pred_y += move_ny * 80.0f;

          // Perpendicular offset (random left/right)
          float perp_nx = -move_ny;
          float perp_ny =  move_nx;
          float half_sweep = MM_TRAIL_SWEEP_DIST * 0.5f;
          int side = (rand() % 2) ? 1 : -1;
          pred_x += perp_nx * half_sweep * (float)side;
          pred_y += perp_ny * half_sweep * (float)side;

          // Store sweep direction (perpendicular to player movement,
          // opposite of the side offset so sweep crosses the path)
          e->mm_dash_dir_x = perp_nx * (float)(-side);
          e->mm_dash_dir_y = perp_ny * (float)(-side);

          // Clamp target to screen
          if (pred_x < 30.0f) pred_x = 30.0f;
          if (pred_x > SCREEN_WIDTH - 30.0f) pred_x = SCREEN_WIDTH - 30.0f;
          if (pred_y < 30.0f) pred_y = 30.0f;
          if (pred_y > SCREEN_HEIGHT - 30.0f) pred_y = SCREEN_HEIGHT - 30.0f;

          e->mm_spin_flank_x = pred_x;
          e->mm_spin_flank_y = pred_y;
        } else {
          // Player barely moving: pick random point ~100px from player
          float angle = RANDF(0.0f, 2.0f * (float)PI);
          e->mm_spin_flank_x = px + cosf(angle) * 100.0f;
          e->mm_spin_flank_y = py + sinf(angle) * 100.0f;

          // Random sweep direction perpendicular
          float perp_angle = angle + (float)PI * 0.5f;
          e->mm_dash_dir_x = cosf(perp_angle);
          e->mm_dash_dir_y = sinf(perp_angle);

          // Clamp target to screen
          if (e->mm_spin_flank_x < 30.0f) e->mm_spin_flank_x = 30.0f;
          if (e->mm_spin_flank_x > SCREEN_WIDTH - 30.0f)
            e->mm_spin_flank_x = SCREEN_WIDTH - 30.0f;
          if (e->mm_spin_flank_y < 30.0f) e->mm_spin_flank_y = 30.0f;
          if (e->mm_spin_flank_y > SCREEN_HEIGHT - 30.0f)
            e->mm_spin_flank_y = SCREEN_HEIGHT - 30.0f;
        }

        e->mm_spin_ai_state = TRAIL_AI_DASH_AHEAD;
        e->mm_spin_ai_timer = MM_TRAIL_DASH_TIMEOUT;
      }
      break;
    }

    case TRAIL_AI_DASH_AHEAD: {
      // Dash toward predicted player position
      ei_move_toward(e, e->mm_spin_flank_x - sz / 2.0f,
                     e->mm_spin_flank_y - sz / 2.0f,
                     MM_TRAIL_DASH_SPEED, sep_x, sep_y, 10.0f, dt);

      e->mm_spin_ai_timer -= dt;

      // Check arrival or timeout
      float fdx = cx - e->mm_spin_flank_x;
      float fdy = cy - e->mm_spin_flank_y;
      if (fdx * fdx + fdy * fdy < MM_TRAIL_ARRIVE_DIST * MM_TRAIL_ARRIVE_DIST ||
          e->mm_spin_ai_timer <= 0.0f) {
        // Start trail laying
        mm_trail_laying = 1;
        mm_trail_active_timer = weapons_get_trail_duration();
        mm_trail_last_x = cx;
        mm_trail_last_y = cy;
        mm_trail_sweep_traveled = 0.0f;

        e->mm_spin_ai_state = TRAIL_AI_TRAIL_ACTIVE;
        e->mm_spin_ai_timer = mm_trail_active_timer;

        fire_particles_spawn(cx, cy, 0.0f, 1.0f);
        game_audio_play_fire_hit();
      }
      break;
    }

    case TRAIL_AI_TRAIL_ACTIVE: {
      // Sweep across player's predicted path, dropping trail segments
      float sweep_speed = MM_TRAIL_SWEEP_SPEED;

      // Blazing Speed upgrade boosts sweep speed
      int blazing = upgrades_get_tier(UPG_TRAIL_BLAZING_SPEED);
      if (blazing > 0) {
        static const float mult[4] = { 1.0f, 1.15f, 1.25f, 1.40f };
        sweep_speed *= mult[blazing];
      }

      // Continuously steer toward a point ahead of the player's path
      // so the trail cuts across where they're actually heading
      float pspd = sqrtf(pvx * pvx + pvy * pvy);
      float target_x, target_y;
      if (pspd > 20.0f) {
        // Predict where player will be, offset perpendicular to stay ahead
        float move_nx = pvx / pspd;
        float move_ny = pvy / pspd;
        target_x = px + move_nx * 60.0f;
        target_y = py + move_ny * 60.0f;
        // Offset perpendicular (keep same side as initial dash dir)
        float perp_nx = -move_ny;
        float perp_ny =  move_nx;
        float dot = perp_nx * e->mm_dash_dir_x + perp_ny * e->mm_dash_dir_y;
        float side = (dot >= 0.0f) ? 1.0f : -1.0f;
        target_x += perp_nx * side * 40.0f;
        target_y += perp_ny * side * 40.0f;
      } else {
        // Player barely moving — sweep toward them
        target_x = px;
        target_y = py;
      }

      // Steer: blend current direction toward target
      float steer_dx = target_x - cx;
      float steer_dy = target_y - cy;
      float steer_dist = sqrtf(steer_dx * steer_dx + steer_dy * steer_dy);
      if (steer_dist > 1.0f) {
        float want_nx = steer_dx / steer_dist;
        float want_ny = steer_dy / steer_dist;
        // Blend: mostly keep sweep momentum, steer partially toward target
        float blend = 3.0f * dt; // steering rate
        e->mm_dash_dir_x += (want_nx - e->mm_dash_dir_x) * blend;
        e->mm_dash_dir_y += (want_ny - e->mm_dash_dir_y) * blend;
        // Re-normalize
        float dlen = sqrtf(e->mm_dash_dir_x * e->mm_dash_dir_x +
                           e->mm_dash_dir_y * e->mm_dash_dir_y);
        if (dlen > 0.01f) {
          e->mm_dash_dir_x /= dlen;
          e->mm_dash_dir_y /= dlen;
        }
      }

      e->vx = e->mm_dash_dir_x * sweep_speed;
      e->vy = e->mm_dash_dir_y * sweep_speed;
      e->x += e->vx * dt;
      e->y += e->vy * dt;

      // Clamp to screen
      if (e->x < 0) e->x = 0;
      if (e->y < 0) e->y = 0;
      if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
      if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

      // Update center after movement
      cx = e->x + sz / 2.0f;
      cy = e->y + sz / 2.0f;

      mm_trail_sweep_traveled += sweep_speed * dt;

      // Drop segments based on distance from last drop point
      float tdx = cx - mm_trail_last_x;
      float tdy = cy - mm_trail_last_y;
      float tdist = sqrtf(tdx * tdx + tdy * tdy);

      if (tdist >= MM_TRAIL_DROP_DISTANCE) {
        float ndx = tdx / tdist;
        float ndy = tdy / tdist;

        while (tdist >= MM_TRAIL_DROP_DISTANCE) {
          mm_trail_last_x += ndx * MM_TRAIL_DROP_DISTANCE;
          mm_trail_last_y += ndy * MM_TRAIL_DROP_DISTANCE;
          spawn_segment(mm_trail_last_x, mm_trail_last_y, ndx, ndy);
          tdist -= MM_TRAIL_DROP_DISTANCE;
        }
      }

      // Timer countdown
      e->mm_spin_ai_timer -= dt;
      mm_trail_active_timer -= dt;

      // End trail when duration expires
      if (e->mm_spin_ai_timer <= 0.0f) {
        mm_trail_laying = 0;
        e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);
        e->mm_spin_ai_state = TRAIL_AI_STRAFE;
      }
      break;
    }
  }

  // ==========================================================================
  // Update segment lifetimes
  // ==========================================================================

  float half_w = weapons_get_trail_width() * 0.5f;

  for (int i = 0; i < MM_TRAIL_MAX_SEGMENTS; i++) {
    if (!mm_trail_segs[i].active) continue;
    mm_trail_segs[i].lifetime -= dt;
    if (mm_trail_segs[i].lifetime <= 0.0f) {
      spawn_ember(mm_trail_segs[i].x, mm_trail_segs[i].y);

      // Queue for Inferno Wake chain detonation
      if (upgrades_get_tier(UPG_TRAIL_INFERNO_WAKE) > 0 && !mm_inferno_active &&
          mm_inferno_queue_count < MM_INFERNO_MAX_QUEUE) {
        mm_inferno_queue[mm_inferno_queue_count++] = (MmInfernoPos_t){
          mm_trail_segs[i].x, mm_trail_segs[i].y
        };
      }

      mm_trail_segs[i].active = 0;
    }
  }

  // ==========================================================================
  // Player damage — tick-based, 1 hit per second cooldown
  // ==========================================================================

  if (mm_trail_player_cd > 0.0f)
    mm_trail_player_cd -= dt;

  if (mm_trail_player_cd <= 0.0f) {
    for (int i = 0; i < MM_TRAIL_MAX_SEGMENTS; i++) {
      if (!mm_trail_segs[i].active) continue;
      float sdx = px - mm_trail_segs[i].x;
      float sdy = py - mm_trail_segs[i].y;
      if (sqrtf(sdx * sdx + sdy * sdy) < half_w + MIMIC_PLAYER_RADIUS) {
        player_take_damage(MIMIC_CONTACT_DAMAGE,
                           mm_trail_segs[i].x, mm_trail_segs[i].y,
                           ENEMY_TYPE_MIMIC);
        fire_particles_spawn(px, py, 0.0f, 1.0f);
        game_audio_play_fire_hit();
        mm_trail_player_cd = MM_TRAIL_TICK_RATE;
        break;
      }
    }
  }

  // ==========================================================================
  // Scorched Earth — slow player while standing in trail
  // ==========================================================================

  {
    int scorch_tier = upgrades_get_tier(UPG_TRAIL_SCORCHED_EARTH);
    if (scorch_tier > 0) {
      static const float slow[4] = { 1.0f, 0.8f, 0.7f, 0.6f };
      for (int i = 0; i < MM_TRAIL_MAX_SEGMENTS; i++) {
        if (!mm_trail_segs[i].active) continue;
        float sdx = px - mm_trail_segs[i].x;
        float sdy = py - mm_trail_segs[i].y;
        if (sqrtf(sdx * sdx + sdy * sdy) < half_w + MIMIC_PLAYER_RADIUS) {
          player_apply_slow(slow[scorch_tier], 0.2f);
          break;
        }
      }
    }
  }

  // ==========================================================================
  // Heat Mirage — pull player toward trail segments
  // ==========================================================================

  {
    int mirage_tier = upgrades_get_tier(UPG_TRAIL_HEAT_MIRAGE);
    // Base pull always active, stronger with player's heat mirage upgrade
    static const float base_range = 70.0f;
    static const float bonus_range[4] = { 0.0f, 10.0f, -5.0f, -20.0f };
    float range = base_range + bonus_range[mirage_tier];

    // Find nearest active segment to player (every other segment)
    float best_dist = range + MIMIC_PLAYER_RADIUS;
    int found_seg = 0;
    for (int s = 0; s < MM_TRAIL_MAX_SEGMENTS; s++) {
      if (!mm_trail_segs[s].active) continue;
      if (s % 2 != 0) continue;
      float sdx = mm_trail_segs[s].x - px;
      float sdy = mm_trail_segs[s].y - py;
      float sdist = sqrtf(sdx * sdx + sdy * sdy);
      if (sdist < best_dist) {
        best_dist = sdist;
        found_seg = 1;
      }
    }

    if (found_seg && best_dist > 1.0f) {
      // Slow player near trail segments (stronger when closer)
      float t = 1.0f - (best_dist / range);  // 1.0 at center, 0.0 at edge
      if (t < 0.0f) t = 0.0f;
      float slow = 1.0f - (0.30f * t);  // up to 30% slow at center
      player_apply_slow(slow, 0.2f);
    }
  }

  // ==========================================================================
  // Inferno Wake — chain detonation on segment expiry (damages player)
  // ==========================================================================

  if (!mm_inferno_active && mm_inferno_queue_count > 0) {
    mm_inferno_active = 1;
    mm_inferno_index = 0;
    mm_inferno_timer = 0.0f;
  }

  if (mm_inferno_active) {
    int inferno_tier = upgrades_get_tier(UPG_TRAIL_INFERNO_WAKE);
    static const float aoe_r[4] = { 0.0f, 30.0f, 40.0f, 50.0f };
    static const float delays[4] = { 0.0f, 0.05f, 0.03f, 0.03f };
    float blast_r = aoe_r[inferno_tier];
    float delay = delays[inferno_tier];

    mm_inferno_timer -= dt;
    while (mm_inferno_timer <= 0.0f && mm_inferno_index < mm_inferno_queue_count) {
      float ix = mm_inferno_queue[mm_inferno_index].x;
      float iy = mm_inferno_queue[mm_inferno_index].y;

      // AoE damage at player
      float idist_x = px - ix;
      float idist_y = py - iy;
      float idist = sqrtf(idist_x * idist_x + idist_y * idist_y);
      if (idist < blast_r + MIMIC_PLAYER_RADIUS) {
        player_take_damage(MIMIC_CONTACT_DAMAGE, ix, iy, ENEMY_TYPE_MIMIC);
        fire_particles_spawn(px, py, 0.0f, 1.0f);
      }

      // Spawn visual explosion
      for (int v = 0; v < MM_INFERNO_VISUAL_MAX; v++) {
        if (!mm_inferno_blasts[v].active) {
          mm_inferno_blasts[v] = (MmInfernoBlast_t){
            .x = ix, .y = iy,
            .radius = blast_r,
            .timer = 0.2f,
            .active = 1
          };
          break;
        }
      }

      mm_inferno_index++;
      mm_inferno_timer += delay;
    }

    if (mm_inferno_index >= mm_inferno_queue_count) {
      mm_inferno_active = 0;
      mm_inferno_queue_count = 0;
    }
  }

  // ==========================================================================
  // Ember bursts — T3 damages player
  // ==========================================================================

  for (int i = 0; i < MM_TRAIL_EMBER_MAX; i++) {
    if (!mm_trail_embers[i].active) continue;
    mm_trail_embers[i].timer -= dt;
    if (mm_trail_embers[i].timer <= 0.0f) {
      mm_trail_embers[i].active = 0;
      continue;
    }

    if (mm_trail_embers[i].has_damage) {
      mm_trail_embers[i].has_damage = 0;
      float er_x = mm_trail_embers[i].x;
      float er_y = mm_trail_embers[i].y;
      float er_r = mm_trail_embers[i].radius;
      float edx = px - er_x;
      float edy = py - er_y;
      if (sqrtf(edx * edx + edy * edy) < er_r + MIMIC_PLAYER_RADIUS) {
        player_take_damage(MIMIC_CONTACT_DAMAGE, er_x, er_y, ENEMY_TYPE_MIMIC);
        fire_particles_spawn(px, py, 0.0f, 1.0f);
      }
    }
  }

  // Update inferno blast visuals
  for (int i = 0; i < MM_INFERNO_VISUAL_MAX; i++) {
    if (!mm_inferno_blasts[i].active) continue;
    mm_inferno_blasts[i].timer -= dt;
    if (mm_inferno_blasts[i].timer <= 0.0f)
      mm_inferno_blasts[i].active = 0;
  }
}

// ============================================================================
// Draw
// ============================================================================

void mimic_trail_draw(Enemy_t* e)
{
  (void)e;

  float tw = weapons_get_trail_width();
  int half_w = (int)(tw * 0.5f);
  int half_h = 4;

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  // Draw trail segments — hostile red-orange (redder than player trail)
  for (int i = 0; i < MM_TRAIL_MAX_SEGMENTS; i++) {
    if (!mm_trail_segs[i].active) continue;
    float ratio = mm_trail_segs[i].lifetime / mm_trail_segs[i].max_lifetime;
    int r, g, b, a;

    if (ratio > 0.7f) {
      // Fresh: bright hostile orange-red
      r = 255; g = 100; b = 50; a = 200;
    } else if (ratio > 0.3f) {
      // Mid: darker red
      r = 200; g = 50; b = 30; a = 150;
    } else {
      // Fading: dark red
      r = 160; g = 30; b = 20;
      a = (int)(150.0f * (ratio / 0.3f));
      if (a < 0) a = 0;
    }

    float sx = mm_trail_segs[i].x;
    float sy = mm_trail_segs[i].y;

    // Rotated quad along movement direction
    float ddx = mm_trail_segs[i].dir_x;
    float ddy = mm_trail_segs[i].dir_y;
    float dlen = sqrtf(ddx * ddx + ddy * ddy);
    if (dlen < 0.01f) { ddx = 1.0f; ddy = 0.0f; dlen = 1.0f; }
    float nx = ddx / dlen;
    float ny = ddy / dlen;
    float perp_x = -ny;
    float perp_y = nx;

    float hw = (float)half_w;
    float hh = (float)half_h;

    // 4 corners: along +-half_w in dir, +-half_h perpendicular
    float ax = sx + nx * hw + perp_x * hh;
    float ay = sy + ny * hw + perp_y * hh;
    float bx = sx + nx * hw - perp_x * hh;
    float by = sy + ny * hw - perp_y * hh;
    float ccx = sx - nx * hw - perp_x * hh;
    float ccy = sy - ny * hw - perp_y * hh;
    float d_x = sx - nx * hw + perp_x * hh;
    float d_y = sy - ny * hw + perp_y * hh;

    aColor_t seg_col = {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a};
    a_DrawFilledTriangle((int)ax, (int)ay, (int)bx, (int)by,
                         (int)ccx, (int)ccy, seg_col);
    a_DrawFilledTriangle((int)ax, (int)ay, (int)ccx, (int)ccy,
                         (int)d_x, (int)d_y, seg_col);

    // Bright core line on fresh segments
    if (ratio > 0.4f) {
      int core_a = (int)(120.0f * ratio);
      float core_hw = hw - 2.0f;
      if (core_hw < 1.0f) core_hw = 1.0f;
      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.renderer, 255, 180, 60, (uint8_t)core_a);
      SDL_RenderDrawLine(app.renderer,
        (int)(sx - nx * core_hw), (int)(sy - ny * core_hw),
        (int)(sx + nx * core_hw), (int)(sy + ny * core_hw));
    }
  }

  // Ember bursts — expanding orange circles
  for (int i = 0; i < MM_TRAIL_EMBER_MAX; i++) {
    if (!mm_trail_embers[i].active) continue;
    float progress = 1.0f - (mm_trail_embers[i].timer / 0.2f);
    float cur_r = mm_trail_embers[i].radius * progress;
    int ea = (int)(180.0f * (1.0f - progress));
    if (ea < 0) ea = 0;

    a_DrawFilledCircle((int)mm_trail_embers[i].x, (int)mm_trail_embers[i].y,
                       (int)cur_r,
                       (aColor_t){255, 120, 30, (uint8_t)ea});
  }

  // Heat Mirage: reddish pull rings + 4 inward arrows per even segment (always active)
  {
    int mirage_tier = upgrades_get_tier(UPG_TRAIL_HEAT_MIRAGE);
    static const float base_range_vis = 70.0f;
    static const float bonus_range_vis[4] = { 0.0f, 10.0f, -5.0f, -20.0f };
    float range = base_range_vis + bonus_range_vis[mirage_tier];

    for (int i = 0; i < MM_TRAIL_MAX_SEGMENTS; i++) {
      if (!mm_trail_segs[i].active) continue;
      if (i % 2 != 0) continue;

      float sx = mm_trail_segs[i].x;
      float sy = mm_trail_segs[i].y;
      float ratio = mm_trail_segs[i].lifetime / mm_trail_segs[i].max_lifetime;
      int base_a = (int)(60.0f * ratio);
      if (base_a < 8) continue;

      // Reddish pull ring
      draw_ring(sx, sy, range, 255, 120, 80, (uint8_t)(base_a / 2));

      // 4 inward arrows animating from ring toward center
      float phase = 1.0f - fmodf(mm_trail_segs[i].lifetime * 3.0f, 1.0f);
      for (int a = 0; a < 4; a++) {
        float angle = (float)a / 4.0f * 2.0f * (float)PI;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);
        float tip_r = range * (1.0f - phase);
        float tail_r = tip_r + range * 0.2f;
        if (tail_r > range) tail_r = range;

        SDL_SetRenderDrawColor(app.renderer, 255, 140, 100,
                               (uint8_t)(base_a * 2 / 3));
        SDL_RenderDrawLine(app.renderer,
          (int)(sx + cos_a * tail_r), (int)(sy + sin_a * tail_r),
          (int)(sx + cos_a * tip_r),  (int)(sy + sin_a * tip_r));
      }
    }
  }

  // Inferno Wake blast visuals — expanding orange ring
  for (int i = 0; i < MM_INFERNO_VISUAL_MAX; i++) {
    if (!mm_inferno_blasts[i].active) continue;
    float p = 1.0f - (mm_inferno_blasts[i].timer / 0.2f);
    float cur_r = mm_inferno_blasts[i].radius * (0.3f + 0.7f * p);
    int alpha = (int)(180.0f * (1.0f - p));
    if (alpha < 0) alpha = 0;

    draw_ring(mm_inferno_blasts[i].x, mm_inferno_blasts[i].y,
              cur_r, 255, 100, 20, (uint8_t)alpha);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_trail_cleanup(void)
{
  memset(mm_trail_segs, 0, sizeof(mm_trail_segs));
  memset(mm_trail_embers, 0, sizeof(mm_trail_embers));
  memset(mm_inferno_queue, 0, sizeof(mm_inferno_queue));
  memset(mm_inferno_blasts, 0, sizeof(mm_inferno_blasts));
  mm_inferno_queue_count = 0;
  mm_inferno_active = 0;
  mm_inferno_index = 0;
  mm_inferno_timer = 0.0f;
  mm_trail_laying = 0;
  mm_trail_active_timer = 0.0f;
  mm_trail_last_x = 0.0f;
  mm_trail_last_y = 0.0f;
  mm_trail_player_cd = 0.0f;
  mm_trail_sweep_traveled = 0.0f;
}
