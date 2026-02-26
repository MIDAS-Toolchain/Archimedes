#include <string.h>
#include "stats.h"
#include "save_data.h"

// Points per kill
static const int kill_points[ENEMY_TYPE_COUNT] = {
  [ENEMY_TYPE_GRUNT]  = 10,
  [ENEMY_TYPE_DASHER] = 25,
  [ENEMY_TYPE_BRUTE]  = 50,
  [ENEMY_TYPE_SHAMAN] = 30,
  [ENEMY_TYPE_SNAKE]  = 200,
  [ENEMY_TYPE_BEHOLDER] = 300,
};

// Current run
static int score = 0;
static int total_kills = 0;
static int kills_by_type[ENEMY_TYPE_COUNT];

// Per-weapon kill tracking (current run)
#define WEAPON_ID_COUNT 7
static int weapon_kills[WEAPON_ID_COUNT];

// Map WeaponType_t → weapon id (0-6), or -1 for non-weapon sources
static int weapon_type_to_id(int wtype)
{
  switch (wtype) {
    case 1: return 0;  // WEAPON_WAND
    case 2: return 1;  // WEAPON_SPIN
    case 3: return 2;  // WEAPON_CHAIN
    case 4: return 3;  // WEAPON_ORBIT
    case 5: return 4;  // WEAPON_BOMB
    case 6: return 5;  // WEAPON_TURRET
    case 7: return 6;  // WEAPON_TRAIL
    default: return -1;
  }
}

// Persistent best
static int  best_score = 0;
static float best_time = 0.0f;
static int  best_kills = 0;
static int   total_runs = 0;
static float total_time_played = 0.0f;

// Persistent death counts
static int deaths_by_type[ENEMY_TYPE_COUNT];
static const char* death_keys[ENEMY_TYPE_COUNT] = {
  "deaths_grunt", "deaths_dasher", "deaths_brute", "deaths_shaman", "deaths_snake", "deaths_beholder"
};

// Persistent shaman heal total
static int shaman_total_healed = 0;

void stats_init(void)
{
  best_score = save_data_get_int("best_score", 0);
  best_time  = save_data_get_float("best_time", 0.0f);
  best_kills = save_data_get_int("best_kills", 0);
  total_runs = save_data_get_int("total_runs", 0);
  total_time_played = save_data_get_float("total_time", 0.0f);
  for (int i = 0; i < ENEMY_TYPE_COUNT; i++)
    deaths_by_type[i] = save_data_get_int(death_keys[i], 0);
  shaman_total_healed = save_data_get_int("shaman_healed", 0);
  stats_reset();
}

void stats_reset(void)
{
  score = 0;
  total_kills = 0;
  memset(kills_by_type, 0, sizeof(kills_by_type));
  memset(weapon_kills, 0, sizeof(weapon_kills));
}

void stats_record_kill(EnemyType_t type)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return;
  kills_by_type[type]++;
  total_kills++;
  score += kill_points[type];
}

void stats_add_score(int points)
{
  score += points;
}

int stats_get_score(void)
{
  return score;
}

int stats_get_kills(void)
{
  return total_kills;
}

int stats_get_kills_by_type(EnemyType_t type)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return 0;
  return kills_by_type[type];
}

int stats_get_best_score(void)
{
  return best_score;
}

float stats_get_best_time(void)
{
  return best_time;
}

int stats_get_best_kills(void)
{
  return best_kills;
}

void stats_save_if_best(float elapsed_time)
{
  int changed = 0;
  if (score > best_score) {
    best_score = score;
    changed = 1;
  }
  if (elapsed_time > best_time) {
    best_time = elapsed_time;
    changed = 1;
  }
  if (total_kills > best_kills) {
    best_kills = total_kills;
    changed = 1;
  }
  if (changed) {
    save_data_set_int("best_score", best_score);
    save_data_set_float("best_time", best_time);
    save_data_set_int("best_kills", best_kills);
    save_data_flush();
  }
}

int stats_get_total_runs(void)
{
  return total_runs;
}

void stats_increment_runs(void)
{
  total_runs++;
  save_data_set_int("total_runs", total_runs);
  save_data_flush();
}

float stats_get_total_time(void)
{
  return total_time_played;
}

void stats_add_run_time(float t)
{
  total_time_played += t;
  save_data_set_float("total_time", total_time_played);
  save_data_flush();
}

void stats_record_death(EnemyType_t killer)
{
  if (killer < 0 || killer >= ENEMY_TYPE_COUNT) return;
  deaths_by_type[killer]++;
  save_data_set_int(death_keys[killer], deaths_by_type[killer]);
  save_data_flush();
}

int stats_get_deaths_by_type(EnemyType_t type)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return 0;
  return deaths_by_type[type];
}

void stats_record_shaman_heal(int amount)
{
  shaman_total_healed += amount;
  save_data_set_int("shaman_healed", shaman_total_healed);
  save_data_flush();
}

int stats_get_shaman_total_healed(void)
{
  return shaman_total_healed;
}

void stats_record_weapon_kill(int weapon_type)
{
  int id = weapon_type_to_id(weapon_type);
  if (id >= 0 && id < WEAPON_ID_COUNT) weapon_kills[id]++;
}

int stats_get_weapon_kills_by_id(int weapon_id)
{
  if (weapon_id < 0 || weapon_id >= WEAPON_ID_COUNT) return 0;
  return weapon_kills[weapon_id];
}
