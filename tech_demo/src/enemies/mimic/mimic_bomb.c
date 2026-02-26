#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Bomb Projectile Pool
// ============================================================================

typedef struct {
  float start_x, start_y, target_x, target_y;
  float flight_progress;
  int active;
} MimicBombFlight_t;

typedef struct {
  float x, y, radius, timer;
  int active;
} MimicBombExplosion_t;

typedef struct {
  float x, y, vx, vy;
  float lifetime;
  int active;
} MimicBombShrapnel_t;

typedef struct {
  float x, y, radius;
  float fuse;
  int active;
} MimicBombCluster_t;

typedef struct {
  float x, y, angle;
  float current_len, max_len;
  float lifetime, max_lifetime;
  float tick_timer;
  int active;
} MimicBombNapalm_t;

typedef struct {
  float x, y, radius;
  float lifetime, max_lifetime;
  int active;
} MimicBombCrater_t;

static MimicBombFlight_t    mm_flights[MIMIC_BOMB_MAX];
static MimicBombExplosion_t mm_explosions[MIMIC_BOMB_MAX];
static MimicBombShrapnel_t  mm_shrapnel[MIMIC_BOMB_SHRAPNEL_MAX];
static MimicBombCluster_t   mm_clusters[MIMIC_BOMB_CLUSTER_MAX];
static MimicBombNapalm_t    mm_napalm[MIMIC_BOMB_NAPALM_MAX];
static MimicBombCrater_t    mm_craters[MIMIC_BOMB_CRATER_MAX];

// Telegraph state
#define MIMIC_BOMB_TELEGRAPH_TIME 0.3f
static int   mm_bomb_telegraphing;
static float mm_bomb_telegraph_timer;
static float mm_bomb_tele_sx, mm_bomb_tele_sy;  // source (mimic) position
static float mm_bomb_tele_tx, mm_bomb_tele_ty;  // locked target position

// ============================================================================
// Fire — lob bomb toward a target position
// ============================================================================

static void fire_bomb_at(float cx, float cy, float tx, float ty)
{
  for (int i = 0; i < MIMIC_BOMB_MAX; i++) {
    if (mm_flights[i].active) continue;
    mm_flights[i] = (MimicBombFlight_t){
      .start_x = cx, .start_y = cy,
      .target_x = tx, .target_y = ty,
      .flight_progress = 0.0f,
      .active = 1
    };
    game_audio_play_bomb_throw();
    return;
  }
}

// ============================================================================
// Explosion — check player hit + spawn upgrade effects
// ============================================================================

