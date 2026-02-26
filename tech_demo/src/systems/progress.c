#include <string.h>
#include "progress.h"
#include "save_data.h"

// Forward declarations for weapon meta-progression
static void wprog_init(void);
static void wprog_save(void);

typedef struct {
  int total_kills;
  int points_spent;
  int bonus_points;  // from achievements, separate from kills
} EnemyProgress_t;

static EnemyProgress_t progress[ENEMY_TYPE_COUNT];
static int tiers[PROG_COUNT];
static int upgrade_points = 0;

// Special source lifetime hits (fire cone, conductor)
static int fire_cone_lifetime_hits = 0;
static int conductor_lifetime_hits = 0;

// Global upgrades
static int global_tiers[GLOBAL_COUNT];

static const int global_max_tiers[GLOBAL_COUNT] = { 3, 3, 2, 1, 5, 5, 3, 2, 5, 5, 3, 3, 5, 1, 1, 1, 3, 3, 4, 3, 3, 1, 1, 1 };

static const int global_costs[GLOBAL_COUNT][5] = {
  [GLOBAL_POWERUP_DURATION]    = { 15, 30, 55 },
  [GLOBAL_SHIELD_TICKS]        = { 20, 60, 170 },
  [GLOBAL_DASH_COOLDOWN]       = { 25, 95 },
  [GLOBAL_DASH_EXTRA_CHARGE]   = { 75 },
  [GLOBAL_HEALTH_PICKUP_BONUS] = { 10, 20, 35, 55, 80 },
  [GLOBAL_LUCKY_ROLLS]         = { 25, 50, 75, 100, 150 },
  [GLOBAL_REROLL]              = { 35, 120, 200 },
  [GLOBAL_QUICK_LEARNER]       = { 50, 180 },
  [GLOBAL_MAX_HP]              = { 15, 30, 50, 75, 100 },
  [GLOBAL_TOUGHNESS]           = { 20, 40, 150, 300, 500 },
  [GLOBAL_XP_MAGNET]           = { 20, 45, 80 },
  [GLOBAL_HEALTH_REGEN]        = { 75, 250, 500 },
  [GLOBAL_STARTING_SHIELD]     = { 25, 50, 80, 120, 175 },
  [GLOBAL_EXTRA_CHOICE]        = { 100 },
  [GLOBAL_EXTRA_WEAPON_SLOT]   = { 125 },
  [GLOBAL_RETALIATION_KB]      = { 60 },
  [GLOBAL_RETALIATION_STUN]    = { 40, 100, 200 },
  [GLOBAL_HEAD_START]          = { 25, 60, 120 },
  [GLOBAL_PICKUP_MAGNET]       = { 15, 40, 85, 150 },
  [GLOBAL_PITY_TIMER]          = { 20, 55, 110 },
  [GLOBAL_PICKUP_DURATION]     = { 15, 45, 100 },
  [GLOBAL_REVIVE]              = { 350 },
  [GLOBAL_THORNS]              = { 120 },
  [GLOBAL_BOUNTY]              = { 250 },
};

static const char* global_names[GLOBAL_COUNT] = {
  "Powerup Duration",
  "Shield Ticks",
  "Dash Cooldown",
  "Dash Extra Charge",
  "Health Pickup Bonus",
  "Lucky Rolls",
  "Reroll",
  "Quick Learner",
  "Max HP",
  "Toughness",
  "XP Magnet",
  "Health Regen",
  "Starting Shield",
  "Extra Choice",
  "Extra Weapon Slot",
  "Retaliation",
  "Aftershock",
  "Head Start",
  "Magnetism",
  "Fortune",
  "Lingering Gifts",
  "Second Wind",
  "Thorns",
  "Bounty",
};

static const char* global_descs[GLOBAL_COUNT] = {
  "Powerups last longer",
  "Shield absorbs more hits",
  "Dash recharges faster",
  "Start with 2 dash charges",
  "Health pickups heal more",
  "Better rarity when rolling cards",
  "Reroll upgrade cards during runs",
  "Less XP needed to level up",
  "Increases maximum HP by 10 per tier",
  "Reduces all damage taken by 1 per tier",
  "XP orbs are attracted from further away",
  "Slowly regenerate HP over time",
  "Start each run with shield ticks",
  "Extra upgrade card on level up",
  "Enemies have a chance to drop a weapon on kill",
  "Knockback all enemies when you lose HP",
  "Stun all enemies when you lose HP",
  "Begin each run with bonus XP",
  "Attract powerups from further away",
  "Guaranteed pickups arrive sooner",
  "Pickups stay on the ground longer",
  "Revive once per run when killed",
  "Damage nearby enemies when you lose HP",
  "Enemies drop +1 XP orb on kill",
};

static const char* global_tier_descs[GLOBAL_COUNT][5] = {
  [GLOBAL_POWERUP_DURATION]    = { "+1.0s duration", "+2.0s duration", "+3.0s duration" },
  [GLOBAL_SHIELD_TICKS]        = { "+1 shield hit", "+2 shield hits", "+3 shield hits" },
  [GLOBAL_DASH_COOLDOWN]       = { "-0.5s cooldown", "-1.0s cooldown" },
  [GLOBAL_DASH_EXTRA_CHARGE]   = { "2 dash charges" },
  [GLOBAL_HEALTH_PICKUP_BONUS] = { "+2 heal", "+4 heal", "+6 heal", "+8 heal", "+10 heal" },
  [GLOBAL_LUCKY_ROLLS]         = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [GLOBAL_REROLL]              = { "1 reroll/run", "2 rerolls/run", "3 rerolls/run" },
  [GLOBAL_QUICK_LEARNER]       = { "-1 XP per level", "-2 XP per level" },
  [GLOBAL_MAX_HP]              = { "110 max HP", "120 max HP", "130 max HP", "140 max HP", "150 max HP" },
  [GLOBAL_TOUGHNESS]           = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [GLOBAL_XP_MAGNET]           = { "+50% magnet range", "+100% magnet range", "+150% magnet range" },
  [GLOBAL_HEALTH_REGEN]        = { "1 HP every 15s", "1 HP every 10s", "1 HP every 7s" },
  [GLOBAL_STARTING_SHIELD]     = { "1 shield", "2 shields", "3 shields", "4 shields", "5 shields" },
  [GLOBAL_EXTRA_CHOICE]        = { "4 cards" },
  [GLOBAL_EXTRA_WEAPON_SLOT]   = { "+1 weapon slot, kill drop chance" },
  [GLOBAL_RETALIATION_KB]      = { "AoE knockback on hit" },
  [GLOBAL_RETALIATION_STUN]    = { "0.1s stun", "0.2s stun", "0.3s stun" },
  [GLOBAL_HEAD_START]          = { "Start with 3 XP", "Start with 7 XP", "Start with 12 XP" },
  [GLOBAL_PICKUP_MAGNET]       = { "+15px range", "+30px range", "+50px range", "+75px range" },
  [GLOBAL_PITY_TIMER]          = { "-2s pity timer", "-4s pity timer", "-6s pity timer" },
  [GLOBAL_PICKUP_DURATION]     = { "+3s lifetime", "+6s lifetime", "+10s lifetime" },
  [GLOBAL_REVIVE]              = { "Revive at 25% HP" },
  [GLOBAL_THORNS]              = { "Hit attacker back" },
  [GLOBAL_BOUNTY]              = { "+1 XP orb per kill" },
};

