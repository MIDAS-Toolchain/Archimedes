#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Archimedes.h"
#include "snake.h"
#include "player_actions.h"
#include "weapons.h"
#include "upgrades.h"
#include "fire_particles.h"
#include "xp.h"
#include "stats.h"
#include "enemy.h"
#include "progress.h"
#include "game_audio.h"
#include "game_director.h"
#include "achievements.h"

// ============================================================================
// Constants
// ============================================================================

#define SNAKE_MAX_ACTIVE       4
#define SNAKE_MAX_SEGMENTS    32
#define SNAKE_HISTORY_SIZE   512

#define SNAKE_HEAD_SIZE       20.0f
#define SNAKE_SEGMENT_SIZE    16.0f
#define SNAKE_SEGMENT_SPACING 18
#define SNAKE_HEAD_RADIUS     10.0f
#define SNAKE_SEGMENT_RADIUS   8.0f
#define SNAKE_EYE_SIZE         4.0f

#define SNAKE_BASE_SPEED     180.0f
#define SNAKE_CHARGE2_MULT     1.3f
#define SNAKE_CHARGE3_MULT     1.8f
#define SNAKE_SPEED_PER_LOST  12.0f
#define SNAKE_CYCLE_SPEED_MULT 0.12f  // +12% speed per completed cycle
#define SNAKE_TELEGRAPH_TIME   1.0f
#define SNAKE_PULLBACK_DIST   12.0f    // Head recoils this many pixels during windup
#define SNAKE_REST_MIN         3.0f
#define SNAKE_REST_MAX         5.0f
#define SNAKE_CONTACT_DAMAGE  20
#define SNAKE_SEGMENT_BASE_HP  3

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Types
// ============================================================================

typedef enum {
  SNAKE_STATE_INACTIVE,
  SNAKE_STATE_OFFSCREEN,
  SNAKE_STATE_ENTERING,
  SNAKE_STATE_CHARGING,
  SNAKE_STATE_PAUSING,
  SNAKE_STATE_CHARGING_EXIT,
} SnakeState_t;

typedef struct { float x, y; } SnakePos_t;
typedef struct { int hp, max_hp; } SnakeSegment_t;

typedef struct {
  int active;
  SnakeState_t state;

  float head_x, head_y;
  float dir_x, dir_y;
  float speed;

  SnakePos_t history[SNAKE_HISTORY_SIZE];
  int history_head;
  int history_count;

  SnakeSegment_t segments[SNAKE_MAX_SEGMENTS];
  int segment_count;
  int total_segments;
  int hp_per_segment;

  int head_hp, head_max_hp;

  int charge_number;
  float pause_timer;
  float rest_timer;

  float target_x, target_y;     // Where the current charge is aiming
  int has_target;                // Whether we're committed to a target

  float hit_cooldown;            // Invulnerability timer after taking damage
  int cycle_count;               // Times the snake has completed a full charge cycle

  float stun_timer;
  float stun_damage_mult;
  float conductor_timer;
  int conductor_jumps;

  float tail_pulse_timer;

  // Windup telegraph: pre-computed next charge direction
  float next_dir_x, next_dir_y;
  float telegraph_max;           // Initial pause_timer value (for progress calc)

  float linger_cd;               // Per-snake cooldown for lingering damage (0.5s)
  float spawn_time;              // elapsed game time when spawned
} Snake_t;

static Snake_t snakes[SNAKE_MAX_ACTIVE];

// ============================================================================
// Scale Shatter Projectiles
// ============================================================================

#define SNAKE_SHATTER_MAX     64
#define SNAKE_SHATTER_SPEED   250.0f
#define SNAKE_SHATTER_LIFE    1.5f
#define SNAKE_SHATTER_SIZE    8.0f
#define SNAKE_SHATTER_HOMING  550.0f

typedef struct {
  float x, y, vx, vy, life;
  int pierces_left;
  int active;
} SnakeShatterProj_t;

static SnakeShatterProj_t snake_shatters[SNAKE_SHATTER_MAX];

// ============================================================================
// Helpers
// ============================================================================

static void aim_direction(float from_x, float from_y, float to_x, float to_y,
                          float* out_dx, float* out_dy)
{
  float dx = to_x - from_x;
  float dy = to_y - from_y;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.1f) { *out_dx = 1.0f; *out_dy = 0.0f; return; }
  *out_dx = dx / len;
  *out_dy = dy / len;
}

static float snake_get_speed(Snake_t* s, float base)
{
  int lost = s->total_segments - s->segment_count;
  float spd = base + lost * SNAKE_SPEED_PER_LOST;
  spd *= (1.0f + s->cycle_count * SNAKE_CYCLE_SPEED_MULT);
  return spd;
}

// Record history at fixed 1px intervals, interpolating when the head moves
// multiple pixels in a single frame (critical for framerate independence)
static void snake_record_history(Snake_t* s)
{
  int prev = (s->history_head - 1 + SNAKE_HISTORY_SIZE) % SNAKE_HISTORY_SIZE;
  float dx = s->head_x - s->history[prev].x;
  float dy = s->head_y - s->history[prev].y;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 1.0f) return;

  // Interpolate to record one entry per pixel of movement
  int steps = (int)dist;
  if (steps > 32) steps = 32;  // safety cap per frame
  float inv_dist = 1.0f / dist;
  float step_dx = dx * inv_dist;
  float step_dy = dy * inv_dist;
  float start_x = s->history[prev].x;
  float start_y = s->history[prev].y;

  for (int i = 1; i <= steps; i++) {
    float t = (float)i;
    s->history[s->history_head] = (SnakePos_t){
      start_x + step_dx * t,
      start_y + step_dy * t
    };
    s->history_head = (s->history_head + 1) % SNAKE_HISTORY_SIZE;
    if (s->history_count < SNAKE_HISTORY_SIZE) s->history_count++;
  }
}

static void snake_get_segment_pos(Snake_t* s, int seg_index, float* ox, float* oy)
{
  int offset = (seg_index + 1) * SNAKE_SEGMENT_SPACING;
  if (offset >= s->history_count) {
    *ox = s->head_x;
    *oy = s->head_y;
    return;
  }
  int idx = (s->history_head - 1 - offset + SNAKE_HISTORY_SIZE) % SNAKE_HISTORY_SIZE;
  *ox = s->history[idx].x;
  *oy = s->history[idx].y;
}

static int snake_is_on_screen(float x, float y, float margin)
{
  return x >= -margin && x <= SCREEN_WIDTH + margin &&
         y >= -margin && y <= SCREEN_HEIGHT + margin;
}

