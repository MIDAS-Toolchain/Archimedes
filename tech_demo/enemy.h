#ifndef ENEMY_H
#define ENEMY_H

#include "../include/Archimedes.h"
#include "pickups.h"
#include "weapons.h"

// Enemy types
typedef enum {
  ENEMY_TYPE_GRUNT,
  ENEMY_TYPE_DASHER,
  ENEMY_TYPE_BRUTE,
  ENEMY_TYPE_SHAMAN,
  ENEMY_TYPE_SNAKE,
  ENEMY_TYPE_BEHOLDER,
  ENEMY_TYPE_COUNT
} EnemyType_t;

// Per-type stats (shared const lookup table)
typedef struct {
  float speed;
  float size;
  float radius;
  int hits_to_kill;
  int damage;
  float attack_speed_mult;
  float attack_duration_min, attack_duration_max;
  float reposition_duration_min, reposition_duration_max;
  float flank_distance;
  float separation_radius;
  int skip_reposition;
} EnemyStats_t;

// Enemy state
typedef enum {
  ENEMY_STATE_ALIVE,
  ENEMY_STATE_REPOSITIONING,  // Backing off to find new angle
  ENEMY_STATE_WINDUP,         // Dasher telegraph: pull back + red indicator
  ENEMY_STATE_ATTACKING,      // Speed boost charge after repositioning
  ENEMY_STATE_RETREAT,        // Moving back to flanking position after hitting player
  ENEMY_STATE_SEEKING_HEALTH, // Brute sprinting to steal a health pickup
  ENEMY_STATE_SEEKING_PICKUP, // Brute sprinting to steal a power pickup
  ENEMY_STATE_SEEKING_CORPSE, // Shaman moving toward a corpse to eat
  ENEMY_STATE_EATING,         // Shaman consuming a corpse
  ENEMY_STATE_SEEKING_ALLY,   // Shaman moving toward ally to heal
  ENEMY_STATE_HEALING,        // Shaman channeling heal beam
  ENEMY_STATE_FLEEING,        // Shaman running from player
  ENEMY_STATE_BH_ENTERING,
  ENEMY_STATE_BH_STRAFING,
  ENEMY_STATE_BH_AIMING,
  ENEMY_STATE_BH_FIRING,
  ENEMY_STATE_BH_FLEEING,
  ENEMY_STATE_BH_CHANNELING,
  ENEMY_STATE_HIT_KNOCKBACK,
  ENEMY_STATE_CORPSE
} EnemyState_t;

