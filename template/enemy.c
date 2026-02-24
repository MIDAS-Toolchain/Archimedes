#include "enemy.h"
#include "drops.h"
#include "weapons.h"
#include "player_actions.h"
#include "pickups.h"
#include "game_audio.h"
#include "blood.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Constants (shared across all types)
// ============================================================================

// Knockback
#define KNOCKBACK_DURATION 0.1f
#define KNOCKBACK_STRENGTH 20.0f
#define KILL_KNOCKBACK_MIN 1.5f
#define KILL_KNOCKBACK_RANGE 0.75f

// AI tuning (shared)
#define PREDICTION_TIME 0.4f
#define CHARGE_DISTANCE 200.0f
#define ATTACK_RANGE 120.0f
#define AGGRO_ATTACK_RANGE 200.0f
#define RETREAT_SPEED_MULT 1.5f
#define SEPARATION_WEIGHT 60.0f
#define ATTACK_SEPARATION_SCALE 0.3f
#define ATTACK_SEPARATION_WEIGHT 30.0f
#define FLANK_ARRIVE_DIST 20.0f
#define RETREAT_ARRIVE_DIST 10.0f
#define PLAYER_MIN_DISTANCE 17.0f
#define OFFSCREEN_MARGIN 100.0f
#define CORPSE_LIFETIME 4.0f
#define CORPSE_FADE_START 2.0f
#define STUCK_THRESHOLD 2.0f
#define DASHER_WINDUP_TIME 0.6f
#define DASHER_WINDUP_PULLBACK 30.0f
#define DASHER_INDICATOR_LENGTH 200.0f

// Brute health-stealing
#define BRUTE_HEAL_BASE_RADIUS 80.0f
#define BRUTE_HEAL_PER_HIT_RADIUS 30.0f  // Search radius grows per hit taken
#define BRUTE_HEAL_MAX_RADIUS 400.0f
#define BRUTE_HEAL_SPEED_MULT 2.5f       // Sprint speed when chasing health (on top of rage)
#define BRUTE_HEAL_PICKUP_DIST 20.0f     // Close enough to consume the drop
#define BRUTE_HEAL_AMOUNT 3              // Hits healed per pickup

// Brute rage: speed scales with damage taken
#define BRUTE_RAGE_MAX_SPEED_MULT 2.0f   // At full damage: 2x base speed

// Brute buff constants
#define BRUTE_FIRE_CONE_RANGE     100.0f
#define BRUTE_FIRE_CONE_HALF_ANGLE 0.5236f // 30 degrees
#define BRUTE_FIRE_CONE_TICK_RATE 0.2f
#define BRUTE_FIRE_CONE_DAMAGE    10
#define BRUTE_FIRE_DURATION       4.0f
#define BRUTE_SPEED_DURATION      3.0f
#define BRUTE_SPEED_MULT          1.5f
#define BRUTE_SHIELD_DURATION     5.0f
#define BRUTE_SHIELD_HITS         3

// ============================================================================
// Per-type stats table
// ============================================================================

static const EnemyStats_t enemy_stats[ENEMY_TYPE_COUNT] = {
  [ENEMY_TYPE_GRUNT] = {
    .speed = 100.0f, .size = 16.0f, .radius = 8.0f,
    .hits_to_kill = 5, .damage = 20,
    .attack_speed_mult = 2.0f,
    .attack_duration_min = 1.0f, .attack_duration_max = 4.0f,
    .reposition_duration_min = 2.0f, .reposition_duration_max = 5.0f,
    .flank_distance = 120.0f, .separation_radius = 30.0f,
    .skip_reposition = 0
  },
  [ENEMY_TYPE_DASHER] = {
    .speed = 180.0f, .size = 18.0f, .radius = 9.0f,
    .hits_to_kill = 4, .damage = 15,
    .attack_speed_mult = 3.0f,
    .attack_duration_min = 0.5f, .attack_duration_max = 1.5f,
    .reposition_duration_min = 0.5f, .reposition_duration_max = 1.0f,
    .flank_distance = 150.0f, .separation_radius = 25.0f,
    .skip_reposition = 0
  },
  [ENEMY_TYPE_BRUTE] = {
    .speed = 60.0f, .size = 24.0f, .radius = 12.0f,
    .hits_to_kill = 12, .damage = 40,
    .attack_speed_mult = 1.5f,
    .attack_duration_min = 3.0f, .attack_duration_max = 7.0f,
    .reposition_duration_min = 1.0f, .reposition_duration_max = 2.0f,
    .flank_distance = 80.0f, .separation_radius = 45.0f,
    .skip_reposition = 0
  },
};

// Damage color: all enemies go green -> red
#define ENEMY_COLOR_HEALTHY_R 0
#define ENEMY_COLOR_HEALTHY_G 255
#define ENEMY_COLOR_HEALTHY_B 0
#define ENEMY_COLOR_DAMAGED_R 255
#define ENEMY_COLOR_DAMAGED_G 0
#define ENEMY_COLOR_DAMAGED_B 0

// ============================================================================
// Internal State
// ============================================================================

static Enemy_t* enemies = NULL;
static int max_enemies = 0;
static int first_kill_dropped = 0;
static int current_attackers = 0;

extern aApp_t app;

// Forward declarations
static int brute_evaluate_priorities(Enemy_t* e);

// ============================================================================
// Utility Helpers
// ============================================================================

static float lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

static float dist_between(float x1, float y1, float x2, float y2)
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  return sqrtf(dx * dx + dy * dy);
}

static float normalize(float* x, float* y)
{
  float len = sqrtf((*x) * (*x) + (*y) * (*y));
  if (len > 0.1f) {
    *x /= len;
    *y /= len;
  }
  return len;
}

static int is_offscreen(float x, float y)
{
  return x < -OFFSCREEN_MARGIN || x > SCREEN_WIDTH + OFFSCREEN_MARGIN ||
         y < -OFFSCREEN_MARGIN || y > SCREEN_HEIGHT + OFFSCREEN_MARGIN;
}

static const EnemyStats_t* get_stats(Enemy_t* e)
{
  return &enemy_stats[e->type];
}

static int get_total_hp(Enemy_t* e)
{
  return get_stats(e)->hits_to_kill + e->bonus_hp;
}

