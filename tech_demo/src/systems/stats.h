#ifndef STATS_H
#define STATS_H

#include "enemy.h"

void stats_init(void);
void stats_reset(void);

void stats_record_kill(EnemyType_t type);
void stats_add_score(int points);

int stats_get_score(void);
int stats_get_kills(void);
int stats_get_kills_by_type(EnemyType_t type);

// High score persistence
int  stats_get_best_score(void);
float stats_get_best_time(void);
int  stats_get_best_kills(void);
void stats_save_if_best(float elapsed_time);

// Death tracking (persistent)
void stats_record_death(EnemyType_t killer);
int  stats_get_deaths_by_type(EnemyType_t type);

// Shaman heal tracking (persistent, actual HP healed)
void stats_record_shaman_heal(int amount);
int  stats_get_shaman_total_healed(void);
int  stats_get_shaman_max_single_heal(void);

// Per-run HP healed tracking
void stats_record_heal(int amount);
int  stats_get_run_hp_healed(void);

// Per-weapon kill tracking (for weapon meta-progression)
void stats_record_weapon_kill(int weapon_type);
int  stats_get_weapon_kills_by_id(int weapon_id);

// Per-run pickup collection tracking
void stats_record_pickup_collected(int pickup_type);
int  stats_get_pickup_collected(int pickup_type);

// Per-run shield hits lost tracking
void stats_record_shield_hit_lost(void);
int  stats_get_shield_hits_lost(void);

// Lifetime stats
int   stats_get_total_runs(void);
void  stats_increment_runs(void);
float stats_get_total_time(void);
void  stats_add_run_time(float t);

#endif /* STATS_H */
