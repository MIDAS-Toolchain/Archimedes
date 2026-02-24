#ifndef WEAPONS_H
#define WEAPONS_H

#define WEAPON_MAX_SLOTS 5

typedef enum {
  WEAPON_NONE,
  WEAPON_WAND,
  WEAPON_SPIN,
  WEAPON_CHAIN,
  WEAPON_ORBIT,
  WEAPON_BOMB,
  DROP_HEALTH,  // Not a weapon — health pickup handled by drops system
} WeaponType_t;

// Chain lightning constants
#define CHAIN_MAX_JUMPS       8
#define CHAIN_DELAY           0.065f   // 65ms between jumps
#define CHAIN_RADIUS          90.0f
#define CHAIN_VISUAL_DURATION 0.15f
#define CHAIN_COOLDOWN        2.5f
#define CHAIN_KNOCKBACK       20.0f

typedef struct {
  float x1, y1, x2, y2;
  float timer;
} ChainSegment_t;

typedef struct {
  int targets[CHAIN_MAX_JUMPS];   // enemy indices
  int num_targets;
  int current_jump;               // which jump we're on
  float propagation_timer;        // counts down to next jump
  int active;                     // is a chain currently in flight?
} ChainState_t;

// Orbit weapon constants
#define ORBIT_COOLDOWN       5.0f
#define ORBIT_DURATION       3.0f
#define ORBIT_RADIUS         60.0f
#define ORBIT_ORB_SIZE       11.0f
#define ORBIT_HIT_COOLDOWN   0.3f
#define ORBIT_KNOCKBACK      200.0f
#define ORBIT_MAX_HIT_TRACK  50

typedef struct {
  float angle;
  float active_timer;
  int is_active;
} OrbitState_t;

typedef struct {
  int enemy_index;
  float cooldown;
} OrbitHitEntry_t;

// Bomb weapon constants
#define BOMB_COOLDOWN         2.5f
#define BOMB_FLIGHT_TIME      0.6f
#define BOMB_EXPLOSION_RADIUS 85.0f
#define BOMB_VISUAL_DURATION  0.25f
#define BOMB_KNOCKBACK        250.0f
#define BOMB_MAX_ACTIVE       8

typedef struct {
  float start_x, start_y;
  float target_x, target_y;
  float flight_progress;
  int in_flight;
} BombFlight_t;

typedef struct {
  float x, y;
  float timer;
  int active;
} BombExplosion_t;

typedef struct {
  WeaponType_t type;
  const char* label;
  float cooldown;
  float timer;
} Weapon_t;

void weapons_init(void);
void weapons_update(float dt);
void weapons_draw(void);

int weapons_add(WeaponType_t type);
int weapons_has(WeaponType_t type);
const Weapon_t* weapons_get_slot(int slot);
float weapons_get_cooldown_progress(int slot);
int weapons_get_count(void);

#endif /* WEAPONS_H */