static int snake_entirely_offscreen(Snake_t* s)
{
  float margin = SNAKE_HEAD_SIZE;
  if (snake_is_on_screen(s->head_x, s->head_y, margin)) return 0;
  for (int i = 0; i < s->segment_count; i++) {
    float sx, sy;
    snake_get_segment_pos(s, i, &sx, &sy);
    if (snake_is_on_screen(sx, sy, margin)) return 0;
  }
  return 1;
}

// ============================================================================
// Damage System
// ============================================================================

static void snake_fire_shatter(float x, float y, int count, int pierces)
{
  float angle_start = RANDF(0.0f, (float)(2.0 * M_PI));
  float angle_step = (float)(2.0 * M_PI) / (float)count;
  for (int i = 0; i < count; i++) {
    // Find free slot
    int slot = -1;
    for (int j = 0; j < SNAKE_SHATTER_MAX; j++) {
      if (!snake_shatters[j].active) { slot = j; break; }
    }
    if (slot < 0) break;

    float angle = angle_start + angle_step * (float)i;
    snake_shatters[slot].x = x;
    snake_shatters[slot].y = y;
    snake_shatters[slot].vx = cosf(angle) * SNAKE_SHATTER_SPEED;
    snake_shatters[slot].vy = sinf(angle) * SNAKE_SHATTER_SPEED;
    snake_shatters[slot].life = SNAKE_SHATTER_LIFE;
    snake_shatters[slot].pierces_left = pierces;
    snake_shatters[slot].active = 1;
  }
}

static void snake_pop_segment(int snake_index, int seg_index)
{
  Snake_t* s = &snakes[snake_index];
  float sx, sy;
  snake_get_segment_pos(s, seg_index, &sx, &sy);

  // Orange fire particle burst
  for (int i = 0; i < 6; i++) {
    fire_particles_spawn(sx, sy, RANDF(-1.0f, 1.0f), RANDF(-1.0f, 1.0f));
  }

  // Drop 1 XP orb
  xp_spawn_orbs(sx, sy, ENEMY_TYPE_GRUNT);

  // Scale shatter projectiles
  int proj_count = progress_get_snake_shatter_projectiles();
  if (proj_count > 0)
    snake_fire_shatter(sx, sy, proj_count, progress_get_snake_shatter_pierces());

  game_audio_play_snake_losetail();
  s->segment_count--;
}

static void snake_kill(int snake_index)
{
  Snake_t* s = &snakes[snake_index];

  // Large particle burst at head
  for (int i = 0; i < 12; i++) {
    fire_particles_spawn(s->head_x, s->head_y,
                         RANDF(-1.0f, 1.0f), RANDF(-1.0f, 1.0f));
  }

  // Drop 5 XP orbs
  for (int i = 0; i < 5; i++) {
    xp_spawn_orbs(s->head_x, s->head_y, ENEMY_TYPE_GRUNT);
  }

  game_audio_play_snake_die();
  stats_record_kill(ENEMY_TYPE_SNAKE);

  // Speed-kill achievement: killed within 30s of spawning
  if (director_get_elapsed() - s->spawn_time <= 30.0f)
    achievements_notify_boss_speedkill(ENEMY_TYPE_SNAKE);

  s->active = 0;
  s->state = SNAKE_STATE_INACTIVE;
}

#define SNAKE_HIT_COOLDOWN 0.15f

static void snake_damage_tail(int snake_index)
{
  Snake_t* s = &snakes[snake_index];

  // Hit cooldown — prevent multiple weapons melting the tail in one frame
  if (s->hit_cooldown > 0.0f) return;
  s->hit_cooldown = SNAKE_HIT_COOLDOWN;

  game_audio_play_snake_gethit();

  int hit_increment = 1;
  if (s->stun_timer > 0.0f && s->stun_damage_mult > 1.0f) {
    hit_increment++;
  }

  if (s->segment_count > 0) {
    int last = s->segment_count - 1;
    s->segments[last].hp -= hit_increment;

    float sx, sy;
    snake_get_segment_pos(s, last, &sx, &sy);
    fire_particles_spawn(sx, sy, RANDF(-1.0f, 1.0f), RANDF(-1.0f, 1.0f));

    if (s->segments[last].hp <= 0) {
      snake_pop_segment(snake_index, last);
    }
  } else {
    s->head_hp -= hit_increment;
    fire_particles_spawn(s->head_x, s->head_y,
                         RANDF(-1.0f, 1.0f), RANDF(-1.0f, 1.0f));
    if (s->head_hp <= 0) {
      snake_kill(snake_index);
    }
  }

  weapons_record_hit();
}

// ============================================================================
// Init / Cleanup
// ============================================================================

void snake_init(void)
{
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    snakes[i].active = 0;
    snakes[i].state = SNAKE_STATE_INACTIVE;
  }
  memset(snake_shatters, 0, sizeof(snake_shatters));
}

void snake_cleanup(void)
{
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    snakes[i].active = 0;
    snakes[i].state = SNAKE_STATE_INACTIVE;
  }
  memset(snake_shatters, 0, sizeof(snake_shatters));
}

// ============================================================================
// Spawn
// ============================================================================

void snake_spawn(int segment_count, int hp_per_segment)
{
  // Find empty slot
  int slot = -1;
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    if (!snakes[i].active) { slot = i; break; }
  }
  if (slot < 0) return;

  if (segment_count > SNAKE_MAX_SEGMENTS) segment_count = SNAKE_MAX_SEGMENTS;

  Snake_t* s = &snakes[slot];
  s->active = 1;
  s->state = SNAKE_STATE_OFFSCREEN;
  s->rest_timer = 0.0f;  // Immediate entry on first spawn

  s->head_x = -100.0f;
  s->head_y = -100.0f;
  s->dir_x = 1.0f;
  s->dir_y = 0.0f;
  s->speed = SNAKE_BASE_SPEED;
  s->target_x = 0.0f;
  s->target_y = 0.0f;
  s->has_target = 0;
  s->hit_cooldown = 0.0f;
  s->cycle_count = 0;

  s->history_head = 0;
  s->history_count = 0;

  s->segment_count = segment_count;
  s->total_segments = segment_count;
  s->hp_per_segment = hp_per_segment;
  for (int i = 0; i < segment_count; i++) {
    s->segments[i].hp = hp_per_segment;
    s->segments[i].max_hp = hp_per_segment;
  }

  s->head_hp = 6;
  s->head_max_hp = 6;

  s->charge_number = 0;
  s->pause_timer = 0.0f;

  s->stun_timer = 0.0f;
  s->stun_damage_mult = 1.0f;
  s->conductor_timer = 0.0f;
  s->conductor_jumps = 0;

  s->tail_pulse_timer = 0.0f;
  s->spawn_time = director_get_elapsed();
}

