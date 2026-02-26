#include "enemy.h"
#include "drops.h"
#include "weapons.h"
#include "player_actions.h"
#include "pickups.h"
#include "game_audio.h"
#include "blood.h"
#include "progress.h"
#include "snake.h"
#include "stats.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Beholder AI
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

// Shaman AI
#define SHAMAN_FLEE_RADIUS        120.0f
#define SHAMAN_SAFE_DISTANCE      160.0f
#define SHAMAN_CORPSE_SCAN_RADIUS 400.0f
#define SHAMAN_CORPSE_SPEED_MULT  1.8f    // Sprint toward corpses
#define SHAMAN_HEAL_RANGE_MIN     75.0f   // Heal range for slightly damaged ally
#define SHAMAN_HEAL_RANGE_MAX     187.0f  // Heal range for nearly-dead ally
#define SHAMAN_HEAL_SCAN_MIN      200.0f  // Scan radius for slightly damaged ally
#define SHAMAN_HEAL_SCAN_MAX      500.0f  // Scan radius for nearly-dead ally
#define SHAMAN_HEAL_COOLDOWN      5.5f
#define SHAMAN_CHANNEL_TIME       0.75f
#define SHAMAN_EAT_TIME           0.5f
#define SHAMAN_EAT_DIST           15.0f
#define SHAMAN_ORBIT_CHANGE_MIN   3.0f
#define SHAMAN_ORBIT_CHANGE_MAX   5.0f
#define SHAMAN_ADVANCE_SPEED_MIN  1.3f    // Speed mult for slightly damaged ally
#define SHAMAN_ADVANCE_SPEED_MAX  2.5f    // Speed mult for nearly-dead ally
#define SHAMAN_FLEE_OVERRIDE_PCT  0.6f    // Ally damage % above which shaman ignores flee

// Forward declarations
static float get_brute_fire_cone_range(Enemy_t* e);

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
    .hits_to_kill = 8, .damage = 20,
    .attack_speed_mult = 0.0f,
    .attack_duration_min = 0.0f, .attack_duration_max = 0.0f,
    .reposition_duration_min = 0.0f, .reposition_duration_max = 0.0f,
    .flank_distance = 250.0f, .separation_radius = 150.0f,
    .skip_reposition = 1
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
    if (e->brute_buffs[PICKUP_SPEED].active) {
      speed *= BRUTE_SPEED_MULT;
    }

    // Meta-progression: diminishing returns on speed bonuses
    float effectiveness = progress_get_brute_speed_effectiveness();
    if (effectiveness < 1.0f) {
      float base_speed = get_stats(e)->speed * e->speed_mult;
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
  float radius = get_stats(e)->radius;
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
    float dist2 = dx * dx + dy * dy;

    // Use the larger of the two separation radii
    float other_sep_radius = get_stats(&enemies[j])->separation_radius;
    float sep_radius = (my_sep_radius > other_sep_radius) ? my_sep_radius : other_sep_radius;

    if (dist2 < sep_radius * sep_radius && dist2 > 0.01f) {
      float dist = sqrtf(dist2);
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

  // Stop beholder sounds on kill
  if (e->type == ENEMY_TYPE_BEHOLDER) {
    game_audio_stop_beholder_windup(e->bh_windup_channel);
    e->bh_windup_channel = -1;
    game_audio_stop_beholder_beam();
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
        .conductor_timer = 0.0f,
        .conductor_jumps = 0,
        .stun_timer = 0.0f,
        .stun_damage_mult = 1.0f,
        // Corpse tracking (all types)
        .base_hits_to_kill = enemy_stats[type].hits_to_kill,
        .corpse_consumed = 0,
        // Shaman fields
        .stored_heal_value = 0,
        .heal_target = -1,
        .heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN,
        .target_corpse = -1,
        .orbit_angle = RANDF(0, 2.0f * (float)PI),
        .orbit_dir_timer = RANDF(SHAMAN_ORBIT_CHANGE_MIN, SHAMAN_ORBIT_CHANGE_MAX),
        .orbit_direction = (rand() % 2) ? 1 : -1,
        .heal_flash_timer = 0.0f,
        .last_hit_source = WEAPON_NONE,
      };
      return i;
    }
  }
  return -1;
}

// ============================================================================
// Beholder Spawn + Helpers
// ============================================================================

static void beholder_start_flee(Enemy_t* e)
{
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float px = player_get_x() + 16.0f;
  float py = player_get_y() + 16.0f;
  float dx = cx - px;
  float dy = cy - py;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > 0.1f) {
    e->bh_flee_vx = (dx / len) * BEHOLDER_FLEE_SPEED;
    e->bh_flee_vy = (dy / len) * BEHOLDER_FLEE_SPEED;
  } else {
    e->bh_flee_vx = BEHOLDER_FLEE_SPEED;
    e->bh_flee_vy = 0.0f;
  }
  e->state = ENEMY_STATE_BH_FLEEING;
  e->bh_state_timer = BEHOLDER_FLEE_TIME;
  // Cut any active beholder sounds
  game_audio_stop_beholder_windup(e->bh_windup_channel);
  e->bh_windup_channel = -1;
  game_audio_stop_beholder_beam();
}

