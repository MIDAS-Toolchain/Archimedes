#ifndef BEHOLDER_H
#define BEHOLDER_H

#include "enemy.h"

int beholder_spawn(int hp, int shield);

// Called from enemy_update switch for all BH_* states
void beholder_update(Enemy_t* e, int index, float dt,
                     float player_x, float player_y,
                     float player_vx, float player_vy);

// Called from enemy_draw when type == BEHOLDER
void beholder_draw(Enemy_t* e, int r, int g, int b, int alpha);

// Called from enemy_hit when type == BEHOLDER
// Returns 1 if beholder handled the hit (caller should return early)
int beholder_on_hit(Enemy_t* e, float bullet_vx, float bullet_vy,
                    int killed, float knockback_dist);

// Called from start_knockback when type == BEHOLDER
void beholder_on_knockback(Enemy_t* e);

// Contact damage check — called after state machine in enemy_update
void beholder_contact_damage(Enemy_t* e, float player_x, float player_y);

// Tick cosmetic timers (tendril phase, blink) — called after state machine
void beholder_tick_cosmetics(Enemy_t* e, float dt);

// Stop all beholder sounds (used on game reset)
void beholder_stop_sounds(void);

#endif /* BEHOLDER_H */
