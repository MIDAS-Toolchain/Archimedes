#include "enemy.h"
#include "enemy_internal.h"
#include "beholder.h"
#include "shaman.h"
#include "drops.h"
#include "weapons.h"
#include "player_actions.h"
#include "pickups.h"
#include "game_audio.h"
#include "blood.h"
#include "progress.h"
#include "snake.h"
#include "stats.h"
#include "game_director.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Constants (shared across all types)
// ============================================================================

// AI tuning (local to enemy.c)
#define PREDICTION_TIME 0.4f
#define CHARGE_DISTANCE 200.0f
#define ATTACK_RANGE 120.0f
#define AGGRO_ATTACK_RANGE 200.0f
#define RETREAT_SPEED_MULT 1.5f
#define ATTACK_SEPARATION_SCALE 0.3f
#define ATTACK_SEPARATION_WEIGHT 30.0f
#define FLANK_ARRIVE_DIST 20.0f
#define RETREAT_ARRIVE_DIST 10.0f
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
#define BRUTE_FIRE_CONE_MIN_RANGE  110.0f
#define BRUTE_FIRE_CONE_MAX_RANGE  160.0f
#define BRUTE_FIRE_CONE_GROWTH_INTERVAL 1.0f
#define BRUTE_FIRE_CONE_HALF_ANGLE 0.5236f // 30 degrees
#define BRUTE_FIRE_CONE_TICK_RATE 0.2f
#define BRUTE_FIRE_CONE_DAMAGE    10
#define BRUTE_FIRE_DURATION       BUFF_FIRE_CONE_DURATION
#define BRUTE_SPEED_DURATION      BUFF_SPEED_DURATION
#define BRUTE_SPEED_MULT          BUFF_SPEED_MULT
#define BRUTE_SHIELD_HITS         BUFF_SHIELD_HITS
#define BRUTE_SLOW_AURA_DURATION  BUFF_SLOW_AURA_DURATION
#define BRUTE_SLOW_AURA_MIN_R     40.0f
#define BRUTE_SLOW_AURA_MAX_R     100.0f

// Beholder and Shaman AI constants are in beholder.c and shaman.c

// Forward declarations
static float get_brute_fire_cone_range(Enemy_t* e);

// ============================================================================
// Per-type stats table
// ============================================================================

const EnemyStats_t enemy_stats[ENEMY_TYPE_COUNT] = {
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
    .hits_to_kill = 4, .damage = 20,
    .attack_speed_mult = 3.0f,
    .attack_duration_min = 0.5f, .attack_duration_max = 1.5f,
    .reposition_duration_min = 0.5f, .reposition_duration_max = 1.0f,
    .flank_distance = 150.0f, .separation_radius = 25.0f,
    .skip_reposition = 0
  },
  [ENEMY_TYPE_BRUTE] = {
    .speed = 60.0f, .size = 24.0f, .radius = 12.0f,
    .hits_to_kill = 12, .damage = 20,
    .attack_speed_mult = 1.5f,
    .attack_duration_min = 3.0f, .attack_duration_max = 7.0f,
    .reposition_duration_min = 1.0f, .reposition_duration_max = 2.0f,
    .flank_distance = 80.0f, .separation_radius = 45.0f,
    .skip_reposition = 0
  },
  [ENEMY_TYPE_SHAMAN] = {
    .speed = 90.0f, .size = 14.0f, .radius = 7.0f,
    .hits_to_kill = 2, .damage = 0,
    .attack_speed_mult = 0.0f,
    .attack_duration_min = 0.0f, .attack_duration_max = 0.0f,
    .reposition_duration_min = 0.0f, .reposition_duration_max = 0.0f,
    .flank_distance = 200.0f, .separation_radius = 25.0f,
    .skip_reposition = 1
  },
  [ENEMY_TYPE_BEHOLDER] = {
    .speed = 80.0f, .size = 28.0f, .radius = 14.0f,
    .hits_to_kill = 9, .damage = 20,
    .attack_speed_mult = 0.0f,
    .attack_duration_min = 0.0f, .attack_duration_max = 0.0f,
    .reposition_duration_min = 0.0f, .reposition_duration_max = 0.0f,
    .flank_distance = 250.0f, .separation_radius = 150.0f,
    .skip_reposition = 1
  },
  [ENEMY_TYPE_MIMIC] = {
    .speed = 80.0f, .size = 36.0f, .radius = 18.0f,
    .hits_to_kill = 37, .damage = 20,
    .attack_speed_mult = 1.0f,
    .attack_duration_min = 0.0f, .attack_duration_max = 0.0f,
    .reposition_duration_min = 0.0f, .reposition_duration_max = 0.0f,
    .flank_distance = 0.0f, .separation_radius = 50.0f,
    .skip_reposition = 1
  },
};

// Damage color defines are in enemy_internal.h

// ============================================================================
// Internal State
// ============================================================================

Enemy_t* enemies = NULL;
int max_enemies = 0;
static int first_kill_dropped = 0;
static int extra_slot_dropped = 0;
static int extra_slot_pity = 0;
static int current_attackers = 0;

// Explosion visual effects
#define MAX_EXPLOSIONS 16
#define EXPLOSION_DURATION 0.35f
typedef struct {
  float x, y, radius;
  float timer;
  int active;
} Explosion_t;
static Explosion_t explosions[MAX_EXPLOSIONS];

extern aApp_t app;

// Forward declarations
static int brute_evaluate_priorities(Enemy_t* e);

// ============================================================================
// Utility Helpers
// ============================================================================

float ei_lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

float ei_dist_between(float x1, float y1, float x2, float y2)
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  return sqrtf(dx * dx + dy * dy);
}

float ei_normalize(float* x, float* y)
{
  float len = sqrtf((*x) * (*x) + (*y) * (*y));
  if (len > 0.1f) {
    *x /= len;
    *y /= len;
  }
  return len;
}

int ei_is_offscreen(float x, float y)
{
  return x < -OFFSCREEN_MARGIN || x > SCREEN_WIDTH + OFFSCREEN_MARGIN ||
         y < -OFFSCREEN_MARGIN || y > SCREEN_HEIGHT + OFFSCREEN_MARGIN;
}

const EnemyStats_t* ei_get_stats(Enemy_t* e)
{
  return &enemy_stats[e->type];
}

int ei_get_total_hp(Enemy_t* e)
{
  return ei_get_stats(e)->hits_to_kill + e->bonus_hp;
}

// Returns 0..1 multiplier from all environmental slows (stun, craters, turret, trail, aura)
float ei_get_slow_multiplier(Enemy_t* e)
{
  if (e->stun_timer > 0.0f) return 0.0f;

  float mult = 1.0f;
  float radius = ei_get_stats(e)->radius;
  float ecx = e->x + radius;
  float ecy = e->y + radius;

  float crater_mult;
  if (weapons_is_in_crater(ecx, ecy, &crater_mult)) mult *= 0.5f;

  float turret_slow;
  if (weapons_get_turret_slow(ecx, ecy, &turret_slow)) mult *= turret_slow;

  mult *= weapons_get_trail_slow(ecx, ecy);

  float aura_r = player_get_slow_aura_radius();
  if (aura_r > 0.0f) {
    float px = player_get_x() + 16;
    float py = player_get_y() + 16;
    float dx = ecx - px;
    float dy = ecy - py;
    if (sqrtf(dx * dx + dy * dy) <= aura_r) mult *= 0.5f;
  }

  return mult;
}