static const char* global_details[GLOBAL_COUNT] = {
  "Adds bonus seconds to Fire Cone, Speed Boost, and Slow Aura duration.",
  "Shield pickups grant extra absorb hits. Also raises the max shield cap by the same amount.",
  "Reduces the dash cooldown timer, letting you dash more often. Base cooldown is 3.0s.",
  "Unlocks a second dash charge so you can dash twice in quick succession before any cooldown.",
  "Each health heart pickup restores bonus HP on top of the base 10.",
  "Increases chance of uncommon and rare upgrade cards.",
  "Grants reroll charges each run. Press R to discard all upgrade cards and roll new ones.",
  "Reduces XP needed per level by a flat amount.",
  "Permanently increases your maximum HP by 10 per tier. You start each run with the bonus HP.",
  "Reduces all damage taken by 1 per tier.",
  "Increases the radius at which XP orbs are pulled toward you. Base range is 144px.",
  "Passively regenerate 1 HP at a fixed interval. Doesn't work while taking damage.",
  "Begin every run with shield hits already active. Does not affect the shield from pickups.",
  "See an extra upgrade card when leveling up, giving you more choices each time.",
  "Extra weapon slot. Kills have a chance to drop a new weapon.",
  "When you take damage, all enemies within range are violently knocked back. Ignores knockback immunity.",
  "When you take damage, all enemies within range are stunned. Pierces stun immunity and cancels beholders.",
  "Start each run with XP already accumulated. Level 1 needs 5 XP, level 2 needs 10 XP.",
  "Increases the collection distance for powerup pickups (fire, speed, shield, slow). Base range is 34px.",
  "Reduces the pity timer that guarantees a pickup spawn during dry spells. Base is 15 seconds.",
  "Increases how long powerup pickups stay on the ground. Base lifetime is 10 seconds.",
  "When you would die, instead restore 25% of max HP and gain 3 seconds of invincibility. Triggers once per run.",
  "When you lose HP, the enemy that hit you takes a hit back.",
  "Every enemy kill drops one extra XP orb. Stacks with natural enemy XP values.",
};

static const char* global_tier_keys[GLOBAL_COUNT] = {
  "g_powerup_dur", "g_shield", "g_dash_cd", "g_dash_charge", "g_hp_bonus",
  "g_lucky", "g_reroll", "g_xp_red", "g_max_hp", "g_tough", "g_xp_mag", "g_regen",
  "g_start_shield", "g_extra_choice", "g_extra_wslot",
  "g_ret_kb", "g_ret_stun", "g_head_start", "g_pickup_mag",
  "g_pity", "g_pickup_dur", "g_revive",
  "g_thorns", "g_bounty"
};

static const int max_tiers[PROG_COUNT] = { 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3 };

static const int costs[PROG_COUNT][5] = {
  [PROG_GRUNT_DMG_RED]       = { 37, 94, 234, 512, 1007 },
  [PROG_GRUNT_EXPLOSION]     = { 76, 234, 586 },
  [PROG_DASHER_DMG_RED]      = { 30, 68, 131, 225, 375 },
  [PROG_DASHER_STUN]         = { 60, 150, 300 },
  [PROG_BRUTE_DMG_RED]       = { 16, 32, 64, 111, 191 },
  [PROG_BRUTE_SPEED_DIM]     = { 32, 80, 160 },
  [PROG_SHAMAN_CORPSE_DECAY] = { 21, 50, 102, 169, 270 },
  [PROG_SHAMAN_HEAL_RED]     = { 27, 68, 152 },
  [PROG_SNAKE_DMG_RED]       = { 4, 9, 16, 26, 40 },
  [PROG_SNAKE_SCALE_SHATTER] = { 6, 13, 23 },
  [PROG_BEHOLDER_DMG_RED]    = { 3, 7, 14, 23, 36 },
  [PROG_BEHOLDER_TELEGRAPH]  = { 5, 15, 33 },
  [PROG_MIMIC_DMG_RED]       = { 3, 7, 14, 23, 37 },
  [PROG_MIMIC_WEAPON_CD]     = { 5, 14, 29 },
};

static const EnemyType_t upgrade_owner[PROG_COUNT] = {
  ENEMY_TYPE_GRUNT, ENEMY_TYPE_GRUNT,
  ENEMY_TYPE_DASHER, ENEMY_TYPE_DASHER,
  ENEMY_TYPE_BRUTE, ENEMY_TYPE_BRUTE,
  ENEMY_TYPE_SHAMAN, ENEMY_TYPE_SHAMAN,
  ENEMY_TYPE_SNAKE, ENEMY_TYPE_SNAKE,
  ENEMY_TYPE_BEHOLDER, ENEMY_TYPE_BEHOLDER,
  ENEMY_TYPE_MIMIC, ENEMY_TYPE_MIMIC,
};

static const char* upgrade_names[PROG_COUNT] = {
  "Damage Reduction",
  "Death Explosion",
  "Damage Reduction",
  "Self-Stun on Miss",
  "Damage Reduction",
  "Speed Diminishing Returns",
  "Corpse Decay",
  "Heal Reduction",
  "Damage Reduction",
  "Scale Shatter",
  "Damage Reduction",
  "Slow Gaze",
  "Damage Reduction",
  "Weapon Cooldown",
};

static const char* upgrade_details[PROG_COUNT] = {
  [PROG_GRUNT_DMG_RED]       = "Reduces damage taken from Grunt attacks. Each tier subtracts 1 hit from their damage.",
  [PROG_GRUNT_EXPLOSION]     = "Grunts explode on death, dealing AoE damage. Higher tiers increase radius.",
  [PROG_DASHER_DMG_RED]      = "Reduces damage taken from Dasher charge attacks. Each tier subtracts 1 hit from their damage.",
  [PROG_DASHER_STUN]         = "When a Dasher misses its charge, it stuns itself. Higher tiers increase stun duration.",
  [PROG_BRUTE_DMG_RED]       = "Reduces damage taken from Brute attacks. Each tier subtracts 1 hit from their damage.",
  [PROG_BRUTE_SPEED_DIM]     = "Reduces Brute speed bonus from taking damage. Lower % = slower when hurt.",
  [PROG_SHAMAN_CORPSE_DECAY] = "Corpses decay faster, less time for Shamans to consume them. Base: 9s.",
  [PROG_SHAMAN_HEAL_RED]     = "Reduces HP restored when Shamans consume corpses to heal allies.",
  [PROG_SNAKE_DMG_RED]       = "Reduces contact damage taken from the Snake boss. Each tier subtracts 1 damage.",
  [PROG_SNAKE_SCALE_SHATTER] = "When a snake segment pops, it fires shrapnel projectiles that damage nearby enemies.",
  [PROG_BEHOLDER_DMG_RED]    = "Reduces damage taken from Beholder beam attacks. Each tier subtracts 1 damage.",
  [PROG_BEHOLDER_TELEGRAPH]  = "Slows the Beholder's beam sweep speed, giving you more time to dodge.",
  [PROG_MIMIC_DMG_RED]       = "Reduces damage taken from the Mimic's attacks. Each tier subtracts 1 damage.",
  [PROG_MIMIC_WEAPON_CD]     = "Increases the Mimic's weapon cooldown, giving you more time between attacks.",
};

