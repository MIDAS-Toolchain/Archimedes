#ifndef PROGRESS_H
#define PROGRESS_H

#include "enemy.h"

typedef enum {
  PROG_GRUNT_DMG_RED,
  PROG_GRUNT_EXPLOSION,
  PROG_DASHER_DMG_RED,
  PROG_DASHER_STUN,
  PROG_BRUTE_DMG_RED,
  PROG_BRUTE_SPEED_DIM,
  PROG_SHAMAN_CORPSE_DECAY,
  PROG_SHAMAN_HEAL_RED,
  PROG_SNAKE_DMG_RED,
  PROG_SNAKE_SCALE_SHATTER,
  PROG_BEHOLDER_DMG_RED,
  PROG_BEHOLDER_TELEGRAPH,
  PROG_MIMIC_DMG_RED,
  PROG_MIMIC_WEAPON_CD,
  PROG_COUNT
} ProgressUpgradeId_t;

typedef enum {
  GLOBAL_POWERUP_DURATION,    // 3 tiers
  GLOBAL_SHIELD_TICKS,        // 3 tiers
  GLOBAL_DASH_COOLDOWN,       // 2 tiers
  GLOBAL_DASH_EXTRA_CHARGE,   // 1 tier
  GLOBAL_HEALTH_PICKUP_BONUS, // 5 tiers
  GLOBAL_LUCKY_ROLLS,         // 5 tiers
  GLOBAL_REROLL,              // 3 tiers
  GLOBAL_QUICK_LEARNER,       // 2 tiers
  GLOBAL_MAX_HP,              // 5 tiers
  GLOBAL_TOUGHNESS,           // 5 tiers (last 3 super expensive)
  GLOBAL_XP_MAGNET,           // 3 tiers
  GLOBAL_HEALTH_REGEN,        // 3 tiers (last 2 super expensive)
  GLOBAL_STARTING_SHIELD,     // 5 tiers
  GLOBAL_EXTRA_CHOICE,        // 1 tier (room for more)
  GLOBAL_EXTRA_WEAPON_SLOT,   // 1 tier — most expensive upgrade
  GLOBAL_RETALIATION_KB,      // 1 tier — AoE knockback on HP loss
  GLOBAL_RETALIATION_STUN,    // 3 tiers — AoE stun on HP loss
  GLOBAL_HEAD_START,          // 3 tiers — starting XP
  GLOBAL_PICKUP_MAGNET,       // 4 tiers — pickup collect distance
  GLOBAL_PITY_TIMER,          // 3 tiers — faster pity spawns
  GLOBAL_PICKUP_DURATION,     // 3 tiers — pickups last longer
  GLOBAL_REVIVE,              // 1 tier — revive once per run
  GLOBAL_THORNS,              // 3 tiers — damage nearby enemies on HP loss
  GLOBAL_BOUNTY,              // 1 tier — +1 XP orb per kill
  GLOBAL_COUNT
} GlobalUpgradeId_t;

void progress_init(void);
void progress_save(void);
void progress_bank_run_kills(int kills[ENEMY_TYPE_COUNT]);
void progress_add_enemy_bonus(EnemyType_t type, int pts);

int  progress_get_lifetime_kills(EnemyType_t type);
int  progress_get_available_points(EnemyType_t type);

int  progress_get_tier(ProgressUpgradeId_t id);
int  progress_get_max_tier(ProgressUpgradeId_t id);
int  progress_get_next_cost(ProgressUpgradeId_t id);
int  progress_can_afford(ProgressUpgradeId_t id);
int  progress_purchase(ProgressUpgradeId_t id);

const char* progress_get_upgrade_name(ProgressUpgradeId_t id);
const char* progress_get_upgrade_detail(ProgressUpgradeId_t id);
const char* progress_get_tier_desc(ProgressUpgradeId_t id, int tier);

int  progress_has_affordable_upgrade(void);
int  progress_has_affordable_upgrade_for(EnemyType_t type);

void progress_bank_upgrade_points(int points);
int  progress_get_upgrade_points(void);

// Global upgrades
int         global_get_tier(GlobalUpgradeId_t id);
int         global_get_max_tier(GlobalUpgradeId_t id);
int         global_get_next_cost(GlobalUpgradeId_t id);
int         global_can_afford(GlobalUpgradeId_t id);
int         global_purchase(GlobalUpgradeId_t id);
int         global_has_affordable(void);

const char* global_get_name(GlobalUpgradeId_t id);
const char* global_get_desc(GlobalUpgradeId_t id);
const char* global_get_detail(GlobalUpgradeId_t id);
const char* global_get_tier_desc(GlobalUpgradeId_t id, int tier);

// Global upgrade gameplay queries
float global_get_buff_duration_bonus(void);
int   global_get_shield_hits_bonus(void);
float global_get_dash_cooldown_reduction(void);
int   global_has_dash_extra_charge(void);
int   global_get_health_pickup_bonus(void);
int   global_get_uncommon_weight_bonus(void);
int   global_get_rare_weight_bonus(void);
int   global_get_rerolls_per_run(void);
int   global_get_xp_reduction(void);
int   global_get_max_hp_bonus(void);
int   global_get_toughness(void);
float global_get_xp_magnet_bonus(void);
float global_get_regen_interval(void);
int   global_get_starting_shield(void);
int   global_get_extra_choices(void);
int   global_has_extra_weapon_slot(void);
int   global_has_retaliation_kb(void);
float global_get_retaliation_stun(void);
int   global_get_starting_xp(void);
float global_get_pickup_magnet_bonus(void);
float global_get_pity_reduction(void);
float global_get_pickup_lifetime_bonus(void);
int   global_has_revive(void);
int   global_get_thorns_tier(void);
int   global_has_bounty(void);

