#ifndef COLLISION_H
#define COLLISION_H

void collision_init(int max_enemies);
void collision_cleanup(void);
void collision_begin_death_tracking(void);
void collision_check_bullets(void);
void collision_resolve_deaths(void);

#endif /* COLLISION_H */
