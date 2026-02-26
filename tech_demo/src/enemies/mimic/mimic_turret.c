#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Turret — dash in, place turret, dash out, strafe on cooldown
// ============================================================================

#define MIMIC_TURRET_MAX          8
#define MIMIC_TURRET_BULLET_MAX  16
#define MIMIC_TURRET_PLACE_RANGE 80.0f
#define MIMIC_TURRET_DASH_TIMEOUT 2.0f
#define MM_TESLA_INTERVAL        1.5f

#define MM_MINI_RANGE_MULT       0.5f
#define MM_MINI_DURATION_MULT    0.5f
#define MM_MINI_FIRE_RATE_MULT   1.4f
#define MM_MINI_PLACEMENT_DIST   1.15f

enum {
  TURRET_AI_STRAFE,
  TURRET_AI_DASH_IN,
  TURRET_AI_DASH_OUT,
};

// --- Turret pool ---

typedef struct {
  float x, y;
  float lifetime, max_lifetime;
  float fire_timer;
  float tesla_timer;
  int   active;
  int   overcharge_halfway_fired;
  int   is_mini;
  int   mini_halfway_spawned;
} MimicTurret_t;

typedef struct {
  float x, y, vx, vy;
  float lifetime;
  int   active;
} MimicTurretBullet_t;

typedef struct {
  float x, y, radius, timer;
  int active;
} MimicTurretExplosion_t;

static MimicTurret_t          mm_turrets[MIMIC_TURRET_MAX];
static MimicTurretBullet_t    mm_bullets[MIMIC_TURRET_BULLET_MAX];
static MimicTurretExplosion_t mm_explosions[MIMIC_TURRET_MAX];

// ============================================================================
// Helpers
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

// ============================================================================
// Overcharge explosion — damages player, knockback
// ============================================================================

static void turret_overcharge_at(int i, float range_override)
{
  float radius = (range_override > 0.0f ? range_override : weapons_get_turret_range()) * 1.15f;
  float tx = mm_turrets[i].x;
  float ty = mm_turrets[i].y;
  float px = player_get_x();
  float py = player_get_y();

  float dx = px - tx;
  float dy = py - ty;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < radius + MIMIC_PLAYER_RADIUS) {
    player_take_damage(MIMIC_CONTACT_DAMAGE, tx, ty, ENEMY_TYPE_MIMIC);
    player_apply_knockback(tx, ty, 250.0f);
  }

  // Explosion visual
  for (int e = 0; e < MIMIC_TURRET_MAX; e++) {
    if (!mm_explosions[e].active) {
      mm_explosions[e] = (MimicTurretExplosion_t){
        .x = tx, .y = ty,
        .radius = radius,
        .timer = 0.25f,
        .active = 1
      };
      break;
    }
  }

  game_audio_play_bomb_explode();
}

// ============================================================================
// Mini turret spawn (toward player since mimic turrets target the player)
// ============================================================================

static void mm_spawn_mini_turret(float parent_x, float parent_y, float px, float py)
{
  int slot = -1;
  for (int i = 0; i < MIMIC_TURRET_MAX; i++) {
    if (!mm_turrets[i].active) { slot = i; break; }
  }
  if (slot < 0) return;

  // Direction toward player
  float dx = px - parent_x;
  float dy = py - parent_y;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > 0.1f) { dx /= len; dy /= len; }
  else { dx = 0.0f; dy = -1.0f; }

  float place_dist = weapons_get_turret_range() * MM_MINI_PLACEMENT_DIST;
  float mx = parent_x + dx * place_dist;
  float my = parent_y + dy * place_dist;

  if (mx < 20.0f) mx = 20.0f;
  if (mx > SCREEN_WIDTH - 20.0f) mx = SCREEN_WIDTH - 20.0f;
  if (my < 20.0f) my = 20.0f;
  if (my > SCREEN_HEIGHT - 20.0f) my = SCREEN_HEIGHT - 20.0f;

  float dur = weapons_get_turret_duration() * MM_MINI_DURATION_MULT;
  mm_turrets[slot] = (MimicTurret_t){
    .x = mx, .y = my,
    .lifetime = dur, .max_lifetime = dur,
    .fire_timer = 0.3f,
    .tesla_timer = MM_TESLA_INTERVAL,
    .active = 1,
    .overcharge_halfway_fired = 0,
    .is_mini = 1,
    .mini_halfway_spawned = 0
  };

  if (upgrades_get_tier(UPG_TURRET_OVERCHARGE) >= 3) {
    turret_overcharge_at(slot, weapons_get_turret_range() * MM_MINI_RANGE_MULT);
  }
}