static const char* tier_descs[PROG_COUNT][5] = {
  [PROG_GRUNT_DMG_RED] = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [PROG_GRUNT_EXPLOSION] = { "30 radius AoE on death", "45 radius AoE", "60 radius AoE" },
  [PROG_DASHER_DMG_RED] = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [PROG_DASHER_STUN] = { "0.3s stun on miss", "0.6s stun", "0.9s stun" },
  [PROG_BRUTE_DMG_RED] = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [PROG_BRUTE_SPEED_DIM] = { "80% speed bonus", "60% speed bonus", "40% speed bonus" },
  [PROG_SHAMAN_CORPSE_DECAY] = { "8s corpse life", "7s corpse life", "6s corpse life", "5s corpse life", "4s corpse life" },
  [PROG_SHAMAN_HEAL_RED] = { "-1 heal amount", "-2 heal amount", "-3 heal amount" },
  [PROG_SNAKE_DMG_RED] = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [PROG_SNAKE_SCALE_SHATTER] = { "3 shrapnel", "4 shrapnel", "5 + pierce" },
  [PROG_BEHOLDER_DMG_RED] = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [PROG_BEHOLDER_TELEGRAPH] = { "Slower sweep", "Much slower", "Sluggish sweep" },
  [PROG_MIMIC_DMG_RED] = { "-1 damage", "-2 damage", "-3 damage", "-4 damage", "-5 damage" },
  [PROG_MIMIC_WEAPON_CD] = { "+10% cooldown", "+20% cooldown", "+30% cooldown" },
};

static const char* kill_keys[ENEMY_TYPE_COUNT] = {
  [ENEMY_TYPE_GRUNT]    = "kills_grunt",
  [ENEMY_TYPE_DASHER]   = "kills_dasher",
  [ENEMY_TYPE_BRUTE]    = "kills_brute",
  [ENEMY_TYPE_SHAMAN]   = "kills_shaman",
  [ENEMY_TYPE_SNAKE]    = "kills_snake",
  [ENEMY_TYPE_BEHOLDER] = "kills_beholder",
  [ENEMY_TYPE_MIMIC]    = "kills_mimic",
};
static const char* spent_keys[ENEMY_TYPE_COUNT] = {
  [ENEMY_TYPE_GRUNT]    = "spent_grunt",
  [ENEMY_TYPE_DASHER]   = "spent_dasher",
  [ENEMY_TYPE_BRUTE]    = "spent_brute",
  [ENEMY_TYPE_SHAMAN]   = "spent_shaman",
  [ENEMY_TYPE_SNAKE]    = "spent_snake",
  [ENEMY_TYPE_BEHOLDER] = "spent_beholder",
  [ENEMY_TYPE_MIMIC]    = "spent_mimic",
};
static const char* bonus_keys[ENEMY_TYPE_COUNT] = {
  [ENEMY_TYPE_GRUNT]    = "bonus_grunt",
  [ENEMY_TYPE_DASHER]   = "bonus_dasher",
  [ENEMY_TYPE_BRUTE]    = "bonus_brute",
  [ENEMY_TYPE_SHAMAN]   = "bonus_shaman",
  [ENEMY_TYPE_SNAKE]    = "bonus_snake",
  [ENEMY_TYPE_BEHOLDER] = "bonus_beholder",
  [ENEMY_TYPE_MIMIC]    = "bonus_mimic",
};
static const char* tier_keys[PROG_COUNT] = {
  "tier_0", "tier_1", "tier_2", "tier_3", "tier_4", "tier_5", "tier_6", "tier_7",
  "tier_8", "tier_9", "tier_10", "tier_11",
  "tier_12", "tier_13"
};

void progress_init(void)
{
  memset(progress, 0, sizeof(progress));
  memset(tiers, 0, sizeof(tiers));
  upgrade_points = 0;

  for (int i = 0; i < ENEMY_TYPE_COUNT; i++) {
    progress[i].total_kills  = save_data_get_int(kill_keys[i], 0);
    progress[i].points_spent = save_data_get_int(spent_keys[i], 0);
    progress[i].bonus_points = save_data_get_int(bonus_keys[i], 0);
  }
  for (int i = 0; i < PROG_COUNT; i++) {
    tiers[i] = save_data_get_int(tier_keys[i], 0);
    if (tiers[i] < 0) tiers[i] = 0;
    if (tiers[i] > max_tiers[i]) tiers[i] = max_tiers[i];
  }
  upgrade_points = save_data_get_int("upgrade_points", 0);
  if (upgrade_points < 0) upgrade_points = 0;

  memset(global_tiers, 0, sizeof(global_tiers));
  for (int i = 0; i < GLOBAL_COUNT; i++) {
    global_tiers[i] = save_data_get_int(global_tier_keys[i], 0);
    if (global_tiers[i] < 0) global_tiers[i] = 0;
    if (global_tiers[i] > global_max_tiers[i]) global_tiers[i] = global_max_tiers[i];
  }

  fire_cone_lifetime_hits = save_data_get_int("src_fc_hits", 0);
  conductor_lifetime_hits = save_data_get_int("src_cd_hits", 0);

  wprog_init();
}

void progress_save(void)
{
  for (int i = 0; i < ENEMY_TYPE_COUNT; i++) {
    save_data_set_int(kill_keys[i],  progress[i].total_kills);
    save_data_set_int(spent_keys[i], progress[i].points_spent);
    save_data_set_int(bonus_keys[i], progress[i].bonus_points);
  }
  for (int i = 0; i < PROG_COUNT; i++) {
    save_data_set_int(tier_keys[i], tiers[i]);
  }
  save_data_set_int("upgrade_points", upgrade_points);
  for (int i = 0; i < GLOBAL_COUNT; i++) {
    save_data_set_int(global_tier_keys[i], global_tiers[i]);
  }
  save_data_set_int("src_fc_hits", fire_cone_lifetime_hits);
  save_data_set_int("src_cd_hits", conductor_lifetime_hits);
  wprog_save();
  save_data_flush();
}

void progress_bank_run_kills(int kills[ENEMY_TYPE_COUNT])
{
  for (int i = 0; i < ENEMY_TYPE_COUNT; i++) {
    progress[i].total_kills += kills[i];
  }
  progress_save();
}

void progress_add_enemy_bonus(EnemyType_t type, int pts)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return;
  progress[type].bonus_points += pts;
  progress_save();
}

int progress_get_lifetime_kills(EnemyType_t type)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return 0;
  return progress[type].total_kills;
}

int progress_get_available_points(EnemyType_t type)
{
  if (type < 0 || type >= ENEMY_TYPE_COUNT) return 0;
  return progress[type].total_kills + progress[type].bonus_points - progress[type].points_spent;
}

int progress_get_tier(ProgressUpgradeId_t id)
{
  if (id < 0 || id >= PROG_COUNT) return 0;
  return tiers[id];
}

int progress_get_max_tier(ProgressUpgradeId_t id)
{
  if (id < 0 || id >= PROG_COUNT) return 0;
  return max_tiers[id];
}

