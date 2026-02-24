#include <stdlib.h>
#include "upgrades.h"
#include "weapons.h"

static int tiers[UPG_COUNT];

static const UpgradeInfo_t upgrade_table[UPG_COUNT] = {
  // === WAND ===
  [UPG_WAND_COOLDOWN] = {
    UPG_WAND_COOLDOWN, WEAPON_WAND, "WAND", "Cooldown",
    {"Fire rate +15%", "Fire rate +30%", "Fire rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_MULTISHOT] = {
    UPG_WAND_MULTISHOT, WEAPON_WAND, "WAND", "Multi-shot",
    {"2 projectiles in a fan", "3 projectiles", "4 projectiles"},
    RARITY_UNCOMMON, 30
  },
  [UPG_WAND_PIERCE] = {
    UPG_WAND_PIERCE, WEAPON_WAND, "WAND", "Pierce",
    {"Passes through 1 enemy", "Through 2 enemies", "Through all enemies in line"},
    RARITY_UNCOMMON, 30
  },
  [UPG_WAND_PROJ_SPEED] = {
    UPG_WAND_PROJ_SPEED, WEAPON_WAND, "WAND", "Velocity",
    {"Projectile speed +25%", "Speed +50%", "Speed +75%"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_RICOCHET] = {
    UPG_WAND_RICOCHET, WEAPON_WAND, "WAND", "Ricochet",
    {"Bounces to 1 nearby enemy", "Bounces to 2", "Bounces to 3"},
    RARITY_UNCOMMON, 30
  },
  [UPG_WAND_HOMING] = {
    UPG_WAND_HOMING, WEAPON_WAND, "WAND", "Homing",
    {"Bullets gently curve toward enemies", "Stronger curve", "Aggressive tracking"},
    RARITY_COMMON, 60
  },
  [UPG_WAND_SPLINTER] = {
    UPG_WAND_SPLINTER, WEAPON_WAND, "WAND", "Splinter",
    {"On hit: 2 fragments at +/-45 deg", "3 fragments", "4 fragments + pierce"},
    RARITY_RARE, 10
  },

  // === SPIN ===
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
  [UPG_SPIN_DOUBLE_PULSE] = {
    UPG_SPIN_DOUBLE_PULSE, WEAPON_SPIN, "SPIN", "Double Pulse",
    {"Hits twice", "Three pulses", "Four pulses"},
    RARITY_UNCOMMON, 30
  },
  [UPG_SPIN_LINGER_ZONE] = {
    UPG_SPIN_LINGER_ZONE, WEAPON_SPIN, "SPIN", "Lingering Zone",
    {"Leaves damage zone for 1s", "Zone lasts 1.5s", "Zone lasts 2s"},
    RARITY_RARE, 10
  },
  [UPG_SPIN_VACUUM] = {
    UPG_SPIN_VACUUM, WEAPON_SPIN, "SPIN", "Vacuum",
    {"Pulls enemies from donut ring inward", "Wider pull ring", "Massive pull from 2x radius"},
    RARITY_UNCOMMON, 30
  },
  [UPG_SPIN_AFTERSHOCK] = {
    UPG_SPIN_AFTERSHOCK, WEAPON_SPIN, "SPIN", "Aftershock",
    {"Expanding shockwave ring after spin", "Ring travels further", "Far ring + more damage"},
    RARITY_UNCOMMON, 30
  },
  [UPG_SPIN_ELECTRIC] = {
    UPG_SPIN_ELECTRIC, WEAPON_SPIN, "SPIN", "Electric Spin",
    {"Spin-hit enemies become conductors 2s", "3s duration", "4s + 50% wider chain radius"},
    RARITY_RARE, 10
  },

  // === CHAIN ===
  [UPG_CHAIN_COOLDOWN] = {
    UPG_CHAIN_COOLDOWN, WEAPON_CHAIN, "CHAIN", "Cooldown",
    {"Fire rate +15%", "Fire rate +30%", "Fire rate +45%"},
    RARITY_COMMON, 60
  },
  [UPG_CHAIN_EXTRA_JUMPS] = {
    UPG_CHAIN_EXTRA_JUMPS, WEAPON_CHAIN, "CHAIN", "Extra Jumps",
    {"+1 jump (4 total)", "+2 jumps (5 total)", "+3 jumps (6 total)"},
    RARITY_UNCOMMON, 30
  },
  [UPG_CHAIN_RADIUS] = {
    UPG_CHAIN_RADIUS, WEAPON_CHAIN, "CHAIN", "Chain Radius",
    {"Jump range +20%", "Range +40%", "Range +60%"},
    RARITY_COMMON, 60
  },
  [UPG_CHAIN_LINGER_ARC] = {
    UPG_CHAIN_LINGER_ARC, WEAPON_CHAIN, "CHAIN", "Lingering Arc",
    {"Arcs persist 0.5s dealing tick damage", "Arcs persist 0.8s", "Arcs persist 1.2s"},
    RARITY_RARE, 10
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
  [UPG_CHAIN_STATIC_FIELD] = {
    UPG_CHAIN_STATIC_FIELD, WEAPON_CHAIN, "CHAIN", "Static Field",
    {"Chained enemies stunned 0.3s", "0.5s stun", "0.8s stun + 50% more damage"},
    RARITY_RARE, 10
  },

  // === ORBIT ===
  [UPG_ORBIT_EXTRA_ORB] = {
    UPG_ORBIT_EXTRA_ORB, WEAPON_ORBIT, "ORBIT", "Extra Orb",
    {"2 orbs (180 apart)", "3 orbs (120 apart)", "4 orbs (90 apart)"},
    RARITY_UNCOMMON, 30
  },
  [UPG_ORBIT_DURATION] = {
    UPG_ORBIT_DURATION, WEAPON_ORBIT, "ORBIT", "Duration",
    {"Lasts 3.5s", "Lasts 4.0s", "Lasts 5.0s (permanent uptime!)"},
    RARITY_COMMON, 60
  },
  [UPG_ORBIT_RADIUS] = {
    UPG_ORBIT_RADIUS, WEAPON_ORBIT, "ORBIT", "Orbit Radius",
    {"Orbit +15px wider", "+30px wider", "+50px wider"},
    RARITY_COMMON, 60
  },
  [UPG_ORBIT_LINGER_TRAIL] = {
    UPG_ORBIT_LINGER_TRAIL, WEAPON_ORBIT, "ORBIT", "Lingering Trail",
    {"Orb leaves damaging trail 0.3s", "Trail lasts 0.5s", "Trail lasts 0.8s"},
    RARITY_RARE, 10
  },
  [UPG_ORBIT_GRAVITY_WELL] = {
    UPG_ORBIT_GRAVITY_WELL, WEAPON_ORBIT, "ORBIT", "Gravity Well",
    {"Orbs leave a trailing slow field", "Wider + longer field", "Biggest field + bonus damage"},
    RARITY_UNCOMMON, 30
  },
  [UPG_ORBIT_SHATTER] = {
    UPG_ORBIT_SHATTER, WEAPON_ORBIT, "ORBIT", "Shatter",
    {"Orbs explode into 3 projectiles", "4 projectiles", "6 projectiles + pierce"},
    RARITY_UNCOMMON, 30
  },
  [UPG_ORBIT_GROWING_ORB] = {
    UPG_ORBIT_GROWING_ORB, WEAPON_ORBIT, "ORBIT", "Growing Orb",
    {"Orbs grow to 1.5x size over duration", "Grow to 2x", "3x + damage scales with size"},
    RARITY_RARE, 10
  },

  // === BOMB ===
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
  [UPG_BOMB_MULTI_BOMB] = {
    UPG_BOMB_MULTI_BOMB, WEAPON_BOMB, "BOMB", "Multi-bomb",
    {"2 bombs per volley", "3 bombs", "4 bombs"},
    RARITY_UNCOMMON, 30
  },
  [UPG_BOMB_LINGER_FIRE] = {
    UPG_BOMB_LINGER_FIRE, WEAPON_BOMB, "BOMB", "Lingering Fire",
    {"Ground burns for 1.5s", "Burns 2.5s", "Burns 3.5s"},
    RARITY_RARE, 10
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
  [UPG_BOMB_CRATER] = {
    UPG_BOMB_CRATER, WEAPON_BOMB, "BOMB", "Crater",
    {"Explosion leaves slow zone 5s", "7s duration", "8s + enemies take 30% more damage"},
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

  for (int i = 0; i < UPG_COUNT; i++) {
    if (tiers[i] >= UPG_MAX_TIER) continue;
    if (!weapons_has(upgrade_table[i].weapon_type)) continue;
    candidates[candidate_count] = (UpgradeId_t)i;
    weights[candidate_count] = upgrade_table[i].weight;
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
