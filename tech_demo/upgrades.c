#include <stdlib.h>
#include "upgrades.h"
#include "weapons.h"
#include "xp.h"
#include "progress.h"

static int tiers[UPG_COUNT];

static const UpgradeInfo_t upgrade_table[UPG_COUNT] = {
  // === WAND === (4C / 3U / 2R)
  [UPG_WAND_COOLDOWN] = {
    UPG_WAND_COOLDOWN, WEAPON_WAND, "WAND", "Cooldown",
    {"Fire rate +15%", "Fire rate +30%", "Fire rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_PROJ_SIZE] = {
    UPG_WAND_PROJ_SIZE, WEAPON_WAND, "WAND", "Projectile Size",
    {"+25% bullet size", "+50% bullet size", "+75% bullet size"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_PIERCE] = {
    UPG_WAND_PIERCE, WEAPON_WAND, "WAND", "Pierce",
    {"Passes through 1 enemy", "Through 2 enemies", "Through all enemies in line"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_BULLET_SPEED] = {
    UPG_WAND_BULLET_SPEED, WEAPON_WAND, "WAND", "Bullet Speed",
    {"+25% projectile speed", "+50% speed", "+75% speed"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_MULTISHOT] = {
    UPG_WAND_MULTISHOT, WEAPON_WAND, "WAND", "Multi-shot",
    {"2 projectiles in a fan", "3 projectiles", "4 projectiles"},
    RARITY_UNCOMMON, 30
  },
  [UPG_WAND_CONDUCTOR] = {
    UPG_WAND_CONDUCTOR, WEAPON_WAND, "WAND", "Conductor",
    {"Wand hits conduct: zap 1 enemy after 2s", "Zaps 2 enemies", "Zaps 3 enemies"},
    RARITY_UNCOMMON, 30
  },
  [UPG_WAND_RICOCHET] = {
    UPG_WAND_RICOCHET, WEAPON_WAND, "WAND", "Ricochet",
    {"Bounces to 1 nearby enemy", "Bounces to 2", "Bounces to 3"},
    RARITY_UNCOMMON, 30
  },
  [UPG_WAND_SPLINTER] = {
    UPG_WAND_SPLINTER, WEAPON_WAND, "WAND", "Splinter",
    {"On hit: 2 fragments at +/-45 deg", "3 fragments", "4 fragments + pierce"},
    RARITY_RARE, 10
  },
  [UPG_WAND_BARRAGE] = {
    UPG_WAND_BARRAGE, WEAPON_WAND, "WAND", "Barrage",
    {"Every 3rd shot: 3-round burst", "Every 2nd shot: 3-round burst", "Every shot: 5-round burst"},
    RARITY_RARE, 10
  },

  // === SPIN === (4C / 3U / 2R)
  [UPG_SPIN_COOLDOWN] = {
    UPG_SPIN_COOLDOWN, WEAPON_SPIN, "SPIN", "Cooldown",
    {"Fire rate +15%", "Fire rate +30%", "Fire rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_SPIN_RADIUS] = {
    UPG_SPIN_RADIUS, WEAPON_SPIN, "SPIN", "Radius",
    {"AoE radius +20%", "Radius +40%", "Radius +60%"},
    RARITY_COMMON, 60
  },
  [UPG_SPIN_KNOCKBACK] = {
    UPG_SPIN_KNOCKBACK, WEAPON_SPIN, "SPIN", "Knockback",
    {"+20% knockback force", "+40% knockback", "+60% knockback"},
    RARITY_COMMON, 60
  },
  [UPG_SPIN_SLOW] = {
    UPG_SPIN_SLOW, WEAPON_SPIN, "SPIN", "Slow Zone",
    {"Pulse leaves 20% slow zone 1.5s", "30% slow 2s", "40% slow 2.5s"},
    RARITY_COMMON, 60
  },
  [UPG_SPIN_DOUBLE_PULSE] = {
    UPG_SPIN_DOUBLE_PULSE, WEAPON_SPIN, "SPIN", "Double Pulse",
    {"Hits twice", "Three pulses", "Four pulses"},
    RARITY_UNCOMMON, 30
  },
  [UPG_SPIN_VACUUM] = {
    UPG_SPIN_VACUUM, WEAPON_SPIN, "SPIN", "Vacuum",
    {"Pulls enemies from donut ring inward", "Wider pull ring", "Massive pull from 2x radius"},
    RARITY_UNCOMMON, 30
  },
  [UPG_SPIN_AFTERSHOCK] = {
    UPG_SPIN_AFTERSHOCK, WEAPON_SPIN, "SPIN", "Aftershock",
    {"Shockwave toward nearest enemy", "Travels further", "Far + wider wave"},
    RARITY_UNCOMMON, 30
  },
  [UPG_SPIN_LINGER_ZONE] = {
    UPG_SPIN_LINGER_ZONE, WEAPON_SPIN, "SPIN", "Lingering Zone",
    {"Leaves damage zone for 1s", "Zone lasts 1.5s", "Zone lasts 2s"},
    RARITY_RARE, 10
  },
  [UPG_SPIN_ELECTRIC] = {
    UPG_SPIN_ELECTRIC, WEAPON_SPIN, "SPIN", "Electric Spin",
    {"Spin hits conduct: zap 1 enemy after 2s", "Zaps 2 enemies", "Zaps 3 enemies"},
    RARITY_RARE, 10
  },

  // === CHAIN === (4C / 3U / 2R)
  [UPG_CHAIN_COOLDOWN] = {
    UPG_CHAIN_COOLDOWN, WEAPON_CHAIN, "CHAIN", "Cooldown",
    {"Fire rate +15%", "Fire rate +30%", "Fire rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_CHAIN_RADIUS] = {
    UPG_CHAIN_RADIUS, WEAPON_CHAIN, "CHAIN", "Chain Radius",
    {"Jump range +20%", "Range +40%", "Range +60%"},
    RARITY_COMMON, 60
  },
  [UPG_CHAIN_MINI_STUN] = {
    UPG_CHAIN_MINI_STUN, WEAPON_CHAIN, "CHAIN", "Mini Stun",
    {"Chain hits stun 0.1s", "0.15s stun", "0.2s stun"},
    RARITY_COMMON, 60
  },
  [UPG_CHAIN_INITIAL_RANGE] = {
    UPG_CHAIN_INITIAL_RANGE, WEAPON_CHAIN, "CHAIN", "First Jump",
    {"First jump range +20%", "+40% first jump", "+60% first jump"},
    RARITY_COMMON, 60
  },
  [UPG_CHAIN_EXTRA_JUMPS] = {
    UPG_CHAIN_EXTRA_JUMPS, WEAPON_CHAIN, "CHAIN", "Extra Jumps",
    {"+1 jump (4 total)", "+2 jumps (5 total)", "+3 jumps (6 total)"},
    RARITY_UNCOMMON, 30
  },
  [UPG_CHAIN_OVERLOAD] = {
    UPG_CHAIN_OVERLOAD, WEAPON_CHAIN, "CHAIN", "Overload",
    {"3+ chain hits: final target explodes", "Medium AoE", "Large AoE + chains again"},
    RARITY_UNCOMMON, 30
  },
  [UPG_CHAIN_MAGNETIC_PULL] = {
    UPG_CHAIN_MAGNETIC_PULL, WEAPON_CHAIN, "CHAIN", "Magnetic Pull",
    {"Chained enemies pulled together", "Stronger pull", "Enemies clump at midpoint"},
    RARITY_UNCOMMON, 30
  },
  [UPG_CHAIN_LINGER_ARC] = {
    UPG_CHAIN_LINGER_ARC, WEAPON_CHAIN, "CHAIN", "Lingering Arc",
    {"Arcs persist 0.75s dealing tick damage", "Arcs persist 1.5s", "Arcs persist 2.25s"},
    RARITY_RARE, 10
  },
  [UPG_CHAIN_BOUNCE_BACK] = {
    UPG_CHAIN_BOUNCE_BACK, WEAPON_CHAIN, "CHAIN", "Bounce Back",
    {"Chain reverses through last target", "Reverses through 2", "Full reverse through all"},
    RARITY_RARE, 10
  },

  // === ORBIT === (4C / 3U / 2R)
  [UPG_ORBIT_EXTRA_ORB] = {
    UPG_ORBIT_EXTRA_ORB, WEAPON_ORBIT, "ORBIT", "Extra Orb",
    {"2 orbs (180 apart)", "3 orbs (120 apart)", "4 orbs (90 apart)"},
    RARITY_COMMON, 60
  },
  [UPG_ORBIT_DURATION] = {
    UPG_ORBIT_DURATION, WEAPON_ORBIT, "ORBIT", "Duration",
    {"Lasts 3.5s", "Lasts 4.0s", "Lasts 5.0s (permanent uptime!)"},
    RARITY_COMMON, 60
  },
  [UPG_ORBIT_SPEED] = {
    UPG_ORBIT_SPEED, WEAPON_ORBIT, "ORBIT", "Orbit Speed",
    {"+15% rotation speed", "+30% speed", "+45% speed"},
    RARITY_COMMON, 60
  },
  [UPG_ORBIT_RADIUS] = {
    UPG_ORBIT_RADIUS, WEAPON_ORBIT, "ORBIT", "Orbit Radius",
    {"Orbit +15px wider", "+30px wider", "+45px wider"},
    RARITY_COMMON, 60
  },
  [UPG_ORBIT_GROWING_ORB] = {
    UPG_ORBIT_GROWING_ORB, WEAPON_ORBIT, "ORBIT", "Growing Orb",
    {"Orbs grow to 1.5x size over duration", "Grow to 2x", "3x + hits faster as they grow"},
    RARITY_UNCOMMON, 30
  },
  [UPG_ORBIT_CONDUCTOR] = {
    UPG_ORBIT_CONDUCTOR, WEAPON_ORBIT, "ORBIT", "Conductor",
    {"Orb hits conduct: zap 1 enemy after 2s", "Zaps 2 enemies", "Zaps 3 enemies"},
    RARITY_UNCOMMON, 30
  },
  [UPG_ORBIT_GRAVITY_WELL] = {
    UPG_ORBIT_GRAVITY_WELL, WEAPON_ORBIT, "ORBIT", "Gravity Well",
    {"Orbs leave a trailing slow field", "Wider + longer field", "Biggest field + bonus hit in zone"},
    RARITY_UNCOMMON, 30
  },
  [UPG_ORBIT_LINGER_TRAIL] = {
    UPG_ORBIT_LINGER_TRAIL, WEAPON_ORBIT, "ORBIT", "Lingering Trail",
    {"Orb leaves damaging trail 0.3s", "Trail lasts 0.5s", "Trail lasts 0.8s"},
    RARITY_RARE, 10
  },
  [UPG_ORBIT_SHATTER] = {
    UPG_ORBIT_SHATTER, WEAPON_ORBIT, "ORBIT", "Shatter",
    {"Orbs explode into 3 projectiles", "4 projectiles", "6 projectiles + pierce"},
    RARITY_RARE, 10
  },

  // === BOMB === (4C / 3U / 2R)
  [UPG_BOMB_COOLDOWN] = {
    UPG_BOMB_COOLDOWN, WEAPON_BOMB, "BOMB", "Cooldown",
    {"Fire rate +15%", "Fire rate +30%", "Fire rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_BOMB_BLAST_RADIUS] = {
    UPG_BOMB_BLAST_RADIUS, WEAPON_BOMB, "BOMB", "Blast Radius",
    {"Explosion +20% bigger", "+40% bigger", "+60% bigger"},
    RARITY_COMMON, 60
  },
  [UPG_BOMB_FLIGHT_SPEED] = {
    UPG_BOMB_FLIGHT_SPEED, WEAPON_BOMB, "BOMB", "Flight Speed",
    {"Bomb arrives 20% faster", "35% faster", "50% faster"},
    RARITY_COMMON, 60
  },
  [UPG_BOMB_IMPLOSION] = {
    UPG_BOMB_IMPLOSION, WEAPON_BOMB, "BOMB", "Implosion",
    {"Blast pulls enemies inward", "Stronger pull", "Max pull force"},
    RARITY_COMMON, 60
  },
  [UPG_BOMB_CONDUCTOR] = {
    UPG_BOMB_CONDUCTOR, WEAPON_BOMB, "BOMB", "Conductor",
    {"Blast conducts: zap 1 enemy after 2s", "Zaps 2 enemies", "Zaps 3 enemies"},
    RARITY_UNCOMMON, 30
  },
  [UPG_BOMB_CLUSTER] = {
    UPG_BOMB_CLUSTER, WEAPON_BOMB, "BOMB", "Cluster Bomb",
    {"4 mini-bombs scatter from explosion", "5 mini-bombs", "6 mini-bombs + bigger blasts"},
    RARITY_UNCOMMON, 30
  },
  [UPG_BOMB_NAPALM] = {
    UPG_BOMB_NAPALM, WEAPON_BOMB, "BOMB", "Napalm",
    {"Impact leaves spreading fire (2s)", "Faster spread, larger", "Huge spread + intensifies"},
    RARITY_UNCOMMON, 30
  },
  [UPG_BOMB_LINGER_FIRE] = {
    UPG_BOMB_LINGER_FIRE, WEAPON_BOMB, "BOMB", "Lingering Fire",
    {"Ground burns for 2.25s", "Burns 3.0s", "Burns 4.5s"},
    RARITY_RARE, 10
  },
  [UPG_BOMB_CRATER] = {
    UPG_BOMB_CRATER, WEAPON_BOMB, "BOMB", "Crater",
    {"Explosion leaves slow zone 5s", "7s duration", "8s + bonus hit in zone"},
    RARITY_RARE, 10
  },

  // === TURRET === (4C / 3U / 2R)
  [UPG_TURRET_COOLDOWN] = {
    UPG_TURRET_COOLDOWN, WEAPON_TURRET, "TURRET", "Cooldown",
    {"Deploy rate +15%", "Deploy rate +30%", "Deploy rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_TURRET_DURATION] = {
    UPG_TURRET_DURATION, WEAPON_TURRET, "TURRET", "Duration",
    {"Lasts 4.5s", "Lasts 5.5s", "Lasts 7.0s"},
    RARITY_COMMON, 60
  },
  [UPG_TURRET_FIRE_RATE] = {
    UPG_TURRET_FIRE_RATE, WEAPON_TURRET, "TURRET", "Fire Rate",
    {"Fires every 0.55s", "Every 0.45s", "Every 0.35s"},
    RARITY_COMMON, 60
  },
  [UPG_TURRET_RANGE] = {
    UPG_TURRET_RANGE, WEAPON_TURRET, "TURRET", "Range",
    {"Turret range +20px", "+35px range", "+50px range"},
    RARITY_COMMON, 60
  },
  [UPG_TURRET_SLOW_ZONE] = {
    UPG_TURRET_SLOW_ZONE, WEAPON_TURRET, "TURRET", "Slow Zone",
    {"Hits leave 30% slow 1.5s", "40% slow 2s", "50% slow 3s"},
    RARITY_UNCOMMON, 30
  },
  [UPG_TURRET_SPREAD_SHOT] = {
    UPG_TURRET_SPREAD_SHOT, WEAPON_TURRET, "TURRET", "Spread Shot",
    {"2 bullets per shot", "3 bullets", "4 bullet fan"},
    RARITY_UNCOMMON, 30
  },
  [UPG_TURRET_CONDUCTOR] = {
    UPG_TURRET_CONDUCTOR, WEAPON_TURRET, "TURRET", "Conductor",
    {"Hits conduct: zap 1 enemy after 2s", "Zaps 2 enemies", "Zaps 3 enemies"},
    RARITY_UNCOMMON, 30
  },
  [UPG_TURRET_OVERCHARGE] = {
    UPG_TURRET_OVERCHARGE, WEAPON_TURRET, "TURRET", "Overcharge",
    {"AoE on expiry 50px", "70px blast", "100px + fire zone 2s"},
    RARITY_RARE, 10
  },
  [UPG_TURRET_TESLA_COIL] = {
    UPG_TURRET_TESLA_COIL, WEAPON_TURRET, "TURRET", "Tesla Coil",
    {"Chain zaps 2 enemies every 1.5s", "Chains to 3", "Chains to 5 + stun"},
    RARITY_RARE, 10
  },

  // === TRAIL === (4C / 3U / 2R)
  [UPG_TRAIL_COOLDOWN] = {
    UPG_TRAIL_COOLDOWN, WEAPON_TRAIL, "TRAIL", "Cooldown",
    {"Cooldown -15%", "Cooldown -30%", "Cooldown -45%"},
    RARITY_COMMON, 60
  },
  [UPG_TRAIL_DURATION] = {
    UPG_TRAIL_DURATION, WEAPON_TRAIL, "TRAIL", "Duration",
    {"Active for 3.0s", "3.5s", "4.5s"},
    RARITY_COMMON, 60
  },
  [UPG_TRAIL_PERSIST] = {
    UPG_TRAIL_PERSIST, WEAPON_TRAIL, "TRAIL", "Persist",
    {"Trail lasts 1.5s", "2.0s", "3.0s"},
    RARITY_COMMON, 60
  },
  [UPG_TRAIL_WIDE] = {
    UPG_TRAIL_WIDE, WEAPON_TRAIL, "TRAIL", "Wide Trail",
    {"Trail width 24px", "32px", "40px"},
    RARITY_COMMON, 60
  },
  [UPG_TRAIL_EMBER_BURST] = {
    UPG_TRAIL_EMBER_BURST, WEAPON_TRAIL, "TRAIL", "Ember Burst",
    {"Segments pop on expiry (20px)", "30px", "40px + damage"},
    RARITY_UNCOMMON, 30
  },
  [UPG_TRAIL_SCORCHED_EARTH] = {
    UPG_TRAIL_SCORCHED_EARTH, WEAPON_TRAIL, "TRAIL", "Scorched Earth",
    {"Trail slows enemies 20%", "30% slow", "40% slow"},
    RARITY_UNCOMMON, 30
  },
  [UPG_TRAIL_BLAZING_SPEED] = {
    UPG_TRAIL_BLAZING_SPEED, WEAPON_TRAIL, "TRAIL", "Blazing Speed",
    {"+15% move speed while active", "+25%", "+40%"},
    RARITY_UNCOMMON, 30
  },
  [UPG_TRAIL_INFERNO_WAKE] = {
    UPG_TRAIL_INFERNO_WAKE, WEAPON_TRAIL, "TRAIL", "Inferno Wake",
    {"Segments chain-detonate on expiry (30px)", "40px AoE", "50px + linger fire"},
    RARITY_RARE, 10
  },
  [UPG_TRAIL_HEAT_MIRAGE] = {
    UPG_TRAIL_HEAT_MIRAGE, WEAPON_TRAIL, "TRAIL", "Heat Mirage",
    {"Trail pulls nearby enemies (40px)", "55px pull range", "70px strong pull"},
    RARITY_RARE, 10
  },

};

void upgrades_init(void)
{
  for (int i = 0; i < UPG_COUNT; i++)
    tiers[i] = 0;
}

void upgrades_reset(void)
{
  upgrades_init();
}

int upgrades_get_tier(UpgradeId_t id)
{
  if (id < 0 || id >= UPG_COUNT) return 0;
  return tiers[id];
}

void upgrades_apply(UpgradeId_t id)
{
  if (id < 0 || id >= UPG_COUNT) return;
  if (tiers[id] < UPG_MAX_TIER)
    tiers[id]++;
}

const UpgradeInfo_t* upgrades_get_info(UpgradeId_t id)
{
  if (id < 0 || id >= UPG_COUNT) return &upgrade_table[0];
  return &upgrade_table[id];
}

int upgrades_get_for_weapon(int weapon_type, UpgradeId_t* out_ids, int max_out)
{
  int count = 0;
  for (int i = 0; i < UPG_COUNT && count < max_out; i++) {
    if (upgrade_table[i].weapon_type == weapon_type) {
      out_ids[count++] = (UpgradeId_t)i;
    }
  }
  return count;
}

int upgrades_roll_cards(UpgradeId_t out_cards[3])
{
  // Build candidate pool: owned weapons, tier < max
  UpgradeId_t candidates[UPG_COUNT];
  int weights[UPG_COUNT];
  int candidate_count = 0;

  // Level-gated rarity scaling:
  //   Level 1: commons only
  //   Level 2: uncommons unlock (half weight), rares still locked
  //   Level 3: uncommons full, rares unlock (half weight)
  //   Level 4+: everything at full weight
  int level = xp_get_level();
  float uncommon_mult = 1.0f;
  float rare_mult = 1.0f;
  if (level <= 1) {
    uncommon_mult = 0.0f;
    rare_mult = 0.0f;
  } else if (level <= 2) {
    uncommon_mult = 0.50f;
    rare_mult = 0.0f;
  } else if (level <= 3) {
    uncommon_mult = 1.0f;
    rare_mult = 0.50f;
  }

  for (int i = 0; i < UPG_COUNT; i++) {
    if (tiers[i] >= UPG_MAX_TIER) continue;
    if (!weapons_has(upgrade_table[i].weapon_type)) continue;
    candidates[candidate_count] = (UpgradeId_t)i;
    int w = upgrade_table[i].weight;
    if (upgrade_table[i].rarity == RARITY_UNCOMMON) {
      w = (int)(w * uncommon_mult);
      if (w < 1 && uncommon_mult > 0.0f) w = 1;
      w += global_get_uncommon_weight_bonus();
    } else if (upgrade_table[i].rarity == RARITY_RARE) {
      w = (int)(w * rare_mult);
      if (w < 1 && rare_mult > 0.0f) w = 1;
      w += global_get_rare_weight_bonus();
    }
    weights[candidate_count] = w;
    candidate_count++;
  }

  if (candidate_count == 0) return 0;

  int card_count = 0;
  int picked_weapon = -1;

  for (int c = 0; c < 3 && candidate_count > 0; c++) {
    // For card 2, boost weight of different weapons to encourage diversity
    int boosted_weights[UPG_COUNT];
    int total_weight = 0;
    for (int i = 0; i < candidate_count; i++) {
      boosted_weights[i] = weights[i];
      if (c == 1 && picked_weapon >= 0 &&
          upgrade_table[candidates[i]].weapon_type != picked_weapon) {
        boosted_weights[i] *= 2;
      }
      total_weight += boosted_weights[i];
    }

    if (total_weight <= 0) break;

    // Weighted random pick
    int roll = rand() % total_weight;
    int chosen = 0;
    for (int i = 0; i < candidate_count; i++) {
      roll -= boosted_weights[i];
      if (roll < 0) { chosen = i; break; }
    }

    out_cards[card_count] = candidates[chosen];
    if (c == 0) picked_weapon = upgrade_table[candidates[chosen]].weapon_type;
    card_count++;

    // Remove chosen from candidates (swap with last)
    candidates[chosen] = candidates[candidate_count - 1];
    weights[chosen] = weights[candidate_count - 1];
    candidate_count--;
  }

  return card_count;
}

int upgrades_roll_cards_for_weapon(int weapon_type, UpgradeId_t out_cards[3])
{
  UpgradeId_t candidates[UPG_COUNT];
  int weights[UPG_COUNT];
  int candidate_count = 0;

  for (int i = 0; i < UPG_COUNT; i++) {
    if (tiers[i] >= UPG_MAX_TIER) continue;
    if (upgrade_table[i].weapon_type != weapon_type) continue;
    candidates[candidate_count] = (UpgradeId_t)i;
    weights[candidate_count] = upgrade_table[i].weight;
    candidate_count++;
  }

  if (candidate_count == 0) return 0;

  int card_count = 0;
  for (int c = 0; c < 3 && candidate_count > 0; c++) {
    int total_weight = 0;
    for (int i = 0; i < candidate_count; i++)
      total_weight += weights[i];
    if (total_weight <= 0) break;

    int roll = rand() % total_weight;
    int chosen = 0;
    for (int i = 0; i < candidate_count; i++) {
      roll -= weights[i];
      if (roll < 0) { chosen = i; break; }
    }

    out_cards[card_count++] = candidates[chosen];
    candidates[chosen] = candidates[candidate_count - 1];
    weights[chosen] = weights[candidate_count - 1];
    candidate_count--;
  }

  return card_count;
}

/* ── Reroll charges (per-run) ── */

static int rerolls_remaining = 0;

void upgrades_reset_rerolls(void) { rerolls_remaining = global_get_rerolls_per_run(); }
int  upgrades_get_rerolls(void)   { return rerolls_remaining; }
void upgrades_use_reroll(void)    { if (rerolls_remaining > 0) rerolls_remaining--; }