// ============================================================================
// Place turret at position
// ============================================================================

static void place_turret(float cx, float cy, float px, float py)
{
  for (int i = 0; i < MIMIC_TURRET_MAX; i++) {
    if (mm_turrets[i].active) continue;

    float dur = weapons_get_turret_duration();
    mm_turrets[i] = (MimicTurret_t){
      .x = cx, .y = cy,
      .lifetime = dur, .max_lifetime = dur,
      .fire_timer = 0.3f,
      .tesla_timer = MM_TESLA_INTERVAL,
      .active = 1,
      .overcharge_halfway_fired = 0,
      .is_mini = 0,
      .mini_halfway_spawned = 0
    };

    // T3 Overcharge: explode on deploy
    if (upgrades_get_tier(UPG_TURRET_OVERCHARGE) >= 3) {
      turret_overcharge_at(i, 0.0f);
    }

    // Mini turret: spawn on deploy
    if (upgrades_get_tier(UPG_TURRET_MINI_TURRET) >= 1) {
      mm_spawn_mini_turret(cx, cy, px, py);
    }
    return;
  }
}

// ============================================================================
// Fire bullets at player from a turret
// ============================================================================

static void turret_fire(int ti, float px, float py)
{
  float tx = mm_turrets[ti].x;
  float ty = mm_turrets[ti].y;
  float dx = px - tx;
  float dy = py - ty;
  float mag = sqrtf(dx * dx + dy * dy);
  if (mag < 0.1f) { dx = 0; dy = -1; mag = 1; }
  dx /= mag;
  dy /= mag;

  int count = weapons_get_turret_spread();

  if (count <= 1) {
    // Single bullet
    for (int b = 0; b < MIMIC_TURRET_BULLET_MAX; b++) {
      if (mm_bullets[b].active) continue;
      mm_bullets[b] = (MimicTurretBullet_t){
        .x = tx, .y = ty,
        .vx = dx * TURRET_BULLET_SPEED,
        .vy = dy * TURRET_BULLET_SPEED,
        .lifetime = TURRET_BULLET_LIFETIME,
        .active = 1
      };
      break;
    }
  } else {
    // Fan spread (60 degrees)
    float base_angle = atan2f(dy, dx);
    float total_angle = 1.0472f; // 60 degrees
    float step = total_angle / (float)(count - 1);
    float start = base_angle - total_angle * 0.5f;

    for (int s = 0; s < count; s++) {
      float angle = start + step * (float)s;
      for (int b = 0; b < MIMIC_TURRET_BULLET_MAX; b++) {
        if (mm_bullets[b].active) continue;
        mm_bullets[b] = (MimicTurretBullet_t){
          .x = tx, .y = ty,
          .vx = cosf(angle) * TURRET_BULLET_SPEED,
          .vy = sinf(angle) * TURRET_BULLET_SPEED,
          .lifetime = TURRET_BULLET_LIFETIME,
          .active = 1
        };
        break;
      }
    }
  }
}

// ============================================================================
// Update turrets (firing, lifetime, tesla, overcharge, slow)
// ============================================================================