static void bomb_explode(float ix, float iy, float sx, float sy,
                         float px, float py)
{
  float blast_r = weapons_get_bomb_blast_radius();
  game_audio_play_bomb_explode();

  // Start explosion visual
  for (int i = 0; i < MIMIC_BOMB_MAX; i++) {
    if (!mm_explosions[i].active) {
      mm_explosions[i] = (MimicBombExplosion_t){
        .x = ix, .y = iy,
        .radius = blast_r,
        .timer = MIMIC_BOMB_VISUAL_DURATION,
        .active = 1
      };
      break;
    }
  }

  // Check player hit
  float dx = px - ix;
  float dy = py - iy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < blast_r + MIMIC_PLAYER_RADIUS) {
    player_take_damage(MIMIC_CONTACT_DAMAGE, ix, iy, ENEMY_TYPE_MIMIC);

    // Implosion: pull player toward center
    int impl_tier = upgrades_get_tier(UPG_BOMB_IMPLOSION);
    if (impl_tier > 0) {
      // Pull toward explosion center (knockback FROM opposite side)
      float far_x = px + (px - ix);
      float far_y = py + (py - iy);
      static const float impl_force[4] = { 0.0f, 120.0f, 180.0f, 240.0f };
      player_apply_knockback(far_x, far_y, impl_force[impl_tier]);
    } else {
      // Normal: push player away from explosion
      player_apply_knockback(ix, iy, 250.0f);
    }
  }

  // Shrapnel
  int shrapnel_tier = upgrades_get_tier(UPG_BOMB_SHRAPNEL);
  if (shrapnel_tier > 0) {
    static const int shard_count[4] = { 0, 4, 6, 8 };
    int count = shard_count[shrapnel_tier];
    for (int s = 0; s < count; s++) {
      float angle = (2.0f * PI * (float)s) / (float)count;
      for (int si = 0; si < MIMIC_BOMB_SHRAPNEL_MAX; si++) {
        if (!mm_shrapnel[si].active) {
          mm_shrapnel[si] = (MimicBombShrapnel_t){
            .x = ix, .y = iy,
            .vx = cosf(angle) * MIMIC_BOMB_SHRAPNEL_SPEED,
            .vy = sinf(angle) * MIMIC_BOMB_SHRAPNEL_SPEED,
            .lifetime = 0.5f,
            .active = 1
          };
          break;
        }
      }
    }
  }

  // Cluster bombs
  int cluster_tier = upgrades_get_tier(UPG_BOMB_CLUSTER);
  if (cluster_tier > 0) {
    static const int mini_count[4] = { 0, 3, 4, 5 };
    int count = mini_count[cluster_tier];
    for (int m = 0; m < count; m++) {
      for (int ci = 0; ci < MIMIC_BOMB_CLUSTER_MAX; ci++) {
        if (!mm_clusters[ci].active) {
          float angle = RANDF(0.0f, 2.0f * PI);
          float speed = 150.0f;
          mm_clusters[ci] = (MimicBombCluster_t){
            .x = ix + cosf(angle) * 5.0f,
            .y = iy + sinf(angle) * 5.0f,
            .radius = blast_r * 0.5f,
            .fuse = 0.1f + RANDF(0.0f, 0.2f),
            .active = 1
          };
          // Scatter velocity stored as position offset per frame
          mm_clusters[ci].x += cosf(angle) * speed * mm_clusters[ci].fuse;
          mm_clusters[ci].y += sinf(angle) * speed * mm_clusters[ci].fuse;
          break;
        }
      }
    }
  }

  // Napalm fire streak
  int napalm_tier = upgrades_get_tier(UPG_BOMB_NAPALM);
  if (napalm_tier > 0) {
    static const float np_max_len[4] = { 0.0f, 50.0f, 65.0f, 80.0f };
    float bdx = ix - sx;
    float bdy = iy - sy;
    float bang = atan2f(bdy, bdx);
    for (int ni = 0; ni < MIMIC_BOMB_NAPALM_MAX; ni++) {
      if (!mm_napalm[ni].active) {
        float dur = 1.0f + 0.5f * (float)napalm_tier;
        mm_napalm[ni] = (MimicBombNapalm_t){
          .x = ix, .y = iy,
          .angle = bang,
          .current_len = np_max_len[napalm_tier] * 0.3f,
          .max_len = np_max_len[napalm_tier],
          .lifetime = dur, .max_lifetime = dur,
          .tick_timer = 0.0f,
          .active = 1
        };
        break;
      }
    }
  }

  // Crater slow zone
  int crater_tier = upgrades_get_tier(UPG_BOMB_CRATER);
  if (crater_tier > 0) {
    static const float crater_dur[4] = { 0.0f, 3.0f, 4.5f, 6.0f };
    for (int ci = 0; ci < MIMIC_BOMB_CRATER_MAX; ci++) {
      if (!mm_craters[ci].active) {
        mm_craters[ci] = (MimicBombCrater_t){
          .x = ix, .y = iy,
          .radius = blast_r * 0.8f,
          .lifetime = crater_dur[crater_tier],
          .max_lifetime = crater_dur[crater_tier],
          .active = 1
        };
        break;
      }
    }
  }
}

// ============================================================================
// Update
// ============================================================================