int beholder_spawn(int hp, int shield)
{
  // Pick a random screen edge spawn position
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
  float er = get_stats(e)->radius;
  if (weapons_is_in_crater(e->x + er, e->y + er, &crater_dmg) && crater_dmg > 1.0f) {
    hit_increment++;  // +1 hit for crater damage amp
  }
  e->hit_count += hit_increment;
  e->last_hit_source = weapons_get_damage_source();

  // Track weapon damage
  for (int h = 0; h < hit_increment; h++) weapons_record_hit();

  float knockback_dist = KNOCKBACK_STRENGTH;
  int killed = (e->hit_count >= get_total_hp(e));

  // Beholder: custom sounds for shield, health, and death — no knockback
  if (e->type == ENEMY_TYPE_BEHOLDER) {
    if (!killed && e->bh_shield > 0) {
      e->hit_count--;  // absorb hit
      e->bh_shield--;
      if (e->bh_shield <= 0) {
        game_audio_play_beholder_shield_break();
        if (e->state == ENEMY_STATE_BH_FIRING) {
          e->bh_shield_broke_during_fire = 1;
        } else {
          beholder_start_flee(e);
        }
      } else {
        game_audio_play_beholder_shield_hit();
      }
      return;
    }
    if (killed) {
      e->vx = bullet_vx;
      e->vy = bullet_vy;
      game_audio_play_beholder_death();
      start_knockback(e, bullet_vx, bullet_vy,
        knockback_dist * (KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE)));
    } else {
      game_audio_play_beholder_health_hit();
    }
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
  if (e->type == ENEMY_TYPE_BRUTE && !killed) {
    if (e->brute_buffs[PICKUP_SHIELD].active && e->brute_buffs[PICKUP_SHIELD].shield_hits > 0) {
      e->hit_count--; // Undo the hit_count increment
      e->brute_buffs[PICKUP_SHIELD].shield_hits--;
      if (e->brute_buffs[PICKUP_SHIELD].shield_hits <= 0) {
        e->brute_buffs[PICKUP_SHIELD].active = 0;
      }
      return; // Shield absorbed, no knockback
    }

    // Re-evaluate priorities on every hit (interrupt current action)
    brute_evaluate_priorities(e);
    return;  // No knockback for brutes
  }

  start_knockback(e, bullet_vx, bullet_vy, knockback_dist);
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

  if (e->hit_count >= get_total_hp(e)) {
    // Kill — do full knockback death sequence
    float knockback_dist = KNOCKBACK_STRENGTH *
      (KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE));
    e->vx = 0.0f;
    e->vy = 0.0f;
    game_audio_play_die();
    start_knockback(e, 0.0f, 0.0f, knockback_dist);
  }
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
      for (int b = 0; b < PICKUP_TYPE_COUNT; b++) e->brute_buffs[b].active = 0; // Clear all buffs on death
      e->target_corpse = -1;    // Release any claimed corpse
      e->heal_target = -1;
      if (e->type == ENEMY_TYPE_SHAMAN) {
        blood_spawn_short(e->x, e->y, e->vx, e->vy, get_stats(e)->radius, 2.0f);
        e->corpse_consumed = 1;  // Skip corpse lingering
      } else {
        blood_spawn(e->x, e->y, e->vx, e->vy, get_stats(e)->radius);
      }

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
    } else if (e->aggro && (e->type == ENEMY_TYPE_DASHER || !player_is_invincible())) {
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

  float cx = e->x + radius;
  float cy = e->y + radius;
  float dist = dist_between(cx, cy, player_x, player_y);

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
    calc_separation(index, &sep_x, &sep_y, 0.5f);
    move_toward(e, target_x, target_y, fire_speed, sep_x, sep_y, SEPARATION_WEIGHT * 0.5f, dt);
    resolve_player_collision(e, player_x, player_y);
    if (is_offscreen(e->x, e->y)) e->active = 0;
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
    normalize(&dx, &dy);
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
  const EnemyStats_t* stats = get_stats(e);
  float speed = get_effective_speed(e) * stats->attack_speed_mult;

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
    calc_separation(index, &sep_x, &sep_y, ATTACK_SEPARATION_SCALE);
    move_toward(e, player_x, player_y, speed,
                sep_x, sep_y, ATTACK_SEPARATION_WEIGHT, dt);
  }

  if (resolve_player_collision(e, player_x, player_y)) {
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

  if (is_offscreen(e->x, e->y)) {
    if (e->state == ENEMY_STATE_ATTACKING && current_attackers > 0) current_attackers--;
    e->active = 0;
  }
}

static void update_retreat(Enemy_t* e, float dt)
{
  float speed = get_effective_speed(e) * RETREAT_SPEED_MULT;
  // Cap brute retreat speed so buffs don't send them across the map
  if (e->type == ENEMY_TYPE_BRUTE && speed > 150.0f) speed = 150.0f;

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

  resolve_player_collision(e, player_x, player_y);
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

      float radius = get_stats(e)->radius;
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
// Shaman Helpers
// ============================================================================

// Find nearest unconsumed corpse within radius that's safe from the player
// Check if any shaman is targeting this corpse (seeking or eating)
static int corpse_is_claimed(int corpse_index)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_SHAMAN) continue;
    if (enemies[i].target_corpse != corpse_index) continue;
    if (enemies[i].state == ENEMY_STATE_SEEKING_CORPSE ||
        enemies[i].state == ENEMY_STATE_EATING) {
      return 1;
    }
  }
  return 0;
}

// Decode a snake target from heal_target: -2 = snake 0, -3 = snake 1, etc.
static int decode_snake_target(int heal_target)
{
  if (heal_target <= -2) return -(heal_target + 2);
  return -1;
}

// Check if another shaman is already healing/seeking this ally
static int ally_is_heal_targeted(int ally_index, int ignore_shaman)
{
  for (int i = 0; i < max_enemies; i++) {
    if (i == ignore_shaman) continue;
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_SHAMAN) continue;
    if (enemies[i].heal_target != ally_index) continue;
    if (enemies[i].state == ENEMY_STATE_SEEKING_ALLY ||
        enemies[i].state == ENEMY_STATE_HEALING) {
      return 1;
    }
  }
  return 0;
}

static int shaman_find_corpse(float x, float y, float radius,
                              float player_x, float player_y, float safe_dist)
{
  float best_dist = radius + 1.0f;
  int best = -1;
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].state != ENEMY_STATE_CORPSE) continue;
    if (enemies[i].corpse_consumed) continue;
    // Don't eat shaman corpses (no value)
    if (enemies[i].type == ENEMY_TYPE_SHAMAN) continue;
    // Skip corpses already claimed by another shaman
    if (corpse_is_claimed(i)) continue;

    float cx = enemies[i].x + get_stats(&enemies[i])->radius;
    float cy = enemies[i].y + get_stats(&enemies[i])->radius;

    // Must be safe distance from player
    float dp = dist_between(cx, cy, player_x, player_y);
    if (dp < safe_dist) continue;

    // Must not be near an active turret
    if (weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) continue;

    float d = dist_between(x, y, cx, cy);
    if (d <= radius && d < best_dist) {
      best_dist = d;
      best = i;
    }
  }
  return best;
}

// Find the ally (not self, not shaman, not corpse) with the lowest HP ratio that isn't full
// Find most damaged ally — scan radius scales with how hurt they are.
// Each ally's effective scan range = lerp(SCAN_MIN, SCAN_MAX, damage_pct).
// Returns index and writes urgency (0-1 damage ratio) to out_urgency.
static int shaman_find_lowest_hp_ally(float x, float y, int self_index, float* out_urgency)
{
  float best_ratio = 0.0f;
  int best = -1;
  float best_dist = 9999.0f;

  for (int i = 0; i < max_enemies; i++) {
    if (i == self_index) continue;
    if (!enemies[i].active) continue;
    if (enemies[i].type == ENEMY_TYPE_SHAMAN) continue;
    if (!enemy_is_alive(i)) continue;
    if (enemies[i].hit_count <= 0) continue; // Full HP
    // Skip allies already being healed by another shaman
    if (ally_is_heal_targeted(i, self_index)) continue;

    int total_hp = get_total_hp(&enemies[i]);
    if (total_hp <= 0) continue;
    float ratio = (float)enemies[i].hit_count / (float)total_hp;
    if (ratio > 1.0f) ratio = 1.0f;

    // Scan radius scales with how badly hurt this ally is
    float scan_r = SHAMAN_HEAL_SCAN_MIN +
      (SHAMAN_HEAL_SCAN_MAX - SHAMAN_HEAL_SCAN_MIN) * ratio;

    float ecx = enemies[i].x + get_stats(&enemies[i])->radius;
    float ecy = enemies[i].y + get_stats(&enemies[i])->radius;
    float d = dist_between(x, y, ecx, ecy);
    if (d > scan_r) continue;

    // Prefer most damaged ally; break ties by closer distance
    if (ratio > best_ratio || (ratio == best_ratio && d < best_dist)) {
      best_ratio = ratio;
      best = i;
      best_dist = d;
    }
  }

  // Also scan active snakes for heal targets
  for (int si = 0; si < 4; si++) {  // SNAKE_MAX_ACTIVE = 4
    if (!snake_is_on_screen_active(si)) continue;
    float ratio = snake_get_damage_ratio(si);
    if (ratio <= 0.0f) continue;

    int encoded = -(si + 2);
    if (ally_is_heal_targeted(encoded, self_index)) continue;

    float scan_r = SHAMAN_HEAL_SCAN_MIN +
      (SHAMAN_HEAL_SCAN_MAX - SHAMAN_HEAL_SCAN_MIN) * ratio;

    float sx, sy;
    snake_get_tail_position(si, &sx, &sy);
    float d = dist_between(x, y, sx, sy);
    if (d > scan_r) continue;

    if (ratio > best_ratio || (ratio == best_ratio && d < best_dist)) {
      best_ratio = ratio;
      best = encoded;
      best_dist = d;
    }
  }

  if (out_urgency) *out_urgency = best_ratio;
  return best;
}