int progress_get_next_cost(ProgressUpgradeId_t id)
{
  if (id < 0 || id >= PROG_COUNT) return -1;
  if (tiers[id] >= max_tiers[id]) return -1;
  return costs[id][tiers[id]];
}

int progress_can_afford(ProgressUpgradeId_t id)
{
  if (id < 0 || id >= PROG_COUNT) return 0;
  int cost = progress_get_next_cost(id);
  if (cost < 0) return 0;
  EnemyType_t owner = upgrade_owner[id];
  return progress_get_available_points(owner) >= cost;
}

int progress_has_affordable_upgrade(void)
{
  for (int i = 0; i < PROG_COUNT; i++) {
    if (progress_can_afford((ProgressUpgradeId_t)i)) return 1;
  }
  return 0;
}

int progress_has_affordable_upgrade_for(EnemyType_t type)
{
  for (int i = 0; i < PROG_COUNT; i++) {
    if (upgrade_owner[i] == type && progress_can_afford((ProgressUpgradeId_t)i)) return 1;
  }
  return 0;
}

int progress_purchase(ProgressUpgradeId_t id)
{
  if (!progress_can_afford(id)) return 0;
  int cost = progress_get_next_cost(id);
  EnemyType_t owner = upgrade_owner[id];
  progress[owner].points_spent += cost;
  tiers[id]++;
  progress_save();
  return 1;
}

const char* progress_get_upgrade_name(ProgressUpgradeId_t id)
{
  if (id < 0 || id >= PROG_COUNT) return "";
  return upgrade_names[id];
}

const char* progress_get_upgrade_detail(ProgressUpgradeId_t id)
{
  if (id < 0 || id >= PROG_COUNT) return "";
  return upgrade_details[id];
}

const char* progress_get_tier_desc(ProgressUpgradeId_t id, int tier)
{
  if (id < 0 || id >= PROG_COUNT) return "";
  if (tier < 0 || tier >= max_tiers[id]) return "";
  return tier_descs[id][tier];
}

// ============================================================================
// Gameplay effect queries
// ============================================================================

int progress_get_dmg_reduction(EnemyType_t type)
{
  switch (type) {
    case ENEMY_TYPE_GRUNT:  return tiers[PROG_GRUNT_DMG_RED];
    case ENEMY_TYPE_DASHER: return tiers[PROG_DASHER_DMG_RED];
    case ENEMY_TYPE_BRUTE:  return tiers[PROG_BRUTE_DMG_RED];
    case ENEMY_TYPE_MIMIC:  return tiers[PROG_MIMIC_DMG_RED];
    default: return 0;
  }
}

int progress_get_grunt_explosion_tier(void)
{
  return tiers[PROG_GRUNT_EXPLOSION];
}

float progress_get_grunt_explosion_radius(void)
{
  switch (tiers[PROG_GRUNT_EXPLOSION]) {
    case 1: return 30.0f;
    case 2: return 45.0f;
    case 3: return 60.0f;
    default: return 0.0f;
  }
}

float progress_get_dasher_stun_duration(void)
{
  switch (tiers[PROG_DASHER_STUN]) {
    case 1: return 0.3f;
    case 2: return 0.6f;
    case 3: return 0.9f;
    default: return 0.0f;
  }
}

float progress_get_brute_speed_effectiveness(void)
{
  switch (tiers[PROG_BRUTE_SPEED_DIM]) {
    case 1: return 0.8f;
    case 2: return 0.6f;
    case 3: return 0.4f;
    default: return 1.0f;
  }
}

float progress_get_corpse_lifetime(void)
{
  // Base 9.0, each tier -1.0, min 4.0
  return 9.0f - (float)tiers[PROG_SHAMAN_CORPSE_DECAY];
}

float progress_get_blood_lifetime(void)
{
  return progress_get_corpse_lifetime() + 2.0f;
}

int progress_get_heal_reduction(void)
{
  return tiers[PROG_SHAMAN_HEAL_RED];
}

int progress_get_snake_dmg_reduction(void)
{
  return tiers[PROG_SNAKE_DMG_RED];
}

int progress_get_snake_shatter_projectiles(void)
{
  switch (tiers[PROG_SNAKE_SCALE_SHATTER]) {
    case 1: return 3;
    case 2: return 4;
    case 3: return 5;
    default: return 0;
  }
}

int progress_get_snake_shatter_pierces(void)
{
  return tiers[PROG_SNAKE_SCALE_SHATTER] >= 3 ? 1 : 0;
}

int progress_is_snake_discovered(void)
{
  return save_data_get_int("snake_discovered", 0);
}

int progress_get_beholder_dmg_reduction(void)
{
  return tiers[PROG_BEHOLDER_DMG_RED];
}

float progress_get_beholder_telegraph_bonus(void)
{
  return 0.0f;  // telegraph time no longer upgradeable
}

float progress_get_beholder_aim_slow(void)
{
  // Reduces aim tracking speed: base 1.5 rad/s, each tier subtracts
  static const float slow[4] = { 0.0f, 0.1f, 0.2f, 0.3f };
  int t = tiers[PROG_BEHOLDER_TELEGRAPH];
  if (t < 0) t = 0;
  if (t > 3) t = 3;
  return slow[t];
}

int progress_is_beholder_discovered(void)
{
  return save_data_get_int("beholder_discovered", 0);
}

int progress_get_mimic_dmg_reduction(void)
{
  return tiers[PROG_MIMIC_DMG_RED];
}

float progress_get_mimic_cooldown_mult(void)
{
  return 1.0f + 0.1f * (float)tiers[PROG_MIMIC_WEAPON_CD];
}

int progress_is_mimic_discovered(void)
{
  return save_data_get_int("mimic_discovered", 0);
}

void progress_bank_upgrade_points(int pts)
{
  upgrade_points += pts;
  progress_save();
}

int progress_get_upgrade_points(void)
{
  return upgrade_points;
}

// ============================================================================
// Global Upgrades
// ============================================================================

int global_get_tier(GlobalUpgradeId_t id)
{
  if (id < 0 || id >= GLOBAL_COUNT) return 0;
  return global_tiers[id];
}

int global_get_max_tier(GlobalUpgradeId_t id)
{
  if (id < 0 || id >= GLOBAL_COUNT) return 0;
  return global_max_tiers[id];
}

int global_get_next_cost(GlobalUpgradeId_t id)
{
  if (id < 0 || id >= GLOBAL_COUNT) return -1;
  if (global_tiers[id] >= global_max_tiers[id]) return -1;
  return global_costs[id][global_tiers[id]];
}

int global_can_afford(GlobalUpgradeId_t id)
{
  int cost = global_get_next_cost(id);
  if (cost < 0) return 0;
  return upgrade_points >= cost;
}

int global_purchase(GlobalUpgradeId_t id)
{
  if (!global_can_afford(id)) return 0;
  int cost = global_get_next_cost(id);
  upgrade_points -= cost;
  global_tiers[id]++;
  progress_save();
  return 1;
}

int global_has_affordable(void)
{
  for (int i = 0; i < GLOBAL_COUNT; i++) {
    if (global_can_afford((GlobalUpgradeId_t)i)) return 1;
  }
  return 0;
}

const char* global_get_name(GlobalUpgradeId_t id)
{
  if (id < 0 || id >= GLOBAL_COUNT) return "";
  return global_names[id];
}

