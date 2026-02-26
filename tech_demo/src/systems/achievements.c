#include "achievements.h"
#include "save_data.h"
#include "stats.h"
#include "progress.h"
#include "game_director.h"
#include "weapons.h"
#include "upgrades.h"
#include "xp.h"
#include "player_actions.h"
#include "pickups.h"
#include <stdio.h>
#include <math.h>

// ============================================================================
// Achievement definitions
// ============================================================================

static const AchDef_t ach_defs[ACH_COUNT] = {
  // General — survive X minutes
  [ACH_SURVIVE_1MIN] = {
    .name = "Baby Steps",
    .desc = "Survive for 1 minute",
    .type = ACH_TYPE_GENERAL, .reward = 5, .reward_index = -1
  },
  [ACH_SURVIVE_3MIN] = {
    .name = "Getting Warmed Up",
    .desc = "Survive for 3 minutes",
    .type = ACH_TYPE_GENERAL, .reward = 15, .reward_index = -1
  },
  [ACH_SURVIVE_5MIN] = {
    .name = "Seasoned Survivor",
    .desc = "Survive for 5 minutes",
    .type = ACH_TYPE_GENERAL, .reward = 30, .reward_index = -1
  },

  // General — reach level milestones
  [ACH_REACH_LEVEL_3] = {
    .name = "Getting Started",
    .desc = "Reach level 3 in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 5, .reward_index = -1
  },
  [ACH_REACH_LEVEL_6] = {
    .name = "Picking Up Steam",
    .desc = "Reach level 6 in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 10, .reward_index = -1
  },
  [ACH_REACH_LEVEL_12] = {
    .name = "Powerhouse",
    .desc = "Reach level 12 in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 20, .reward_index = -1
  },
  [ACH_REACH_LEVEL_15] = {
    .name = "Unstoppable",
    .desc = "Reach level 15 in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 30, .reward_index = -1
  },

  // General — healing
  [ACH_HEAL_100_IN_RUN] = {
    .name = "Health Hoarder",
    .desc = "Heal 100 HP in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 15, .reward_index = -1
  },

  // Enemy — first kill of each type
  [ACH_FIRST_KILL_GRUNT] = {
    .name = "Grunt Work",
    .desc = "Kill your first Grunt",
    .type = ACH_TYPE_ENEMY, .reward = 15, .reward_index = ENEMY_TYPE_GRUNT
  },
  [ACH_FIRST_KILL_DASHER] = {
    .name = "Speed Bump",
    .desc = "Kill your first Dasher",
    .type = ACH_TYPE_ENEMY, .reward = 10, .reward_index = ENEMY_TYPE_DASHER
  },
  [ACH_FIRST_KILL_BRUTE] = {
    .name = "Brute Force",
    .desc = "Kill your first Brute",
    .type = ACH_TYPE_ENEMY, .reward = 10, .reward_index = ENEMY_TYPE_BRUTE
  },
  [ACH_FIRST_KILL_SHAMAN] = {
    .name = "Witch Doctor",
    .desc = "Kill your first Shaman",
    .type = ACH_TYPE_ENEMY, .reward = 10, .reward_index = ENEMY_TYPE_SHAMAN
  },
  [ACH_FIRST_KILL_SNAKE] = {
    .name = "Snake Slayer",
    .desc = "Kill your first Snake",
    .type = ACH_TYPE_ENEMY, .reward = 10, .reward_index = ENEMY_TYPE_SNAKE
  },
  [ACH_FIRST_KILL_BEHOLDER] = {
    .name = "Eye for an Eye",
    .desc = "Kill your first Beholder",
    .type = ACH_TYPE_ENEMY, .reward = 15, .reward_index = ENEMY_TYPE_BEHOLDER
  },
  [ACH_FIRST_KILL_MIMIC] = {
    .name = "Copycat",
    .desc = "Kill your first Mimic",
    .type = ACH_TYPE_ENEMY, .reward = 15, .reward_index = ENEMY_TYPE_MIMIC
  },

  // Pain — die to each enemy type (shaman is special)
  [ACH_PAIN_GRUNT] = {
    .name = "Grunt's Revenge",
    .desc = "Die to a Grunt",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_GRUNT
  },
  [ACH_PAIN_DASHER] = {
    .name = "Too Slow",
    .desc = "Die to a Dasher",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_DASHER
  },
  [ACH_PAIN_BRUTE] = {
    .name = "Crushed",
    .desc = "Die to a Brute",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_BRUTE
  },
  [ACH_PAIN_SHAMAN] = {
    .name = "Unholy Mending",
    .desc = "Watch a Shaman heal 10+ HP at once",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_SHAMAN
  },
  [ACH_PAIN_SNAKE] = {
    .name = "Eaten Alive",
    .desc = "Die to a Snake",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_SNAKE
  },
  [ACH_PAIN_BEHOLDER] = {
    .name = "Disintegrated",
    .desc = "Die to a Beholder",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_BEHOLDER
  },
  [ACH_PAIN_MIMIC] = {
    .name = "Taste of Your Own Medicine",
    .desc = "Die to a Mimic",
    .type = ACH_TYPE_ENEMY, .reward = 0, .reward_index = ENEMY_TYPE_MIMIC
  },

  // Weapon — 100 hits
  [ACH_WAND_100_HITS] = {
    .name = "Apprentice Mage",
    .desc = "Land 100 hits with the Wand",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_WAND
  },
  [ACH_SPIN_100_HITS] = {
    .name = "Spin to Win",
    .desc = "Land 100 hits with Spin",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_100_HITS] = {
    .name = "Chain Reaction",
    .desc = "Land 100 hits with Chain",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_100_HITS] = {
    .name = "Orbital Strike",
    .desc = "Land 100 hits with Orbit",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_100_HITS] = {
    .name = "Demolitions Expert",
    .desc = "Land 100 hits with Bomb",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_BOMB
  },
  [ACH_TURRET_100_HITS] = {
    .name = "Turret Syndrome",
    .desc = "Land 100 hits with Turret",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_100_HITS] = {
    .name = "Trailblazer",
    .desc = "Land 100 hits with Trail",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_100_HITS] = {
    .name = "Reaper's Apprentice",
    .desc = "Land 100 hits with Scythe",
    .type = ACH_TYPE_WEAPON, .reward = 20, .reward_index = WID_SCYTHE
  },

  // Weapon — 100 kills
  [ACH_WAND_100_KILLS] = {
    .name = "Wand Executioner",
    .desc = "Get 100 kills with the Wand",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_WAND
  },
  [ACH_SPIN_100_KILLS] = {
    .name = "Spin Cycle",
    .desc = "Get 100 kills with Spin",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_100_KILLS] = {
    .name = "Chain Gang",
    .desc = "Get 100 kills with Chain",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_100_KILLS] = {
    .name = "Orbital Bombardment",
    .desc = "Get 100 kills with Orbit",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_100_KILLS] = {
    .name = "Demolition Derby",
    .desc = "Get 100 kills with Bomb",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_BOMB
  },
  [ACH_TURRET_100_KILLS] = {
    .name = "Automated Warfare",
    .desc = "Get 100 kills with Turret",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_100_KILLS] = {
    .name = "Trail of Destruction",
    .desc = "Get 100 kills with Trail",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_100_KILLS] = {
    .name = "Death Incarnate",
    .desc = "Get 100 kills with Scythe",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_SCYTHE
  },

  // Weapon — 1000 hits
  [ACH_WAND_1000_HITS] = {
    .name = "Archmage",
    .desc = "Land 1000 hits with the Wand",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_WAND
  },
  [ACH_SPIN_1000_HITS] = {
    .name = "Cyclone",
    .desc = "Land 1000 hits with Spin",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_1000_HITS] = {
    .name = "Thunder God",
    .desc = "Land 1000 hits with Chain",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_1000_HITS] = {
    .name = "Gravity Well",
    .desc = "Land 1000 hits with Orbit",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_1000_HITS] = {
    .name = "Shock and Awe",
    .desc = "Land 1000 hits with Bomb",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_BOMB
  },
  [ACH_TURRET_1000_HITS] = {
    .name = "Turret Overlord",
    .desc = "Land 1000 hits with Turret",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_1000_HITS] = {
    .name = "Path of Ruin",
    .desc = "Land 1000 hits with Trail",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_1000_HITS] = {
    .name = "Soul Harvester",
    .desc = "Land 1000 hits with Scythe",
    .type = ACH_TYPE_WEAPON, .reward = 40, .reward_index = WID_SCYTHE
  },

  // Weapon — 500 kills
  [ACH_WAND_500_KILLS] = {
    .name = "Wand Annihilator",
    .desc = "Get 500 kills with the Wand",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_WAND
  },
  [ACH_SPIN_500_KILLS] = {
    .name = "Spin Massacre",
    .desc = "Get 500 kills with Spin",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_500_KILLS] = {
    .name = "Chain Extinction",
    .desc = "Get 500 kills with Chain",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_500_KILLS] = {
    .name = "Orbital Genocide",
    .desc = "Get 500 kills with Orbit",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_500_KILLS] = {
    .name = "Total Annihilation",
    .desc = "Get 500 kills with Bomb",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_BOMB
  },
  [ACH_TURRET_500_KILLS] = {
    .name = "Turret Terminator",
    .desc = "Get 500 kills with Turret",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_500_KILLS] = {
    .name = "Trail of Corpses",
    .desc = "Get 500 kills with Trail",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_500_KILLS] = {
    .name = "Death Eternal",
    .desc = "Get 500 kills with Scythe",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_SCYTHE
  },

  // Weapon mastery — T3 common
  [ACH_WAND_T3_COMMON] = {
    .name = "Wand Fundamentals",
    .desc = "Max a common Wand upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_WAND
  },
  [ACH_SPIN_T3_COMMON] = {
    .name = "Spin Fundamentals",
    .desc = "Max a common Spin upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_T3_COMMON] = {
    .name = "Chain Fundamentals",
    .desc = "Max a common Chain upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_T3_COMMON] = {
    .name = "Orbit Fundamentals",
    .desc = "Max a common Orbit upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_T3_COMMON] = {
    .name = "Bomb Fundamentals",
    .desc = "Max a common Bomb upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_BOMB
  },
  [ACH_TURRET_T3_COMMON] = {
    .name = "Turret Fundamentals",
    .desc = "Max a common Turret upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_T3_COMMON] = {
    .name = "Trail Fundamentals",
    .desc = "Max a common Trail upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_T3_COMMON] = {
    .name = "Scythe Fundamentals",
    .desc = "Max a common Scythe upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 60, .reward_index = WID_SCYTHE
  },

  // Weapon mastery — T3 uncommon
  [ACH_WAND_T3_UNCOMMON] = {
    .name = "Wand Specialist",
    .desc = "Max an uncommon Wand upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_WAND
  },
  [ACH_SPIN_T3_UNCOMMON] = {
    .name = "Spin Specialist",
    .desc = "Max an uncommon Spin upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_T3_UNCOMMON] = {
    .name = "Chain Specialist",
    .desc = "Max an uncommon Chain upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_T3_UNCOMMON] = {
    .name = "Orbit Specialist",
    .desc = "Max an uncommon Orbit upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_T3_UNCOMMON] = {
    .name = "Bomb Specialist",
    .desc = "Max an uncommon Bomb upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_BOMB
  },
  [ACH_TURRET_T3_UNCOMMON] = {
    .name = "Turret Specialist",
    .desc = "Max an uncommon Turret upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_T3_UNCOMMON] = {
    .name = "Trail Specialist",
    .desc = "Max an uncommon Trail upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_T3_UNCOMMON] = {
    .name = "Scythe Specialist",
    .desc = "Max an uncommon Scythe upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 80, .reward_index = WID_SCYTHE
  },

  // Weapon mastery — T3 rare
  [ACH_WAND_T3_RARE] = {
    .name = "Wand Virtuoso",
    .desc = "Max a rare Wand upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_WAND
  },
  [ACH_SPIN_T3_RARE] = {
    .name = "Spin Virtuoso",
    .desc = "Max a rare Spin upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_SPIN
  },
  [ACH_CHAIN_T3_RARE] = {
    .name = "Chain Virtuoso",
    .desc = "Max a rare Chain upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_CHAIN
  },
  [ACH_ORBIT_T3_RARE] = {
    .name = "Orbit Virtuoso",
    .desc = "Max a rare Orbit upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_ORBIT
  },
  [ACH_BOMB_T3_RARE] = {
    .name = "Bomb Virtuoso",
    .desc = "Max a rare Bomb upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_BOMB
  },
  [ACH_TURRET_T3_RARE] = {
    .name = "Turret Virtuoso",
    .desc = "Max a rare Turret upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_TURRET
  },
  [ACH_TRAIL_T3_RARE] = {
    .name = "Trail Virtuoso",
    .desc = "Max a rare Trail upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_TRAIL
  },
  [ACH_SCYTHE_T3_RARE] = {
    .name = "Scythe Virtuoso",
    .desc = "Max a rare Scythe upgrade",
    .type = ACH_TYPE_WEAPON, .reward = 100, .reward_index = WID_SCYTHE
  },

  // Boss speed-kill — kill within 30 seconds
  [ACH_SPEEDKILL_SNAKE] = {
    .name = "Serpent Slaughter",
    .desc = "Kill a Snake within 30s of it spawning",
    .type = ACH_TYPE_ENEMY, .reward = 10, .reward_index = ENEMY_TYPE_SNAKE
  },
  [ACH_SPEEDKILL_BEHOLDER] = {
    .name = "Blink and You'll Miss It",
    .desc = "Kill a Beholder within 30s of it spawning",
    .type = ACH_TYPE_ENEMY, .reward = 8, .reward_index = ENEMY_TYPE_BEHOLDER
  },
  [ACH_SPEEDKILL_MIMIC] = {
    .name = "No Time for Tricks",
    .desc = "Kill a Mimic within 30s of it spawning",
    .type = ACH_TYPE_ENEMY, .reward = 12, .reward_index = ENEMY_TYPE_MIMIC
  },

  // General — combat & pickups
  [ACH_KILL_500_IN_RUN] = {
    .name = "Mass Extinction",
    .desc = "Kill 500 enemies in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 25, .reward_index = -1
  },
  [ACH_COLLECT_3_FIRE] = {
    .name = "Pyromancer",
    .desc = "Collect 3 fire powerups in one run",
    .type = ACH_TYPE_GENERAL, .reward = 10, .reward_index = -1
  },
  [ACH_COLLECT_3_SPEED] = {
    .name = "Speed Demon",
    .desc = "Collect 3 speed powerups in one run",
    .type = ACH_TYPE_GENERAL, .reward = 10, .reward_index = -1
  },
  [ACH_COLLECT_3_ICE] = {
    .name = "Frost Walker",
    .desc = "Collect 3 ice powerups in one run",
    .type = ACH_TYPE_GENERAL, .reward = 10, .reward_index = -1
  },
  [ACH_LOSE_10_SHIELD] = {
    .name = "Bulletproof",
    .desc = "Lose 10 shield hits in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 15, .reward_index = -1
  },
  [ACH_FIRE_10SEC] = {
    .name = "Burning Man",
    .desc = "Keep fire active for 10 seconds",
    .type = ACH_TYPE_GENERAL, .reward = 15, .reward_index = -1
  },
  [ACH_ICE_10SEC] = {
    .name = "Permafrost",
    .desc = "Keep ice active for 10 seconds",
    .type = ACH_TYPE_GENERAL, .reward = 15, .reward_index = -1
  },

  [ACH_LOSE_30_SHIELD] = {
    .name = "Walking Fortress",
    .desc = "Lose 30 shield hits in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 25, .reward_index = -1
  },
  [ACH_HEAL_300_IN_RUN] = {
    .name = "Immortal",
    .desc = "Heal 300 HP in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 25, .reward_index = -1
  },

  // Weapon challenges — one unique challenge per weapon
  [ACH_CHALLENGE_WAND] = {
    .name = "Bullet Hell",
    .desc = "Fire 15+ wand projectiles in 3 seconds",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_WAND
  },
  [ACH_CHALLENGE_SPIN] = {
    .name = "Eye of the Storm",
    .desc = "Hit 10+ enemies with a single spin pulse",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_SPIN
  },
  [ACH_CHALLENGE_CHAIN] = {
    .name = "Lightning Rod",
    .desc = "Kill 3 enemies in a single chain",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_CHAIN
  },
  [ACH_CHALLENGE_ORBIT] = {
    .name = "Orbital Supremacy",
    .desc = "Kill 8 enemies during a single orbit",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_ORBIT
  },
  [ACH_CHALLENGE_BOMB] = {
    .name = "Carpet Bomber",
    .desc = "Hit 10 enemies with a single explosion",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_BOMB
  },
  [ACH_CHALLENGE_TURRET] = {
    .name = "Turret Defense",
    .desc = "Have 4+ turrets active at once",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_TURRET
  },
  [ACH_CHALLENGE_TRAIL] = {
    .name = "Scorched Earth",
    .desc = "Damage 10 enemies in a single trail",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_TRAIL
  },
  [ACH_CHALLENGE_SCYTHE] = {
    .name = "Grim Reaper",
    .desc = "Kill 4 enemies in a single scythe sweep",
    .type = ACH_TYPE_WEAPON, .reward = 75, .reward_index = WID_SCYTHE
  },

  // Mimic retrieval — retrieve each weapon type from a mimic
  [ACH_RETRIEVE_WAND] = {
    .name = "Wand Reclaimed",
    .desc = "Retrieve a stolen Wand from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_WAND
  },
  [ACH_RETRIEVE_SPIN] = {
    .name = "Spin Reclaimed",
    .desc = "Retrieve a stolen Spin from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_SPIN
  },
  [ACH_RETRIEVE_CHAIN] = {
    .name = "Chain Reclaimed",
    .desc = "Retrieve a stolen Chain from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_CHAIN
  },
  [ACH_RETRIEVE_ORBIT] = {
    .name = "Orbit Reclaimed",
    .desc = "Retrieve a stolen Orbit from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_ORBIT
  },
  [ACH_RETRIEVE_BOMB] = {
    .name = "Bomb Reclaimed",
    .desc = "Retrieve a stolen Bomb from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_BOMB
  },
  [ACH_RETRIEVE_TURRET] = {
    .name = "Turret Reclaimed",
    .desc = "Retrieve a stolen Turret from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_TURRET
  },
  [ACH_RETRIEVE_TRAIL] = {
    .name = "Trail Reclaimed",
    .desc = "Retrieve a stolen Trail from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_TRAIL
  },
  [ACH_RETRIEVE_SCYTHE] = {
    .name = "Scythe Reclaimed",
    .desc = "Retrieve a stolen Scythe from a Mimic",
    .type = ACH_TYPE_WEAPON, .reward = 50, .reward_index = WID_SCYTHE
  },

  // Special source achievements — 100 hits
  [ACH_FIRE_CONE_100_HITS] = {
    .name = "Pyromaniac",
    .desc = "Land 100 hits with the Fire Cone",
    .type = ACH_TYPE_GENERAL, .reward = 20, .reward_index = -1
  },
  [ACH_CONDUCTOR_100_HITS] = {
    .name = "Live Wire",
    .desc = "Land 100 hits with Conductor",
    .type = ACH_TYPE_GENERAL, .reward = 20, .reward_index = -1
  },

  // Progression-gated achievements
  [ACH_SPEED_RUNNER] = {
    .name = "Speed Runner",
    .desc = "Reach level 5 before 60 seconds",
    .type = ACH_TYPE_GENERAL, .reward = 20, .reward_index = -1
  },
  [ACH_QUICK_STUDY] = {
    .name = "Quick Study",
    .desc = "Reach level 10 before 3 minutes",
    .type = ACH_TYPE_GENERAL, .reward = 30, .reward_index = -1
  },
  [ACH_UNKILLABLE] = {
    .name = "Unkillable",
    .desc = "Survive for 8 minutes",
    .type = ACH_TYPE_GENERAL, .reward = 50, .reward_index = -1
  },
  [ACH_IRON_WALL] = {
    .name = "Iron Wall",
    .desc = "Block 50 hits with shields in a single run",
    .type = ACH_TYPE_GENERAL, .reward = 30, .reward_index = -1
  },
  [ACH_ARSENAL] = {
    .name = "Arsenal",
    .desc = "Have 5 different weapons active at once",
    .type = ACH_TYPE_GENERAL, .reward = 40, .reward_index = -1
  },

  // Shame
  [ACH_LOSER] = {
    .name = "Loser",
    .desc = "Die before 60 seconds",
    .type = ACH_TYPE_GENERAL, .reward = -1, .reward_index = -1
  },
};