static void update_turrets(float dt, float px, float py)
{
  float base_range = weapons_get_turret_range();
  float base_fire_rate = weapons_get_turret_fire_rate();
  int overcharge_tier = upgrades_get_tier(UPG_TURRET_OVERCHARGE);
  int mini_tier = upgrades_get_tier(UPG_TURRET_MINI_TURRET);
  int tesla_tier = upgrades_get_tier(UPG_TURRET_TESLA_COIL);
  int slow_tier = upgrades_get_tier(UPG_TURRET_SLOW_ZONE);

  for (int i = 0; i < MIMIC_TURRET_MAX; i++) {
    if (!mm_turrets[i].active) continue;

    // Per-turret stats (scaled for minis)
    float trange = base_range;
    float fire_rate = base_fire_rate;
    float range_ovr = 0.0f;
    if (mm_turrets[i].is_mini) {
      trange *= MM_MINI_RANGE_MULT;
      fire_rate *= MM_MINI_FIRE_RATE_MULT;
      range_ovr = trange;
    }

    mm_turrets[i].lifetime -= dt;

    // Overcharge: halfway explosion
    if (overcharge_tier >= 2 && !mm_turrets[i].overcharge_halfway_fired) {
      if (mm_turrets[i].lifetime <= mm_turrets[i].max_lifetime * 0.5f) {
        mm_turrets[i].overcharge_halfway_fired = 1;
        turret_overcharge_at(i, range_ovr);
      }
    }

    // Mini turret: spawn at half-life
    if (mini_tier >= 2 && !mm_turrets[i].is_mini && !mm_turrets[i].mini_halfway_spawned) {
      if (mm_turrets[i].lifetime <= mm_turrets[i].max_lifetime * 0.5f) {
        mm_turrets[i].mini_halfway_spawned = 1;
        mm_spawn_mini_turret(mm_turrets[i].x, mm_turrets[i].y, px, py);
      }
    }

    // Expired
    if (mm_turrets[i].lifetime <= 0.0f) {
      // Mini turret: spawn on expiry
      if (mini_tier >= 3 && !mm_turrets[i].is_mini) {
        mm_spawn_mini_turret(mm_turrets[i].x, mm_turrets[i].y, px, py);
      }

      if (overcharge_tier >= 1) {
        turret_overcharge_at(i, range_ovr);
      }
      mm_turrets[i].active = 0;
      continue;
    }

    // Fire at player when in range
    float tdx = px - mm_turrets[i].x;
    float tdy = py - mm_turrets[i].y;
    float tdist = sqrtf(tdx * tdx + tdy * tdy);

    mm_turrets[i].fire_timer -= dt;
    if (mm_turrets[i].fire_timer <= 0.0f && tdist < trange) {
      mm_turrets[i].fire_timer += fire_rate;
      turret_fire(i, px, py);
    }

    // Vacuum slow: always active in range, stronger with tesla upgrade
    {
      float inner_r = trange * 0.3f;
      if (tdist > inner_r && tdist < trange) {
        float base_slow = 0.88f;
        static const float bonus_slow[4] = { 0.0f, 0.04f, 0.08f, 0.14f };
        player_apply_slow(base_slow - bonus_slow[tesla_tier], 0.2f);
      }
    }

    // Slow zone: always active in turret range, stronger with upgrade
    if (tdist < trange) {
      float base_slow = 0.92f;
      static const float upgrade_slow[4] = { 0.0f, 0.04f, 0.10f, 0.16f };
      player_apply_slow(base_slow - upgrade_slow[slow_tier], 0.2f);
    }
  }
}

// ============================================================================
// Update bullets (homing toward player)
// ============================================================================

static void update_bullets(float dt, float px, float py)
{
  for (int i = 0; i < MIMIC_TURRET_BULLET_MAX; i++) {
    if (!mm_bullets[i].active) continue;

    // Homing toward player
    float bx = mm_bullets[i].x;
    float by = mm_bullets[i].y;
    float bul_angle = atan2f(mm_bullets[i].vy, mm_bullets[i].vx);
    float target_angle = atan2f(py - by, px - bx);
    float diff = target_angle - bul_angle;
    while (diff > PI) diff -= 2.0f * (float)PI;
    while (diff < -PI) diff += 2.0f * (float)PI;
    float max_turn = TURRET_HOMING_RATE * dt;
    if (diff > max_turn) diff = max_turn;
    else if (diff < -max_turn) diff = -max_turn;
    float new_angle = bul_angle + diff;
    float spd = sqrtf(mm_bullets[i].vx * mm_bullets[i].vx +
                       mm_bullets[i].vy * mm_bullets[i].vy);
    mm_bullets[i].vx = cosf(new_angle) * spd;
    mm_bullets[i].vy = sinf(new_angle) * spd;

    // Accelerate
    float accel = 1.0f + 0.6f * dt;
    mm_bullets[i].vx *= accel;
    mm_bullets[i].vy *= accel;

    // Move
    mm_bullets[i].x += mm_bullets[i].vx * dt;
    mm_bullets[i].y += mm_bullets[i].vy * dt;

    // Lifetime
    mm_bullets[i].lifetime -= dt;
    if (mm_bullets[i].lifetime <= 0.0f) {
      mm_bullets[i].active = 0;
      continue;
    }

    // Off-screen
    if (mm_bullets[i].x < -20 || mm_bullets[i].x > SCREEN_WIDTH + 20 ||
        mm_bullets[i].y < -20 || mm_bullets[i].y > SCREEN_HEIGHT + 20) {
      mm_bullets[i].active = 0;
      continue;
    }

    // Player collision
    float bdx = mm_bullets[i].x - px;
    float bdy = mm_bullets[i].y - py;
    if (bdx * bdx + bdy * bdy < MIMIC_PLAYER_RADIUS * MIMIC_PLAYER_RADIUS) {
      player_take_damage(MIMIC_CONTACT_DAMAGE, mm_bullets[i].x,
                         mm_bullets[i].y, ENEMY_TYPE_MIMIC);
      mm_bullets[i].active = 0;
    }
  }
}