// ============================================================================
// State Machine — Entry from Offscreen
// ============================================================================

static void snake_pick_entry(Snake_t* s, float px, float py)
{
  int edge = rand() % 4;
  float margin = SNAKE_HEAD_SIZE + 10.0f;
  switch (edge) {
    case 0: // Top
      s->head_x = RANDF(margin, SCREEN_WIDTH - margin);
      s->head_y = -margin;
      break;
    case 1: // Bottom
      s->head_x = RANDF(margin, SCREEN_WIDTH - margin);
      s->head_y = SCREEN_HEIGHT + margin;
      break;
    case 2: // Left
      s->head_x = -margin;
      s->head_y = RANDF(margin, SCREEN_HEIGHT - margin);
      break;
    case 3: // Right
      s->head_x = SCREEN_WIDTH + margin;
      s->head_y = RANDF(margin, SCREEN_HEIGHT - margin);
      break;
  }
  aim_direction(s->head_x, s->head_y, px, py, &s->dir_x, &s->dir_y);
  s->speed = snake_get_speed(s, SNAKE_BASE_SPEED);
  s->target_x = px;
  s->target_y = py;
  s->has_target = 1;

  // Pre-fill history so segments start stacked behind head
  for (int i = 0; i < SNAKE_HISTORY_SIZE; i++) {
    s->history[i] = (SnakePos_t){s->head_x, s->head_y};
  }
  s->history_head = 0;
  s->history_count = SNAKE_HISTORY_SIZE;
}

// ============================================================================
// State Machine — Pick Charge Direction
// ============================================================================

static void snake_pick_charge_dir(Snake_t* s, float px, float py,
                                  float pvx, float pvy)
{
  static const float mults[3] = { 1.0f, SNAKE_CHARGE2_MULT, SNAKE_CHARGE3_MULT };
  float mult = mults[s->charge_number < 3 ? s->charge_number : 2];
  s->speed = snake_get_speed(s, SNAKE_BASE_SPEED * mult);

  float tx = px;
  float ty = py;

  // Prediction for charges 1 and 2
  if (s->charge_number >= 1) {
    float dx = px - s->head_x;
    float dy = py - s->head_y;
    float dist = sqrtf(dx * dx + dy * dy);
    float lead_time = dist / s->speed;
    if (s->charge_number >= 2) lead_time *= 1.5f;  // Aggressive
    tx = px + pvx * lead_time;
    ty = py + pvy * lead_time;
  }

  aim_direction(s->head_x, s->head_y, tx, ty, &s->dir_x, &s->dir_y);
  s->target_x = tx;
  s->target_y = ty;
  s->has_target = 1;
}

// ============================================================================
// Update
// ============================================================================