float ei_get_effective_speed(Enemy_t* e)
{
  float speed = ei_get_stats(e)->speed * e->speed_mult;

  // Brute rage: speed increases with damage taken
  if (e->type == ENEMY_TYPE_BRUTE) {
    int total_hp = ei_get_total_hp(e);
    float damage_pct = (total_hp > 0) ? (float)e->hit_count / (float)total_hp : 0.0f;
    if (damage_pct > 1.0f) damage_pct = 1.0f;
    // Lerp from 1.0x to BRUTE_RAGE_MAX_SPEED_MULT based on damage
    speed *= 1.0f + (BRUTE_RAGE_MAX_SPEED_MULT - 1.0f) * damage_pct;

    // Speed buff stacks on top of rage
    if (e->brute_buffs[PICKUP_SPEED].active) {
      speed *= BRUTE_SPEED_MULT;
    }

    // Meta-progression: diminishing returns on speed bonuses
    float effectiveness = progress_get_brute_speed_effectiveness();
    if (effectiveness < 1.0f) {
      float base_speed = ei_get_stats(e)->speed * e->speed_mult;
      float bonus = speed - base_speed;
      if (bonus > 0.0f) {
        speed = base_speed + bonus * effectiveness;
      }
    }
  }

  // Stun: speed = 0
  if (e->stun_timer > 0.0f) return 0.0f;

  // Crater slow: 50% speed reduction
  float crater_mult;
  float radius = ei_get_stats(e)->radius;
  if (weapons_is_in_crater(e->x + radius, e->y + radius, &crater_mult)) {
    speed *= 0.5f;
  }

  // Turret slow zone
  float turret_slow;
  if (weapons_get_turret_slow(e->x + radius, e->y + radius, &turret_slow)) {
    speed *= turret_slow;
  }

  // Trail scorched earth slow
  float trail_slow = weapons_get_trail_slow(e->x + radius, e->y + radius);
  speed *= trail_slow;

  // Player slow aura: 50% speed reduction
  float aura_r = player_get_slow_aura_radius();
  if (aura_r > 0.0f) {
    float ecx = e->x + radius;
    float ecy = e->y + radius;
    float px = player_get_x() + 16;
    float py = player_get_y() + 16;
    float dx = ecx - px;
    float dy = ecy - py;
    if (sqrtf(dx * dx + dy * dy) <= aura_r) {
      speed *= 0.5f;
    }
  }

  return speed;
}

void ei_calc_separation(int self_index, float* out_x, float* out_y, float scale_factor)
{
  *out_x = 0.0f;
  *out_y = 0.0f;

  Enemy_t* self = &enemies[self_index];
  float my_sep_radius = ei_get_stats(self)->separation_radius;

  for (int j = 0; j < max_enemies; j++) {
    if (j == self_index || !enemies[j].active) continue;
    if (enemies[j].state == ENEMY_STATE_CORPSE || enemies[j].state == ENEMY_STATE_HIT_KNOCKBACK) continue;

    float dx = self->x - enemies[j].x;
    float dy = self->y - enemies[j].y;
    float dist2 = dx * dx + dy * dy;

    // Use the larger of the two separation radii
    float other_sep_radius = ei_get_stats(&enemies[j])->separation_radius;
    float sep_radius = (my_sep_radius > other_sep_radius) ? my_sep_radius : other_sep_radius;

    if (dist2 < sep_radius * sep_radius && dist2 > 0.01f) {
      float dist = sqrtf(dist2);
      float strength = (sep_radius - dist) / sep_radius * scale_factor;
      *out_x += (dx / dist) * strength;
      *out_y += (dy / dist) * strength;
    }
  }
}

void ei_move_toward(Enemy_t* e, float target_x, float target_y,
                    float speed, float sep_x, float sep_y,
                    float sep_weight, float dt)
{
  float radius = ei_get_stats(e)->radius;
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

int ei_resolve_player_collision(Enemy_t* e, float player_x, float player_y)
{
  float radius = ei_get_stats(e)->radius;
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

void ei_start_knockback(Enemy_t* e, float bullet_vx, float bullet_vy, float knockback_dist)
{
  float bx = bullet_vx;
  float by = bullet_vy;
  float speed = ei_normalize(&bx, &by);

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

  // Stop beholder sounds on knockback
  if (e->type == ENEMY_TYPE_BEHOLDER) {
    beholder_on_knockback(e);
  }

  e->state = ENEMY_STATE_HIT_KNOCKBACK;
}

static void enter_reposition(Enemy_t* e)
{
  const EnemyStats_t* stats = ei_get_stats(e);

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
  const EnemyStats_t* stats = ei_get_stats(e);
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
  extra_slot_dropped = 0;
  extra_slot_pity = 0;
  current_attackers = 0;
  memset(explosions, 0, sizeof(explosions));
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
        .spawn_time = director_get_elapsed(),
        .conductor_timer = 0.0f,
        .conductor_jumps = 0,
        .stun_timer = 0.0f,
        .stun_damage_mult = 1.0f,
        // Corpse tracking (all types)
        .base_hits_to_kill = enemy_stats[type].hits_to_kill,
        .corpse_consumed = 0,
        // Shaman fields (defaults; shaman_init_fields overrides cooldown/orbit)
        .stored_heal_value = 0,
        .heal_target = -1,
        .heal_cooldown_timer = 0.0f,
        .target_corpse = -1,
        .orbit_angle = RANDF(0, 2.0f * (float)PI),
        .orbit_dir_timer = 0.0f,
        .orbit_direction = (rand() % 2) ? 1 : -1,
        .heal_flash_timer = 0.0f,
        .last_hit_source = WEAPON_NONE,
      };
      if (type == ENEMY_TYPE_SHAMAN) {
        shaman_init_fields(&enemies[i]);
      }
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

  // Bonus damage from stun (Static Field T3) and crater (Crater T3)
  int hit_increment = 1;
  if (e->stun_timer > 0.0f && e->stun_damage_mult > 1.0f) {
    hit_increment++;  // +1 hit for stun damage amp
  }
  float crater_dmg;
  float er = ei_get_stats(e)->radius;
  if (weapons_is_in_crater(e->x + er, e->y + er, &crater_dmg) && crater_dmg > 1.0f) {
    hit_increment++;  // +1 hit for crater damage amp
  }
  e->hit_count += hit_increment;
  e->last_hit_source = weapons_get_damage_source();

  // Track weapon damage
  for (int h = 0; h < hit_increment; h++) weapons_record_hit();

  float knockback_dist = KNOCKBACK_STRENGTH;
  int killed = (e->hit_count >= ei_get_total_hp(e));

  // Beholder: delegate to beholder.c for custom shield/sound handling
  if (e->type == ENEMY_TYPE_BEHOLDER) {
    beholder_on_hit(e, bullet_vx, bullet_vy, killed, knockback_dist);
    return;
  }

  // Mimic: delegate to mimic.c for custom death (weapon restore) handling
  if (e->type == ENEMY_TYPE_MIMIC) {
    if (mimic_on_hit(e, bullet_vx, bullet_vy, killed, knockback_dist))
      return;
  }

  if (killed) {
    knockback_dist *= KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE);

    // Store bullet velocity for blood splatter direction after knockback
    e->vx = bullet_vx;
    e->vy = bullet_vy;

    game_audio_play_die();
  } else {
    game_audio_play_hit();
  }

  // Dashers mid-dash: take damage but don't interrupt their attack
  if (e->type == ENEMY_TYPE_DASHER && !killed &&
      (e->state == ENEMY_STATE_WINDUP || e->state == ENEMY_STATE_ATTACKING)) {
    return;
  }

  // Brutes are unstoppable — no knockback
  // Shield buff absorbs hits
  if (e->type == ENEMY_TYPE_BRUTE &&
      e->brute_buffs[PICKUP_SHIELD].active && e->brute_buffs[PICKUP_SHIELD].shield_hits > 0) {
    e->hit_count--; // Undo the hit_count increment
    e->brute_buffs[PICKUP_SHIELD].shield_hits--;
    if (e->brute_buffs[PICKUP_SHIELD].shield_hits <= 0) {
      e->brute_buffs[PICKUP_SHIELD].active = 0;
    }
    return; // Shield absorbed, no knockback
  }
  if (e->type == ENEMY_TYPE_BRUTE && !killed) {

    // Re-evaluate priorities on every hit (interrupt current action)
    brute_evaluate_priorities(e);
    return;  // No knockback for brutes
  }

  ei_start_knockback(e, bullet_vx, bullet_vy, knockback_dist);
}

