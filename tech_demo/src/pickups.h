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

// Shared buff constants (used by both player and brutes)
#define BUFF_FIRE_CONE_DURATION  4.0f
#define BUFF_SPEED_DURATION      4.0f
#define BUFF_SPEED_MULT          1.5f
#define BUFF_SHIELD_HITS         3    // hits per pickup
#define BUFF_SHIELD_PLAYER_CAP   3    // max hits player can hold (upgradable later)
#define BUFF_SLOW_AURA_DURATION  4.0f

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

// Pickup collection distance (base + global upgrade bonus)
float pickups_get_collect_dist(void);

#endif /* PICKUPS_H */