void snake_update(float dt, float px, float py, float pvx, float pvy)
{
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;

    s->tail_pulse_timer += dt;

    // Hit cooldown tick
    if (s->hit_cooldown > 0.0f) s->hit_cooldown -= dt;
    if (s->linger_cd > 0.0f) s->linger_cd -= dt;

    // Conductor tick
    if (s->conductor_timer > 0.0f) {
      s->conductor_timer -= dt;
      if (s->conductor_timer <= 0.0f) {
        s->conductor_timer = 0.0f;
        // Fire conductor chain from snake head position
        weapons_fire_conductor_chain_at(s->head_x, s->head_y, s->conductor_jumps);
      }
    }

    // Stun tick
    if (s->stun_timer > 0.0f) {
      s->stun_timer -= dt;
      if (s->stun_timer <= 0.0f) {
        s->stun_timer = 0.0f;
        s->stun_damage_mult = 1.0f;
      }
      continue;  // Stunned — skip movement
    }

    // Slow effects (crater, turret slow, trail scorched earth, player aura)
    float slow_mult = 1.0f;
    if (s->state != SNAKE_STATE_OFFSCREEN && s->state != SNAKE_STATE_INACTIVE) {
      float dummy;
      if (weapons_is_in_crater(s->head_x, s->head_y, &dummy))
        slow_mult *= 0.5f;
      float turret_slow;
      if (weapons_get_turret_slow(s->head_x, s->head_y, &turret_slow))
        slow_mult *= turret_slow;
      slow_mult *= weapons_get_trail_slow(s->head_x, s->head_y);
      float aura_r = player_get_slow_aura_radius();
      if (aura_r > 0.0f) {
        float adx = s->head_x - (px);
        float ady = s->head_y - (py);
        if (sqrtf(adx * adx + ady * ady) <= aura_r)
          slow_mult *= 0.5f;
      }
    }
    float effective_speed = s->speed * slow_mult;

    switch (s->state) {
      case SNAKE_STATE_INACTIVE:
        break;

      case SNAKE_STATE_OFFSCREEN:
        s->rest_timer -= dt;
        if (s->rest_timer <= 0.0f) {
          snake_pick_entry(s, px, py);
          s->state = SNAKE_STATE_ENTERING;
          s->charge_number = 0;
        }
        break;

      case SNAKE_STATE_ENTERING:
        s->head_x += s->dir_x * effective_speed * dt;
        s->head_y += s->dir_y * effective_speed * dt;
        snake_record_history(s);

        if (snake_is_on_screen(s->head_x, s->head_y, 0.0f)) {
          s->charge_number = 0;
          snake_pick_charge_dir(s, px, py, pvx, pvy);
          s->state = SNAKE_STATE_CHARGING;
        }
        break;

      case SNAKE_STATE_CHARGING: {
        s->head_x += s->dir_x * effective_speed * dt;
        s->head_y += s->dir_y * effective_speed * dt;
        snake_record_history(s);

        // Check if we've reached or passed the target
        float to_target_x = s->target_x - s->head_x;
        float to_target_y = s->target_y - s->head_y;
        float dot = to_target_x * s->dir_x + to_target_y * s->dir_y;
        int enter_pause = 0;
        if (dot <= 0.0f) {
          s->head_x = s->target_x;
          s->head_y = s->target_y;
          enter_pause = 1;
        }

        // Gentle wall bounce: reflect and keep moving (don't pause at wall)
        {
          int bounced = 0;
          if (s->head_x < 0)            { s->head_x = 0;            s->dir_x = fabsf(s->dir_x);  bounced = 1; }
          if (s->head_x > SCREEN_WIDTH)  { s->head_x = SCREEN_WIDTH; s->dir_x = -fabsf(s->dir_x); bounced = 1; }
          if (s->head_y < 0)             { s->head_y = 0;            s->dir_y = fabsf(s->dir_y);  bounced = 1; }
          if (s->head_y > SCREEN_HEIGHT) { s->head_y = SCREEN_HEIGHT; s->dir_y = -fabsf(s->dir_y); bounced = 1; }
          if (bounced) {
            // Re-normalize direction
            float len = sqrtf(s->dir_x * s->dir_x + s->dir_y * s->dir_y);
            if (len > 0.01f) { s->dir_x /= len; s->dir_y /= len; }
            // Set new target 200px along bounced direction (short slide inward)
            s->target_x = s->head_x + s->dir_x * 200.0f;
            s->target_y = s->head_y + s->dir_y * 200.0f;
            // Don't set enter_pause — let the snake slide inward first
          }
        }

        if (enter_pause) {
          s->state = SNAKE_STATE_PAUSING;
          s->pause_timer = SNAKE_TELEGRAPH_TIME;
          s->telegraph_max = SNAKE_TELEGRAPH_TIME;
          // Pre-compute next charge direction for telegraph
          int next_charge = s->charge_number + 1;
          if (next_charge >= 3) {
            // Will exit — aim away from player (toward nearest edge)
            float ex = (s->head_x < SCREEN_WIDTH / 2) ? -100.0f : SCREEN_WIDTH + 100.0f;
            float ey = (s->head_y < SCREEN_HEIGHT / 2) ? -100.0f : SCREEN_HEIGHT + 100.0f;
            aim_direction(s->head_x, s->head_y, ex, ey,
                          &s->next_dir_x, &s->next_dir_y);
          } else {
            // Peek at where next charge would aim (using current player pos)
            float ntx = px;
            float nty = py;
            if (next_charge >= 1) {
              float ndx = px - s->head_x;
              float ndy = py - s->head_y;
              float ndist = sqrtf(ndx * ndx + ndy * ndy);
              float nspd = snake_get_speed(s, SNAKE_BASE_SPEED *
                (next_charge == 1 ? SNAKE_CHARGE2_MULT : SNAKE_CHARGE3_MULT));
              float lead = ndist / (nspd > 0.1f ? nspd : 1.0f);
              if (next_charge >= 2) lead *= 1.5f;
              ntx = px + pvx * lead;
              nty = py + pvy * lead;
            }
            aim_direction(s->head_x, s->head_y, ntx, nty,
                          &s->next_dir_x, &s->next_dir_y);
          }
        }
        break;
      }

      case SNAKE_STATE_PAUSING: {
        s->pause_timer -= dt;

        // Pullback: smoothly recoil opposite to next charge direction
        float progress = 1.0f - (s->pause_timer / s->telegraph_max);
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        // Pull back during first half, hold during second half
        if (progress < 0.5f) {
          float pull_speed = SNAKE_PULLBACK_DIST / (s->telegraph_max * 0.5f);
          s->head_x -= s->next_dir_x * pull_speed * dt;
          s->head_y -= s->next_dir_y * pull_speed * dt;
          snake_record_history(s);
        }

        // 3rd charge windup: slither side-to-side
        if (s->charge_number == 2) {
          float perp_x = -s->next_dir_y;
          float perp_y =  s->next_dir_x;
          float freq = 12.0f + 8.0f * progress;
          float amp = (6.0f + 10.0f * progress) * dt * freq;
          float wave = sinf(s->tail_pulse_timer * freq);
          s->head_x += perp_x * wave * amp;
          s->head_y += perp_y * wave * amp;
          snake_record_history(s);
        }

        if (s->pause_timer <= 0.0f) {
          s->charge_number++;
          // Always use the pre-computed direction (shown to player via telegraph)
          s->dir_x = s->next_dir_x;
          s->dir_y = s->next_dir_y;
          if (s->charge_number >= 3) {
            s->speed = snake_get_speed(s, SNAKE_BASE_SPEED * SNAKE_CHARGE3_MULT);
            s->target_x = s->head_x + s->dir_x * 2000.0f;
            s->target_y = s->head_y + s->dir_y * 2000.0f;
            s->has_target = 1;
            s->state = SNAKE_STATE_CHARGING_EXIT;
          } else {
            static const float mults[3] = { 1.0f, SNAKE_CHARGE2_MULT, SNAKE_CHARGE3_MULT };
            float mult = mults[s->charge_number < 3 ? s->charge_number : 2];
            s->speed = snake_get_speed(s, SNAKE_BASE_SPEED * mult);
            // Set target far along the direction
            s->target_x = s->head_x + s->next_dir_x * 2000.0f;
            s->target_y = s->head_y + s->next_dir_y * 2000.0f;
            s->has_target = 1;
            s->state = SNAKE_STATE_CHARGING;
          }
        }
        break;
      }

      case SNAKE_STATE_CHARGING_EXIT:
        s->head_x += s->dir_x * effective_speed * dt;
        s->head_y += s->dir_y * effective_speed * dt;
        snake_record_history(s);

        if (snake_entirely_offscreen(s)) {
          s->cycle_count++;
          s->state = SNAKE_STATE_OFFSCREEN;
          // Rest shrinks each cycle: 3-5, 2-4, 1-3, 1-2, then flat 1
          float rest_min = SNAKE_REST_MIN - (float)s->cycle_count;
          float rest_max = SNAKE_REST_MAX - (float)s->cycle_count;
          if (rest_min < 1.0f) rest_min = 1.0f;
          if (rest_max < rest_min) rest_max = rest_min;
          s->rest_timer = RANDF(rest_min, rest_max);
        }
        break;
    }

    // Player contact damage
    if (s->state != SNAKE_STATE_OFFSCREEN && s->state != SNAKE_STATE_INACTIVE) {
      float player_r = 16.0f;
      int hit_player = 0;
      float hit_x = s->head_x, hit_y = s->head_y;

      // Head
      float dx = s->head_x - px;
      float dy = s->head_y - py;
      if (sqrtf(dx * dx + dy * dy) < SNAKE_HEAD_RADIUS + player_r) {
        hit_player = 1;
      }

      // Segments
      if (!hit_player) {
        for (int seg = 0; seg < s->segment_count; seg++) {
          float sx, sy;
          snake_get_segment_pos(s, seg, &sx, &sy);
          dx = sx - px;
          dy = sy - py;
          if (sqrtf(dx * dx + dy * dy) < SNAKE_SEGMENT_RADIUS + player_r) {
            hit_player = 1;
            hit_x = sx;
            hit_y = sy;
            break;
          }
        }
      }

      if (hit_player) {
        int dmg = SNAKE_CONTACT_DAMAGE - progress_get_snake_dmg_reduction();
        if (dmg < 1) dmg = 1;
        player_take_damage(dmg, hit_x, hit_y, ENEMY_TYPE_SNAKE);
      }
    }
  }
}

