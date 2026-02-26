#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

#include "progress.h"

// Achievement types — determines which point currency is rewarded
typedef enum {
  ACH_TYPE_GENERAL,   // rewards global upgrade points
  ACH_TYPE_ENEMY,     // rewards enemy-specific kill points
  ACH_TYPE_WEAPON,    // rewards weapon-specific points
} AchType_t;

// Individual achievement definition
typedef struct {
  const char* name;
  const char* desc;
  AchType_t   type;
  int         reward;         // points awarded
  int         reward_index;   // enemy type or weapon id (for ENEMY/WEAPON types, -1 for GENERAL)
} AchDef_t;

// Achievement IDs
typedef enum {
  // General achievements — survive X minutes
  ACH_SURVIVE_1MIN,
  ACH_SURVIVE_3MIN,
  ACH_SURVIVE_5MIN,

  // General achievements — reach level milestones
  ACH_REACH_LEVEL_3,
  ACH_REACH_LEVEL_6,
  ACH_REACH_LEVEL_12,
  ACH_REACH_LEVEL_15,

  // General achievements — healing
  ACH_HEAL_100_IN_RUN,

  // Enemy achievements — first kill of each type
  ACH_FIRST_KILL_GRUNT,
  ACH_FIRST_KILL_DASHER,
  ACH_FIRST_KILL_BRUTE,
  ACH_FIRST_KILL_SHAMAN,
  ACH_FIRST_KILL_SNAKE,
  ACH_FIRST_KILL_BEHOLDER,
  ACH_FIRST_KILL_MIMIC,

  // Pain achievements — die to each enemy type (shaman is special)
  ACH_PAIN_GRUNT,
  ACH_PAIN_DASHER,
  ACH_PAIN_BRUTE,
  ACH_PAIN_SHAMAN,    // Watch a shaman heal 10+ HP at once
  ACH_PAIN_SNAKE,
  ACH_PAIN_BEHOLDER,
  ACH_PAIN_MIMIC,

  // Weapon achievements — 100 hits with each weapon
  ACH_WAND_100_HITS,
  ACH_SPIN_100_HITS,
  ACH_CHAIN_100_HITS,
  ACH_ORBIT_100_HITS,
  ACH_BOMB_100_HITS,
  ACH_TURRET_100_HITS,
  ACH_TRAIL_100_HITS,
  ACH_SCYTHE_100_HITS,

  // Weapon achievements — 100 kills with each weapon
  ACH_WAND_100_KILLS,
  ACH_SPIN_100_KILLS,
  ACH_CHAIN_100_KILLS,
  ACH_ORBIT_100_KILLS,
  ACH_BOMB_100_KILLS,
  ACH_TURRET_100_KILLS,
  ACH_TRAIL_100_KILLS,
  ACH_SCYTHE_100_KILLS,

  // Weapon achievements — 1000 hits with each weapon
  ACH_WAND_1000_HITS,
  ACH_SPIN_1000_HITS,
  ACH_CHAIN_1000_HITS,
  ACH_ORBIT_1000_HITS,
  ACH_BOMB_1000_HITS,
  ACH_TURRET_1000_HITS,
  ACH_TRAIL_1000_HITS,
  ACH_SCYTHE_1000_HITS,

  // Weapon achievements — 500 kills with each weapon
  ACH_WAND_500_KILLS,
  ACH_SPIN_500_KILLS,
  ACH_CHAIN_500_KILLS,
  ACH_ORBIT_500_KILLS,
  ACH_BOMB_500_KILLS,
  ACH_TURRET_500_KILLS,
  ACH_TRAIL_500_KILLS,
  ACH_SCYTHE_500_KILLS,

  // Weapon mastery — get a T3 common upgrade on each weapon
  ACH_WAND_T3_COMMON,
  ACH_SPIN_T3_COMMON,
  ACH_CHAIN_T3_COMMON,
  ACH_ORBIT_T3_COMMON,
  ACH_BOMB_T3_COMMON,
  ACH_TURRET_T3_COMMON,
  ACH_TRAIL_T3_COMMON,
  ACH_SCYTHE_T3_COMMON,

  // Weapon mastery — get a T3 uncommon upgrade on each weapon
  ACH_WAND_T3_UNCOMMON,
  ACH_SPIN_T3_UNCOMMON,
  ACH_CHAIN_T3_UNCOMMON,
  ACH_ORBIT_T3_UNCOMMON,
  ACH_BOMB_T3_UNCOMMON,
  ACH_TURRET_T3_UNCOMMON,
  ACH_TRAIL_T3_UNCOMMON,
  ACH_SCYTHE_T3_UNCOMMON,

  // Weapon mastery — get a T3 rare upgrade on each weapon
  ACH_WAND_T3_RARE,
  ACH_SPIN_T3_RARE,
  ACH_CHAIN_T3_RARE,
  ACH_ORBIT_T3_RARE,
  ACH_BOMB_T3_RARE,
  ACH_TURRET_T3_RARE,
  ACH_TRAIL_T3_RARE,
  ACH_SCYTHE_T3_RARE,

  // Boss speed-kill — kill within 30 seconds of spawning
  ACH_SPEEDKILL_SNAKE,
  ACH_SPEEDKILL_BEHOLDER,
  ACH_SPEEDKILL_MIMIC,

  // General achievements — combat & pickups
  ACH_KILL_500_IN_RUN,
  ACH_COLLECT_3_FIRE,
  ACH_COLLECT_3_SPEED,
  ACH_COLLECT_3_ICE,
  ACH_LOSE_10_SHIELD,
  ACH_FIRE_10SEC,
  ACH_ICE_10SEC,

  ACH_LOSE_30_SHIELD,
  ACH_HEAL_300_IN_RUN,

  // Weapon challenges — one unique challenge per weapon
  ACH_CHALLENGE_WAND,     // Bullet Hell: fire 15+ projectiles in 3 seconds
  ACH_CHALLENGE_SPIN,     // Eye of the Storm: hit 10+ enemies in a single pulse
  ACH_CHALLENGE_CHAIN,    // Lightning Rod: kill 3 enemies in a single chain
  ACH_CHALLENGE_ORBIT,    // Orbital Supremacy: kill 8 enemies in a single orbit
  ACH_CHALLENGE_BOMB,     // Carpet Bomber: hit 10 enemies in a single explosion
  ACH_CHALLENGE_TURRET,   // Turret Defense: 4+ turrets active at once
  ACH_CHALLENGE_TRAIL,    // Scorched Earth: damage 10 enemies in a single trail
  ACH_CHALLENGE_SCYTHE,   // Grim Reaper: kill 4 enemies in a single sweep

  // Mimic retrieval — retrieve each weapon type from a mimic
  ACH_RETRIEVE_WAND,
  ACH_RETRIEVE_SPIN,
  ACH_RETRIEVE_CHAIN,
  ACH_RETRIEVE_ORBIT,
  ACH_RETRIEVE_BOMB,
  ACH_RETRIEVE_TURRET,
  ACH_RETRIEVE_TRAIL,
  ACH_RETRIEVE_SCYTHE,

  // Special source achievements — 100 hits
  ACH_FIRE_CONE_100_HITS,
  ACH_CONDUCTOR_100_HITS,

  // Progression-gated achievements
  ACH_SPEED_RUNNER,       // Reach level 5 before 60 seconds
  ACH_QUICK_STUDY,        // Reach level 10 before 3 minutes
  ACH_UNKILLABLE,         // Survive 8 minutes
  ACH_IRON_WALL,          // Block 50 shield hits in a single run
  ACH_ARSENAL,            // Have 5 different weapons active at once

  // Shame
  ACH_LOSER,

  ACH_COUNT
} AchId_t;

// Initialization (loads unlock state from save)
void achievements_init(void);
void achievements_save(void);

// Check + unlock (called during gameplay)
void achievements_check_all(void);

// Query
int         achievements_is_unlocked(AchId_t id);
const AchDef_t* achievements_get_def(AchId_t id);
int         achievements_get_total_count(void);
int         achievements_get_unlocked_count(void);

// Filter helpers for the UI
int achievements_count_by_type(AchType_t type);
int achievements_count_unlocked_by_type(AchType_t type);

// Notify: boss killed within time limit (called from collision/snake death)
void achievements_notify_boss_speedkill(int enemy_type);

// Notify: weapon challenge events (called from weapons.c)
void achievements_notify_weapon_challenge(int weapon_type, int value);

// Notify: weapon retrieved from mimic (called from weapons.c)
void achievements_notify_weapon_retrieve(int weapon_type);

// Popup notification system
void achievements_clear_popups(void);
void achievements_update_popup(float dt);
void achievements_draw_popup(void);

#endif /* ACHIEVEMENTS_H */