// ============================================================================
// Unlock state
// ============================================================================

static int unlocked[ACH_COUNT];

static const char* save_keys[ACH_COUNT] = {
  [ACH_SURVIVE_1MIN]       = "ach_surv1",
  [ACH_SURVIVE_3MIN]       = "ach_surv3",
  [ACH_SURVIVE_5MIN]       = "ach_surv5",
  [ACH_REACH_LEVEL_3]      = "ach_lv3",
  [ACH_REACH_LEVEL_6]      = "ach_lv6",
  [ACH_REACH_LEVEL_12]     = "ach_lv12",
  [ACH_REACH_LEVEL_15]     = "ach_lv15",
  [ACH_HEAL_100_IN_RUN]    = "ach_heal100",
  [ACH_FIRST_KILL_GRUNT]   = "ach_fk_gr",
  [ACH_FIRST_KILL_DASHER]  = "ach_fk_da",
  [ACH_FIRST_KILL_BRUTE]   = "ach_fk_br",
  [ACH_FIRST_KILL_SHAMAN]  = "ach_fk_sh",
  [ACH_FIRST_KILL_SNAKE]   = "ach_fk_sn",
  [ACH_FIRST_KILL_BEHOLDER]= "ach_fk_be",
  [ACH_FIRST_KILL_MIMIC]   = "ach_fk_mi",
  [ACH_PAIN_GRUNT]         = "ach_pn_gr",
  [ACH_PAIN_DASHER]        = "ach_pn_da",
  [ACH_PAIN_BRUTE]         = "ach_pn_br",
  [ACH_PAIN_SHAMAN]        = "ach_pn_sh",
  [ACH_PAIN_SNAKE]         = "ach_pn_sn",
  [ACH_PAIN_BEHOLDER]      = "ach_pn_be",
  [ACH_PAIN_MIMIC]         = "ach_pn_mi",
  [ACH_WAND_100_HITS]      = "ach_w_wan",
  [ACH_SPIN_100_HITS]      = "ach_w_spn",
  [ACH_CHAIN_100_HITS]     = "ach_w_chn",
  [ACH_ORBIT_100_HITS]     = "ach_w_orb",
  [ACH_BOMB_100_HITS]      = "ach_w_bmb",
  [ACH_TURRET_100_HITS]    = "ach_w_tur",
  [ACH_TRAIL_100_HITS]     = "ach_w_trl",
  [ACH_SCYTHE_100_HITS]    = "ach_w_scy",
  [ACH_WAND_100_KILLS]     = "ach_k_wan",
  [ACH_SPIN_100_KILLS]     = "ach_k_spn",
  [ACH_CHAIN_100_KILLS]    = "ach_k_chn",
  [ACH_ORBIT_100_KILLS]    = "ach_k_orb",
  [ACH_BOMB_100_KILLS]     = "ach_k_bmb",
  [ACH_TURRET_100_KILLS]   = "ach_k_tur",
  [ACH_TRAIL_100_KILLS]    = "ach_k_trl",
  [ACH_SCYTHE_100_KILLS]   = "ach_k_scy",
  // 1000 hits
  [ACH_WAND_1000_HITS]     = "ach_h1k_wan",
  [ACH_SPIN_1000_HITS]     = "ach_h1k_spn",
  [ACH_CHAIN_1000_HITS]    = "ach_h1k_chn",
  [ACH_ORBIT_1000_HITS]    = "ach_h1k_orb",
  [ACH_BOMB_1000_HITS]     = "ach_h1k_bmb",
  [ACH_TURRET_1000_HITS]   = "ach_h1k_tur",
  [ACH_TRAIL_1000_HITS]    = "ach_h1k_trl",
  [ACH_SCYTHE_1000_HITS]   = "ach_h1k_scy",
  // 500 kills
  [ACH_WAND_500_KILLS]     = "ach_k5_wan",
  [ACH_SPIN_500_KILLS]     = "ach_k5_spn",
  [ACH_CHAIN_500_KILLS]    = "ach_k5_chn",
  [ACH_ORBIT_500_KILLS]    = "ach_k5_orb",
  [ACH_BOMB_500_KILLS]     = "ach_k5_bmb",
  [ACH_TURRET_500_KILLS]   = "ach_k5_tur",
  [ACH_TRAIL_500_KILLS]    = "ach_k5_trl",
  [ACH_SCYTHE_500_KILLS]   = "ach_k5_scy",
  // T3 common mastery
  [ACH_WAND_T3_COMMON]     = "ach_t3c_wan",
  [ACH_SPIN_T3_COMMON]     = "ach_t3c_spn",
  [ACH_CHAIN_T3_COMMON]    = "ach_t3c_chn",
  [ACH_ORBIT_T3_COMMON]    = "ach_t3c_orb",
  [ACH_BOMB_T3_COMMON]     = "ach_t3c_bmb",
  [ACH_TURRET_T3_COMMON]   = "ach_t3c_tur",
  [ACH_TRAIL_T3_COMMON]    = "ach_t3c_trl",
  [ACH_SCYTHE_T3_COMMON]   = "ach_t3c_scy",
  // T3 uncommon mastery
  [ACH_WAND_T3_UNCOMMON]   = "ach_t3u_wan",
  [ACH_SPIN_T3_UNCOMMON]   = "ach_t3u_spn",
  [ACH_CHAIN_T3_UNCOMMON]  = "ach_t3u_chn",
  [ACH_ORBIT_T3_UNCOMMON]  = "ach_t3u_orb",
  [ACH_BOMB_T3_UNCOMMON]   = "ach_t3u_bmb",
  [ACH_TURRET_T3_UNCOMMON] = "ach_t3u_tur",
  [ACH_TRAIL_T3_UNCOMMON]  = "ach_t3u_trl",
  [ACH_SCYTHE_T3_UNCOMMON] = "ach_t3u_scy",
  // T3 rare mastery
  [ACH_WAND_T3_RARE]       = "ach_t3r_wan",
  [ACH_SPIN_T3_RARE]       = "ach_t3r_spn",
  [ACH_CHAIN_T3_RARE]      = "ach_t3r_chn",
  [ACH_ORBIT_T3_RARE]      = "ach_t3r_orb",
  [ACH_BOMB_T3_RARE]       = "ach_t3r_bmb",
  [ACH_TURRET_T3_RARE]     = "ach_t3r_tur",
  [ACH_TRAIL_T3_RARE]      = "ach_t3r_trl",
  [ACH_SCYTHE_T3_RARE]     = "ach_t3r_scy",
  // Boss speed-kills
  [ACH_SPEEDKILL_SNAKE]    = "ach_sk_sn",
  [ACH_SPEEDKILL_BEHOLDER] = "ach_sk_be",
  [ACH_SPEEDKILL_MIMIC]    = "ach_sk_mi",
  // General — combat & pickups
  [ACH_KILL_500_IN_RUN]    = "ach_k500",
  [ACH_COLLECT_3_FIRE]     = "ach_3fire",
  [ACH_COLLECT_3_SPEED]    = "ach_3spd",
  [ACH_COLLECT_3_ICE]      = "ach_3ice",
  [ACH_LOSE_10_SHIELD]     = "ach_10shld",
  [ACH_FIRE_10SEC]         = "ach_fire10",
  [ACH_ICE_10SEC]          = "ach_ice10",
  [ACH_LOSE_30_SHIELD]     = "ach_30shld",
  [ACH_HEAL_300_IN_RUN]    = "ach_heal300",
  // Weapon challenges
  [ACH_CHALLENGE_WAND]     = "ach_ch_wan",
  [ACH_CHALLENGE_SPIN]     = "ach_ch_spn",
  [ACH_CHALLENGE_CHAIN]    = "ach_ch_chn",
  [ACH_CHALLENGE_ORBIT]    = "ach_ch_orb",
  [ACH_CHALLENGE_BOMB]     = "ach_ch_bmb",
  [ACH_CHALLENGE_TURRET]   = "ach_ch_tur",
  [ACH_CHALLENGE_TRAIL]    = "ach_ch_trl",
  [ACH_CHALLENGE_SCYTHE]   = "ach_ch_scy",
  // Mimic retrieval
  [ACH_RETRIEVE_WAND]      = "ach_rv_wan",
  [ACH_RETRIEVE_SPIN]      = "ach_rv_spn",
  [ACH_RETRIEVE_CHAIN]     = "ach_rv_chn",
  [ACH_RETRIEVE_ORBIT]     = "ach_rv_orb",
  [ACH_RETRIEVE_BOMB]      = "ach_rv_bmb",
  [ACH_RETRIEVE_TURRET]    = "ach_rv_tur",
  [ACH_RETRIEVE_TRAIL]     = "ach_rv_trl",
  [ACH_RETRIEVE_SCYTHE]    = "ach_rv_scy",
  // Special source hits
  [ACH_FIRE_CONE_100_HITS] = "ach_fc_100",
  [ACH_CONDUCTOR_100_HITS] = "ach_cd_100",
  // Progression-gated
  [ACH_SPEED_RUNNER]       = "ach_spd_run",
  [ACH_QUICK_STUDY]        = "ach_qk_stdy",
  [ACH_UNKILLABLE]         = "ach_unkill",
  [ACH_IRON_WALL]          = "ach_iron_w",
  [ACH_ARSENAL]            = "ach_arsenal",
  [ACH_LOSER]              = "ach_loser",
};