// ============================================================================
// AI State Machine + main update
// ============================================================================

void mimic_turret_update(Enemy_t* e, int index, float dt,
                         float px, float py, float pvx, float pvy)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float sep_x, sep_y;
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);

  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;
  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);

  switch (e->mm_spin_ai_state) {
    case TURRET_AI_STRAFE: {
      // Orbit at preferred range
      if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
      float ndx = dx / dist;
      float ndy = dy / dist;

      float steer_x = 0.0f, steer_y = 0.0f;

      // Radial spring toward preferred range
      if (dist > MIMIC_RANGE_TURRET_IDLE + 30.0f) {
        steer_x += -ndx * 0.5f;
        steer_y += -ndy * 0.5f;
      } else if (dist < MIMIC_RANGE_TURRET_IDLE - 30.0f) {
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
        e->vx = (steer_x / slen) * MIMIC_SPEED_TURRET_IDLE;
        e->vy = (steer_y / slen) * MIMIC_SPEED_TURRET_IDLE;
      } else {
        e->vx = 0.0f;
        e->vy = 0.0f;
      }

      // Transition: cooldown expired → dash in
      if (e->mm_weapon_cooldown <= 0.0f) {
        e->mm_spin_ai_state = TURRET_AI_DASH_IN;
        e->mm_spin_ai_timer = MIMIC_TURRET_DASH_TIMEOUT;
      }
      break;
    }

    case TURRET_AI_DASH_IN: {
      // Charge toward predicted player position
      float trange = weapons_get_turret_range();
      float predict_time = dist / MIMIC_SPEED_TURRET_DASH;
      if (predict_time > 1.0f) predict_time = 1.0f;
      float pred_px = px + pvx * predict_time;
      float pred_py = py + pvy * predict_time;

      ei_move_toward(e, pred_px - stats->size / 2.0f,
                     pred_py - stats->size / 2.0f,
                     MIMIC_SPEED_TURRET_DASH, sep_x, sep_y, 10.0f, dt);

      e->mm_spin_ai_timer -= dt;

      // Place turret when close enough or timeout
      if (dist < MIMIC_TURRET_PLACE_RANGE || e->mm_spin_ai_timer <= 0.0f) {
        cx = e->x + stats->size / 2.0f;
        cy = e->y + stats->size / 2.0f;

        // Place turret ahead of where the player is moving
        // so turret range covers their predicted path
        float lead_time = trange * 0.4f;
        float pspd = sqrtf(pvx * pvx + pvy * pvy);
        float place_x, place_y;
        if (pspd > 10.0f) {
          // Place between mimic and where player is heading
          float lead_t = lead_time / pspd;
          if (lead_t > 0.8f) lead_t = 0.8f;
          place_x = px + pvx * lead_t;
          place_y = py + pvy * lead_t;
        } else {
          // Player barely moving — place at mimic position (near player)
          place_x = cx;
          place_y = cy;
        }

        // Clamp to screen
        if (place_x < 20.0f) place_x = 20.0f;
        if (place_x > SCREEN_WIDTH - 20.0f) place_x = SCREEN_WIDTH - 20.0f;
        if (place_y < 20.0f) place_y = 20.0f;
        if (place_y > SCREEN_HEIGHT - 20.0f) place_y = SCREEN_HEIGHT - 20.0f;

        place_turret(place_x, place_y, px, py);
        e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);

        // Pick retreat target: away from player + random flank angle
        if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
        float ndx = dx / dist;
        float ndy = dy / dist;
        float angle_offset = RANDF(-0.5f, 0.5f);
        float cos_a = cosf(angle_offset);
        float sin_a = sinf(angle_offset);
        float flank_dx = ndx * cos_a - ndy * sin_a;
        float flank_dy = ndx * sin_a + ndy * cos_a;
        float retreat_dist = RANDF(180.0f, 250.0f);

        e->mm_spin_flank_x = cx + flank_dx * retreat_dist;
        e->mm_spin_flank_y = cy + flank_dy * retreat_dist;

        // Clamp to screen
        float margin = 30.0f;
        if (e->mm_spin_flank_x < margin) e->mm_spin_flank_x = margin;
        if (e->mm_spin_flank_x > SCREEN_WIDTH - margin) e->mm_spin_flank_x = SCREEN_WIDTH - margin;
        if (e->mm_spin_flank_y < margin) e->mm_spin_flank_y = margin;
        if (e->mm_spin_flank_y > SCREEN_HEIGHT - margin) e->mm_spin_flank_y = SCREEN_HEIGHT - margin;

        e->mm_spin_ai_state = TURRET_AI_DASH_OUT;
        e->mm_spin_ai_timer = 1.0f;
      }
      break;
    }

    case TURRET_AI_DASH_OUT: {
      // Retreat to flank point
      ei_move_toward(e, e->mm_spin_flank_x - stats->size / 2.0f,
                     e->mm_spin_flank_y - stats->size / 2.0f,
                     MIMIC_SPEED_TURRET_DASH, sep_x, sep_y, 20.0f, dt);

      e->mm_spin_ai_timer -= dt;

      // Arrived or timeout → back to strafe
      float fdx = cx - e->mm_spin_flank_x;
      float fdy = cy - e->mm_spin_flank_y;
      if (fdx * fdx + fdy * fdy < 20.0f * 20.0f || e->mm_spin_ai_timer <= 0.0f) {
        e->mm_spin_ai_state = TURRET_AI_STRAFE;
      }
      break;
    }
  }

  // Apply movement (for strafe state which sets vx/vy directly)
  if (e->mm_spin_ai_state == TURRET_AI_STRAFE) {
    e->x += e->vx * dt;
    e->y += e->vy * dt;
    if (e->x < 0) e->x = 0;
    if (e->y < 0) e->y = 0;
    if (e->x > SCREEN_WIDTH - stats->size) e->x = SCREEN_WIDTH - stats->size;
    if (e->y > SCREEN_HEIGHT - stats->size) e->y = SCREEN_HEIGHT - stats->size;
  }

  // Update turrets and bullets
  update_turrets(dt, px, py);
  update_bullets(dt, px, py);

  // Update explosion visuals
  for (int i = 0; i < MIMIC_TURRET_MAX; i++) {
    if (!mm_explosions[i].active) continue;
    mm_explosions[i].timer -= dt;
    if (mm_explosions[i].timer <= 0.0f)
      mm_explosions[i].active = 0;
  }
}