static float get_effective_speed(Enemy_t* e)
{
  float speed = get_stats(e)->speed * e->speed_mult;

  // Brute rage: speed increases with damage taken
  if (e->type == ENEMY_TYPE_BRUTE) {
    int total_hp = get_total_hp(e);
    float damage_pct = (total_hp > 0) ? (float)e->hit_count / (float)total_hp : 0.0f;
    if (damage_pct > 1.0f) damage_pct = 1.0f;
    // Lerp from 1.0x to BRUTE_RAGE_MAX_SPEED_MULT based on damage
    speed *= 1.0f + (BRUTE_RAGE_MAX_SPEED_MULT - 1.0f) * damage_pct;

    // Speed buff stacks on top of rage
    if (e->brute_buff.active && e->brute_buff_type == PICKUP_SPEED) {
      speed *= BRUTE_SPEED_MULT;
    }
  }

  return speed;
}

static void calc_separation(int self_index, float* out_x, float* out_y, float scale_factor)
{
  *out_x = 0.0f;
  *out_y = 0.0f;

  Enemy_t* self = &enemies[self_index];
  float my_sep_radius = get_stats(self)->separation_radius;

  for (int j = 0; j < max_enemies; j++) {
    if (j == self_index || !enemies[j].active) continue;
    if (enemies[j].state == ENEMY_STATE_CORPSE || enemies[j].state == ENEMY_STATE_HIT_KNOCKBACK) continue;

    float dx = self->x - enemies[j].x;
    float dy = self->y - enemies[j].y;
    float dist = sqrtf(dx * dx + dy * dy);

    // Use the larger of the two separation radii
    float other_sep_radius = get_stats(&enemies[j])->separation_radius;
    float sep_radius = (my_sep_radius > other_sep_radius) ? my_sep_radius : other_sep_radius;

    if (dist < sep_radius && dist > 0.1f) {
      float strength = (sep_radius - dist) / sep_radius * scale_factor;
      *out_x += (dx / dist) * strength;
      *out_y += (dy / dist) * strength;
    }
  }
}

static void move_toward(Enemy_t* e, float target_x, float target_y,
                        float speed, float sep_x, float sep_y,
                        float sep_weight, float dt)
{
  float radius = get_stats(e)->radius;
  float dx = (target_x - (e->x + radius)) + sep_x * sep_weight;
  float dy = (target_y - (e->y + radius)) + sep_y * sep_weight;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist > 0.1f) {
    e->vx = (dx / dist) * speed;
    e->vy = (dy / dist) * speed;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

static int resolve_player_collision(Enemy_t* e, float player_x, float player_y)
{
  float radius = get_stats(e)->radius;
  float dx = (e->x + radius) - player_x;
  float dy = (e->y + radius) - player_y;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < PLAYER_MIN_DISTANCE && dist > 0.1f) {
    float overlap = PLAYER_MIN_DISTANCE - dist;
    e->x += (dx / dist) * overlap;
    e->y += (dy / dist) * overlap;
    return 1;
  }
  return 0;
}

static void start_knockback(Enemy_t* e, float bullet_vx, float bullet_vy, float knockback_dist)
{
  float bx = bullet_vx;
  float by = bullet_vy;
  float speed = normalize(&bx, &by);

  e->knockback_start_x = e->x;
  e->knockback_start_y = e->y;

  if (speed > 0.1f) {
    e->knockback_target_x = e->x + bx * knockback_dist;
    e->knockback_target_y = e->y + by * knockback_dist;
  } else {
    e->knockback_target_x = e->x;
    e->knockback_target_y = e->y;
  }

  e->knockback_timer = 0.0f;

  // Track attacker count when leaving ATTACKING/WINDUP state via knockback
  if ((e->state == ENEMY_STATE_ATTACKING || e->state == ENEMY_STATE_WINDUP) && current_attackers > 0) {
    current_attackers--;
  }

  e->state = ENEMY_STATE_HIT_KNOCKBACK;
}

static void enter_reposition(Enemy_t* e)
{
  const EnemyStats_t* stats = get_stats(e);

  // Track attacker count when leaving ATTACKING/WINDUP state
  if ((e->state == ENEMY_STATE_ATTACKING || e->state == ENEMY_STATE_WINDUP) && current_attackers > 0) {
    current_attackers--;
  }

  if (stats->skip_reposition) {
    e->state = ENEMY_STATE_ALIVE;
    e->stuck_timer = 0.0f;
    e->last_distance_to_player = 9999.0f;
    return;
  }

  e->state = ENEMY_STATE_REPOSITIONING;
  e->target_angle = RANDF(0, 2.0f * PI);
  e->reposition_duration = RANDF(stats->reposition_duration_min, stats->reposition_duration_max);
  e->stuck_timer = 0.0f;
}

static void enter_attacking(Enemy_t* e)
{
  const EnemyStats_t* stats = get_stats(e);
  e->aggro = 1;
  e->flank_x = e->x;
  e->flank_y = e->y;
  current_attackers++;

  // Dashers do a windup telegraph before charging
  if (e->type == ENEMY_TYPE_DASHER) {
    e->state = ENEMY_STATE_WINDUP;
    e->windup_timer = DASHER_WINDUP_TIME;
    e->dash_dir_x = 0.0f;
    e->dash_dir_y = 0.0f;
    return;
  }

  e->state = ENEMY_STATE_ATTACKING;
  e->attack_duration = RANDF(stats->attack_duration_min, stats->attack_duration_max);
}

// ============================================================================
// Initialization
// ============================================================================

void enemy_init(int max_enemy_count, int max_blood_count)
{
  max_enemies = max_enemy_count;
  enemies = (Enemy_t*)calloc(max_enemies, sizeof(Enemy_t));
  blood_init(max_blood_count);
  first_kill_dropped = 0;
  current_attackers = 0;
}

void enemy_cleanup(void)
{
  free(enemies);
  enemies = NULL;
  blood_cleanup();
  current_attackers = 0;
}

// ============================================================================
// Spawning
// ============================================================================