// ============================================================================
// Sound
// ============================================================================

static aSoundEffect_t ach_sound;
static int ach_sound_loaded = 0;
static aSoundEffect_t ach_downer_sound;
static int ach_downer_loaded = 0;

static void play_ach_sound(AchId_t id)
{
  const AchDef_t* def = &ach_defs[id];
  aSoundEffect_t* snd = (def->reward <= 0 && ach_downer_loaded) ? &ach_downer_sound : &ach_sound;
  int loaded = (def->reward <= 0) ? ach_downer_loaded : ach_sound_loaded;
  if (loaded)
  {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 100,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(snd, &opts);
  }
}

// ============================================================================
// Popup notification queue
// ============================================================================

#define ACH_POPUP_MAX_QUEUE 8
#define ACH_POPUP_SLIDE_IN  0.4f   // seconds to slide in
#define ACH_POPUP_HOLD      3.0f   // seconds to hold on screen
#define ACH_POPUP_SLIDE_OUT 0.4f   // seconds to slide out
#define ACH_POPUP_TOTAL     (ACH_POPUP_SLIDE_IN + ACH_POPUP_HOLD + ACH_POPUP_SLIDE_OUT)
#define ACH_POPUP_W         280
#define ACH_POPUP_H         70

static AchId_t popup_queue[ACH_POPUP_MAX_QUEUE];
static int     popup_queue_count = 0;
static float   popup_timer       = -1.0f;  // <0 means no active popup