// ============================================================================
// Draw
// ============================================================================

void mimic_turret_draw(Enemy_t* e)
{
  (void)e;

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < MIMIC_TURRET_MAX; i++) {
    if (!mm_turrets[i].active) continue;
    float remaining = mm_turrets[i].lifetime;

    // Fade in last 0.2s
    float alpha_mult = 1.0f;
    if (remaining < 0.2f) alpha_mult = remaining / 0.2f;

    // Blink in last 0.5s at ~8Hz
    if (remaining < 0.5f && remaining >= 0.2f) {
      if (((int)(remaining * 16.0f)) % 2 == 0) continue;
    }

    // Pulse 180-255 at 2Hz
    float pulse = 180.0f + 75.0f * sinf(remaining * 4.0f * (float)PI);
    int p = (int)(pulse * alpha_mult);
    if (p > 255) p = 255;
    if (p < 0) p = 0;

    int tx = (int)mm_turrets[i].x;
    int ty = (int)mm_turrets[i].y;

    // Dimmer for minis
    if (mm_turrets[i].is_mini) {
      p = p * 7 / 10;
    }

    // Range circle with slow zone tint
    {
      float trange = mm_turrets[i].is_mini
        ? weapons_get_turret_range() * MM_MINI_RANGE_MULT
        : weapons_get_turret_range();
      int ra = mm_turrets[i].is_mini
        ? (int)(12.0f * alpha_mult)
        : (int)(20.0f * alpha_mult);
      if (ra > 0) {
        draw_ring((float)tx, (float)ty, trange, 120, 150, 200, (uint8_t)(ra + 10));
      }
    }

    // Diamond body (smaller for minis)
    int sz = mm_turrets[i].is_mini ? 3 : 5;
    // Dark grey fill
    SDL_SetRenderDrawColor(app.renderer, 120, 120, 120, (uint8_t)(p * 180 / 255));
    for (int dy = -sz; dy <= sz; dy++) {
      int hw = sz - abs(dy);
      SDL_RenderDrawLine(app.renderer, tx - hw, ty + dy, tx + hw, ty + dy);
    }
    // Silver outline
    SDL_SetRenderDrawColor(app.renderer, 180, 180, 180, (uint8_t)p);
    SDL_RenderDrawLine(app.renderer, tx, ty - sz, tx + sz, ty);
    SDL_RenderDrawLine(app.renderer, tx + sz, ty, tx, ty + sz);
    SDL_RenderDrawLine(app.renderer, tx, ty + sz, tx - sz, ty);
    SDL_RenderDrawLine(app.renderer, tx - sz, ty, tx, ty - sz);

    // Vacuum pull arrows (always visible)
    {
      int ra = (int)(20.0f * alpha_mult);
      if (ra > 5) {
        float trange = mm_turrets[i].is_mini
          ? weapons_get_turret_range() * MM_MINI_RANGE_MULT
          : weapons_get_turret_range();
        float phase = 1.0f - fmodf(remaining * 2.0f, 1.0f);
        int num_arrows = 6;
        for (int a = 0; a < num_arrows; a++) {
          float angle = (float)a / num_arrows * 2.0f * (float)PI;
          float cos_a = cosf(angle);
          float sin_a = sinf(angle);
          float tip_r = trange * (1.0f - phase);
          float tail_r = tip_r + trange * 0.15f;
          if (tail_r > trange) tail_r = trange;

          SDL_SetRenderDrawColor(app.renderer, 150, 180, 200, (uint8_t)ra);
          SDL_RenderDrawLine(app.renderer,
            (int)((float)tx + cos_a * tail_r), (int)((float)ty + sin_a * tail_r),
            (int)((float)tx + cos_a * tip_r),  (int)((float)ty + sin_a * tip_r));
        }
      }
    }
  }

  // Bullets (4x4 grey squares)
  for (int i = 0; i < MIMIC_TURRET_BULLET_MAX; i++) {
    if (!mm_bullets[i].active) continue;
    int bx = (int)mm_bullets[i].x;
    int by = (int)mm_bullets[i].y;
    // Glow (6x6)
    SDL_SetRenderDrawColor(app.renderer, 180, 180, 180, 40);
    { SDL_Rect glow = { bx - 3, by - 3, 6, 6 }; SDL_RenderFillRect(app.renderer, &glow); }
    // Core (4x4)
    SDL_SetRenderDrawColor(app.renderer, 200, 200, 200, 255);
    { SDL_Rect core = { bx - 2, by - 2, 4, 4 }; SDL_RenderFillRect(app.renderer, &core); }
  }

  // Overcharge explosions
  for (int i = 0; i < MIMIC_TURRET_MAX; i++) {
    if (!mm_explosions[i].active) continue;
    float p = 1.0f - (mm_explosions[i].timer / 0.25f);
    float current_radius = mm_explosions[i].radius * (0.3f + 0.7f * p);
    int alpha = (int)(200.0f * (1.0f - p));
    if (alpha < 0) alpha = 0;

    int ex = (int)mm_explosions[i].x;
    int ey = (int)mm_explosions[i].y;

    // Filled explosion circle (row-by-row)
    int ri = (int)current_radius;
    SDL_SetRenderDrawColor(app.renderer, 200, 200, 200, (uint8_t)alpha);
    for (int row = -ri; row <= ri; row++) {
      int half = (int)sqrtf((float)(ri * ri - row * row));
      SDL_RenderDrawLine(app.renderer, ex - half, ey + row, ex + half, ey + row);
    }

    // Outer ring
    int ring_alpha = alpha + 55;
    if (ring_alpha > 255) ring_alpha = 255;
    draw_ring((float)ex, (float)ey, current_radius, 220, 220, 220, (uint8_t)ring_alpha);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_turret_cleanup(void)
{
  memset(mm_turrets, 0, sizeof(mm_turrets));
  memset(mm_bullets, 0, sizeof(mm_bullets));
  memset(mm_explosions, 0, sizeof(mm_explosions));
}