int enemy_spawn(float x, float y, EnemyType_t type, float speed_mult, int bonus_hp)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) {
      enemies[i] = (Enemy_t){
        .x = x, .y = y,
        .target_angle = RANDF(0, 2.0f * PI),
        .state = ENEMY_STATE_ALIVE,
        .type = type,
        .active = 1,
        .last_distance_to_player = 9999.0f,
        .speed_mult = speed_mult,
        .bonus_hp = bonus_hp,
      };
      return i;
    }
  }
  return -1;
}

// ============================================================================
// Hit and Knockback
// ============================================================================

void enemy_hit(int enemy_index, float bullet_vx, float bullet_vy)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  if (!enemies[enemy_index].active) return;

  Enemy_t* e = &enemies[enemy_index];
  if (e->state == ENEMY_STATE_CORPSE) return;

  e->hit_count++;

  float knockback_dist = KNOCKBACK_STRENGTH;
  int killed = (e->hit_count >= get_total_hp(e));

  if (killed) {
    knockback_dist *= KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE);

    // Store bullet velocity for blood splatter direction after knockback
    e->vx = bullet_vx;
    e->vy = bullet_vy;

    game_audio_play_die();
  } else {
    game_audio_play_hit();
  }

  // Brutes are unstoppable — no knockback
  // Shield buff absorbs hits
  if (e->type == ENEMY_TYPE_BRUTE && !killed) {
    if (e->brute_buff.active && e->brute_buff_type == PICKUP_SHIELD && e->brute_buff.shield_hits > 0) {
      e->hit_count--; // Undo the hit_count increment
      e->brute_buff.shield_hits--;
      if (e->brute_buff.shield_hits <= 0) {
        e->brute_buff.active = 0;
      }
      return; // Shield absorbed, no knockback
    }

    // Re-evaluate priorities on every hit (interrupt current action)
    brute_evaluate_priorities(e);
    return;  // No knockback for brutes
  }

  start_knockback(e, bullet_vx, bullet_vy, knockback_dist);
}

// ============================================================================
// Collision
// ============================================================================

int enemy_check_collision(float x, float y, float radius)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active || enemies[i].state == ENEMY_STATE_CORPSE) continue;

    float er = get_stats(&enemies[i])->radius;
    float dist = dist_between(x + radius, y + radius,
                              enemies[i].x + er, enemies[i].y + er);
    if (dist < (radius + er)) {
      return i;
    }
  }
  return -1;
}

// ============================================================================
// State Update Handlers
// ============================================================================

static void update_knockback(Enemy_t* e, float dt)
{
  e->knockback_timer += dt;

  if (e->knockback_timer >= KNOCKBACK_DURATION) {
    e->x = e->knockback_target_x;
    e->y = e->knockback_target_y;

    if (e->hit_count >= get_total_hp(e)) {
      e->state = ENEMY_STATE_CORPSE;
      e->death_timer = 0.0f;
      e->brute_buff.active = 0; // Clear buff on death
      blood_spawn(e->x, e->y, e->vx, e->vy, get_stats(e)->radius);

      if (!first_kill_dropped) {
        WeaponType_t pool[5];
        int pool_count = 0;
        if (!weapons_has(WEAPON_WAND)  && !drops_has_type(WEAPON_WAND))  pool[pool_count++] = WEAPON_WAND;
        if (!weapons_has(WEAPON_SPIN)  && !drops_has_type(WEAPON_SPIN))  pool[pool_count++] = WEAPON_SPIN;
        if (!weapons_has(WEAPON_CHAIN) && !drops_has_type(WEAPON_CHAIN)) pool[pool_count++] = WEAPON_CHAIN;
        if (!weapons_has(WEAPON_ORBIT) && !drops_has_type(WEAPON_ORBIT)) pool[pool_count++] = WEAPON_ORBIT;
        if (!weapons_has(WEAPON_BOMB)  && !drops_has_type(WEAPON_BOMB))  pool[pool_count++] = WEAPON_BOMB;
        if (pool_count > 0) {
          drops_spawn(e->x, e->y, pool[rand() % pool_count]);
        }
        first_kill_dropped = 1;
      }
    } else if (e->aggro && !player_is_invincible()) {
      enter_attacking(e);
    } else {
      e->state = ENEMY_STATE_ALIVE;
      e->stuck_timer = 0.0f;
      e->last_distance_to_player = 9999.0f;
    }
  } else {
    float t = e->knockback_timer / KNOCKBACK_DURATION;
    e->x = lerp(e->knockback_start_x, e->knockback_target_x, t);
    e->y = lerp(e->knockback_start_y, e->knockback_target_y, t);
  }
}

