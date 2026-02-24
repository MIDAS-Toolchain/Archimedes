#ifndef ENEMY_H
#define ENEMY_H

#include "../include/Archimedes.h"
#include "pickups.h"

// Enemy types
typedef enum {
  ENEMY_TYPE_GRUNT,
  ENEMY_TYPE_DASHER,
  ENEMY_TYPE_BRUTE,
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

  // Brute buff (one active at a time)
  Buff_t brute_buff;
  PickupType_t brute_buff_type;
  float fire_cone_tick_timer;

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
} Enemy_t;

void enemy_init(int max_enemies, int max_blood_particles);
void enemy_cleanup(void);

int enemy_spawn(float x, float y, EnemyType_t type, float speed_mult, int bonus_hp);

void enemy_update(float dt, float player_x, float player_y, float player_vx, float player_vy);
void enemy_draw(void);

void enemy_hit(int enemy_index, float bullet_vx, float bullet_vy);
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

int enemy_find_cluster_target(float radius, float player_x, float player_y);
int enemy_find_cluster_position(float radius, float player_x, float player_y,
                                float lead_time, float* out_x, float* out_y);

#endif /* ENEMY_H */