static void popup_push(AchId_t id)
{
  if (popup_queue_count < ACH_POPUP_MAX_QUEUE)
    popup_queue[popup_queue_count++] = id;
}

static void popup_advance(void)
{
  if (popup_queue_count > 1)
  {
    for (int i = 1; i < popup_queue_count; i++)
      popup_queue[i - 1] = popup_queue[i];
  }
  popup_queue_count--;
  popup_timer = -1.0f;
}

// ============================================================================
// Init / Save
// ============================================================================

void achievements_init(void)
{
  for (int i = 0; i < ACH_COUNT; i++)
    unlocked[i] = save_keys[i] ? save_data_get_int(save_keys[i], 0) : 0;

  if (!ach_sound_loaded)
  {
    if (a_AudioLoadSound("resources/soundEffects/achievementget.wav", &ach_sound) == 0)
      ach_sound_loaded = 1;
  }
  if (!ach_downer_loaded)
  {
    if (a_AudioLoadSound("resources/soundEffects/downer01.wav", &ach_downer_sound) == 0)
      ach_downer_loaded = 1;
  }

  popup_queue_count = 0;
  popup_timer = -1.0f;
}

void achievements_save(void)
{
  for (int i = 0; i < ACH_COUNT; i++)
    if (save_keys[i]) save_data_set_int(save_keys[i], unlocked[i]);
  save_data_flush();
}