static void update_alive(Enemy_t* e, int index, float dt,
                         float player_x, float player_y,
                         float player_vx, float player_vy)
{
  const EnemyStats_t* stats = get_stats(e);
  float radius = stats->radius;
  float speed = get_effective_speed(e);

  // Brutes proactively scan for pickups/health even when not being hit
  if (e->type == ENEMY_TYPE_BRUTE && brute_evaluate_priorities(e)) {
    return;
  }

  float dist = dist_between(e->x + radius, e->y + radius, player_x, player_y);

  // Stuck detection: if not making progress toward player, try repositioning
  if (dist < e->last_distance_to_player - 1.0f) {
    e->stuck_timer = 0.0f;
    e->last_distance_to_player = dist;
  } else {
    e->stuck_timer += dt;
    if (e->stuck_timer > STUCK_THRESHOLD) {
      enter_reposition(e);
      e->stuck_timer = 0.0f;
      e->last_distance_to_player = 9999.0f;
      return;
    }
  }

  // Close enough? Switch to attack charge (but not while player is invincible)
  // Dashers engage from further out for snappier behavior
  float engage_range;
  if (e->type == ENEMY_TYPE_DASHER) {
    engage_range = e->aggro ? AGGRO_ATTACK_RANGE : 200.0f;
  } else {
    engage_range = e->aggro ? AGGRO_ATTACK_RANGE : ATTACK_RANGE;
  }
  if (dist <= engage_range && !player_is_invincible()) {
    // Attack stagger: limit simultaneous attackers
    int max_attackers = 3 + (enemy_get_count() / 10);
    if (current_attackers < max_attackers) {
      enter_attacking(e);
      return;
    }
  }

  // Pick a target: predict player position when far, charge direct when close
  float target_x, target_y;
  if (dist > CHARGE_DISTANCE) {
    target_x = player_x + player_vx * PREDICTION_TIME + RANDF(-30.0f, 30.0f);
    target_y = player_y + player_vy * PREDICTION_TIME + RANDF(-30.0f, 30.0f);
  } else {
    target_x = player_x;
    target_y = player_y;
  }

  float sep_x, sep_y;
  calc_separation(index, &sep_x, &sep_y, 1.0f);
  move_toward(e, target_x, target_y, speed, sep_x, sep_y, SEPARATION_WEIGHT, dt);
  resolve_player_collision(e, player_x, player_y);

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_repositioning(Enemy_t* e, int index, float dt,
                                 float player_x, float player_y)
{
  const EnemyStats_t* stats = get_stats(e);
  float radius = stats->radius;
  float speed = get_effective_speed(e);

  // Brutes proactively scan for pickups/health during repositioning too
  if (e->type == ENEMY_TYPE_BRUTE && brute_evaluate_priorities(e)) {
    return;
  }

  e->reposition_duration -= dt;

  float target_x = player_x + cosf(e->target_angle) * stats->flank_distance;
  float target_y = player_y + sinf(e->target_angle) * stats->flank_distance;

  float dist_to_target = dist_between(e->x + radius, e->y + radius,
                                      target_x, target_y);

  if (dist_to_target < FLANK_ARRIVE_DIST || e->reposition_duration <= 0.0f) {
    if (e->aggro && !player_is_invincible()) {
      int max_attackers = 3 + (enemy_get_count() / 10);
      if (current_attackers < max_attackers) {
        enter_attacking(e);
      } else {
        e->state = ENEMY_STATE_ALIVE;
        e->stuck_timer = 0.0f;
        e->last_distance_to_player = 9999.0f;
      }
    } else {
      e->state = ENEMY_STATE_ALIVE;
      e->stuck_timer = 0.0f;
      e->last_distance_to_player = 9999.0f;
    }
    return;
  }

  float sep_x, sep_y;
  calc_separation(index, &sep_x, &sep_y, 1.0f);
  move_toward(e, target_x, target_y, speed, sep_x, sep_y, SEPARATION_WEIGHT, dt);
  resolve_player_collision(e, player_x, player_y);

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_windup(Enemy_t* e, float dt, float player_x, float player_y,
                          float player_vx, float player_vy)
{
  const EnemyStats_t* stats = get_stats(e);
  float radius = stats->radius;
  float dash_speed = get_effective_speed(e) * stats->attack_speed_mult;

  // Predict where the player will be when the dash arrives
  float dx_raw = player_x - (e->x + radius);
  float dy_raw = player_y - (e->y + radius);
  float dist_to_player = sqrtf(dx_raw * dx_raw + dy_raw * dy_raw);
  float time_to_reach = (dash_speed > 0.1f) ? dist_to_player / dash_speed : 0.0f;

  float predict_x = player_x + player_vx * time_to_reach;
  float predict_y = player_y + player_vy * time_to_reach;

  float dx = predict_x - (e->x + radius);
  float dy = predict_y - (e->y + radius);
  normalize(&dx, &dy);
  e->dash_dir_x = dx;
  e->dash_dir_y = dy;

  // Pull back slightly (opposite of dash direction)
  float pullback_speed = DASHER_WINDUP_PULLBACK / DASHER_WINDUP_TIME;
  e->x -= dx * pullback_speed * dt;
  e->y -= dy * pullback_speed * dt;

  e->windup_timer -= dt;
  if (e->windup_timer <= 0.0f) {
    // Transition to actual attack with locked direction
    e->state = ENEMY_STATE_ATTACKING;
    e->attack_duration = RANDF(stats->attack_duration_min, stats->attack_duration_max);
  }
}

static void update_attacking(Enemy_t* e, int index, float dt,
                             float player_x, float player_y)
{
  const EnemyStats_t* stats = get_stats(e);
  float speed = get_effective_speed(e) * stats->attack_speed_mult;

  e->attack_duration -= dt;

  if (player_is_invincible()) {
    enter_reposition(e);
    return;
  }

  if (e->attack_duration <= 0.0f) {
    enter_reposition(e);
    return;
  }

  // Dashers charge in their locked direction; others track the player
  if (e->type == ENEMY_TYPE_DASHER) {
    e->vx = e->dash_dir_x * speed;
    e->vy = e->dash_dir_y * speed;
    e->x += e->vx * dt;
    e->y += e->vy * dt;

    // Bounce off screen edges
    float sz = stats->size;
    if (e->x < 0.0f)                    { e->x = 0.0f;                    e->dash_dir_x = -e->dash_dir_x; }
    if (e->x + sz > (float)SCREEN_WIDTH) { e->x = (float)SCREEN_WIDTH - sz; e->dash_dir_x = -e->dash_dir_x; }
    if (e->y < 0.0f)                     { e->y = 0.0f;                    e->dash_dir_y = -e->dash_dir_y; }
    if (e->y + sz > (float)SCREEN_HEIGHT){ e->y = (float)SCREEN_HEIGHT - sz; e->dash_dir_y = -e->dash_dir_y; }
  } else {
    float sep_x, sep_y;
    calc_separation(index, &sep_x, &sep_y, ATTACK_SEPARATION_SCALE);
    move_toward(e, player_x, player_y, speed,
                sep_x, sep_y, ATTACK_SEPARATION_WEIGHT, dt);
  }

  if (resolve_player_collision(e, player_x, player_y)) {
    player_take_damage(stats->damage);

    // Leave ATTACKING state
    if (current_attackers > 0) current_attackers--;

    if (stats->skip_reposition) {
      e->state = ENEMY_STATE_ALIVE;
      e->stuck_timer = 0.0f;
      e->last_distance_to_player = 9999.0f;
    } else {
      e->state = ENEMY_STATE_RETREAT;
    }
  }

  if (is_offscreen(e->x, e->y)) {
    if (e->state == ENEMY_STATE_ATTACKING && current_attackers > 0) current_attackers--;
    e->active = 0;
  }
}

static void update_retreat(Enemy_t* e, float dt)
{
  float speed = get_effective_speed(e) * RETREAT_SPEED_MULT;

  float dist = dist_between(e->x, e->y, e->flank_x, e->flank_y);

  if (dist < RETREAT_ARRIVE_DIST) {
    enter_reposition(e);
    return;
  }

  float dx = e->flank_x - e->x;
  float dy = e->flank_y - e->y;
  normalize(&dx, &dy);

  e->vx = dx * speed;
  e->vy = dy * speed;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_seeking_health(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  const EnemyStats_t* stats = get_stats(e);
  float radius = stats->radius;
  float speed = get_effective_speed(e) * BRUTE_HEAL_SPEED_MULT;

  // Re-check if the health drop still exists, update target
  float search_r = BRUTE_HEAL_BASE_RADIUS + BRUTE_HEAL_PER_HIT_RADIUS * (float)e->hit_count;
  if (search_r > BRUTE_HEAL_MAX_RADIUS) search_r = BRUTE_HEAL_MAX_RADIUS;
  float hx, hy;
  if (!drops_find_nearest_health(e->x + radius, e->y + radius, search_r, &hx, &hy)) {
    // Health drop gone (player grabbed it or it expired), go back to normal
    e->state = ENEMY_STATE_ALIVE;
    e->stuck_timer = 0.0f;
    e->last_distance_to_player = 9999.0f;
    return;
  }
  e->heal_target_x = hx;
  e->heal_target_y = hy;

  // Sprint toward the health pickup
  float dx = e->heal_target_x - (e->x + radius);
  float dy = e->heal_target_y - (e->y + radius);
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < BRUTE_HEAL_PICKUP_DIST) {
    // Consume the drop and heal
    drops_consume_nearest_health(e->x + radius, e->y + radius, BRUTE_HEAL_PICKUP_DIST + 10.0f);
    e->hit_count -= BRUTE_HEAL_AMOUNT;
    if (e->hit_count < 0) e->hit_count = 0;

    // Back to normal behavior
    e->state = ENEMY_STATE_ALIVE;
    e->stuck_timer = 0.0f;
    e->last_distance_to_player = 9999.0f;
    return;
  }

  // Move toward pickup (ignore separation — sprint through enemies)
  if (dist > 0.1f) {
    e->vx = (dx / dist) * speed;
    e->vy = (dy / dist) * speed;
  }
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  // Still damage the player if we run into them on the way
  resolve_player_collision(e, player_x, player_y);
}

// Brute priority AI: evaluate what a brute should be doing based on damage level
// Returns 1 if the brute changed state (should skip normal behavior), 0 otherwise
static int brute_evaluate_priorities(Enemy_t* e)
{
  if (e->type != ENEMY_TYPE_BRUTE) return 0;
  if (e->state == ENEMY_STATE_CORPSE || e->state == ENEMY_STATE_HIT_KNOCKBACK) return 0;

  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  int total_hp = get_total_hp(e);
  float damage_pct = (total_hp > 0) ? (float)e->hit_count / (float)total_hp : 0.0f;
  if (damage_pct > 1.0f) damage_pct = 1.0f;

  float health_search_r = 0.0f;
  float power_search_r = 0.0f;
  int prefer_health = 0;

  if (damage_pct > 0.75f) {
    // CRITICAL: desperate for health, will cross the map
    health_search_r = 300.0f;
    power_search_r = 200.0f;
    prefer_health = 1;
  } else if (damage_pct > 0.40f) {
    // HURT: opportunistic healing, also check for power
    health_search_r = 150.0f;
    power_search_r = 180.0f;
    prefer_health = 1;
  } else {
    // HEALTHY: hunt power pickups to become more dangerous
    health_search_r = 0.0f;
    power_search_r = 120.0f + 20.0f * (float)e->hit_count;
    prefer_health = 0;
  }

  float hx, hy;
  int found_health = 0;
  if (health_search_r > 0.0f && e->state != ENEMY_STATE_SEEKING_HEALTH) {
    found_health = drops_find_nearest_health(cx, cy, health_search_r, &hx, &hy);
  }

  PickupType_t ptype;
  float px, py;
  int found_power = 0;
  if (power_search_r > 0.0f && e->state != ENEMY_STATE_SEEKING_PICKUP) {
    found_power = pickups_find_nearest(cx, cy, power_search_r, &ptype, &px, &py);
  }

  // Decide based on priority
  if (prefer_health && found_health) {
    if ((e->state == ENEMY_STATE_ATTACKING || e->state == ENEMY_STATE_WINDUP) && current_attackers > 0) {
      current_attackers--;
    }
    e->state = ENEMY_STATE_SEEKING_HEALTH;
    e->heal_target_x = hx;
    e->heal_target_y = hy;
    return 1;
  }

  if (found_power) {
    if ((e->state == ENEMY_STATE_ATTACKING || e->state == ENEMY_STATE_WINDUP) && current_attackers > 0) {
      current_attackers--;
    }
    e->state = ENEMY_STATE_SEEKING_PICKUP;
    e->pickup_target_x = px;
    e->pickup_target_y = py;
    return 1;
  }

  if (!prefer_health && found_health) {
    if ((e->state == ENEMY_STATE_ATTACKING || e->state == ENEMY_STATE_WINDUP) && current_attackers > 0) {
      current_attackers--;
    }
    e->state = ENEMY_STATE_SEEKING_HEALTH;
    e->heal_target_x = hx;
    e->heal_target_y = hy;
    return 1;
  }

  return 0;
}

static void update_seeking_pickup(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  const EnemyStats_t* stats = get_stats(e);
  float radius = stats->radius;
  float speed = get_effective_speed(e) * BRUTE_HEAL_SPEED_MULT; // Sprint speed

  float cx = e->x + radius;
  float cy = e->y + radius;

  // Re-check if pickup still exists
  PickupType_t ptype;
  float px, py;
  float search_r = 120.0f + 20.0f * (float)e->hit_count;
  if (search_r > BRUTE_HEAL_MAX_RADIUS) search_r = BRUTE_HEAL_MAX_RADIUS;

  if (!pickups_find_nearest(cx, cy, search_r + 50.0f, &ptype, &px, &py)) {
    // Pickup gone, go back to normal
    e->state = ENEMY_STATE_ALIVE;
    e->stuck_timer = 0.0f;
    e->last_distance_to_player = 9999.0f;
    return;
  }
  e->pickup_target_x = px;
  e->pickup_target_y = py;

  float dx = e->pickup_target_x - cx;
  float dy = e->pickup_target_y - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < BRUTE_HEAL_PICKUP_DIST) {
    // Consume the pickup and apply buff
    int consumed = pickups_consume_nearest(cx, cy, BRUTE_HEAL_PICKUP_DIST + 10.0f);
    if (consumed >= 0) {
      PickupType_t type = (PickupType_t)consumed;
      e->brute_buff_type = type;
      e->brute_buff.active = 1;
      e->fire_cone_tick_timer = 0.0f;

      switch (type) {
        case PICKUP_FIRE_CONE:
          e->brute_buff.duration = BRUTE_FIRE_DURATION;
          break;
        case PICKUP_SPEED:
          e->brute_buff.duration = BRUTE_SPEED_DURATION;
          break;
        case PICKUP_SHIELD:
          e->brute_buff.duration = BRUTE_SHIELD_DURATION;
          e->brute_buff.shield_hits += BRUTE_SHIELD_HITS;
          break;
        default: break;
      }
    }

    e->state = ENEMY_STATE_ALIVE;
    e->stuck_timer = 0.0f;
    e->last_distance_to_player = 9999.0f;
    return;
  }

  // Sprint toward pickup
  if (dist > 0.1f) {
    e->vx = (dx / dist) * speed;
    e->vy = (dy / dist) * speed;
  }
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  resolve_player_collision(e, player_x, player_y);
}

// Update brute buff ticking (fire cone damage, duration countdown)
static void update_brute_buff(Enemy_t* e, float dt, float player_x, float player_y)
{
  if (!e->brute_buff.active) return;

  // Shield has no time limit — only expires when all hits consumed
  if (e->brute_buff_type != PICKUP_SHIELD) {
    e->brute_buff.duration -= dt;
    if (e->brute_buff.duration <= 0.0f) {
      e->brute_buff.active = 0;
      return;
    }
  }

  // Fire cone: aim at player and tick damage
  if (e->brute_buff_type == PICKUP_FIRE_CONE) {
    e->fire_cone_tick_timer += dt;
    if (e->fire_cone_tick_timer >= BRUTE_FIRE_CONE_TICK_RATE) {
      e->fire_cone_tick_timer -= BRUTE_FIRE_CONE_TICK_RATE;

      float radius = get_stats(e)->radius;
      float cx = e->x + radius;
      float cy = e->y + radius;

      float dx = player_x - cx;
      float dy = player_y - cy;
      float dist = sqrtf(dx * dx + dy * dy);

      if (dist > 0.1f && dist <= BRUTE_FIRE_CONE_RANGE) {
        // The cone is aimed from brute toward player — always hits if in range
        player_take_damage(BRUTE_FIRE_CONE_DAMAGE);
      }
    }
  }
}

static void update_corpse(Enemy_t* e, float dt)
{
  e->death_timer += dt;
  if (e->death_timer >= CORPSE_LIFETIME) {
    e->active = 0;
  }
}

// ============================================================================
// Update (main loop)
// ============================================================================

void enemy_update(float dt, float player_x, float player_y,
                  float player_vx, float player_vy)
{
  blood_update(dt);

  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;

    Enemy_t* e = &enemies[i];

    switch (e->state) {
      case ENEMY_STATE_HIT_KNOCKBACK: update_knockback(e, dt);                                  break;
      case ENEMY_STATE_ALIVE:         update_alive(e, i, dt, player_x, player_y, player_vx, player_vy); break;
      case ENEMY_STATE_REPOSITIONING: update_repositioning(e, i, dt, player_x, player_y);       break;
      case ENEMY_STATE_WINDUP:        update_windup(e, dt, player_x, player_y, player_vx, player_vy); break;
      case ENEMY_STATE_ATTACKING:     update_attacking(e, i, dt, player_x, player_y);           break;
      case ENEMY_STATE_RETREAT:        update_retreat(e, dt);                                      break;
      case ENEMY_STATE_SEEKING_HEALTH: update_seeking_health(e, i, dt, player_x, player_y);      break;
      case ENEMY_STATE_SEEKING_PICKUP: update_seeking_pickup(e, i, dt, player_x, player_y);     break;
      case ENEMY_STATE_CORPSE:        update_corpse(e, dt);                                      break;
    }

    // Tick brute buff effects (fire cone damage, duration countdown)
    if (e->type == ENEMY_TYPE_BRUTE && e->state != ENEMY_STATE_CORPSE) {
      update_brute_buff(e, dt, player_x, player_y);
    }
  }
}

