#ifndef STATS_H
#define STATS_H

#include "enemy.h"

void stats_init(void);
void stats_reset(void);

void stats_record_kill(EnemyType_t type);

int stats_get_score(void);
int stats_get_kills(void);
int stats_get_kills_by_type(EnemyType_t type);

// High score persistence
int  stats_get_best_score(void);
float stats_get_best_time(void);
void stats_save_if_best(float elapsed_time);

#endif /* STATS_H */
