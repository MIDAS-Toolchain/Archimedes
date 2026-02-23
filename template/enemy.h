#ifndef ENEMY_H
#define ENEMY_H

#include "../include/Archimedes.h"

// Enemy state
typedef enum {
  ENEMY_STATE_ALIVE,
  ENEMY_STATE_REPOSITIONING,  // Backing off to find new angle
  ENEMY_STATE_ATTACKING,      // Speed boost charge after repositioning
  ENEMY_STATE_RETREAT,        // Moving back to flanking position after hitting player
  ENEMY_STATE_HIT_KNOCKBACK,
  ENEMY_STATE_CORPSE
} EnemyState_t;

// Enemy structure
typedef struct {
  float x, y;                   // Position
  float vx, vy;                 // Velocity
  float target_angle;           // Offset angle around player (reused for stuck positioning)
  float next_retarget;          // Time until next retarget

  // Knockback tween
  float knockback_start_x, knockback_start_y;
  float knockback_target_x, knockback_target_y;
  float knockback_timer;        // 0.0 to 0.1s

  // Stuck detection (for intelligent repositioning)
  float last_distance_to_player;
  float stuck_timer;            // Time spent not getting closer
  float reposition_timer;       // Time until next reposition attempt
  float reposition_duration;    // How long to stay in repositioning state
  float attack_duration;        // How long to stay in attacking state
  float flank_x, flank_y;       // Saved flanking position to jump back to

  // State
  EnemyState_t state;
  int active;                   // Is this enemy slot used?
  int hit_count;                // Number of times hit (for color)
  int aggro;                    // Has entered attack mode at least once
  float death_timer;            // Time since death (for fade)
} Enemy_t;

// Blood particle structure
typedef struct {
  float x, y;                   // Position
  float vx, vy;                 // Velocity
  float lifetime;               // Time alive
  int active;                   // Is particle alive?
  int frozen;                   // Physics frozen after spill animation
} BloodParticle_t;

/**
 * @brief Initialize enemy system
 * @param max_enemies Maximum number of enemies
 * @param max_blood_particles Maximum number of blood particles
 */
void enemy_init(int max_enemies, int max_blood_particles);

/**
 * @brief Cleanup enemy system
 */
void enemy_cleanup(void);

/**
 * @brief Spawn a new enemy at position
 * @param x X position
 * @param y Y position
 * @return Index of spawned enemy, or -1 if failed
 */
int enemy_spawn(float x, float y);

/**
 * @brief Update all enemies
 * @param dt Delta time in seconds
 * @param player_x Player X position
 * @param player_y Player Y position
 * @param player_vx Player X velocity
 * @param player_vy Player Y velocity
 */
void enemy_update(float dt, float player_x, float player_y, float player_vx, float player_vy);

/**
 * @brief Draw all enemies
 */
void enemy_draw(void);

/**
 * @brief Check bullet collision with enemy and apply damage/knockback
 * @param enemy_index Index of enemy
 * @param bullet_vx Bullet velocity X
 * @param bullet_vy Bullet velocity Y
 */
void enemy_hit(int enemy_index, float bullet_vx, float bullet_vy);

/**
 * @brief Check if point collides with any enemy
 * @param x X position
 * @param y Y position
 * @param radius Collision radius
 * @return Index of hit enemy, or -1 if no hit
 */
int enemy_check_collision(float x, float y, float radius);

/**
 * @brief Get enemy position
 * @param enemy_index Index of enemy
 * @param out_x Output X position
 * @param out_y Output Y position
 */
void enemy_get_position(int enemy_index, float* out_x, float* out_y);

/**
 * @brief Get enemy state
 * @param enemy_index Index of enemy
 * @return Enemy state
 */
EnemyState_t enemy_get_state(int enemy_index);

/**
 * @brief Check if enemy is active
 * @param enemy_index Index of enemy
 * @return 1 if active, 0 otherwise
 */
int enemy_is_active(int enemy_index);

/**
 * @brief Get total number of enemies
 * @return Total enemy count
 */
int enemy_get_count(void);

/**
 * @brief Get maximum enemy array size (for iteration)
 * @return Max enemy slots
 */
int enemy_get_max_count(void);

/**
 * @brief Find the enemy that maximizes chain hits (cluster center)
 * @param radius Chain jump radius
 * @param player_x Player X for tiebreaking
 * @param player_y Player Y for tiebreaking
 * @return Index of best cluster target, or -1 if no alive enemies
 */
int enemy_find_cluster_target(float radius, float player_x, float player_y);

/**
 * @brief Check if enemy is alive (active and not a corpse or in knockback)
 * @param enemy_index Index of enemy
 * @return 1 if alive and targetable, 0 otherwise
 */
int enemy_is_alive(int enemy_index);

/**
 * @brief Find the densest cluster and return its center position
 * @param radius Cluster search radius
 * @param player_x Player X for tiebreaking
 * @param player_y Player Y for tiebreaking
 * @param out_x Output X position (enemy center)
 * @param out_y Output Y position (enemy center)
 * @return 1 if a target was found, 0 if no alive enemies
 */
int enemy_find_cluster_position(float radius, float player_x, float player_y,
                                float lead_time, float* out_x, float* out_y);

#endif /* ENEMY_H */