// ============================================================================
// Shared Steering Helpers (used by shaman + any future smart enemies)
// ============================================================================

// Returns 1 if (x,y) is inside any active hazard (linger zone or fire trail)
static int is_in_hazard(float x, float y)
{
  return weapons_is_in_linger_zone(x, y) || weapons_is_in_trail(x, y);
}

// Compute a repulsion vector pushing away from the nearest hazard within radius.
// Strength falls off linearly with distance. weight controls the max magnitude.
static void calc_hazard_avoidance(float cx, float cy, float radius, float weight,
                                  float* out_x, float* out_y)
{
  *out_x = 0;
  *out_y = 0;
  float hz_x, hz_y;
  if (weapons_get_nearest_hazard(cx, cy, radius, &hz_x, &hz_y)) {
    float dx = cx - hz_x, dy = cy - hz_y;
    float d = normalize(&dx, &dy);
    float urg = 1.0f - (d / radius);
    if (urg < 0) urg = 0;
    *out_x = dx * urg * weight;
    *out_y = dy * urg * weight;
  }
}

// Compute a repulsion vector pushing away from the nearest turret within radius.
// Strength falls off linearly. weight controls the max magnitude.
static void calc_turret_avoidance(float cx, float cy, float radius, float weight,
                                  float* out_x, float* out_y)
{
  *out_x = 0;
  *out_y = 0;
  float tx, ty;
  if (weapons_get_nearest_turret(cx, cy, radius, &tx, &ty)) {
    float dx = cx - tx, dy = cy - ty;
    float d = normalize(&dx, &dy);
    float urg = 1.0f - (d / radius);
    if (urg < 0) urg = 0;
    *out_x = dx * urg * weight;
    *out_y = dy * urg * weight;
  }
}

// Steer toward (tcx,tcy) while avoiding hazards. If the direct path hits a
// hazard, steer perpendicular (picks whichever side is clear). Writes to vx/vy.
static void steer_toward_avoiding_hazards(float cx, float cy, float tcx, float tcy,
                                          float speed, float dt,
                                          float* out_vx, float* out_vy)
{
  float dx = tcx - cx;
  float dy = tcy - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < 0.1f) return;

  float next_x = cx + (dx / dist) * speed * dt;
  float next_y = cy + (dy / dist) * speed * dt;

  if (is_in_hazard(next_x, next_y)) {
    // Steer perpendicular — try both sides, pick the clear one
    float perp_lx = -dy / dist, perp_ly = dx / dist;
    float perp_rx = dy / dist, perp_ry = -dx / dist;

    float try_lx = cx + perp_lx * speed * dt;
    float try_ly = cy + perp_ly * speed * dt;
    float try_rx = cx + perp_rx * speed * dt;
    float try_ry = cy + perp_ry * speed * dt;

    int left_clear = !is_in_hazard(try_lx, try_ly);
    int right_clear = !is_in_hazard(try_rx, try_ry);

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

// ============================================================================
// Shaman State Handlers
// ============================================================================

static void update_shaman_alive(Enemy_t* e, int index, float dt,
                                float player_x, float player_y)
{
  float speed = get_effective_speed(e);
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float dp = dist_between(cx, cy, player_x, player_y);

  // Priority 1: Check for heal target first — urgency can override flee
  float urgency = 0.0f;
  int ally = -1;
  if (e->heal_cooldown_timer <= 0.0f) {
    ally = shaman_find_lowest_hp_ally(cx, cy, index, &urgency);
  }

  // Flee if player is too close — UNLESS ally is critically hurt
  if (dp < SHAMAN_FLEE_RADIUS && (ally == -1 || urgency < SHAMAN_FLEE_OVERRIDE_PCT)) {
    e->state = ENEMY_STATE_FLEEING;
    return;
  }

  // Flee if near an active turret — UNLESS ally is critically hurt
  float turret_x, turret_y;
  if (weapons_get_nearest_turret(cx, cy, TURRET_RANGE, &turret_x, &turret_y)) {
    if (ally == -1 || urgency < SHAMAN_FLEE_OVERRIDE_PCT) {
      e->state = ENEMY_STATE_FLEEING;
      return;
    }
    // Urgency is high enough — proceed despite turret danger
  }

  // Priority 2: Heal the ally we found (>= 0 = enemy, <= -2 = snake)
  if (ally != -1) {
    e->heal_target = ally;
    e->state = ENEMY_STATE_SEEKING_ALLY;
    return;
  }

  // Priority 3: Seek corpse to eat (stockpile heal value)
  int corpse = shaman_find_corpse(cx, cy, SHAMAN_CORPSE_SCAN_RADIUS,
                                  player_x, player_y, SHAMAN_FLEE_RADIUS);
  if (corpse >= 0) {
    e->target_corpse = corpse;
    e->state = ENEMY_STATE_SEEKING_CORPSE;
    return;
  }

  // Default: Orbit at safe distance from player
  e->orbit_dir_timer -= dt;
  if (e->orbit_dir_timer <= 0.0f) {
    e->orbit_direction = -e->orbit_direction;
    e->orbit_dir_timer = RANDF(SHAMAN_ORBIT_CHANGE_MIN, SHAMAN_ORBIT_CHANGE_MAX);
  }

  // Maintain orbit distance
  float desired_dist = SHAMAN_SAFE_DISTANCE;
  float dx = cx - player_x;
  float dy = cy - player_y;
  float len = normalize(&dx, &dy);

  // Tangential orbit direction
  float tx = -dy * (float)e->orbit_direction;
  float ty = dx * (float)e->orbit_direction;

  // Radial correction: push outward if too close, inward if too far
  float radial_weight = (len - desired_dist) / desired_dist;
  if (radial_weight > 1.0f) radial_weight = 1.0f;
  if (radial_weight < -1.0f) radial_weight = -1.0f;

  float mx = tx - dx * radial_weight;
  float my = ty - dy * radial_weight;
  normalize(&mx, &my);

  // Turret avoidance: push away from nearby turrets
  float tav_x, tav_y;
  calc_turret_avoidance(cx, cy, TURRET_RANGE * 1.3f, 1.5f, &tav_x, &tav_y);

  // Hazard avoidance: push away from linger zones and trail
  float haz_x, haz_y;
  calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);

  // Separation from other shamans
  float sep_x, sep_y;
  calc_separation(index, &sep_x, &sep_y, 1.0f);

  e->vx = (mx * speed) + sep_x * SEPARATION_WEIGHT + tav_x * speed + haz_x * speed;
  e->vy = (my * speed) + sep_y * SEPARATION_WEIGHT + tav_y * speed + haz_y * speed;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  // Keep on screen
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - get_stats(e)->size) e->x = SCREEN_WIDTH - get_stats(e)->size;
  if (e->y > SCREEN_HEIGHT - get_stats(e)->size) e->y = SCREEN_HEIGHT - get_stats(e)->size;
}

