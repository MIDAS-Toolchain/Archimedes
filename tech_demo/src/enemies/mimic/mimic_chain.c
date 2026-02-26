#include "mimic_internal.h"
#include "upgrades.h"
#include "game_audio.h"
#include <math.h>
#include <string.h>

// ============================================================================
// Hostile Chain Lightning — telegraphed bolt aimed at player position
// ============================================================================

#define MIMIC_CHAIN_TELEGRAPH_TIME 0.3f
#define MIMIC_CHAIN_HIT_RADIUS     30.0f  // how close player must be to target to get hit

typedef struct {
  float x1, y1, x2, y2;
  float timer;
} MimicChainSegment_t;

typedef struct {
  int active;
  int num_segments;
  int current_segment;
  float propagation_timer;
  float source_x, source_y;
  float target_x, target_y;
  int hit_player;
  int bounce_remaining;
  float bounce_timer;
  int telegraphing;           // 1 = showing telegraph, 0 = bolt firing
  float telegraph_timer;
  MimicChainSegment_t segments[MIMIC_CHAIN_MAX_SEGMENTS];
} MimicChainState_t;

typedef struct {
  float x, y, radius;
  float lifetime, max_lifetime, tick_timer;
  int active;
} MimicChainLinger_t;

typedef struct {
  float x, y, radius, timer;
  int active;
} MimicChainOverload_t;

static MimicChainState_t mm_chain;
static MimicChainLinger_t mm_linger[MIMIC_CHAIN_LINGER_MAX];
static MimicChainOverload_t mm_overload;

// ============================================================================
// Build chain path — zig-zag waypoints between source and target
// ============================================================================

static void build_chain_path(float sx, float sy, float tx, float ty, int count)
{
  mm_chain.source_x = sx;
  mm_chain.source_y = sy;
  mm_chain.num_segments = count;
  mm_chain.current_segment = 0;
  mm_chain.propagation_timer = MIMIC_CHAIN_DELAY;
  mm_chain.hit_player = 0;
  mm_chain.bounce_remaining = 0;
  mm_chain.bounce_timer = 0.0f;
  mm_chain.telegraphing = 0;
  mm_chain.telegraph_timer = 0.0f;

  float dx = tx - sx;
  float dy = ty - sy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) dist = 1.0f;
  float perp_x = -dy / dist;
  float perp_y =  dx / dist;

  for (int i = 0; i < count; i++) {
    float t0 = (float)i / (float)count;
    float t1 = (float)(i + 1) / (float)count;

    float x0 = sx + dx * t0;
    float y0 = sy + dy * t0;
    float x1 = sx + dx * t1;
    float y1 = sy + dy * t1;

    // Zig-zag jitter on intermediate points
    if (i > 0) {
      float jitter = RANDF(-20.0f, 20.0f);
      x0 += perp_x * jitter;
      y0 += perp_y * jitter;
    }
    if (i < count - 1) {
      float jitter = RANDF(-20.0f, 20.0f);
      x1 += perp_x * jitter;
      y1 += perp_y * jitter;
    }

    mm_chain.segments[i] = (MimicChainSegment_t){
      .x1 = x0, .y1 = y0,
      .x2 = x1, .y2 = y1,
      .timer = 0.0f
    };
  }
}

// ============================================================================
// Start telegraph — lock target position, show warning line
// ============================================================================

static void start_telegraph(float cx, float cy, float px, float py)
{
  if (mm_chain.active) return;

  mm_chain.active = 1;
  mm_chain.telegraphing = 1;
  mm_chain.telegraph_timer = MIMIC_CHAIN_TELEGRAPH_TIME;
  mm_chain.source_x = cx;
  mm_chain.source_y = cy;
  mm_chain.target_x = px; // lock player position NOW — dodgeable
  mm_chain.target_y = py;
  mm_chain.num_segments = 0;
  mm_chain.current_segment = 0;
  mm_chain.hit_player = 0;
  mm_chain.bounce_remaining = 0;
}

// ============================================================================
// On-hit effects
// ============================================================================

static void chain_hit_player(float hit_x, float hit_y)
{
  player_take_damage(MIMIC_CONTACT_DAMAGE, hit_x, hit_y, ENEMY_TYPE_MIMIC);

  // Mini-stun (slow)
  int stun_tier = upgrades_get_tier(UPG_CHAIN_MINI_STUN);
  if (stun_tier > 0) {
    static const float stun_dur[4] = { 0.0f, 0.15f, 0.25f, 0.35f };
    player_apply_slow(0.3f, stun_dur[stun_tier]);
  }

  // Linger arc at hit position
  int linger_tier = upgrades_get_tier(UPG_CHAIN_LINGER_ARC);
  if (linger_tier > 0) {
    static const float linger_dur[4] = { 0.0f, 0.75f, 1.5f, 2.25f };
    for (int i = 0; i < MIMIC_CHAIN_LINGER_MAX; i++) {
      if (!mm_linger[i].active) {
        mm_linger[i] = (MimicChainLinger_t){
          .x = hit_x, .y = hit_y,
          .radius = 30.0f,
          .lifetime = linger_dur[linger_tier],
          .max_lifetime = linger_dur[linger_tier],
          .tick_timer = 0.0f,
          .active = 1
        };
        break;
      }
    }
  }
}

