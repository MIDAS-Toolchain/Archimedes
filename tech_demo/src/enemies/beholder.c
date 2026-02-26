#include "beholder.h"
#include "enemy_internal.h"
#include "drops.h"
#include "pickups.h"
#include <stdlib.h>
#include <string.h>

extern aApp_t app;

// ============================================================================
// Beholder AI Constants
// ============================================================================

#define BEHOLDER_BASE_HP         8
#define BEHOLDER_BASE_SHIELD     3
#define BEHOLDER_SHIELD_GROWTH   2
#define BEHOLDER_BEAM_DAMAGE     20
#define BEHOLDER_BEAM_WIDTH      6.0f
#define BEHOLDER_SWEEP_ARC       1.2f
#define BEHOLDER_SWEEP_RATE      0.7f
#define BEHOLDER_TELEGRAPH_TIME  0.8f
#define BEHOLDER_BEAM_TIME       1.2f
#define BEHOLDER_AIM_MISS_ANGLE  0.35f
#define BEHOLDER_FIRE_STRAFE_SPD 40.0f
#define BEHOLDER_TRAIL_COUNT     8
#define BEHOLDER_TRAIL_INTERVAL  0.04f
#define BEHOLDER_CHANNEL_TIME    3.0f
#define BEHOLDER_FLEE_TIME       0.3f
#define BEHOLDER_FLEE_SPEED      350.0f
#define BEHOLDER_PREFERRED_RANGE 250.0f
#define BEHOLDER_STRAFE_SPEED    80.0f
#define BEHOLDER_ENTER_SPEED     120.0f
#define BEHOLDER_SEPARATION      150.0f
#define BEHOLDER_COOLDOWN_1      4.0f
#define BEHOLDER_COOLDOWN_2      3.0f
#define BEHOLDER_COOLDOWN_3      2.0f

// ============================================================================
// Helpers
// ============================================================================

static int beholder_spot_is_dangerous(float x, float y)
{
  float hx, hy;
  if (weapons_get_nearest_hazard(x, y, 60.0f, &hx, &hy)) return 1;
  float tx, ty;
  if (weapons_get_nearest_turret(x, y, 120.0f, &tx, &ty)) return 1;
  return 0;
}

static void beholder_start_flee(Enemy_t* e)
{
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float px = player_get_x() + 16.0f;
  float py = player_get_y() + 16.0f;
  float dx = cx - px;
  float dy = cy - py;
  float len = sqrtf(dx * dx + dy * dy);
  float base_angle;
  if (len > 0.1f) {
    base_angle = atan2f(dy, dx);
  } else {
    base_angle = 0.0f;
  }

  float flee_dist = BEHOLDER_FLEE_SPEED * BEHOLDER_FLEE_TIME;

  float best_angle = base_angle;
  for (int attempt = 0; attempt < 8; attempt++) {
    static const float offsets[8] = { 0.0f, 0.785f, -0.785f, 1.571f, -1.571f, 2.356f, -2.356f, 3.14159f };
    float angle = base_angle + offsets[attempt];
    float land_x = cx + cosf(angle) * flee_dist;
    float land_y = cy + sinf(angle) * flee_dist;
    float sz = ei_get_stats(e)->size;
    if (land_x < sz) land_x = sz;
    if (land_x > SCREEN_WIDTH - sz) land_x = SCREEN_WIDTH - sz;
    if (land_y < sz) land_y = sz;
    if (land_y > SCREEN_HEIGHT - sz) land_y = SCREEN_HEIGHT - sz;

    if (!beholder_spot_is_dangerous(land_x, land_y)) {
      best_angle = angle;
      break;
    }
  }

  e->bh_flee_vx = cosf(best_angle) * BEHOLDER_FLEE_SPEED;
  e->bh_flee_vy = sinf(best_angle) * BEHOLDER_FLEE_SPEED;
  e->state = ENEMY_STATE_BH_FLEEING;
  e->bh_state_timer = BEHOLDER_FLEE_TIME;
  game_audio_stop_beholder_windup(e->bh_windup_channel);
  e->bh_windup_channel = -1;
  game_audio_stop_beholder_beam();
}

// ============================================================================
// Spawn
// ============================================================================