void mimic_bomb_update(Enemy_t* e, int index, float dt,
                       float px, float py, float pvx, float pvy)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  // --- Movement: strafe at MIMIC_RANGE_MID (like wand but closer) ---
  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  float steer_x = 0.0f, steer_y = 0.0f;

  // Radial spring toward preferred range
  if (dist > MIMIC_RANGE_MID + 30.0f) {
    steer_x += -ndx * 0.5f;
    steer_y += -ndy * 0.5f;
  } else if (dist < MIMIC_RANGE_MID - 30.0f) {
    steer_x += ndx * 0.8f;
    steer_y += ndy * 0.8f;
  }

  // Perpendicular strafe
  float perp_x = -ndy;
  float perp_y =  ndx;
  steer_x += perp_x * 0.6f;
  steer_y += perp_y * 0.6f;

  // Separation
  float sep_x, sep_y;
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);
  steer_x += sep_x;
  steer_y += sep_y;

  // Screen edge repulsion
  float margin = 30.0f;
  if (cx < margin)                  steer_x += (margin - cx) / margin;
  if (cx > SCREEN_WIDTH - margin)   steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
  if (cy < margin)                  steer_y += (margin - cy) / margin;
  if (cy > SCREEN_HEIGHT - margin)  steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

  float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
  if (slen > 0.1f) {
    e->vx = (steer_x / slen) * MIMIC_SPEED_BOMB;
    e->vy = (steer_y / slen) * MIMIC_SPEED_BOMB;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  e->x += e->vx * dt;
  e->y += e->vy * dt;

  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - stats->size) e->x = SCREEN_WIDTH - stats->size;
  if (e->y > SCREEN_HEIGHT - stats->size) e->y = SCREEN_HEIGHT - stats->size;

  // --- Attack: telegraph then fire bomb ---
  if (e->mm_weapon_cooldown <= 0.0f && !mm_bomb_telegraphing) {
    float flight_time = weapons_get_bomb_flight_time();
    cx = e->x + stats->size / 2.0f;
    cy = e->y + stats->size / 2.0f;
    mm_bomb_tele_sx = cx;
    mm_bomb_tele_sy = cy;
    mm_bomb_tele_tx = px + pvx * flight_time;
    mm_bomb_tele_ty = py + pvy * flight_time;
    mm_bomb_telegraphing = 1;
    mm_bomb_telegraph_timer = MIMIC_BOMB_TELEGRAPH_TIME;
  }

  if (mm_bomb_telegraphing) {
    // Track mimic position during telegraph
    mm_bomb_tele_sx = e->x + stats->size / 2.0f;
    mm_bomb_tele_sy = e->y + stats->size / 2.0f;
    mm_bomb_telegraph_timer -= dt;
    if (mm_bomb_telegraph_timer <= 0.0f) {
      mm_bomb_telegraphing = 0;
      fire_bomb_at(mm_bomb_tele_sx, mm_bomb_tele_sy,
                   mm_bomb_tele_tx, mm_bomb_tele_ty);
      e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);
    }
  }

  // --- Update bomb flights ---
  float flight_time = weapons_get_bomb_flight_time();
  for (int i = 0; i < MIMIC_BOMB_MAX; i++) {
    if (!mm_flights[i].active) continue;
    mm_flights[i].flight_progress += dt / flight_time;
    if (mm_flights[i].flight_progress >= 1.0f) {
      mm_flights[i].active = 0;
      bomb_explode(mm_flights[i].target_x, mm_flights[i].target_y,
                   mm_flights[i].start_x, mm_flights[i].start_y,
                   px, py);
    }
  }

  // --- Update explosion visuals ---
  for (int i = 0; i < MIMIC_BOMB_MAX; i++) {
    if (!mm_explosions[i].active) continue;
    mm_explosions[i].timer -= dt;
    if (mm_explosions[i].timer <= 0.0f)
      mm_explosions[i].active = 0;
  }

  // --- Update shrapnel ---
  for (int i = 0; i < MIMIC_BOMB_SHRAPNEL_MAX; i++) {
    if (!mm_shrapnel[i].active) continue;
    mm_shrapnel[i].x += mm_shrapnel[i].vx * dt;
    mm_shrapnel[i].y += mm_shrapnel[i].vy * dt;
    mm_shrapnel[i].lifetime -= dt;
    if (mm_shrapnel[i].lifetime <= 0.0f) {
      mm_shrapnel[i].active = 0;
      continue;
    }
    // Player collision
    float sdx = mm_shrapnel[i].x - px;
    float sdy = mm_shrapnel[i].y - py;
    if (sdx * sdx + sdy * sdy < MIMIC_PLAYER_RADIUS * MIMIC_PLAYER_RADIUS) {
      player_take_damage(MIMIC_CONTACT_DAMAGE / 2, mm_shrapnel[i].x,
                         mm_shrapnel[i].y, ENEMY_TYPE_MIMIC);
      mm_shrapnel[i].active = 0;
    }
  }

  // --- Update cluster mini-bombs ---
  for (int i = 0; i < MIMIC_BOMB_CLUSTER_MAX; i++) {
    if (!mm_clusters[i].active) continue;
    mm_clusters[i].fuse -= dt;
    if (mm_clusters[i].fuse <= 0.0f) {
      mm_clusters[i].active = 0;
      // Mini-explosion: check player hit
      float cdx = px - mm_clusters[i].x;
      float cdy = py - mm_clusters[i].y;
      if (cdx * cdx + cdy * cdy <
          (mm_clusters[i].radius + MIMIC_PLAYER_RADIUS) *
          (mm_clusters[i].radius + MIMIC_PLAYER_RADIUS)) {
        player_take_damage(MIMIC_CONTACT_DAMAGE / 2, mm_clusters[i].x,
                           mm_clusters[i].y, ENEMY_TYPE_MIMIC);
      }
      // Mini explosion visual
      for (int j = 0; j < MIMIC_BOMB_MAX; j++) {
        if (!mm_explosions[j].active) {
          mm_explosions[j] = (MimicBombExplosion_t){
            .x = mm_clusters[i].x, .y = mm_clusters[i].y,
            .radius = mm_clusters[i].radius,
            .timer = MIMIC_BOMB_VISUAL_DURATION * 0.7f,
            .active = 1
          };
          break;
        }
      }
    }
  }

  // --- Update napalm zones ---
  for (int i = 0; i < MIMIC_BOMB_NAPALM_MAX; i++) {
    if (!mm_napalm[i].active) continue;
    mm_napalm[i].lifetime -= dt;
    if (mm_napalm[i].lifetime <= 0.0f) {
      mm_napalm[i].active = 0;
      continue;
    }
    // Grow length
    float grow_pct = 1.0f - (mm_napalm[i].lifetime / mm_napalm[i].max_lifetime);
    if (grow_pct > 1.0f) grow_pct = 1.0f;
    mm_napalm[i].current_len = mm_napalm[i].max_len * (0.3f + 0.7f * grow_pct);

    // Tick damage
    mm_napalm[i].tick_timer -= dt;
    if (mm_napalm[i].tick_timer <= 0.0f) {
      mm_napalm[i].tick_timer = 0.3f;
      // Check if player is inside napalm rectangle
      float nx = mm_napalm[i].x;
      float ny = mm_napalm[i].y;
      float cos_a = cosf(mm_napalm[i].angle);
      float sin_a = sinf(mm_napalm[i].angle);
      float half_w = 12.0f;
      // Project player position onto napalm local space
      float lpx = (px - nx) * cos_a + (py - ny) * sin_a;
      float lpy = -(px - nx) * sin_a + (py - ny) * cos_a;
      if (lpx > -5.0f && lpx < mm_napalm[i].current_len + 5.0f &&
          lpy > -half_w - MIMIC_PLAYER_RADIUS && lpy < half_w + MIMIC_PLAYER_RADIUS) {
        player_take_damage(MIMIC_CONTACT_DAMAGE / 3, px, py, ENEMY_TYPE_MIMIC);
      }
    }
  }

  // --- Update crater slow zones ---
  for (int i = 0; i < MIMIC_BOMB_CRATER_MAX; i++) {
    if (!mm_craters[i].active) continue;
    mm_craters[i].lifetime -= dt;
    if (mm_craters[i].lifetime <= 0.0f) {
      mm_craters[i].active = 0;
      continue;
    }
    // If player is inside crater, apply slow
    float cdx = px - mm_craters[i].x;
    float cdy = py - mm_craters[i].y;
    if (cdx * cdx + cdy * cdy <
        (mm_craters[i].radius + MIMIC_PLAYER_RADIUS) *
        (mm_craters[i].radius + MIMIC_PLAYER_RADIUS)) {
      player_apply_slow(0.6f, 0.2f);
    }
  }
}