// ============================================================================
// Internal: try to unlock, award points if newly unlocked
// ============================================================================

static int try_unlock(AchId_t id)
{
  if (unlocked[id]) return 0;
  unlocked[id] = 1;

  const AchDef_t* def = &ach_defs[id];
  switch (def->type)
  {
    case ACH_TYPE_GENERAL:
      progress_bank_upgrade_points(def->reward);
      break;
    case ACH_TYPE_ENEMY:
      progress_add_enemy_bonus((EnemyType_t)def->reward_index, def->reward);
      break;
    case ACH_TYPE_WEAPON:
      progress_add_weapon_bonus((WeaponId_t)def->reward_index, def->reward);
      break;
  }

  // Queue popup notification
  popup_push(id);

  return 1;
}

// ============================================================================
// Check all achievements (called at end of run or periodically)
// ============================================================================

void achievements_check_all(void)
{
  int changed = 0;

  // --- Survival achievements ---
  float elapsed = director_get_elapsed();
  if (elapsed >= 60.0f)  changed |= try_unlock(ACH_SURVIVE_1MIN);
  if (elapsed >= 180.0f) changed |= try_unlock(ACH_SURVIVE_3MIN);
  if (elapsed >= 300.0f) changed |= try_unlock(ACH_SURVIVE_5MIN);

  // --- Level milestone achievements ---
  {
    int level = xp_get_level();
    if (level >= 3)  changed |= try_unlock(ACH_REACH_LEVEL_3);
    if (level >= 6)  changed |= try_unlock(ACH_REACH_LEVEL_6);
    if (level >= 12) changed |= try_unlock(ACH_REACH_LEVEL_12);
    if (level >= 15) changed |= try_unlock(ACH_REACH_LEVEL_15);
  }

  // --- Healing achievements ---
  if (stats_get_run_hp_healed() >= 100)
    changed |= try_unlock(ACH_HEAL_100_IN_RUN);
  if (stats_get_run_hp_healed() >= 300)
    changed |= try_unlock(ACH_HEAL_300_IN_RUN);

  // --- Kill 500 in a run ---
  if (stats_get_kills() >= 500)
    changed |= try_unlock(ACH_KILL_500_IN_RUN);

  // --- Pickup collection achievements ---
  if (stats_get_pickup_collected(0) >= 3)  // PICKUP_FIRE_CONE
    changed |= try_unlock(ACH_COLLECT_3_FIRE);
  if (stats_get_pickup_collected(1) >= 3)  // PICKUP_SPEED
    changed |= try_unlock(ACH_COLLECT_3_SPEED);
  if (stats_get_pickup_collected(3) >= 3)  // PICKUP_SLOW_AURA (ice)
    changed |= try_unlock(ACH_COLLECT_3_ICE);

  // --- Shield hits lost ---
  if (stats_get_shield_hits_lost() >= 10)
    changed |= try_unlock(ACH_LOSE_10_SHIELD);
  if (stats_get_shield_hits_lost() >= 30)
    changed |= try_unlock(ACH_LOSE_30_SHIELD);

  // --- Buff duration achievements ---
  if (player_get_buff_duration(PICKUP_FIRE_CONE) >= 10.0f)
    changed |= try_unlock(ACH_FIRE_10SEC);
  if (player_get_buff_duration(PICKUP_SLOW_AURA) >= 10.0f)
    changed |= try_unlock(ACH_ICE_10SEC);

  // --- Progression-gated achievements ---
  {
    int level = xp_get_level();
    if (level >= 5 && elapsed < 60.0f)
      changed |= try_unlock(ACH_SPEED_RUNNER);
    if (level >= 10 && elapsed < 180.0f)
      changed |= try_unlock(ACH_QUICK_STUDY);
  }
  if (elapsed >= 480.0f)
    changed |= try_unlock(ACH_UNKILLABLE);
  if (stats_get_shield_hits_lost() >= 50)
    changed |= try_unlock(ACH_IRON_WALL);
  if (weapons_get_active_count() >= 5)
    changed |= try_unlock(ACH_ARSENAL);

  // --- Shame ---
  if (!player_is_alive() && elapsed < 60.0f)
    changed |= try_unlock(ACH_LOSER);

  // --- First kill achievements ---
  // Check both current-run kills and lifetime kills (for mid-game + end-of-run)
  {
    static const AchId_t kill_achs[ENEMY_TYPE_COUNT] = {
      ACH_FIRST_KILL_GRUNT, ACH_FIRST_KILL_DASHER, ACH_FIRST_KILL_BRUTE,
      ACH_FIRST_KILL_SHAMAN, ACH_FIRST_KILL_SNAKE, ACH_FIRST_KILL_BEHOLDER,
      ACH_FIRST_KILL_MIMIC,
    };
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
    {
      if (stats_get_kills_by_type((EnemyType_t)t) > 0 ||
          progress_get_lifetime_kills((EnemyType_t)t) > 0)
        changed |= try_unlock(kill_achs[t]);
    }
  }

  // --- Pain achievements — die to each enemy type ---
  {
    static const AchId_t pain_achs[ENEMY_TYPE_COUNT] = {
      ACH_PAIN_GRUNT, ACH_PAIN_DASHER, ACH_PAIN_BRUTE,
      ACH_PAIN_SHAMAN, ACH_PAIN_SNAKE, ACH_PAIN_BEHOLDER,
      ACH_PAIN_MIMIC,
    };
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
    {
      if (t == ENEMY_TYPE_SHAMAN)
      {
        // Shaman pain: watch a shaman heal 10+ HP at once
        if (stats_get_shaman_max_single_heal() >= 10)
          changed |= try_unlock(ACH_PAIN_SHAMAN);
      }
      else
      {
        if (stats_get_deaths_by_type((EnemyType_t)t) > 0)
          changed |= try_unlock(pain_achs[t]);
      }
    }
  }

  // --- Weapon hit achievements (100 / 1000) ---
  // Check both current-run hits and lifetime hits
  {
    static const AchId_t hit_achs[WID_COUNT] = {
      ACH_WAND_100_HITS, ACH_SPIN_100_HITS, ACH_CHAIN_100_HITS, ACH_ORBIT_100_HITS,
      ACH_BOMB_100_HITS, ACH_TURRET_100_HITS, ACH_TRAIL_100_HITS, ACH_SCYTHE_100_HITS,
    };
    static const AchId_t hit_1k_achs[WID_COUNT] = {
      ACH_WAND_1000_HITS, ACH_SPIN_1000_HITS, ACH_CHAIN_1000_HITS, ACH_ORBIT_1000_HITS,
      ACH_BOMB_1000_HITS, ACH_TURRET_1000_HITS, ACH_TRAIL_1000_HITS, ACH_SCYTHE_1000_HITS,
    };
    for (int w = 0; w < WID_COUNT; w++)
    {
      int total = progress_get_weapon_lifetime_hits((WeaponId_t)w)
                + weapons_get_total_hits((WeaponType_t)(w + 1));
      if (total >= 100)
        changed |= try_unlock(hit_achs[w]);
      if (total >= 1000)
        changed |= try_unlock(hit_1k_achs[w]);
    }
  }

  // --- Special source hit achievements (fire cone, conductor) ---
  {
    int fc_total = progress_get_fire_cone_lifetime_hits()
                 + weapons_get_total_hits(SOURCE_FIRE_CONE);
    if (fc_total >= 100)
      changed |= try_unlock(ACH_FIRE_CONE_100_HITS);

    int cd_total = progress_get_conductor_lifetime_hits()
                 + weapons_get_total_hits(SOURCE_CONDUCTOR);
    if (cd_total >= 100)
      changed |= try_unlock(ACH_CONDUCTOR_100_HITS);
  }

  // --- Weapon kill achievements (100 / 500) ---
  // Check both current-run kills and lifetime kills
  {
    static const AchId_t kill_achs[WID_COUNT] = {
      ACH_WAND_100_KILLS, ACH_SPIN_100_KILLS, ACH_CHAIN_100_KILLS, ACH_ORBIT_100_KILLS,
      ACH_BOMB_100_KILLS, ACH_TURRET_100_KILLS, ACH_TRAIL_100_KILLS, ACH_SCYTHE_100_KILLS,
    };
    static const AchId_t kill_500_achs[WID_COUNT] = {
      ACH_WAND_500_KILLS, ACH_SPIN_500_KILLS, ACH_CHAIN_500_KILLS, ACH_ORBIT_500_KILLS,
      ACH_BOMB_500_KILLS, ACH_TURRET_500_KILLS, ACH_TRAIL_500_KILLS, ACH_SCYTHE_500_KILLS,
    };
    for (int w = 0; w < WID_COUNT; w++)
    {
      int total = progress_get_weapon_lifetime_kills((WeaponId_t)w)
                + stats_get_weapon_kills_by_id(w);
      if (total >= 100)
        changed |= try_unlock(kill_achs[w]);
      if (total >= 500)
        changed |= try_unlock(kill_500_achs[w]);
    }
  }

  // --- Weapon mastery — T3 common/uncommon/rare per weapon ---
  {
    // Achievement IDs indexed by [weapon_id][rarity]
    static const AchId_t mastery_achs[WID_COUNT][3] = {
      { ACH_WAND_T3_COMMON,   ACH_WAND_T3_UNCOMMON,   ACH_WAND_T3_RARE },
      { ACH_SPIN_T3_COMMON,   ACH_SPIN_T3_UNCOMMON,   ACH_SPIN_T3_RARE },
      { ACH_CHAIN_T3_COMMON,  ACH_CHAIN_T3_UNCOMMON,  ACH_CHAIN_T3_RARE },
      { ACH_ORBIT_T3_COMMON,  ACH_ORBIT_T3_UNCOMMON,  ACH_ORBIT_T3_RARE },
      { ACH_BOMB_T3_COMMON,   ACH_BOMB_T3_UNCOMMON,   ACH_BOMB_T3_RARE },
      { ACH_TURRET_T3_COMMON, ACH_TURRET_T3_UNCOMMON, ACH_TURRET_T3_RARE },
      { ACH_TRAIL_T3_COMMON,  ACH_TRAIL_T3_UNCOMMON,  ACH_TRAIL_T3_RARE },
      { ACH_SCYTHE_T3_COMMON, ACH_SCYTHE_T3_UNCOMMON, ACH_SCYTHE_T3_RARE },
    };

    for (int w = 0; w < WID_COUNT; w++)
    {
      int weapon_type = w + 1; // WeaponType_t = WID + 1
      UpgradeId_t ids[UPG_COUNT];
      int count = upgrades_get_for_weapon(weapon_type, ids, UPG_COUNT);
      int has_t3[3] = {0, 0, 0}; // common, uncommon, rare

      for (int u = 0; u < count; u++)
      {
        if (upgrades_get_tier(ids[u]) >= UPG_MAX_TIER)
        {
          const UpgradeInfo_t* info = upgrades_get_info(ids[u]);
          if (info && info->rarity >= 0 && info->rarity <= 2)
            has_t3[info->rarity] = 1;
        }
      }

      for (int r = 0; r < 3; r++)
        if (has_t3[r]) changed |= try_unlock(mastery_achs[w][r]);
    }
  }

  if (changed) achievements_save();
}