// ============================================================================
// Drawing
// ============================================================================

void enemy_draw(void)
{
  blood_draw();

  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;

    Enemy_t* e = &enemies[i];
    const EnemyStats_t* stats = get_stats(e);

    // Damage gradient: green -> red for all enemy types
    int total_hp = get_total_hp(e);
    float damage_pct = (total_hp > 0) ? (float)e->hit_count / (float)total_hp : 0.0f;
    if (damage_pct > 1.0f) damage_pct = 1.0f;

    int r = ENEMY_COLOR_HEALTHY_R + (int)((ENEMY_COLOR_DAMAGED_R - ENEMY_COLOR_HEALTHY_R) * damage_pct);
    int g = ENEMY_COLOR_HEALTHY_G + (int)((ENEMY_COLOR_DAMAGED_G - ENEMY_COLOR_HEALTHY_G) * damage_pct);
    int b = ENEMY_COLOR_HEALTHY_B + (int)((ENEMY_COLOR_DAMAGED_B - ENEMY_COLOR_HEALTHY_B) * damage_pct);
    if (r > 255) r = 255;
    if (r < 0) r = 0;
    if (g > 255) g = 255;
    if (g < 0) g = 0;
    if (b > 255) b = 255;
    if (b < 0) b = 0;

    // Corpses fade out in their final 2 seconds
    int alpha = 255;
    if (e->state == ENEMY_STATE_CORPSE && e->death_timer > CORPSE_FADE_START) {
      float fade = (e->death_timer - CORPSE_FADE_START) / (CORPSE_LIFETIME - CORPSE_FADE_START);
      alpha = (int)(255 * (1.0f - fade));
      if (alpha < 0) alpha = 0;
    }

    aColor_t color = {r, g, b, alpha};

    if (e->type == ENEMY_TYPE_DASHER) {
      // Triangle pointing toward player (or toward dash direction during windup/attack)
      float cx = e->x + stats->size / 2.0f;
      float cy = e->y + stats->size / 2.0f;
      float half = stats->size / 2.0f;

      float dir_x, dir_y;
      if ((e->state == ENEMY_STATE_WINDUP || e->state == ENEMY_STATE_ATTACKING)
          && (e->dash_dir_x != 0.0f || e->dash_dir_y != 0.0f)) {
        dir_x = e->dash_dir_x;
        dir_y = e->dash_dir_y;
      } else {
        dir_x = e->vx;
        dir_y = e->vy;
        float len = sqrtf(dir_x * dir_x + dir_y * dir_y);
        if (len > 0.1f) { dir_x /= len; dir_y /= len; }
        else { dir_x = 0.0f; dir_y = -1.0f; }
      }

      // Tip = center + dir * half, two base corners perpendicular to dir
      float perp_x = -dir_y;
      float perp_y = dir_x;
      int tip_x = (int)(cx + dir_x * half * 1.3f);
      int tip_y = (int)(cy + dir_y * half * 1.3f);
      int base1_x = (int)(cx - dir_x * half + perp_x * half);
      int base1_y = (int)(cy - dir_y * half + perp_y * half);
      int base2_x = (int)(cx - dir_x * half - perp_x * half);
      int base2_y = (int)(cy - dir_y * half - perp_y * half);

      a_DrawFilledTriangle(tip_x, tip_y, base1_x, base1_y, base2_x, base2_y, color);
    } else {
      a_DrawFilledRect(
        (aRectf_t){e->x, e->y, stats->size, stats->size},
        color
      );

      // Brute devil horns
      if (e->type == ENEMY_TYPE_BRUTE) {
        float sz = stats->size;
        int horn_h = (int)(sz * 0.4f);
        int horn_w = (int)(sz * 0.25f);
        // Left horn
        a_DrawFilledTriangle(
          (int)(e->x + sz * 0.2f), (int)e->y,                    // base inner
          (int)(e->x - horn_w * 0.3f), (int)(e->y - horn_h),     // tip (outward)
          (int)(e->x), (int)e->y,                                 // base outer
          color
        );
        // Right horn
        a_DrawFilledTriangle(
          (int)(e->x + sz * 0.8f), (int)e->y,                    // base inner
          (int)(e->x + sz + horn_w * 0.3f), (int)(e->y - horn_h),// tip (outward)
          (int)(e->x + sz), (int)e->y,                            // base outer
          color
        );
      }
    }

    // Dasher windup: draw red indicator line showing dash trajectory
    if (e->state == ENEMY_STATE_WINDUP && e->dash_dir_x != 0.0f && e->dash_dir_y != 0.0f) {
      float cx = e->x + stats->size / 2.0f;
      float cy = e->y + stats->size / 2.0f;
      float progress = 1.0f - (e->windup_timer / DASHER_WINDUP_TIME);  // 0 -> 1
      int line_alpha = (int)(180 * progress);  // Fades in as windup progresses
      if (line_alpha > 180) line_alpha = 180;
      float line_len = DASHER_INDICATOR_LENGTH * progress;
      a_DrawLine(
        (int)cx, (int)cy,
        (int)(cx + e->dash_dir_x * line_len),
        (int)(cy + e->dash_dir_y * line_len),
        (aColor_t){255, 40, 40, line_alpha}
      );
    }

    // State indicator letter
    const char* label = NULL;
    switch (e->state) {
      case ENEMY_STATE_WINDUP:         label = "W"; break;
      case ENEMY_STATE_ATTACKING:      label = "C"; break;
      case ENEMY_STATE_REPOSITIONING:  label = "F"; break;
      case ENEMY_STATE_RETREAT:        label = "R"; break;
      case ENEMY_STATE_SEEKING_HEALTH: label = "H"; break;
      case ENEMY_STATE_SEEKING_PICKUP: label = "P"; break;
      case ENEMY_STATE_HIT_KNOCKBACK:  label = "!"; break;
      default: break;
    }

    if (label) {
      aTextStyle_t style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {0, 0, 0, (uint8_t)alpha},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.6f
      };
      int lx = (int)(e->x + stats->size / 2);
      int ly = (int)(e->y - 4);

      a_DrawText(label, lx - 1, ly, style);
      a_DrawText(label, lx + 1, ly, style);
      a_DrawText(label, lx, ly - 1, style);
      a_DrawText(label, lx, ly + 1, style);
      a_DrawText(label, lx, ly, style);
    }

    // Brute buff visuals
    if (e->type == ENEMY_TYPE_BRUTE && e->brute_buff.active && e->state != ENEMY_STATE_CORPSE) {
      float sz = stats->size;

      if (e->brute_buff_type == PICKUP_FIRE_CONE) {
        // Orange cone from brute toward player
        float bcx = e->x + sz / 2.0f;
        float bcy = e->y + sz / 2.0f;
        float px = player_get_x();
        float py = player_get_y();
        float fdx = px - bcx;
        float fdy = py - bcy;
        float fdist = sqrtf(fdx * fdx + fdy * fdy);
        if (fdist > 0.1f) {
          fdx /= fdist; fdy /= fdist;
          float cos_a = cosf(BRUTE_FIRE_CONE_HALF_ANGLE);
          float sin_a = sinf(BRUTE_FIRE_CONE_HALF_ANGLE);
          float l1x = fdx * cos_a - fdy * sin_a;
          float l1y = fdx * sin_a + fdy * cos_a;
          float l2x = fdx * cos_a + fdy * sin_a;
          float l2y = -fdx * sin_a + fdy * cos_a;
          float pulse = 60.0f + 40.0f * sinf(e->brute_buff.duration * 8.0f);
          a_DrawFilledTriangle(
            (int)bcx, (int)bcy,
            (int)(bcx + l1x * BRUTE_FIRE_CONE_RANGE), (int)(bcy + l1y * BRUTE_FIRE_CONE_RANGE),
            (int)(bcx + l2x * BRUTE_FIRE_CONE_RANGE), (int)(bcy + l2y * BRUTE_FIRE_CONE_RANGE),
            (aColor_t){255, 130, 30, (uint8_t)pulse}
          );
        }
      } else if (e->brute_buff_type == PICKUP_SPEED) {
        // Yellow glow around brute
        float pulse = 30.0f + 20.0f * sinf(e->brute_buff.duration * 10.0f);
        a_DrawFilledRect(
          (aRectf_t){e->x - 3, e->y - 3, sz + 6, sz + 6},
          (aColor_t){255, 230, 50, (uint8_t)pulse}
        );
      } else if (e->brute_buff_type == PICKUP_SHIELD) {
        int hits = e->brute_buff.shield_hits;
        // Brighter with more shields (cap visual at 220)
        float pulse = 100.0f + 60.0f * sinf(e->death_timer * 6.0f + e->x);
        int sa = (int)pulse;
        if (hits > 3) sa = (int)(pulse * 1.4f);
        if (sa > 220) sa = 220;
        a_DrawRect(
          (aRectf_t){e->x - 2, e->y - 2, sz + 4, sz + 4},
          (aColor_t){50, 150, 255, (uint8_t)sa}
        );
        a_DrawRect(
          (aRectf_t){e->x - 3, e->y - 3, sz + 6, sz + 6},
          (aColor_t){50, 150, 255, (uint8_t)(sa / 2)}
        );

        // Dynamic shield charge pips below brute
        int pip_sz = 4;
        int pip_gap = 2;
        int max_pips = hits > 12 ? 12 : hits; // cap visual at 12 pips
        int total_w = max_pips * pip_sz + (max_pips - 1) * pip_gap;
        int pip_start_x = (int)(e->x + sz / 2.0f) - total_w / 2;
        int pip_y = (int)(e->y + sz + 3);
        for (int p = 0; p < max_pips; p++) {
          int px = pip_start_x + p * (pip_sz + pip_gap);
          a_DrawFilledRect(
            (aRectf_t){(float)px, (float)pip_y, (float)pip_sz, (float)pip_sz},
            (aColor_t){50, 180, 255, 220}
          );
        }
        // If more than 12, show "+N" text
        if (hits > 12) {
          char extra[8];
          snprintf(extra, sizeof(extra), "+%d", hits - 12);
          aTextStyle_t extra_style = {
            .type = FONT_ENTER_COMMAND,
            .fg = {50, 180, 255, 200},
            .align = TEXT_ALIGN_LEFT,
            .scale = 0.25f
          };
          a_DrawText(extra, pip_start_x + total_w + 3, pip_y - 1, extra_style);
        }
      }
    }
  }
}