int beholder_spawn(int hp, int shield)
{
  int side = rand() % 4;
  float spawn_x, spawn_y;
  if (side == 0) {
    spawn_x = RANDF(0, SCREEN_WIDTH);
    spawn_y = -28;
  } else if (side == 1) {
    spawn_x = SCREEN_WIDTH + 28;
    spawn_y = RANDF(0, SCREEN_HEIGHT);
  } else if (side == 2) {
    spawn_x = RANDF(0, SCREEN_WIDTH);
    spawn_y = SCREEN_HEIGHT + 28;
  } else {
    spawn_x = -28;
    spawn_y = RANDF(0, SCREEN_HEIGHT);
  }

  int idx = enemy_spawn(spawn_x, spawn_y, ENEMY_TYPE_BEHOLDER, 1.0f, hp - BEHOLDER_BASE_HP);
  if (idx < 0) return -1;

  Enemy_t* e = &enemies[idx];
  e->bh_starting_shield = shield;
  e->bh_shield = shield;
  e->state = ENEMY_STATE_BH_ENTERING;
  e->bh_state_timer = 0.0f;
  e->bh_cooldown = BEHOLDER_COOLDOWN_1;
  e->bh_orbit_dir = (rand() % 2) ? 1 : -1;
  e->bh_attack_count = 0;
  e->bh_tendril_phase = RANDF(0.0f, 6.28f);
  e->bh_blink_timer = RANDF(2.0f, 3.0f);
  e->bh_beam_has_hit = 0;
  e->bh_shield_broke_during_fire = 0;
  e->bh_charge_progress = 0.0f;
  e->bh_trail_count = 0;
  e->bh_aim_offset = 0.0f;
  e->bh_windup_channel = -1;
  e->bh_last_pupil_ox = 0.0f;
  e->bh_last_pupil_oy = 0.0f;
  e->bh_predicted_angle = 0.0f;
  return idx;
}

// ============================================================================
// Hit Handling
// ============================================================================

int beholder_on_hit(Enemy_t* e, float bullet_vx, float bullet_vy,
                    int killed, float knockback_dist)
{
  if (e->bh_shield > 0) {
    e->hit_count--;
    e->bh_shield--;
    if (e->bh_shield <= 0) {
      game_audio_play_beholder_shield_break();
      if (e->state == ENEMY_STATE_BH_AIMING || e->state == ENEMY_STATE_BH_FIRING) {
        e->bh_shield_broke_during_fire = 1;
      } else {
        beholder_start_flee(e);
      }
    } else {
      game_audio_play_beholder_shield_hit();
    }
    return 1;
  }
  if (killed) {
    e->vx = bullet_vx;
    e->vy = bullet_vy;
    e->bh_shield = 0;
    game_audio_play_beholder_death();
    ei_start_knockback(e, bullet_vx, bullet_vy,
      knockback_dist * (KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE)));
  } else {
    game_audio_play_beholder_health_hit();
  }
  return 1;
}

void beholder_on_knockback(Enemy_t* e)
{
  game_audio_stop_beholder_windup(e->bh_windup_channel);
  e->bh_windup_channel = -1;
  game_audio_stop_beholder_beam();
}

// ============================================================================
// State Handlers
// ============================================================================

static void update_bh_entering(Enemy_t* e, int i, float dt,
                                float player_x, float player_y)
{
  (void)i;
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float dx = player_x - cx;
  float dy = player_y - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist > BEHOLDER_PREFERRED_RANGE + 50.0f) {
    if (dist > 0.1f) {
      e->vx = (dx / dist) * BEHOLDER_ENTER_SPEED;
      e->vy = (dy / dist) * BEHOLDER_ENTER_SPEED;
    }
  } else if (dist < BEHOLDER_PREFERRED_RANGE - 50.0f) {
    if (dist > 0.1f) {
      e->vx = -(dx / dist) * BEHOLDER_ENTER_SPEED;
      e->vy = -(dy / dist) * BEHOLDER_ENTER_SPEED;
    }
  } else {
    e->state = ENEMY_STATE_BH_STRAFING;
    e->bh_cooldown = 0.0f;
    return;
  }

  float haz_x, haz_y, tav_x, tav_y;
  ei_calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);
  ei_calc_turret_avoidance(cx, cy, TURRET_RANGE * 1.3f, 1.5f, &tav_x, &tav_y);
  e->vx += (haz_x + tav_x) * BEHOLDER_ENTER_SPEED;
  e->vy += (haz_y + tav_y) * BEHOLDER_ENTER_SPEED;

  float enter_slow = ei_get_slow_multiplier(e);
  e->x += e->vx * dt * enter_slow;
  e->y += e->vy * dt * enter_slow;
}