const char* global_get_desc(GlobalUpgradeId_t id)
{
  if (id < 0 || id >= GLOBAL_COUNT) return "";
  return global_descs[id];
}

const char* global_get_detail(GlobalUpgradeId_t id)
{
  if (id < 0 || id >= GLOBAL_COUNT) return "";
  return global_details[id];
}

const char* global_get_tier_desc(GlobalUpgradeId_t id, int tier)
{
  if (id < 0 || id >= GLOBAL_COUNT) return "";
  if (tier < 0 || tier >= global_max_tiers[id]) return "";
  return global_tier_descs[id][tier];
}

// Gameplay queries

float global_get_buff_duration_bonus(void)
{
  return (float)global_tiers[GLOBAL_POWERUP_DURATION];
}

int global_get_shield_hits_bonus(void)
{
  return global_tiers[GLOBAL_SHIELD_TICKS];
}

float global_get_dash_cooldown_reduction(void)
{
  return 0.5f * (float)global_tiers[GLOBAL_DASH_COOLDOWN];
}

int global_has_dash_extra_charge(void)
{
  return global_tiers[GLOBAL_DASH_EXTRA_CHARGE] > 0;
}

int global_get_health_pickup_bonus(void)
{
  return 2 * global_tiers[GLOBAL_HEALTH_PICKUP_BONUS];
}

int global_get_uncommon_weight_bonus(void)
{
  return 3 * global_tiers[GLOBAL_LUCKY_ROLLS];
}

int global_get_rare_weight_bonus(void)
{
  return 2 * global_tiers[GLOBAL_LUCKY_ROLLS];
}

int global_get_rerolls_per_run(void)
{
  return global_tiers[GLOBAL_REROLL];
}

int global_get_xp_reduction(void)
{
  return global_tiers[GLOBAL_QUICK_LEARNER];
}

int global_get_max_hp_bonus(void)
{
  return 10 * global_tiers[GLOBAL_MAX_HP];
}

int global_get_toughness(void)
{
  return global_tiers[GLOBAL_TOUGHNESS];
}

float global_get_xp_magnet_bonus(void)
{
  return 72.0f * (float)global_tiers[GLOBAL_XP_MAGNET];  // +50%/+100%/+150% of 144
}

float global_get_regen_interval(void)
{
  switch (global_tiers[GLOBAL_HEALTH_REGEN]) {
    case 1: return 15.0f;
    case 2: return 10.0f;
    case 3: return 7.0f;
    default: return 0.0f;
  }
}

int global_get_starting_shield(void)
{
  return global_tiers[GLOBAL_STARTING_SHIELD];
}

int global_get_extra_choices(void)
{
  return global_tiers[GLOBAL_EXTRA_CHOICE];
}

int global_has_extra_weapon_slot(void)
{
  return global_tiers[GLOBAL_EXTRA_WEAPON_SLOT] > 0;
}

int global_has_retaliation_kb(void)
{
  return global_tiers[GLOBAL_RETALIATION_KB] > 0;
}

float global_get_retaliation_stun(void)
{
  static const float vals[] = { 0.0f, 0.1f, 0.2f, 0.3f };
  int t = global_tiers[GLOBAL_RETALIATION_STUN];
  return vals[t < 4 ? t : 3];
}

int global_get_starting_xp(void)
{
  static const int vals[] = { 0, 3, 7, 12 };
  int t = global_tiers[GLOBAL_HEAD_START];
  return vals[t < 4 ? t : 3];
}

float global_get_pickup_magnet_bonus(void)
{
  static const float vals[] = { 0.0f, 15.0f, 30.0f, 50.0f, 75.0f };
  int t = global_tiers[GLOBAL_PICKUP_MAGNET];
  return vals[t < 5 ? t : 4];
}

float global_get_pity_reduction(void)
{
  return 2.0f * global_tiers[GLOBAL_PITY_TIMER];
}

float global_get_pickup_lifetime_bonus(void)
{
  static const float vals[] = { 0.0f, 3.0f, 6.0f, 10.0f };
  int t = global_tiers[GLOBAL_PICKUP_DURATION];
  return vals[t < 4 ? t : 3];
}

int global_has_revive(void)
{
  return global_tiers[GLOBAL_REVIVE] > 0;
}

int global_get_thorns_tier(void)
{
  return global_tiers[GLOBAL_THORNS] > 0;
}

int global_has_bounty(void)
{
  return global_tiers[GLOBAL_BOUNTY] > 0;
}

// ============================================================================
// Weapon Meta-Progression
// ============================================================================

typedef struct {
  int total_hits;
  int total_kills;
  int points_spent;
  int bonus_points;  // from achievements, separate from hits
} WeaponProgress_t;

static WeaponProgress_t wprogress[WID_COUNT];
static int wtiers[WPROG_COUNT];

static const int wprog_max_tiers[WPROG_COUNT] = {
  5, 3, 1, 5, 1,  // Wand: cooldown, reach, free upgrade, rarity, extra choice
  5, 3, 1, 5, 1,  // Spin
  5, 3, 1, 5, 1,  // Chain
  5, 3, 1, 5, 1, 3,  // Orbit (+shatter)
  5, 3, 1, 5, 1,     // Bomb
  5, 3, 1, 5, 1, 3,  // Turret (+duration)
  5, 3, 1, 5, 1,  // Trail
  5, 3, 1, 5, 1,  // Scythe
};

static const int wprog_costs[WPROG_COUNT][5] = {
  [WPROG_WAND_COOLDOWN]      = { 40, 100, 230, 530, 1100 },
  [WPROG_WAND_REACH]         = { 100, 300, 750 },
  [WPROG_WAND_FREE_UPG]      = { 300 },
  [WPROG_WAND_RARITY]        = { 25, 50, 75, 100, 150 },
  [WPROG_WAND_EXTRA_CHOICE]  = { 500 },
  [WPROG_SPIN_COOLDOWN]      = { 40, 100, 230, 530, 1100 },
  [WPROG_SPIN_REACH]         = { 100, 300, 750 },
  [WPROG_SPIN_FREE_UPG]      = { 300 },
  [WPROG_SPIN_RARITY]        = { 25, 50, 75, 100, 150 },
  [WPROG_SPIN_EXTRA_CHOICE]  = { 500 },
  [WPROG_CHAIN_COOLDOWN]     = { 40, 100, 230, 530, 1100 },
  [WPROG_CHAIN_REACH]        = { 100, 300, 750 },
  [WPROG_CHAIN_FREE_UPG]     = { 300 },
  [WPROG_CHAIN_RARITY]       = { 25, 50, 75, 100, 150 },
  [WPROG_CHAIN_EXTRA_CHOICE] = { 500 },
  [WPROG_ORBIT_COOLDOWN]     = { 40, 100, 230, 530, 1100 },
  [WPROG_ORBIT_REACH]        = { 100, 300, 750 },
  [WPROG_ORBIT_FREE_UPG]     = { 300 },
  [WPROG_ORBIT_RARITY]       = { 25, 50, 75, 100, 150 },
  [WPROG_ORBIT_EXTRA_CHOICE] = { 500 },
  [WPROG_ORBIT_SHATTER]      = { 150, 400, 900 },
  [WPROG_BOMB_COOLDOWN]      = { 40, 100, 230, 530, 1100 },
  [WPROG_BOMB_REACH]         = { 100, 300, 750 },
  [WPROG_BOMB_FREE_UPG]      = { 300 },
  [WPROG_BOMB_RARITY]        = { 25, 50, 75, 100, 150 },
  [WPROG_BOMB_EXTRA_CHOICE]  = { 500 },
  [WPROG_TURRET_COOLDOWN]    = { 40, 100, 230, 530, 1100 },
  [WPROG_TURRET_REACH]       = { 100, 300, 750 },
  [WPROG_TURRET_FREE_UPG]    = { 300 },
  [WPROG_TURRET_RARITY]      = { 25, 50, 75, 100, 150 },
  [WPROG_TURRET_EXTRA_CHOICE]= { 500 },
  [WPROG_TURRET_DURATION]    = { 100, 300, 750 },
  [WPROG_TRAIL_COOLDOWN]     = { 40, 100, 230, 530, 1100 },
  [WPROG_TRAIL_REACH]        = { 100, 300, 750 },
  [WPROG_TRAIL_FREE_UPG]     = { 300 },
  [WPROG_TRAIL_RARITY]       = { 25, 50, 75, 100, 150 },
  [WPROG_TRAIL_EXTRA_CHOICE] = { 500 },
  [WPROG_SCYTHE_COOLDOWN]    = { 40, 100, 230, 530, 1100 },
  [WPROG_SCYTHE_REACH]       = { 100, 300, 750 },
  [WPROG_SCYTHE_FREE_UPG]    = { 300 },
  [WPROG_SCYTHE_RARITY]      = { 25, 50, 75, 100, 150 },
  [WPROG_SCYTHE_EXTRA_CHOICE]= { 500 },
};