// ============================================================================
// Drawing
// ============================================================================

// Helper: compute where the snake's path hits the screen edge
static void snake_edge_intersection(Snake_t* s, float* out_x, float* out_y)
{
  // March along dir from head until we hit the screen rect [0, SCREEN_WIDTH] x [0, SCREEN_HEIGHT]
  float ix = s->head_x, iy = s->head_y;
  float best_t = 1e9f;

  // Check each edge
  if (s->dir_x > 0.001f) { // hits left edge (x=0)
    float t = (0.0f - s->head_x) / s->dir_x;
    if (t > 0.0f && t < best_t) { float yy = s->head_y + s->dir_y * t; if (yy >= 0 && yy <= SCREEN_HEIGHT) { best_t = t; ix = 0; iy = yy; } }
  }
  if (s->dir_x < -0.001f) { // hits right edge (x=SCREEN_WIDTH)
    float t = ((float)SCREEN_WIDTH - s->head_x) / s->dir_x;
    if (t > 0.0f && t < best_t) { float yy = s->head_y + s->dir_y * t; if (yy >= 0 && yy <= SCREEN_HEIGHT) { best_t = t; ix = SCREEN_WIDTH; iy = yy; } }
  }
  if (s->dir_y > 0.001f) { // hits top edge (y=0)
    float t = (0.0f - s->head_y) / s->dir_y;
    if (t > 0.0f && t < best_t) { float xx = s->head_x + s->dir_x * t; if (xx >= 0 && xx <= SCREEN_WIDTH) { best_t = t; ix = xx; iy = 0; } }
  }
  if (s->dir_y < -0.001f) { // hits bottom edge (y=SCREEN_HEIGHT)
    float t = ((float)SCREEN_HEIGHT - s->head_y) / s->dir_y;
    if (t > 0.0f && t < best_t) { float xx = s->head_x + s->dir_x * t; if (xx >= 0 && xx <= SCREEN_WIDTH) { best_t = t; ix = xx; iy = SCREEN_HEIGHT; } }
  }

  *out_x = ix;
  *out_y = iy;
}