static void update_bh_strafing(Enemy_t* e, int i, float dt,
                                float player_x, float player_y,
                                float player_vx, float player_vy)
{
  (void)player_vx;
  (void)player_vy;
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float dx = cx - player_x;
  float dy = cy - player_y;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  float radial_x = 0, radial_y = 0;
  if (dist > BEHOLDER_PREFERRED_RANGE + 30.0f) {
    radial_x = -ndx * 0.5f;
    radial_y = -ndy * 0.5f;
  } else if (dist < BEHOLDER_PREFERRED_RANGE - 30.0f) {
    radial_x = ndx * 0.8f;
    radial_y = ndy * 0.8f;
  }

  float perp_x = -ndy * (float)e->bh_orbit_dir;
  float perp_y = ndx * (float)e->bh_orbit_dir;

  float steer_x = perp_x + radial_x;
  float steer_y = perp_y + radial_y;

  // Repel from other beholders
  for (int j = 0; j < max_enemies; j++) {
    if (j == i || !enemies[j].active) continue;
    if (enemies[j].type != ENEMY_TYPE_BEHOLDER) continue;
    if (enemies[j].state == ENEMY_STATE_CORPSE) continue;
    float or = ei_get_stats(&enemies[j])->radius;
    float odx = cx - (enemies[j].x + or);
    float ody = cy - (enemies[j].y + or);
    float od = sqrtf(odx * odx + ody * ody);
    if (od < BEHOLDER_SEPARATION && od > 0.1f) {
      float strength = (BEHOLDER_SEPARATION - od) / BEHOLDER_SEPARATION;
      steer_x += (odx / od) * strength;
      steer_y += (ody / od) * strength;
    }
  }

  // Screen edge repulsion
  float margin = 30.0f;
  if (cx < margin)                   steer_x += (margin - cx) / margin;
  if (cx > SCREEN_WIDTH - margin)    steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
  if (cy < margin)                   steer_y += (margin - cy) / margin;
  if (cy > SCREEN_HEIGHT - margin)   steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

  float haz_x, haz_y, tav_x, tav_y;
  ei_calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);
  ei_calc_turret_avoidance(cx, cy, TURRET_RANGE * 1.3f, 1.5f, &tav_x, &tav_y);
  steer_x += haz_x + tav_x;
  steer_y += haz_y + tav_y;

  float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
  if (slen > 0.1f) {
    e->vx = (steer_x / slen) * BEHOLDER_STRAFE_SPEED;
    e->vy = (steer_y / slen) * BEHOLDER_STRAFE_SPEED;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  float strafe_slow = ei_get_slow_multiplier(e);
  e->x += e->vx * dt * strafe_slow;
  e->y += e->vy * dt * strafe_slow;

  float sz = ei_get_stats(e)->size;
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
  if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

  e->bh_cooldown -= dt;
  if (e->bh_cooldown <= 0.0f) {
    float telegraph = BEHOLDER_TELEGRAPH_TIME;
    e->bh_state_timer = telegraph;
    e->state = ENEMY_STATE_BH_AIMING;
    e->vx = 0.0f;
    e->vy = 0.0f;
    e->bh_windup_channel = game_audio_play_beholder_windup();

    float to_player = atan2f(player_y - cy, player_x - cx);
    e->bh_aim_offset = ((rand() % 2) ? 1.0f : -1.0f) * BEHOLDER_AIM_MISS_ANGLE;
    e->bh_predicted_angle = to_player + e->bh_aim_offset;
  }
}