// ============================================================================
// Boss speed-kill notification
// ============================================================================

void achievements_notify_boss_speedkill(int enemy_type)
{
  int changed = 0;
  switch (enemy_type)
  {
    case ENEMY_TYPE_SNAKE:    changed = try_unlock(ACH_SPEEDKILL_SNAKE);    break;
    case ENEMY_TYPE_BEHOLDER: changed = try_unlock(ACH_SPEEDKILL_BEHOLDER); break;
    case ENEMY_TYPE_MIMIC:    changed = try_unlock(ACH_SPEEDKILL_MIMIC);    break;
  }
  if (changed) achievements_save();
}

// ============================================================================
// Weapon challenge notifications
// ============================================================================

void achievements_notify_weapon_challenge(int weapon_type, int value)
{
  int changed = 0;
  switch (weapon_type)
  {
    case WEAPON_WAND:   if (value >= 15) changed = try_unlock(ACH_CHALLENGE_WAND);   break;
    case WEAPON_SPIN:   if (value >= 10) changed = try_unlock(ACH_CHALLENGE_SPIN);   break;
    case WEAPON_CHAIN:  if (value >= 3)  changed = try_unlock(ACH_CHALLENGE_CHAIN);  break;
    case WEAPON_ORBIT:  if (value >= 8)  changed = try_unlock(ACH_CHALLENGE_ORBIT);  break;
    case WEAPON_BOMB:   if (value >= 10) changed = try_unlock(ACH_CHALLENGE_BOMB);   break;
    case WEAPON_TURRET: if (value >= 4)  changed = try_unlock(ACH_CHALLENGE_TURRET); break;
    case WEAPON_TRAIL:  if (value >= 10) changed = try_unlock(ACH_CHALLENGE_TRAIL);  break;
    case WEAPON_SCYTHE: if (value >= 4)  changed = try_unlock(ACH_CHALLENGE_SCYTHE); break;
  }
  if (changed) achievements_save();
}

