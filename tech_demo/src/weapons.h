#ifndef WEAPONS_H
#define WEAPONS_H

#define WEAPON_MAX_SLOTS 7

typedef enum {
  WEAPON_NONE,
  WEAPON_WAND,
  WEAPON_SPIN,
  WEAPON_CHAIN,
  WEAPON_ORBIT,
  WEAPON_BOMB,
  WEAPON_TURRET,
  WEAPON_TRAIL,
  WEAPON_SCYTHE,
  SOURCE_FIRE_CONE,        // Player fire cone pickup buff
  SOURCE_CONDUCTOR,        // Conductor chain lightning (from any weapon)
  SOURCE_GRUNT_EXPLOSION,  // Meta-progression grunt death AoE
  SOURCE_SNAKE_POP,        // Scale shatter projectiles from snake segment pops
  DROP_HEALTH,  // Not a weapon — health pickup handled by drops system
} WeaponType_t;

// Chain lightning constants
#define CHAIN_MAX_JUMPS       24
#define CHAIN_DELAY           0.065f   // 65ms between jumps
#define CHAIN_RADIUS          90.0f
#define CHAIN_VISUAL_DURATION 0.45f
#define CHAIN_COOLDOWN        2.5f
#define CHAIN_KNOCKBACK       20.0f

typedef struct {
  float x1, y1, x2, y2;
  float timer;
} ChainSegment_t;

typedef struct {
  int targets[CHAIN_MAX_JUMPS];   // enemy indices
  int target_is_corpse[CHAIN_MAX_JUMPS]; // 1 if target was a corpse (Corpse Chain upgrade)
  int num_targets;
  int current_jump;               // which jump we're on
  float propagation_timer;        // counts down to next jump
  int active;                     // is a chain currently in flight?
  int bounced;                    // has bounce back already fired?
} ChainState_t;

// Orbit weapon constants
#define ORBIT_COOLDOWN       5.0f
#define ORBIT_DURATION       3.0f
#define ORBIT_RADIUS         60.0f
#define ORBIT_ORB_SIZE       12.0f
#define ORBIT_HIT_COOLDOWN   0.5f
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
  float radius;
  int active;
} BombExplosion_t;

// Turret weapon constants
#define TURRET_COOLDOWN         4.5f
#define TURRET_DURATION         3.5f
#define TURRET_FIRE_RATE        0.7f
#define TURRET_RANGE            150.0f
#define TURRET_BULLET_SPEED     180.0f
#define TURRET_BULLET_LIFETIME  0.9f
#define TURRET_HOMING_RATE      3.0f
#define TURRET_BULLET_SIZE      4
#define TURRET_MAX              8
#define TURRET_BULLET_MAX       24
#define TURRET_SLOW_MAX         24

typedef struct {
  float x, y;
  float lifetime;
  float max_lifetime;
  float fire_timer;
  float tesla_timer;
  int active;
  int overcharge_halfway_fired;
  int is_mini;
  int mini_halfway_spawned;
} Turret_t;

typedef struct {
  float x, y, vx, vy;
  float lifetime;
  int active;
  int pierce_remaining;   // how many more enemies it can pass through (-1 = infinite)
  int hit_enemies[8];     // track already-hit enemies to prevent double-hit
  int hit_count;          // number of entries in hit_enemies
} TurretBullet_t;

typedef struct {
  float x, y, radius;
  float lifetime, max_lifetime;
  float slow_mult;
  int active;
} TurretSlowZone_t;

// Trail weapon constants
#define TRAIL_COOLDOWN         4.5f
#define TRAIL_ACTIVE_DURATION  2.5f
#define TRAIL_PERSIST          1.0f
#define TRAIL_TICK_RATE        1.0f
#define TRAIL_WIDTH            16.0f
#define TRAIL_DROP_DISTANCE    8.0f
#define TRAIL_MAX_SEGMENTS     80
#define TRAIL_EMBER_MAX        16

#define TRAIL_HIT_TRACK 32

typedef struct {
  int enemy;
  float cooldown;
  int primed;       // 0 = initial delay (no damage yet), 1 = will deal damage when cooldown expires
} TrailHit_t;

typedef struct {
  float x, y;
  float dir_x, dir_y;
  float lifetime, max_lifetime;
  int active;
} TrailSegment_t;

typedef struct {
  float x, y, radius;
  float timer;
  int has_damage;
  int active;
} TrailEmber_t;