void snake_draw(void)
{
  // Draw edge warning arrows for ENTERING snakes that are still offscreen
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state != SNAKE_STATE_ENTERING) continue;
    // Only show warning while head is still offscreen
    if (snake_is_on_screen(s->head_x, s->head_y, 0.0f)) continue;

    float edge_x, edge_y;
    snake_edge_intersection(s, &edge_x, &edge_y);

    // Distance from head to screen edge = how far away the snake is
    float dx = edge_x - s->head_x;
    float dy = edge_y - s->head_y;
    float dist_to_edge = sqrtf(dx * dx + dy * dy);

    // Fade in as snake gets closer: full at 0px, gone at 200px
    float proximity = 1.0f - (dist_to_edge / 200.0f);
    if (proximity < 0.0f) proximity = 0.0f;
    if (proximity > 1.0f) proximity = 1.0f;

    // Pulsing alpha
    float pulse = 0.6f + 0.4f * sinf(s->tail_pulse_timer * 10.0f);
    int alpha = (int)(proximity * pulse * 220.0f);
    if (alpha < 30) continue;  // Too faint, skip
    if (alpha > 220) alpha = 220;

    // Draw 2-3 triangles at the edge pointing inward (along dir)
    float perp_x = -s->dir_y;
    float perp_y =  s->dir_x;

    int tri_count = 2 + (proximity > 0.6f ? 1 : 0);
    for (int c = 0; c < tri_count; c++) {
      // Offset inward from edge
      float inset = 8.0f + (float)c * 16.0f;
      float cx = edge_x + s->dir_x * inset;
      float cy = edge_y + s->dir_y * inset;

      // Clamp to stay on screen
      if (cx < 2) cx = 2;
      if (cx > SCREEN_WIDTH - 2) cx = SCREEN_WIDTH - 2;
      if (cy < 2) cy = 2;
      if (cy > SCREEN_HEIGHT - 2) cy = SCREEN_HEIGHT - 2;

      float tri_size = 16.0f - (float)c * 2.0f;
      float tri_len  = 20.0f - (float)c * 3.0f;

      // Per-chevron alpha fade
      int ca = alpha - c * 40;
      if (ca < 30) continue;

      float tip_x = cx + s->dir_x * tri_len;
      float tip_y = cy + s->dir_y * tri_len;
      float bl_x = cx - s->dir_x * 2.0f + perp_x * tri_size;
      float bl_y = cy - s->dir_y * 2.0f + perp_y * tri_size;
      float br_x = cx - s->dir_x * 2.0f - perp_x * tri_size;
      float br_y = cy - s->dir_y * 2.0f - perp_y * tri_size;

      a_DrawFilledTriangle(
        (int)tip_x, (int)tip_y,
        (int)bl_x, (int)bl_y,
        (int)br_x, (int)br_y,
        (aColor_t){255, 200, 40, (uint8_t)ca}
      );
    }
  }

  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;

    // Draw segments back-to-front
    for (int seg = s->segment_count - 1; seg >= 0; seg--) {
      float sx, sy;
      snake_get_segment_pos(s, seg, &sx, &sy);

      float damage_pct = 1.0f - ((float)s->segments[seg].hp /
                                  (float)s->segments[seg].max_hp);
      if (damage_pct < 0.0f) damage_pct = 0.0f;
      if (damage_pct > 1.0f) damage_pct = 1.0f;

      int r = (int)(damage_pct * 255.0f);
      int g = (int)((1.0f - damage_pct) * 255.0f);
      aColor_t color = {(uint8_t)r, (uint8_t)g, 0, 255};

      float half = SNAKE_SEGMENT_SIZE / 2.0f;
      a_DrawFilledRect(
        (aRectf_t){sx - half, sy - half, SNAKE_SEGMENT_SIZE, SNAKE_SEGMENT_SIZE},
        color
      );

      // Last segment pulse (weak point indicator)
      if (seg == s->segment_count - 1) {
        float pulse = 0.5f + 0.5f * sinf(s->tail_pulse_timer * 6.0f);
        int pulse_alpha = (int)(40.0f * pulse);
        a_DrawFilledRect(
          (aRectf_t){sx - half - 1, sy - half - 1,
                     SNAKE_SEGMENT_SIZE + 2, SNAKE_SEGMENT_SIZE + 2},
          (aColor_t){255, 255, 255, (uint8_t)pulse_alpha}
        );
      }
    }

    // Draw head
    {
      aColor_t head_color = {40, 220, 40, 255};

      // Head exposed: damage gradient
      if (s->segment_count == 0) {
        float damage_pct = 1.0f - ((float)s->head_hp / (float)s->head_max_hp);
        if (damage_pct < 0.0f) damage_pct = 0.0f;
        if (damage_pct > 1.0f) damage_pct = 1.0f;
        head_color.r = (uint8_t)(int)(damage_pct * 255.0f);
        head_color.g = (uint8_t)(int)((1.0f - damage_pct) * 255.0f);
        head_color.b = 0;
      }

      float half = SNAKE_HEAD_SIZE / 2.0f;
      a_DrawFilledRect(
        (aRectf_t){s->head_x - half, s->head_y - half,
                   SNAKE_HEAD_SIZE, SNAKE_HEAD_SIZE},
        head_color
      );

      // During windup: eyes look toward next charge direction
      // Otherwise: eyes follow current movement direction
      float look_dx = s->dir_x;
      float look_dy = s->dir_y;
      if (s->state == SNAKE_STATE_PAUSING) {
        look_dx = s->next_dir_x;
        look_dy = s->next_dir_y;
      }

      // Eyes — two 4×4 black dots along look direction
      float eye_fwd = 4.0f;
      float eye_lat = 4.0f;
      float perp_x = -look_dy;
      float perp_y = look_dx;

      float ex1 = s->head_x + look_dx * eye_fwd + perp_x * eye_lat;
      float ey1 = s->head_y + look_dy * eye_fwd + perp_y * eye_lat;
      float ex2 = s->head_x + look_dx * eye_fwd - perp_x * eye_lat;
      float ey2 = s->head_y + look_dy * eye_fwd - perp_y * eye_lat;

      a_DrawFilledRect(
        (aRectf_t){ex1 - 2, ey1 - 2, SNAKE_EYE_SIZE, SNAKE_EYE_SIZE},
        (aColor_t){0, 0, 0, 255}
      );
      a_DrawFilledRect(
        (aRectf_t){ex2 - 2, ey2 - 2, SNAKE_EYE_SIZE, SNAKE_EYE_SIZE},
        (aColor_t){0, 0, 0, 255}
      );

      // Windup telegraph: dashed line + arrowhead showing charge path
      if (s->state == SNAKE_STATE_PAUSING) {
        float progress = 1.0f - (s->pause_timer / s->telegraph_max);
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

        float ndx = s->next_dir_x;
        float ndy = s->next_dir_y;
        float perp_x = -ndy;
        float perp_y =  ndx;

        // Dashed line extending from head along charge direction
        float line_len = 60.0f + 200.0f * progress;  // grows as windup progresses
        float dash_len = 8.0f;
        float gap_len = 6.0f;
        float pulse = 0.6f + 0.4f * sinf(s->tail_pulse_timer * 8.0f);
        int line_alpha = (int)(pulse * (100.0f + 130.0f * progress));
        if (line_alpha > 230) line_alpha = 230;

        SDL_SetRenderDrawColor(app.renderer, 255, 60, 40, (uint8_t)line_alpha);
        float d = 20.0f;
        int drawing = 1;
        while (d < line_len) {
          float seg = drawing ? dash_len : gap_len;
          if (drawing) {
            float x1 = s->head_x + ndx * d;
            float y1 = s->head_y + ndy * d;
            float end = d + seg;
            if (end > line_len) end = line_len;
            float x2 = s->head_x + ndx * end;
            float y2 = s->head_y + ndy * end;
            SDL_RenderDrawLine(app.renderer, (int)x1, (int)y1, (int)x2, (int)y2);
            // Draw parallel line for thickness
            SDL_RenderDrawLine(app.renderer,
              (int)(x1 + perp_x), (int)(y1 + perp_y),
              (int)(x2 + perp_x), (int)(y2 + perp_y));
          }
          d += seg;
          drawing = !drawing;
        }

        // Arrowhead at the end of the line
        float arrow_tip_x = s->head_x + ndx * line_len;
        float arrow_tip_y = s->head_y + ndy * line_len;
        float arrow_back = line_len - 16.0f;
        float arrow_arm = 10.0f + 4.0f * progress;
        float al_x = s->head_x + ndx * arrow_back + perp_x * arrow_arm;
        float al_y = s->head_y + ndy * arrow_back + perp_y * arrow_arm;
        float ar_x = s->head_x + ndx * arrow_back - perp_x * arrow_arm;
        float ar_y = s->head_y + ndy * arrow_back - perp_y * arrow_arm;

        int arrow_alpha = (int)(pulse * (150.0f + 100.0f * progress));
        if (arrow_alpha > 240) arrow_alpha = 240;
        a_DrawFilledTriangle(
          (int)arrow_tip_x, (int)arrow_tip_y,
          (int)al_x, (int)al_y,
          (int)ar_x, (int)ar_y,
          (aColor_t){255, 60, 40, (uint8_t)arrow_alpha}
        );
      }
    }

    // Stun visual: white flash + orbiting stars
    if (s->stun_timer > 0.0f) {
      float half = SNAKE_HEAD_SIZE / 2.0f;
      float pulse = 0.5f + 0.5f * sinf(s->stun_timer * 16.0f);
      int alpha = 80 + (int)(100.0f * pulse);
      a_DrawFilledRect(
        (aRectf_t){s->head_x - half - 1, s->head_y - half - 1,
                   SNAKE_HEAD_SIZE + 2, SNAKE_HEAD_SIZE + 2},
        (aColor_t){255, 255, 255, (uint8_t)alpha}
      );
      float orbit_r = SNAKE_HEAD_SIZE * 0.45f;
      float phase = s->stun_timer * 6.0f;
      float star_cy = s->head_y - half - 6.0f;
      for (int st = 0; st < 3; st++) {
        float a = phase + (float)st * 2.094f;
        float sx = s->head_x + cosf(a) * orbit_r;
        float sy = star_cy + sinf(a) * 0.35f * orbit_r;
        a_DrawFilledRect(
          (aRectf_t){sx - 1.5f, sy - 1.5f, 3, 3},
          (aColor_t){255, 255, 100, 220}
        );
      }
    }
  }
}

// ============================================================================
// Collision API
// ============================================================================

int snake_check_hit_aoe(float x, float y, float radius)
{
  int hit = 0;
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;

    int hit_this = 0;

    // Check head
    float dx = s->head_x - x;
    float dy = s->head_y - y;
    if (sqrtf(dx * dx + dy * dy) < radius + SNAKE_HEAD_RADIUS) {
      hit_this = 1;
    }

    // Check segments
    if (!hit_this) {
      for (int seg = 0; seg < s->segment_count; seg++) {
        float sx, sy;
        snake_get_segment_pos(s, seg, &sx, &sy);
        dx = sx - x;
        dy = sy - y;
        if (sqrtf(dx * dx + dy * dy) < radius + SNAKE_SEGMENT_RADIUS) {
          hit_this = 1;
          break;
        }
      }
    }

    if (hit_this) {
      snake_damage_tail(i);
      hit = 1;
    }
  }
  return hit;
}

