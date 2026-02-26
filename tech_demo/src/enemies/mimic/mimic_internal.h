#ifndef MIMIC_INTERNAL_H
#define MIMIC_INTERNAL_H

#include "enemy_internal.h"
#include "mimic.h"

// ============================================================================
// Mimic Constants
// ============================================================================

#define MIMIC_BODY_SIZE          36.0f
#define MIMIC_BASE_HP            40
#define MIMIC_HP_GROWTH          5
#define MIMIC_CONTACT_DAMAGE     20
#define MIMIC_STEAL_WINDUP       0.8f     // shake/telegraph before lunge
#define MIMIC_STEAL_DASH_SPEED   450.0f   // lunge speed toward player
#define MIMIC_STEAL_DASH_STOP    60.0f    // stop dash when this close to player
#define MIMIC_STEAL_DASH_MAX     0.8f     // max dash duration (safety timeout)
#define MIMIC_STEAL_ORB_SPEED    350.0f   // steal orb projectile speed
#define MIMIC_STEAL_ORB_RADIUS   10.0f    // steal orb collision radius
#define MIMIC_STEAL_ORB_MAX      2.0f     // max orb flight time before despawn
#define MIMIC_ENTER_TIME         0.8f     // time to waddle onto screen
#define MIMIC_DEATH_ORB_TIME     0.3f     // weapon return orb flight
#define MIMIC_TEXT_DURATION      2.0f     // how long text shows
#define MIMIC_CHOMP_RATE         0.5f     // mouth chomp period (seconds)
#define MIMIC_WADDLE_SPEED       6.0f     // waddle oscillation speed
#define MIMIC_BASE_XP            8
#define MIMIC_SCORE              500

// AI ranges per stolen weapon (Phase 2)
#define MIMIC_RANGE_FAR          250.0f
#define MIMIC_RANGE_MID          150.0f
#define MIMIC_RANGE_MIDCLOSE     120.0f
#define MIMIC_RANGE_CLOSE_ORBIT  80.0f
#define MIMIC_RANGE_CLOSE_SPIN   60.0f
#define MIMIC_RANGE_TURRET_IDLE  200.0f

// Movement speeds per stolen weapon (Phase 2)
#define MIMIC_SPEED_STRAFE       80.0f
#define MIMIC_SPEED_SPIN         120.0f
#define MIMIC_SPEED_ORBIT        90.0f
#define MIMIC_SPEED_TRAIL        90.0f
#define MIMIC_SPEED_BOMB         60.0f
#define MIMIC_SPEED_TURRET_DASH  300.0f
#define MIMIC_SPEED_TURRET_IDLE  40.0f

// Spin AoE
#define MIMIC_SPIN_PULSE_DURATION 0.15f

// Wand projectile
#define MIMIC_WAND_BULLET_SPEED  600.0f
#define MIMIC_WAND_MAX_BULLETS   16
#define MIMIC_WAND_BULLET_SIZE   4
#define MIMIC_PLAYER_RADIUS      16.0f

// Bomb
#define MIMIC_BOMB_MAX           4
#define MIMIC_BOMB_FLIGHT_HEIGHT 40.0f
#define MIMIC_BOMB_SHRAPNEL_MAX  24
#define MIMIC_BOMB_SHRAPNEL_SPEED 300.0f
#define MIMIC_BOMB_CLUSTER_MAX   15
#define MIMIC_BOMB_NAPALM_MAX    4
#define MIMIC_BOMB_CRATER_MAX    4
#define MIMIC_BOMB_VISUAL_DURATION 0.25f

// Chain
#define MIMIC_SPEED_CHAIN        85.0f
#define MIMIC_CHAIN_MAX_SEGMENTS 8
#define MIMIC_CHAIN_LINGER_MAX   3
#define MIMIC_CHAIN_VISUAL_DURATION 0.45f
#define MIMIC_CHAIN_DELAY        0.0585f

// Weapon glow colors (r, g, b) per weapon type
// Used for the orb and the glow inside the mouth
// (Defined as arrays in mimic.c)

// Phase 2: Weapon-specific AI (defined in mimic_wand.c / mimic_spin.c)
void mimic_wand_init(void);
void mimic_wand_update(Enemy_t* e, int index, float dt,
                       float px, float py, float pvx, float pvy);
void mimic_wand_draw(Enemy_t* e);
void mimic_wand_cleanup(void);

void mimic_spin_update(Enemy_t* e, int index, float dt,
                       float px, float py, float pvx, float pvy);
void mimic_spin_draw(Enemy_t* e);

void mimic_bomb_update(Enemy_t* e, int index, float dt,
                       float px, float py, float pvx, float pvy);
void mimic_bomb_draw(Enemy_t* e);
void mimic_bomb_cleanup(void);

void mimic_chain_update(Enemy_t* e, int index, float dt,
                        float px, float py, float pvx, float pvy);
void mimic_chain_draw(Enemy_t* e);
void mimic_chain_cleanup(void);

void mimic_turret_update(Enemy_t* e, int index, float dt,
                         float px, float py, float pvx, float pvy);
void mimic_turret_draw(Enemy_t* e);
void mimic_turret_cleanup(void);

void mimic_trail_update(Enemy_t* e, int index, float dt,
                        float px, float py, float pvx, float pvy);
void mimic_trail_draw(Enemy_t* e);
void mimic_trail_cleanup(void);

void mimic_orbit_update(Enemy_t* e, int index, float dt,
                        float px, float py, float pvx, float pvy);
void mimic_orbit_draw(Enemy_t* e);
void mimic_orbit_cleanup(void);

void mimic_scythe_update(Enemy_t* e, int index, float dt,
                         float px, float py, float pvx, float pvy);
void mimic_scythe_draw(Enemy_t* e);
void mimic_scythe_cleanup(void);

#endif /* MIMIC_INTERNAL_H */
