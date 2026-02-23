#ifndef DROPS_H
#define DROPS_H

#include "weapons.h"

void drops_init(void);
void drops_spawn(float x, float y, WeaponType_t type);
void drops_update(float dt);
void drops_draw(void);
int drops_has_type(WeaponType_t type);

#endif /* DROPS_H */