static void chain_finish(float px, float py, float cx, float cy)
{
  // Overload: AoE at target position
  int overload_tier = upgrades_get_tier(UPG_CHAIN_OVERLOAD);
  if (overload_tier > 0) {
    static const float overload_radius[4] = { 0.0f, 40.0f, 60.0f, 80.0f };
    float r = overload_radius[overload_tier];
    float dx = px - mm_chain.target_x;
    float dy = py - mm_chain.target_y;
    if (dx * dx + dy * dy < (r + MIMIC_PLAYER_RADIUS) * (r + MIMIC_PLAYER_RADIUS)) {
      player_take_damage(MIMIC_CONTACT_DAMAGE / 2, mm_chain.target_x,
                         mm_chain.target_y, ENEMY_TYPE_MIMIC);
    }
    mm_overload = (MimicChainOverload_t){
      .x = mm_chain.target_x, .y = mm_chain.target_y,
      .radius = r, .timer = 0.2f,
      .active = 1
    };
  }

  // Bounce back: extra damage ticks at target pos
  int bounce_tier = upgrades_get_tier(UPG_CHAIN_BOUNCE_BACK);
  if (bounce_tier > 0) {
    mm_chain.bounce_remaining = bounce_tier;
    mm_chain.bounce_timer = MIMIC_CHAIN_DELAY * 2.0f;
    return;
  }

  (void)cx; (void)cy;
}

// ============================================================================
// Update
// ============================================================================