void enemy_hit_no_knockback(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  if (!enemies[enemy_index].active) return;

  Enemy_t* e = &enemies[enemy_index];
  if (e->state == ENEMY_STATE_CORPSE) return;

  e->hit_count++;
  e->last_hit_source = weapons_get_damage_source();
  weapons_record_hit();

  if (e->hit_count >= ei_get_total_hp(e)) {
    // Kill — do full knockback death sequence
    float knockback_dist = KNOCKBACK_STRENGTH *
      (KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE));
    e->vx = 0.0f;
    e->vy = 0.0f;
    game_audio_play_die();
    ei_start_knockback(e, 0.0f, 0.0f, knockback_dist);
  }
}

// ============================================================================
// Collision
// ============================================================================

int enemy_check_collision(float x, float y, float radius)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active || enemies[i].state == ENEMY_STATE_CORPSE) continue;

    float er = ei_get_stats(&enemies[i])->radius;
    float dist = ei_dist_between(x + radius, y + radius,
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

    if (e->hit_count >= ei_get_total_hp(e)) {
      e->state = ENEMY_STATE_CORPSE;
      e->death_timer = 0.0f;
      for (int b = 0; b < PICKUP_TYPE_COUNT; b++) e->brute_buffs[b].active = 0; // Clear all buffs on death
      e->target_corpse = -1;    // Release any claimed corpse
      e->heal_target = -1;
      if (e->type == ENEMY_TYPE_MIMIC) {
        mimic_on_death(e);  // Restore stolen weapon
        e->corpse_consumed = 1;  // Skip corpse lingering
      } else if (e->type == ENEMY_TYPE_SHAMAN) {
        blood_spawn_short(e->x, e->y, e->vx, e->vy, ei_get_stats(e)->radius, 2.0f);
        e->corpse_consumed = 1;  // Skip corpse lingering
      } else {
        blood_spawn(e->x, e->y, e->vx, e->vy, ei_get_stats(e)->radius);
      }

      if (!first_kill_dropped) {
        WeaponType_t pool[5];
        int pool_count = 0;
        if (!weapons_has(WEAPON_WAND)  && !drops_has_type(WEAPON_WAND)  && !weapons_is_type_stolen(WEAPON_WAND))  pool[pool_count++] = WEAPON_WAND;
        if (!weapons_has(WEAPON_SPIN)  && !drops_has_type(WEAPON_SPIN)  && !weapons_is_type_stolen(WEAPON_SPIN))  pool[pool_count++] = WEAPON_SPIN;
        if (!weapons_has(WEAPON_CHAIN) && !drops_has_type(WEAPON_CHAIN) && !weapons_is_type_stolen(WEAPON_CHAIN)) pool[pool_count++] = WEAPON_CHAIN;
        if (!weapons_has(WEAPON_ORBIT) && !drops_has_type(WEAPON_ORBIT) && !weapons_is_type_stolen(WEAPON_ORBIT)) pool[pool_count++] = WEAPON_ORBIT;
        if (!weapons_has(WEAPON_BOMB)  && !drops_has_type(WEAPON_BOMB)  && !weapons_is_type_stolen(WEAPON_BOMB))  pool[pool_count++] = WEAPON_BOMB;
        if (pool_count > 0) {
          drops_spawn(e->x, e->y, pool[rand() % pool_count]);
        }
        first_kill_dropped = 1;
      } else if (global_has_extra_weapon_slot() && !extra_slot_dropped) {
        // Extra Weapon Slot upgrade: 0%, 2%, 4%, 6%... (+2% per failed kill)
        int chance = extra_slot_pity * 2;
        if ((rand() % 100) < chance) {
          WeaponType_t all[] = {WEAPON_WAND, WEAPON_SPIN, WEAPON_CHAIN, WEAPON_ORBIT,
                                WEAPON_BOMB, WEAPON_TURRET, WEAPON_TRAIL};
          WeaponType_t pool[7];
          int pool_count = 0;
          for (int w = 0; w < 7; w++) {
            if (!weapons_has(all[w]) && !drops_has_type(all[w]) && !weapons_is_type_stolen(all[w]))
              pool[pool_count++] = all[w];
          }
          if (pool_count > 0) {
            drops_spawn(e->x, e->y, pool[rand() % pool_count]);
            extra_slot_dropped = 1;
          }
        } else {
          extra_slot_pity++;
        }
      }
    } else if (e->type == ENEMY_TYPE_MIMIC) {
      e->state = ENEMY_STATE_MM_FIGHTING;
    } else if (e->aggro && (e->type == ENEMY_TYPE_DASHER || !player_is_invincible())) {
      enter_attacking(e);
    } else {
      e->state = ENEMY_STATE_ALIVE;
      e->stuck_timer = 0.0f;
      e->last_distance_to_player = 9999.0f;
    }
  } else {
    float t = e->knockback_timer / KNOCKBACK_DURATION;
    e->x = ei_lerp(e->knockback_start_x, e->knockback_target_x, t);
    e->y = ei_lerp(e->knockback_start_y, e->knockback_target_y, t);
  }
}