// ============================================================================
// Getters
// ============================================================================

void enemy_get_position(int enemy_index, float* out_x, float* out_y)
{
  if (enemy_index >= 0 && enemy_index < max_enemies && enemies[enemy_index].active) {
    *out_x = enemies[enemy_index].x;
    *out_y = enemies[enemy_index].y;
  } else {
    *out_x = 0.0f;
    *out_y = 0.0f;
  }
}

EnemyState_t enemy_get_state(int enemy_index)
{
  if (enemy_index >= 0 && enemy_index < max_enemies && enemies[enemy_index].active) {
    return enemies[enemy_index].state;
  }
  return ENEMY_STATE_CORPSE;
}

int enemy_is_active(int enemy_index)
{
  if (enemy_index >= 0 && enemy_index < max_enemies) {
    return enemies[enemy_index].active;
  }
  return 0;
}

int enemy_get_count(void)
{
  int count = 0;
  for (int i = 0; i < max_enemies; i++) {
    if (enemies[i].active && enemies[i].state != ENEMY_STATE_CORPSE) {
      count++;
    }
  }
  return count;
}

int enemy_get_max_count(void)
{
  return max_enemies;
}

int enemy_is_alive(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return 0;
  if (!enemies[enemy_index].active) return 0;
  EnemyState_t s = enemies[enemy_index].state;
  return (s != ENEMY_STATE_CORPSE && s != ENEMY_STATE_HIT_KNOCKBACK);
}