// ============================================================================
// Weapon retrieval from mimic notifications
// ============================================================================

void achievements_notify_weapon_retrieve(int weapon_type)
{
  int changed = 0;
  switch (weapon_type)
  {
    case WEAPON_WAND:   changed = try_unlock(ACH_RETRIEVE_WAND);   break;
    case WEAPON_SPIN:   changed = try_unlock(ACH_RETRIEVE_SPIN);   break;
    case WEAPON_CHAIN:  changed = try_unlock(ACH_RETRIEVE_CHAIN);  break;
    case WEAPON_ORBIT:  changed = try_unlock(ACH_RETRIEVE_ORBIT);  break;
    case WEAPON_BOMB:   changed = try_unlock(ACH_RETRIEVE_BOMB);   break;
    case WEAPON_TURRET: changed = try_unlock(ACH_RETRIEVE_TURRET); break;
    case WEAPON_TRAIL:  changed = try_unlock(ACH_RETRIEVE_TRAIL);  break;
    case WEAPON_SCYTHE: changed = try_unlock(ACH_RETRIEVE_SCYTHE); break;
  }
  if (changed) achievements_save();
}

// ============================================================================
// Queries
// ============================================================================

int achievements_is_unlocked(AchId_t id)
{
  if (id < 0 || id >= ACH_COUNT) return 0;
  return unlocked[id];
}