static void update_alive(Enemy_t* e, int index, float dt,
                         float player_x, float player_y,
                         float player_vx, float player_vy)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float radius = stats->radius;
  float speed = ei_get_effective_speed(e);

  // Brutes proactively scan for pickups/health even when not being hit
  if (e->type == ENEMY_TYPE_BRUTE && brute_evaluate_priorities(e)) {
    return;
  }

  float cx = e->x + radius;
  float cy = e->y + radius;
  float dist = ei_dist_between(cx, cy, player_x, player_y);

  // Brute with fire cone: orbit at fire range instead of charging in
  if (e->type == ENEMY_TYPE_BRUTE && e->brute_buffs[PICKUP_FIRE_CONE].active) {
    float ideal_dist = get_brute_fire_cone_range(e) * 0.75f;
    float tolerance = 15.0f;

    // Direction from player to brute
    float dx = cx - player_x;
    float dy = cy - player_y;
    if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
    float ndx = dx / dist;
    float ndy = dy / dist;

    // Strafe perpendicular to maintain orbit
    float perp_x = -ndy;
    float perp_y = ndx;

    float target_x, target_y;
    if (dist < ideal_dist - tolerance) {
      // Too close: back away + strafe
      target_x = cx + ndx * 40.0f + perp_x * 30.0f;
      target_y = cy + ndy * 40.0f + perp_y * 30.0f;
    } else if (dist > ideal_dist + tolerance) {
      // Too far: close in + strafe
      target_x = player_x + ndx * ideal_dist + perp_x * 30.0f;
      target_y = player_y + ndy * ideal_dist + perp_y * 30.0f;
    } else {
      // In range: strafe around player to keep in cone
      target_x = cx + perp_x * 50.0f;
      target_y = cy + perp_y * 50.0f;
    }

    float fire_speed = speed * 1.3f; // Slightly faster while fire-breathing
    float sep_x, sep_y;
    ei_calc_separation(index, &sep_x, &sep_y, 0.5f);
    ei_move_toward(e, target_x, target_y, fire_speed, sep_x, sep_y, SEPARATION_WEIGHT * 0.5f, dt);
    ei_resolve_player_collision(e, player_x, player_y);
    if (ei_is_offscreen(e->x, e->y)) e->active = 0;
    return;
  }

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
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);
  ei_move_toward(e, target_x, target_y, speed, sep_x, sep_y, SEPARATION_WEIGHT, dt);
  ei_resolve_player_collision(e, player_x, player_y);

  if (ei_is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_repositioning(Enemy_t* e, int index, float dt,
                                 float player_x, float player_y)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float radius = stats->radius;
  float speed = ei_get_effective_speed(e);

  // Brutes proactively scan for pickups/health during repositioning too
  if (e->type == ENEMY_TYPE_BRUTE && brute_evaluate_priorities(e)) {
    return;
  }

  e->reposition_duration -= dt;

  float target_x = player_x + cosf(e->target_angle) * stats->flank_distance;
  float target_y = player_y + sinf(e->target_angle) * stats->flank_distance;

  float dist_to_target = ei_dist_between(e->x + radius, e->y + radius,
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
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);
  ei_move_toward(e, target_x, target_y, speed, sep_x, sep_y, SEPARATION_WEIGHT, dt);
  ei_resolve_player_collision(e, player_x, player_y);

  if (ei_is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_windup(Enemy_t* e, float dt, float player_x, float player_y,
                          float player_vx, float player_vy)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float radius = stats->radius;
  float dash_speed = ei_get_effective_speed(e) * stats->attack_speed_mult;

  // Lock aim during final 10% of windup so player can dodge-react
  float lock_threshold = DASHER_WINDUP_TIME * 0.1f;
  if (e->windup_timer > lock_threshold) {
    // Predict where the player will be when the dash arrives
    float dx_raw = player_x - (e->x + radius);
    float dy_raw = player_y - (e->y + radius);
    float dist_to_player = sqrtf(dx_raw * dx_raw + dy_raw * dy_raw);
    float time_to_reach = (dash_speed > 0.1f) ? dist_to_player / dash_speed : 0.0f;

    float predict_x = player_x + player_vx * time_to_reach;
    float predict_y = player_y + player_vy * time_to_reach;

    float dx = predict_x - (e->x + radius);
    float dy = predict_y - (e->y + radius);
    ei_normalize(&dx, &dy);
    e->dash_dir_x = dx;
    e->dash_dir_y = dy;
  }

  // Pull back slightly (opposite of dash direction)
  float pullback_speed = DASHER_WINDUP_PULLBACK / DASHER_WINDUP_TIME;
  e->x -= e->dash_dir_x * pullback_speed * dt;
  e->y -= e->dash_dir_y * pullback_speed * dt;

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
  const EnemyStats_t* stats = ei_get_stats(e);
  float speed = ei_get_effective_speed(e) * stats->attack_speed_mult;

  e->attack_duration -= dt;

  // Grunts/brutes back off when player is invincible, but dashers commit to their charge
  if (e->type != ENEMY_TYPE_DASHER && player_is_invincible()) {
    enter_reposition(e);
    return;
  }

  if (e->attack_duration <= 0.0f) {
    // Dasher self-stun on missed charge
    if (e->type == ENEMY_TYPE_DASHER) {
      float stun_dur = progress_get_dasher_stun_duration();
      if (stun_dur > 0.0f) {
        int idx = (int)(e - enemies);
        enemy_set_stun(idx, stun_dur, 1.0f);
      }
    }
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
    ei_calc_separation(index, &sep_x, &sep_y, ATTACK_SEPARATION_SCALE);
    ei_move_toward(e, player_x, player_y, speed,
                sep_x, sep_y, ATTACK_SEPARATION_WEIGHT, dt);
  }

  if (ei_resolve_player_collision(e, player_x, player_y)) {
    int dmg = stats->damage - progress_get_dmg_reduction(e->type);
    if (dmg < 1) dmg = 1;
    player_take_damage(dmg, e->x + stats->radius, e->y + stats->radius, e->type);

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

  if (ei_is_offscreen(e->x, e->y)) {
    if (e->state == ENEMY_STATE_ATTACKING && current_attackers > 0) current_attackers--;
    e->active = 0;
  }
}

static void update_retreat(Enemy_t* e, float dt)
{
  float speed = ei_get_effective_speed(e) * RETREAT_SPEED_MULT;
  // Cap brute retreat speed so buffs don't send them across the map
  if (e->type == ENEMY_TYPE_BRUTE && speed > 150.0f) speed = 150.0f;

  float dist = ei_dist_between(e->x, e->y, e->flank_x, e->flank_y);

  if (dist < RETREAT_ARRIVE_DIST) {
    enter_reposition(e);
    return;
  }

  float dx = e->flank_x - e->x;
  float dy = e->flank_y - e->y;
  ei_normalize(&dx, &dy);

  e->vx = dx * speed;
  e->vy = dy * speed;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  if (ei_is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_seeking_health(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  const EnemyStats_t* stats = ei_get_stats(e);
  float radius = stats->radius;
  float speed = ei_get_effective_speed(e) * BRUTE_HEAL_SPEED_MULT;

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
  ei_resolve_player_collision(e, player_x, player_y);
}

// Brute priority AI: evaluate what a brute should be doing based on damage level
// Returns 1 if the brute changed state (should skip normal behavior), 0 otherwise
static int brute_evaluate_priorities(Enemy_t* e)
{
  if (e->type != ENEMY_TYPE_BRUTE) return 0;
  if (e->state == ENEMY_STATE_CORPSE || e->state == ENEMY_STATE_HIT_KNOCKBACK) return 0;

  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  int total_hp = ei_get_total_hp(e);
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
  const EnemyStats_t* stats = ei_get_stats(e);
  float radius = stats->radius;
  float speed = ei_get_effective_speed(e) * BRUTE_HEAL_SPEED_MULT; // Sprint speed

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

      switch (type) {
        case PICKUP_FIRE_CONE:
          if (e->brute_buffs[PICKUP_FIRE_CONE].active) {
            e->brute_buffs[PICKUP_FIRE_CONE].duration += BRUTE_FIRE_DURATION;
          } else {
            e->brute_buffs[PICKUP_FIRE_CONE].duration = BRUTE_FIRE_DURATION;
            e->fire_cone_elapsed = 0.0f;
            e->fire_cone_tick_timer = 0.0f;
          }
          e->brute_buffs[PICKUP_FIRE_CONE].active = 1;
          break;
        case PICKUP_SPEED:
          e->brute_buffs[PICKUP_SPEED].active = 1;
          e->brute_buffs[PICKUP_SPEED].duration = BRUTE_SPEED_DURATION;
          break;
        case PICKUP_SHIELD:
          e->brute_buffs[PICKUP_SHIELD].active = 1;
          e->brute_buffs[PICKUP_SHIELD].shield_hits += BRUTE_SHIELD_HITS;
          break;
        case PICKUP_SLOW_AURA:
          if (e->brute_buffs[PICKUP_SLOW_AURA].active) {
            e->brute_buffs[PICKUP_SLOW_AURA].duration += BRUTE_SLOW_AURA_DURATION;
          } else {
            e->brute_buffs[PICKUP_SLOW_AURA].duration = BRUTE_SLOW_AURA_DURATION;
            e->slow_aura_elapsed = 0.0f;
          }
          e->brute_buffs[PICKUP_SLOW_AURA].active = 1;
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

  ei_resolve_player_collision(e, player_x, player_y);
}

// Update brute buff ticking (fire cone damage, duration countdown)
static float get_brute_fire_cone_range(Enemy_t* e)
{
  // Grows from min toward max over base duration, keeps growing beyond if stacked
  float pct = e->fire_cone_elapsed / BRUTE_FIRE_DURATION;
  return BRUTE_FIRE_CONE_MIN_RANGE +
    (BRUTE_FIRE_CONE_MAX_RANGE - BRUTE_FIRE_CONE_MIN_RANGE) * pct;
}

static void update_brute_buff(Enemy_t* e, float dt, float player_x, float player_y)
{
  // Tick each buff type independently
  for (int b = 0; b < PICKUP_TYPE_COUNT; b++) {
    if (!e->brute_buffs[b].active) continue;

    // Shield has no time limit — only expires when all hits consumed
    if (b == PICKUP_SHIELD) continue;

    e->brute_buffs[b].duration -= dt;
    if (e->brute_buffs[b].duration <= 0.0f) {
      e->brute_buffs[b].active = 0;
      if (b == PICKUP_FIRE_CONE) e->fire_cone_elapsed = 0.0f;
      if (b == PICKUP_SLOW_AURA) e->slow_aura_elapsed = 0.0f;
      continue;
    }

    if (b == PICKUP_FIRE_CONE) {
      e->fire_cone_elapsed += dt;
    }
    if (b == PICKUP_SLOW_AURA) {
      e->slow_aura_elapsed += dt;
    }
  }

  // Fire cone: aim at player and tick damage
  if (e->brute_buffs[PICKUP_FIRE_CONE].active) {
    float cone_range = get_brute_fire_cone_range(e);

    e->fire_cone_tick_timer += dt;
    if (e->fire_cone_tick_timer >= BRUTE_FIRE_CONE_TICK_RATE) {
      e->fire_cone_tick_timer -= BRUTE_FIRE_CONE_TICK_RATE;

      float radius = ei_get_stats(e)->radius;
      float cx = e->x + radius;
      float cy = e->y + radius;

      float dx = player_x - cx;
      float dy = player_y - cy;
      float dist = sqrtf(dx * dx + dy * dy);

      if (dist > 0.1f && dist <= cone_range) {
        int dmg = BRUTE_FIRE_CONE_DAMAGE - progress_get_dmg_reduction(ENEMY_TYPE_BRUTE);
        if (dmg < 1) dmg = 1;
        player_take_damage(dmg, cx, cy, ENEMY_TYPE_BRUTE);
      }
    }
  }
}

// ============================================================================
// Shared Steering Helpers (used by shaman, beholder, and any future smart enemies)
// ============================================================================

// Returns 1 if (x,y) is inside any active hazard (linger zone or fire trail)
int ei_is_in_hazard(float x, float y)
{
  return weapons_is_in_linger_zone(x, y) || weapons_is_in_trail(x, y);
}

// Compute a repulsion vector pushing away from the nearest hazard within radius.
// Strength falls off linearly with distance. weight controls the max magnitude.
void ei_calc_hazard_avoidance(float cx, float cy, float radius, float weight,
                              float* out_x, float* out_y)
{
  *out_x = 0;
  *out_y = 0;
  float hz_x, hz_y;
  if (weapons_get_nearest_hazard(cx, cy, radius, &hz_x, &hz_y)) {
    float dx = cx - hz_x, dy = cy - hz_y;
    float d = ei_normalize(&dx, &dy);
    float urg = 1.0f - (d / radius);
    if (urg < 0) urg = 0;
    *out_x = dx * urg * weight;
    *out_y = dy * urg * weight;
  }
}

// Compute a repulsion vector pushing away from the nearest turret within radius.
// Strength falls off linearly. weight controls the max magnitude.
void ei_calc_turret_avoidance(float cx, float cy, float radius, float weight,
                              float* out_x, float* out_y)
{
  *out_x = 0;
  *out_y = 0;
  float tx, ty;
  if (weapons_get_nearest_turret(cx, cy, radius, &tx, &ty)) {
    float dx = cx - tx, dy = cy - ty;
    float d = ei_normalize(&dx, &dy);
    float urg = 1.0f - (d / radius);
    if (urg < 0) urg = 0;
    *out_x = dx * urg * weight;
    *out_y = dy * urg * weight;
  }
}

// Steer toward (tcx,tcy) while avoiding hazards. If the direct path hits a
// hazard, steer perpendicular (picks whichever side is clear). Writes to vx/vy.
void ei_steer_toward_avoiding_hazards(float cx, float cy, float tcx, float tcy,
                                      float speed, float dt,
                                      float* out_vx, float* out_vy)
{
  float dx = tcx - cx;
  float dy = tcy - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < 0.1f) return;

  float next_x = cx + (dx / dist) * speed * dt;
  float next_y = cy + (dy / dist) * speed * dt;

  if (ei_is_in_hazard(next_x, next_y)) {
    // Steer perpendicular — try both sides, pick the clear one
    float perp_lx = -dy / dist, perp_ly = dx / dist;
    float perp_rx = dy / dist, perp_ry = -dx / dist;

    float try_lx = cx + perp_lx * speed * dt;
    float try_ly = cy + perp_ly * speed * dt;
    float try_rx = cx + perp_rx * speed * dt;
    float try_ry = cy + perp_ry * speed * dt;

    int left_clear = !ei_is_in_hazard(try_lx, try_ly);
    int right_clear = !ei_is_in_hazard(try_rx, try_ry);

    if (left_clear && !right_clear) {
      *out_vx = perp_lx * speed;
      *out_vy = perp_ly * speed;
    } else if (right_clear && !left_clear) {
      *out_vx = perp_rx * speed;
      *out_vy = perp_ry * speed;
    } else {
      *out_vx = perp_lx * speed;
      *out_vy = perp_ly * speed;
    }
  } else {
    *out_vx = (dx / dist) * speed;
    *out_vy = (dy / dist) * speed;
  }
}

// Shaman and Beholder state handlers are in shaman.c and beholder.c

// ============================================================================
// Corpse
// ============================================================================

static void update_corpse(Enemy_t* e, int index, float dt)
{
  if (e->corpse_consumed) {
    e->active = 0;
    return;
  }

  // Freeze timer if a shaman is coming to eat this corpse
  if (!shaman_corpse_is_claimed(index)) {
    e->death_timer += dt;
  }

  float corpse_life = progress_get_corpse_lifetime();
  if (e->death_timer >= corpse_life) {
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

  // Tick explosion effects
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (explosions[i].active) {
      explosions[i].timer -= dt;
      if (explosions[i].timer <= 0.0f) explosions[i].active = 0;
    }
  }

  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;

    Enemy_t* e = &enemies[i];

    // Tick down debuff timers
    // Conductor: fires chain lightning on expiry
    if (e->conductor_timer > 0.0f && e->state != ENEMY_STATE_CORPSE) {
      e->conductor_timer -= dt;
      if (e->conductor_timer <= 0.0f) {
        e->conductor_timer = 0.0f;
        weapons_set_damage_source(SOURCE_CONDUCTOR);
        weapons_fire_conductor_chain(i, e->conductor_jumps);
        weapons_set_damage_source(WEAPON_NONE);
        e->conductor_jumps = 0;
      }
    }

    if (e->stun_timer > 0.0f) {
      e->stun_timer -= dt;
      if (e->stun_timer <= 0.0f) {
        e->stun_timer = 0.0f;
        e->stun_damage_mult = 1.0f;
      }
      // Stunned: skip movement/state machine (still tick brute buff below)
      if (e->state != ENEMY_STATE_CORPSE && e->state != ENEMY_STATE_HIT_KNOCKBACK) {
        e->vx = 0.0f;
        e->vy = 0.0f;
        goto skip_state_machine;
      }
    }

    switch (e->state) {
      case ENEMY_STATE_HIT_KNOCKBACK: update_knockback(e, dt);                                  break;
      case ENEMY_STATE_ALIVE:
        if (e->type == ENEMY_TYPE_SHAMAN) {
          shaman_update(e, i, dt, player_x, player_y);
        } else {
          update_alive(e, i, dt, player_x, player_y, player_vx, player_vy);
        }
        break;
      case ENEMY_STATE_REPOSITIONING: update_repositioning(e, i, dt, player_x, player_y);       break;
      case ENEMY_STATE_WINDUP:        update_windup(e, dt, player_x, player_y, player_vx, player_vy); break;
      case ENEMY_STATE_ATTACKING:     update_attacking(e, i, dt, player_x, player_y);           break;
      case ENEMY_STATE_RETREAT:        update_retreat(e, dt);                                      break;
      case ENEMY_STATE_SEEKING_HEALTH: update_seeking_health(e, i, dt, player_x, player_y);      break;
      case ENEMY_STATE_SEEKING_PICKUP: update_seeking_pickup(e, i, dt, player_x, player_y);     break;
      // Shaman states — delegated to shaman.c
      case ENEMY_STATE_SEEKING_CORPSE:
      case ENEMY_STATE_EATING:
      case ENEMY_STATE_SEEKING_ALLY:
      case ENEMY_STATE_HEALING:
      case ENEMY_STATE_FLEEING:        shaman_update(e, i, dt, player_x, player_y);              break;
      // Beholder states — delegated to beholder.c
      case ENEMY_STATE_BH_ENTERING:
      case ENEMY_STATE_BH_STRAFING:
      case ENEMY_STATE_BH_AIMING:
      case ENEMY_STATE_BH_FIRING:
      case ENEMY_STATE_BH_FLEEING:
      case ENEMY_STATE_BH_CHANNELING:  beholder_update(e, i, dt, player_x, player_y, player_vx, player_vy); break;
      // Mimic states — delegated to mimic.c
      case ENEMY_STATE_MM_SPAWNING:
      case ENEMY_STATE_MM_STEALING:
      case ENEMY_STATE_MM_FIGHTING:
      case ENEMY_STATE_MM_DYING:       mimic_update(e, i, dt, player_x, player_y, player_vx, player_vy); break;
      case ENEMY_STATE_CORPSE:        update_corpse(e, i, dt);                                   break;
    }

skip_state_machine:
    // Beholder contact damage — delegated to beholder.c
    if (e->type == ENEMY_TYPE_BEHOLDER && e->state != ENEMY_STATE_CORPSE) {
      beholder_contact_damage(e, player_x, player_y);
    }

    // Tick brute buff effects (fire cone damage, duration countdown)
    if (e->type == ENEMY_TYPE_BRUTE && e->state != ENEMY_STATE_CORPSE) {
      update_brute_buff(e, dt, player_x, player_y);
    }

    // Tick shaman heal cooldown — delegated to shaman.c
    if (e->type == ENEMY_TYPE_SHAMAN && e->state != ENEMY_STATE_CORPSE) {
      shaman_tick_cooldown(e, dt);
    }

    // Tick beholder cosmetic timers — delegated to beholder.c
    if (e->type == ENEMY_TYPE_BEHOLDER && e->state != ENEMY_STATE_CORPSE) {
      beholder_tick_cosmetics(e, dt);
    }

    // Tick heal flash timer (any enemy that was just healed)
    if (e->heal_flash_timer > 0.0f) e->heal_flash_timer -= dt;
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
    const EnemyStats_t* stats = ei_get_stats(e);

    // Damage gradient: green -> red for all enemy types
    int total_hp = ei_get_total_hp(e);
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
    if (e->state == ENEMY_STATE_CORPSE) {
      float corpse_life = progress_get_corpse_lifetime();
      float fade_start = corpse_life - 2.0f;
      if (fade_start < 0.0f) fade_start = 0.0f;
      if (e->death_timer > fade_start) {
        float fade = (e->death_timer - fade_start) / (corpse_life - fade_start);
        alpha = (int)(255 * (1.0f - fade));
        if (alpha < 0) alpha = 0;
      }
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
    } else if (e->type == ENEMY_TYPE_SHAMAN || e->type == ENEMY_TYPE_BEHOLDER) {
      // Delegated to shaman.c / beholder.c
      if (e->type == ENEMY_TYPE_SHAMAN) {
        shaman_draw(e, r, g, b, alpha);
      } else {
        beholder_draw(e, r, g, b, alpha);
      }
    } else if (e->type == ENEMY_TYPE_MIMIC) {
      mimic_draw(e, r, g, b, alpha);
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


    // Stun visual: white flash + orbiting stars
    if (e->stun_timer > 0.0f && e->state != ENEMY_STATE_CORPSE) {
      // Pulsing white overlay
      float pulse = 0.5f + 0.5f * sinf(e->stun_timer * 16.0f);
      int alpha = 80 + (int)(100.0f * pulse);
      a_DrawFilledRect(
        (aRectf_t){e->x - 1, e->y - 1, stats->size + 2, stats->size + 2},
        (aColor_t){255, 255, 255, (uint8_t)alpha}
      );
      // Orbiting stars above head
      float cx = e->x + stats->size / 2.0f;
      float cy = e->y - 6.0f;
      float orbit_r = stats->size * 0.45f;
      float phase = e->stun_timer * 6.0f;
      for (int s = 0; s < 3; s++) {
        float a = phase + (float)s * 2.094f;
        float sx = cx + cosf(a) * orbit_r;
        float sy = cy + sinf(a) * 0.35f * orbit_r;
        a_DrawFilledRect(
          (aRectf_t){sx - 1.5f, sy - 1.5f, 3, 3},
          (aColor_t){255, 255, 100, 220}
        );
      }
    }

    // Conductor visual: electric sparks (small blue dots)
    if (e->conductor_timer > 0.0f && e->state != ENEMY_STATE_CORPSE) {
      float cx = e->x + stats->size / 2.0f;
      float cy = e->y + stats->size / 2.0f;
      float spark_r = stats->size * 0.6f;
      int sparks = 3;
      for (int s = 0; s < sparks; s++) {
        float angle = (float)s / sparks * 2.0f * (float)PI + e->conductor_timer * 8.0f;
        float sx = cx + cosf(angle) * spark_r;
        float sy = cy + sinf(angle) * spark_r;
        a_DrawFilledRect(
          (aRectf_t){sx - 1, sy - 1, 3, 3},
          (aColor_t){150, 200, 255, 200}
        );
      }
    }

    // Brute buff visuals — draw ALL active buffs independently
    if (e->type == ENEMY_TYPE_BRUTE && e->state != ENEMY_STATE_CORPSE) {
      float sz = stats->size;
      float bcx = e->x + sz / 2.0f;
      float bcy = e->y + sz / 2.0f;

      if (e->brute_buffs[PICKUP_FIRE_CONE].active) {
        // Orange cone from brute toward player
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
          float pulse = 60.0f + 40.0f * sinf(e->brute_buffs[PICKUP_FIRE_CONE].duration * 8.0f);
          float bcr = get_brute_fire_cone_range(e);
          a_DrawFilledTriangle(
            (int)bcx, (int)bcy,
            (int)(bcx + l1x * bcr), (int)(bcy + l1y * bcr),
            (int)(bcx + l2x * bcr), (int)(bcy + l2y * bcr),
            (aColor_t){255, 130, 30, (uint8_t)pulse}
          );
        }
      }
      if (e->brute_buffs[PICKUP_SPEED].active) {
        // Yellow glow around brute
        float pulse = 30.0f + 20.0f * sinf(e->brute_buffs[PICKUP_SPEED].duration * 10.0f);
        a_DrawFilledRect(
          (aRectf_t){e->x - 3, e->y - 3, sz + 6, sz + 6},
          (aColor_t){255, 230, 50, (uint8_t)pulse}
        );
      }
      if (e->brute_buffs[PICKUP_SHIELD].active) {
        int hits = e->brute_buffs[PICKUP_SHIELD].shield_hits;
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
          char extra[16];
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
      if (e->brute_buffs[PICKUP_SLOW_AURA].active) {
        // Icy aura circle around brute — grows over time, past max if stacked
        float pct = e->slow_aura_elapsed / BRUTE_SLOW_AURA_DURATION;
        float aura_r = BRUTE_SLOW_AURA_MIN_R + (BRUTE_SLOW_AURA_MAX_R - BRUTE_SLOW_AURA_MIN_R) * pct;
        float pulse = 0.7f + 0.3f * sinf(e->brute_buffs[PICKUP_SLOW_AURA].duration * 4.0f);
        int alpha = (int)(40.0f * pulse);

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        int ri = (int)aura_r;
        for (int dy = -ri; dy <= ri; dy++) {
          int half_w = (int)sqrtf((float)(ri * ri - dy * dy));
          SDL_SetRenderDrawColor(app.renderer, 150, 80, 80, (uint8_t)alpha);
          SDL_RenderDrawLine(app.renderer,
            (int)bcx - half_w, (int)bcy + dy,
            (int)bcx + half_w, (int)bcy + dy);
        }
        // Edge ring (line segments)
        int edge_alpha = (int)(80.0f * pulse);
        SDL_SetRenderDrawColor(app.renderer, 200, 100, 100, (uint8_t)edge_alpha);
        int ring_segs = 24;
        for (int s = 0; s < ring_segs; s++) {
          float a1 = (float)s / ring_segs * 2.0f * (float)PI;
          float a2 = (float)(s + 1) / ring_segs * 2.0f * (float)PI;
          SDL_RenderDrawLine(app.renderer,
            (int)(bcx + cosf(a1) * aura_r), (int)(bcy + sinf(a1) * aura_r),
            (int)(bcx + cosf(a2) * aura_r), (int)(bcy + sinf(a2) * aura_r));
        }
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
      }
    }

    // Heal flash (green overlay when enemy was just healed by shaman)
    if (e->heal_flash_timer > 0.0f && e->state != ENEMY_STATE_CORPSE) {
      float flash_alpha = (e->heal_flash_timer / 0.15f) * 150.0f;
      a_DrawFilledRect(
        (aRectf_t){e->x - 2, e->y - 2, stats->size + 4, stats->size + 4},
        (aColor_t){50, 255, 50, (uint8_t)(int)flash_alpha}
      );
    }

  }

  // Draw explosion effects
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (!explosions[i].active) continue;
    float progress = 1.0f - (explosions[i].timer / EXPLOSION_DURATION);
    float r = explosions[i].radius * progress;
    int alpha = (int)(180.0f * (1.0f - progress));
    if (alpha < 0) alpha = 0;

    // Expanding orange ring
    float cx = explosions[i].x;
    float cy = explosions[i].y;
    int segments = 24;
    for (int s = 0; s < segments; s++) {
      float a1 = (float)s / segments * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / segments * 2.0f * (float)PI;
      int x1 = (int)(cx + cosf(a1) * r);
      int y1 = (int)(cy + sinf(a1) * r);
      int x2 = (int)(cx + cosf(a2) * r);
      int y2 = (int)(cy + sinf(a2) * r);
      a_DrawLine(x1, y1, x2, y2, (aColor_t){255, 160, 30, alpha});
    }

    // Inner flash (fades faster)
    int inner_alpha = (int)(120.0f * (1.0f - progress * 1.5f));
    if (inner_alpha > 0) {
      float ir = r * 0.6f;
      a_DrawFilledRect(
        (aRectf_t){cx - ir, cy - ir, ir * 2.0f, ir * 2.0f},
        (aColor_t){255, 200, 50, inner_alpha}
      );
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
      if (enemies[i].type == ENEMY_TYPE_BEHOLDER) continue;
      if (enemies[i].type == ENEMY_TYPE_MIMIC) continue;
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
  return ei_get_stats(&enemies[enemy_index])->radius;
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

int enemy_get_last_hit_source(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return WEAPON_NONE;
  return enemies[enemy_index].last_hit_source;
}

float enemy_get_spawn_time(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return 0.0f;
  return enemies[enemy_index].spawn_time;
}

int enemy_get_shaman_count(void)
{
  return shaman_get_count();
}

int enemy_get_remaining_hp(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return 0;
  Enemy_t* e = &enemies[enemy_index];
  if (!e->active || e->state == ENEMY_STATE_CORPSE) return 0;
  int total = ei_get_total_hp(e);
  int remaining = total - e->hit_count;
  return remaining > 0 ? remaining : 0;
}

void enemy_consume_corpse(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  Enemy_t* e = &enemies[enemy_index];
  if (e->active && e->state == ENEMY_STATE_CORPSE) {
    e->corpse_consumed = 1;
  }
}

// ============================================================================
// Upgrade Debuff Setters/Getters
// ============================================================================

void enemy_set_conductor(int enemy_index, float duration, int jumps)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  if (!enemies[enemy_index].active) return;
  Enemy_t* e = &enemies[enemy_index];
  // Only set timer if not already conducting — don't reset the countdown
  if (e->conductor_timer <= 0.0f) {
    e->conductor_timer = duration;
  }
  if (jumps > e->conductor_jumps) e->conductor_jumps = jumps;
}

int enemy_is_conductor(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return 0;
  return enemies[enemy_index].conductor_timer > 0.0f;
}

void enemy_set_stun(int enemy_index, float duration, float damage_mult)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  if (!enemies[enemy_index].active) return;
  Enemy_t* e = &enemies[enemy_index];
  if (e->state == ENEMY_STATE_CORPSE) return;
  e->stun_timer = duration;
  e->stun_damage_mult = damage_mult;
}

int enemy_brute_slow_aura_check(float x, float y)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].state == ENEMY_STATE_CORPSE) continue;
    if (enemies[i].type != ENEMY_TYPE_BRUTE) continue;
    if (!enemies[i].brute_buffs[PICKUP_SLOW_AURA].active) continue;

    float pct = enemies[i].slow_aura_elapsed / BRUTE_SLOW_AURA_DURATION;
    float aura_r = BRUTE_SLOW_AURA_MIN_R + (BRUTE_SLOW_AURA_MAX_R - BRUTE_SLOW_AURA_MIN_R) * pct;

    float sz = ei_get_stats(&enemies[i])->size;
    float bcx = enemies[i].x + sz / 2.0f;
    float bcy = enemies[i].y + sz / 2.0f;
    float dx = x - bcx;
    float dy = y - bcy;
    if (sqrtf(dx * dx + dy * dy) <= aura_r) {
      return 1;
    }
  }
  return 0;
}