void mimic_chain_update(Enemy_t* e, int index, float dt,
                        float px, float py, float pvx, float pvy)
{
  (void)pvx; (void)pvy;
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;

  // --- Movement: mid-range chase with slight strafe ---
  float dx = cx - px;
  float dy = cy - py;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  float steer_x = 0.0f, steer_y = 0.0f;

  if (dist > MIMIC_RANGE_MIDCLOSE + 20.0f) {
    steer_x += -ndx * 0.6f;
    steer_y += -ndy * 0.6f;
  } else if (dist < MIMIC_RANGE_MIDCLOSE - 20.0f) {
    steer_x += ndx * 0.6f;
    steer_y += ndy * 0.6f;
  }

  float perp_x = -ndy;
  float perp_y =  ndx;
  steer_x += perp_x * 0.4f;
  steer_y += perp_y * 0.4f;

  float sep_x, sep_y;
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);
  steer_x += sep_x;
  steer_y += sep_y;

  float margin = 30.0f;
  if (cx < margin)                  steer_x += (margin - cx) / margin;
  if (cx > SCREEN_WIDTH - margin)   steer_x -= (cx - (SCREEN_WIDTH - margin)) / margin;
  if (cy < margin)                  steer_y += (margin - cy) / margin;
  if (cy > SCREEN_HEIGHT - margin)  steer_y -= (cy - (SCREEN_HEIGHT - margin)) / margin;

  float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
  if (slen > 0.1f) {
    e->vx = (steer_x / slen) * MIMIC_SPEED_CHAIN;
    e->vy = (steer_y / slen) * MIMIC_SPEED_CHAIN;
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

  // --- Attack: start telegraph when off cooldown ---
  if (e->mm_weapon_cooldown <= 0.0f && !mm_chain.active) {
    cx = e->x + stats->size / 2.0f;
    cy = e->y + stats->size / 2.0f;
    start_telegraph(cx, cy, px, py);
    e->mm_weapon_cooldown = weapons_get_slot_cooldown(e->mm_stolen_slot);
  }

  // --- Telegraph phase: wait, then fire bolt ---
  if (mm_chain.active && mm_chain.telegraphing) {
    // Update source position to follow mimic during telegraph
    mm_chain.source_x = e->x + stats->size / 2.0f;
    mm_chain.source_y = e->y + stats->size / 2.0f;

    mm_chain.telegraph_timer -= dt;
    if (mm_chain.telegraph_timer <= 0.0f) {
      // Fire the actual bolt
      mm_chain.telegraphing = 0;
      int jumps = 3 + upgrades_get_tier(UPG_CHAIN_EXTRA_JUMPS);
      if (jumps > MIMIC_CHAIN_MAX_SEGMENTS) jumps = MIMIC_CHAIN_MAX_SEGMENTS;

      cx = e->x + stats->size / 2.0f;
      cy = e->y + stats->size / 2.0f;
      build_chain_path(cx, cy, mm_chain.target_x, mm_chain.target_y, jumps);
      mm_chain.active = 1; // build_chain_path doesn't set this
      game_audio_play_chain_attack();
    }
  }

  // --- Propagate chain bolt ---
  if (mm_chain.active && !mm_chain.telegraphing) {
    if (mm_chain.bounce_remaining > 0 && mm_chain.current_segment >= mm_chain.num_segments) {
      mm_chain.bounce_timer -= dt;
      if (mm_chain.bounce_timer <= 0.0f) {
        mm_chain.bounce_timer = MIMIC_CHAIN_DELAY * 2.0f;
        mm_chain.bounce_remaining--;
        // Re-check if player is near target
        float tdx = px - mm_chain.target_x;
        float tdy = py - mm_chain.target_y;
        if (tdx * tdx + tdy * tdy <
            (MIMIC_CHAIN_HIT_RADIUS + MIMIC_PLAYER_RADIUS) *
            (MIMIC_CHAIN_HIT_RADIUS + MIMIC_PLAYER_RADIUS)) {
          player_take_damage(MIMIC_CONTACT_DAMAGE / 2, mm_chain.target_x,
                             mm_chain.target_y, ENEMY_TYPE_MIMIC);
        }
        if (mm_chain.num_segments > 0) {
          mm_chain.segments[mm_chain.num_segments - 1].timer = MIMIC_CHAIN_VISUAL_DURATION;
        }
        if (mm_chain.bounce_remaining <= 0) {
          mm_chain.active = 0;
        }
      }
    } else if (mm_chain.current_segment < mm_chain.num_segments) {
      mm_chain.propagation_timer -= dt;
      if (mm_chain.propagation_timer <= 0.0f) {
        mm_chain.propagation_timer = MIMIC_CHAIN_DELAY;
        int seg = mm_chain.current_segment;
        mm_chain.segments[seg].timer = MIMIC_CHAIN_VISUAL_DURATION;
        mm_chain.current_segment++;

        // Last segment — check if player is near the target position
        if (mm_chain.current_segment >= mm_chain.num_segments && !mm_chain.hit_player) {
          float hdx = px - mm_chain.target_x;
          float hdy = py - mm_chain.target_y;
          if (hdx * hdx + hdy * hdy <
              (MIMIC_CHAIN_HIT_RADIUS + MIMIC_PLAYER_RADIUS) *
              (MIMIC_CHAIN_HIT_RADIUS + MIMIC_PLAYER_RADIUS)) {
            mm_chain.hit_player = 1;
            cx = e->x + stats->size / 2.0f;
            cy = e->y + stats->size / 2.0f;
            chain_hit_player(mm_chain.target_x, mm_chain.target_y);
          }
          cx = e->x + stats->size / 2.0f;
          cy = e->y + stats->size / 2.0f;
          chain_finish(px, py, cx, cy);
        }
      }
    } else {
      // All segments done, wait for visuals to fade
      int any_visible = 0;
      for (int i = 0; i < mm_chain.num_segments; i++) {
        if (mm_chain.segments[i].timer > 0.0f) { any_visible = 1; break; }
      }
      if (!any_visible) {
        mm_chain.active = 0;
      }
    }
  }

  // --- Update segment visual timers ---
  for (int i = 0; i < mm_chain.num_segments; i++) {
    if (mm_chain.segments[i].timer > 0.0f) {
      mm_chain.segments[i].timer -= dt;
    }
  }

  // --- Overload visual ---
  if (mm_overload.active) {
    mm_overload.timer -= dt;
    if (mm_overload.timer <= 0.0f) mm_overload.active = 0;
  }

  // --- Linger zones ---
  for (int i = 0; i < MIMIC_CHAIN_LINGER_MAX; i++) {
    if (!mm_linger[i].active) continue;
    mm_linger[i].lifetime -= dt;
    if (mm_linger[i].lifetime <= 0.0f) {
      mm_linger[i].active = 0;
      continue;
    }
    mm_linger[i].tick_timer -= dt;
    if (mm_linger[i].tick_timer <= 0.0f) {
      mm_linger[i].tick_timer = 0.3f;
      float ldx = px - mm_linger[i].x;
      float ldy = py - mm_linger[i].y;
      if (ldx * ldx + ldy * ldy <
          (mm_linger[i].radius + MIMIC_PLAYER_RADIUS) *
          (mm_linger[i].radius + MIMIC_PLAYER_RADIUS)) {
        player_take_damage(MIMIC_CONTACT_DAMAGE / 3, mm_linger[i].x,
                           mm_linger[i].y, ENEMY_TYPE_MIMIC);
      }
    }
  }
}

// ============================================================================
// Draw
// ============================================================================

