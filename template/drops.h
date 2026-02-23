#ifndef DROPS_H
#define DROPS_H

#include "weapons.h"

void drops_init(void);
void drops_spawn(float x, float y, WeaponType_t type);
void drops_update(float dt);
void drops_draw(void);

#endif /* DROPS_H */