void enemy_displace(int enemy_index, float dx, float dy)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  if (!enemies[enemy_index].active) return;
  enemies[enemy_index].x += dx;
  enemies[enemy_index].y += dy;
  // Brief stun so aggressive AI doesn't instantly undo the pull
  if ((enemies[enemy_index].type == ENEMY_TYPE_BEHOLDER ||
       enemies[enemy_index].type == ENEMY_TYPE_MIMIC) &&
      enemies[enemy_index].stun_timer < 0.15f) {
    enemies[enemy_index].stun_timer = 0.15f;
  }
}

int enemy_find_cluster_target(float radius, float player_x, float player_y)
{
  int best = -1;
  int best_neighbors = -1;
  float best_dist = 9999.0f;

  for (int i = 0; i < max_enemies; i++) {
    if (!enemy_is_alive(i)) continue;

    float er_i = ei_get_stats(&enemies[i])->radius;

    int neighbors = 0;
    for (int j = 0; j < max_enemies; j++) {
      if (j == i || !enemy_is_alive(j)) continue;
      float er_j = ei_get_stats(&enemies[j])->radius;
      float d = ei_dist_between(
        enemies[i].x + er_i, enemies[i].y + er_i,
        enemies[j].x + er_j, enemies[j].y + er_j
      );
      if (d <= radius) neighbors++;
    }

    float d_player = ei_dist_between(
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
  float er = ei_get_stats(&enemies[idx])->radius;
  *out_x = enemies[idx].x + er + enemies[idx].vx * lead_time;
  *out_y = enemies[idx].y + er + enemies[idx].vy * lead_time;
  return 1;
}

// ============================================================================
// Explosion Visual Effects
// ============================================================================

void enemy_stop_beholder_sounds(void)
{
  beholder_stop_sounds();
}

void enemy_spawn_explosion(float x, float y, float radius)
{
  for (int i = 0; i < MAX_EXPLOSIONS; i++) {
    if (!explosions[i].active) {
      explosions[i].x = x;
      explosions[i].y = y;
      explosions[i].radius = radius;
      explosions[i].timer = EXPLOSION_DURATION;
      explosions[i].active = 1;
      return;
    }
  }
}

void enemy_aoe_knockback_stun(float cx, float cy, float radius,
                              float knockback_dist, float stun_duration)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].state == ENEMY_STATE_CORPSE) continue;

    Enemy_t* e = &enemies[i];
    const EnemyStats_t* stats = ei_get_stats(e);
    float ex = e->x + stats->size / 2.0f;
    float ey = e->y + stats->size / 2.0f;
    float dx = ex - cx;
    float dy = ey - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > radius) continue;

    // Knockback away from center (bypasses immunity)
    if (knockback_dist > 0.0f && dist > 0.1f) {
      float nx = dx / dist;
      float ny = dy / dist;
      ei_start_knockback(e, nx, ny, knockback_dist);
    }

    // Stun (bypasses immunity — applied directly)
    if (stun_duration > 0.0f && e->stun_timer < stun_duration) {
      e->stun_timer = stun_duration;
      e->stun_damage_mult = 1.0f;
      // Cancel beholder beam/charge
      if (e->type == ENEMY_TYPE_BEHOLDER) {
        beholder_on_knockback(e);
      }
    }
  }
}

void enemy_hit_nearest(float x, float y)
{
  int best = -1;
  float best_dist = 1e9f;
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].state == ENEMY_STATE_CORPSE) continue;

    Enemy_t* e = &enemies[i];
    const EnemyStats_t* stats = ei_get_stats(e);
    float ex = e->x + stats->size / 2.0f;
    float ey = e->y + stats->size / 2.0f;
    float dx = ex - x;
    float dy = ey - y;
    float dist = dx * dx + dy * dy;
    if (dist < best_dist) {
      best_dist = dist;
      best = i;
    }
  }
  if (best >= 0)
    enemy_hit_no_knockback(best);
}