static void draw_ring(float rcx, float rcy, float r, uint8_t red, uint8_t green,
                      uint8_t blue, uint8_t alpha)
{
  int segments = 32;
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(app.renderer, red, green, blue, alpha);
  for (int i = 0; i < segments; i++) {
    float a1 = (float)i / segments * 2.0f * PI;
    float a2 = (float)(i + 1) / segments * 2.0f * PI;
    int x1 = (int)(rcx + cosf(a1) * r);
    int y1 = (int)(rcy + sinf(a1) * r);
    int x2 = (int)(rcx + cosf(a2) * r);
    int y2 = (int)(rcy + sinf(a2) * r);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
  }
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

void mimic_chain_draw(Enemy_t* e)
{
  (void)e;

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  // Telegraph: flickering targeting line from mimic to locked position
  if (mm_chain.active && mm_chain.telegraphing) {
    float pct = mm_chain.telegraph_timer / MIMIC_CHAIN_TELEGRAPH_TIME;
    // Flicker faster as it gets closer to firing
    float flicker_rate = 8.0f + (1.0f - pct) * 20.0f;
    float flicker = sinf(mm_chain.telegraph_timer * flicker_rate * PI);
    int alpha = (int)(80.0f + 80.0f * fabsf(flicker));
    if (alpha > 200) alpha = 200;

    int sx = (int)mm_chain.source_x;
    int sy = (int)mm_chain.source_y;
    int tx = (int)mm_chain.target_x;
    int ty = (int)mm_chain.target_y;

    // Thin warning line
    SDL_SetRenderDrawColor(app.renderer, 150, 200, 255, (uint8_t)alpha);
    SDL_RenderDrawLine(app.renderer, sx, sy, tx, ty);

    // Target indicator circle
    int target_alpha = (int)(60.0f + 60.0f * fabsf(flicker));
    draw_ring((float)tx, (float)ty, MIMIC_CHAIN_HIT_RADIUS,
              150, 200, 255, (uint8_t)target_alpha);
  }

  // Chain lightning segments (matches player: white-blue core, 5 parallel lines)
  for (int i = 0; i < mm_chain.num_segments; i++) {
    if (mm_chain.segments[i].timer <= 0.0f) continue;

    float alpha_pct = mm_chain.segments[i].timer / MIMIC_CHAIN_VISUAL_DURATION;
    int alpha = (int)(alpha_pct * 255.0f);
    if (alpha > 255) alpha = 255;
    if (alpha < 0) alpha = 0;

    int x1 = (int)mm_chain.segments[i].x1;
    int y1 = (int)mm_chain.segments[i].y1;
    int x2 = (int)mm_chain.segments[i].x2;
    int y2 = (int)mm_chain.segments[i].y2;

    // White-blue core (matches player)
    SDL_SetRenderDrawColor(app.renderer, 200, 220, 255, (uint8_t)alpha);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
    // Thicken with 4 parallel lines (matches player)
    SDL_RenderDrawLine(app.renderer, x1 - 1, y1, x2 - 1, y2);
    SDL_RenderDrawLine(app.renderer, x1 + 1, y1, x2 + 1, y2);
    SDL_RenderDrawLine(app.renderer, x1, y1 - 1, x2, y2 - 1);
    SDL_RenderDrawLine(app.renderer, x1, y1 + 1, x2, y2 + 1);
  }

  // Linger zones (cyan pulsing circles)
  for (int i = 0; i < MIMIC_CHAIN_LINGER_MAX; i++) {
    if (!mm_linger[i].active) continue;
    float pct = mm_linger[i].lifetime / mm_linger[i].max_lifetime;
    int a = (int)(60.0f * pct);
    a_DrawFilledCircle((int)mm_linger[i].x, (int)mm_linger[i].y,
                       (int)mm_linger[i].radius,
                       (aColor_t){100, 200, 255, (uint8_t)a});
    draw_ring(mm_linger[i].x, mm_linger[i].y, mm_linger[i].radius,
              100, 200, 255, (uint8_t)(a * 2 > 255 ? 255 : a * 2));
  }

  // Overload AoE visual
  if (mm_overload.active) {
    float pct = mm_overload.timer / 0.2f;
    float r = mm_overload.radius * (0.5f + 0.5f * (1.0f - pct));
    int alpha = (int)(180.0f * pct);
    a_DrawFilledCircle((int)mm_overload.x, (int)mm_overload.y,
                       (int)(r * 0.5f),
                       (aColor_t){100, 200, 255, (uint8_t)(alpha / 3)});
    draw_ring(mm_overload.x, mm_overload.y, r, 100, 200, 255, (uint8_t)alpha);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Cleanup
// ============================================================================

void mimic_chain_cleanup(void)
{
  memset(&mm_chain, 0, sizeof(mm_chain));
  memset(mm_linger, 0, sizeof(mm_linger));
  memset(&mm_overload, 0, sizeof(mm_overload));
}