static const WeaponId_t wprog_owner[WPROG_COUNT] = {
  WID_WAND, WID_WAND, WID_WAND, WID_WAND, WID_WAND,
  WID_SPIN, WID_SPIN, WID_SPIN, WID_SPIN, WID_SPIN,
  WID_CHAIN, WID_CHAIN, WID_CHAIN, WID_CHAIN, WID_CHAIN,
  WID_ORBIT, WID_ORBIT, WID_ORBIT, WID_ORBIT, WID_ORBIT, WID_ORBIT,
  WID_BOMB, WID_BOMB, WID_BOMB, WID_BOMB, WID_BOMB,
  WID_TURRET, WID_TURRET, WID_TURRET, WID_TURRET, WID_TURRET, WID_TURRET,
  WID_TRAIL, WID_TRAIL, WID_TRAIL, WID_TRAIL, WID_TRAIL,
  WID_SCYTHE, WID_SCYTHE, WID_SCYTHE, WID_SCYTHE, WID_SCYTHE,
};

static const char* wprog_names[WPROG_COUNT] = {
  "Base Cooldown", "Base Bullet Speed", "Free Upgrade", "Lucky Rolls", "Extra Choice",
  "Base Cooldown", "Base Radius", "Free Upgrade", "Lucky Rolls", "Extra Choice",
  "Base Cooldown", "Base Jump Range", "Free Upgrade", "Lucky Rolls", "Extra Choice",
  "Base Cooldown", "Base Orbit Radius", "Free Upgrade", "Lucky Rolls", "Extra Choice", "Base Shatter",
  "Base Cooldown", "Base Blast Radius", "Free Upgrade", "Lucky Rolls", "Extra Choice",
  "Base Cooldown", "Base Range", "Free Upgrade", "Lucky Rolls", "Extra Choice", "Base Duration",
  "Base Cooldown", "Base Width", "Free Upgrade", "Lucky Rolls", "Extra Choice",
  "Base Cooldown", "Base Sweep Radius", "Free Upgrade", "Lucky Rolls", "Extra Choice",
};

static const char* wprog_details[WPROG_COUNT] = {
  [WPROG_WAND_COOLDOWN]      = "Permanently reduces wand shot cooldown.",
  [WPROG_WAND_REACH]         = "Permanently increases wand projectile speed.",
  [WPROG_WAND_FREE_UPG]      = "Get a free upgrade choice for this weapon at the start of each run.",
  [WPROG_WAND_RARITY]        = "Better rarity when rolling wand upgrade cards.",
  [WPROG_WAND_EXTRA_CHOICE]  = "See an extra upgrade card when rolling wand upgrades.",
  [WPROG_SPIN_COOLDOWN]      = "Permanently reduces spin attack cooldown.",
  [WPROG_SPIN_REACH]         = "Permanently increases the spin attack radius.",
  [WPROG_SPIN_FREE_UPG]      = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_SPIN_RARITY]        = "Better rarity when rolling spin upgrade cards.",
  [WPROG_SPIN_EXTRA_CHOICE]  = "See an extra upgrade card when rolling spin upgrades.",
  [WPROG_CHAIN_COOLDOWN]     = "Permanently reduces chain lightning cooldown.",
  [WPROG_CHAIN_REACH]        = "Permanently increases the jump distance between chain targets.",
  [WPROG_CHAIN_FREE_UPG]     = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_CHAIN_RARITY]       = "Better rarity when rolling chain upgrade cards.",
  [WPROG_CHAIN_EXTRA_CHOICE] = "See an extra upgrade card when rolling chain upgrades.",
  [WPROG_ORBIT_COOLDOWN]     = "Permanently reduces orbit activation cooldown.",
  [WPROG_ORBIT_REACH]        = "Permanently increases the orbital path radius.",
  [WPROG_ORBIT_FREE_UPG]     = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_ORBIT_RARITY]       = "Better rarity when rolling orbit upgrade cards.",
  [WPROG_ORBIT_EXTRA_CHOICE] = "See an extra upgrade card when rolling orbit upgrades.",
  [WPROG_ORBIT_SHATTER]      = "Permanently adds extra shatter projectiles when orbs expire.",
  [WPROG_BOMB_COOLDOWN]      = "Permanently reduces bomb launch cooldown.",
  [WPROG_BOMB_REACH]         = "Permanently increases the explosion radius.",
  [WPROG_BOMB_FREE_UPG]      = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_BOMB_RARITY]        = "Better rarity when rolling bomb upgrade cards.",
  [WPROG_BOMB_EXTRA_CHOICE]  = "See an extra upgrade card when rolling bomb upgrades.",
  [WPROG_TURRET_COOLDOWN]    = "Permanently reduces turret deployment cooldown.",
  [WPROG_TURRET_REACH]       = "Permanently increases turret targeting range.",
  [WPROG_TURRET_FREE_UPG]    = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_TURRET_RARITY]      = "Better rarity when rolling turret upgrade cards.",
  [WPROG_TURRET_EXTRA_CHOICE]= "See an extra upgrade card when rolling turret upgrades.",
  [WPROG_TURRET_DURATION]    = "Permanently increases turret active duration.",
  [WPROG_TRAIL_COOLDOWN]     = "Permanently reduces trail cooldown.",
  [WPROG_TRAIL_REACH]        = "Permanently increases the fire trail width.",
  [WPROG_TRAIL_FREE_UPG]     = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_TRAIL_RARITY]       = "Better rarity when rolling trail upgrade cards.",
  [WPROG_TRAIL_EXTRA_CHOICE] = "See an extra upgrade card when rolling trail upgrades.",
  [WPROG_SCYTHE_COOLDOWN]    = "Permanently reduces scythe sweep cooldown.",
  [WPROG_SCYTHE_REACH]       = "Permanently increases the scythe sweep radius.",
  [WPROG_SCYTHE_FREE_UPG]    = "Get a free upgrade choice for this weapon on pickup.",
  [WPROG_SCYTHE_RARITY]      = "Better rarity when rolling scythe upgrade cards.",
  [WPROG_SCYTHE_EXTRA_CHOICE]= "See an extra upgrade card when rolling scythe upgrades.",
};