// Enemy structure
typedef struct {
  float x, y;                   // Position
  float vx, vy;                 // Velocity
  float target_angle;           // Offset angle around player
  float next_retarget;          // Time until next retarget

  // Knockback tween
  float knockback_start_x, knockback_start_y;
  float knockback_target_x, knockback_target_y;
  float knockback_timer;        // 0.0 to 0.1s

  // Stuck detection
  float last_distance_to_player;
  float stuck_timer;            // Time spent not getting closer
  float reposition_timer;       // (unused legacy)
  float reposition_duration;    // How long to stay in repositioning state
  float attack_duration;        // How long to stay in attacking state
  float flank_x, flank_y;       // Saved flanking position to jump back to

  // Dasher windup
  float windup_timer;           // Countdown during WINDUP state
  float dash_dir_x, dash_dir_y; // Locked dash direction after windup

  // Brute health-seeking
  float heal_target_x, heal_target_y; // Health pickup position to sprint toward

  // Brute pickup-seeking
  float pickup_target_x, pickup_target_y;

  // Brute buffs (one per pickup type, all independent)
  Buff_t brute_buffs[PICKUP_TYPE_COUNT];
  float fire_cone_tick_timer;
  float fire_cone_elapsed;      // Total time fire cone has been active (for growth)
  float slow_aura_elapsed;      // Total time slow aura has been active (for growth)

  // State
  EnemyState_t state;
  EnemyType_t type;
  int active;                   // Is this enemy slot used?
  int hit_count;                // Number of times hit (for color)
  int aggro;                    // Has entered attack mode at least once
  float death_timer;            // Time since death (for fade)

  // Difficulty scaling (set at spawn)
  float speed_mult;
  int bonus_hp;

  // Corpse tracking (all types)
  int base_hits_to_kill;        // Original hits_to_kill (set at spawn from stats)
  int corpse_consumed;          // Set when shaman eats this corpse

  // Upgrade-applied debuffs
  float conductor_timer;        // Conductor: fires chain lightning on expiry
  int conductor_jumps;          // Number of chain jumps on expiry (= tier)
  float stun_timer;             // Static Field: can't move/attack while > 0
  float stun_damage_mult;       // Static Field T3: 1.5x damage taken (default 1.0)

  // Shaman healing
  int stored_heal_value;        // HP from eaten corpse (0 = passive 1HP mode)
  int heal_target;              // Enemy index being healed (-1 = none)
  float heal_channel_timer;     // 1.0s countdown during HEALING
  float heal_cooldown_timer;    // Counts down to 0, ready when 0
  float eat_timer;              // 0.5s countdown during EATING
  int target_corpse;            // Enemy index of corpse being sought (-1 = none)
  float orbit_angle;            // For strafing behavior
  float orbit_dir_timer;        // Time until orbit direction change
  int orbit_direction;          // +1 or -1
  float heal_flash_timer;       // Brief green flash on heal target
  int last_hit_source;          // WeaponType_t of weapon that last hit this enemy

  // Beholder
  float bh_beam_angle;
  float bh_beam_start_angle;
  float bh_beam_swept;
  float bh_state_timer;
  float bh_cooldown;
  int   bh_attack_count;
  int   bh_starting_shield;
  int   bh_shield;
  int   bh_beam_has_hit;
  float bh_flee_vx, bh_flee_vy;
  float bh_tendril_phase;
  float bh_blink_timer;
  int   bh_orbit_dir;              // +1 or -1 strafe direction
  int   bh_shield_broke_during_fire; // deferred flee after beam finishes
  float bh_charge_progress;        // 0→1 tween during AIMING
  float bh_trail_angles[8];        // recent beam angles for light trail
  float bh_trail_alphas[8];        // fade alpha for each trail segment
  int   bh_trail_count;            // active trail entries
  float bh_aim_offset;             // angular offset for "miss" prediction
  int   bh_windup_channel;         // audio channel playing windup (-1 = none)
  float bh_last_pupil_ox;          // last pupil offset (frozen on death)
  float bh_last_pupil_oy;
  float bh_predicted_angle;        // telegraph aim angle (predicted + offset)
} Enemy_t;

void enemy_init(int max_enemies, int max_blood_particles);
void enemy_cleanup(void);

int enemy_spawn(float x, float y, EnemyType_t type, float speed_mult, int bonus_hp);
int beholder_spawn(int hp, int shield);

void enemy_update(float dt, float player_x, float player_y, float player_vx, float player_vy);
void enemy_draw(void);

void enemy_hit(int enemy_index, float bullet_vx, float bullet_vy);
void enemy_hit_no_knockback(int enemy_index);  // Damage only, no stun/knockback
int enemy_check_collision(float x, float y, float radius);

void enemy_get_position(int enemy_index, float* out_x, float* out_y);
EnemyState_t enemy_get_state(int enemy_index);
int enemy_is_active(int enemy_index);
int enemy_get_count(void);
int enemy_get_max_count(void);
int enemy_is_alive(int enemy_index);
float enemy_get_radius(int enemy_index);
void enemy_get_velocity(int enemy_index, float* out_vx, float* out_vy);
EnemyType_t enemy_get_type(int enemy_index);
int enemy_get_last_hit_source(int enemy_index);

int enemy_find_cluster_target(float radius, float player_x, float player_y);
int enemy_find_cluster_position(float radius, float player_x, float player_y,
                                float lead_time, float* out_x, float* out_y);

// Upgrade debuff setters/getters
void enemy_set_conductor(int enemy_index, float duration, int jumps);
int enemy_is_conductor(int enemy_index);
void enemy_set_stun(int enemy_index, float duration, float damage_mult);
void enemy_displace(int enemy_index, float dx, float dy);
int enemy_brute_slow_aura_check(float x, float y);

// Shaman support
int enemy_get_shaman_count(void);

// Explosion visual effects
void enemy_spawn_explosion(float x, float y, float radius);

#endif /* ENEMY_H */
