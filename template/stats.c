#include <stdio.h>
#include <string.h>
#include "stats.h"

#define SAVE_FILE "highscore.dat"

// Points per kill
static const int kill_points[ENEMY_TYPE_COUNT] = {
  [ENEMY_TYPE_GRUNT]  = 10,
  [ENEMY_TYPE_DASHER] = 25,
  [ENEMY_TYPE_BRUTE]  = 50,
};

// Current run
static int score = 0;
static int total_kills = 0;
static int kills_by_type[ENEMY_TYPE_COUNT];

// Persistent best
static int  best_score = 0;
static float best_time = 0.0f;

static void load_best(void)
{
  FILE* f = fopen(SAVE_FILE, "r");
  if (!f) return;
  if (fscanf(f, "%d %f", &best_score, &best_time) != 2) {
    best_score = 0;
    best_time = 0.0f;
  }
  fclose(f);
}

static void save_best(void)
{
  FILE* f = fopen(SAVE_FILE, "w");
  if (!f) return;
  fprintf(f, "%d %f\n", best_score, best_time);
  fclose(f);
}

void stats_init(void)
{
  load_best();
  stats_reset();
}

void stats_reset(void)
{
  score = 0;
  total_kills = 0;
  memset(kills_by_type, 0, sizeof(kills_by_type));
}

void stats_record_kill(EnemyType_t type)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return;
  kills_by_type[type]++;
  total_kills++;
  score += kill_points[type];
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
  if (changed) save_best();
}