static const char* wprog_tier_descs[WPROG_COUNT][5] = {
  [WPROG_WAND_COOLDOWN]      = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_WAND_REACH]         = { "+10% speed", "+20% speed", "+30% speed" },
  [WPROG_WAND_FREE_UPG]      = { "Free upgrade on game start" },
  [WPROG_WAND_RARITY]        = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_WAND_EXTRA_CHOICE]  = { "+1 card" },
  [WPROG_SPIN_COOLDOWN]      = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_SPIN_REACH]         = { "+3px", "+7px", "+12px" },
  [WPROG_SPIN_FREE_UPG]      = { "Free upgrade on pickup" },
  [WPROG_SPIN_RARITY]        = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_SPIN_EXTRA_CHOICE]  = { "+1 card" },
  [WPROG_CHAIN_COOLDOWN]     = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_CHAIN_REACH]        = { "+5px", "+12px", "+20px" },
  [WPROG_CHAIN_FREE_UPG]     = { "Free upgrade on pickup" },
  [WPROG_CHAIN_RARITY]       = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_CHAIN_EXTRA_CHOICE] = { "+1 card" },
  [WPROG_ORBIT_COOLDOWN]     = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_ORBIT_REACH]        = { "+5px", "+12px", "+20px" },
  [WPROG_ORBIT_FREE_UPG]     = { "Free upgrade on pickup" },
  [WPROG_ORBIT_RARITY]       = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_ORBIT_EXTRA_CHOICE] = { "+1 card" },
  [WPROG_ORBIT_SHATTER]      = { "+1 proj", "+2 proj", "+3 proj" },
  [WPROG_BOMB_COOLDOWN]      = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_BOMB_REACH]         = { "+3px", "+7px", "+12px" },
  [WPROG_BOMB_FREE_UPG]      = { "Free upgrade on pickup" },
  [WPROG_BOMB_RARITY]        = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_BOMB_EXTRA_CHOICE]  = { "+1 card" },
  [WPROG_TURRET_COOLDOWN]    = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_TURRET_REACH]       = { "+8px", "+18px", "+30px" },
  [WPROG_TURRET_FREE_UPG]    = { "Free upgrade on pickup" },
  [WPROG_TURRET_RARITY]      = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_TURRET_EXTRA_CHOICE]= { "+1 card" },
  [WPROG_TURRET_DURATION]    = { "+0.3s", "+0.7s", "+1.2s" },
  [WPROG_TRAIL_COOLDOWN]     = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_TRAIL_REACH]        = { "+2px", "+4px", "+7px" },
  [WPROG_TRAIL_FREE_UPG]     = { "Free upgrade on pickup" },
  [WPROG_TRAIL_RARITY]       = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_TRAIL_EXTRA_CHOICE] = { "+1 card" },
  [WPROG_SCYTHE_COOLDOWN]    = { "-3%", "-6%", "-9%", "-12%", "-15%" },
  [WPROG_SCYTHE_REACH]       = { "+8px", "+16px", "+25px" },
  [WPROG_SCYTHE_FREE_UPG]    = { "Free upgrade on pickup" },
  [WPROG_SCYTHE_RARITY]      = { "+3 unc / +2 rare", "+6 unc / +4 rare", "+9 unc / +6 rare", "+12 unc / +8 rare", "+15 unc / +10 rare" },
  [WPROG_SCYTHE_EXTRA_CHOICE]= { "+1 card" },
};

static const char* whit_keys[WID_COUNT] = {
  "wh_wand", "wh_spin", "wh_chain", "wh_orbit", "wh_bomb", "wh_turret", "wh_trail", "wh_scythe"
};
static const char* wkill_keys[WID_COUNT] = {
  "wk_wand", "wk_spin", "wk_chain", "wk_orbit", "wk_bomb", "wk_turret", "wk_trail", "wk_scythe"
};
static const char* wspent_keys[WID_COUNT] = {
  "ws_wand", "ws_spin", "ws_chain", "ws_orbit", "ws_bomb", "ws_turret", "ws_trail", "ws_scythe"
};
static const char* wbonus_keys[WID_COUNT] = {
  "wb_wand", "wb_spin", "wb_chain", "wb_orbit", "wb_bomb", "wb_turret", "wb_trail", "wb_scythe"
};
static const char* wtier_keys[WPROG_COUNT] = {
  "wt_wand_cd", "wt_wand_reach", "wt_wand_free", "wt_wand_luck", "wt_wand_xtra",
  "wt_spin_cd", "wt_spin_reach", "wt_spin_free", "wt_spin_luck", "wt_spin_xtra",
  "wt_chain_cd", "wt_chain_reach", "wt_chain_free", "wt_chain_luck", "wt_chain_xtra",
  "wt_orbit_cd", "wt_orbit_reach", "wt_orbit_free", "wt_orbit_luck", "wt_orbit_xtra", "wt_orbit_shatter",
  "wt_bomb_cd", "wt_bomb_reach", "wt_bomb_free", "wt_bomb_luck", "wt_bomb_xtra",
  "wt_turret_cd", "wt_turret_reach", "wt_turret_free", "wt_turret_luck", "wt_turret_xtra", "wt_turret_dur",
  "wt_trail_cd", "wt_trail_reach", "wt_trail_free", "wt_trail_luck", "wt_trail_xtra",
  "wt_scythe_cd", "wt_scythe_reach", "wt_scythe_free", "wt_scythe_luck", "wt_scythe_xtra",
};

static const char* weapon_names[WID_COUNT] = {
  "WAND", "SPIN", "CHAIN", "ORBIT", "BOMB", "TURRET", "TRAIL", "SCYTHE"
};

// Called from progress_init()
static void wprog_init(void)
{
  memset(wprogress, 0, sizeof(wprogress));
  memset(wtiers, 0, sizeof(wtiers));
  for (int i = 0; i < WID_COUNT; i++) {
    wprogress[i].total_hits   = save_data_get_int(whit_keys[i], 0);
    wprogress[i].total_kills  = save_data_get_int(wkill_keys[i], 0);
    wprogress[i].points_spent = save_data_get_int(wspent_keys[i], 0);
    wprogress[i].bonus_points = save_data_get_int(wbonus_keys[i], 0);
  }
  for (int i = 0; i < WPROG_COUNT; i++) {
    wtiers[i] = save_data_get_int(wtier_keys[i], -1);
    if (wtiers[i] < 0) wtiers[i] = 0;
    if (wtiers[i] > wprog_max_tiers[i]) wtiers[i] = wprog_max_tiers[i];
  }

}

