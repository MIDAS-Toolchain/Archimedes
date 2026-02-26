#ifndef MIMIC_H
#define MIMIC_H

#include "enemy.h"

// Spawn a mimic at a random screen edge
int  mimic_spawn(int hp, int spawn_index);

// Called from enemy_update switch for all MM_* states
void mimic_update(Enemy_t* e, int index, float dt,
                  float player_x, float player_y,
                  float player_vx, float player_vy);

// Called from enemy_draw when type == MIMIC
void mimic_draw(Enemy_t* e, int r, int g, int b, int alpha);

// Called from enemy_hit when type == MIMIC
// Returns 1 if mimic handled the hit (caller should return early)
int  mimic_on_hit(Enemy_t* e, float bullet_vx, float bullet_vy,
                  int killed, float knockback_dist);

// Called when mimic dies (knockback resolves to death) — restores weapon
void mimic_on_death(Enemy_t* e);

// Restore all stolen weapons (called on game reset)
void mimic_restore_all_weapons(void);

#endif /* MIMIC_H */