float enemy_get_radius(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return 8.0f;
  return get_stats(&enemies[enemy_index])->radius;
}

void enemy_get_velocity(int enemy_index, float* out_vx, float* out_vy)
{
  if (enemy_index >= 0 && enemy_index < max_enemies && enemies[enemy_index].active) {
    *out_vx = enemies[enemy_index].vx;
    *out_vy = enemies[enemy_index].vy;
  } else {
    *out_vx = 0.0f;
    *out_vy = 0.0f;
  }
}

EnemyType_t enemy_get_type(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return ENEMY_TYPE_GRUNT;
  return enemies[enemy_index].type;
}

int enemy_find_cluster_target(float radius, float player_x, float player_y)
{
  int best = -1;
  int best_neighbors = -1;
  float best_dist = 9999.0f;

  for (int i = 0; i < max_enemies; i++) {
    if (!enemy_is_alive(i)) continue;

    float er_i = get_stats(&enemies[i])->radius;

    int neighbors = 0;
    for (int j = 0; j < max_enemies; j++) {
      if (j == i || !enemy_is_alive(j)) continue;
      float er_j = get_stats(&enemies[j])->radius;
      float d = dist_between(
        enemies[i].x + er_i, enemies[i].y + er_i,
        enemies[j].x + er_j, enemies[j].y + er_j
      );
      if (d <= radius) neighbors++;
    }

    float d_player = dist_between(
      enemies[i].x + er_i, enemies[i].y + er_i,
      player_x, player_y
    );

    if (neighbors > best_neighbors ||
        (neighbors == best_neighbors && d_player < best_dist)) {
      best = i;
      best_neighbors = neighbors;
      best_dist = d_player;
    }
  }

  return best;
}

int enemy_find_cluster_position(float radius, float player_x, float player_y,
                                float lead_time, float* out_x, float* out_y)
{
  int idx = enemy_find_cluster_target(radius, player_x, player_y);
  if (idx < 0) return 0;
  float er = get_stats(&enemies[idx])->radius;
  *out_x = enemies[idx].x + er + enemies[idx].vx * lead_time;
  *out_y = enemies[idx].y + er + enemies[idx].vy * lead_time;
  return 1;
}