// Called from progress_save()
static void wprog_save(void)
{
  for (int i = 0; i < WID_COUNT; i++) {
    save_data_set_int(whit_keys[i],   wprogress[i].total_hits);
    save_data_set_int(wkill_keys[i],  wprogress[i].total_kills);
    save_data_set_int(wspent_keys[i], wprogress[i].points_spent);
    save_data_set_int(wbonus_keys[i], wprogress[i].bonus_points);
  }
  for (int i = 0; i < WPROG_COUNT; i++) {
    save_data_set_int(wtier_keys[i], wtiers[i]);
  }
}

void progress_bank_weapon_hits(int hits[WID_COUNT])
{
  for (int i = 0; i < WID_COUNT; i++) {
    wprogress[i].total_hits += hits[i];
  }
  progress_save();
}

void progress_bank_weapon_kills(int kills[WID_COUNT])
{
  for (int i = 0; i < WID_COUNT; i++) {
    wprogress[i].total_kills += kills[i];
  }
  progress_save();
}

void progress_bank_source_hits(int fc_hits, int cd_hits)
{
  fire_cone_lifetime_hits += fc_hits;
  conductor_lifetime_hits += cd_hits;
  progress_save();
}

int progress_get_fire_cone_lifetime_hits(void)
{
  return fire_cone_lifetime_hits;
}

int progress_get_conductor_lifetime_hits(void)
{
  return conductor_lifetime_hits;
}

void progress_add_weapon_bonus(WeaponId_t id, int pts)
{
  if (id < 0 || id >= WID_COUNT) return;
  wprogress[id].bonus_points += pts;
  progress_save();
}

int progress_get_weapon_lifetime_hits(WeaponId_t id)
{
  if (id < 0 || id >= WID_COUNT) return 0;
  return wprogress[id].total_hits;
}

int progress_get_weapon_lifetime_kills(WeaponId_t id)
{
  if (id < 0 || id >= WID_COUNT) return 0;
  return wprogress[id].total_kills;
}

int progress_get_weapon_available_points(WeaponId_t id)
{
  if (id < 0 || id >= WID_COUNT) return 0;
  return wprogress[id].total_hits + wprogress[id].bonus_points - wprogress[id].points_spent;
}

int wprog_get_tier(WeaponProgressId_t id)
{
  if (id < 0 || id >= WPROG_COUNT) return 0;
  return wtiers[id];
}

int wprog_get_max_tier(WeaponProgressId_t id)
{
  if (id < 0 || id >= WPROG_COUNT) return 0;
  return wprog_max_tiers[id];
}

int wprog_get_next_cost(WeaponProgressId_t id)
{
  if (id < 0 || id >= WPROG_COUNT) return -1;
  if (wtiers[id] >= wprog_max_tiers[id]) return -1;
  return wprog_costs[id][wtiers[id]];
}

int wprog_can_afford(WeaponProgressId_t id)
{
  if (id < 0 || id >= WPROG_COUNT) return 0;
  if (wtiers[id] >= wprog_max_tiers[id]) return 0;
  int cost = wprog_costs[id][wtiers[id]];
  WeaponId_t owner = wprog_owner[id];
  return progress_get_weapon_available_points(owner) >= cost;
}

int wprog_purchase(WeaponProgressId_t id)
{
  if (!wprog_can_afford(id)) return 0;
  int cost = wprog_costs[id][wtiers[id]];
  WeaponId_t owner = wprog_owner[id];
  wprogress[owner].points_spent += cost;
  wtiers[id]++;
  progress_save();
  return 1;
}

const char* wprog_get_name(WeaponProgressId_t id)
{
  if (id < 0 || id >= WPROG_COUNT) return "";
  return wprog_names[id];
}

const char* wprog_get_detail(WeaponProgressId_t id)
{
  if (id < 0 || id >= WPROG_COUNT) return "";
  return wprog_details[id];
}

const char* wprog_get_tier_desc(WeaponProgressId_t id, int tier)
{
  if (id < 0 || id >= WPROG_COUNT) return "";
  if (tier < 0 || tier >= wprog_max_tiers[id]) return "";
  return wprog_tier_descs[id][tier];
}

const char* wprog_get_weapon_name(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return "";
  return weapon_names[wid];
}

int wprog_has_affordable(void)
{
  for (int i = 0; i < WPROG_COUNT; i++) {
    if (wprog_can_afford((WeaponProgressId_t)i)) return 1;
  }
  return 0;
}

int wprog_has_affordable_for(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 0;
  for (int i = 0; i < WPROG_COUNT; i++) {
    if (wprog_owner[i] == wid && wprog_can_afford((WeaponProgressId_t)i)) return 1;
  }
  return 0;
}

// Base enum offset for each weapon's standard 5 tracks (cooldown, reach, free, rarity, choice)
static const WeaponProgressId_t wprog_base[WID_COUNT] = {
  WPROG_WAND_COOLDOWN, WPROG_SPIN_COOLDOWN, WPROG_CHAIN_COOLDOWN, WPROG_ORBIT_COOLDOWN,
  WPROG_BOMB_COOLDOWN, WPROG_TURRET_COOLDOWN, WPROG_TRAIL_COOLDOWN, WPROG_SCYTHE_COOLDOWN,
};

float wprog_get_cooldown_mult(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 1.0f;
  return 1.0f - 0.03f * (float)wtiers[wprog_base[wid]];
}

float wprog_get_reach_bonus(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 0.0f;
  int tier = wtiers[wprog_base[wid] + 1];
  switch (wid) {
    case WID_WAND:   { static const float v[] = {0, 0.10f, 0.20f, 0.30f}; return v[tier]; }
    case WID_SPIN:   { static const float v[] = {0, 3, 7, 12};            return v[tier]; }
    case WID_CHAIN:  { static const float v[] = {0, 5, 12, 20};           return v[tier]; }
    case WID_ORBIT:  { static const float v[] = {0, 5, 12, 20};           return v[tier]; }
    case WID_BOMB:   { static const float v[] = {0, 3, 7, 12};            return v[tier]; }
    case WID_TURRET: { static const float v[] = {0, 8, 18, 30};           return v[tier]; }
    case WID_TRAIL:  { static const float v[] = {0, 2, 4, 7};             return v[tier]; }
    case WID_SCYTHE: { static const float v[] = {0, 8, 16, 25};           return v[tier]; }
    default: return 0.0f;
  }
}

int wprog_has_free_upgrade(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 0;
  return wtiers[wprog_base[wid] + 2] > 0;
}

int wprog_get_rarity_uncommon_bonus(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 0;
  return 3 * wtiers[wprog_base[wid] + 3];
}

int wprog_get_rarity_rare_bonus(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 0;
  return 2 * wtiers[wprog_base[wid] + 3];
}

int wprog_get_extra_choices(WeaponId_t wid)
{
  if (wid < 0 || wid >= WID_COUNT) return 0;
  return wtiers[wprog_base[wid] + 4];
}

int wprog_get_orbit_shatter_bonus(void)
{
  return wtiers[WPROG_ORBIT_SHATTER];
}

float wprog_get_turret_duration_bonus(void)
{
  static const float v[4] = { 0.0f, 0.3f, 0.7f, 1.2f };
  return v[wtiers[WPROG_TURRET_DURATION]];
}
