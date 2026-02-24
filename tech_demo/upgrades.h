#ifndef UPGRADES_H
#define UPGRADES_H

#define UPG_MAX_TIER 3

typedef enum {
  // Wand (7)
  UPG_WAND_COOLDOWN,
  UPG_WAND_MULTISHOT,
  UPG_WAND_PIERCE,
  UPG_WAND_PROJ_SPEED,
  UPG_WAND_RICOCHET,
  UPG_WAND_HOMING,
  UPG_WAND_SPLINTER,
  // Spin (7)
  UPG_SPIN_COOLDOWN,
  UPG_SPIN_RADIUS,
  UPG_SPIN_DOUBLE_PULSE,
  UPG_SPIN_LINGER_ZONE,
  UPG_SPIN_VACUUM,
  UPG_SPIN_AFTERSHOCK,
  UPG_SPIN_ELECTRIC,
  // Chain (7)
  UPG_CHAIN_COOLDOWN,
  UPG_CHAIN_EXTRA_JUMPS,
  UPG_CHAIN_RADIUS,
  UPG_CHAIN_LINGER_ARC,
  UPG_CHAIN_OVERLOAD,
  UPG_CHAIN_MAGNETIC_PULL,
  UPG_CHAIN_STATIC_FIELD,
  // Orbit (7)
  UPG_ORBIT_EXTRA_ORB,
  UPG_ORBIT_DURATION,
  UPG_ORBIT_RADIUS,
  UPG_ORBIT_LINGER_TRAIL,
  UPG_ORBIT_GRAVITY_WELL,
  UPG_ORBIT_SHATTER,
  UPG_ORBIT_GROWING_ORB,
  // Bomb (7)
  UPG_BOMB_COOLDOWN,
  UPG_BOMB_BLAST_RADIUS,
  UPG_BOMB_MULTI_BOMB,
  UPG_BOMB_LINGER_FIRE,
  UPG_BOMB_CLUSTER,
  UPG_BOMB_NAPALM,
  UPG_BOMB_CRATER,
  // Count
  UPG_COUNT  // = 35
} UpgradeId_t;

typedef enum {
  RARITY_COMMON,
  RARITY_UNCOMMON,
  RARITY_RARE
} UpgradeRarity_t;

typedef struct {
  UpgradeId_t id;
  int weapon_type;              // WeaponType_t value
  const char* weapon_name;
  const char* upgrade_name;
  const char* descriptions[UPG_MAX_TIER];
  UpgradeRarity_t rarity;
  int weight;                   // common=60, uncommon=30, rare=10
} UpgradeInfo_t;

void upgrades_init(void);
void upgrades_reset(void);
int  upgrades_get_tier(UpgradeId_t id);
void upgrades_apply(UpgradeId_t id);
int  upgrades_roll_cards(UpgradeId_t out_cards[3]);
const UpgradeInfo_t* upgrades_get_info(UpgradeId_t id);

// Get all upgrade IDs for a weapon type. Returns count, fills out_ids (max UPG_COUNT).
int upgrades_get_for_weapon(int weapon_type, UpgradeId_t* out_ids, int max_out);

#endif /* UPGRADES_H */
