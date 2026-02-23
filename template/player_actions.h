/**
 * player_actions.h - Player movement and combat
 *
 * Handles player character movement, shooting, and bullet physics.
 */

#ifndef PLAYER_ACTIONS_H
#define PLAYER_ACTIONS_H

#include "Archimedes.h"

/**
 * @brief Initialize player state
 */
void player_init(void);

/**
 * @brief Update player movement and bullet physics
 * @param dt Delta time in seconds
 */
void player_update(float dt);

/**
 * @brief Draw player and active bullets
 * @param img Bullet texture image
 */
void player_draw(aImage_t* img);

/**
 * @brief Get player center X position (for enemy targeting)
 * @return Player center X coordinate
 */
float player_get_x(void);

/**
 * @brief Get player center Y position (for enemy targeting)
 * @return Player center Y coordinate
 */
float player_get_y(void);

/**
 * @brief Check if any bullet hit an enemy
 * @param enemy_x Enemy X position
 * @param enemy_y Enemy Y position
 * @param enemy_radius Enemy collision radius
 * @return Index of bullet that hit, or -1 if no hit
 */
int player_check_bullet_collision(float enemy_x, float enemy_y, float enemy_radius);

/**
 * @brief Get bullet velocity for blood splatter direction
 * @param bullet_index Index of the bullet
 * @param out_vx Output X velocity
 * @param out_vy Output Y velocity
 */
void player_get_bullet_velocity(int bullet_index, float* out_vx, float* out_vy);

/**
 * @brief Destroy a bullet by index
 * @param bullet_index Index of the bullet to destroy
 */
void player_destroy_bullet(int bullet_index);

/**
 * @brief Get player velocity X (for enemy AI prediction)
 * @return Player velocity X
 */
float player_get_vx(void);

/**
 * @brief Get player velocity Y (for enemy AI prediction)
 * @return Player velocity Y
 */
float player_get_vy(void);

void player_fire_at_nearest(void);
void player_do_spin_attack(float radius);

// Dash
void player_dash(void);
float player_get_dash_cooldown_progress(void);
int player_is_dashing(void);

int player_take_damage(int amount);
void player_heal(int amount);
void player_draw_screen_flash(void);
int player_get_hp(void);
int player_get_max_hp(void);
int player_is_alive(void);
int player_is_invincible(void);
float player_get_heal_flash_progress(void);

#endif /* PLAYER_ACTIONS_H */
