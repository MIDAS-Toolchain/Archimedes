#ifndef UPGRADES_H
#define UPGRADES_H

#define UPG_MAX_TIER 3

typedef enum {
  // Wand (9: 4C / 3U / 2R)
  UPG_WAND_COOLDOWN,
  UPG_WAND_PROJ_SIZE,
  UPG_WAND_PIERCE,
  UPG_WAND_BULLET_SPEED,
  UPG_WAND_MULTISHOT,
  UPG_WAND_CONDUCTOR,
  UPG_WAND_RICOCHET,
  UPG_WAND_SPLINTER,
  UPG_WAND_BARRAGE,
  // Spin (9: 4C / 3U / 2R)
  UPG_SPIN_COOLDOWN,
  UPG_SPIN_RADIUS,
  UPG_SPIN_KNOCKBACK,
  UPG_SPIN_SLOW,
  UPG_SPIN_DOUBLE_PULSE,
  UPG_SPIN_VACUUM,
  UPG_SPIN_AFTERSHOCK,
  UPG_SPIN_LINGER_ZONE,
  UPG_SPIN_ELECTRIC,
  // Chain (9: 4C / 3U / 2R)
  UPG_CHAIN_COOLDOWN,
  UPG_CHAIN_RADIUS,
  UPG_CHAIN_MINI_STUN,
  UPG_CHAIN_INITIAL_RANGE,
  UPG_CHAIN_EXTRA_JUMPS,
  UPG_CHAIN_OVERLOAD,
  UPG_CHAIN_MAGNETIC_PULL,
  UPG_CHAIN_LINGER_ARC,
  UPG_CHAIN_BOUNCE_BACK,
  // Orbit (9: 4C / 3U / 2R)
  UPG_ORBIT_EXTRA_ORB,
  UPG_ORBIT_DURATION,
  UPG_ORBIT_SPEED,
  UPG_ORBIT_RADIUS,
  UPG_ORBIT_GROWING_ORB,
  UPG_ORBIT_CONDUCTOR,
  UPG_ORBIT_GRAVITY_WELL,
  UPG_ORBIT_LINGER_TRAIL,
  UPG_ORBIT_SHATTER,
  // Bomb (9: 4C / 3U / 2R)
  UPG_BOMB_COOLDOWN,
  UPG_BOMB_BLAST_RADIUS,
  UPG_BOMB_FLIGHT_SPEED,
  UPG_BOMB_IMPLOSION,
  UPG_BOMB_CONDUCTOR,
  UPG_BOMB_CLUSTER,
  UPG_BOMB_NAPALM,
  UPG_BOMB_LINGER_FIRE,
  UPG_BOMB_CRATER,
  // Turret (9: 4C / 3U / 2R)
  UPG_TURRET_COOLDOWN,
  UPG_TURRET_DURATION,
  UPG_TURRET_FIRE_RATE,
  UPG_TURRET_RANGE,
  UPG_TURRET_SLOW_ZONE,
  UPG_TURRET_SPREAD_SHOT,
  UPG_TURRET_CONDUCTOR,
  UPG_TURRET_OVERCHARGE,
  UPG_TURRET_TESLA_COIL,
  // Trail (9: 4C / 3U / 2R)
  UPG_TRAIL_COOLDOWN,
  UPG_TRAIL_DURATION,
  UPG_TRAIL_PERSIST,
  UPG_TRAIL_WIDE,
  UPG_TRAIL_EMBER_BURST,
  UPG_TRAIL_SCORCHED_EARTH,
  UPG_TRAIL_BLAZING_SPEED,
  UPG_TRAIL_INFERNO_WAKE,
  UPG_TRAIL_HEAT_MIRAGE,
  // Count
  UPG_COUNT
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
int  upgrades_roll_cards_for_weapon(int weapon_type, UpgradeId_t out_cards[3]);
const UpgradeInfo_t* upgrades_get_info(UpgradeId_t id);

// Get all upgrade IDs for a weapon type. Returns count, fills out_ids (max UPG_COUNT).
int upgrades_get_for_weapon(int weapon_type, UpgradeId_t* out_ids, int max_out);

// Reroll charges (per-run)
void upgrades_reset_rerolls(void);
int  upgrades_get_rerolls(void);
void upgrades_use_reroll(void);

#endif /* UPGRADES_H */