static void update_shaman_fleeing(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  float speed = get_effective_speed(e);
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  // Run away from player
  float dx = cx - player_x;
  float dy = cy - player_y;
  float dp = normalize(&dx, &dy);

  // Also run away from nearest turret
  float tx, ty;
  float flee_x = dx, flee_y = dy;
  int near_turret = weapons_get_nearest_turret(cx, cy, TURRET_RANGE * 1.3f, &tx, &ty);
  if (near_turret) {
    float tdx = cx - tx, tdy = cy - ty;
    normalize(&tdx, &tdy);
    flee_x += tdx;
    flee_y += tdy;
    normalize(&flee_x, &flee_y);
  }

  // Also run away from nearest hazard (linger zones / trail)
  {
    float hx, hy;
    calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &hx, &hy);
    flee_x += hx;
    flee_y += hy;
    normalize(&flee_x, &flee_y);
  }

  e->vx = flee_x * speed;
  e->vy = flee_y * speed;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  // Slide along screen edges
  float sz = get_stats(e)->size;
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
  if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

  // Return to ALIVE when safe from both player and turrets
  int turret_safe = !weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL);
  if (dp > SHAMAN_SAFE_DISTANCE && turret_safe) {
    e->state = ENEMY_STATE_ALIVE;
  }
}

static void update_seeking_corpse(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  float speed = get_effective_speed(e) * SHAMAN_CORPSE_SPEED_MULT;
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  // Check flee from player or turret
  float dp = dist_between(cx, cy, player_x, player_y);
  if (dp < SHAMAN_FLEE_RADIUS ||
      weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_FLEEING;
    return;
  }

  // Validate corpse still exists
  int ci = e->target_corpse;
  if (ci < 0 || ci >= max_enemies || !enemies[ci].active ||
      enemies[ci].state != ENEMY_STATE_CORPSE || enemies[ci].corpse_consumed) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_ALIVE;
    return;
  }

  // Move toward corpse
  float tcx = enemies[ci].x + get_stats(&enemies[ci])->radius;
  float tcy = enemies[ci].y + get_stats(&enemies[ci])->radius;
  float dx = tcx - cx;
  float dy = tcy - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < SHAMAN_EAT_DIST) {
    // Close enough to eat
    e->eat_timer = SHAMAN_EAT_TIME;
    e->state = ENEMY_STATE_EATING;
    return;
  }

  // Hazard avoidance: push away from linger zones and trail
  float haz_x, haz_y;
  calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);

  if (dist > 0.1f) {
    e->vx = (dx / dist) * speed + haz_x * speed;
    e->vy = (dy / dist) * speed + haz_y * speed;
  }
  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

static void update_eating(Enemy_t* e, int index, float dt,
                          float player_x, float player_y)
{
  (void)index;
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  // Check flee (abort eating) from player or turret
  float dp = dist_between(cx, cy, player_x, player_y);
  if (dp < SHAMAN_FLEE_RADIUS ||
      weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_FLEEING;
    return;
  }

  // Validate corpse still exists
  int ci = e->target_corpse;
  if (ci < 0 || ci >= max_enemies || !enemies[ci].active ||
      enemies[ci].state != ENEMY_STATE_CORPSE || enemies[ci].corpse_consumed) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_ALIVE;
    return;
  }

  // Stationary while eating
  e->vx = 0.0f;
  e->vy = 0.0f;

  e->eat_timer -= dt;
  if (e->eat_timer <= 0.0f) {
    // Consume the corpse: store its base HP as heal value
    int heal_value = enemies[ci].base_hits_to_kill;
    enemies[ci].corpse_consumed = 1;
    e->stored_heal_value = heal_value;
    e->target_corpse = -1;
    e->heal_cooldown_timer = 0.0f; // Ready to heal immediately
    e->state = ENEMY_STATE_ALIVE;
  }
}

static void update_seeking_ally(Enemy_t* e, int index, float dt,
                                float player_x, float player_y)
{
  (void)index;
  (void)player_x;
  (void)player_y;
  float speed = get_effective_speed(e);
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  int snake_idx = decode_snake_target(e->heal_target);
  float urgency, tcx, tcy;

  if (snake_idx >= 0) {
    // --- Snake target path ---
    if (!snake_is_on_screen_active(snake_idx) ||
        snake_get_damage_ratio(snake_idx) <= 0.0f) {
      e->heal_target = -1;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }
    urgency = snake_get_damage_ratio(snake_idx);
    snake_get_tail_position(snake_idx, &tcx, &tcy);
  } else {
    // --- Enemy target path ---
    int ti = e->heal_target;
    if (ti < 0 || ti >= max_enemies || !enemies[ti].active ||
        !enemy_is_alive(ti) || enemies[ti].hit_count <= 0) {
      e->heal_target = -1;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }
    int ally_total = get_total_hp(&enemies[ti]);
    urgency = (ally_total > 0) ? (float)enemies[ti].hit_count / (float)ally_total : 0.0f;
    if (urgency > 1.0f) urgency = 1.0f;
    tcx = enemies[ti].x + get_stats(&enemies[ti])->radius;
    tcy = enemies[ti].y + get_stats(&enemies[ti])->radius;
  }

  // Speed scales with urgency — nearly dead ally = sprint
  float advance_mult = SHAMAN_ADVANCE_SPEED_MIN +
    (SHAMAN_ADVANCE_SPEED_MAX - SHAMAN_ADVANCE_SPEED_MIN) * urgency;
  speed *= advance_mult;

  // Heal range scales with urgency — will get closer for critical allies
  float heal_range = SHAMAN_HEAL_RANGE_MIN +
    (SHAMAN_HEAL_RANGE_MAX - SHAMAN_HEAL_RANGE_MIN) * urgency;

  // Flee if turret is nearby — UNLESS ally urgency overrides
  if (weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) {
    if (urgency < SHAMAN_FLEE_OVERRIDE_PCT) {
      e->heal_target = -1;
      e->state = ENEMY_STATE_FLEEING;
      return;
    }
    // Critical ally — brave through turret zone
  }

  // Move toward target
  float dx = tcx - cx;
  float dy = tcy - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < heal_range) {
    e->heal_channel_timer = SHAMAN_CHANNEL_TIME;
    e->state = ENEMY_STATE_HEALING;
    return;
  }

  steer_toward_avoiding_hazards(cx, cy, tcx, tcy, speed, dt, &e->vx, &e->vy);
  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

