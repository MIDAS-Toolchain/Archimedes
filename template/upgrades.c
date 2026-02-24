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
