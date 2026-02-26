#ifndef SHAMAN_H
#define SHAMAN_H

#include "enemy.h"

// Called from enemy_update switch for all shaman states
void shaman_update(Enemy_t* e, int index, float dt,
                   float player_x, float player_y);

// Called from enemy_draw when type == SHAMAN (cross shape, heal beam, eating sparks)
void shaman_draw(Enemy_t* e, int r, int g, int b, int alpha);

// Tick heal cooldown — called after state machine in enemy_update
void shaman_tick_cooldown(Enemy_t* e, float dt);

// Count active shamans (not corpses)
int shaman_get_count(void);

// Init shaman-specific fields after enemy_spawn creates the slot
void shaman_init_fields(Enemy_t* e);

// Check if a corpse is claimed by a shaman (used by update_corpse in enemy.c)
int shaman_corpse_is_claimed(int corpse_index);

#endif /* SHAMAN_H */
