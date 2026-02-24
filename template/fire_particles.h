#ifndef FIRE_PARTICLES_H
#define FIRE_PARTICLES_H

void fire_particles_init(int max_particles);
void fire_particles_cleanup(void);
void fire_particles_spawn(float x, float y, float dir_x, float dir_y);
void fire_particles_update(float dt);
void fire_particles_draw(void);

#endif /* FIRE_PARTICLES_H */