// ============================================================================
// Draw
// ============================================================================

static void draw_ring(float cx, float cy, float r, uint8_t red, uint8_t green,
                      uint8_t blue, uint8_t alpha)
{
  int segments = 32;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, red, green, blue, alpha);
  for (int i = 0; i < segments; i++) {
    float a1 = (float)i / segments * 2.0f * PI;
    float a2 = (float)(i + 1) / segments * 2.0f * PI;
    int x1 = (int)(cx + cosf(a1) * r);
    int y1 = (int)(cy + sinf(a1) * r);
    int x2 = (int)(cx + cosf(a2) * r);
    int y2 = (int)(cy + sinf(a2) * r);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
  }
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

void mimic_bomb_draw(Enemy_t* e)
{
  (void)e;

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  // Bomb telegraph — pulsing target circle on the ground
  if (mm_bomb_telegraphing) {
    float pct = mm_bomb_telegraph_timer / MIMIC_BOMB_TELEGRAPH_TIME;
    float blast_r = weapons_get_bomb_blast_radius();
    int tx = (int)mm_bomb_tele_tx;
    int ty = (int)mm_bomb_tele_ty;

    // Flicker faster as time runs out
    float flicker_rate = 8.0f + (1.0f - pct) * 24.0f;
    float flicker = sinf(mm_bomb_telegraph_timer * flicker_rate * PI);
    int alpha = (int)(100.0f + 80.0f * (0.5f + 0.5f * flicker));
    if (alpha > 220) alpha = 220;

    // Filled danger zone (row-by-row to avoid scanline gaps)
    int ri = (int)blast_r;
    SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, (uint8_t)alpha);
    for (int row = -ri; row <= ri; row++) {
      int half = (int)sqrtf((float)(ri * ri - row * row));
      SDL_RenderDrawLine(app.renderer, tx - half, ty + row, tx + half, ty + row);
    }

    // Outer ring — brighter
    int ring_alpha = alpha + 35;
    if (ring_alpha > 255) ring_alpha = 255;
    draw_ring((float)tx, (float)ty, blast_r, 255, 255, 255, (uint8_t)ring_alpha);

    // Inner crosshair
    int ch = (int)(blast_r * 0.3f);
    SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, (uint8_t)ring_alpha);
    SDL_RenderDrawLine(app.renderer, tx - ch, ty, tx + ch, ty);
    SDL_RenderDrawLine(app.renderer, tx, ty - ch, tx, ty + ch);
  }

  // In-flight bombs (matches player: shadow circle + shrinking dot, no Y arc)
  for (int i = 0; i < MIMIC_BOMB_MAX; i++) {
    if (!mm_flights[i].active) continue;
    float p = mm_flights[i].flight_progress;
    float bx = mm_flights[i].start_x + (mm_flights[i].target_x - mm_flights[i].start_x) * p;
    float by = mm_flights[i].start_y + (mm_flights[i].target_y - mm_flights[i].start_y) * p;

    // Shadow on ground
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 30, 80);
    a_DrawFilledCircle((int)bx, (int)by, 6, (aColor_t){255, 100, 30, 80});

    // Bomb dot: shrinks at peak (progress=0.5), full at start/end
    float height_factor = sinf(p * PI);
    float bomb_size = 6.0f * (1.0f - height_factor * 0.7f);
    int bomb_alpha = 255 - (int)(height_factor * 150.0f);
    if (bomb_alpha < 50) bomb_alpha = 50;
    int br = (int)(bomb_size / 2.0f);
    if (br < 1) br = 1;
    a_DrawFilledCircle((int)bx, (int)by, br, (aColor_t){255, 180, 50, (uint8_t)bomb_alpha});
  }

  // Explosions (matches player: filled circle + outer ring)
  for (int i = 0; i < MIMIC_BOMB_MAX; i++) {
    if (!mm_explosions[i].active) continue;
    float p = 1.0f - (mm_explosions[i].timer / MIMIC_BOMB_VISUAL_DURATION);
    float current_radius = mm_explosions[i].radius * (0.3f + 0.7f * p);
    int alpha = (int)(200.0f * (1.0f - p));
    if (alpha < 0) alpha = 0;

    int ex = (int)mm_explosions[i].x;
    int ey = (int)mm_explosions[i].y;

    // Filled explosion circle
    a_DrawFilledCircle(ex, ey, (int)current_radius, (aColor_t){255, 100, 30, (uint8_t)alpha});

    // Outer ring
    int ring_alpha = alpha > 180 ? 255 : alpha + 75;
    draw_ring((float)ex, (float)ey, current_radius, 255, 200, 100, (uint8_t)ring_alpha);
  }

  // Shrapnel (matches player: 6x6 orange rects)
  for (int i = 0; i < MIMIC_BOMB_SHRAPNEL_MAX; i++) {
    if (!mm_shrapnel[i].active) continue;
    int alpha = (int)(220.0f * (mm_shrapnel[i].lifetime / 0.5f));
    if (alpha > 220) alpha = 220;
    if (alpha < 0) alpha = 0;
    a_DrawFilledRect(
      (aRectf_t){mm_shrapnel[i].x - 3, mm_shrapnel[i].y - 3, 6, 6},
      (aColor_t){255, 180, 60, (uint8_t)alpha});
  }

  // Cluster mini-bombs (matches player: 5x5 orange rects)
  for (int i = 0; i < MIMIC_BOMB_CLUSTER_MAX; i++) {
    if (!mm_clusters[i].active) continue;
    a_DrawFilledRect(
      (aRectf_t){mm_clusters[i].x - 2, mm_clusters[i].y - 2, 5, 5},
      (aColor_t){255, 140, 40, 220});
  }

  // Napalm streaks (matches player: two filled triangles + core ember line)
  for (int i = 0; i < MIMIC_BOMB_NAPALM_MAX; i++) {
    if (!mm_napalm[i].active) continue;
    float ratio = mm_napalm[i].lifetime / mm_napalm[i].max_lifetime;
    float half_len = mm_napalm[i].current_len;
    float half_w = 8.0f; // matches NAPALM_WIDTH * 0.5
    float ca = cosf(mm_napalm[i].angle);
    float sa = sinf(mm_napalm[i].angle);
    float nx = mm_napalm[i].x;
    float ny = mm_napalm[i].y;

    // Trail-style color phases (matches player)
    int r, g, b, a;
    if (ratio > 0.7f) {
      r = 255; g = 120; b = 30; a = 200;
    } else if (ratio > 0.3f) {
      r = 200; g = 70; b = 20; a = 150;
    } else {
      r = 140; g = 40; b = 10;
      a = (int)(150.0f * (ratio / 0.3f));
      if (a < 0) a = 0;
    }

    // 4 corners of the streak rectangle (centered on nx,ny)
    float ax = nx + ca * half_len - sa * half_w;
    float ay = ny + sa * half_len + ca * half_w;
    float bx = nx + ca * half_len + sa * half_w;
    float by = ny + sa * half_len - ca * half_w;
    float cx_r = nx - ca * half_len + sa * half_w;
    float cy_r = ny - sa * half_len - ca * half_w;
    float ddx = nx - ca * half_len - sa * half_w;
    float ddy = ny - sa * half_len + ca * half_w;

    // Filled streak: two triangles
    a_DrawFilledTriangle(
      (int)ax, (int)ay, (int)bx, (int)by, (int)cx_r, (int)cy_r,
      (aColor_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a});
    a_DrawFilledTriangle(
      (int)bx, (int)by, (int)cx_r, (int)cy_r, (int)ddx, (int)ddy,
      (aColor_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a});

    // Bright ember core line down the center
    if (ratio > 0.4f) {
      int core_a = (int)(120.0f * ratio);
      SDL_SetRenderDrawColor(app.renderer, 255, 220, 80, (uint8_t)core_a);
      SDL_RenderDrawLine(app.renderer,
        (int)(nx - ca * half_len * 0.9f), (int)(ny - sa * half_len * 0.9f),
        (int)(nx + ca * half_len * 0.9f), (int)(ny + sa * half_len * 0.9f));
      SDL_RenderDrawLine(app.renderer,
        (int)(nx - ca * half_len * 0.9f) + 1, (int)(ny - sa * half_len * 0.9f),
        (int)(nx + ca * half_len * 0.9f) + 1, (int)(ny + sa * half_len * 0.9f));
    }
  }

  // Crater zones (matches player: dark ground circle + thin edge ring)
  for (int i = 0; i < MIMIC_BOMB_CRATER_MAX; i++) {
    if (!mm_craters[i].active) continue;
    float fade = mm_craters[i].lifetime / mm_craters[i].max_lifetime;
    int cr = (int)mm_craters[i].radius;
    int ccx = (int)mm_craters[i].x;
    int ccy = (int)mm_craters[i].y;

    // Dark ground circle
    int ground_alpha = (int)(30.0f * fade);
    if (ground_alpha < 0) ground_alpha = 0;
    a_DrawFilledCircle(ccx, ccy, cr, (aColor_t){30, 25, 20, (uint8_t)ground_alpha});

    // Thin ring at edge
    int ring_alpha = (int)(50.0f * fade);
    if (ring_alpha > 255) ring_alpha = 255;
    draw_ring((float)ccx, (float)ccy, (float)cr, 80, 70, 50, (uint8_t)ring_alpha);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_bomb_cleanup(void)
{
  memset(mm_flights, 0, sizeof(mm_flights));
  memset(mm_explosions, 0, sizeof(mm_explosions));
  memset(mm_shrapnel, 0, sizeof(mm_shrapnel));
  memset(mm_clusters, 0, sizeof(mm_clusters));
  memset(mm_napalm, 0, sizeof(mm_napalm));
  memset(mm_craters, 0, sizeof(mm_craters));
  mm_bomb_telegraphing = 0;
  mm_bomb_telegraph_timer = 0.0f;
}