static void update_healing(Enemy_t* e, int index, float dt,
                           float player_x, float player_y)
{
  (void)index;
  (void)player_x;
  (void)player_y;

  // Committed: no flee check during heal channel — finish or die

  int snake_idx = decode_snake_target(e->heal_target);

  if (snake_idx >= 0) {
    // --- Snake target path ---
    if (!snake_is_on_screen_active(snake_idx)) {
      e->heal_target = -1;
      e->stored_heal_value = 0;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }

    e->vx = 0.0f;
    e->vy = 0.0f;

    e->heal_channel_timer -= dt;
    if (e->heal_channel_timer <= 0.0f) {
      int amount = e->stored_heal_value > 0 ? e->stored_heal_value : 1;
      int reduction = progress_get_heal_reduction();
      amount -= reduction;
      if (amount < 1) amount = 1;

      snake_apply_heal(snake_idx, amount);
      stats_record_shaman_heal(amount);

      e->stored_heal_value = 0;
      e->heal_target = -1;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_FLEEING;
    }
  } else {
    // --- Enemy target path ---
    int ti = e->heal_target;
    if (ti < 0 || ti >= max_enemies || !enemies[ti].active ||
        enemies[ti].state == ENEMY_STATE_CORPSE) {
      e->heal_target = -1;
      e->stored_heal_value = 0;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }

    e->vx = 0.0f;
    e->vy = 0.0f;

    e->heal_channel_timer -= dt;
    if (e->heal_channel_timer <= 0.0f) {
      int amount = e->stored_heal_value > 0 ? e->stored_heal_value : 1;
      int reduction = progress_get_heal_reduction();
      amount -= reduction;
      if (amount < 1) amount = 1;
      int before = enemies[ti].hit_count;
      enemies[ti].hit_count -= amount;
      if (enemies[ti].hit_count < 0) enemies[ti].hit_count = 0;
      int actual_healed = before - enemies[ti].hit_count;

      enemies[ti].heal_flash_timer = 0.15f;
      stats_record_shaman_heal(actual_healed);

      e->stored_heal_value = 0;
      e->heal_target = -1;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_FLEEING;
    }
  }
}

// ============================================================================
// Beholder State Handlers
// ============================================================================

static void update_bh_entering(Enemy_t* e, int i, float dt,
                                float player_x, float player_y)
{
  (void)i;
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float dx = player_x - cx;
  float dy = player_y - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  // Move toward preferred range
  if (dist > BEHOLDER_PREFERRED_RANGE + 50.0f) {
    // Too far: move toward player
    if (dist > 0.1f) {
      e->vx = (dx / dist) * BEHOLDER_ENTER_SPEED;
      e->vy = (dy / dist) * BEHOLDER_ENTER_SPEED;
    }
  } else if (dist < BEHOLDER_PREFERRED_RANGE - 50.0f) {
    // Too close: back off
    if (dist > 0.1f) {
      e->vx = -(dx / dist) * BEHOLDER_ENTER_SPEED;
      e->vy = -(dy / dist) * BEHOLDER_ENTER_SPEED;
    }
  } else {
    // Within range, transition to strafing
    e->state = ENEMY_STATE_BH_STRAFING;
    e->bh_cooldown = BEHOLDER_COOLDOWN_1;
    return;
  }

  // Hazard and turret avoidance
  float haz_x, haz_y, tav_x, tav_y;
  calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);
  calc_turret_avoidance(cx, cy, TURRET_RANGE * 1.3f, 1.5f, &tav_x, &tav_y);
  e->vx += (haz_x + tav_x) * BEHOLDER_ENTER_SPEED;
  e->vy += (haz_y + tav_y) * BEHOLDER_ENTER_SPEED;

  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

