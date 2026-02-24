#ifndef PICKUPS_H
#define PICKUPS_H

typedef enum {
  PICKUP_FIRE_CONE,
  PICKUP_SPEED,
  PICKUP_SHIELD,
  PICKUP_SLOW_AURA,
  PICKUP_TYPE_COUNT
} PickupType_t;

typedef struct {
  float x, y;
  PickupType_t type;
  float lifetime;       // counts down from 10s
  int active;
  float age;            // counts up for bob animation
} Pickup_t;

// Buff struct (used by both player and brutes)
typedef struct {
  int active;
  float duration;
  int shield_hits;      // only for PICKUP_SHIELD
} Buff_t;

void pickups_init(void);
void pickups_cleanup(void);
void pickups_spawn(float x, float y, PickupType_t type);
void pickups_spawn_random(float x, float y);
void pickups_update(float dt);
void pickups_draw(void);

// Brute scanning
int pickups_find_nearest(float x, float y, float radius,
                         PickupType_t* out_type, float* out_x, float* out_y);
int pickups_consume_nearest(float x, float y, float radius);

// Pity timer
void pickups_notify_kill(int dropped);
void pickups_check_pity(float dt, float player_x, float player_y);

#endif /* PICKUPS_H */