int snake_check_hit_no_knockback(float x, float y, float radius)
{
  int hit = 0;
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;
    if (s->linger_cd > 0.0f) continue;

    int hit_this = 0;

    float dx = s->head_x - x;
    float dy = s->head_y - y;
    if (sqrtf(dx * dx + dy * dy) < radius + SNAKE_HEAD_RADIUS)
      hit_this = 1;

    if (!hit_this) {
      for (int seg = 0; seg < s->segment_count; seg++) {
        float sx, sy;
        snake_get_segment_pos(s, seg, &sx, &sy);
        dx = sx - x;
        dy = sy - y;
        if (sqrtf(dx * dx + dy * dy) < radius + SNAKE_SEGMENT_RADIUS) {
          hit_this = 1;
          break;
        }
      }
    }

    if (hit_this) {
      snake_damage_tail(i);
      s->linger_cd = 0.5f;
      hit = 1;
    }
  }
  return hit;
}

void snake_apply_conductor_aoe(float x, float y, float radius, float duration, int jumps)
{
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;
    float dx = s->head_x - x;
    float dy = s->head_y - y;
    if (sqrtf(dx * dx + dy * dy) < radius + SNAKE_HEAD_RADIUS) {
      snake_set_conductor(i, duration, jumps);
      continue;
    }
    for (int seg = 0; seg < s->segment_count; seg++) {
      float sx, sy;
      snake_get_segment_pos(s, seg, &sx, &sy);
      dx = sx - x;
      dy = sy - y;
      if (sqrtf(dx * dx + dy * dy) < radius + SNAKE_SEGMENT_RADIUS) {
        snake_set_conductor(i, duration, jumps);
        break;
      }
    }
  }
}

int snake_check_bullet_hit(float bx, float by, float bullet_radius)
{
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;

    // Check head
    float dx = s->head_x - bx;
    float dy = s->head_y - by;
    if (sqrtf(dx * dx + dy * dy) < bullet_radius + SNAKE_HEAD_RADIUS) {
      snake_damage_tail(i);
      int turret_cond = upgrades_get_tier(UPG_TURRET_CONDUCTOR);
      if (turret_cond > 0) snake_set_conductor(i, 2.0f, turret_cond);
      return 1;
    }

    // Check segments
    for (int seg = 0; seg < s->segment_count; seg++) {
      float sx, sy;
      snake_get_segment_pos(s, seg, &sx, &sy);
      dx = sx - bx;
      dy = sy - by;
      if (sqrtf(dx * dx + dy * dy) < bullet_radius + SNAKE_SEGMENT_RADIUS) {
        snake_damage_tail(i);
        int turret_cond = upgrades_get_tier(UPG_TURRET_CONDUCTOR);
        if (turret_cond > 0) snake_set_conductor(i, 2.0f, turret_cond);
        return 1;
      }
    }
  }
  return 0;
}

void snake_check_wand_bullets(void)
{
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;

    // Use negative enemy_index per snake for pierce tracking
    int virtual_id = -(i + 2);

    // Check head
    float hx = s->head_x - SNAKE_HEAD_RADIUS;
    float hy = s->head_y - SNAKE_HEAD_RADIUS;
    int bullet = player_check_bullet_collision_ex(hx, hy, SNAKE_HEAD_RADIUS,
                                                  virtual_id);
    if (bullet >= 0) {
      snake_damage_tail(i);
      player_bullet_on_hit(bullet, -1);
      int wand_cond = upgrades_get_tier(UPG_WAND_CONDUCTOR);
      if (wand_cond > 0) snake_set_conductor(i, 2.0f, wand_cond);
      continue;
    }

    // Check segments
    for (int seg = 0; seg < s->segment_count; seg++) {
      float sx, sy;
      snake_get_segment_pos(s, seg, &sx, &sy);
      float ex = sx - SNAKE_SEGMENT_RADIUS;
      float ey = sy - SNAKE_SEGMENT_RADIUS;
      bullet = player_check_bullet_collision_ex(ex, ey, SNAKE_SEGMENT_RADIUS,
                                                virtual_id);
      if (bullet >= 0) {
        snake_damage_tail(i);
        player_bullet_on_hit(bullet, -1);
        int wand_cond = upgrades_get_tier(UPG_WAND_CONDUCTOR);
        if (wand_cond > 0) snake_set_conductor(i, 2.0f, wand_cond);
        break;
      }
    }
  }
}

// ============================================================================
// Debuffs
// ============================================================================

void snake_set_conductor(int snake_index, float duration, int jumps)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return;
  if (!snakes[snake_index].active) return;
  snakes[snake_index].conductor_timer = duration;
  snakes[snake_index].conductor_jumps = jumps;
}

void snake_set_stun(int snake_index, float duration, float damage_mult)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return;
  if (!snakes[snake_index].active) return;
  snakes[snake_index].stun_timer = duration;
  snakes[snake_index].stun_damage_mult = damage_mult;
}

// ============================================================================
// Queries
// ============================================================================

int snake_get_active_count(void)
{
  int count = 0;
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    if (snakes[i].active) count++;
  }
  return count;
}

int snake_is_alive(int snake_index)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return 0;
  return snakes[snake_index].active;
}

int snake_get_max_count(void)
{
  return SNAKE_MAX_ACTIVE;
}

void snake_get_head_position(int snake_index, float* out_x, float* out_y)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) { *out_x = 0; *out_y = 0; return; }
  Snake_t* s = &snakes[snake_index];
  *out_x = s->head_x;
  *out_y = s->head_y;
}

float snake_get_head_radius(int snake_index)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return 0.0f;
  return (float)SNAKE_HEAD_SIZE / 2.0f;
}

void snake_displace(int snake_index, float dx, float dy)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return;
  Snake_t* s = &snakes[snake_index];
  if (!s->active) return;
  s->head_x += dx;
  s->head_y += dy;
  // Move all history positions so the body follows
  for (int i = 0; i < s->history_count; i++) {
    int idx = (s->history_head - i + SNAKE_HISTORY_SIZE) % SNAKE_HISTORY_SIZE;
    s->history[idx].x += dx;
    s->history[idx].y += dy;
  }
  // Brief stun so steering doesn't instantly undo the pull
  if (s->stun_timer < 0.15f) s->stun_timer = 0.15f;
}