static void update_bh_strafing(Enemy_t* e, int i, float dt,
                                float player_x, float player_y,
                                float player_vx, float player_vy)
{
  (void)player_vx;
  (void)player_vy;
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float dx = cx - player_x;
  float dy = cy - player_y;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
  float ndx = dx / dist;
  float ndy = dy / dist;

  // Spring force: approach if too far, retreat if too close
  float radial_x = 0, radial_y = 0;
  if (dist > BEHOLDER_PREFERRED_RANGE + 30.0f) {
    radial_x = -ndx * 0.5f;
    radial_y = -ndy * 0.5f;
  } else if (dist < BEHOLDER_PREFERRED_RANGE - 30.0f) {
    radial_x = ndx * 0.8f;
    radial_y = ndy * 0.8f;
  }

  // Strafe perpendicular (orbit)
  float perp_x = -ndy * (float)e->bh_orbit_dir;
  float perp_y = ndx * (float)e->bh_orbit_dir;

  float steer_x = perp_x + radial_x;
  float steer_y = perp_y + radial_y;

  // Repel from other beholders
  for (int j = 0; j < max_enemies; j++) {
    if (j == i || !enemies[j].active) continue;
    if (enemies[j].type != ENEMY_TYPE_BEHOLDER) continue;
    if (enemies[j].state == ENEMY_STATE_CORPSE) continue;
    float or = get_stats(&enemies[j])->radius;
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

  // Hazard and turret avoidance
  float haz_x, haz_y, tav_x, tav_y;
  calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);
  calc_turret_avoidance(cx, cy, TURRET_RANGE * 1.3f, 1.5f, &tav_x, &tav_y);
  steer_x += haz_x + tav_x;
  steer_y += haz_y + tav_y;

  // Normalize and apply
  float slen = sqrtf(steer_x * steer_x + steer_y * steer_y);
  if (slen > 0.1f) {
    e->vx = (steer_x / slen) * BEHOLDER_STRAFE_SPEED;
    e->vy = (steer_y / slen) * BEHOLDER_STRAFE_SPEED;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  e->x += e->vx * dt;
  e->y += e->vy * dt;

  // Clamp to screen
  float sz = get_stats(e)->size;
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
  if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

  // Tick cooldown
  e->bh_cooldown -= dt;
  if (e->bh_cooldown <= 0.0f) {
    float telegraph = BEHOLDER_TELEGRAPH_TIME;
    e->bh_state_timer = telegraph;
    e->state = ENEMY_STATE_BH_AIMING;
    e->vx = 0.0f;
    e->vy = 0.0f;
    e->bh_windup_channel = game_audio_play_beholder_windup();

    // Random side offset — consistent once chosen, beam sweeps toward player from here
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
  // Slow drift toward preferred range during aim (not frozen)
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float adx = cx - player_x;
  float ady = cy - player_y;
  float adist = sqrtf(adx * adx + ady * ady);
  if (adist > 0.1f && fabsf(adist - BEHOLDER_PREFERRED_RANGE) > 20.0f) {
    float drift = (adist > BEHOLDER_PREFERRED_RANGE) ? -20.0f : 20.0f;
    e->vx = (adx / adist) * drift;
    e->vy = (ady / adist) * drift;
    e->x += e->vx * dt;
    e->y += e->vy * dt;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  // Charge tween: 0 → 1 over telegraph duration
  float telegraph = BEHOLDER_TELEGRAPH_TIME;
  e->bh_charge_progress = 1.0f - (e->bh_state_timer / telegraph);
  if (e->bh_charge_progress < 0.0f) e->bh_charge_progress = 0.0f;
  if (e->bh_charge_progress > 1.0f) e->bh_charge_progress = 1.0f;

  // Track toward player + fixed offset at constant aim speed
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
    // Beam fires exactly where telegraph was pointing
    e->bh_beam_start_angle = e->bh_predicted_angle;
    e->bh_beam_angle = e->bh_predicted_angle;
    e->bh_beam_swept = 0.0f;
    e->bh_beam_has_hit = 0;
    e->bh_trail_count = 0;
    e->bh_charge_progress = 0.0f;
    e->bh_state_timer = BEHOLDER_BEAM_TIME;
    e->state = ENEMY_STATE_BH_FIRING;
    // Stop windup, start beam sound
    game_audio_stop_beholder_windup(e->bh_windup_channel);
    e->bh_windup_channel = -1;
    game_audio_play_beholder_beam();
  }
}

static void update_bh_firing(Enemy_t* e, int i, float dt,
                              float player_x, float player_y,
                              float player_vx, float player_vy)
{
  float radius = get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  // Strafe while firing (slower, semi-random orbit)
  {
    float fdx = cx - player_x;
    float fdy = cy - player_y;
    float fdist = sqrtf(fdx * fdx + fdy * fdy);
    if (fdist < 0.1f) { fdx = 1.0f; fdy = 0.0f; fdist = 1.0f; }
    float fndx = fdx / fdist;
    float fndy = fdy / fdist;
    // Orbit perpendicular
    float fperp_x = -fndy * (float)e->bh_orbit_dir;
    float fperp_y = fndx * (float)e->bh_orbit_dir;
    // Mild range maintenance
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
    e->x += e->vx * dt;
    e->y += e->vy * dt;
    // Clamp to screen
    float sz = get_stats(e)->size;
    if (e->x < 0) e->x = 0;
    if (e->y < 0) e->y = 0;
    if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
    if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;
    // Recompute center after movement
    cx = e->x + radius;
    cy = e->y + radius;
  }

  // Fixed-direction sweep: beam sweeps from offset side toward and past player
  (void)player_vx;
  (void)player_vy;
  // Sweep direction is opposite of offset (toward player)
  float sweep_dir = (e->bh_aim_offset > 0) ? -1.0f : 1.0f;
  float sweep_rate = BEHOLDER_SWEEP_RATE - progress_get_beholder_aim_slow();
  if (sweep_rate < 0.25f) sweep_rate = 0.25f;
  float step = sweep_dir * sweep_rate * dt;

  // Clamp total sweep arc
  if (e->bh_beam_swept + fabsf(step) > BEHOLDER_SWEEP_ARC) {
    float remaining = BEHOLDER_SWEEP_ARC - e->bh_beam_swept;
    if (remaining < 0) remaining = 0;
    step = sweep_dir * remaining;
  }

  // Record trail before moving beam
  float old_angle = e->bh_beam_angle;

  e->bh_beam_swept += fabsf(step);
  e->bh_beam_angle += step;

  // Push current angle into trail ring buffer
  if (e->bh_trail_count < BEHOLDER_TRAIL_COUNT) {
    e->bh_trail_angles[e->bh_trail_count] = old_angle;
    e->bh_trail_alphas[e->bh_trail_count] = 1.0f;
    e->bh_trail_count++;
  } else {
    // Shift left
    for (int t = 0; t < BEHOLDER_TRAIL_COUNT - 1; t++) {
      e->bh_trail_angles[t] = e->bh_trail_angles[t + 1];
      e->bh_trail_alphas[t] = e->bh_trail_alphas[t + 1];
    }
    e->bh_trail_angles[BEHOLDER_TRAIL_COUNT - 1] = old_angle;
    e->bh_trail_alphas[BEHOLDER_TRAIL_COUNT - 1] = 1.0f;
  }
  // Fade trails
  for (int t = 0; t < e->bh_trail_count; t++) {
    e->bh_trail_alphas[t] -= dt * 4.0f;
    if (e->bh_trail_alphas[t] < 0.0f) e->bh_trail_alphas[t] = 0.0f;
  }

  // Beam collision: check perpendicular distance to player
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

  // Beam sound fade in/out
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

  // Tick timer
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

  e->vx = e->bh_flee_vx;
  e->vy = e->bh_flee_vy;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  // Clamp to screen bounds
  float sz = get_stats(e)->size;
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
    // Restore shield
    e->bh_shield = e->bh_starting_shield;
    e->bh_shield_broke_during_fire = 0;

    // Back to strafing with current ramp cooldown
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
// Corpse
// ============================================================================

static void update_corpse(Enemy_t* e, int index, float dt)
{
  if (e->corpse_consumed) {
    e->active = 0;
    return;
  }

  // Freeze timer if a shaman is coming to eat this corpse
  if (!corpse_is_claimed(index)) {
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
          update_shaman_alive(e, i, dt, player_x, player_y);
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
      case ENEMY_STATE_SEEKING_CORPSE: update_seeking_corpse(e, i, dt, player_x, player_y);     break;
      case ENEMY_STATE_EATING:         update_eating(e, i, dt, player_x, player_y);              break;
      case ENEMY_STATE_SEEKING_ALLY:   update_seeking_ally(e, i, dt, player_x, player_y);       break;
      case ENEMY_STATE_HEALING:        update_healing(e, i, dt, player_x, player_y);            break;
      case ENEMY_STATE_FLEEING:        update_shaman_fleeing(e, i, dt, player_x, player_y);     break;
      case ENEMY_STATE_BH_ENTERING:   update_bh_entering(e, i, dt, player_x, player_y);         break;
      case ENEMY_STATE_BH_STRAFING:   update_bh_strafing(e, i, dt, player_x, player_y, player_vx, player_vy); break;
      case ENEMY_STATE_BH_AIMING:     update_bh_aiming(e, i, dt, player_x, player_y, player_vx, player_vy);   break;
      case ENEMY_STATE_BH_FIRING:     update_bh_firing(e, i, dt, player_x, player_y, player_vx, player_vy);   break;
      case ENEMY_STATE_BH_FLEEING:    update_bh_fleeing(e, i, dt, player_x, player_y);          break;
      case ENEMY_STATE_BH_CHANNELING: update_bh_channeling(e, i, dt, player_x, player_y);       break;
      case ENEMY_STATE_CORPSE:        update_corpse(e, i, dt);                                   break;
    }

skip_state_machine:
    // Beholder contact damage — push away and hurt player on overlap
    if (e->type == ENEMY_TYPE_BEHOLDER && e->state != ENEMY_STATE_CORPSE) {
      if (resolve_player_collision(e, player_x, player_y)) {
        const EnemyStats_t* stats = get_stats(e);
        int dmg = stats->damage - progress_get_beholder_dmg_reduction();
        if (dmg < 1) dmg = 1;
        player_take_damage(dmg, e->x + stats->radius, e->y + stats->radius, ENEMY_TYPE_BEHOLDER);
      }
    }

    // Tick brute buff effects (fire cone damage, duration countdown)
    if (e->type == ENEMY_TYPE_BRUTE && e->state != ENEMY_STATE_CORPSE) {
      update_brute_buff(e, dt, player_x, player_y);
    }

    // Tick shaman heal cooldown (all states except HEALING, which resets on complete)
    if (e->type == ENEMY_TYPE_SHAMAN && e->state != ENEMY_STATE_HEALING && e->state != ENEMY_STATE_CORPSE) {
      if (e->heal_cooldown_timer > 0.0f) e->heal_cooldown_timer -= dt;
    }

    // Tick beholder cosmetic timers
    if (e->type == ENEMY_TYPE_BEHOLDER && e->state != ENEMY_STATE_CORPSE) {
      e->bh_tendril_phase += dt * 4.0f;
      e->bh_blink_timer -= dt;
      if (e->bh_blink_timer < 0.0f) {
        e->bh_blink_timer = RANDF(2.0f, 3.0f);
      }
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
    } else if (e->type == ENEMY_TYPE_SHAMAN) {
      // Cross/plus shape
      float sz = stats->size;
      float cx = e->x + sz / 2.0f;
      float cy = e->y + sz / 2.0f;
      float arm_w = sz * 0.3f;  // width of each arm
      float arm_l = sz / 2.0f;  // half-length of each arm
      // Vertical bar
      a_DrawFilledRect(
        (aRectf_t){cx - arm_w / 2.0f, cy - arm_l, arm_w, sz},
        color
      );
      // Horizontal bar
      a_DrawFilledRect(
        (aRectf_t){cx - arm_l, cy - arm_w / 2.0f, sz, arm_w},
        color
      );
    } else if (e->type == ENEMY_TYPE_BEHOLDER) {
      float sz = stats->size;
      float bcx = e->x + sz / 2.0f;
      float bcy = e->y + sz / 2.0f;
      float px = player_get_x() + 16.0f;
      float py = player_get_y() + 16.0f;

      // Tendrils: 4 at diagonal directions toward player (skip on corpse/knockback)
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
        float base_angle = aim_angle + ((float)t * 0.5f * (float)PI) + 0.785f; // 45° offset
        float wiggle = 0.087f * sinf(e->bh_tendril_phase + (float)t * 1.5f); // ~5 degrees
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

      // Shield glow — faint blue aura behind body when shield is active
      if (e->bh_shield > 0) {
        float pulse = 0.5f + 0.3f * sinf(e->bh_tendril_phase * 1.5f);
        int ga = (int)(pulse * 80.0f);
        if (ga > 80) ga = 80;
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        // Filled glow behind body
        a_DrawFilledCircle((int)bcx, (int)bcy, 20, (aColor_t){30, 100, 255, (uint8_t)ga});
        a_DrawFilledCircle((int)bcx, (int)bcy, 23, (aColor_t){30, 100, 255, (uint8_t)(ga / 3)});
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
      }

      // Body circle
      a_DrawFilledCircle((int)bcx, (int)bcy, 14, color);

      // Shield outline rings on top of body
      if (e->bh_shield > 0) {
        float pulse = 0.5f + 0.3f * sinf(e->bh_tendril_phase * 1.5f);
        int ga = (int)(pulse * 140.0f);
        if (ga > 140) ga = 140;
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        a_DrawCircle((int)bcx, (int)bcy, 15, (aColor_t){50, 150, 255, (uint8_t)ga});
        a_DrawCircle((int)bcx, (int)bcy, 17, (aColor_t){40, 120, 255, (uint8_t)(ga / 2)});
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);

        // Shield pips below body
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

      // Eye — compute pupil position (also used as beam origin)
      float pupil_off_x, pupil_off_y;
      int is_channeling = (e->state == ENEMY_STATE_BH_CHANNELING);
      int is_blinking = (e->bh_blink_timer < 0.15f);
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
        // Eye closed — horizontal line
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app.renderer, 50, 50, 60, 255);
        SDL_RenderDrawLine(app.renderer, (int)bcx - 6, (int)bcy, (int)bcx + 6, (int)bcy);
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
      } else {
        a_DrawFilledCircle((int)eye_x, (int)eye_y, 3,
                           (aColor_t){20, 20, 30, 255});
      }

      // Beam telegraph (BH_AIMING state) — grows in with charge tween
      if (e->state == ENEMY_STATE_BH_AIMING) {
        float tele_angle = e->bh_predicted_angle;
        float tele_dx = cosf(tele_angle);
        float tele_dy = sinf(tele_angle);

        float charge = e->bh_charge_progress;
        // Ease-in-out: smoothstep
        float t = charge * charge * (3.0f - 2.0f * charge);

        // Telegraph reach grows from 0 to full
        float max_reach = 1500.0f;
        float reach = max_reach * t;

        // Pulse intensity increases with charge
        float base_a = 0.15f + 0.55f * t;
        float pulse_a = base_a + 0.25f * t * sinf(e->bh_state_timer * (8.0f + 10.0f * t));
        int tele_alpha = (int)(pulse_a * 255.0f);
        if (tele_alpha < 30) tele_alpha = 30;
        if (tele_alpha > 220) tele_alpha = 220;

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

        // Outer glow lines (wide, faint) — grow in
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

        // Core dashed line with growing reach
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

        // Charging orb at the eye — grows from 0 to 5px radius
        if (t > 0.1f) {
          int orb_r = (int)(5.0f * t);
          if (orb_r < 1) orb_r = 1;
          int orb_a = (int)(180.0f * t);
          a_DrawFilledCircle((int)eye_x, (int)eye_y, orb_r,
            (aColor_t){255, 80, 60, (uint8_t)orb_a});
        }

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
      }

      // Active beam (BH_FIRING state) — with light trails + fade in/out
      if (e->state == ENEMY_STATE_BH_FIRING) {
        // Beam progress: 0→1 over BEHOLDER_BEAM_TIME
        float beam_prog = 1.0f - (e->bh_state_timer / BEHOLDER_BEAM_TIME);
        if (beam_prog < 0.0f) beam_prog = 0.0f;
        if (beam_prog > 1.0f) beam_prog = 1.0f;
        // Fade envelope: ramp up first 15%, full middle, ramp down last 20%
        float beam_fade = 1.0f;
        if (beam_prog < 0.15f)
          beam_fade = beam_prog / 0.15f;
        else if (beam_prog > 0.80f)
          beam_fade = (1.0f - beam_prog) / 0.20f;
        if (beam_fade < 0.0f) beam_fade = 0.0f;

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

        // Draw trail lines first (behind main beam)
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

        // Core beam (3 lines) — alpha fades in/out
        int core_a = (int)(255.0f * beam_fade);
        SDL_SetRenderDrawColor(app.renderer, 255, 40, 40, (uint8_t)core_a);
        for (int off = -1; off <= 1; off++) {
          int x1 = (int)(eye_x + perp_x * (float)off);
          int y1 = (int)(eye_y + perp_y * (float)off);
          int x2 = (int)(eye_x + beam_dx * 1500.0f + perp_x * (float)off);
          int y2 = (int)(eye_y + beam_dy * 1500.0f + perp_y * (float)off);
          SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
        }
        // Glow (outer 2 lines)
        int glow_a = (int)(128.0f * beam_fade);
        SDL_SetRenderDrawColor(app.renderer, 255, 100, 100, (uint8_t)glow_a);
        for (int off = -3; off <= 3; off += 6) {
          int x1 = (int)(eye_x + perp_x * (float)off);
          int y1 = (int)(eye_y + perp_y * (float)off);
          int x2 = (int)(eye_x + beam_dx * 1500.0f + perp_x * (float)off);
          int y2 = (int)(eye_y + beam_dy * 1500.0f + perp_y * (float)off);
          SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
        }
        // Bright core dot at pupil
        int dot_a = (int)(255.0f * beam_fade);
        a_DrawFilledCircle((int)eye_x, (int)eye_y, 4,
          (aColor_t){255, 200, 150, (uint8_t)dot_a});

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
      }

      // Channel visual: body pulses blue + spiraling sparks
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

        // Spiraling sparks
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


    // Stun visual: white flash
    if (e->stun_timer > 0.0f && e->state != ENEMY_STATE_CORPSE) {
      int flash = ((int)(e->stun_timer * 12.0f)) % 2;
      if (flash) {
        a_DrawFilledRect(
          (aRectf_t){e->x - 1, e->y - 1, stats->size + 2, stats->size + 2},
          (aColor_t){255, 255, 255, 120}
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

    // Shaman heal beam (drawn when channeling)
    if (e->type == ENEMY_TYPE_SHAMAN && e->state == ENEMY_STATE_HEALING &&
        e->heal_target != -1) {
      float sz = stats->size;
      float scx = e->x + sz / 2.0f;
      float scy = e->y + sz / 2.0f;
      float tcx = 0, tcy = 0;
      int beam_valid = 0;

      int beam_snake = decode_snake_target(e->heal_target);
      if (beam_snake >= 0) {
        if (snake_is_on_screen_active(beam_snake)) {
          snake_get_tail_position(beam_snake, &tcx, &tcy);
          beam_valid = 1;
        }
      } else if (e->heal_target >= 0 && e->heal_target < max_enemies &&
                 enemies[e->heal_target].active) {
        Enemy_t* target = &enemies[e->heal_target];
        float tsz = get_stats(target)->size;
        tcx = target->x + tsz / 2.0f;
        tcy = target->y + tsz / 2.0f;
        beam_valid = 1;
      }

      if (beam_valid) {
        // Pulsing green beam
        float pulse = 150.0f + 105.0f * sinf(e->heal_channel_timer * 8.0f * 2.0f * (float)PI);
        int beam_alpha = (int)pulse;
        if (beam_alpha > 255) beam_alpha = 255;
        if (beam_alpha < 100) beam_alpha = 100;

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app.renderer, 50, 255, 50, (uint8_t)beam_alpha);
        // Center beam + 2 parallel lines
        SDL_RenderDrawLine(app.renderer, (int)scx, (int)scy, (int)tcx, (int)tcy);
        float dx = tcx - scx;
        float dy = tcy - scy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.1f) {
          float px = -dy / len;
          float py = dx / len;
          SDL_RenderDrawLine(app.renderer, (int)(scx + px), (int)(scy + py), (int)(tcx + px), (int)(tcy + py));
          SDL_RenderDrawLine(app.renderer, (int)(scx - px), (int)(scy - py), (int)(tcx - px), (int)(tcy - py));
        }

        // Small green dots traveling along beam
        float progress = 1.0f - (e->heal_channel_timer / SHAMAN_CHANNEL_TIME);
        for (int d = 0; d < 3; d++) {
          float t = progress * 0.5f + (float)d * 0.2f;
          t = t - (int)t; // wrap to 0-1
          float dotx = scx + (tcx - scx) * t;
          float doty = scy + (tcy - scy) * t;
          a_DrawFilledRect(
            (aRectf_t){dotx - 2, doty - 2, 4, 4},
            (aColor_t){100, 255, 100, 220}
          );
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

    // Shaman eating visual: show particles around shaman
    if (e->type == ENEMY_TYPE_SHAMAN && e->state == ENEMY_STATE_EATING) {
      float sz = stats->size;
      float scx = e->x + sz / 2.0f;
      float scy = e->y + sz / 2.0f;
      float eat_progress = 1.0f - (e->eat_timer / SHAMAN_EAT_TIME);
      int num_sparks = 4;
      for (int s = 0; s < num_sparks; s++) {
        float angle = (float)s / num_sparks * 2.0f * (float)PI + eat_progress * 6.0f;
        float sr = 8.0f + 4.0f * eat_progress;
        float sx = scx + cosf(angle) * sr;
        float sy = scy + sinf(angle) * sr;
        a_DrawFilledRect(
          (aRectf_t){sx - 1.5f, sy - 1.5f, 3, 3},
          (aColor_t){50, 255, 50, 180}
        );
      }
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

int enemy_get_last_hit_source(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return WEAPON_NONE;
  return enemies[enemy_index].last_hit_source;
}

int enemy_get_shaman_count(void)
{
  int count = 0;
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_SHAMAN) continue;
    if (enemies[i].state == ENEMY_STATE_CORPSE) continue;
    count++;
  }
  return count;
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

    float sz = get_stats(&enemies[i])->size;
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

// ============================================================================
// Explosion Visual Effects
// ============================================================================

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