// Scythe weapon constants
#define SCYTHE_COOLDOWN       1.2f
#define SCYTHE_BASE_RADIUS    116.0f
#define SCYTHE_BASE_ARC       120.0f   // degrees
#define SCYTHE_BASE_KNOCKBACK 275.0f
#define SCYTHE_SWEEP_DURATION 0.15f

typedef struct {
  WeaponType_t type;
  const char* label;
  float cooldown;
  float timer;
} Weapon_t;

void weapons_init(void);
void weapons_update(float dt);
void weapons_draw(void);

// Damage tracking
void weapons_set_damage_source(WeaponType_t type);
WeaponType_t weapons_get_damage_source(void);
void weapons_record_hit(void);
int  weapons_get_total_hits(WeaponType_t type);
float weapons_get_dps(WeaponType_t type);
float weapons_get_pickup_time(WeaponType_t type);
const char* weapons_get_name(WeaponType_t type);

int weapons_add(WeaponType_t type);
int weapons_has(WeaponType_t type);
const Weapon_t* weapons_get_slot(int slot);
float weapons_get_cooldown_progress(int slot);
float weapons_get_effective_cooldown(int slot);
int weapons_get_count(void);
int weapons_get_active_count(void);

// Crater zone check (for enemy slow + damage amp)
int weapons_is_in_crater(float x, float y, float* out_damage_mult);

// Conductor: fire chain lightning from an enemy on conductor expiry
void weapons_fire_conductor_chain(int source_enemy, int max_jumps);
void weapons_fire_conductor_chain_at(float x, float y, int max_jumps);

// Turret bullet collision interface
int weapons_get_turret_bullet_count(void);
void weapons_get_turret_bullet(int index, float* x, float* y, float* r);
void weapons_deactivate_turret_bullet(int index);
void weapons_spawn_turret_slow_zone(float x, float y);

// Turret slow zone query (for enemy.c)
int weapons_get_turret_slow(float x, float y, float* out_mult);

// Turret threat query: find nearest active turret within radius. Returns 1 if found.
int weapons_get_nearest_turret(float x, float y, float radius, float* out_x, float* out_y);

// Wand helpers (for player_actions.c)
float weapons_get_wand_bullet_speed_mult(void);
float weapons_get_wand_proj_size_mult(void);

// Trail weapon interface
int weapons_is_trail_active(void);
float weapons_get_trail_speed_mult(void);
float weapons_get_trail_slow(float x, float y);

// Scythe harvest speed buff
float weapons_get_scythe_harvest_speed(void);

// Hazard queries for shaman AI
int weapons_is_in_linger_zone(float x, float y);
int weapons_is_in_trail(float x, float y);
int weapons_get_nearest_hazard(float x, float y, float radius, float* out_x, float* out_y);

// Mimic queries (weapon parameters for hostile use)
float weapons_get_slot_cooldown(int slot);
float weapons_get_spin_radius(void);
float weapons_get_bomb_blast_radius(void);
float weapons_get_bomb_flight_time(void);
float weapons_get_chain_radius(void);
float weapons_get_turret_duration(void);
float weapons_get_turret_fire_rate(void);
float weapons_get_turret_range(void);
int   weapons_get_turret_spread(void);
float weapons_get_trail_duration(void);
float weapons_get_trail_persist(void);
float weapons_get_trail_width(void);
int   weapons_get_orbit_orb_count(void);
float weapons_get_orbit_duration(void);
float weapons_get_orbit_radius(void);
float weapons_get_orbit_speed_mult(void);
float weapons_get_scythe_radius(void);
float weapons_get_scythe_arc(void);
float weapons_get_scythe_knockback(void);

// Achievement kill tracking (called from collision_resolve_deaths)
void weapons_notify_kill(WeaponType_t source);

// Mimic weapon steal/restore
int weapons_disable_slot(int slot);                              // Mark slot as stolen (set to WEAPON_NONE)
void weapons_restore_slot(int slot, WeaponType_t type);         // Restore weapon to slot
void weapons_restore_all_stolen(void);                           // Restore all stolen slots (game reset)
int weapons_is_slot_stolen(int slot);                            // Check if slot is currently stolen
int weapons_is_type_stolen(WeaponType_t type);                   // Check if a weapon type is currently stolen by a mimic
const char* weapons_get_stolen_label(int slot);                  // Get original label of stolen weapon

#endif /* WEAPONS_H */
