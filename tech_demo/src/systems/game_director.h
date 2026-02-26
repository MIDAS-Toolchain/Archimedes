#ifndef GAME_DIRECTOR_H
#define GAME_DIRECTOR_H

void director_init(void);
void director_reset(void);
void director_update(float dt);
float director_get_elapsed(void);
float director_get_spawn_interval(void);
float director_get_spawn_timer(void);
int   director_get_max_active(void);
float director_get_speed_mult(void);
int   director_get_hp_bonus(void);

#endif /* GAME_DIRECTOR_H */