static void update_bh_aiming(Enemy_t* e, int i, float dt,
                              float player_x, float player_y,
                              float player_vx, float player_vy)
{
  (void)i;
  (void)player_vx;
  (void)player_vy;
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float adx = cx - player_x;
  float ady = cy - player_y;
  float adist = sqrtf(adx * adx + ady * ady);
  if (adist > 0.1f && fabsf(adist - BEHOLDER_PREFERRED_RANGE) > 20.0f) {
    float drift = (adist > BEHOLDER_PREFERRED_RANGE) ? -20.0f : 20.0f;
    e->vx = (adx / adist) * drift;
    e->vy = (ady / adist) * drift;
    float aim_slow = ei_get_slow_multiplier(e);
    e->x += e->vx * dt * aim_slow;
    e->y += e->vy * dt * aim_slow;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  float telegraph = BEHOLDER_TELEGRAPH_TIME;
  e->bh_charge_progress = 1.0f - (e->bh_state_timer / telegraph);
  if (e->bh_charge_progress < 0.0f) e->bh_charge_progress = 0.0f;
  if (e->bh_charge_progress > 1.0f) e->bh_charge_progress = 1.0f;

  cx = e->x + radius;
  cy = e->y + radius;
  {
    float fresh_angle = atan2f(player_y - cy, player_x - cx) + e->bh_aim_offset;
    float diff = fresh_angle - e->bh_predicted_angle;
    while (diff > (float)PI)  diff -= 2.0f * (float)PI;
    while (diff < -(float)PI) diff += 2.0f * (float)PI;
    float max_turn = 1.5f * dt;
    if (fabsf(diff) < max_turn)
      e->bh_predicted_angle += diff;
    else
      e->bh_predicted_angle += (diff > 0 ? 1.0f : -1.0f) * max_turn;
  }

  e->bh_state_timer -= dt;
  if (e->bh_state_timer <= 0.0f) {
    e->bh_beam_start_angle = e->bh_predicted_angle;
    e->bh_beam_angle = e->bh_predicted_angle;
    e->bh_beam_swept = 0.0f;
    e->bh_beam_has_hit = 0;
    e->bh_trail_count = 0;
    e->bh_charge_progress = 0.0f;
    e->bh_state_timer = BEHOLDER_BEAM_TIME;
    e->state = ENEMY_STATE_BH_FIRING;
    game_audio_stop_beholder_windup(e->bh_windup_channel);
    e->bh_windup_channel = -1;
    game_audio_play_beholder_beam();
  }
}

static void update_bh_firing(Enemy_t* e, int i, float dt,
                              float player_x, float player_y,
                              float player_vx, float player_vy)
{
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  // Strafe while firing
  {
    float fdx = cx - player_x;
    float fdy = cy - player_y;
    float fdist = sqrtf(fdx * fdx + fdy * fdy);
    if (fdist < 0.1f) { fdx = 1.0f; fdy = 0.0f; fdist = 1.0f; }
    float fndx = fdx / fdist;
    float fndy = fdy / fdist;
    float fperp_x = -fndy * (float)e->bh_orbit_dir;
    float fperp_y = fndx * (float)e->bh_orbit_dir;
    float frad_x = 0, frad_y = 0;
    if (fdist > BEHOLDER_PREFERRED_RANGE + 40.0f) {
      frad_x = -fndx * 0.4f;
      frad_y = -fndy * 0.4f;
    } else if (fdist < BEHOLDER_PREFERRED_RANGE - 40.0f) {
      frad_x = fndx * 0.6f;
      frad_y = fndy * 0.6f;
    }
    float fsx = fperp_x + frad_x;
    float fsy = fperp_y + frad_y;
    float fslen = sqrtf(fsx * fsx + fsy * fsy);
    if (fslen > 0.1f) {
      e->vx = (fsx / fslen) * BEHOLDER_FIRE_STRAFE_SPD;
      e->vy = (fsy / fslen) * BEHOLDER_FIRE_STRAFE_SPD;
    }
    float fire_slow = ei_get_slow_multiplier(e);
    e->x += e->vx * dt * fire_slow;
    e->y += e->vy * dt * fire_slow;
    float sz = ei_get_stats(e)->size;
    if (e->x < 0) e->x = 0;
    if (e->y < 0) e->y = 0;
    if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
    if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;
    cx = e->x + radius;
    cy = e->y + radius;
  }

  // Fixed-direction sweep
  (void)player_vx;
  (void)player_vy;
  float sweep_dir = (e->bh_aim_offset > 0) ? -1.0f : 1.0f;
  float sweep_rate = BEHOLDER_SWEEP_RATE - progress_get_beholder_aim_slow();
  if (sweep_rate < 0.25f) sweep_rate = 0.25f;
  float step = sweep_dir * sweep_rate * dt;

  if (e->bh_beam_swept + fabsf(step) > BEHOLDER_SWEEP_ARC) {
    float remaining = BEHOLDER_SWEEP_ARC - e->bh_beam_swept;
    if (remaining < 0) remaining = 0;
    step = sweep_dir * remaining;
  }

  float old_angle = e->bh_beam_angle;

  e->bh_beam_swept += fabsf(step);
  e->bh_beam_angle += step;

  // Push current angle into trail ring buffer
  if (e->bh_trail_count < BEHOLDER_TRAIL_COUNT) {
    e->bh_trail_angles[e->bh_trail_count] = old_angle;
    e->bh_trail_alphas[e->bh_trail_count] = 1.0f;
    e->bh_trail_count++;
  } else {
    for (int t = 0; t < BEHOLDER_TRAIL_COUNT - 1; t++) {
      e->bh_trail_angles[t] = e->bh_trail_angles[t + 1];
      e->bh_trail_alphas[t] = e->bh_trail_alphas[t + 1];
    }
    e->bh_trail_angles[BEHOLDER_TRAIL_COUNT - 1] = old_angle;
    e->bh_trail_alphas[BEHOLDER_TRAIL_COUNT - 1] = 1.0f;
  }
  for (int t = 0; t < e->bh_trail_count; t++) {
    e->bh_trail_alphas[t] -= dt * 4.0f;
    if (e->bh_trail_alphas[t] < 0.0f) e->bh_trail_alphas[t] = 0.0f;
  }

  // Beam collision
  float beam_dx = cosf(e->bh_beam_angle);
  float beam_dy = sinf(e->bh_beam_angle);
  float to_player_x = player_x - cx;
  float to_player_y = player_y - cy;
  float proj = to_player_x * beam_dx + to_player_y * beam_dy;
  if (proj > 0.0f) {
    float perp_dist = fabsf(to_player_x * beam_dy - to_player_y * beam_dx);
    float hit_threshold = BEHOLDER_BEAM_WIDTH / 2.0f + 16.0f;
    if (perp_dist < hit_threshold && !e->bh_beam_has_hit) {
      int dmg = BEHOLDER_BEAM_DAMAGE - progress_get_beholder_dmg_reduction();
      if (dmg < 1) dmg = 1;
      player_take_damage(dmg, cx, cy, ENEMY_TYPE_BEHOLDER);
      e->bh_beam_has_hit = 1;
    }
  }

  // Beam sound fade
  {
    float bp = 1.0f - (e->bh_state_timer / BEHOLDER_BEAM_TIME);
    if (bp < 0.0f) bp = 0.0f;
    if (bp > 1.0f) bp = 1.0f;
    float vol_fade = 1.0f;
    if (bp < 0.15f)
      vol_fade = bp / 0.15f;
    else if (bp > 0.80f)
      vol_fade = (1.0f - bp) / 0.20f;
    if (vol_fade < 0.0f) vol_fade = 0.0f;
    int beam_vol = (int)(72.0f * vol_fade);
    a_AudioSetChannelVolume(5, beam_vol);
  }

  e->bh_state_timer -= dt;
  if (e->bh_state_timer <= 0.0f) {
    game_audio_stop_beholder_beam();
    e->bh_attack_count++;
    if (e->bh_shield_broke_during_fire) {
      beholder_start_flee(e);
    } else {
      e->state = ENEMY_STATE_BH_STRAFING;
      if (e->bh_attack_count <= 1) {
        e->bh_cooldown = BEHOLDER_COOLDOWN_1;
      } else if (e->bh_attack_count == 2) {
        e->bh_cooldown = BEHOLDER_COOLDOWN_2;
      } else {
        e->bh_cooldown = BEHOLDER_COOLDOWN_3;
      }
    }
  }

  (void)i;
}

static void update_bh_fleeing(Enemy_t* e, int i, float dt,
                               float player_x, float player_y)
{
  (void)i;
  (void)player_x;
  (void)player_y;

  float slow = ei_get_slow_multiplier(e);
  e->vx = e->bh_flee_vx * slow;
  e->vy = e->bh_flee_vy * slow;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  float sz = ei_get_stats(e)->size;
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
  if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

  e->bh_state_timer -= dt;
  if (e->bh_state_timer <= 0.0f) {
    e->state = ENEMY_STATE_BH_CHANNELING;
    e->bh_state_timer = BEHOLDER_CHANNEL_TIME;
    e->vx = 0.0f;
    e->vy = 0.0f;
  }
}

static void update_bh_channeling(Enemy_t* e, int i, float dt,
                                  float player_x, float player_y)
{
  (void)i;
  (void)player_x;
  (void)player_y;

  e->vx = 0.0f;
  e->vy = 0.0f;

  e->bh_state_timer -= dt;
  if (e->bh_state_timer <= 0.0f) {
    e->bh_shield = e->bh_starting_shield;
    e->bh_shield_broke_during_fire = 0;
    e->state = ENEMY_STATE_BH_STRAFING;
    if (e->bh_attack_count <= 1) {
      e->bh_cooldown = BEHOLDER_COOLDOWN_1;
    } else if (e->bh_attack_count == 2) {
      e->bh_cooldown = BEHOLDER_COOLDOWN_2;
    } else {
      e->bh_cooldown = BEHOLDER_COOLDOWN_3;
    }
  }
}

// ============================================================================
// Update Dispatch
// ============================================================================

void beholder_update(Enemy_t* e, int index, float dt,
                     float player_x, float player_y,
                     float player_vx, float player_vy)
{
  switch (e->state) {
    case ENEMY_STATE_BH_ENTERING:   update_bh_entering(e, index, dt, player_x, player_y); break;
    case ENEMY_STATE_BH_STRAFING:   update_bh_strafing(e, index, dt, player_x, player_y, player_vx, player_vy); break;
    case ENEMY_STATE_BH_AIMING:     update_bh_aiming(e, index, dt, player_x, player_y, player_vx, player_vy); break;
    case ENEMY_STATE_BH_FIRING:     update_bh_firing(e, index, dt, player_x, player_y, player_vx, player_vy); break;
    case ENEMY_STATE_BH_FLEEING:    update_bh_fleeing(e, index, dt, player_x, player_y); break;
    case ENEMY_STATE_BH_CHANNELING: update_bh_channeling(e, index, dt, player_x, player_y); break;
    default: break;
  }
}

// ============================================================================
// Contact Damage + Cosmetics
// ============================================================================

void beholder_contact_damage(Enemy_t* e, float player_x, float player_y)
{
  if (e->state == ENEMY_STATE_CORPSE) return;
  if (ei_resolve_player_collision(e, player_x, player_y)) {
    const EnemyStats_t* stats = ei_get_stats(e);
    int dmg = stats->damage - progress_get_beholder_dmg_reduction();
    if (dmg < 1) dmg = 1;
    player_take_damage(dmg, e->x + stats->radius, e->y + stats->radius, ENEMY_TYPE_BEHOLDER);
  }
}

void beholder_tick_cosmetics(Enemy_t* e, float dt)
{
  if (e->state == ENEMY_STATE_CORPSE) return;
  e->bh_tendril_phase += dt * 4.0f;
  e->bh_blink_timer -= dt;
  if (e->bh_blink_timer < 0.0f) {
    e->bh_blink_timer = RANDF(2.0f, 3.0f);
  }
}

// ============================================================================
// Drawing
// ============================================================================

void beholder_draw(Enemy_t* e, int r, int g, int b, int alpha)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  aColor_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)alpha};

  float sz = stats->size;
  float bcx = e->x + sz / 2.0f;
  float bcy = e->y + sz / 2.0f;
  float px = player_get_x() + 16.0f;
  float py = player_get_y() + 16.0f;

  // Tendrils
  int bh_dead = (e->state == ENEMY_STATE_CORPSE || e->state == ENEMY_STATE_HIT_KNOCKBACK);
  float aim_angle = atan2f(py - bcy, px - bcx);
  float tendril_retract = 1.0f;
  if (e->state == ENEMY_STATE_BH_CHANNELING) {
    float progress = 1.0f - (e->bh_state_timer / BEHOLDER_CHANNEL_TIME);
    tendril_retract = 1.0f - progress;
    if (tendril_retract < 0.0f) tendril_retract = 0.0f;
  }
  if (bh_dead) tendril_retract = 0.0f;
  if (!bh_dead) for (int t = 0; t < 4; t++) {
    float base_angle = aim_angle + ((float)t * 0.5f * (float)PI) + 0.785f;
    float wiggle = 0.087f * sinf(e->bh_tendril_phase + (float)t * 1.5f);
    float ta = base_angle + wiggle;
    for (int seg = 0; seg < 3; seg++) {
      float ext = (6.0f + (float)seg * 5.0f) * tendril_retract;
      float tx = bcx + cosf(ta) * (14.0f + ext);
      float ty = bcy + sinf(ta) * (14.0f + ext);
      a_DrawFilledRect(
        (aRectf_t){tx - 2, ty - 2, 4, 4},
        color
      );
    }
  }

  // Body circle
  a_DrawFilledCircle((int)bcx, (int)bcy, 14, color);

  // Shield outline rings
  if (e->bh_shield > 0) {
    float pulse = 0.5f + 0.3f * sinf(e->bh_tendril_phase * 1.5f);
    int ga = (int)(pulse * 140.0f);
    if (ga > 140) ga = 140;
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    a_DrawCircle((int)bcx, (int)bcy, 15, (aColor_t){220, 220, 230, (uint8_t)ga});
    a_DrawCircle((int)bcx, (int)bcy, 17, (aColor_t){220, 220, 230, (uint8_t)(ga / 2)});
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Shield pips
  if (e->bh_shield > 0) {
    int pip_sz = 4;
    int pip_gap = 2;
    int max_pips = e->bh_shield > 12 ? 12 : e->bh_shield;
    int total_pw = max_pips * pip_sz + (max_pips - 1) * pip_gap;
    int pip_start_x = (int)bcx - total_pw / 2;
    int pip_y = (int)(bcy + 18);
    for (int p = 0; p < max_pips; p++) {
      int ppx = pip_start_x + p * (pip_sz + pip_gap);
      a_DrawFilledRect(
        (aRectf_t){(float)ppx, (float)pip_y, (float)pip_sz, (float)pip_sz},
        (aColor_t){50, 180, 255, 220}
      );
    }
    if (e->bh_shield > 12) {
      char extra[16];
      snprintf(extra, sizeof(extra), "+%d", e->bh_shield - 12);
      aTextStyle_t extra_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {50, 180, 255, 200},
        .align = TEXT_ALIGN_LEFT,
        .scale = 0.25f
      };
      a_DrawText(extra, pip_start_x + total_pw + 3, pip_y - 1, extra_style);
    }
  }

  // Eye — compute pupil position
  float pupil_off_x, pupil_off_y;
  int is_channeling = (e->state == ENEMY_STATE_BH_CHANNELING);
  int is_blinking = (e->bh_blink_timer < 0.15f) &&
                    (e->state != ENEMY_STATE_BH_FIRING) &&
                    (e->state != ENEMY_STATE_BH_AIMING);
  int is_dead = (e->state == ENEMY_STATE_CORPSE || e->state == ENEMY_STATE_HIT_KNOCKBACK);
  if (is_dead) {
    pupil_off_x = e->bh_last_pupil_ox;
    pupil_off_y = e->bh_last_pupil_oy;
  } else {
    float to_px = px - bcx;
    float to_py = py - bcy;
    float to_dist = sqrtf(to_px * to_px + to_py * to_py);
    pupil_off_x = 0;
    pupil_off_y = 0;
    if (to_dist > 0.1f) {
      pupil_off_x = (to_px / to_dist) * 4.0f;
      pupil_off_y = (to_py / to_dist) * 4.0f;
    }
    e->bh_last_pupil_ox = pupil_off_x;
    e->bh_last_pupil_oy = pupil_off_y;
  }
  float eye_x = bcx + pupil_off_x;
  float eye_y = bcy + pupil_off_y;

  if (is_channeling || (is_blinking && !is_dead)) {
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.renderer, 50, 50, 60, 255);
    SDL_RenderDrawLine(app.renderer, (int)bcx - 6, (int)bcy, (int)bcx + 6, (int)bcy);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  } else {
    a_DrawFilledCircle((int)eye_x, (int)eye_y, 3,
                       (aColor_t){20, 20, 30, 255});
  }

  // Beam telegraph (BH_AIMING)
  if (e->state == ENEMY_STATE_BH_AIMING) {
    float tele_angle = e->bh_predicted_angle;
    float tele_dx = cosf(tele_angle);
    float tele_dy = sinf(tele_angle);

    float charge = e->bh_charge_progress;
    float t = charge * charge * (3.0f - 2.0f * charge);

    float max_reach = 1500.0f;
    float reach = max_reach * t;

    float base_a = 0.15f + 0.55f * t;
    float pulse_a = base_a + 0.25f * t * sinf(e->bh_state_timer * (8.0f + 10.0f * t));
    int tele_alpha = (int)(pulse_a * 255.0f);
    if (tele_alpha < 30) tele_alpha = 30;
    if (tele_alpha > 220) tele_alpha = 220;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    if (t > 0.3f) {
      float glow_a = (t - 0.3f) / 0.7f;
      int ga = (int)(glow_a * 60.0f);
      SDL_SetRenderDrawColor(app.renderer, 255, 60, 60, (uint8_t)ga);
      float gperp_x = -tele_dy;
      float gperp_y = tele_dx;
      for (int goff = -3; goff <= 3; goff += 6) {
        int gx1 = (int)(eye_x + gperp_x * (float)goff);
        int gy1 = (int)(eye_y + gperp_y * (float)goff);
        int gx2 = (int)(eye_x + tele_dx * reach + gperp_x * (float)goff);
        int gy2 = (int)(eye_y + tele_dy * reach + gperp_y * (float)goff);
        SDL_RenderDrawLine(app.renderer, gx1, gy1, gx2, gy2);
      }
    }

    SDL_SetRenderDrawColor(app.renderer, 255, 40, 40, (uint8_t)tele_alpha);
    float dash_len = 8.0f;
    for (float d = 0; d < reach; d += dash_len * 2.0f) {
      float d2 = d + dash_len;
      if (d2 > reach) d2 = reach;
      float x1 = eye_x + tele_dx * d;
      float y1 = eye_y + tele_dy * d;
      float x2 = eye_x + tele_dx * d2;
      float y2 = eye_y + tele_dy * d2;
      SDL_RenderDrawLine(app.renderer, (int)x1, (int)y1, (int)x2, (int)y2);
    }

    if (t > 0.1f) {
      int orb_r = (int)(5.0f * t);
      if (orb_r < 1) orb_r = 1;
      int orb_a = (int)(180.0f * t);
      a_DrawFilledCircle((int)eye_x, (int)eye_y, orb_r,
        (aColor_t){255, 80, 60, (uint8_t)orb_a});
    }

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Active beam (BH_FIRING)
  if (e->state == ENEMY_STATE_BH_FIRING) {
    float beam_prog = 1.0f - (e->bh_state_timer / BEHOLDER_BEAM_TIME);
    if (beam_prog < 0.0f) beam_prog = 0.0f;
    if (beam_prog > 1.0f) beam_prog = 1.0f;
    float beam_fade = 1.0f;
    if (beam_prog < 0.15f)
      beam_fade = beam_prog / 0.15f;
    else if (beam_prog > 0.80f)
      beam_fade = (1.0f - beam_prog) / 0.20f;
    if (beam_fade < 0.0f) beam_fade = 0.0f;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    // Trail lines
    for (int t = 0; t < e->bh_trail_count; t++) {
      float ta = e->bh_trail_alphas[t];
      if (ta <= 0.01f) continue;
      float trail_dx = cosf(e->bh_trail_angles[t]);
      float trail_dy = sinf(e->bh_trail_angles[t]);
      int trail_a = (int)(ta * 80.0f * beam_fade);
      if (trail_a > 80) trail_a = 80;
      SDL_SetRenderDrawColor(app.renderer, 255, 120, 80, (uint8_t)trail_a);
      int tx1 = (int)eye_x;
      int ty1 = (int)eye_y;
      int tx2 = (int)(eye_x + trail_dx * 1500.0f);
      int ty2 = (int)(eye_y + trail_dy * 1500.0f);
      SDL_RenderDrawLine(app.renderer, tx1, ty1, tx2, ty2);
    }

    // Main beam
    float beam_dx = cosf(e->bh_beam_angle);
    float beam_dy = sinf(e->bh_beam_angle);
    float perp_x = -beam_dy;
    float perp_y = beam_dx;

    int core_a = (int)(255.0f * beam_fade);
    SDL_SetRenderDrawColor(app.renderer, 255, 40, 40, (uint8_t)core_a);
    for (int off = -1; off <= 1; off++) {
      int x1 = (int)(eye_x + perp_x * (float)off);
      int y1 = (int)(eye_y + perp_y * (float)off);
      int x2 = (int)(eye_x + beam_dx * 1500.0f + perp_x * (float)off);
      int y2 = (int)(eye_y + beam_dy * 1500.0f + perp_y * (float)off);
      SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
    }
    int glow_a = (int)(128.0f * beam_fade);
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 100, (uint8_t)glow_a);
    for (int off = -3; off <= 3; off += 6) {
      int x1 = (int)(eye_x + perp_x * (float)off);
      int y1 = (int)(eye_y + perp_y * (float)off);
      int x2 = (int)(eye_x + beam_dx * 1500.0f + perp_x * (float)off);
      int y2 = (int)(eye_y + beam_dy * 1500.0f + perp_y * (float)off);
      SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
    }
    int dot_a = (int)(255.0f * beam_fade);
    a_DrawFilledCircle((int)eye_x, (int)eye_y, 4,
      (aColor_t){255, 200, 150, (uint8_t)dot_a});

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Channel visual
  if (e->state == ENEMY_STATE_BH_CHANNELING) {
    float ch_t = sinf(e->bh_state_timer * 25.0f);
    float blend = 0.5f + 0.5f * ch_t;
    aColor_t chan_color = {
      (uint8_t)((float)color.r * (1.0f - blend) + 100.0f * blend),
      (uint8_t)((float)color.g * (1.0f - blend) + 150.0f * blend),
      (uint8_t)((float)color.b * (1.0f - blend) + 255.0f * blend),
      255
    };
    a_DrawFilledCircle((int)bcx, (int)bcy, 14, chan_color);

    float ch_progress = 1.0f - (e->bh_state_timer / BEHOLDER_CHANNEL_TIME);
    int num_sparks = 4;
    for (int s = 0; s < num_sparks; s++) {
      float angle = (float)s / num_sparks * 2.0f * (float)PI + ch_progress * 8.0f;
      float sr = 12.0f * (1.0f - ch_progress);
      float sx = bcx + cosf(angle) * sr;
      float sy = bcy + sinf(angle) * sr;
      a_DrawFilledRect(
        (aRectf_t){sx - 1.5f, sy - 1.5f, 3, 3},
        (aColor_t){100, 150, 255, 200}
      );
    }
  }
}

// ============================================================================
// Sound Cleanup
// ============================================================================

void beholder_stop_sounds(void)
{
  game_audio_stop_all_beholder();
  for (int i = 0; i < max_enemies; i++) {
    Enemy_t* e = &enemies[i];
    if (e->active && e->type == ENEMY_TYPE_BEHOLDER && e->bh_windup_channel >= 0) {
      game_audio_stop_beholder_windup(e->bh_windup_channel);
      e->bh_windup_channel = -1;
    }
  }
}