int snake_find_nearest_head(float px, float py, float* best_dist,
                            float* out_x, float* out_y)
{
  int found = 0;
  for (int i = 0; i < SNAKE_MAX_ACTIVE; i++) {
    Snake_t* s = &snakes[i];
    if (!s->active) continue;
    if (s->state == SNAKE_STATE_OFFSCREEN || s->state == SNAKE_STATE_INACTIVE)
      continue;
    float dx = s->head_x - px;
    float dy = s->head_y - py;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < *best_dist) {
      *best_dist = dist;
      *out_x = s->head_x;
      *out_y = s->head_y;
      found = 1;
    }
  }
  return found;
}

int snake_is_on_screen_active(int snake_index)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return 0;
  Snake_t* s = &snakes[snake_index];
  if (!s->active) return 0;
  if (s->state == SNAKE_STATE_INACTIVE || s->state == SNAKE_STATE_OFFSCREEN) return 0;
  return 1;
}

float snake_get_damage_ratio(int snake_index)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return 0.0f;
  Snake_t* s = &snakes[snake_index];
  if (!s->active) return 0.0f;

  int total_max = s->total_segments * s->hp_per_segment + s->head_max_hp;
  if (total_max <= 0) return 0.0f;

  int current = 0;
  for (int i = 0; i < s->segment_count; i++)
    current += s->segments[i].hp;
  current += s->head_hp;

  float ratio = 1.0f - ((float)current / (float)total_max);
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  return ratio;
}

void snake_get_tail_position(int snake_index, float* out_x, float* out_y)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) {
    *out_x = 0.0f; *out_y = 0.0f;
    return;
  }
  Snake_t* s = &snakes[snake_index];
  if (s->segment_count > 0) {
    snake_get_segment_pos(s, s->segment_count - 1, out_x, out_y);
  } else {
    *out_x = s->head_x;
    *out_y = s->head_y;
  }
}

int snake_apply_heal(int snake_index, int heal_amount)
{
  if (snake_index < 0 || snake_index >= SNAKE_MAX_ACTIVE) return 0;
  Snake_t* s = &snakes[snake_index];
  if (!s->active) return 0;

  int remaining = heal_amount;
  int healed = 0;

  // Phase 1: If head exposed, heal head first
  if (s->segment_count == 0 && s->head_hp < s->head_max_hp) {
    int missing = s->head_max_hp - s->head_hp;
    int restore = remaining < missing ? remaining : missing;
    s->head_hp += restore;
    remaining -= restore;
    healed = 1;
  }

  // Phase 2: Heal last segment's HP
  if (remaining > 0 && s->segment_count > 0) {
    int last = s->segment_count - 1;
    int missing = s->segments[last].max_hp - s->segments[last].hp;
    if (missing > 0) {
      int restore = remaining < missing ? remaining : missing;
      s->segments[last].hp += restore;
      remaining -= restore;
      healed = 1;
    }
  }

  // Phase 3: Regrow popped segments with overflow
  while (remaining > 0 && s->segment_count < s->total_segments) {
    int next = s->segment_count;
    if (remaining >= s->hp_per_segment) {
      s->segments[next].hp = s->segments[next].max_hp;
      s->segment_count++;
      remaining -= s->hp_per_segment;
    } else {
      s->segments[next].hp = remaining;
      s->segment_count++;
      remaining = 0;
    }
    healed = 1;
  }

  return healed;
}

// ============================================================================
// Scale Shatter Update / Draw
// ============================================================================

void snake_shatter_update(float dt)
{
  for (int i = 0; i < SNAKE_SHATTER_MAX; i++) {
    SnakeShatterProj_t* p = &snake_shatters[i];
    if (!p->active) continue;

    // Soft homing: steer toward nearest enemy
    float best_dist = 250.0f;
    float tgt_x = 0, tgt_y = 0;
    int has_tgt = 0;
    int max_e = enemy_get_max_count();
    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float ddx = ex - p->x;
      float ddy = ey - p->y;
      float d = sqrtf(ddx * ddx + ddy * ddy);
      if (d < best_dist) {
        best_dist = d;
        tgt_x = ex; tgt_y = ey;
        has_tgt = 1;
      }
    }
    if (has_tgt) {
      float to_x = tgt_x - p->x;
      float to_y = tgt_y - p->y;
      float len = sqrtf(to_x * to_x + to_y * to_y);
      if (len > 0.1f) {
        float steer = SNAKE_SHATTER_HOMING * dt;
        p->vx += (to_x / len) * steer;
        p->vy += (to_y / len) * steer;
        // Re-normalize to maintain speed
        float spd = sqrtf(p->vx * p->vx + p->vy * p->vy);
        if (spd > 0.1f) {
          p->vx = (p->vx / spd) * SNAKE_SHATTER_SPEED;
          p->vy = (p->vy / spd) * SNAKE_SHATTER_SPEED;
        }
      }
    }

    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->life -= dt;
    if (p->life <= 0.0f) {
      p->active = 0;
      continue;
    }

    // Check collision with regular enemies (not snakes, not player)
    float proj_r = SNAKE_SHATTER_SIZE / 2.0f;
    int max_enemy = enemy_get_max_count();
    for (int e = 0; e < max_enemy; e++) {
      if (!enemy_is_alive(e)) continue;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float dx = p->x - ex;
      float dy = p->y - ey;
      if (sqrtf(dx * dx + dy * dy) < proj_r + 12.0f) {
        weapons_set_damage_source(SOURCE_SNAKE_POP);
        enemy_hit(e, 0, 0);
        weapons_record_hit();
        if (p->pierces_left > 0) {
          p->pierces_left--;
        } else {
          p->active = 0;
          break;
        }
      }
    }
  }
}

void snake_shatter_draw(void)
{
  for (int i = 0; i < SNAKE_SHATTER_MAX; i++) {
    SnakeShatterProj_t* p = &snake_shatters[i];
    if (!p->active) continue;

    float alpha_pct = p->life / SNAKE_SHATTER_LIFE;
    if (alpha_pct < 0.0f) alpha_pct = 0.0f;
    if (alpha_pct > 1.0f) alpha_pct = 1.0f;
    uint8_t alpha = (uint8_t)(255.0f * alpha_pct);

    float half = SNAKE_SHATTER_SIZE / 2.0f;
    a_DrawFilledRect(
      (aRectf_t){p->x - half, p->y - half, SNAKE_SHATTER_SIZE, SNAKE_SHATTER_SIZE},
      (aColor_t){255, 180, 60, alpha}
    );
  }
}