// Gameplay effect queries
int   progress_get_dmg_reduction(EnemyType_t type);
int   progress_get_grunt_explosion_tier(void);
float progress_get_grunt_explosion_radius(void);
float progress_get_dasher_stun_duration(void);
float progress_get_brute_speed_effectiveness(void);
float progress_get_corpse_lifetime(void);
float progress_get_blood_lifetime(void);
int   progress_get_heal_reduction(void);
int   progress_get_snake_dmg_reduction(void);
int   progress_get_snake_shatter_projectiles(void);
int   progress_get_snake_shatter_pierces(void);
int   progress_is_snake_discovered(void);
int   progress_get_beholder_dmg_reduction(void);
float progress_get_beholder_telegraph_bonus(void);
float progress_get_beholder_aim_slow(void);
int   progress_is_beholder_discovered(void);
int   progress_get_mimic_dmg_reduction(void);
float progress_get_mimic_cooldown_mult(void);
int   progress_is_mimic_discovered(void);

// ============================================================================
// Weapon Meta-Progression
// ============================================================================

typedef enum {
  WID_WAND, WID_SPIN, WID_CHAIN, WID_ORBIT,
  WID_BOMB, WID_TURRET, WID_TRAIL, WID_SCYTHE, WID_COUNT
} WeaponId_t;

typedef enum {
  WPROG_WAND_COOLDOWN, WPROG_WAND_REACH, WPROG_WAND_FREE_UPG, WPROG_WAND_RARITY, WPROG_WAND_EXTRA_CHOICE,
  WPROG_SPIN_COOLDOWN, WPROG_SPIN_REACH, WPROG_SPIN_FREE_UPG, WPROG_SPIN_RARITY, WPROG_SPIN_EXTRA_CHOICE,
  WPROG_CHAIN_COOLDOWN, WPROG_CHAIN_REACH, WPROG_CHAIN_FREE_UPG, WPROG_CHAIN_RARITY, WPROG_CHAIN_EXTRA_CHOICE,
  WPROG_ORBIT_COOLDOWN, WPROG_ORBIT_REACH, WPROG_ORBIT_FREE_UPG, WPROG_ORBIT_RARITY, WPROG_ORBIT_EXTRA_CHOICE, WPROG_ORBIT_SHATTER,
  WPROG_BOMB_COOLDOWN, WPROG_BOMB_REACH, WPROG_BOMB_FREE_UPG, WPROG_BOMB_RARITY, WPROG_BOMB_EXTRA_CHOICE,
  WPROG_TURRET_COOLDOWN, WPROG_TURRET_REACH, WPROG_TURRET_FREE_UPG, WPROG_TURRET_RARITY, WPROG_TURRET_EXTRA_CHOICE, WPROG_TURRET_DURATION,
  WPROG_TRAIL_COOLDOWN, WPROG_TRAIL_REACH, WPROG_TRAIL_FREE_UPG, WPROG_TRAIL_RARITY, WPROG_TRAIL_EXTRA_CHOICE,
  WPROG_SCYTHE_COOLDOWN, WPROG_SCYTHE_REACH, WPROG_SCYTHE_FREE_UPG, WPROG_SCYTHE_RARITY, WPROG_SCYTHE_EXTRA_CHOICE,
  WPROG_COUNT
} WeaponProgressId_t;

void  progress_bank_weapon_hits(int hits[WID_COUNT]);
void  progress_bank_weapon_kills(int kills[WID_COUNT]);
void  progress_add_weapon_bonus(WeaponId_t id, int pts);
int   progress_get_weapon_lifetime_hits(WeaponId_t id);
int   progress_get_weapon_lifetime_kills(WeaponId_t id);
int   progress_get_weapon_available_points(WeaponId_t id);

int   wprog_get_tier(WeaponProgressId_t id);
int   wprog_get_max_tier(WeaponProgressId_t id);
int   wprog_get_next_cost(WeaponProgressId_t id);
int   wprog_can_afford(WeaponProgressId_t id);
int   wprog_purchase(WeaponProgressId_t id);

const char* wprog_get_name(WeaponProgressId_t id);
const char* wprog_get_detail(WeaponProgressId_t id);
const char* wprog_get_tier_desc(WeaponProgressId_t id, int tier);
const char* wprog_get_weapon_name(WeaponId_t wid);

int   wprog_has_affordable(void);
int   wprog_has_affordable_for(WeaponId_t wid);

// Special source lifetime hit tracking (fire cone, conductor)
void  progress_bank_source_hits(int fire_cone_hits, int conductor_hits);
int   progress_get_fire_cone_lifetime_hits(void);
int   progress_get_conductor_lifetime_hits(void);

// Gameplay queries (called by weapons.c / upgrades.c)
float wprog_get_cooldown_mult(WeaponId_t wid);
float wprog_get_reach_bonus(WeaponId_t wid);
int   wprog_has_free_upgrade(WeaponId_t wid);
int   wprog_get_rarity_uncommon_bonus(WeaponId_t wid);
int   wprog_get_rarity_rare_bonus(WeaponId_t wid);
int   wprog_get_extra_choices(WeaponId_t wid);
int   wprog_get_orbit_shatter_bonus(void);
float wprog_get_turret_duration_bonus(void);

#endif /* PROGRESS_H */
