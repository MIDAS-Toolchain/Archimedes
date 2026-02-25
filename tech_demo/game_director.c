#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Archimedes.h"
#include "game_director.h"
#include "enemy.h"
#include "weapons.h"
#include "drops.h"
#include "player_actions.h"
#include "snake.h"
#include "save_data.h"

// ============================================================================
// Difficulty Scaling
// ============================================================================

static float game_elapsed_timer = 0.0f;
static float spawn_timer = 0.0f;

static float get_spawn_interval(void)
{
  float t = game_elapsed_timer;
  if (t > 300.0f) t = 300.0f;
  return 2.0f - (1.6f * (t / 300.0f));  // 2.0s -> 0.4s over 5 minutes
}

static float get_speed_mult(void)
{
  float t = game_elapsed_timer;
  if (t > 600.0f) t = 600.0f;
  return 1.0f + 0.5f * (t / 600.0f);  // 1.0x -> 1.5x over 10 minutes
}

static int get_hp_bonus(void)
{
  float t = game_elapsed_timer;
  if (t > 900.0f) t = 900.0f;
  return (int)(5.0f * (t / 900.0f));  // 0 -> +5 extra hits over 15 minutes
}

static int get_max_active_enemies(void)
{
  float t = game_elapsed_timer;
  if (t > 300.0f) t = 300.0f;
  return 15 + (int)(35.0f * (t / 300.0f));  // 15 -> 50 over 5 minutes
}

// ============================================================================
// Enemy Type Selection (pity counters)
// ============================================================================

static int dasher_unlocked = 0;
static int brute_unlocked = 0;
static int shaman_unlocked = 0;
static int spawns_since_dasher = 0;
static int spawns_since_brute = 0;
static int spawns_since_shaman = 0;

static EnemyType_t pick_enemy_type(void)
{
  float t = game_elapsed_timer;

  if (!dasher_unlocked && t >= 30.0f) {
    dasher_unlocked = 1;
    spawns_since_dasher = 0;
    return ENEMY_TYPE_DASHER;
  }

  if (!shaman_unlocked && t >= 60.0f) {
    shaman_unlocked = 1;
    spawns_since_shaman = 0;
    return ENEMY_TYPE_SHAMAN;
  }

  if (!brute_unlocked && t >= 90.0f) {
    brute_unlocked = 1;
    spawns_since_brute = 0;
    return ENEMY_TYPE_BRUTE;
  }

  float dasher_chance = dasher_unlocked ? 0.08f * (float)spawns_since_dasher : 0.0f;
  float brute_chance  = brute_unlocked  ? 0.08f * (float)spawns_since_brute  : 0.0f;
  float shaman_chance = shaman_unlocked ? 0.06f * (float)spawns_since_shaman : 0.0f;
  if (dasher_chance > 0.7f) dasher_chance = 0.7f;
  if (brute_chance > 0.5f)  brute_chance = 0.5f;
  if (shaman_chance > 0.3f) shaman_chance = 0.3f;
  // Suppress shaman spawns if 2+ already alive
  if (enemy_get_shaman_count() >= 2) shaman_chance *= 0.1f;

  float total = 1.0f + dasher_chance + brute_chance + shaman_chance;
  float roll = RANDF(0, total);

  if (roll < brute_chance) {
    spawns_since_brute = 0;
    spawns_since_dasher++;
    spawns_since_shaman++;
    return ENEMY_TYPE_BRUTE;
  }
  if (roll < brute_chance + dasher_chance) {
    spawns_since_dasher = 0;
    spawns_since_brute++;
    spawns_since_shaman++;
    return ENEMY_TYPE_DASHER;
  }
  if (roll < brute_chance + dasher_chance + shaman_chance) {
    spawns_since_shaman = 0;
    spawns_since_dasher++;
    spawns_since_brute++;
    return ENEMY_TYPE_SHAMAN;
  }

  spawns_since_dasher++;
  spawns_since_brute++;
  spawns_since_shaman++;
  return ENEMY_TYPE_GRUNT;
}

// ============================================================================
// Timed Weapon Drops (data-driven)
// ============================================================================

typedef struct {
  float trigger_time;
  int dropped;
} TimedDrop_t;

#define TIMED_DROP_COUNT 3
static TimedDrop_t timed_drops[TIMED_DROP_COUNT] = {
  {20.0f, 0}, {40.0f, 0}, {60.0f, 0}
};

static void check_timed_weapon_drops(void)
{
  for (int i = 0; i < TIMED_DROP_COUNT; i++) {
    if (timed_drops[i].dropped) continue;
    if (game_elapsed_timer < timed_drops[i].trigger_time) continue;

    timed_drops[i].dropped = 1;

    WeaponType_t pool[7];
    int pool_count = 0;
    WeaponType_t all_weapons[] = {WEAPON_WAND, WEAPON_SPIN, WEAPON_CHAIN, WEAPON_ORBIT, WEAPON_BOMB, WEAPON_TURRET, WEAPON_TRAIL};
    for (int w = 0; w < 7; w++) {
      if (!weapons_has(all_weapons[w]) && !drops_has_type(all_weapons[w]))
        pool[pool_count++] = all_weapons[w];
    }

    if (pool_count > 0) {
      WeaponType_t chosen = pool[rand() % pool_count];
      float wx, wy;
      float px = player_get_x();
      float py = player_get_y();
      do {
        wx = RANDF(40, SCREEN_WIDTH - 40);
        wy = RANDF(40, SCREEN_HEIGHT - 40);
      } while (sqrtf((wx - px) * (wx - px) + (wy - py) * (wy - py)) < 150.0f);
      drops_spawn(wx, wy, chosen);
    }
  }
}