const AchDef_t* achievements_get_def(AchId_t id)
{
  if (id < 0 || id >= ACH_COUNT) return NULL;
  return &ach_defs[id];
}

int achievements_get_total_count(void)
{
  int n = 0;
  for (int i = 0; i < ACH_COUNT; i++)
    if (ach_defs[i].name) n++;
  return n;
}

int achievements_get_unlocked_count(void)
{
  int n = 0;
  for (int i = 0; i < ACH_COUNT; i++)
    if (ach_defs[i].name && unlocked[i]) n++;
  return n;
}

int achievements_count_by_type(AchType_t type)
{
  int n = 0;
  for (int i = 0; i < ACH_COUNT; i++)
    if (ach_defs[i].name && ach_defs[i].type == type) n++;
  return n;
}

int achievements_count_unlocked_by_type(AchType_t type)
{
  int n = 0;
  for (int i = 0; i < ACH_COUNT; i++)
    if (ach_defs[i].name && ach_defs[i].type == type && unlocked[i]) n++;
  return n;
}

// ============================================================================
// Popup update + draw
// ============================================================================

void achievements_clear_popups(void)
{
  popup_queue_count = 0;
  popup_timer = -1.0f;
}

void achievements_update_popup(float dt)
{
  if (popup_queue_count <= 0) return;

  if (popup_timer < 0.0f)
  {
    // Start showing the next popup
    popup_timer = 0.0f;
    play_ach_sound(popup_queue[0]);
  }

  popup_timer += dt;
  if (popup_timer >= ACH_POPUP_TOTAL)
    popup_advance();
}

void achievements_draw_popup(void)
{
  if (popup_queue_count <= 0 || popup_timer < 0.0f) return;

  const AchDef_t* def = &ach_defs[popup_queue[0]];

  // Calculate slide offset (slides in from right)
  float slide = 0.0f;
  if (popup_timer < ACH_POPUP_SLIDE_IN)
  {
    // Slide in: 1.0 → 0.0
    float t = popup_timer / ACH_POPUP_SLIDE_IN;
    slide = (1.0f - t) * (1.0f - t); // ease out
  }
  else if (popup_timer > ACH_POPUP_SLIDE_IN + ACH_POPUP_HOLD)
  {
    // Slide out: 0.0 → 1.0
    float t = (popup_timer - ACH_POPUP_SLIDE_IN - ACH_POPUP_HOLD) / ACH_POPUP_SLIDE_OUT;
    slide = t * t; // ease in
  }

  int off_x = (int)(slide * (float)(ACH_POPUP_W + 20));
  int px = SCREEN_WIDTH - ACH_POPUP_W - 10 + off_x;
  int py = SCREEN_HEIGHT - ACH_POPUP_H - 58;

  // Type colors
  aColor_t type_colors[] = {
    {255, 220, 50, 255},   // GENERAL — gold
    {80, 220, 80, 255},    // ENEMY — green
    {100, 180, 255, 255},  // WEAPON — blue
  };
  aColor_t accent = (def->reward <= 0) ? (aColor_t){220, 60, 60, 255} : type_colors[def->type];

  // Background
  a_DrawFilledRect((aRectf_t){(float)px, (float)py, (float)ACH_POPUP_W, (float)ACH_POPUP_H},
                   (aColor_t){25, 25, 45, 240});
  // Accent border on left
  a_DrawFilledRect((aRectf_t){(float)px, (float)py, 3.0f, (float)ACH_POPUP_H}, accent);
  // Border
  a_DrawRect((aRectf_t){(float)px, (float)py, (float)ACH_POPUP_W, (float)ACH_POPUP_H},
             (aColor_t){accent.r / 2, accent.g / 2, accent.b / 2, 200});

  // "ACHIEVEMENT GET!" header
  aTextStyle_t header_st = {
    .type = FONT_ENTER_COMMAND,
    .fg = accent,
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.45f
  };
  a_DrawText("ACHIEVEMENT GET!", px + 12, py + 4, header_st);

  // Achievement name
  aTextStyle_t name_st = {
    .type = FONT_ENTER_COMMAND,
    .fg = {240, 240, 255, 255},
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.58f
  };
  a_DrawText(def->name, px + 12, py + 23, name_st);

  // Description
  aTextStyle_t desc_st = {
    .type = FONT_ENTER_COMMAND,
    .fg = {160, 160, 180, 220},
    .align = TEXT_ALIGN_LEFT,
    .scale = 0.42f
  };
  a_DrawText(def->desc, px + 12, py + 44, desc_st);

  // Reward on the right
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%d", def->reward > 0 ? "+" : "", def->reward);
    const char* type_labels[] = { "GP", "EP", "WP" };
    aTextStyle_t reward_st = {
      .type = FONT_ENTER_COMMAND,
      .fg = accent,
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.52f
    };
    a_DrawText(buf, px + ACH_POPUP_W - 12, py + 23, reward_st);

    aTextStyle_t rlabel_st = {
      .type = FONT_ENTER_COMMAND,
      .fg = {accent.r / 2, accent.g / 2, accent.b / 2, 200},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.36f
    };
    a_DrawText(type_labels[def->type], px + ACH_POPUP_W - 12, py + 44, rlabel_st);
  }
}
