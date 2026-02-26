#ifndef ENEMY_INTERNAL_H
#define ENEMY_INTERNAL_H

// Internal header shared between enemy.c, beholder.c, and shaman.c
// NOT for external consumption — use enemy.h for the public API.

#include "enemy.h"
#include "game_audio.h"
#include "blood.h"
#include "weapons.h"
#include "player_actions.h"
#include "progress.h"
#include "snake.h"
#include "stats.h"
#include <math.h>

// ============================================================================
// Shared State (defined in enemy.c)
// ============================================================================

extern Enemy_t* enemies;
extern int max_enemies;
extern const EnemyStats_t enemy_stats[];

// ============================================================================
// Shared Constants
// ============================================================================

#define KNOCKBACK_DURATION 0.1f
#define KNOCKBACK_STRENGTH 20.0f
#define KILL_KNOCKBACK_MIN 1.5f
#define KILL_KNOCKBACK_RANGE 0.75f

#define OFFSCREEN_MARGIN 100.0f
#define CORPSE_LIFETIME 4.0f
#define CORPSE_FADE_START 2.0f
#define SEPARATION_WEIGHT 60.0f
#define PLAYER_MIN_DISTANCE 17.0f

#define ENEMY_COLOR_HEALTHY_R 0
#define ENEMY_COLOR_HEALTHY_G 255
#define ENEMY_COLOR_HEALTHY_B 0
#define ENEMY_COLOR_DAMAGED_R 255
#define ENEMY_COLOR_DAMAGED_G 0
#define ENEMY_COLOR_DAMAGED_B 0

// ============================================================================
// Shared Helper Functions (defined in enemy.c)
// ============================================================================

float ei_lerp(float a, float b, float t);
float ei_dist_between(float x1, float y1, float x2, float y2);
float ei_normalize(float* x, float* y);
int ei_is_offscreen(float x, float y);

const EnemyStats_t* ei_get_stats(Enemy_t* e);
int ei_get_total_hp(Enemy_t* e);
float ei_get_slow_multiplier(Enemy_t* e);
float ei_get_effective_speed(Enemy_t* e);

void ei_calc_separation(int self_index, float* out_x, float* out_y, float scale_factor);
void ei_move_toward(Enemy_t* e, float target_x, float target_y,
                    float speed, float sep_x, float sep_y,
                    float sep_weight, float dt);
int ei_resolve_player_collision(Enemy_t* e, float player_x, float player_y);
void ei_start_knockback(Enemy_t* e, float bullet_vx, float bullet_vy, float knockback_dist);

// Hazard avoidance
int ei_is_in_hazard(float x, float y);
void ei_calc_hazard_avoidance(float cx, float cy, float radius, float weight,
                              float* out_x, float* out_y);
void ei_calc_turret_avoidance(float cx, float cy, float radius, float weight,
                              float* out_x, float* out_y);
void ei_steer_toward_avoiding_hazards(float cx, float cy, float tcx, float tcy,
                                      float speed, float dt,
                                      float* out_vx, float* out_vy);

#endif /* ENEMY_INTERNAL_H */