// ============================================================================
// Health Drop Timer
// ============================================================================

#define HEALTH_DROP_INTERVAL 10.0f
static float health_drop_timer = 0.0f;

// Snake boss spawn schedule
static int snake_discovered_this_run = 0;
static int snake_spawn_count = 0;
#define SNAKE_FIRST_SPAWN_TIME  120.0f
#define SNAKE_SPAWN_INTERVAL_T  120.0f
static float next_snake_time = SNAKE_FIRST_SPAWN_TIME;

// Beholder spawn schedule
static float next_beholder_time = 2.0f;
static int beholder_spawn_count = 0;
static int beholder_discovered_this_run = 0;
#define BEHOLDER_FIRST_SPAWN_TIME 150.0f
#define BEHOLDER_SPAWN_INTERVAL   60.0f
#define BEHOLDER_DIR_BASE_SHIELD  3
#define BEHOLDER_DIR_SHIELD_GROWTH 2
#define BEHOLDER_DIR_BASE_HP      8

// ============================================================================
// Public API
// ============================================================================

void director_init(void)
{
  game_elapsed_timer = 0.0f;
  spawn_timer = 0.0f;
  health_drop_timer = 0.0f;
  dasher_unlocked = 0;
  brute_unlocked = 0;
  shaman_unlocked = 0;
  spawns_since_dasher = 0;
  spawns_since_brute = 0;
  spawns_since_shaman = 0;
  for (int i = 0; i < TIMED_DROP_COUNT; i++) {
    timed_drops[i].dropped = 0;
  }
  snake_spawn_count = 0;
  snake_discovered_this_run = 0;
  next_snake_time = SNAKE_FIRST_SPAWN_TIME;

  next_beholder_time = 2.0f;
  beholder_spawn_count = 0;
  beholder_discovered_this_run = 0;
}

void director_reset(void)
{
  director_init();
}

void director_update(float dt)
{
  game_elapsed_timer += dt;

  // Spawn enemies
  int current_enemy_count = enemy_get_count();
  int max_active = get_max_active_enemies();
  if (current_enemy_count < max_active) {
    spawn_timer += dt;
  }

  float current_spawn_interval = get_spawn_interval();
  if (spawn_timer >= current_spawn_interval && current_enemy_count < max_active) {
    spawn_timer = 0.0f;

    int side = rand() % 4;
    float spawn_x, spawn_y;

    if (side == 0) {
      spawn_x = RANDF(0, SCREEN_WIDTH);
      spawn_y = -16;
    } else if (side == 1) {
      spawn_x = SCREEN_WIDTH + 16;
      spawn_y = RANDF(0, SCREEN_HEIGHT);
    } else if (side == 2) {
      spawn_x = RANDF(0, SCREEN_WIDTH);
      spawn_y = SCREEN_HEIGHT + 16;
    } else {
      spawn_x = -16;
      spawn_y = RANDF(0, SCREEN_HEIGHT);
    }

    enemy_spawn(spawn_x, spawn_y, pick_enemy_type(), get_speed_mult(), get_hp_bonus());
  }

  // Snake boss spawn (every 120s starting at 120s)
  if (game_elapsed_timer >= next_snake_time) {
    if (!snake_discovered_this_run) {
      snake_discovered_this_run = 1;
      if (!save_data_get_int("snake_discovered", 0)) {
        save_data_set_int("snake_discovered", 1);
        save_data_flush();
      }
    }
    int n = snake_spawn_count + 1;
    int seg_count = 6 + 2 * (n - 1);
    int seg_hp = 3 + get_hp_bonus();
    snake_spawn(seg_count, seg_hp);
    snake_spawn_count++;
    next_snake_time += SNAKE_SPAWN_INTERVAL_T;
  }

  // Beholder spawn (every 60s starting at 150s)
  if (game_elapsed_timer >= next_beholder_time) {
    if (!beholder_discovered_this_run) {
      beholder_discovered_this_run = 1;
      if (!save_data_get_int("beholder_discovered", 0)) {
        save_data_set_int("beholder_discovered", 1);
        save_data_flush();
      }
    }
    int shield = BEHOLDER_DIR_BASE_SHIELD + BEHOLDER_DIR_SHIELD_GROWTH * beholder_spawn_count;
    int hp = BEHOLDER_DIR_BASE_HP + get_hp_bonus();
    beholder_spawn(hp, shield);
    beholder_spawn_count++;
    next_beholder_time += BEHOLDER_SPAWN_INTERVAL;
  }

  // Timed weapon drops
  check_timed_weapon_drops();

  // Health drops (skip at weapon drop times: 20s, 40s, 60s)
  health_drop_timer += dt;
  if (health_drop_timer >= HEALTH_DROP_INTERVAL) {
    health_drop_timer = 0.0f;

    int skip = 0;
    for (int i = 0; i < TIMED_DROP_COUNT; i++) {
      float diff = game_elapsed_timer - timed_drops[i].trigger_time;
      if (diff >= 0.0f && diff < 1.0f) { skip = 1; break; }
    }

    if (!skip) {
      float hx = RANDF(40, SCREEN_WIDTH - 40);
      float hy = RANDF(40, SCREEN_HEIGHT - 40);
      drops_spawn(hx, hy, DROP_HEALTH);
    }
  }
}

float director_get_elapsed(void)
{
  return game_elapsed_timer;
}

float director_get_spawn_interval(void)
{
  return get_spawn_interval();
}

float director_get_spawn_timer(void)
{
  return spawn_timer;
}

int director_get_max_active(void)
{
  return get_max_active_enemies();
}

float director_get_speed_mult(void)
{
  return get_speed_mult();
}

int director_get_hp_bonus(void)
{
  return get_hp_bonus();
}
