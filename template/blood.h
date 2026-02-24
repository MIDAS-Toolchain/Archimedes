#ifndef BLOOD_H
#define BLOOD_H

void blood_init(int max_particles);
void blood_cleanup(void);
void blood_spawn(float x, float y, float dir_x, float dir_y, float entity_radius);
void blood_update(float dt);
void blood_draw(void);

#endif /* BLOOD_H */
