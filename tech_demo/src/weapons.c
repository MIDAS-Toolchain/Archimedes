#include <math.h>
#include <string.h>
#include "Archimedes.h"
#include "weapons.h"
#include "upgrades.h"
#include "player_actions.h"
#include "enemy.h"
#include "fire_particles.h"
#include "game_audio.h"
#include "game_director.h"
#include "snake.h"
#include "progress.h"
#include "achievements.h"

// ============================================================================
// Weapon Definitions
// ============================================================================

#define WAND_COOLDOWN  1.0f
#define SPIN_COOLDOWN  1.8f
#define SPIN_RADIUS    111.0f
#define SPIN_VISUAL_DURATION 0.15f

// ============================================================================
// State
// ============================================================================

static Weapon_t slots[WEAPON_MAX_SLOTS];
static int weapon_count = 0;

// Mimic weapon steal tracking
static WeaponType_t stolen_weapons[WEAPON_MAX_SLOTS];  // original type before steal
static int slot_stolen[WEAPON_MAX_SLOTS];               // 1 if stolen, 0 if not

// Wand barrage state
static int wand_shot_count = 0;
static int wand_burst_remaining = 0;
static float wand_burst_timer = 0.0f;

static float spin_visual_timer = 0.0f;
static aSoundEffect_t spin_sound;
static int spin_sound_loaded = 0;
static aSoundEffect_t chain_sound;
static int chain_sound_loaded = 0;
static aSoundEffect_t bomb_throw_sound;
static int bomb_throw_sound_loaded = 0;
static aSoundEffect_t bomb_explode_sound;
static int bomb_explode_sound_loaded = 0;

// Chain lightning state
static ChainState_t chain_state;
#define CHAIN_SEG_MAX 48
static ChainSegment_t chain_segments[CHAIN_SEG_MAX];
static int chain_segment_count = 0;

// Conductor chain state (fired on conductor debuff expiry)
#define CONDUCTOR_CHAIN_MAX 8
#define CONDUCTOR_CHAIN_RADIUS 120.0f
typedef struct {
  int targets[CHAIN_MAX_JUMPS];
  int num_targets;
  int current_jump;
  float propagation_timer;
  int active;
} ConductorChain_t;
static ConductorChain_t conductor_chains[CONDUCTOR_CHAIN_MAX];

// Orbit weapon state
static OrbitState_t orbit_state;
static OrbitHitEntry_t orbit_hits[ORBIT_MAX_HIT_TRACK];
static int orbit_hit_count = 0;

// Orbit weave state
#define ORBIT_MAX_ORBS 4
static float orbit_weave_offset[ORBIT_MAX_ORBS];

// Bomb weapon state
static BombFlight_t bomb_flights[BOMB_MAX_ACTIVE];
static BombExplosion_t bomb_explosions[BOMB_MAX_ACTIVE];

// Turret weapon state
static Turret_t turrets[TURRET_MAX];
static TurretBullet_t turret_bullets[TURRET_BULLET_MAX];
static TurretSlowZone_t turret_slow_zones[TURRET_SLOW_MAX];

// Tesla coil visual arcs
#define TESLA_MAX_ARCS 5
#define TESLA_ARC_DURATION 0.15f
#define TESLA_INTERVAL 1.5f
typedef struct {
  float x1, y1, x2, y2;
  float timer;
  int active;
} TeslaArc_t;
static TeslaArc_t tesla_arcs[TESLA_MAX_ARCS];

// Trail state
static TrailSegment_t trail_segments[TRAIL_MAX_SEGMENTS];
static TrailEmber_t trail_embers[TRAIL_EMBER_MAX];
static int trail_active;
static float trail_active_timer;
static float trail_last_x, trail_last_y;
static TrailHit_t trail_hits[TRAIL_HIT_TRACK];
static int trail_hit_count = 0;

// Inferno Wake chain detonation state
#define INFERNO_MAX_QUEUE 80
typedef struct { float x, y; } InfernoPos_t;
static InfernoPos_t inferno_queue[INFERNO_MAX_QUEUE];
static int inferno_queue_count = 0;
static int inferno_active = 0;
static int inferno_index = 0;
static float inferno_timer = 0.0f;

// Inferno explosion visuals (reuse ember pattern)
#define INFERNO_VISUAL_MAX 8
typedef struct {
  float x, y, radius;
  float timer;
  int active;
} InfernoBlast_t;
static InfernoBlast_t inferno_blasts[INFERNO_VISUAL_MAX];

// Scythe state
static float scythe_sweep_timer = 0.0f;
static float scythe_sweep_dir_x = 0.0f;
static float scythe_sweep_dir_y = 0.0f;
static int   scythe_swing_count = 0;
static float scythe_whirlwind_arc = 0.0f;  // 0 = not a whirlwind swing

// Arc-shaped zones (slow + linger damage) spawned by scythe sweeps
#define SCYTHE_ZONE_MAX 8
#define SCYTHE_ZONE_LINGER_MAX_TRACKED 50
typedef struct {
  float cx, cy;               // center (player pos at time of sweep)
  float radius;               // sweep radius
  float facing_angle;         // center angle of arc (radians)
  float arc_deg;              // arc width in degrees (360 = full circle)
  float lifetime, max_lifetime;
  float slow_mult;            // 1.0 = no slow (linger-only), < 1.0 = slow zone
  int   is_linger;            // 1 = deals damage ticks
  float tick_timer;
  int   active;
  float enemy_cd[SCYTHE_ZONE_LINGER_MAX_TRACKED];
} ScytheZone_t;
static ScytheZone_t scythe_zones[SCYTHE_ZONE_MAX];

// Cooldown refunds (applied after timer reset in update loop)
static float scythe_harvest_refund = 0.0f;
static float spin_precision_refund = 0.0f;

// Achievement challenge tracking
#define WAND_SHOT_WINDOW 3.0f
#define WAND_SHOT_LOG_MAX 32
static float wand_shot_log[WAND_SHOT_LOG_MAX];  // timestamps of recent wand shots
static int   wand_shot_log_count = 0;
static float wand_elapsed = 0.0f;               // cumulative time for wand window
static int   chain_activation_kills = 0;
static float chain_kill_window = 0.0f;           // timer: >0 means chain knockbacks resolving
static int   orbit_activation_kills = 0;
static int   trail_total_unique = 0;             // total unique enemies hit this trail activation
static int   scythe_sweep_kills = 0;
static float scythe_sweep_window = 0.0f;         // timer: >0 means sweep knockbacks resolving

// Lingering effects system (shared by rare upgrades)
#define LINGER_MAX_ZONES 24
#define LINGER_TICK_RATE 0.1f
#define LINGER_ENEMY_COOLDOWN 0.5f
#define LINGER_MAX_TRACKED 50

typedef struct {
  float x, y, radius, lifetime, max_lifetime, tick_timer;
  int active;
  aColor_t color;
  WeaponType_t source;
  float enemy_cd[LINGER_MAX_TRACKED];
} LingerZone_t;

static LingerZone_t linger_zones[LINGER_MAX_ZONES];

// Orbit linger trail timer
static float orbit_trail_timer = 0.0f;
// Gravity Well slow trail timer
static float gw_trail_timer = 0.0f;

// Damage tracking
#define WEAPON_TYPE_MAX (SOURCE_SNAKE_POP + 1)
static WeaponType_t damage_source = WEAPON_NONE;
static int weapon_total_hits[WEAPON_TYPE_MAX];
static float weapon_pickup_time[WEAPON_TYPE_MAX];

// Aftershock directional wave (Spin upgrade)
#define AFTERSHOCK_MAX_HIT 50
#define AFTERSHOCK_WIDTH 56.0f
typedef struct {
  float origin_x, origin_y;     // player position when fired
  float dir_x, dir_y;           // normalized direction toward target
  float head_dist;              // how far the wave front has traveled
  float max_dist;               // maximum travel distance
  float speed;                  // travel speed (px/s)
  float width;                  // half-width of the beam
  int active;
  int hit_enemies[AFTERSHOCK_MAX_HIT];
  int hit_count;
  int bonus_hits;
} AfterShockWave_t;
static AfterShockWave_t aftershock_wave = {0};

// Shatter projectiles (Orbit upgrade)
#define SHATTER_MAX 48
typedef struct {
  float x, y, vx, vy, lifetime;
  int pierce, active;
  int hit_enemies[4];
  int hit_count;
} ShatterProj_t;
static ShatterProj_t shatter_projs[SHATTER_MAX];

// Ghost orbits (Afterglow upgrade)
#define GHOST_ORBIT_MAX 3
typedef struct {
  float x, y;
  float angle;
  float timer;
  int orb_count;
  float radius;
  int active;
  float weave_offsets[ORBIT_MAX_ORBS];
  float trail_timer;
} GhostOrbit_t;
static GhostOrbit_t ghost_orbits[GHOST_ORBIT_MAX];

#define GHOST_ORBIT_HIT_MAX 32
#define GHOST_ORBIT_HIT_COOLDOWN 0.6f
typedef struct {
  int enemy_index;
  float cooldown;
  int ghost_index;
} GhostOrbitHit_t;
static GhostOrbitHit_t ghost_orbit_hits[GHOST_ORBIT_HIT_MAX];
static int ghost_orbit_hit_count = 0;

// Cluster mini-bombs (Bomb upgrade)
#define CLUSTER_MAX 24
typedef struct {
  float x, y, vx, vy;
  float fuse;
  int active;
} ClusterMini_t;
static ClusterMini_t cluster_minis[CLUSTER_MAX];

// Napalm zones (Bomb upgrade - fire streaks)
#define NAPALM_MAX 8
#define NAPALM_WIDTH 16.0f
#define NAPALM_ENEMY_COOLDOWN 0.5f
#define NAPALM_MAX_TRACKED 50
typedef struct {
  float x, y;           // center of the streak
  float angle;          // direction of the streak
  float current_len, max_len;  // half-length (extends both directions from center)
  float grow_time, elapsed;
  float burn_time;
  int tier;
  int active;
  float enemy_cd[NAPALM_MAX_TRACKED]; // per-enemy hit cooldown (0 = ready)
} NapalmZone_t;
static NapalmZone_t napalm_zones[NAPALM_MAX];

// Crater zones (Bomb upgrade - slow zones, also Gravity Well trail)
#define CRATER_MAX 24
typedef struct {
  float x, y, radius;
  float lifetime;
  float max_lifetime;
  float damage_mult;
  int active;
  int is_bomb_crater;  // 1 = bomb crater, 0 = gravity well
} CraterZone_t;
static CraterZone_t crater_zones[CRATER_MAX];

// Track when aftershock should fire (delayed after spin)
static int aftershock_pending = 0;
static float aftershock_delay = 0.0f;
#define AFTERSHOCK_DELAY 0.3f

// Vacuum visual + tween state
#define VACUUM_VISUAL_DURATION 0.2f
#define VACUUM_TWEEN_DURATION  0.12f
#define VACUUM_MAX_PULLS 40

typedef struct {
  int enemy_index;
  int is_snake;               // 1 = snake head, 0 = regular enemy
  float start_x, start_y;   // original position offset (dx, dy to displace)
  float dx, dy;              // total displacement to apply
} VacuumPull_t;

static float vacuum_visual_timer = 0.0f;
static float vacuum_visual_cx, vacuum_visual_cy;
static float vacuum_inner_r, vacuum_outer_r;
static VacuumPull_t vacuum_pulls[VACUUM_MAX_PULLS];
static int vacuum_pull_count = 0;
static float vacuum_tween_timer = 0.0f;

// Bomb implosion pull state (reuses VacuumPull_t pattern)
#define IMPLOSION_TWEEN_DURATION 0.15f
#define IMPLOSION_VISUAL_DURATION 0.25f
#define IMPLOSION_MAX_PULLS 40
static VacuumPull_t implosion_pulls[IMPLOSION_MAX_PULLS];
static int implosion_pull_count = 0;
static float implosion_tween_timer = 0.0f;
static float implosion_visual_timer = 0.0f;
static float implosion_visual_cx, implosion_visual_cy;
static float implosion_visual_radius;

// ============================================================================
// Upgrade-aware helpers
// ============================================================================

// Find nearest alive enemy to a point, returns index or -1
static int find_nearest_enemy(float px, float py, float* out_dx, float* out_dy)
{
  int best = -1;
  float best_dist2 = 1e18f;
  int max_e = enemy_get_max_count();
  for (int e = 0; e < max_e; e++) {
    if (!enemy_is_alive(e)) continue;
    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float dx = (ex + er) - px;
    float dy = (ey + er) - py;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_dist2) {
      best_dist2 = d2;
      best = e;
      if (out_dx) *out_dx = dx;
      if (out_dy) *out_dy = dy;
    }
  }
  // Also check snake heads
  float snake_dist = sqrtf(best_dist2);
  float sx, sy;
  if (snake_find_nearest_head(px, py, &snake_dist, &sx, &sy)) {
    if (out_dx) *out_dx = sx - px;
    if (out_dy) *out_dy = sy - py;
    best = 9999;  // Non-negative sentinel — callers only check >= 0
  }
  return best;
}

static const float cooldown_reduction[4] = { 0.0f, 0.15f, 0.30f, 0.45f };

static WeaponId_t wtype_to_wid(WeaponType_t type)
{
  switch (type) {
    case WEAPON_WAND:   return WID_WAND;
    case WEAPON_SPIN:   return WID_SPIN;
    case WEAPON_CHAIN:  return WID_CHAIN;
    case WEAPON_ORBIT:  return WID_ORBIT;
    case WEAPON_BOMB:   return WID_BOMB;
    case WEAPON_TURRET: return WID_TURRET;
    case WEAPON_TRAIL:   return WID_TRAIL;
    case WEAPON_SCYTHE:  return WID_SCYTHE;
    default:             return WID_COUNT;
  }
}

static float get_slot_cooldown(int slot)
{
  float base = slots[slot].cooldown;
  WeaponType_t type = slots[slot].type;

  // If slot is stolen, use the original weapon type for upgrade lookups
  if (type == WEAPON_NONE && slot_stolen[slot])
    type = stolen_weapons[slot];

  UpgradeId_t upg;

  switch (type) {
    case WEAPON_WAND:  upg = UPG_WAND_COOLDOWN;  break;
    case WEAPON_SPIN:  upg = UPG_SPIN_COOLDOWN;   break;
    case WEAPON_CHAIN: upg = UPG_CHAIN_COOLDOWN;  break;
    case WEAPON_ORBIT: break; // Orbit has no cooldown upgrade — uses duration instead
    case WEAPON_BOMB:  upg = UPG_BOMB_COOLDOWN;   break;
    case WEAPON_TURRET: upg = UPG_TURRET_COOLDOWN; break;
    case WEAPON_TRAIL:   upg = UPG_TRAIL_COOLDOWN;   break;
    case WEAPON_SCYTHE:  upg = UPG_SCYTHE_COOLDOWN;  break;
    default: return base;
  }

  WeaponId_t wid = wtype_to_wid(type);

  if (type == WEAPON_ORBIT)
    return base * wprog_get_cooldown_mult(wid);

  int tier = upgrades_get_tier(upg);
  float result = base * (1.0f - cooldown_reduction[tier]);
  if (wid < WID_COUNT) result *= wprog_get_cooldown_mult(wid);
  return result;
}

static float get_spin_radius(void)
{
  return SPIN_RADIUS + 18.0f * upgrades_get_tier(UPG_SPIN_RADIUS)
         + wprog_get_reach_bonus(WID_SPIN);
}

static int get_chain_max_jumps(void)
{
  return 3 + upgrades_get_tier(UPG_CHAIN_EXTRA_JUMPS);
}

static float get_chain_radius(void)
{
  return CHAIN_RADIUS + 18.0f * upgrades_get_tier(UPG_CHAIN_RADIUS)
         + wprog_get_reach_bonus(WID_CHAIN);
}

static int get_orbit_orb_count(void)
{
  return 1 + upgrades_get_tier(UPG_ORBIT_EXTRA_ORB);
}

static float get_orbit_duration(void)
{
  static const float duration_bonus[4] = { 0.0f, 0.5f, 1.0f, 2.0f };
  return ORBIT_DURATION + duration_bonus[upgrades_get_tier(UPG_ORBIT_DURATION)];
}

static float get_orbit_radius(void)
{
  return ORBIT_RADIUS + 15.0f * upgrades_get_tier(UPG_ORBIT_RADIUS)
         + wprog_get_reach_bonus(WID_ORBIT);
}

static float get_spin_knockback_mult(void)
{
  return 1.0f + 0.2f * upgrades_get_tier(UPG_SPIN_KNOCKBACK);
}

// get_spin_duration removed — spin visual uses SPIN_VISUAL_DURATION directly
// get_chain_delay removed — chain uses CHAIN_DELAY directly

static float get_chain_initial_radius(void)
{
  return get_chain_radius() * (1.0f + 0.2f * upgrades_get_tier(UPG_CHAIN_INITIAL_RANGE));
}

static float get_orbit_speed_mult(void)
{
  return 1.0f + 0.15f * upgrades_get_tier(UPG_ORBIT_SPEED);
}

static float get_weave_range(void)
{
  static const float range[4] = { 0.0f, 10.0f, 18.0f, 28.0f };
  return range[upgrades_get_tier(UPG_ORBIT_WEAVE)];
}

static float get_afterglow_duration(void)
{
  static const float dur[4] = { 0.0f, 1.0f, 2.0f, 3.0f };
  return dur[upgrades_get_tier(UPG_ORBIT_AFTERGLOW)];
}

static float get_bomb_flight_time(void)
{
  static const float mult[4] = { 1.0f, 0.80f, 0.65f, 0.50f };
  return BOMB_FLIGHT_TIME * mult[upgrades_get_tier(UPG_BOMB_FLIGHT_SPEED)];
}

static float get_bomb_implosion_force(void)
{
  static const float force[4] = { 0.0f, 60.0f, 120.0f, 180.0f };
  return force[upgrades_get_tier(UPG_BOMB_IMPLOSION)];
}

float weapons_get_wand_bullet_speed_mult(void)
{
  return 1.0f + 0.25f * upgrades_get_tier(UPG_WAND_BULLET_SPEED)
         + wprog_get_reach_bonus(WID_WAND);
}

float weapons_get_wand_proj_size_mult(void)
{
  return 1.0f + 0.25f * upgrades_get_tier(UPG_WAND_PROJ_SIZE);
}

static float get_bomb_blast_radius(void)
{
  return BOMB_EXPLOSION_RADIUS + 17.0f * upgrades_get_tier(UPG_BOMB_BLAST_RADIUS)
         + wprog_get_reach_bonus(WID_BOMB);
}

static float get_turret_duration(void)
{
  static const float d[4] = { 3.5f, 4.5f, 5.5f, 7.0f };
  return d[upgrades_get_tier(UPG_TURRET_DURATION)] + wprog_get_turret_duration_bonus();
}

static float get_turret_fire_rate(void)
{
  static const float r[4] = { 0.7f, 0.55f, 0.45f, 0.35f };
  return r[upgrades_get_tier(UPG_TURRET_FIRE_RATE)];
}

static int get_turret_count(void)
{
  return 1;
}

static int get_turret_spread(void)
{
  static const int s[4] = { 1, 2, 3, 4 };
  return s[upgrades_get_tier(UPG_TURRET_SPREAD_SHOT)];
}

static float get_turret_range(void)
{
  static const float bonus[4] = { 0.0f, 20.0f, 35.0f, 50.0f };
  return TURRET_RANGE + bonus[upgrades_get_tier(UPG_TURRET_RANGE)]
         + wprog_get_reach_bonus(WID_TURRET);
}

static int get_turret_pierce(void)
{
  static const int p[4] = { 0, 1, 2, -1 }; // -1 = infinite
  return p[upgrades_get_tier(UPG_TURRET_PIERCE)];
}

static float get_trail_duration(void)
{
  static const float d[4] = { 2.5f, 3.0f, 3.5f, 4.5f };
  return d[upgrades_get_tier(UPG_TRAIL_DURATION)];
}

static float get_trail_persist(void)
{
  static const float p[4] = { 1.0f, 1.5f, 2.0f, 3.0f };
  return p[upgrades_get_tier(UPG_TRAIL_PERSIST)];
}

static float get_trail_width(void)
{
  static const float w[4] = { 16.0f, 24.0f, 32.0f, 40.0f };
  return w[upgrades_get_tier(UPG_TRAIL_WIDE)]
         + wprog_get_reach_bonus(WID_TRAIL);
}

static void linger_spawn(float x, float y, float radius, float duration, aColor_t color)
{
  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) {
      linger_zones[i] = (LingerZone_t){
        .x = x, .y = y, .radius = radius,
        .lifetime = duration, .max_lifetime = duration,
        .tick_timer = 0.0f, .active = 1, .color = color,
        .source = damage_source
      };
      for (int ec = 0; ec < LINGER_MAX_TRACKED; ec++)
        linger_zones[i].enemy_cd[ec] = 0.0f;
      return;
    }
  }
}

static void linger_update(float dt)
{
  int max_e = enemy_get_max_count();
  if (max_e > LINGER_MAX_TRACKED) max_e = LINGER_MAX_TRACKED;

  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) continue;

    linger_zones[i].lifetime -= dt;
    if (linger_zones[i].lifetime <= 0.0f) {
      linger_zones[i].active = 0;
      continue;
    }

    // Tick down per-enemy cooldowns
    for (int e = 0; e < max_e; e++) {
      if (linger_zones[i].enemy_cd[e] > 0.0f)
        linger_zones[i].enemy_cd[e] -= dt;
    }

    linger_zones[i].tick_timer += dt;
    if (linger_zones[i].tick_timer >= LINGER_TICK_RATE) {
      linger_zones[i].tick_timer -= LINGER_TICK_RATE;

      // Set damage source to the weapon that spawned this zone
      damage_source = linger_zones[i].source;

      // Damage enemies in radius (per-enemy cooldown)
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        if (linger_zones[i].enemy_cd[e] > 0.0f) continue;

        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er;
        float ecy = ey + er;

        float dx = ecx - linger_zones[i].x;
        float dy = ecy - linger_zones[i].y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist < linger_zones[i].radius + er) {
          enemy_hit(e, 0.0f, 0.0f);
          linger_zones[i].enemy_cd[e] = LINGER_ENEMY_COOLDOWN;
          float pdx = (dist > 1.0f) ? dx / dist : 0.0f;
          float pdy = (dist > 1.0f) ? dy / dist : 1.0f;
          fire_particles_spawn(ecx, ecy, pdx, pdy);
        }
      }
      snake_check_hit_no_knockback(linger_zones[i].x, linger_zones[i].y, linger_zones[i].radius);
    }
  }
}

// Fast filled circle using horizontal scanline rects (much faster than per-pixel on WebGL)
static void draw_filled_circle_scanline(int cx, int cy, int radius)
{
  for (int dy = -radius; dy <= radius; dy++) {
    int dx = (int)sqrtf((float)(radius * radius - dy * dy));
    SDL_Rect row = { cx - dx, cy + dy, dx * 2 + 1, 1 };
    SDL_RenderFillRect(app.renderer, &row);
  }
}

static void linger_draw(void)
{
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) continue;

    float fade = linger_zones[i].lifetime / linger_zones[i].max_lifetime;
    int alpha = (int)(fade * (float)linger_zones[i].color.a * 0.4f);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    // Filled circle
    int r = (int)linger_zones[i].radius;
    int zx = (int)linger_zones[i].x;
    int zy = (int)linger_zones[i].y;

    SDL_SetRenderDrawColor(app.renderer,
      linger_zones[i].color.r, linger_zones[i].color.g, linger_zones[i].color.b,
      (uint8_t)alpha);

    draw_filled_circle_scanline(zx, zy, r);

    // Outer ring (slightly brighter)
    int ring_alpha = alpha + 40;
    if (ring_alpha > 255) ring_alpha = 255;
    SDL_SetRenderDrawColor(app.renderer,
      linger_zones[i].color.r, linger_zones[i].color.g, linger_zones[i].color.b,
      (uint8_t)ring_alpha);

    int segments = 24;
    for (int s = 0; s < segments; s++) {
      float a1 = (float)s / segments * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / segments * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        zx + (int)(cosf(a1) * (float)r),
        zy + (int)(sinf(a1) * (float)r),
        zx + (int)(cosf(a2) * (float)r),
        zy + (int)(sinf(a2) * (float)r));
    }
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Init
// ============================================================================

void weapons_init(void)
{
  for (int i = 0; i < WEAPON_MAX_SLOTS; i++) {
    slots[i] = (Weapon_t){ .type = WEAPON_NONE, .label = "", .cooldown = 0, .timer = 0 };
    stolen_weapons[i] = WEAPON_NONE;
    slot_stolen[i] = 0;
  }

  // Reset wand barrage
  wand_shot_count = 0;
  wand_burst_remaining = 0;
  wand_burst_timer = 0.0f;

  // Reset damage tracking
  damage_source = WEAPON_NONE;
  for (int i = 0; i < WEAPON_TYPE_MAX; i++) {
    weapon_total_hits[i] = 0;
    weapon_pickup_time[i] = 0.0f;
  }

  // Always start with the wand
  weapon_count = 0;
  weapons_add(WEAPON_WAND);

  // Load spin attack sound
  if (a_AudioLoadSound("resources/soundEffects/swish-7.wav", &spin_sound) == 0) {
    spin_sound_loaded = 1;
  }

  // Load chain lightning sound
  if (a_AudioLoadSound("resources/soundEffects/lightning.wav", &chain_sound) == 0) {
    chain_sound_loaded = 1;
  }

  // Load bomb sounds
  if (a_AudioLoadSound("resources/soundEffects/bomb_throw.wav", &bomb_throw_sound) == 0) {
    bomb_throw_sound_loaded = 1;
  }
  if (a_AudioLoadSound("resources/soundEffects/bomb_explosion.wav", &bomb_explode_sound) == 0) {
    bomb_explode_sound_loaded = 1;
  }

  // Init chain state
  chain_state.active = 0;
  chain_state.num_targets = 0;
  chain_state.bounced = 0;
  chain_state.current_jump = 0;
  chain_segment_count = 0;
  for (int i = 0; i < CONDUCTOR_CHAIN_MAX; i++) {
    conductor_chains[i].active = 0;
  }

  // Init orbit state
  orbit_state.is_active = 0;
  orbit_state.active_timer = 0.0f;
  orbit_state.angle = 0.0f;
  orbit_hit_count = 0;
  for (int i = 0; i < ORBIT_MAX_ORBS; i++) orbit_weave_offset[i] = 0.0f;
  for (int i = 0; i < GHOST_ORBIT_MAX; i++) ghost_orbits[i].active = 0;
  ghost_orbit_hit_count = 0;

  // Init bomb state
  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    bomb_flights[i].in_flight = 0;
    bomb_explosions[i].active = 0;
  }

  // Init linger zones
  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    linger_zones[i].active = 0;
  }
  orbit_trail_timer = 0.0f;
  gw_trail_timer = 0.0f;
  scythe_harvest_refund = 0.0f;
  spin_precision_refund = 0.0f;
  aftershock_wave.active = 0;
  aftershock_pending = 0;
  vacuum_visual_timer = 0.0f;
  vacuum_pull_count = 0;
  vacuum_tween_timer = 0.0f;
  implosion_pull_count = 0;
  implosion_tween_timer = 0.0f;
  implosion_visual_timer = 0.0f;

  // Init shatter projectiles
  for (int i = 0; i < SHATTER_MAX; i++) shatter_projs[i].active = 0;

  // Init cluster minis
  for (int i = 0; i < CLUSTER_MAX; i++) cluster_minis[i].active = 0;

  // Init napalm zones
  for (int i = 0; i < NAPALM_MAX; i++) napalm_zones[i].active = 0;

  // Init crater zones
  for (int i = 0; i < CRATER_MAX; i++) crater_zones[i].active = 0;

  // Init turret state
  memset(turrets, 0, sizeof(turrets));
  memset(turret_bullets, 0, sizeof(turret_bullets));
  for (int i = 0; i < TURRET_SLOW_MAX; i++) turret_slow_zones[i].active = 0;
  for (int i = 0; i < TESLA_MAX_ARCS; i++) tesla_arcs[i].active = 0;

  // Init trail state
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) trail_segments[i].active = 0;
  for (int i = 0; i < TRAIL_EMBER_MAX; i++) trail_embers[i].active = 0;
  trail_active = 0;
  trail_active_timer = 0.0f;
  trail_hit_count = 0;
  inferno_active = 0;
  inferno_queue_count = 0;
  inferno_timer = 0.0f;
  for (int i = 0; i < INFERNO_VISUAL_MAX; i++) inferno_blasts[i].active = 0;

  // Init scythe state
  scythe_sweep_timer = 0.0f;
  scythe_sweep_dir_x = 0.0f;
  scythe_sweep_dir_y = 0.0f;
  scythe_swing_count = 0;
  scythe_whirlwind_arc = 0.0f;
  for (int i = 0; i < SCYTHE_ZONE_MAX; i++) scythe_zones[i].active = 0;

  // Init achievement challenge tracking
  wand_shot_log_count = 0;
  wand_elapsed = 0.0f;
  chain_activation_kills = 0;
  chain_kill_window = 0.0f;
  orbit_activation_kills = 0;
  trail_total_unique = 0;
  scythe_sweep_kills = 0;
  scythe_sweep_window = 0.0f;
}

// ============================================================================
// Add weapon (returns slot index, or -1 if full)
// ============================================================================

int weapons_add(WeaponType_t type)
{
  if (weapon_count >= WEAPON_MAX_SLOTS) return -1;

  int slot = weapon_count;

  switch (type) {
    case WEAPON_WAND:
      slots[slot] = (Weapon_t){ .type = WEAPON_WAND, .label = "W",
                                .cooldown = WAND_COOLDOWN, .timer = WAND_COOLDOWN };
      break;
    case WEAPON_SPIN:
      slots[slot] = (Weapon_t){ .type = WEAPON_SPIN, .label = "S",
                                .cooldown = SPIN_COOLDOWN, .timer = SPIN_COOLDOWN };
      break;
    case WEAPON_CHAIN:
      slots[slot] = (Weapon_t){ .type = WEAPON_CHAIN, .label = "L",
                                .cooldown = CHAIN_COOLDOWN, .timer = CHAIN_COOLDOWN };
      break;
    case WEAPON_ORBIT:
      slots[slot] = (Weapon_t){ .type = WEAPON_ORBIT, .label = "O",
                                .cooldown = ORBIT_COOLDOWN, .timer = ORBIT_COOLDOWN };
      break;
    case WEAPON_BOMB:
      slots[slot] = (Weapon_t){ .type = WEAPON_BOMB, .label = "B",
                                .cooldown = BOMB_COOLDOWN, .timer = BOMB_COOLDOWN };
      break;
    case WEAPON_TURRET:
      slots[slot] = (Weapon_t){ .type = WEAPON_TURRET, .label = "T",
                                .cooldown = TURRET_COOLDOWN, .timer = TURRET_COOLDOWN };
      break;
    case WEAPON_TRAIL:
      slots[slot] = (Weapon_t){ .type = WEAPON_TRAIL, .label = "F",
                                .cooldown = TRAIL_COOLDOWN, .timer = TRAIL_COOLDOWN };
      break;
    case WEAPON_SCYTHE:
      slots[slot] = (Weapon_t){ .type = WEAPON_SCYTHE, .label = "X",
                                .cooldown = SCYTHE_COOLDOWN, .timer = SCYTHE_COOLDOWN };
      break;
    default:
      return -1;
  }

  weapon_pickup_time[type] = director_get_elapsed();
  weapon_count++;
  return slot;
}

int weapons_has(WeaponType_t type)
{
  for (int i = 0; i < weapon_count; i++) {
    if (slots[i].type == type) return 1;
  }
  return 0;
}

// ============================================================================
// Fire Weapons
// ============================================================================

static void fire_wand_single(void)
{
  static const int multishot_count[4] = { 1, 2, 3, 5 };
  int count = multishot_count[upgrades_get_tier(UPG_WAND_MULTISHOT)];

  if (count <= 1) {
    player_fire_at_nearest();
  } else {
    player_fire_fan_at_nearest(count);
  }

  // Log projectiles for Bullet Hell achievement
  if (wand_shot_log_count < WAND_SHOT_LOG_MAX) {
    for (int p = 0; p < count && wand_shot_log_count < WAND_SHOT_LOG_MAX; p++)
      wand_shot_log[wand_shot_log_count++] = wand_elapsed;
  }
}

static void fire_wand(void)
{
  fire_wand_single();

  // Barrage: check if this shot triggers a burst
  int tier = upgrades_get_tier(UPG_WAND_BARRAGE);
  if (tier <= 0) return;

  wand_shot_count++;
  static const int every_n[4] = { 0, 3, 2, 1 };
  static const int burst[4]   = { 0, 2, 2, 4 };
  if (wand_shot_count % every_n[tier] == 0) {
    wand_burst_remaining = burst[tier] - 1; // -1 because first shot already fired
    wand_burst_timer = 0.08f;
  }
}

static void spawn_aftershock_wave(int tier, float tdx, float tdy)
{
  static const float as_range[4] = { 0.0f, 135.0f, 198.0f, 270.0f };
  float px = player_get_x();
  float py = player_get_y();

  aftershock_wave = (AfterShockWave_t){
    .origin_x = px, .origin_y = py,
    .dir_x = tdx, .dir_y = tdy,
    .head_dist = 0.0f,
    .max_dist = as_range[tier],
    .speed = 450.0f,
    .width = AFTERSHOCK_WIDTH * 0.5f,
    .active = 1,
    .hit_count = 0,
    .bonus_hits = (tier >= 3) ? 1 : 0
  };
}

static void fire_spin(void)
{
  float radius = get_spin_radius();

  // Vacuum upgrade: pull enemies from donut ring inward (tweened + visual)
  int vacuum_tier = upgrades_get_tier(UPG_SPIN_VACUUM);
  if (vacuum_tier > 0) {
    static const float vacuum_mult[4] = { 1.0f, 1.3f, 1.5f, 1.8f };
    float outer_r = radius * vacuum_mult[vacuum_tier];
    float px = player_get_x();
    float py = player_get_y();
    int max_e = enemy_get_max_count();

    // Start visual
    vacuum_visual_timer = VACUUM_VISUAL_DURATION;
    vacuum_visual_cx = px;
    vacuum_visual_cy = py;
    vacuum_inner_r = radius;
    vacuum_outer_r = outer_r;

    // Queue up tweens
    vacuum_pull_count = 0;
    vacuum_tween_timer = VACUUM_TWEEN_DURATION;

    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      if (vacuum_pull_count >= VACUUM_MAX_PULLS) break;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er;
      float ecy = ey + er;
      float dx = ecx - px;
      float dy = ecy - py;
      float dist = sqrtf(dx * dx + dy * dy);

      // Only pull enemies in the donut (between spin radius and outer radius)
      if (dist > radius && dist <= outer_r && dist > 0.1f) {
        float target_dist = radius * 0.8f;
        float pull = (dist - target_dist) * 0.7f;
        float ndx = dx / dist;
        float ndy = dy / dist;
        VacuumPull_t* vp = &vacuum_pulls[vacuum_pull_count++];
        vp->enemy_index = e;
        vp->is_snake = 0;
        vp->dx = -ndx * pull;
        vp->dy = -ndy * pull;
      }
    }

    // Also pull snakes
    int max_s = snake_get_max_count();
    for (int s = 0; s < max_s; s++) {
      if (!snake_is_alive(s)) continue;
      if (vacuum_pull_count >= VACUUM_MAX_PULLS) break;
      float sx, sy;
      snake_get_head_position(s, &sx, &sy);
      float sr = snake_get_head_radius(s);
      float scx = sx + sr;
      float scy = sy + sr;
      float dx = scx - px;
      float dy = scy - py;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist > radius && dist <= outer_r && dist > 0.1f) {
        float target_dist = radius * 0.8f;
        float pull = (dist - target_dist) * 0.7f;
        float ndx = dx / dist;
        float ndy = dy / dist;
        VacuumPull_t* vp = &vacuum_pulls[vacuum_pull_count++];
        vp->enemy_index = s;
        vp->is_snake = 1;
        vp->dx = -ndx * pull;
        vp->dy = -ndy * pull;
      }
    }
  }

  float spin_kb = 300.0f * get_spin_knockback_mult();
  int spin_hits = player_do_spin_attack(radius, spin_kb);
  spin_visual_timer = SPIN_VISUAL_DURATION;

  // Eye of the Storm achievement
  achievements_notify_weapon_challenge(WEAPON_SPIN, spin_hits);

  // Spin linger zone (rare upgrade)
  int spin_linger_tier = upgrades_get_tier(UPG_SPIN_LINGER_ZONE);
  if (spin_linger_tier > 0) {
    static const float spin_linger_dur[4] = { 0.0f, 1.2f, 1.8f, 2.4f };
    linger_spawn(player_get_x(), player_get_y(), radius * 0.8f,
                 spin_linger_dur[spin_linger_tier], (aColor_t){255, 200, 50, 200});
  }

  // Spin slow zone (common upgrade) — fixed 92px radius regardless of spin radius upgrades
  {
    int slow_tier = upgrades_get_tier(UPG_SPIN_SLOW);
    if (slow_tier > 0) {
      static const float slow_vals[4] = { 1.0f, 0.80f, 0.70f, 0.60f };
      static const float slow_durs[4] = { 0.0f, 1.5f, 2.0f, 2.5f };
      for (int si = 0; si < TURRET_SLOW_MAX; si++) {
        if (!turret_slow_zones[si].active) {
          turret_slow_zones[si] = (TurretSlowZone_t){
            .x = player_get_x(), .y = player_get_y(),
            .radius = 92.0f,
            .lifetime = slow_durs[slow_tier],
            .max_lifetime = slow_durs[slow_tier],
            .slow_mult = slow_vals[slow_tier],
            .active = 1
          };
          break;
        }
      }
    }
  }

  // Precision upgrade: half cooldown if few enemies hit
  {
    int prec_tier = upgrades_get_tier(UPG_SPIN_PRECISION);
    if (prec_tier > 0 && spin_hits >= 1 && spin_hits <= prec_tier) {
      // Find the spin slot to read its cooldown
      for (int si = 0; si < weapon_count; si++) {
        if (slots[si].type == WEAPON_SPIN) {
          spin_precision_refund = get_slot_cooldown(si) * 0.5f;
          break;
        }
      }
    }
  }

  // Aftershock: schedule directional wave with 0.3s delay
  // Targeting happens when the wave fires, not now
  int aftershock_tier = upgrades_get_tier(UPG_SPIN_AFTERSHOCK);
  if (aftershock_tier > 0) {
    aftershock_pending = 1;
    aftershock_delay = AFTERSHOCK_DELAY;
  }

  if (spin_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_PLAYER,
      .volume = 80,
      .loops = 0,
      .fade_ms = 0,
      .interrupt = 0
    };
    a_AudioPlaySound(&spin_sound, &opts);
  }
}

static void fire_chain(void)
{
  // Don't fire if a chain is already in flight
  if (chain_state.active) return;

  float px = player_get_x();
  float py = player_get_y();

  float chain_radius = get_chain_radius();
  float initial_radius = get_chain_initial_radius();
  int chain_max = get_chain_max_jumps();

  // Find best cluster target (global search, neighbor counting uses chain_radius)
  int first = enemy_find_cluster_target(chain_radius, px, py);
  if (first < 0) return;

  if (chain_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 80,
      .loops = 0,
      .fade_ms = 0,
      .interrupt = 0
    };
    a_AudioPlaySound(&chain_sound, &opts);
  }

  // Finalize previous chain's kill window if still open
  if (chain_kill_window > 0.0f) {
    achievements_notify_weapon_challenge(WEAPON_CHAIN, chain_activation_kills);
    chain_kill_window = 0.0f;
  }

  // Resolve full chain path
  chain_state.targets[0] = first;
  chain_state.target_is_corpse[0] = 0;
  chain_state.num_targets = 1;
  chain_activation_kills = 0;

  int cc_tier = upgrades_get_tier(UPG_CHAIN_CORPSE_CHAIN);

  int max_e = enemy_get_max_count();
  int jumps_remaining = chain_max - 1;
  while (jumps_remaining > 0 && chain_state.num_targets < CHAIN_MAX_JUMPS) {
    int prev = chain_state.targets[chain_state.num_targets - 1];
    float prev_x, prev_y;
    enemy_get_position(prev, &prev_x, &prev_y);
    float prev_r = enemy_get_radius(prev);
    float cx = prev_x + prev_r;
    float cy = prev_y + prev_r;

    // First jump (target 1→2) uses boosted initial_radius, rest use chain_radius
    float jump_r = (chain_state.num_targets == 1) ? initial_radius : chain_radius;

    // First pass: prefer alive enemies
    int best = -1;
    float best_dist = jump_r + 1.0f;

    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;

      // Check if already in chain
      int already = 0;
      for (int k = 0; k < chain_state.num_targets; k++) {
        if (chain_state.targets[k] == e) { already = 1; break; }
      }
      if (already) continue;

      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er;
      float ecy = ey + er;

      float dx = ecx - cx;
      float dy = ecy - cy;
      float d = sqrtf(dx * dx + dy * dy);

      if (d <= jump_r && d < best_dist) {
        best = e;
        best_dist = d;
      }
    }

    if (best >= 0) {
      chain_state.targets[chain_state.num_targets] = best;
      chain_state.target_is_corpse[chain_state.num_targets] = 0;
      chain_state.num_targets++;
      jumps_remaining--;
      continue;
    }

    // Second pass: if no alive enemy found and Corpse Chain upgrade active, try corpses
    if (cc_tier > 0) {
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_active(e)) continue;
        if (enemy_get_state(e) != ENEMY_STATE_CORPSE) continue;

        int already = 0;
        for (int k = 0; k < chain_state.num_targets; k++) {
          if (chain_state.targets[k] == e) { already = 1; break; }
        }
        if (already) continue;

        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er;
        float ecy = ey + er;

        float dx = ecx - cx;
        float dy = ecy - cy;
        float d = sqrtf(dx * dx + dy * dy);

        if (d <= jump_r && d < best_dist) {
          best = e;
          best_dist = d;
        }
      }
    }

    if (best < 0) break;
    chain_state.targets[chain_state.num_targets] = best;
    chain_state.target_is_corpse[chain_state.num_targets] = 1;
    chain_state.num_targets++;
    jumps_remaining--;
  }

  // Apply first hit immediately
  {
    int t = chain_state.targets[0];
    float ex, ey;
    enemy_get_position(t, &ex, &ey);
    float tr = enemy_get_radius(t);
    float ecx = ex + tr;
    float ecy = ey + tr;

    // Knockback direction: player -> enemy
    float dx = ecx - px;
    float dy = ecy - py;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.1f) { dx /= len; dy /= len; }
    enemy_hit(t, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);

    // Mini stun on first chain hit
    {
      int ms_tier = upgrades_get_tier(UPG_CHAIN_MINI_STUN);
      if (ms_tier > 0) {
        static const float stun_dur[4] = { 0.0f, 0.1f, 0.15f, 0.2f };
        enemy_set_stun(t, stun_dur[ms_tier], 1.0f);
      }
    }

    // Chain linger arc for first hit
    {
      int cl_tier = upgrades_get_tier(UPG_CHAIN_LINGER_ARC);
      if (cl_tier > 0) {
        static const float cl_dur[4] = { 0.0f, 0.75f, 1.5f, 2.25f };
        linger_spawn(ecx, ecy, 30.0f, cl_dur[cl_tier], (aColor_t){200, 220, 255, 200});
      }
    }

    // First visual segment: player -> first enemy
    chain_segments[0] = (ChainSegment_t){
      .x1 = px, .y1 = py,
      .x2 = ecx, .y2 = ecy,
      .timer = CHAIN_VISUAL_DURATION
    };
    chain_segment_count = 1;
  }

  chain_state.current_jump = 1;
  chain_state.propagation_timer = CHAIN_DELAY;
  chain_state.bounced = 0;
  chain_state.active = (chain_state.num_targets > 1) ? 1 : 0;
  // If only 1 target, chain is done immediately (no propagation needed)

  // Snake head: check if any snake head is within chain radius of any target
  for (int t = 0; t < chain_state.num_targets; t++) {
    int ti = chain_state.targets[t];
    if (!enemy_is_alive(ti)) continue;
    float tex, tey;
    enemy_get_position(ti, &tex, &tey);
    float ter = enemy_get_radius(ti);
    if (snake_check_hit_aoe(tex + ter, tey + ter, chain_radius)) break;
  }
}

static void fire_orbit(void)
{
  orbit_state.is_active = 1;
  orbit_state.active_timer = get_orbit_duration();
  orbit_state.angle = 0.0f;
  orbit_hit_count = 0;
  orbit_activation_kills = 0;
  for (int i = 0; i < ORBIT_MAX_ORBS; i++)
    orbit_weave_offset[i] = 0.0f;
}

static int bomb_find_empty_flight(void)
{
  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    if (!bomb_flights[i].in_flight) return i;
  }
  return -1;
}

static void fire_bomb(void)
{
  float px = player_get_x();
  float py = player_get_y();
  float blast_r = get_bomb_blast_radius();

  // Find densest cluster, predict where they'll be when the bomb lands
  float tx, ty;
  if (!enemy_find_cluster_position(blast_r, px, py, get_bomb_flight_time(), &tx, &ty))
    return;

  for (int b = 0; b < 1; b++) {
    int slot = bomb_find_empty_flight();
    if (slot < 0) break;

    // Additional bombs offset ±40px from primary target
    float bx = tx, by = ty;
    if (b > 0) {
      float angle = (float)b * 2.094f; // ~120 degree spread
      bx += cosf(angle) * 40.0f;
      by += sinf(angle) * 40.0f;
    }

    bomb_flights[slot] = (BombFlight_t){
      .start_x = px, .start_y = py,
      .target_x = bx, .target_y = by,
      .flight_progress = 0.0f,
      .in_flight = 1
    };
  }

  if (bomb_throw_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 80,
      .loops = 0,
      .fade_ms = 0,
      .interrupt = 0
    };
    a_AudioPlaySound(&bomb_throw_sound, &opts);
  }
}

// ============================================================================
// Bomb update
// ============================================================================

static void bomb_update(float dt)
{
  int max_e = enemy_get_max_count();
  float blast_r = get_bomb_blast_radius();

  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    if (!bomb_flights[i].in_flight) continue;

    bomb_flights[i].flight_progress += dt / get_bomb_flight_time();

    if (bomb_flights[i].flight_progress >= 1.0f) {
      // Impact — apply AoE damage
      float ix = bomb_flights[i].target_x;
      float iy = bomb_flights[i].target_y;
      bomb_flights[i].in_flight = 0;

      float impl_force = get_bomb_implosion_force();
      int bomb_cond_tier = upgrades_get_tier(UPG_BOMB_CONDUCTOR);

      if (impl_force > 0.0f) {
        // Implosion: queue tween pulls inward (like vacuum)
        implosion_pull_count = 0;
        implosion_tween_timer = IMPLOSION_TWEEN_DURATION;
        implosion_visual_timer = IMPLOSION_VISUAL_DURATION;
        implosion_visual_cx = ix;
        implosion_visual_cy = iy;
        implosion_visual_radius = blast_r;
      }

      int bomb_hit_count = 0;
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;

        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er;
        float ecy = ey + er;

        float dx = ecx - ix;
        float dy = ecy - iy;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist < blast_r) {
          bomb_hit_count++;
          if (impl_force > 0.0f) {
            // Implosion: hit without knockback, queue tween pull
            enemy_hit(e, 0.0f, 0.0f);
            if (implosion_pull_count < IMPLOSION_MAX_PULLS) {
              float len = dist > 0.1f ? dist : 1.0f;
              float pull_dist = dist * 0.7f; // pull 70% of the way to center
              VacuumPull_t* vp = &implosion_pulls[implosion_pull_count++];
              vp->enemy_index = e;
              vp->is_snake = 0;
              vp->dx = -(dx / len) * pull_dist;
              vp->dy = -(dy / len) * pull_dist;
            }
          } else {
            // Normal: knockback outward
            float len = dist > 0.1f ? dist : 1.0f;
            float kx = dx / len;
            float ky = dy / len;
            enemy_hit(e, kx * BOMB_KNOCKBACK, ky * BOMB_KNOCKBACK);
          }

          if (bomb_cond_tier > 0) {
            enemy_set_conductor(e, 2.0f, bomb_cond_tier);
          }
        }
      }
      // Carpet Bomber achievement
      achievements_notify_weapon_challenge(WEAPON_BOMB, bomb_hit_count);
      snake_check_hit_aoe(ix, iy, blast_r);
      if (bomb_cond_tier > 0)
        snake_apply_conductor_aoe(ix, iy, blast_r, 2.0f, bomb_cond_tier);

      // Implosion: also pull snakes
      if (impl_force > 0.0f) {
        int max_s = snake_get_max_count();
        for (int s = 0; s < max_s; s++) {
          if (!snake_is_alive(s)) continue;
          if (implosion_pull_count >= IMPLOSION_MAX_PULLS) break;
          float sx, sy;
          snake_get_head_position(s, &sx, &sy);
          float sr = snake_get_head_radius(s);
          float scx = sx + sr, scy = sy + sr;
          float sdx = scx - ix, sdy = scy - iy;
          float sdist = sqrtf(sdx * sdx + sdy * sdy);
          if (sdist < blast_r) {
            float len = sdist > 0.1f ? sdist : 1.0f;
            float pull_dist = sdist * 0.7f;
            VacuumPull_t* vp = &implosion_pulls[implosion_pull_count++];
            vp->enemy_index = s;
            vp->is_snake = 1;
            vp->dx = -(sdx / len) * pull_dist;
            vp->dy = -(sdy / len) * pull_dist;
          }
        }
      }

      if (bomb_explode_sound_loaded) {
        aAudioOptions_t opts = {
          .channel = AUDIO_CHANNEL_AUTO,
          .volume = 96,
          .loops = 0,
          .fade_ms = 0,
          .interrupt = 0
        };
        a_AudioPlaySound(&bomb_explode_sound, &opts);
      }

      // Start explosion visual
      for (int j = 0; j < BOMB_MAX_ACTIVE; j++) {
        if (!bomb_explosions[j].active) {
          bomb_explosions[j] = (BombExplosion_t){
            .x = ix, .y = iy,
            .timer = BOMB_VISUAL_DURATION,
            .radius = get_bomb_blast_radius(),
            .active = 1
          };
          break;
        }
      }

      // Bomb shrapnel (rare upgrade): launch shatter projectiles from impact
      {
        int shrapnel_tier = upgrades_get_tier(UPG_BOMB_SHRAPNEL);
        if (shrapnel_tier > 0) {
          static const int shard_count[4] = { 0, 4, 5, 6 };
          int count = shard_count[shrapnel_tier];
          int s_pierce = 0;
          WeaponType_t prev_source = damage_source;
          damage_source = WEAPON_BOMB;
          for (int s = 0; s < count; s++) {
            float angle = (2.0f * (float)PI * s) / count;
            for (int si = 0; si < SHATTER_MAX; si++) {
              if (!shatter_projs[si].active) {
                shatter_projs[si] = (ShatterProj_t){
                  .x = ix, .y = iy,
                  .vx = cosf(angle) * 300.0f,
                  .vy = sinf(angle) * 300.0f,
                  .lifetime = 0.5f,
                  .pierce = s_pierce,
                  .active = 1,
                  .hit_count = 0
                };
                break;
              }
            }
          }
          damage_source = prev_source;
        }
      }

      // Cluster Bomb: spawn mini-bombs that scatter and pop
      {
        int cluster_tier = upgrades_get_tier(UPG_BOMB_CLUSTER);
        if (cluster_tier > 0) {
          static const int mini_count[4] = { 0, 3, 4, 5 };

          int count = mini_count[cluster_tier];
          for (int m = 0; m < count; m++) {
            for (int ci = 0; ci < CLUSTER_MAX; ci++) {
              if (!cluster_minis[ci].active) {
                float angle = RANDF(0, 2.0f * (float)PI);
                float scatter_speed = 150.0f;
                cluster_minis[ci] = (ClusterMini_t){
                  .x = ix, .y = iy,
                  .vx = cosf(angle) * scatter_speed,
                  .vy = sinf(angle) * scatter_speed,
                  .fuse = 0.1f + RANDF(0.0f, 0.2f),
                  .active = 1
                };
                break;
              }
            }
          }
        }
      }

      // Napalm: fire streak along bomb travel direction
      {
        int napalm_tier = upgrades_get_tier(UPG_BOMB_NAPALM);
        if (napalm_tier > 0) {
          static const float np_max_len[4] = { 0.0f, 50.0f, 65.0f, 80.0f };
          static const float np_grow[4] = { 0.0f, 2.0f, 1.5f, 1.5f };
          // Streak angle from bomb travel direction
          float bdx = ix - bomb_flights[i].start_x;
          float bdy = iy - bomb_flights[i].start_y;
          float bang = atan2f(bdy, bdx);
          for (int ni = 0; ni < NAPALM_MAX; ni++) {
            if (!napalm_zones[ni].active) {
              napalm_zones[ni] = (NapalmZone_t){
                .x = ix, .y = iy,
                .angle = bang,
                .current_len = np_max_len[napalm_tier] * 0.3f,
                .max_len = np_max_len[napalm_tier],
                .grow_time = np_grow[napalm_tier],
                .elapsed = 0.0f,
                .burn_time = 1.0f,
                .tier = napalm_tier,
                .active = 1
              };
              for (int ec = 0; ec < NAPALM_MAX_TRACKED; ec++)
                napalm_zones[ni].enemy_cd[ec] = 0.0f;
              break;
            }
          }
        }
      }

      // Crater: slow zone
      {
        int crater_tier = upgrades_get_tier(UPG_BOMB_CRATER);
        if (crater_tier > 0) {
          static const float crater_dur[4] = { 0.0f, 5.0f, 7.0f, 8.0f };
          float dmg_mult = (crater_tier >= 3) ? 1.3f : 1.0f;
          for (int ci = 0; ci < CRATER_MAX; ci++) {
            if (!crater_zones[ci].active) {
              crater_zones[ci] = (CraterZone_t){
                .x = ix, .y = iy,
                .radius = blast_r * 0.8f,
                .lifetime = crater_dur[crater_tier],
                .max_lifetime = crater_dur[crater_tier],
                .damage_mult = dmg_mult,
                .active = 1,
                .is_bomb_crater = 1
              };
              break;
            }
          }
        }
      }
    }
  }

  // Update explosion visuals
  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    if (!bomb_explosions[i].active) continue;
    bomb_explosions[i].timer -= dt;
    if (bomb_explosions[i].timer <= 0.0f)
      bomb_explosions[i].active = 0;
  }
}

// ============================================================================
// Turret
// ============================================================================

static void turret_overcharge_explode(int i, float range_override);

// Mini turret constants
#define MINI_TURRET_RANGE_MULT     0.5f
#define MINI_TURRET_DURATION_MULT  0.5f
#define MINI_TURRET_FIRE_RATE_MULT 1.4f
#define MINI_TURRET_PLACEMENT_DIST 1.15f

static void spawn_mini_turret(float parent_x, float parent_y)
{
  int slot = -1;
  for (int i = 0; i < TURRET_MAX; i++) {
    if (!turrets[i].active) { slot = i; break; }
  }
  if (slot < 0) return;

  // Find nearest enemy to parent turret for placement direction
  int max_e = enemy_get_max_count();
  float best_d2 = 1e18f;
  float best_dx = 0, best_dy = -1; // default: place above
  for (int e = 0; e < max_e; e++) {
    if (!enemy_is_alive(e)) continue;
    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float dx = (ex + er) - parent_x;
    float dy = (ey + er) - parent_y;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      best_dx = dx;
      best_dy = dy;
    }
  }

  // Normalize direction
  float len = sqrtf(best_dx * best_dx + best_dy * best_dy);
  if (len > 0.1f) { best_dx /= len; best_dy /= len; }

  // Place just outside main turret range
  float place_dist = get_turret_range() * MINI_TURRET_PLACEMENT_DIST;
  float mx = parent_x + best_dx * place_dist;
  float my = parent_y + best_dy * place_dist;

  if (mx < 20.0f) mx = 20.0f;
  if (mx > SCREEN_WIDTH - 20.0f) mx = SCREEN_WIDTH - 20.0f;
  if (my < 20.0f) my = 20.0f;
  if (my > SCREEN_HEIGHT - 20.0f) my = SCREEN_HEIGHT - 20.0f;

  float dur = get_turret_duration() * MINI_TURRET_DURATION_MULT;

  turrets[slot] = (Turret_t){
    .x = mx, .y = my,
    .lifetime = dur,
    .max_lifetime = dur,
    .fire_timer = 0.0f,
    .tesla_timer = TESLA_INTERVAL,
    .active = 1,
    .is_mini = 1,
    .overcharge_halfway_fired = 0,
    .mini_halfway_spawned = 0
  };

  // Mini turrets also get T3 Overcharge on deploy
  if (upgrades_get_tier(UPG_TURRET_OVERCHARGE) >= 3) {
    turret_overcharge_explode(slot, get_turret_range() * MINI_TURRET_RANGE_MULT);
  }
}

static void fire_turret(void)
{
  float px = player_get_x();
  float py = player_get_y();
  int max_turrets = get_turret_count();

  // Find nearest enemy to player for placement
  int nearest = -1;
  float nearest_d2 = 1e18f;
  int max_e = enemy_get_max_count();
  for (int e = 0; e < max_e; e++) {
    if (!enemy_is_alive(e)) continue;
    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float dx = (ex + er) - px;
    float dy = (ey + er) - py;
    float d2 = dx * dx + dy * dy;
    if (d2 < nearest_d2) { nearest_d2 = d2; nearest = e; }
  }

  // Calculate base placement point and direction
  float base_x, base_y;
  float dir_x = 0, dir_y = -1; // default up
  if (nearest >= 0) {
    float ex, ey;
    enemy_get_position(nearest, &ex, &ey);
    float er = enemy_get_radius(nearest);
    float ecx = ex + er, ecy = ey + er;
    base_x = (px + ecx) * 0.5f;
    base_y = (py + ecy) * 0.5f;
    float dx = ecx - px, dy = ecy - py;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.1f) { dir_x = dx / len; dir_y = dy / len; }
  } else {
    float fx = player_get_facing_x();
    float fy = player_get_facing_y();
    float flen = sqrtf(fx * fx + fy * fy);
    if (flen > 0.1f) { dir_x = fx / flen; dir_y = fy / flen; }
    base_x = px + dir_x * 60.0f;
    base_y = py + dir_y * 60.0f;
  }

  // Perpendicular direction for multi-turret spread
  float perp_x = -dir_y, perp_y = dir_x;

  for (int t = 0; t < max_turrets; t++) {
    int slot = -1;
    for (int i = 0; i < TURRET_MAX; i++) {
      if (!turrets[i].active) { slot = i; break; }
    }
    if (slot < 0) break;

    float tx = base_x, ty = base_y;
    if (max_turrets == 2) {
      float offset = (t == 0) ? -30.0f : 30.0f;
      tx += perp_x * offset;
      ty += perp_y * offset;
    } else if (max_turrets == 3) {
      if (t == 1) { tx += perp_x * 60.0f; ty += perp_y * 60.0f; }
      if (t == 2) { tx -= perp_x * 60.0f; ty -= perp_y * 60.0f; }
    }

    // Clamp to 20px screen margin
    if (tx < 20.0f) tx = 20.0f;
    if (tx > SCREEN_WIDTH - 20.0f) tx = SCREEN_WIDTH - 20.0f;
    if (ty < 20.0f) ty = 20.0f;
    if (ty > SCREEN_HEIGHT - 20.0f) ty = SCREEN_HEIGHT - 20.0f;

    float dur = get_turret_duration();
    turrets[slot] = (Turret_t){
      .x = tx, .y = ty,
      .lifetime = dur,
      .max_lifetime = dur,
      .fire_timer = 0.0f,
      .tesla_timer = TESLA_INTERVAL,
      .active = 1,
      .overcharge_halfway_fired = 0,
      .is_mini = 0,
      .mini_halfway_spawned = 0
    };

    // T3 Overcharge: explode on deploy
    if (upgrades_get_tier(UPG_TURRET_OVERCHARGE) >= 3) {
      turret_overcharge_explode(slot, 0.0f);
    }

    // Mini turret: spawn on deploy
    if (upgrades_get_tier(UPG_TURRET_MINI_TURRET) >= 1) {
      spawn_mini_turret(turrets[slot].x, turrets[slot].y);
    }
  }
}

// Overcharge explosion helper — used at expiry, halfway, and deploy
static void turret_overcharge_explode(int i, float range_override)
{
  float radius = (range_override > 0.0f ? range_override : get_turret_range()) * 1.15f;
  int max_e = enemy_get_max_count();
  int overcharge_tier = upgrades_get_tier(UPG_TURRET_OVERCHARGE);

  for (int e = 0; e < max_e; e++) {
    if (!enemy_is_alive(e)) continue;
    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float dx = (ex + er) - turrets[i].x;
    float dy = (ey + er) - turrets[i].y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < radius) {
      float len = dist > 0.1f ? dist : 1.0f;
      enemy_hit(e, (dx / len) * 100.0f, (dy / len) * 100.0f);
    }
  }
  snake_check_hit_aoe(turrets[i].x, turrets[i].y, radius);

  // Explosion visual
  for (int j = 0; j < BOMB_MAX_ACTIVE; j++) {
    if (!bomb_explosions[j].active) {
      bomb_explosions[j] = (BombExplosion_t){
        .x = turrets[i].x, .y = turrets[i].y,
        .timer = BOMB_VISUAL_DURATION,
        .radius = radius,
        .active = 1
      };
      break;
    }
  }
  if (bomb_explode_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 72,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(&bomb_explode_sound, &opts);
  }

  // T3: linger fire zone 2s
  if (overcharge_tier >= 3) {
    linger_spawn(turrets[i].x, turrets[i].y, radius,
                 2.0f, (aColor_t){255, 120, 30, 80});
  }
}

static void turret_update(float dt)
{
  // Turret Defense achievement: count active turrets (including minis)
  {
    int active_count = 0;
    for (int ti = 0; ti < TURRET_MAX; ti++)
      if (turrets[ti].active) active_count++;
    achievements_notify_weapon_challenge(WEAPON_TURRET, active_count);
  }

  float base_fire_rate = get_turret_fire_rate();
  int spread_count = get_turret_spread();
  int overcharge_tier = upgrades_get_tier(UPG_TURRET_OVERCHARGE);
  int mini_tier = upgrades_get_tier(UPG_TURRET_MINI_TURRET);
  int max_e = enemy_get_max_count();

  // Update turrets
  for (int i = 0; i < TURRET_MAX; i++) {
    if (!turrets[i].active) continue;

    // Per-turret stats (scaled for minis)
    float fire_rate = base_fire_rate;
    float trange = get_turret_range();
    float range_ovr = 0.0f;
    if (turrets[i].is_mini) {
      fire_rate *= MINI_TURRET_FIRE_RATE_MULT;
      trange *= MINI_TURRET_RANGE_MULT;
      range_ovr = trange;
    }

    turrets[i].lifetime -= dt;

    // T2+ Overcharge: explode at halfway
    if (overcharge_tier >= 2 && !turrets[i].overcharge_halfway_fired) {
      if (turrets[i].lifetime <= turrets[i].max_lifetime * 0.5f) {
        turrets[i].overcharge_halfway_fired = 1;
        turret_overcharge_explode(i, range_ovr);
      }
    }

    // T2+ Mini Turret: spawn at half-life
    if (mini_tier >= 2 && !turrets[i].is_mini && !turrets[i].mini_halfway_spawned) {
      if (turrets[i].lifetime <= turrets[i].max_lifetime * 0.5f) {
        turrets[i].mini_halfway_spawned = 1;
        spawn_mini_turret(turrets[i].x, turrets[i].y);
      }
    }

    if (turrets[i].lifetime <= 0.0f) {
      // T3 Mini Turret: spawn on expiry
      if (mini_tier >= 3 && !turrets[i].is_mini) {
        spawn_mini_turret(turrets[i].x, turrets[i].y);
      }

      turrets[i].active = 0;

      // Overcharge: explode on expiry
      if (overcharge_tier > 0) {
        turret_overcharge_explode(i, range_ovr);
      }
      continue;
    }

    // Fire at nearest enemy within range
    turrets[i].fire_timer -= dt;
    if (turrets[i].fire_timer <= 0.0f) {
      turrets[i].fire_timer += fire_rate;

      int best = -1;
      float best_d2 = trange * trange;
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float tdx = (ex + er) - turrets[i].x;
        float tdy = (ey + er) - turrets[i].y;
        float d2 = tdx * tdx + tdy * tdy;
        if (d2 < best_d2) { best_d2 = d2; best = e; }
      }

      // Also check snake heads
      float snake_best = sqrtf(best_d2);
      float shx, shy;
      int snake_is_target = snake_find_nearest_head(turrets[i].x, turrets[i].y,
                                                     &snake_best, &shx, &shy);

      if (best < 0 && !snake_is_target) continue; // No target, skip shot

      float dx, dy, dist;
      if (snake_is_target) {
        dx = shx - turrets[i].x;
        dy = shy - turrets[i].y;
        dist = sqrtf(dx * dx + dy * dy);
      } else {
        float ex, ey;
        enemy_get_position(best, &ex, &ey);
        float er = enemy_get_radius(best);
        dx = (ex + er) - turrets[i].x;
        dy = (ey + er) - turrets[i].y;
        dist = sqrtf(dx * dx + dy * dy);
      }
      if (dist < 0.01f) continue;
      dx /= dist; dy /= dist;

      // Spawn bullet(s)
      for (int s = 0; s < spread_count; s++) {
        int bslot = -1;
        for (int b = 0; b < TURRET_BULLET_MAX; b++) {
          if (!turret_bullets[b].active) { bslot = b; break; }
        }
        if (bslot < 0) break;

        float bdx = dx, bdy = dy;
        if (spread_count > 1) {
          float total_angle = 1.0472f; // 60 degrees
          float step = total_angle / (float)(spread_count - 1);
          float offset = -total_angle * 0.5f + step * (float)s;
          float cos_o = cosf(offset), sin_o = sinf(offset);
          bdx = dx * cos_o - dy * sin_o;
          bdy = dx * sin_o + dy * cos_o;
        }

        turret_bullets[bslot] = (TurretBullet_t){
          .x = turrets[i].x, .y = turrets[i].y,
          .vx = bdx * TURRET_BULLET_SPEED,
          .vy = bdy * TURRET_BULLET_SPEED,
          .lifetime = TURRET_BULLET_LIFETIME,
          .active = 1,
          .pierce_remaining = get_turret_pierce(),
          .hit_count = 0
        };
      }
    }

    // Turret Vacuum: periodic donut pull toward turret
    int tesla_tier = upgrades_get_tier(UPG_TURRET_TESLA_COIL);
    if (tesla_tier > 0) {
      turrets[i].tesla_timer -= dt;
      if (turrets[i].tesla_timer <= 0.0f) {
        turrets[i].tesla_timer += TESLA_INTERVAL;
        static const float pull_dist[4] = { 0.0f, 20.0f, 28.0f, 38.0f };
        float vac_range = trange; // already scaled for minis
        float inner_r = vac_range * 0.4f;
        float pull = pull_dist[tesla_tier];
        float tx = turrets[i].x;
        float ty = turrets[i].y;

        for (int e = 0; e < max_e; e++) {
          if (!enemy_is_alive(e)) continue;
          float ex2, ey2;
          enemy_get_position(e, &ex2, &ey2);
          float er2 = enemy_get_radius(e);
          float ecx2 = ex2 + er2, ecy2 = ey2 + er2;
          float tdx = ecx2 - tx;
          float tdy = ecy2 - ty;
          float dist = sqrtf(tdx * tdx + tdy * tdy);
          if (dist > inner_r && dist < vac_range && dist > 1.0f) {
            enemy_displace(e, (-tdx / dist) * pull, (-tdy / dist) * pull);
          }
        }

        // Also pull snake heads in donut
        for (int si = 0; si < snake_get_max_count(); si++) {
          if (!snake_is_on_screen_active(si)) continue;
          float shx, shy;
          snake_get_head_position(si, &shx, &shy);
          float sdx = shx - tx;
          float sdy = shy - ty;
          float sdist = sqrtf(sdx * sdx + sdy * sdy);
          if (sdist > inner_r && sdist < vac_range && sdist > 1.0f) {
            snake_displace(si, (-sdx / sdist) * pull, (-sdy / sdist) * pull);
          }
        }
      }
    }
  }

  // Update tesla arc visuals
  for (int i = 0; i < TESLA_MAX_ARCS; i++) {
    if (!tesla_arcs[i].active) continue;
    tesla_arcs[i].timer -= dt;
    if (tesla_arcs[i].timer <= 0.0f) tesla_arcs[i].active = 0;
  }

  // Update turret bullets (with homing)
  for (int i = 0; i < TURRET_BULLET_MAX; i++) {
    if (!turret_bullets[i].active) continue;

    // Homing: steer toward nearest alive enemy (skip already-pierced)
    float bx = turret_bullets[i].x;
    float by = turret_bullets[i].y;
    float best_dist = 200.0f;
    float best_ex = 0, best_ey = 0;
    int found = 0;
    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      // Skip already-hit enemies
      int already = 0;
      for (int h = 0; h < turret_bullets[i].hit_count; h++) {
        if (turret_bullets[i].hit_enemies[h] == e) { already = 1; break; }
      }
      if (already) continue;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float hdx = (ex + er) - bx;
      float hdy = (ey + er) - by;
      float hd = sqrtf(hdx * hdx + hdy * hdy);
      if (hd < best_dist) {
        best_dist = hd;
        best_ex = ex + er;
        best_ey = ey + er;
        found = 1;
      }
    }
    if (found) {
      float bul_angle = atan2f(turret_bullets[i].vy, turret_bullets[i].vx);
      float target_angle = atan2f(best_ey - by, best_ex - bx);
      float diff = target_angle - bul_angle;
      while (diff > PI) diff -= 2.0f * (float)PI;
      while (diff < -PI) diff += 2.0f * (float)PI;
      float max_turn = TURRET_HOMING_RATE * dt;
      if (diff > max_turn) diff = max_turn;
      else if (diff < -max_turn) diff = -max_turn;
      float new_angle = bul_angle + diff;
      float spd = sqrtf(turret_bullets[i].vx * turret_bullets[i].vx +
                         turret_bullets[i].vy * turret_bullets[i].vy);
      turret_bullets[i].vx = cosf(new_angle) * spd;
      turret_bullets[i].vy = sinf(new_angle) * spd;
    }

    // Accelerate: ramp up ~50% over full lifetime
    float accel = 1.0f + 0.6f * dt;
    turret_bullets[i].vx *= accel;
    turret_bullets[i].vy *= accel;

    turret_bullets[i].x += turret_bullets[i].vx * dt;
    turret_bullets[i].y += turret_bullets[i].vy * dt;
    turret_bullets[i].lifetime -= dt;
    if (turret_bullets[i].lifetime <= 0.0f) turret_bullets[i].active = 0;
  }

  // Turret bullet enemy collision (with pierce support)
  weapons_set_damage_source(WEAPON_TURRET);
  for (int i = 0; i < TURRET_BULLET_MAX; i++) {
    if (!turret_bullets[i].active) continue;

    float bx = turret_bullets[i].x;
    float by = turret_bullets[i].y;
    float br = (float)TURRET_BULLET_SIZE;

    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_active(e)) continue;
      EnemyState_t state = enemy_get_state(e);
      if (state == ENEMY_STATE_CORPSE || state == ENEMY_STATE_HIT_KNOCKBACK)
        continue;

      // Skip already-hit enemies
      int already = 0;
      for (int h = 0; h < turret_bullets[i].hit_count; h++) {
        if (turret_bullets[i].hit_enemies[h] == e) { already = 1; break; }
      }
      if (already) continue;

      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float dx = bx - (ex + er);
      float dy = by - (ey + er);
      float threshold = er + br;

      if (dx * dx + dy * dy < threshold * threshold) {
        enemy_hit(e, turret_bullets[i].vx * 0.3f, turret_bullets[i].vy * 0.3f);

        // Track hit enemy
        if (turret_bullets[i].hit_count < 8) {
          turret_bullets[i].hit_enemies[turret_bullets[i].hit_count++] = e;
        }

        // Slow zone on hit
        weapons_spawn_turret_slow_zone(ex + er, ey + er);

        // Conductor on hit
        int cond_tier = upgrades_get_tier(UPG_TURRET_CONDUCTOR);
        if (cond_tier > 0) {
          enemy_set_conductor(e, 2.0f, cond_tier);
        }

        // Pierce check
        if (turret_bullets[i].pierce_remaining == 0) {
          turret_bullets[i].active = 0;
          break;
        } else if (turret_bullets[i].pierce_remaining > 0) {
          turret_bullets[i].pierce_remaining--;
        }
        // -1 = infinite, don't decrement
      }
    }

    // Snake hit check
    if (turret_bullets[i].active) {
      if (snake_check_bullet_hit(turret_bullets[i].x, turret_bullets[i].y, br)) {
        if (turret_bullets[i].pierce_remaining == 0) {
          turret_bullets[i].active = 0;
        } else if (turret_bullets[i].pierce_remaining > 0) {
          turret_bullets[i].pierce_remaining--;
        }
      }
    }
  }
  weapons_set_damage_source(WEAPON_NONE);

  // Update turret slow zones
  for (int i = 0; i < TURRET_SLOW_MAX; i++) {
    if (!turret_slow_zones[i].active) continue;
    turret_slow_zones[i].lifetime -= dt;
    if (turret_slow_zones[i].lifetime <= 0.0f) turret_slow_zones[i].active = 0;
  }
}

// ============================================================================
// Orbit update
// ============================================================================

static float get_orbit_growth_factor(void)
{
  int grow_tier = upgrades_get_tier(UPG_ORBIT_GROWING_ORB);
  if (grow_tier <= 0 || !orbit_state.is_active) return 1.0f;
  static const float growth_mult[4] = { 1.0f, 1.5f, 2.0f, 3.0f };
  float total_dur = get_orbit_duration();
  float elapsed = total_dur - orbit_state.active_timer;
  float t = elapsed / total_dur;
  if (t > 1.0f) t = 1.0f;
  return 1.0f + (growth_mult[grow_tier] - 1.0f) * t;
}

static void orbit_update(float dt)
{
  if (!orbit_state.is_active) return;

  orbit_state.active_timer -= dt;
  if (orbit_state.active_timer <= 0.0f) {
    float px = player_get_x();
    float py = player_get_y();
    float radius = get_orbit_radius();
    int orb_count = get_orbit_orb_count();

    // Afterglow: spawn ghost orbit at current position
    int afterglow_tier = upgrades_get_tier(UPG_ORBIT_AFTERGLOW);
    if (afterglow_tier > 0) {
      for (int g = 0; g < GHOST_ORBIT_MAX; g++) {
        if (!ghost_orbits[g].active) {
          ghost_orbits[g] = (GhostOrbit_t){
            .x = px, .y = py,
            .angle = orbit_state.angle,
            .timer = get_afterglow_duration(),
            .orb_count = orb_count,
            .radius = radius,
            .active = 1
          };
          if (afterglow_tier >= 3) {
            for (int o = 0; o < ORBIT_MAX_ORBS; o++)
              ghost_orbits[g].weave_offsets[o] = orbit_weave_offset[o];
          } else {
            for (int o = 0; o < ORBIT_MAX_ORBS; o++)
              ghost_orbits[g].weave_offsets[o] = 0.0f;
          }
          break;
        }
      }
    }

    // Shatter: spawn projectiles from each orb before deactivating
    int shatter_tier = upgrades_get_tier(UPG_ORBIT_SHATTER);
    int shatter_meta = wprog_get_orbit_shatter_bonus();
    if (shatter_tier > 0 || shatter_meta > 0) {
      static const int shatter_count[4] = { 0, 3, 4, 5 };
      int count = shatter_count[shatter_tier] + shatter_meta;
      int s_pierce = 0;

      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
        float eff_r = radius + orbit_weave_offset[orb];
        float ox = px + cosf(orb_angle) * eff_r;
        float oy = py + sinf(orb_angle) * eff_r;

        for (int s = 0; s < count; s++) {
          float sa = (2.0f * (float)PI * s) / count;
          for (int si = 0; si < SHATTER_MAX; si++) {
            if (!shatter_projs[si].active) {
              shatter_projs[si] = (ShatterProj_t){
                .x = ox, .y = oy,
                .vx = cosf(sa) * 300.0f,
                .vy = sinf(sa) * 300.0f,
                .lifetime = 0.65f,
                .pierce = s_pierce,
                .active = 1,
                .hit_count = 0
              };
              break;
            }
          }
        }
      }
    }

    orbit_state.is_active = 0;
    return;
  }

  // Advance angle (1 rev/sec = 2*PI rad/s, scaled by orbit speed upgrade)
  orbit_state.angle += (2.0f * (float)PI) * dt * get_orbit_speed_mult();

  float px = player_get_x();
  float py = player_get_y();
  float radius = get_orbit_radius();
  int orb_count = get_orbit_orb_count();
  int max_e = enemy_get_max_count();

  // --- Weave: per-orb radius offset toward nearby enemies ---
  int weave_tier = upgrades_get_tier(UPG_ORBIT_WEAVE);
  float weave_max = get_weave_range();

  for (int orb = 0; orb < orb_count; orb++) {
    if (weave_tier <= 0) {
      orbit_weave_offset[orb] = 0.0f;
      continue;
    }

    float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
    float look_angle = orb_angle + 0.5f; // ~30° ahead
    float look_x = px + cosf(look_angle) * radius;
    float look_y = py + sinf(look_angle) * radius;

    float best_dist = weave_max + radius * 0.3f;
    float best_radial_diff = 0.0f;
    int found = 0;

    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er, ecy = ey + er;
      float dx = ecx - look_x, dy = ecy - look_y;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist < best_dist) {
        best_dist = dist;
        float edist = sqrtf((ecx - px) * (ecx - px) + (ecy - py) * (ecy - py));
        best_radial_diff = edist - radius;
        found = 1;
      }
    }

    float target_offset = 0.0f;
    if (found) {
      target_offset = best_radial_diff;
      if (target_offset > weave_max) target_offset = weave_max;
      if (target_offset < -weave_max) target_offset = -weave_max;
    }

    static const float track_speed[4] = { 0.0f, 3.0f, 5.0f, 8.0f };
    float lerp_speed = track_speed[weave_tier] * dt;
    if (lerp_speed > 1.0f) lerp_speed = 1.0f;
    orbit_weave_offset[orb] += (target_offset - orbit_weave_offset[orb]) * lerp_speed;
  }

  // Tick down per-enemy hit cooldowns
  for (int i = 0; i < orbit_hit_count; i++) {
    orbit_hits[i].cooldown -= dt;
    if (orbit_hits[i].cooldown <= 0.0f) {
      orbit_hits[i] = orbit_hits[orbit_hit_count - 1];
      orbit_hit_count--;
      i--;
    }
  }

  // Growing Orb: scale size over duration
  float growth = get_orbit_growth_factor();
  float orb_size = ORBIT_ORB_SIZE * growth;

  // Growing Orb T3: reduce hit cooldown as orbs grow
  int grow_tier = upgrades_get_tier(UPG_ORBIT_GROWING_ORB);
  float hit_cd = ORBIT_HIT_COOLDOWN;
  if (grow_tier >= 3 && growth > 1.0f) {
    hit_cd = ORBIT_HIT_COOLDOWN / growth;
    if (hit_cd < 0.1f) hit_cd = 0.1f;
  }

  // Check collisions for each orb (with weave offset)
  for (int orb = 0; orb < orb_count; orb++) {
    float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
    float eff_r = radius + orbit_weave_offset[orb];
    float orb_x = px + cosf(orb_angle) * eff_r;
    float orb_y = py + sinf(orb_angle) * eff_r;

    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;

      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er;
      float ecy = ey + er;

      float dx = orb_x - ecx;
      float dy = orb_y - ecy;
      float dist = sqrtf(dx * dx + dy * dy);

      int on_cooldown = 0;
      for (int h = 0; h < orbit_hit_count; h++) {
        if (orbit_hits[h].enemy_index == e) { on_cooldown = 1; break; }
      }
      if (on_cooldown) continue;

      if (dist < (orb_size + er)) {
        float kx = ecx - px;
        float ky = ecy - py;
        float klen = sqrtf(kx * kx + ky * ky);
        if (klen > 0.1f) { kx /= klen; ky /= klen; }
        enemy_hit(e, kx * ORBIT_KNOCKBACK, ky * ORBIT_KNOCKBACK);

        if (orbit_hit_count < ORBIT_MAX_HIT_TRACK) {
          orbit_hits[orbit_hit_count].enemy_index = e;
          orbit_hits[orbit_hit_count].cooldown = hit_cd;
          orbit_hit_count++;
        }

        int cond_tier = upgrades_get_tier(UPG_ORBIT_CONDUCTOR);
        if (cond_tier > 0) {
          enemy_set_conductor(e, 2.0f, cond_tier);
        }
      }
    }
    snake_check_hit_aoe(orb_x, orb_y, orb_size);
    {
      int cond_tier = upgrades_get_tier(UPG_ORBIT_CONDUCTOR);
      if (cond_tier > 0)
        snake_apply_conductor_aoe(orb_x, orb_y, orb_size, 2.0f, cond_tier);
    }
  }

  // Orbit linger trail (with weave offset)
  int linger_tier = upgrades_get_tier(UPG_ORBIT_LINGER_TRAIL);
  if (linger_tier > 0) {
    orbit_trail_timer += dt;
    if (orbit_trail_timer >= 0.3f) {
      orbit_trail_timer -= 0.3f;
      static const float trail_dur[4] = { 0.0f, 0.9f, 1.8f, 2.7f };
      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
        float eff_r = radius + orbit_weave_offset[orb];
        float ox = px + cosf(orb_angle) * eff_r;
        float oy = py + sinf(orb_angle) * eff_r;
        linger_spawn(ox, oy, 24.0f, trail_dur[linger_tier], (aColor_t){255, 160, 50, 200});
      }
    }
  }

  // Gravity Well (with weave offset)
  int gw_tier = upgrades_get_tier(UPG_ORBIT_GRAVITY_WELL);
  if (gw_tier > 0) {
    static const float gw_interval[4] = { 0.0f, 0.4f, 0.3f, 0.25f };
    static const float gw_dur[4]      = { 0.0f, 2.1f, 2.8f, 3.5f };
    static const float gw_radius_t[4] = { 0.0f, 25.0f, 31.0f, 39.0f };
    gw_trail_timer += dt;
    if (gw_trail_timer >= gw_interval[gw_tier]) {
      gw_trail_timer -= gw_interval[gw_tier];
      float dmg_mult = (gw_tier >= 3) ? 1.3f : 1.0f;
      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
        float eff_r = radius + orbit_weave_offset[orb];
        float ox = px + cosf(orb_angle) * eff_r;
        float oy = py + sinf(orb_angle) * eff_r;
        for (int ci = 0; ci < CRATER_MAX; ci++) {
          if (!crater_zones[ci].active) {
            crater_zones[ci] = (CraterZone_t){
              .x = ox, .y = oy,
              .radius = gw_radius_t[gw_tier],
              .lifetime = gw_dur[gw_tier],
              .max_lifetime = gw_dur[gw_tier],
              .damage_mult = dmg_mult,
              .active = 1,
              .is_bomb_crater = 0
            };
            break;
          }
        }
      }
    }
  }
}

// ============================================================================
// Ghost orbit update (Afterglow)
// ============================================================================

static void ghost_orbit_update(float dt)
{
  int max_e = enemy_get_max_count();
  int weave_tier = upgrades_get_tier(UPG_ORBIT_WEAVE);
  int shatter_tier = upgrades_get_tier(UPG_ORBIT_SHATTER);
  int shatter_meta = wprog_get_orbit_shatter_bonus();
  int cond_tier = upgrades_get_tier(UPG_ORBIT_CONDUCTOR);
  float orb_sz = ORBIT_ORB_SIZE;

  // Tick down ghost hit cooldowns
  for (int h = ghost_orbit_hit_count - 1; h >= 0; h--) {
    ghost_orbit_hits[h].cooldown -= dt;
    if (ghost_orbit_hits[h].cooldown <= 0.0f) {
      ghost_orbit_hits[h] = ghost_orbit_hits[ghost_orbit_hit_count - 1];
      ghost_orbit_hit_count--;
    }
  }

  for (int g = 0; g < GHOST_ORBIT_MAX; g++) {
    if (!ghost_orbits[g].active) continue;

    ghost_orbits[g].timer -= dt;
    if (ghost_orbits[g].timer <= 0.0f) {
      // Ghost expires — Shatter if upgrade present
      if (shatter_tier > 0 || shatter_meta > 0) {
        static const int shatter_count[4] = { 0, 3, 4, 5 };
        int count = shatter_count[shatter_tier] + shatter_meta;

        for (int orb = 0; orb < ghost_orbits[g].orb_count; orb++) {
          float orb_angle = ghost_orbits[g].angle +
            (2.0f * (float)PI * orb) / ghost_orbits[g].orb_count;
          float eff_r = ghost_orbits[g].radius + ghost_orbits[g].weave_offsets[orb];
          float ox = ghost_orbits[g].x + cosf(orb_angle) * eff_r;
          float oy = ghost_orbits[g].y + sinf(orb_angle) * eff_r;

          for (int s = 0; s < count; s++) {
            float sa = (2.0f * (float)PI * s) / count;
            for (int si = 0; si < SHATTER_MAX; si++) {
              if (!shatter_projs[si].active) {
                shatter_projs[si] = (ShatterProj_t){
                  .x = ox, .y = oy,
                  .vx = cosf(sa) * 300.0f,
                  .vy = sinf(sa) * 300.0f,
                  .lifetime = 0.65f,
                  .pierce = 0,
                  .active = 1,
                  .hit_count = 0
                };
                break;
              }
            }
          }
        }
      }

      // Clean up hit entries for this ghost
      for (int h = ghost_orbit_hit_count - 1; h >= 0; h--) {
        if (ghost_orbit_hits[h].ghost_index == g) {
          ghost_orbit_hits[h] = ghost_orbit_hits[ghost_orbit_hit_count - 1];
          ghost_orbit_hit_count--;
        }
      }

      ghost_orbits[g].active = 0;
      continue;
    }

    // Advance ghost angle
    ghost_orbits[g].angle += (2.0f * (float)PI) * dt * get_orbit_speed_mult();

    // Linger trail: ghost orbs drop lingering ground zones
    int linger_tier = upgrades_get_tier(UPG_ORBIT_LINGER_TRAIL);
    if (linger_tier > 0) {
      ghost_orbits[g].trail_timer += dt;
      if (ghost_orbits[g].trail_timer >= 0.3f) {
        ghost_orbits[g].trail_timer -= 0.3f;
        static const float trail_dur[4] = { 0.0f, 0.9f, 1.8f, 2.7f };
        float dur = trail_dur[linger_tier] * 0.6f;  // shorter than live orbit
        for (int orb = 0; orb < ghost_orbits[g].orb_count; orb++) {
          float orb_angle = ghost_orbits[g].angle +
            (2.0f * (float)PI * orb) / ghost_orbits[g].orb_count;
          float eff_r = ghost_orbits[g].radius + ghost_orbits[g].weave_offsets[orb];
          float ox = ghost_orbits[g].x + cosf(orb_angle) * eff_r;
          float oy = ghost_orbits[g].y + sinf(orb_angle) * eff_r;
          linger_spawn(ox, oy, 20.0f, dur, (aColor_t){200, 140, 255, 160});
        }
      }
    }

    // Weave update (T3 Afterglow only)
    int afterglow_tier = upgrades_get_tier(UPG_ORBIT_AFTERGLOW);
    if (afterglow_tier >= 3 && weave_tier > 0) {
      float weave_max = get_weave_range();
      float gcx = ghost_orbits[g].x;
      float gcy = ghost_orbits[g].y;
      float g_radius = ghost_orbits[g].radius;

      for (int orb = 0; orb < ghost_orbits[g].orb_count; orb++) {
        float orb_angle = ghost_orbits[g].angle +
          (2.0f * (float)PI * orb) / ghost_orbits[g].orb_count;
        float look_angle = orb_angle + 0.5f;
        float look_x = gcx + cosf(look_angle) * g_radius;
        float look_y = gcy + sinf(look_angle) * g_radius;

        float best_dist = weave_max + g_radius * 0.3f;
        float best_radial_diff = 0.0f;
        int found = 0;

        for (int e = 0; e < max_e; e++) {
          if (!enemy_is_alive(e)) continue;
          float ex, ey;
          enemy_get_position(e, &ex, &ey);
          float er = enemy_get_radius(e);
          float ecx = ex + er, ecy = ey + er;
          float dx = ecx - look_x, dy = ecy - look_y;
          float dist = sqrtf(dx * dx + dy * dy);
          if (dist < best_dist) {
            best_dist = dist;
            float edist = sqrtf((ecx - gcx) * (ecx - gcx) + (ecy - gcy) * (ecy - gcy));
            best_radial_diff = edist - g_radius;
            found = 1;
          }
        }

        float target = 0.0f;
        if (found) {
          target = best_radial_diff;
          if (target > weave_max) target = weave_max;
          if (target < -weave_max) target = -weave_max;
        }

        static const float track_speed[4] = { 0.0f, 3.0f, 5.0f, 8.0f };
        float lerp = track_speed[weave_tier] * dt;
        if (lerp > 1.0f) lerp = 1.0f;
        ghost_orbits[g].weave_offsets[orb] +=
          (target - ghost_orbits[g].weave_offsets[orb]) * lerp;
      }
    }

    // Collision detection
    for (int orb = 0; orb < ghost_orbits[g].orb_count; orb++) {
      float orb_angle = ghost_orbits[g].angle +
        (2.0f * (float)PI * orb) / ghost_orbits[g].orb_count;
      float eff_r = ghost_orbits[g].radius + ghost_orbits[g].weave_offsets[orb];
      float orb_x = ghost_orbits[g].x + cosf(orb_angle) * eff_r;
      float orb_y = ghost_orbits[g].y + sinf(orb_angle) * eff_r;

      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;

        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er, ecy = ey + er;
        float dx = orb_x - ecx, dy = orb_y - ecy;
        float dist = sqrtf(dx * dx + dy * dy);

        // Check ghost-specific hit cooldown
        int on_cd = 0;
        for (int h = 0; h < ghost_orbit_hit_count; h++) {
          if (ghost_orbit_hits[h].ghost_index == g &&
              ghost_orbit_hits[h].enemy_index == e) {
            on_cd = 1;
            break;
          }
        }
        if (on_cd) continue;

        if (dist < (orb_sz + er)) {
          float kx = ecx - ghost_orbits[g].x;
          float ky = ecy - ghost_orbits[g].y;
          float klen = sqrtf(kx * kx + ky * ky);
          if (klen > 0.1f) { kx /= klen; ky /= klen; }
          enemy_hit(e, kx * ORBIT_KNOCKBACK, ky * ORBIT_KNOCKBACK);

          if (ghost_orbit_hit_count < GHOST_ORBIT_HIT_MAX) {
            ghost_orbit_hits[ghost_orbit_hit_count++] = (GhostOrbitHit_t){
              .enemy_index = e,
              .cooldown = GHOST_ORBIT_HIT_COOLDOWN,
              .ghost_index = g
            };
          }

          if (cond_tier > 0) {
            enemy_set_conductor(e, 2.0f, cond_tier);
          }
        }
      }

      snake_check_hit_aoe(orb_x, orb_y, orb_sz);
      if (cond_tier > 0)
        snake_apply_conductor_aoe(orb_x, orb_y, orb_sz, 2.0f, cond_tier);
    }
  }
}

// ============================================================================
// Chain propagation update
// ============================================================================

static void chain_update(float dt)
{
  if (!chain_state.active) return;

  chain_state.propagation_timer -= dt;

  if (chain_state.propagation_timer <= 0.0f) {
    int idx = chain_state.current_jump;
    int cur = chain_state.targets[idx];

    // Check if target is a corpse (Corpse Chain upgrade)
    int is_corpse_target = chain_state.target_is_corpse[idx];

    // Skip fully inactive enemies (can happen during bounce back)
    if (!enemy_is_active(cur) || (!enemy_is_alive(cur) && !is_corpse_target)) {
      chain_state.current_jump++;
      if (chain_state.current_jump < chain_state.num_targets) {
        chain_state.propagation_timer = CHAIN_DELAY;
        goto chain_propagation_done;
      }
      // Fall through to chain-finished logic below (bounce back / post-chain effects)
    } else {
      int prev = chain_state.targets[idx - 1];

      float cur_x, cur_y, prev_x, prev_y;
      enemy_get_position(cur, &cur_x, &cur_y);
      float cr = enemy_get_radius(cur);
      float ccx = cur_x + cr;
      float ccy = cur_y + cr;

      // For visual segment: use previous target position
      float pcx = ccx, pcy = ccy;
      if (enemy_is_active(prev)) {
        enemy_get_position(prev, &prev_x, &prev_y);
        float pr = enemy_get_radius(prev);
        pcx = prev_x + pr;
        pcy = prev_y + pr;
      }

      if (is_corpse_target) {
        // Corpse Chain: consume corpse and spawn explosion
        enemy_consume_corpse(cur);
        static const float cc_radius[4] = { 0.0f, 40.0f, 55.0f, 70.0f };
        int cc_t = upgrades_get_tier(UPG_CHAIN_CORPSE_CHAIN);
        float boom_r = cc_radius[cc_t];

        // Visual explosion
        enemy_spawn_explosion(ccx, ccy, boom_r);

        // AoE damage to alive enemies in range
        int ae_max = enemy_get_max_count();
        for (int ae = 0; ae < ae_max; ae++) {
          if (!enemy_is_alive(ae)) continue;
          float aex, aey;
          enemy_get_position(ae, &aex, &aey);
          float aer = enemy_get_radius(ae);
          float aecx = aex + aer;
          float aecy = aey + aer;
          float adx = aecx - ccx;
          float ady = aecy - ccy;
          float adist = sqrtf(adx * adx + ady * ady);
          if (adist < boom_r + aer) {
            float kx = (adist > 0.1f) ? (adx / adist) * 200.0f : 200.0f;
            float ky = (adist > 0.1f) ? (ady / adist) * 200.0f : 0.0f;
            enemy_hit(ae, kx, ky);
          }
        }
        snake_check_hit_aoe(ccx, ccy, boom_r);
      } else {
        // Normal hit: knockback direction previous -> current
        float dx = ccx - pcx;
        float dy = ccy - pcy;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.1f) { dx /= len; dy /= len; }
        enemy_hit(cur, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);
      }

      if (chain_sound_loaded) {
        aAudioOptions_t opts = {
          .channel = AUDIO_CHANNEL_AUTO,
          .volume = 70, .loops = 0, .fade_ms = 0, .interrupt = 0
        };
        a_AudioPlaySound(&chain_sound, &opts);
      }

      // Mini stun on chain hit
      {
        int ms_tier = upgrades_get_tier(UPG_CHAIN_MINI_STUN);
        if (ms_tier > 0) {
          static const float stun_dur[4] = { 0.0f, 0.1f, 0.15f, 0.2f };
          enemy_set_stun(cur, stun_dur[ms_tier], 1.0f);
        }
      }

      // Chain linger arc (rare upgrade): spawn small zone at target
      {
        int chain_linger_tier = upgrades_get_tier(UPG_CHAIN_LINGER_ARC);
        if (chain_linger_tier > 0) {
          static const float chain_linger_dur[4] = { 0.0f, 0.75f, 1.5f, 2.25f };
          linger_spawn(ccx, ccy, 30.0f, chain_linger_dur[chain_linger_tier],
                       (aColor_t){200, 220, 255, 200});
        }
      }

      // Snake head: check if nearby
      snake_check_hit_aoe(ccx, ccy, get_chain_radius());

      // Add visual segment
      if (chain_segment_count < CHAIN_SEG_MAX) {
        chain_segments[chain_segment_count] = (ChainSegment_t){
          .x1 = pcx, .y1 = pcy,
          .x2 = ccx, .y2 = ccy,
          .timer = CHAIN_VISUAL_DURATION
        };
        chain_segment_count++;
      }

      chain_state.current_jump++;
    }
    if (chain_state.current_jump >= chain_state.num_targets) {

      // Bounce Back: reverse the chain and re-hit targets
      if (!chain_state.bounced) {
        int bb_tier = upgrades_get_tier(UPG_CHAIN_BOUNCE_BACK);
        if (bb_tier > 0 && chain_state.num_targets >= 2) {
          int orig_count = chain_state.num_targets;
          // T1: re-hit last 1, T2: last 2, T3: all in reverse
          int bounce_count = (bb_tier >= 3) ? (orig_count - 1)
                           : (bb_tier < orig_count ? bb_tier : orig_count - 1);
          for (int b = 0; b < bounce_count && chain_state.num_targets < CHAIN_MAX_JUMPS; b++) {
            // Walk backwards from second-to-last target
            int src_idx = orig_count - 2 - b;
            if (src_idx < 0) break;
            chain_state.targets[chain_state.num_targets] = chain_state.targets[src_idx];
            chain_state.num_targets++;
          }
          chain_state.bounced = 1;
          chain_state.propagation_timer = CHAIN_DELAY;
          // Don't deactivate — let propagation continue through reversed targets
          goto chain_propagation_done;
        }
      }

      chain_state.active = 0;

      // Lightning Rod achievement: start kill window (knockbacks resolve next frame)
      chain_kill_window = 0.2f;

      // === Post-chain effects (all chain jumps done) ===

      // Overload: AoE on final target if 3+ enemies hit
      {
        int ol_tier = upgrades_get_tier(UPG_CHAIN_OVERLOAD);
        if (ol_tier > 0 && chain_state.num_targets >= 3) {
          static const float ol_radius[4] = { 0.0f, 40.0f, 60.0f, 80.0f };
          int final_t = chain_state.targets[chain_state.num_targets - 1];
          float fx, fy;
          enemy_get_position(final_t, &fx, &fy);
          float fr = enemy_get_radius(final_t);
          float fcx = fx + fr;
          float fcy = fy + fr;

          // AoE damage at final target position
          float aoe_r = ol_radius[ol_tier];
          int ae_max = enemy_get_max_count();
          for (int ae = 0; ae < ae_max; ae++) {
            if (!enemy_is_alive(ae)) continue;
            float aex, aey;
            enemy_get_position(ae, &aex, &aey);
            float aer = enemy_get_radius(ae);
            float aecx = aex + aer;
            float aecy = aey + aer;
            float adx = aecx - fcx;
            float ady = aecy - fcy;
            float adist = sqrtf(adx * adx + ady * ady);
            if (adist < aoe_r + aer) {
              float kx = (adist > 0.1f) ? (adx / adist) * 100.0f : 100.0f;
              float ky = (adist > 0.1f) ? (ady / adist) * 100.0f : 0.0f;
              enemy_hit(ae, kx, ky);
            }
          }

          // Spawn overload explosion visual (reuse linger as a quick flash)
          linger_spawn(fcx, fcy, aoe_r, 0.3f, (aColor_t){180, 200, 255, 220});
        }
      }

    } else {
      chain_state.propagation_timer = CHAIN_DELAY;
    }
  chain_propagation_done:;
  }
}

// ============================================================================
// Trail
// ============================================================================

static void fire_trail(void)
{
  trail_active = 1;
  trail_active_timer = get_trail_duration();
  trail_last_x = player_get_x();
  trail_last_y = player_get_y();
  trail_total_unique = 0;

  // Activation burst: fire particles at player feet + sound
  fire_particles_spawn(trail_last_x, trail_last_y, 0.0f, 1.0f);
  game_audio_play_fire_hit();
}

static void trail_spawn_segment(float x, float y, float dx, float dy)
{
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) {
      float persist = get_trail_persist();
      trail_segments[i] = (TrailSegment_t){
        .x = x, .y = y,
        .dir_x = dx, .dir_y = dy,
        .lifetime = persist, .max_lifetime = persist,
        .active = 1
      };
      return;
    }
  }
}

static void trail_spawn_ember(float x, float y)
{
  int tier = upgrades_get_tier(UPG_TRAIL_EMBER_BURST);
  if (tier <= 0) return;
  static const float radii[4] = { 0.0f, 20.0f, 30.0f, 40.0f };
  for (int i = 0; i < TRAIL_EMBER_MAX; i++) {
    if (!trail_embers[i].active) {
      trail_embers[i] = (TrailEmber_t){
        .x = x, .y = y,
        .radius = radii[tier],
        .timer = 0.2f,
        .has_damage = (tier >= 3) ? 1 : 0,
        .active = 1
      };
      return;
    }
  }
}

static void trail_update(float dt)
{
  int max_e = enemy_get_max_count();

  // Active timer countdown
  if (trail_active) {
    trail_active_timer -= dt;
    if (trail_active_timer <= 0.0f) {
      trail_active = 0;
      trail_active_timer = 0.0f;
    }

    // Drop segments based on player movement distance
    float px = player_get_x();
    float py = player_get_y();
    float dx = px - trail_last_x;
    float dy = py - trail_last_y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist >= TRAIL_DROP_DISTANCE) {
      // Normalize direction
      float ndx = dx / dist;
      float ndy = dy / dist;

      // Drop segments at each interval along the path
      while (dist >= TRAIL_DROP_DISTANCE) {
        trail_last_x += ndx * TRAIL_DROP_DISTANCE;
        trail_last_y += ndy * TRAIL_DROP_DISTANCE;
        trail_spawn_segment(trail_last_x, trail_last_y, ndx, ndy);
        dist -= TRAIL_DROP_DISTANCE;
      }
    }
  }

  // Update segment lifetimes first
  float half_w = get_trail_width() * 0.5f;
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) continue;
    trail_segments[i].lifetime -= dt;
    if (trail_segments[i].lifetime <= 0.0f) {
      trail_spawn_ember(trail_segments[i].x, trail_segments[i].y);
      // Queue for Inferno Wake chain detonation
      if (upgrades_get_tier(UPG_TRAIL_INFERNO_WAKE) > 0 && !inferno_active &&
          inferno_queue_count < INFERNO_MAX_QUEUE) {
        inferno_queue[inferno_queue_count++] = (InfernoPos_t){
          trail_segments[i].x, trail_segments[i].y
        };
      }
      trail_segments[i].active = 0;
    }
  }

  // Tick trail hit cooldowns — remove entries for dead/off-trail enemies
  for (int h = trail_hit_count - 1; h >= 0; h--) {
    trail_hits[h].cooldown -= dt;
    if (trail_hits[h].cooldown > 0.0f) continue;

    int e = trail_hits[h].enemy;
    // Check if enemy is still alive and overlapping any segment
    int on_trail = 0;
    float hit_sx = 0, hit_sy = 0, hit_ecx = 0, hit_ecy = 0;
    if (enemy_is_alive(e)) {
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er, ecy = ey + er;
      for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
        if (!trail_segments[i].active) continue;
        float sdx = ecx - trail_segments[i].x;
        float sdy = ecy - trail_segments[i].y;
        if (sqrtf(sdx * sdx + sdy * sdy) < half_w + er) {
          on_trail = 1;
          hit_sx = trail_segments[i].x;
          hit_sy = trail_segments[i].y;
          hit_ecx = ecx; hit_ecy = ecy;
          break;
        }
      }
    }

    if (on_trail) {
      // Still on fire — deal tick damage, reset cooldown
      enemy_hit(e, 0.0f, 0.0f);
      float sdx = hit_ecx - hit_sx, sdy = hit_ecy - hit_sy;
      float pdist = sqrtf(sdx * sdx + sdy * sdy);
      fire_particles_spawn(hit_ecx, hit_ecy,
                           (pdist > 1.0f) ? sdx / pdist : 0.0f,
                           (pdist > 1.0f) ? sdy / pdist : 1.0f);
      game_audio_play_fire_hit();
      trail_hits[h].cooldown = TRAIL_TICK_RATE;
    } else {
      // Enemy left trail or died — remove entry
      trail_hits[h] = trail_hits[trail_hit_count - 1];
      trail_hit_count--;
    }
  }

  // Add new trail hit entries for enemies overlapping segments (first contact = instant hit)
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) continue;
    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er, ecy = ey + er;
      float sdx = ecx - trail_segments[i].x;
      float sdy = ecy - trail_segments[i].y;
      if (sqrtf(sdx * sdx + sdy * sdy) >= half_w + er) continue;

      // Check if already tracked (on cooldown from recent hit)
      int found = 0;
      for (int h = 0; h < trail_hit_count; h++) {
        if (trail_hits[h].enemy == e) { found = 1; break; }
      }
      if (found) continue;

      // First contact — instant hit + start cooldown
      enemy_hit(e, 0.0f, 0.0f);
      float pdist = sqrtf(sdx * sdx + sdy * sdy);
      fire_particles_spawn(ecx, ecy,
                           (pdist > 1.0f) ? sdx / pdist : 0.0f,
                           (pdist > 1.0f) ? sdy / pdist : 1.0f);
      game_audio_play_fire_hit();
      if (trail_hit_count < TRAIL_HIT_TRACK) {
        trail_hits[trail_hit_count] = (TrailHit_t){ e, TRAIL_TICK_RATE, 1 };
        trail_hit_count++;
      }
      // Scorched Earth achievement
      trail_total_unique++;
      achievements_notify_weapon_challenge(WEAPON_TRAIL, trail_total_unique);
    }
  }

  // Snake trail contact damage
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) continue;
    snake_check_hit_no_knockback(trail_segments[i].x, trail_segments[i].y, half_w);
  }

  // Heat Mirage: pull nearby enemies toward trail segments
  {
    int mirage_tier = upgrades_get_tier(UPG_TRAIL_HEAT_MIRAGE);
    if (mirage_tier > 0) {
      static const float pull_range[4] = { 0.0f, 80.0f, 65.0f, 50.0f };
      static const float pull_speed[4] = { 0.0f, 90.0f, 130.0f, 180.0f };
      float range = pull_range[mirage_tier];
      float speed_val = pull_speed[mirage_tier];

      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er, ecy = ey + er;

        // Find nearest active pull segment (every other segment pulls)
        float best_dist = range + er;
        float best_sx = 0, best_sy = 0;
        int found_seg = 0;
        for (int s = 0; s < TRAIL_MAX_SEGMENTS; s++) {
          if (!trail_segments[s].active) continue;
          if (s % 2 != 0) continue;  // Only even-index segments pull
          float sdx = trail_segments[s].x - ecx;
          float sdy = trail_segments[s].y - ecy;
          float sdist = sqrtf(sdx * sdx + sdy * sdy);
          if (sdist < best_dist) {
            best_dist = sdist;
            best_sx = trail_segments[s].x;
            best_sy = trail_segments[s].y;
            found_seg = 1;
          }
        }

        if (found_seg && best_dist > 1.0f) {
          float pdx = best_sx - ecx;
          float pdy = best_sy - ecy;
          float plen = sqrtf(pdx * pdx + pdy * pdy);
          enemy_displace(e, (pdx / plen) * speed_val * dt, (pdy / plen) * speed_val * dt);
        }
      }
    }
  }

  // Inferno Wake: start chain if segments queued and not already running
  if (!inferno_active && inferno_queue_count > 0) {
    inferno_active = 1;
    inferno_index = 0;
    inferno_timer = 0.0f;
  }

  // Inferno Wake: process chain detonation
  if (inferno_active) {
    int inferno_tier = upgrades_get_tier(UPG_TRAIL_INFERNO_WAKE);
    static const float aoe_r[4] = { 0.0f, 30.0f, 40.0f, 50.0f };
    static const float delays[4] = { 0.0f, 0.05f, 0.03f, 0.03f };
    float blast_r = aoe_r[inferno_tier];
    float delay = delays[inferno_tier];

    inferno_timer -= dt;
    while (inferno_timer <= 0.0f && inferno_index < inferno_queue_count) {
      float ix = inferno_queue[inferno_index].x;
      float iy = inferno_queue[inferno_index].y;

      // AoE damage at this position
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er, ecy = ey + er;
        float ddx = ecx - ix, ddy = ecy - iy;
        float dist = sqrtf(ddx * ddx + ddy * ddy);
        if (dist < blast_r + er) {
          float len = dist > 0.1f ? dist : 1.0f;
          enemy_hit(e, (ddx / len) * 60.0f, (ddy / len) * 60.0f);
          fire_particles_spawn(ecx, ecy, ddx / len, ddy / len);
        }
      }
      snake_check_hit_aoe(ix, iy, blast_r);

      // T3: spawn mini linger fire zone
      if (inferno_tier >= 3) {
        linger_spawn(ix, iy, blast_r * 0.6f, 0.75f, (aColor_t){255, 100, 20, 60});
      }

      // Spawn visual explosion
      for (int v = 0; v < INFERNO_VISUAL_MAX; v++) {
        if (!inferno_blasts[v].active) {
          inferno_blasts[v] = (InfernoBlast_t){
            .x = ix, .y = iy,
            .radius = blast_r,
            .timer = 0.2f,
            .active = 1
          };
          break;
        }
      }

      inferno_index++;
      inferno_timer += delay;
    }

    // Chain complete
    if (inferno_index >= inferno_queue_count) {
      inferno_active = 0;
      inferno_queue_count = 0;
    }
  }

  // Update inferno blast visuals
  for (int i = 0; i < INFERNO_VISUAL_MAX; i++) {
    if (!inferno_blasts[i].active) continue;
    inferno_blasts[i].timer -= dt;
    if (inferno_blasts[i].timer <= 0.0f) inferno_blasts[i].active = 0;
  }

  // Update ember bursts
  for (int i = 0; i < TRAIL_EMBER_MAX; i++) {
    if (!trail_embers[i].active) continue;
    trail_embers[i].timer -= dt;
    if (trail_embers[i].timer <= 0.0f) {
      trail_embers[i].active = 0;
      continue;
    }

    // Ember T3 damage: hit enemies in radius (once per ember)
    if (trail_embers[i].has_damage) {
      trail_embers[i].has_damage = 0;  // Only damage once
      float er_x = trail_embers[i].x;
      float er_y = trail_embers[i].y;
      float er_r = trail_embers[i].radius;
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float erad = enemy_get_radius(e);
        float ecx = ex + erad, ecy = ey + erad;
        float edx = ecx - er_x, edy = ecy - er_y;
        if (sqrtf(edx * edx + edy * edy) < er_r + erad) {
          float klen = sqrtf(edx * edx + edy * edy);
          float kvx = klen > 0.1f ? edx / klen * 80.0f : 0.0f;
          float kvy = klen > 0.1f ? edy / klen * 80.0f : 0.0f;
          enemy_hit(e, kvx, kvy);
        }
      }
      snake_check_hit_aoe(er_x, er_y, er_r);
    }
  }
}

// ============================================================================
// Scythe
// ============================================================================

static float get_scythe_radius(void)
{
  return SCYTHE_BASE_RADIUS + 20.0f * upgrades_get_tier(UPG_SCYTHE_REACH)
         + wprog_get_reach_bonus(WID_SCYTHE);
}

static float get_scythe_arc(void)
{
  static const float arcs[4] = { 120.0f, 150.0f, 180.0f, 240.0f };
  return arcs[upgrades_get_tier(UPG_SCYTHE_WIDE_SWEEP)];
}

static float get_scythe_knockback(void)
{
  return SCYTHE_BASE_KNOCKBACK * (1.0f + 0.2f * upgrades_get_tier(UPG_SCYTHE_KNOCKBACK));
}

static int angle_in_arc(float a, float start, float end)
{
  // Normalize all to [0, 2*PI)
  while (a < 0) a += 2.0f * (float)PI;
  while (a >= 2.0f * (float)PI) a -= 2.0f * (float)PI;
  while (start < 0) start += 2.0f * (float)PI;
  while (start >= 2.0f * (float)PI) start -= 2.0f * (float)PI;
  while (end < 0) end += 2.0f * (float)PI;
  while (end >= 2.0f * (float)PI) end -= 2.0f * (float)PI;

  if (start <= end) {
    return (a >= start && a <= end);
  } else {
    return (a >= start || a <= end);  // wraps around 0
  }
}

static void fire_scythe(void)
{
  float px = player_get_x();
  float py = player_get_y();

  // Target nearest alive enemy; fall back to facing direction
  float fx = player_get_facing_x();
  float fy = player_get_facing_y();
  {
    float best_dist = 1e9f;
    int max_e_scan = enemy_get_max_count();
    for (int i = 0; i < max_e_scan; i++) {
      if (!enemy_is_active(i)) continue;
      EnemyState_t st = enemy_get_state(i);
      if (st == ENEMY_STATE_CORPSE || st == ENEMY_STATE_HIT_KNOCKBACK) continue;
      float ex, ey;
      enemy_get_position(i, &ex, &ey);
      float er = enemy_get_radius(i);
      float dx = (ex + er) - px;
      float dy = (ey + er) - py;
      float d = sqrtf(dx * dx + dy * dy);
      if (d < best_dist) {
        best_dist = d;
        fx = dx;
        fy = dy;
      }
    }
    // Also check snake heads
    float sx, sy;
    if (snake_find_nearest_head(px, py, &best_dist, &sx, &sy)) {
      fx = sx - px;
      fy = sy - py;
    }

    float len = sqrtf(fx * fx + fy * fy);
    if (len > 0.001f) { fx /= len; fy /= len; }
    else { fx = 0.0f; fy = -1.0f; }
  }

  float radius = get_scythe_radius();
  float arc_deg = get_scythe_arc();
  float kb = get_scythe_knockback();

  // Whirlwind: override arc to 360 on Nth swing
  int ww_tier = upgrades_get_tier(UPG_SCYTHE_WHIRLWIND);
  static const int whirlwind_every[4] = { 0, 4, 3, 2 };
  int is_whirlwind = 0;
  if (ww_tier > 0 && whirlwind_every[ww_tier] > 0 &&
      (scythe_swing_count % whirlwind_every[ww_tier]) == 0) {
    arc_deg = 360.0f;
    is_whirlwind = 1;
  }

  float facing_angle = atan2f(fy, fx);
  float half_arc = (arc_deg * (float)PI / 180.0f) / 2.0f;
  float arc_start = facing_angle - half_arc;
  float arc_end = facing_angle + half_arc;

  // Reap threshold
  int reap_tier = upgrades_get_tier(UPG_SCYTHE_REAP);
  static const int reap_threshold[4] = { 0, 2, 3, 4 };

  int max_e = enemy_get_max_count();
  for (int i = 0; i < max_e; i++) {
    if (!enemy_is_active(i)) continue;

    EnemyState_t state = enemy_get_state(i);
    if (state == ENEMY_STATE_CORPSE || state == ENEMY_STATE_HIT_KNOCKBACK) continue;

    float ex, ey;
    enemy_get_position(i, &ex, &ey);
    float er = enemy_get_radius(i);
    float ecx = ex + er;
    float ecy = ey + er;

    float dx = ecx - px;
    float dy = ecy - py;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > radius) continue;

    // Arc check (skip for 360° whirlwind)
    if (arc_deg < 360.0f) {
      float enemy_angle = atan2f(dy, dx);
      if (!angle_in_arc(enemy_angle, arc_start, arc_end)) continue;
    }

    // Reap: execute low-HP enemies (not snakes — those use snake_check_hit_aoe)
    if (reap_tier > 0) {
      int remaining = enemy_get_remaining_hp(i);
      if (remaining > 0 && remaining <= reap_threshold[reap_tier]) {
        // Kill by repeated hits through normal pipeline
        for (int h = 0; h < remaining; h++) {
          enemy_hit(i, 0.0f, 0.0f);
        }
        continue;
      }
    }

    // Normal hit with perpendicular knockback
    float len = dist > 0.1f ? dist : 1.0f;
    float ndx = dx / len;
    float ndy = dy / len;

    float kx = -ndy * kb;  // rotate 90° clockwise
    float ky = ndx * kb;
    enemy_hit(i, kx, ky);
  }

  // Grim Reaper achievement: finalize previous sweep if window still open
  if (scythe_sweep_window > 0.0f) {
    achievements_notify_weapon_challenge(WEAPON_SCYTHE, scythe_sweep_kills);
    scythe_sweep_window = 0.0f;
  }
  scythe_sweep_kills = 0;
  scythe_sweep_window = 0.2f;  // 200ms window for knockback kills to resolve

  // Snake AOE check (simplified circle, no arc)
  snake_check_hit_aoe(px, py, radius);

  // Harvest: consume corpses in arc, reduce scythe cooldown per corpse
  int harvest_tier = upgrades_get_tier(UPG_SCYTHE_HARVEST);
  if (harvest_tier > 0) {
    for (int i = 0; i < max_e; i++) {
      if (!enemy_is_active(i)) continue;
      if (enemy_get_state(i) != ENEMY_STATE_CORPSE) continue;

      float ex, ey;
      enemy_get_position(i, &ex, &ey);
      float er = enemy_get_radius(i);
      float ecx = ex + er;
      float ecy = ey + er;

      float dx = ecx - px;
      float dy = ecy - py;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist > radius) continue;

      if (arc_deg < 360.0f) {
        float enemy_angle = atan2f(dy, dx);
        if (!angle_in_arc(enemy_angle, arc_start, arc_end)) continue;
      }

      enemy_consume_corpse(i);
      scythe_harvest_refund += 0.2f;
    }
  }

  // Spawn arc-shaped zones (slow and/or linger)
  float zone_facing = atan2f(fy, fx);

  // Slow Zone (arc-shaped)
  int slow_tier = upgrades_get_tier(UPG_SCYTHE_SLOW_ZONE);
  if (slow_tier > 0) {
    static const float slow_mult[4] = { 1.0f, 0.80f, 0.70f, 0.60f };
    static const float slow_dur[4]  = { 0.0f, 1.0f,  1.5f,  2.0f  };
    for (int si = 0; si < SCYTHE_ZONE_MAX; si++) {
      if (!scythe_zones[si].active) {
        scythe_zones[si] = (ScytheZone_t){
          .cx = px, .cy = py,
          .radius = radius,
          .facing_angle = zone_facing,
          .arc_deg = arc_deg,
          .lifetime = slow_dur[slow_tier],
          .max_lifetime = slow_dur[slow_tier],
          .slow_mult = slow_mult[slow_tier],
          .is_linger = 0,
          .tick_timer = 0.0f,
          .active = 1
        };
        break;
      }
    }
  }

  // Lingering Trail (arc-shaped damage zone)
  int linger_tier = upgrades_get_tier(UPG_SCYTHE_LINGER_TRAIL);
  if (linger_tier > 0) {
    static const float linger_dur[4] = { 0.0f, 1.0f, 1.5f, 2.0f };
    for (int si = 0; si < SCYTHE_ZONE_MAX; si++) {
      if (!scythe_zones[si].active) {
        scythe_zones[si] = (ScytheZone_t){
          .cx = px, .cy = py,
          .radius = radius,
          .facing_angle = zone_facing,
          .arc_deg = arc_deg,
          .lifetime = linger_dur[linger_tier],
          .max_lifetime = linger_dur[linger_tier],
          .slow_mult = 1.0f,
          .is_linger = 1,
          .tick_timer = 0.0f,
          .active = 1
        };
        for (int ec = 0; ec < SCYTHE_ZONE_LINGER_MAX_TRACKED; ec++)
          scythe_zones[si].enemy_cd[ec] = 0.0f;
        break;
      }
    }
  }

  // Visual state
  scythe_sweep_timer = SCYTHE_SWEEP_DURATION;
  scythe_sweep_dir_x = fx;
  scythe_sweep_dir_y = fy;
  scythe_whirlwind_arc = is_whirlwind ? 360.0f : 0.0f;

  // Increment swing count
  scythe_swing_count++;

  // Sound (reuse spin swish)
  if (spin_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 80,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(&spin_sound, &opts);
  }
}

// ============================================================================
// Update
// ============================================================================

void weapons_update(float dt)
{
  // Wand shot window tracking (Bullet Hell achievement)
  wand_elapsed += dt;
  {
    // Expire old entries outside the 3-second window
    float cutoff = wand_elapsed - WAND_SHOT_WINDOW;
    int expired = 0;
    while (expired < wand_shot_log_count && wand_shot_log[expired] < cutoff)
      expired++;
    if (expired > 0) {
      for (int i = expired; i < wand_shot_log_count; i++)
        wand_shot_log[i - expired] = wand_shot_log[i];
      wand_shot_log_count -= expired;
    }
    achievements_notify_weapon_challenge(WEAPON_WAND, wand_shot_log_count);
  }

  // Chain kill window countdown
  if (chain_kill_window > 0.0f) {
    chain_kill_window -= dt;
    if (chain_kill_window <= 0.0f) {
      achievements_notify_weapon_challenge(WEAPON_CHAIN, chain_activation_kills);
      chain_kill_window = 0.0f;
    }
  }

  // Scythe sweep kill window countdown
  if (scythe_sweep_window > 0.0f) {
    scythe_sweep_window -= dt;
    if (scythe_sweep_window <= 0.0f) {
      // Window closed — final achievement check
      achievements_notify_weapon_challenge(WEAPON_SCYTHE, scythe_sweep_kills);
      scythe_sweep_window = 0.0f;
    }
  }

  // Wand barrage burst timer
  if (wand_burst_remaining > 0) {
    wand_burst_timer -= dt;
    if (wand_burst_timer <= 0.0f) {
      damage_source = WEAPON_WAND;
      fire_wand_single();
      wand_burst_remaining--;
      wand_burst_timer = 0.08f;
    }
  }

  if (spin_visual_timer > 0.0f) {
    spin_visual_timer -= dt;
  }

  // Vacuum visual fade
  if (vacuum_visual_timer > 0.0f) {
    vacuum_visual_timer -= dt;
  }

  // Vacuum tween: smoothly pull enemies inward over VACUUM_TWEEN_DURATION
  if (vacuum_tween_timer > 0.0f) {
    float prev_t = vacuum_tween_timer;
    vacuum_tween_timer -= dt;
    if (vacuum_tween_timer < 0.0f) vacuum_tween_timer = 0.0f;

    // How much progress this frame: old_progress -> new_progress (0→1)
    float old_pct = 1.0f - (prev_t / VACUUM_TWEEN_DURATION);
    float new_pct = 1.0f - (vacuum_tween_timer / VACUUM_TWEEN_DURATION);
    float delta_pct = new_pct - old_pct;

    for (int i = 0; i < vacuum_pull_count; i++) {
      VacuumPull_t* vp = &vacuum_pulls[i];
      if (vp->is_snake) {
        if (!snake_is_alive(vp->enemy_index)) continue;
        snake_displace(vp->enemy_index, vp->dx * delta_pct, vp->dy * delta_pct);
      } else {
        if (!enemy_is_alive(vp->enemy_index)) continue;
        enemy_displace(vp->enemy_index, vp->dx * delta_pct, vp->dy * delta_pct);
      }
    }
  }

  // Bomb implosion tween: pull enemies inward over IMPLOSION_TWEEN_DURATION
  if (implosion_tween_timer > 0.0f) {
    float prev_t = implosion_tween_timer;
    implosion_tween_timer -= dt;
    if (implosion_tween_timer < 0.0f) implosion_tween_timer = 0.0f;

    float old_pct = 1.0f - (prev_t / IMPLOSION_TWEEN_DURATION);
    float new_pct = 1.0f - (implosion_tween_timer / IMPLOSION_TWEEN_DURATION);
    float delta_pct = new_pct - old_pct;

    for (int i = 0; i < implosion_pull_count; i++) {
      VacuumPull_t* vp = &implosion_pulls[i];
      if (vp->is_snake) {
        if (!snake_is_alive(vp->enemy_index)) continue;
        snake_displace(vp->enemy_index, vp->dx * delta_pct, vp->dy * delta_pct);
      } else {
        if (!enemy_is_alive(vp->enemy_index)) continue;
        enemy_displace(vp->enemy_index, vp->dx * delta_pct, vp->dy * delta_pct);
      }
    }
  }
  if (implosion_visual_timer > 0.0f) {
    implosion_visual_timer -= dt;
  }

  // Aftershock delay countdown — finds target when it fires
  if (aftershock_pending) {
    aftershock_delay -= dt;
    if (aftershock_delay <= 0.0f) {
      aftershock_pending = 0;
      int as_tier = upgrades_get_tier(UPG_SPIN_AFTERSHOCK);
      float adx = 0.0f, ady = -1.0f;
      float apx = player_get_x(), apy = player_get_y();
      int atgt = find_nearest_enemy(apx, apy, &adx, &ady);
      if (atgt >= 0) {
        float alen = sqrtf(adx * adx + ady * ady);
        if (alen > 0.1f) { adx /= alen; ady /= alen; }
      }
      spawn_aftershock_wave(as_tier, adx, ady);
    }
  }

  // Update chain visual segment timers, compact expired ones
  {
    int write = 0;
    for (int i = 0; i < chain_segment_count; i++) {
      chain_segments[i].timer -= dt;
      if (chain_segments[i].timer > 0.0f) {
        if (write != i) chain_segments[write] = chain_segments[i];
        write++;
      }
    }
    chain_segment_count = write;
  }

  // Update chain propagation
  damage_source = WEAPON_CHAIN;
  chain_update(dt);

  // Update conductor chain propagation
  damage_source = SOURCE_CONDUCTOR;
  for (int c = 0; c < CONDUCTOR_CHAIN_MAX; c++) {
    ConductorChain_t* cc = &conductor_chains[c];
    if (!cc->active) continue;
    if (cc->current_jump >= cc->num_targets) {
      cc->active = 0;
      continue;
    }
    cc->propagation_timer -= dt;
    if (cc->propagation_timer <= 0.0f) {
      cc->propagation_timer += CHAIN_DELAY;
      int cur = cc->targets[cc->current_jump];
      int prev = cc->targets[cc->current_jump - 1];

      float px, py, pcx, pcy;
      enemy_get_position(prev, &px, &py);
      float pr = enemy_get_radius(prev);
      pcx = px + pr; pcy = py + pr;

      float cx, cy, ccx, ccy;
      enemy_get_position(cur, &cx, &cy);
      float cr = enemy_get_radius(cur);
      ccx = cx + cr; ccy = cy + cr;

      // Hit enemy
      float dx = ccx - pcx;
      float dy = ccy - pcy;
      float len = sqrtf(dx * dx + dy * dy);
      if (len > 0.1f) { dx /= len; dy /= len; }
      enemy_hit(cur, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);

      if (chain_sound_loaded) {
        aAudioOptions_t opts = {
          .channel = AUDIO_CHANNEL_AUTO,
          .volume = 55, .loops = 0, .fade_ms = 0, .interrupt = 0
        };
        a_AudioPlaySound(&chain_sound, &opts);
      }

      // Mini stun on conductor chain hit
      {
        int ms_tier = upgrades_get_tier(UPG_CHAIN_MINI_STUN);
        if (ms_tier > 0) {
          static const float stun_dur[4] = { 0.0f, 0.1f, 0.15f, 0.2f };
          enemy_set_stun(cur, stun_dur[ms_tier], 1.0f);
        }
      }

      // Add visual segment
      if (chain_segment_count < CHAIN_SEG_MAX) {
        chain_segments[chain_segment_count] = (ChainSegment_t){
          .x1 = pcx, .y1 = pcy,
          .x2 = ccx, .y2 = ccy,
          .timer = CHAIN_VISUAL_DURATION
        };
        chain_segment_count++;
      }

      // Snake head: check if nearby
      snake_check_hit_aoe(ccx, ccy, CONDUCTOR_CHAIN_RADIUS);

      cc->current_jump++;
      if (cc->current_jump >= cc->num_targets) {
        cc->active = 0;
      }
    }
  }

  // Update orbit
  damage_source = WEAPON_ORBIT;
  orbit_update(dt);
  ghost_orbit_update(dt);

  // Update bombs
  damage_source = WEAPON_BOMB;
  bomb_update(dt);

  // Update turrets
  damage_source = WEAPON_TURRET;
  turret_update(dt);

  // Update trail
  damage_source = WEAPON_TRAIL;
  trail_update(dt);

  // Update scythe
  damage_source = WEAPON_SCYTHE;

  // Update scythe arc zones (slow + linger)
  {
    int max_e = enemy_get_max_count();
    for (int zi = 0; zi < SCYTHE_ZONE_MAX; zi++) {
      if (!scythe_zones[zi].active) continue;
      scythe_zones[zi].lifetime -= dt;
      if (scythe_zones[zi].lifetime <= 0.0f) {
        scythe_zones[zi].active = 0;
        continue;
      }

      // Linger damage ticks (same mechanics as LingerZone_t)
      if (scythe_zones[zi].is_linger) {
        // Tick down per-enemy cooldowns
        for (int e = 0; e < max_e && e < SCYTHE_ZONE_LINGER_MAX_TRACKED; e++) {
          if (scythe_zones[zi].enemy_cd[e] > 0.0f)
            scythe_zones[zi].enemy_cd[e] -= dt;
        }

        scythe_zones[zi].tick_timer += dt;
        if (scythe_zones[zi].tick_timer >= LINGER_TICK_RATE) {
          scythe_zones[zi].tick_timer -= LINGER_TICK_RATE;
          float z_facing = scythe_zones[zi].facing_angle;
          float z_half = (scythe_zones[zi].arc_deg * (float)PI / 180.0f) / 2.0f;
          float z_start = z_facing - z_half;
          float z_end = z_facing + z_half;
          int linger_t3 = upgrades_get_tier(UPG_SCYTHE_LINGER_TRAIL) >= 3;

          damage_source = WEAPON_SCYTHE;

          for (int e = 0; e < max_e; e++) {
            if (!enemy_is_alive(e)) continue;
            if (e < SCYTHE_ZONE_LINGER_MAX_TRACKED && scythe_zones[zi].enemy_cd[e] > 0.0f) continue;

            float ex, ey;
            enemy_get_position(e, &ex, &ey);
            float er = enemy_get_radius(e);
            float ecx = ex + er, ecy = ey + er;

            float dx = ecx - scythe_zones[zi].cx;
            float dy = ecy - scythe_zones[zi].cy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > scythe_zones[zi].radius + er) continue;

            if (scythe_zones[zi].arc_deg < 360.0f) {
              float ea = atan2f(dy, dx);
              if (!angle_in_arc(ea, z_start, z_end)) continue;
            }

            enemy_hit(e, 0.0f, 0.0f);
            if (linger_t3) enemy_hit(e, 0.0f, 0.0f);  // T3 bonus hit
            if (e < SCYTHE_ZONE_LINGER_MAX_TRACKED)
              scythe_zones[zi].enemy_cd[e] = LINGER_ENEMY_COOLDOWN;
            float pdx = (dist > 1.0f) ? dx / dist : 0.0f;
            float pdy = (dist > 1.0f) ? dy / dist : 1.0f;
            fire_particles_spawn(ecx, ecy, pdx, pdy);
          }
          snake_check_hit_no_knockback(scythe_zones[zi].cx, scythe_zones[zi].cy, scythe_zones[zi].radius);
        }
      }
    }
  }

  // Sweep visual timer
  if (scythe_sweep_timer > 0.0f) {
    scythe_sweep_timer -= dt;
  }

  // Update lingering effects (mixed sources — attribute to current zone's origin)
  damage_source = WEAPON_NONE;
  linger_update(dt);

  // Update aftershock directional wave
  damage_source = WEAPON_SPIN;
  if (aftershock_wave.active) {
    aftershock_wave.head_dist += aftershock_wave.speed * dt;
    if (aftershock_wave.head_dist >= aftershock_wave.max_dist) {
      aftershock_wave.active = 0;
    } else {
      // Check enemies inside the beam rectangle
      // Beam extends from origin along dir, width = wave.width on each side
      // Perpendicular axis: (-dir_y, dir_x)
      float perp_x = -aftershock_wave.dir_y;
      float perp_y = aftershock_wave.dir_x;

      int max_e = enemy_get_max_count();
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;

        int already = 0;
        for (int h = 0; h < aftershock_wave.hit_count; h++) {
          if (aftershock_wave.hit_enemies[h] == e) { already = 1; break; }
        }
        if (already) continue;

        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er;
        float ecy = ey + er;

        // Project enemy position onto beam axis
        float rel_x = ecx - aftershock_wave.origin_x;
        float rel_y = ecy - aftershock_wave.origin_y;
        float along = rel_x * aftershock_wave.dir_x + rel_y * aftershock_wave.dir_y;
        float across = rel_x * perp_x + rel_y * perp_y;

        // Only hit enemies at the traveling wavefront (matches the visual)
        float tail = aftershock_wave.head_dist - 60.0f;
        if (tail < 0.0f) tail = 0.0f;
        if (along >= tail - er && along <= aftershock_wave.head_dist + er &&
            fabsf(across) <= aftershock_wave.width + er) {
          // Knockback along beam direction
          float kx = aftershock_wave.dir_x * 200.0f;
          float ky = aftershock_wave.dir_y * 200.0f;
          enemy_hit(e, kx, ky);
          if (aftershock_wave.bonus_hits > 0) {
            enemy_hit(e, 0.0f, 0.0f);
          }
          if (aftershock_wave.hit_count < AFTERSHOCK_MAX_HIT) {
            aftershock_wave.hit_enemies[aftershock_wave.hit_count++] = e;
          }
        }
      }
      snake_check_hit_aoe(aftershock_wave.origin_x, aftershock_wave.origin_y, aftershock_wave.head_dist);
    }
  }

  // Update shatter projectiles
  damage_source = WEAPON_ORBIT;
  {
    int max_e = enemy_get_max_count();
    for (int i = 0; i < SHATTER_MAX; i++) {
      if (!shatter_projs[i].active) continue;

      // Soft homing: steer toward nearest enemy
      {
        float best_dist = 250.0f;
        float tgt_x = 0, tgt_y = 0;
        int has_tgt = 0;
        for (int e = 0; e < max_e; e++) {
          if (!enemy_is_alive(e)) continue;
          float ex, ey;
          enemy_get_position(e, &ex, &ey);
          float er = enemy_get_radius(e);
          float ddx = (ex + er) - shatter_projs[i].x;
          float ddy = (ey + er) - shatter_projs[i].y;
          float d = sqrtf(ddx * ddx + ddy * ddy);
          if (d < best_dist) {
            best_dist = d;
            tgt_x = ex + er; tgt_y = ey + er;
            has_tgt = 1;
          }
        }
        if (has_tgt) {
          float to_x = tgt_x - shatter_projs[i].x;
          float to_y = tgt_y - shatter_projs[i].y;
          float len = sqrtf(to_x * to_x + to_y * to_y);
          if (len > 0.1f) {
            float steer = 550.0f * dt;
            shatter_projs[i].vx += (to_x / len) * steer;
            shatter_projs[i].vy += (to_y / len) * steer;
            float spd = sqrtf(shatter_projs[i].vx * shatter_projs[i].vx +
                              shatter_projs[i].vy * shatter_projs[i].vy);
            if (spd > 0.1f) {
              shatter_projs[i].vx = (shatter_projs[i].vx / spd) * 300.0f;
              shatter_projs[i].vy = (shatter_projs[i].vy / spd) * 300.0f;
            }
          }
        }
      }

      shatter_projs[i].x += shatter_projs[i].vx * dt;
      shatter_projs[i].y += shatter_projs[i].vy * dt;
      shatter_projs[i].lifetime -= dt;
      if (shatter_projs[i].lifetime <= 0.0f) {
        shatter_projs[i].active = 0;
        continue;
      }
      // Check enemy collisions
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        // Skip already-hit enemies
        int already = 0;
        for (int h = 0; h < shatter_projs[i].hit_count; h++) {
          if (shatter_projs[i].hit_enemies[h] == e) { already = 1; break; }
        }
        if (already) continue;

        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float dx = shatter_projs[i].x - (ex + er);
        float dy = shatter_projs[i].y - (ey + er);
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < er + 4.0f) {
          enemy_hit(e, shatter_projs[i].vx * 0.3f, shatter_projs[i].vy * 0.3f);
          if (shatter_projs[i].hit_count < 4) {
            shatter_projs[i].hit_enemies[shatter_projs[i].hit_count++] = e;
          }
          if (shatter_projs[i].pierce > 0) {
            shatter_projs[i].pierce--;
          } else {
            shatter_projs[i].active = 0;
            break;
          }
        }
      }
      snake_check_hit_aoe(shatter_projs[i].x, shatter_projs[i].y, 4.0f);
    }
  }

  // Update cluster mini-bombs
  damage_source = WEAPON_BOMB;
  {
    int max_e = enemy_get_max_count();
    int cluster_tier = upgrades_get_tier(UPG_BOMB_CLUSTER);
    static const float mini_radii[4] = { 25.0f, 25.0f, 35.0f, 45.0f };
    float mini_radius = mini_radii[cluster_tier];

    for (int i = 0; i < CLUSTER_MAX; i++) {
      if (!cluster_minis[i].active) continue;
      cluster_minis[i].x += cluster_minis[i].vx * dt;
      cluster_minis[i].y += cluster_minis[i].vy * dt;
      cluster_minis[i].fuse -= dt;
      if (cluster_minis[i].fuse <= 0.0f) {
        // Detonate mini-bomb
        float ix = cluster_minis[i].x;
        float iy = cluster_minis[i].y;
        cluster_minis[i].active = 0;

        for (int e = 0; e < max_e; e++) {
          if (!enemy_is_alive(e)) continue;
          float ex, ey;
          enemy_get_position(e, &ex, &ey);
          float er = enemy_get_radius(e);
          float dx = (ex + er) - ix;
          float dy = (ey + er) - iy;
          float dist = sqrtf(dx * dx + dy * dy);
          if (dist < mini_radius + er) {
            float len = (dist > 0.1f) ? dist : 1.0f;
            enemy_hit(e, (dx / len) * 120.0f, (dy / len) * 120.0f);
          }
        }
        snake_check_hit_aoe(ix, iy, mini_radius);

        // Mini explosion visual
        linger_spawn(ix, iy, mini_radius * 0.6f, 0.2f, (aColor_t){255, 160, 60, 200});
      }
    }
  }

  // Update napalm streaks (spreading fire lines — per-enemy hit cooldown)
  damage_source = WEAPON_BOMB;
  {
    int max_e = enemy_get_max_count();
    if (max_e > NAPALM_MAX_TRACKED) max_e = NAPALM_MAX_TRACKED;
    for (int i = 0; i < NAPALM_MAX; i++) {
      if (!napalm_zones[i].active) continue;
      napalm_zones[i].elapsed += dt;

      // Grow length
      if (napalm_zones[i].elapsed < napalm_zones[i].grow_time) {
        float t = napalm_zones[i].elapsed / napalm_zones[i].grow_time;
        napalm_zones[i].current_len = napalm_zones[i].max_len * (0.3f + 0.7f * t);
      } else {
        napalm_zones[i].current_len = napalm_zones[i].max_len;
      }

      // Burn time after grow phase
      float total_dur = napalm_zones[i].grow_time + napalm_zones[i].burn_time;
      if (napalm_zones[i].elapsed >= total_dur) {
        napalm_zones[i].active = 0;
        continue;
      }

      // Tick down per-enemy cooldowns
      for (int e = 0; e < max_e; e++) {
        if (napalm_zones[i].enemy_cd[e] > 0.0f)
          napalm_zones[i].enemy_cd[e] -= dt;
      }

      int extra_hits = 0;
      if (napalm_zones[i].tier >= 3) {
        float age_pct = napalm_zones[i].elapsed / total_dur;
        extra_hits = (age_pct > 0.5f) ? 1 : 0;
      }

      float nx = napalm_zones[i].x;
      float ny = napalm_zones[i].y;
      float half_len = napalm_zones[i].current_len;
      float half_w = NAPALM_WIDTH * 0.5f;
      float ca = cosf(napalm_zones[i].angle);
      float sa = sinf(napalm_zones[i].angle);

      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        if (napalm_zones[i].enemy_cd[e] > 0.0f) continue;
        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er;
        float ecy = ey + er;
        // Transform enemy position into streak-local space
        float dx = ecx - nx;
        float dy = ecy - ny;
        float along = dx * ca + dy * sa;    // projection along streak
        float perp = -dx * sa + dy * ca;    // perpendicular distance
        if (fabsf(along) < half_len + er && fabsf(perp) < half_w + er) {
          enemy_hit_no_knockback(e);
          for (int h = 0; h < extra_hits; h++) enemy_hit_no_knockback(e);
          napalm_zones[i].enemy_cd[e] = NAPALM_ENEMY_COOLDOWN;
          float pdist = sqrtf(dx * dx + dy * dy);
          float pdx = (pdist > 1.0f) ? dx / pdist : 0.0f;
          float pdy = (pdist > 1.0f) ? dy / pdist : 1.0f;
          fire_particles_spawn(ecx, ecy, pdx, pdy);
          game_audio_play_fire_hit();
        }
      }
      snake_check_hit_no_knockback(nx, ny, half_len);
    }
  }

  // Update crater zones (tick lifetime)
  for (int i = 0; i < CRATER_MAX; i++) {
    if (!crater_zones[i].active) continue;
    crater_zones[i].lifetime -= dt;
    if (crater_zones[i].lifetime <= 0.0f) {
      crater_zones[i].active = 0;
    }
  }

  for (int i = 0; i < weapon_count; i++) {
    if (slots[i].type == WEAPON_NONE) continue;

    slots[i].timer += dt;

    if (slots[i].timer >= get_slot_cooldown(i)) {
      damage_source = slots[i].type;
      switch (slots[i].type) {
        case WEAPON_WAND: fire_wand(); break;
        case WEAPON_SPIN: fire_spin(); break;
        case WEAPON_CHAIN: fire_chain(); break;
        case WEAPON_ORBIT: fire_orbit(); break;
        case WEAPON_BOMB: fire_bomb(); break;
        case WEAPON_TURRET: fire_turret(); break;
        case WEAPON_TRAIL: fire_trail(); break;
        case WEAPON_SCYTHE: fire_scythe(); break;
        default: break;
      }
      slots[i].timer = 0.0f;
      // Apply cooldown refunds after timer reset
      if (slots[i].type == WEAPON_SCYTHE && scythe_harvest_refund > 0.0f) {
        slots[i].timer += scythe_harvest_refund;
        scythe_harvest_refund = 0.0f;
      }
      if (slots[i].type == WEAPON_SPIN && spin_precision_refund > 0.0f) {
        slots[i].timer += spin_precision_refund;
        spin_precision_refund = 0.0f;
      }
    }
  }
  damage_source = WEAPON_NONE;
}

// ============================================================================
// Draw
// ============================================================================

void weapons_draw(void)
{
  // Draw lingering effect zones (below other weapon visuals)
  linger_draw();

  // Draw trail segments
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  {
    float tw = get_trail_width();
    int half_w = (int)(tw * 0.5f);
    int half_h = 4;  // 8px tall / 2

    for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
      if (!trail_segments[i].active) continue;
      float ratio = trail_segments[i].lifetime / trail_segments[i].max_lifetime;
      int r, g, b, a;

      if (ratio > 0.7f) {
        // Fresh: bright orange-red
        r = 255; g = 120; b = 30; a = 200;
      } else if (ratio > 0.3f) {
        // Middle: darker red-orange
        r = 200; g = 70; b = 20; a = 150;
      } else {
        // Fading: dark red, alpha fading to 0
        r = 140; g = 40; b = 10;
        a = (int)(150.0f * (ratio / 0.3f));
        if (a < 0) a = 0;
      }

      float sx = trail_segments[i].x;
      float sy = trail_segments[i].y;

      // Rotated quad along movement direction
      float dx = trail_segments[i].dir_x;
      float dy = trail_segments[i].dir_y;
      float dlen = sqrtf(dx * dx + dy * dy);
      if (dlen < 0.01f) { dx = 1.0f; dy = 0.0f; dlen = 1.0f; }
      float nx = dx / dlen;
      float ny = dy / dlen;
      // perpendicular
      float px = -ny;
      float py = nx;

      float hw = (float)half_w;
      float hh = (float)half_h;
      // 4 corners: along +-half_w in dir, +-half_h perpendicular
      float ax = sx + nx * hw + px * hh;
      float ay = sy + ny * hw + py * hh;
      float bx = sx + nx * hw - px * hh;
      float by = sy + ny * hw - py * hh;
      float cx = sx - nx * hw - px * hh;
      float cy = sy - ny * hw - py * hh;
      float ddx = sx - nx * hw + px * hh;
      float ddy = sy - ny * hw + py * hh;

      aColor_t seg_col = {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a};
      a_DrawFilledTriangle((int)ax, (int)ay, (int)bx, (int)by, (int)cx, (int)cy, seg_col);
      a_DrawFilledTriangle((int)ax, (int)ay, (int)cx, (int)cy, (int)ddx, (int)ddy, seg_col);

      // Bright core line along center
      if (ratio > 0.4f) {
        int core_a = (int)(120.0f * ratio);
        float core_hw = hw - 2.0f;
        if (core_hw < 1.0f) core_hw = 1.0f;
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app.renderer, 255, 220, 80, (uint8_t)core_a);
        SDL_RenderDrawLine(app.renderer,
          (int)(sx - nx * core_hw), (int)(sy - ny * core_hw),
          (int)(sx + nx * core_hw), (int)(sy + ny * core_hw));
      }
    }

    // Draw ember bursts
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < TRAIL_EMBER_MAX; i++) {
      if (!trail_embers[i].active) continue;
      float progress = 1.0f - (trail_embers[i].timer / 0.2f);
      float cur_r = trail_embers[i].radius * progress;
      int ea = (int)(180.0f * (1.0f - progress));
      if (ea < 0) ea = 0;

      int ecx = (int)trail_embers[i].x;
      int ecy = (int)trail_embers[i].y;
      int er = (int)cur_r;
      SDL_SetRenderDrawColor(app.renderer, 255, 140, 40, (uint8_t)ea);
      draw_filled_circle_scanline(ecx, ecy, er);
    }

    // Heat Mirage: pull radius ring + inward arrows per trail segment
    {
      int mirage_tier = upgrades_get_tier(UPG_TRAIL_HEAT_MIRAGE);
      if (mirage_tier > 0) {
        static const float pull_range_vis[4] = { 0.0f, 80.0f, 65.0f, 50.0f };
        float range = pull_range_vis[mirage_tier];
        int segs = 24;

        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
          if (!trail_segments[i].active) continue;
          if (i % 2 != 0) continue;  // Only even-index segments show pull visual
          float sx = trail_segments[i].x;
          float sy = trail_segments[i].y;
          float ratio = trail_segments[i].lifetime / trail_segments[i].max_lifetime;
          int base_a = (int)(60.0f * ratio);
          if (base_a < 8) continue;

          // Outer pull ring
          SDL_SetRenderDrawColor(app.renderer, 100, 200, 255, (uint8_t)(base_a / 2));
          for (int s = 0; s < segs; s++) {
            float a1 = (float)s / segs * 2.0f * (float)PI;
            float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
            SDL_RenderDrawLine(app.renderer,
              (int)(sx + cosf(a1) * range), (int)(sy + sinf(a1) * range),
              (int)(sx + cosf(a2) * range), (int)(sy + sinf(a2) * range));
          }

          // 4 inward arrows animating from outer ring toward center
          float phase = 1.0f - fmodf(trail_segments[i].lifetime * 3.0f, 1.0f);
          int num_arrows = 4;
          for (int a = 0; a < num_arrows; a++) {
            float angle = (float)a / num_arrows * 2.0f * (float)PI;
            float cos_a = cosf(angle);
            float sin_a = sinf(angle);
            float tip_r = range * (1.0f - phase);
            float tail_r = tip_r + range * 0.2f;
            if (tail_r > range) tail_r = range;

            SDL_SetRenderDrawColor(app.renderer, 150, 230, 255, (uint8_t)(base_a * 2 / 3));
            SDL_RenderDrawLine(app.renderer,
              (int)(sx + cos_a * tail_r), (int)(sy + sin_a * tail_r),
              (int)(sx + cos_a * tip_r), (int)(sy + sin_a * tip_r));
          }
        }
      }
    }

    // Draw inferno wake blasts (expanding orange circles)
    for (int i = 0; i < INFERNO_VISUAL_MAX; i++) {
      if (!inferno_blasts[i].active) continue;
      float progress = 1.0f - (inferno_blasts[i].timer / 0.2f);
      float cur_r = inferno_blasts[i].radius * progress;
      int ia = (int)(200.0f * (1.0f - progress));
      if (ia < 0) ia = 0;

      int icx = (int)inferno_blasts[i].x;
      int icy = (int)inferno_blasts[i].y;
      int ir = (int)cur_r;

      // Draw as ring (outline circle, not filled — faster for larger radii)
      SDL_SetRenderDrawColor(app.renderer, 255, 100, 20, (uint8_t)ia);
      int segs = 24;
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          icx + (int)(cosf(a1) * (float)ir), icy + (int)(sinf(a1) * (float)ir),
          icx + (int)(cosf(a2) * (float)ir), icy + (int)(sinf(a2) * (float)ir));
      }
      // Inner bright ring
      int ir2 = ir / 2;
      int ia2 = (int)(140.0f * (1.0f - progress));
      if (ia2 < 0) ia2 = 0;
      SDL_SetRenderDrawColor(app.renderer, 255, 180, 60, (uint8_t)ia2);
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          icx + (int)(cosf(a1) * (float)ir2), icy + (int)(sinf(a1) * (float)ir2),
          icx + (int)(cosf(a2) * (float)ir2), icy + (int)(sinf(a2) * (float)ir2));
      }
    }

    // Player glow while trail active
    if (trail_active) {
      float pulse = 0.7f + 0.3f * sinf(trail_active_timer * 6.0f);
      int glow_a = (int)(40.0f * pulse);
      int pcx = (int)player_get_x();
      int pcy = (int)player_get_y();
      int gr = 24;
      SDL_SetRenderDrawColor(app.renderer, 255, 150, 50, (uint8_t)glow_a);
      draw_filled_circle_scanline(pcx, pcy, gr);
    }
  }
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);

  // Draw vacuum donut visual
  if (vacuum_visual_timer > 0.0f) {
    float alpha_pct = vacuum_visual_timer / VACUUM_VISUAL_DURATION;
    int alpha = (int)(160.0f * alpha_pct);
    if (alpha > 255) alpha = 255;

    float cx = vacuum_visual_cx;
    float cy = vacuum_visual_cy;
    int segments = 48;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    // Outer ring (fading cyan)
    SDL_SetRenderDrawColor(app.renderer, 100, 200, 255, (uint8_t)alpha);
    for (int i = 0; i < segments; i++) {
      float a1 = (float)i / segments * 2.0f * (float)PI;
      float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cosf(a1) * vacuum_outer_r), (int)(cy + sinf(a1) * vacuum_outer_r),
        (int)(cx + cosf(a2) * vacuum_outer_r), (int)(cy + sinf(a2) * vacuum_outer_r));
    }

    // Inner ring (brighter cyan)
    SDL_SetRenderDrawColor(app.renderer, 150, 230, 255, (uint8_t)alpha);
    for (int i = 0; i < segments; i++) {
      float a1 = (float)i / segments * 2.0f * (float)PI;
      float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cosf(a1) * vacuum_inner_r), (int)(cy + sinf(a1) * vacuum_inner_r),
        (int)(cx + cosf(a2) * vacuum_inner_r), (int)(cy + sinf(a2) * vacuum_inner_r));
    }

    // Inward arrows: short lines from outer toward inner at 8 evenly spaced angles
    float shrink = 1.0f - alpha_pct; // arrows move inward as visual fades
    int num_arrows = 8;
    for (int i = 0; i < num_arrows; i++) {
      float angle = (float)i / num_arrows * 2.0f * (float)PI;
      float cos_a = cosf(angle);
      float sin_a = sinf(angle);

      // Arrow tip moves from outer toward inner over the visual lifetime
      float mid_r = vacuum_outer_r - (vacuum_outer_r - vacuum_inner_r) * shrink;
      float arrow_len = (vacuum_outer_r - vacuum_inner_r) * 0.3f;
      float tip_r = mid_r;
      float tail_r = tip_r + arrow_len;
      if (tail_r > vacuum_outer_r) tail_r = vacuum_outer_r;

      // Arrow shaft
      SDL_SetRenderDrawColor(app.renderer, 150, 230, 255, (uint8_t)(alpha * 3 / 4));
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cos_a * tail_r), (int)(cy + sin_a * tail_r),
        (int)(cx + cos_a * tip_r),  (int)(cy + sin_a * tip_r));

      // Arrow head (two angled lines from tip)
      float head_len = 5.0f;
      float spread = 0.4f;
      // The arrow points inward (toward center), so head points outward from tip
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cos_a * tip_r), (int)(cy + sin_a * tip_r),
        (int)(cx + cosf(angle + spread) * (tip_r + head_len)),
        (int)(cy + sinf(angle + spread) * (tip_r + head_len)));
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cos_a * tip_r), (int)(cy + sin_a * tip_r),
        (int)(cx + cosf(angle - spread) * (tip_r + head_len)),
        (int)(cy + sinf(angle - spread) * (tip_r + head_len)));
    }

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Draw bomb implosion visual (orange donut with inward arrows, like vacuum)
  if (implosion_visual_timer > 0.0f) {
    float alpha_pct = implosion_visual_timer / IMPLOSION_VISUAL_DURATION;
    int alpha = (int)(180.0f * alpha_pct);
    if (alpha > 255) alpha = 255;

    float cx = implosion_visual_cx;
    float cy = implosion_visual_cy;
    float outer_r = implosion_visual_radius;
    float inner_r = outer_r * 0.5f;
    int segments = 48;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    // Outer ring (orange)
    SDL_SetRenderDrawColor(app.renderer, 255, 140, 40, (uint8_t)alpha);
    for (int i = 0; i < segments; i++) {
      float a1 = (float)i / segments * 2.0f * (float)PI;
      float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cosf(a1) * outer_r), (int)(cy + sinf(a1) * outer_r),
        (int)(cx + cosf(a2) * outer_r), (int)(cy + sinf(a2) * outer_r));
    }

    // Inner ring (brighter orange)
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 80, (uint8_t)alpha);
    for (int i = 0; i < segments; i++) {
      float a1 = (float)i / segments * 2.0f * (float)PI;
      float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cosf(a1) * inner_r), (int)(cy + sinf(a1) * inner_r),
        (int)(cx + cosf(a2) * inner_r), (int)(cy + sinf(a2) * inner_r));
    }

    // Inward arrows at 8 evenly spaced angles
    float shrink = 1.0f - alpha_pct;
    int num_arrows = 8;
    for (int i = 0; i < num_arrows; i++) {
      float angle = (float)i / num_arrows * 2.0f * (float)PI;
      float cos_a = cosf(angle);
      float sin_a = sinf(angle);

      float mid_r = outer_r - (outer_r - inner_r) * shrink;
      float arrow_len = (outer_r - inner_r) * 0.3f;
      float tip_r = mid_r;
      float tail_r = tip_r + arrow_len;
      if (tail_r > outer_r) tail_r = outer_r;

      // Arrow shaft
      SDL_SetRenderDrawColor(app.renderer, 255, 180, 80, (uint8_t)(alpha * 3 / 4));
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cos_a * tail_r), (int)(cy + sin_a * tail_r),
        (int)(cx + cos_a * tip_r),  (int)(cy + sin_a * tip_r));

      // Arrow head
      float head_len = 5.0f;
      float spread = 0.4f;
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cos_a * tip_r), (int)(cy + sin_a * tip_r),
        (int)(cx + cosf(angle + spread) * (tip_r + head_len)),
        (int)(cy + sinf(angle + spread) * (tip_r + head_len)));
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cos_a * tip_r), (int)(cy + sin_a * tip_r),
        (int)(cx + cosf(angle - spread) * (tip_r + head_len)),
        (int)(cy + sinf(angle - spread) * (tip_r + head_len)));
    }

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Draw spin visual
  if (spin_visual_timer > 0.0f) {
    float alpha_pct = spin_visual_timer / SPIN_VISUAL_DURATION;
    int alpha = (int)(200.0f * alpha_pct);
    if (alpha > 255) alpha = 255;

    float cx = player_get_x();
    float cy = player_get_y();

    float ring_progress = 1.0f - alpha_pct;
    float r = get_spin_radius() * (0.5f + 0.5f * ring_progress);
    int segments = 32;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.renderer, 255, 200, 50, (uint8_t)alpha);

    for (int i = 0; i < segments; i++) {
      float a1 = (float)i / segments * 2.0f * (float)PI;
      float a2 = (float)(i + 1) / segments * 2.0f * (float)PI;
      int x1 = (int)(cx + cosf(a1) * r);
      int y1 = (int)(cy + sinf(a1) * r);
      int x2 = (int)(cx + cosf(a2) * r);
      int y2 = (int)(cy + sinf(a2) * r);
      SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
    }

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Draw aftershock directional wave
  if (aftershock_wave.active) {
    float progress = aftershock_wave.head_dist / aftershock_wave.max_dist;
    int alpha = (int)(200.0f * (1.0f - progress * 0.6f));
    if (alpha > 255) alpha = 255;
    if (alpha < 0) alpha = 0;

    float dx = aftershock_wave.dir_x;
    float dy = aftershock_wave.dir_y;
    float perp_x = -dy;
    float perp_y = dx;
    float w = aftershock_wave.width;
    float ox = aftershock_wave.origin_x;
    float oy = aftershock_wave.origin_y;
    float head = aftershock_wave.head_dist;

    // Beam trail fades behind the head
    float tail = head - 60.0f;
    if (tail < 0.0f) tail = 0.0f;

    // Four corners of the beam rectangle
    float x1 = ox + dx * tail + perp_x * w;
    float y1 = oy + dy * tail + perp_y * w;
    float x2 = ox + dx * tail - perp_x * w;
    float y2 = oy + dy * tail - perp_y * w;
    float x3 = ox + dx * head - perp_x * w;
    float y3 = oy + dy * head - perp_y * w;
    float x4 = ox + dx * head + perp_x * w;
    float y4 = oy + dy * head + perp_y * w;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    // Filled beam: draw horizontal lines between edges
    int steps = (int)(head - tail);
    if (steps < 1) steps = 1;
    if (steps > 80) steps = 80;
    for (int s = 0; s <= steps; s++) {
      float t = (float)s / (float)steps;
      float dist_along = tail + t * (head - tail);
      float fade = 1.0f - (head - dist_along) / 60.0f;
      if (fade < 0.2f) fade = 0.2f;
      if (fade > 1.0f) fade = 1.0f;
      int a = (int)((float)alpha * fade);

      float lx = ox + dx * dist_along + perp_x * w;
      float ly = oy + dy * dist_along + perp_y * w;
      float rx = ox + dx * dist_along - perp_x * w;
      float ry = oy + dy * dist_along - perp_y * w;

      SDL_SetRenderDrawColor(app.renderer, 255, 220, 80, (uint8_t)a);
      SDL_RenderDrawLine(app.renderer, (int)lx, (int)ly, (int)rx, (int)ry);
    }

    // Outline edges
    int edge_alpha = alpha * 3 / 4;
    SDL_SetRenderDrawColor(app.renderer, 255, 240, 120, (uint8_t)edge_alpha);
    SDL_RenderDrawLine(app.renderer, (int)x1, (int)y1, (int)x4, (int)y4);
    SDL_RenderDrawLine(app.renderer, (int)x2, (int)y2, (int)x3, (int)y3);
    // Head cap
    SDL_SetRenderDrawColor(app.renderer, 255, 255, 180, (uint8_t)alpha);
    SDL_RenderDrawLine(app.renderer, (int)x3, (int)y3, (int)x4, (int)y4);

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Draw chain lightning segments
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < chain_segment_count; i++) {
    if (chain_segments[i].timer <= 0.0f) continue;

    float alpha_pct = chain_segments[i].timer / CHAIN_VISUAL_DURATION;
    int alpha = (int)(alpha_pct * 255.0f);
    if (alpha > 255) alpha = 255;
    if (alpha < 0) alpha = 0;

    int x1 = (int)chain_segments[i].x1;
    int y1 = (int)chain_segments[i].y1;
    int x2 = (int)chain_segments[i].x2;
    int y2 = (int)chain_segments[i].y2;

    // White-blue core
    SDL_SetRenderDrawColor(app.renderer, 200, 220, 255, (uint8_t)alpha);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
    // Thicken: draw parallel lines offset by 1px
    SDL_RenderDrawLine(app.renderer, x1 - 1, y1, x2 - 1, y2);
    SDL_RenderDrawLine(app.renderer, x1 + 1, y1, x2 + 1, y2);
    SDL_RenderDrawLine(app.renderer, x1, y1 - 1, x2, y2 - 1);
    SDL_RenderDrawLine(app.renderer, x1, y1 + 1, x2, y2 + 1);
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);

  // Draw bomb flights and explosions
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    if (!bomb_flights[i].in_flight) continue;

    float p = bomb_flights[i].flight_progress;
    float cx = bomb_flights[i].start_x + (bomb_flights[i].target_x - bomb_flights[i].start_x) * p;
    float cy = bomb_flights[i].start_y + (bomb_flights[i].target_y - bomb_flights[i].start_y) * p;

    // Shadow on ground
    int sr = 6;
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 30, 80);
    draw_filled_circle_scanline((int)cx, (int)cy, sr);

    // Bomb dot — height curve: small at peak (progress=0.5), full at start/end
    float height_factor = sinf(p * (float)PI);
    float bomb_size = 6.0f * (1.0f - height_factor * 0.7f);
    int bomb_alpha = 255 - (int)(height_factor * 150.0f);
    if (bomb_alpha < 50) bomb_alpha = 50;

    int br = (int)(bomb_size / 2.0f);
    if (br < 1) br = 1;
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, (uint8_t)bomb_alpha);
    draw_filled_circle_scanline((int)cx, (int)cy, br);
  }

  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    if (!bomb_explosions[i].active) continue;

    float p = 1.0f - (bomb_explosions[i].timer / BOMB_VISUAL_DURATION);
    float current_radius = bomb_explosions[i].radius * (0.3f + 0.7f * p);
    int alpha = (int)(200.0f * (1.0f - p));
    if (alpha < 0) alpha = 0;

    int ex = (int)bomb_explosions[i].x;
    int ey = (int)bomb_explosions[i].y;
    int er = (int)current_radius;

    // Filled explosion circle
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 30, (uint8_t)alpha);
    draw_filled_circle_scanline(ex, ey, er);

    // Outer ring
    int ring_alpha = alpha > 180 ? 255 : alpha + 75;
    SDL_SetRenderDrawColor(app.renderer, 255, 200, 100, (uint8_t)ring_alpha);
    int segments = 32;
    for (int s = 0; s < segments; s++) {
      float a1 = (float)s / segments * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / segments * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        ex + (int)(cosf(a1) * current_radius),
        ey + (int)(sinf(a1) * current_radius),
        ex + (int)(cosf(a2) * current_radius),
        ey + (int)(sinf(a2) * current_radius));
    }
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);

  // Draw orbit orbs (with weave offset)
  if (orbit_state.is_active) {
    float px = player_get_x();
    float py = player_get_y();
    float radius = get_orbit_radius();
    int orb_count = get_orbit_orb_count();
    float growth = get_orbit_growth_factor();
    float orb_sz = ORBIT_ORB_SIZE * growth;

    for (int orb = 0; orb < orb_count; orb++) {
      float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
      float eff_r = radius + orbit_weave_offset[orb];
      float orb_x = px + cosf(orb_angle) * eff_r - orb_sz / 2.0f;
      float orb_y = py + sinf(orb_angle) * eff_r - orb_sz / 2.0f;

      a_DrawFilledRect(
        (aRectf_t){orb_x - 2, orb_y - 2, orb_sz + 4, orb_sz + 4},
        (aColor_t){255, 160, 50, 80}
      );
      a_DrawFilledRect(
        (aRectf_t){orb_x, orb_y, orb_sz, orb_sz},
        (aColor_t){255, 160, 50, 255}
      );
    }
  }

  // Draw ghost orbits (Afterglow)
  for (int g = 0; g < GHOST_ORBIT_MAX; g++) {
    if (!ghost_orbits[g].active) continue;

    float gcx = ghost_orbits[g].x;
    float gcy = ghost_orbits[g].y;
    int g_orb_count = ghost_orbits[g].orb_count;
    float g_radius = ghost_orbits[g].radius;
    float orb_sz = ORBIT_ORB_SIZE;

    float fade = ghost_orbits[g].timer / get_afterglow_duration();
    if (fade > 1.0f) fade = 1.0f;
    int base_alpha = (int)(180.0f * fade);

    for (int orb = 0; orb < g_orb_count; orb++) {
      float orb_angle = ghost_orbits[g].angle +
        (2.0f * (float)PI * orb) / g_orb_count;
      float eff_r = g_radius + ghost_orbits[g].weave_offsets[orb];
      float orb_x = gcx + cosf(orb_angle) * eff_r - orb_sz / 2.0f;
      float orb_y = gcy + sinf(orb_angle) * eff_r - orb_sz / 2.0f;

      a_DrawFilledRect(
        (aRectf_t){orb_x - 2, orb_y - 2, orb_sz + 4, orb_sz + 4},
        (aColor_t){200, 180, 100, (uint8_t)(base_alpha * 40 / 180)}
      );
      a_DrawFilledRect(
        (aRectf_t){orb_x, orb_y, orb_sz, orb_sz},
        (aColor_t){220, 180, 80, (uint8_t)base_alpha}
      );
    }

    // Faint orbit ring
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    int ring_alpha = (int)(30.0f * fade);
    SDL_SetRenderDrawColor(app.renderer, 220, 180, 80, (uint8_t)ring_alpha);
    int segs = 24;
    for (int s = 0; s < segs; s++) {
      float a1 = (float)s / segs * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        (int)(gcx + cosf(a1) * g_radius), (int)(gcy + sinf(a1) * g_radius),
        (int)(gcx + cosf(a2) * g_radius), (int)(gcy + sinf(a2) * g_radius));
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Draw shatter projectiles
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < SHATTER_MAX; i++) {
    if (!shatter_projs[i].active) continue;
    int alpha = (int)(220.0f * (shatter_projs[i].lifetime / 0.5f));
    if (alpha > 220) alpha = 220;
    if (alpha < 0) alpha = 0;
    a_DrawFilledRect(
      (aRectf_t){shatter_projs[i].x - 3, shatter_projs[i].y - 3, 6, 6},
      (aColor_t){255, 180, 60, (uint8_t)alpha}
    );
  }

  // Draw cluster mini-bombs (small orange dots in flight)
  for (int i = 0; i < CLUSTER_MAX; i++) {
    if (!cluster_minis[i].active) continue;
    a_DrawFilledRect(
      (aRectf_t){cluster_minis[i].x - 2, cluster_minis[i].y - 2, 5, 5},
      (aColor_t){255, 140, 40, 220}
    );
  }

  // Draw napalm streaks (trail-style fire visuals along bomb travel direction)
  for (int i = 0; i < NAPALM_MAX; i++) {
    if (!napalm_zones[i].active) continue;
    float total_dur = napalm_zones[i].grow_time + napalm_zones[i].burn_time;
    float ratio = 1.0f - (napalm_zones[i].elapsed / total_dur);
    float half_len = napalm_zones[i].current_len;
    float half_w = NAPALM_WIDTH * 0.5f;
    float ca = cosf(napalm_zones[i].angle);
    float sa = sinf(napalm_zones[i].angle);
    float nx = napalm_zones[i].x;
    float ny = napalm_zones[i].y;

    // Trail-style color phases
    int r, g, b, a;
    if (ratio > 0.7f) {
      r = 255; g = 120; b = 30; a = 200;
    } else if (ratio > 0.3f) {
      r = 200; g = 70; b = 20; a = 150;
    } else {
      r = 140; g = 40; b = 10;
      a = (int)(150.0f * (ratio / 0.3f));
      if (a < 0) a = 0;
    }

    // 4 corners of the streak rectangle
    float ax = nx + ca * half_len - sa * half_w;
    float ay = ny + sa * half_len + ca * half_w;
    float bx = nx + ca * half_len + sa * half_w;
    float by = ny + sa * half_len - ca * half_w;
    float cx_r = nx - ca * half_len + sa * half_w;
    float cy_r = ny - sa * half_len - ca * half_w;
    float ddx = nx - ca * half_len - sa * half_w;
    float ddy = ny - sa * half_len + ca * half_w;

    // Filled streak: two triangles
    a_DrawFilledTriangle(
      (int)ax, (int)ay, (int)bx, (int)by, (int)cx_r, (int)cy_r,
      (aColor_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a});
    a_DrawFilledTriangle(
      (int)bx, (int)by, (int)cx_r, (int)cy_r, (int)ddx, (int)ddy,
      (aColor_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a});

    // Bright ember core line down the center (like trail segments)
    if (ratio > 0.4f) {
      int core_a = (int)(120.0f * ratio);
      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.renderer, 255, 220, 80, (uint8_t)core_a);
      // Draw core line along the streak center
      SDL_RenderDrawLine(app.renderer,
        (int)(nx - ca * half_len * 0.9f), (int)(ny - sa * half_len * 0.9f),
        (int)(nx + ca * half_len * 0.9f), (int)(ny + sa * half_len * 0.9f));
      SDL_RenderDrawLine(app.renderer,
        (int)(nx - ca * half_len * 0.9f) + 1, (int)(ny - sa * half_len * 0.9f),
        (int)(nx + ca * half_len * 0.9f) + 1, (int)(ny + sa * half_len * 0.9f));
    }
  }

  // Draw crater zones
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < CRATER_MAX; i++) {
    if (!crater_zones[i].active) continue;

    float life_pct = crater_zones[i].lifetime / crater_zones[i].max_lifetime;
    // Fade out over full lifetime
    float fade = life_pct;
    if (fade > 1.0f) fade = 1.0f;

    int r = (int)crater_zones[i].radius;
    int cx = (int)crater_zones[i].x;
    int cy = (int)crater_zones[i].y;

    if (crater_zones[i].is_bomb_crater) {
      // Bomb crater: dark ground circle, subtle ring
      int ground_alpha = (int)(30.0f * fade);
      if (ground_alpha < 0) ground_alpha = 0;
      SDL_SetRenderDrawColor(app.renderer, 30, 25, 20, (uint8_t)ground_alpha);
      draw_filled_circle_scanline(cx, cy, r);

      // Thin ring at edge
      int ring_alpha = (int)(50.0f * fade);
      if (ring_alpha > 255) ring_alpha = 255;
      SDL_SetRenderDrawColor(app.renderer, 80, 70, 50, (uint8_t)ring_alpha);
      int segs = 24;
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          cx + (int)(cosf(a1) * (float)r), cy + (int)(sinf(a1) * (float)r),
          cx + (int)(cosf(a2) * (float)r), cy + (int)(sinf(a2) * (float)r));
      }
    } else {
      // Gravity well: blue-purple tint
      int ground_alpha = (int)(25.0f * fade);
      if (ground_alpha < 0) ground_alpha = 0;
      SDL_SetRenderDrawColor(app.renderer, 40, 30, 80, (uint8_t)ground_alpha);
      draw_filled_circle_scanline(cx, cy, r);

      // Subtle blue ring
      int ring_alpha = (int)(40.0f * fade);
      if (ring_alpha > 255) ring_alpha = 255;
      SDL_SetRenderDrawColor(app.renderer, 60, 50, 120, (uint8_t)ring_alpha);
      int segs = 16;
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          cx + (int)(cosf(a1) * (float)r), cy + (int)(sinf(a1) * (float)r),
          cx + (int)(cosf(a2) * (float)r), cy + (int)(sinf(a2) * (float)r));
      }
    }
  }

  // Draw turret/spin slow zones
  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
  for (int i = 0; i < TURRET_SLOW_MAX; i++) {
    if (!turret_slow_zones[i].active) continue;
    float za = 40.0f * (turret_slow_zones[i].lifetime / turret_slow_zones[i].max_lifetime);
    if (za > 40.0f) za = 40.0f;
    if (za < 0.0f) za = 0.0f;
    int zr = (int)turret_slow_zones[i].radius;
    SDL_SetRenderDrawColor(app.renderer, 100, 120, 160, (uint8_t)za);
    int zx = (int)turret_slow_zones[i].x;
    int zy = (int)turret_slow_zones[i].y;
    draw_filled_circle_scanline(zx, zy, zr);
  }

  // Draw turrets
  for (int i = 0; i < TURRET_MAX; i++) {
    if (!turrets[i].active) continue;
    float remaining = turrets[i].lifetime;

    // Fade in last 0.2s
    float alpha_mult = 1.0f;
    if (remaining < 0.2f) alpha_mult = remaining / 0.2f;

    // Blink in last 0.5s at ~8Hz
    if (remaining < 0.5f && remaining >= 0.2f) {
      if (((int)(remaining * 16.0f)) % 2 == 0) continue;
    }

    // Pulse 180-255 at 2Hz
    float pulse = 180.0f + 75.0f * sinf(remaining * 4.0f * (float)PI);
    int p = (int)(pulse * alpha_mult);
    if (p > 255) p = 255;
    if (p < 0) p = 0;

    int tx = (int)turrets[i].x;
    int ty = (int)turrets[i].y;

    // Dimmer pulse for minis
    if (turrets[i].is_mini) {
      p = p * 7 / 10;
    }

    // Faint range circle
    {
      float trange = turrets[i].is_mini
        ? get_turret_range() * MINI_TURRET_RANGE_MULT
        : get_turret_range();
      int ra = turrets[i].is_mini
        ? (int)(12.0f * alpha_mult)
        : (int)(20.0f * alpha_mult);
      if (ra > 0) {
        int slow_tier = upgrades_get_tier(UPG_TURRET_SLOW_ZONE);
        if (slow_tier > 0) {
          SDL_SetRenderDrawColor(app.renderer, 100, 160, 255, (uint8_t)(ra + 10));
        } else {
          SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, (uint8_t)ra);
        }
        int segs = turrets[i].is_mini ? 20 : 32;
        for (int s = 0; s < segs; s++) {
          float a1 = (float)s / segs * 2.0f * (float)PI;
          float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
          SDL_RenderDrawLine(app.renderer,
            tx + (int)(cosf(a1) * trange),
            ty + (int)(sinf(a1) * trange),
            tx + (int)(cosf(a2) * trange),
            ty + (int)(sinf(a2) * trange));
        }
      }
    }

    // Diamond body (smaller for minis)
    int sz = turrets[i].is_mini ? 3 : 5;
    // Dark fill
    SDL_SetRenderDrawColor(app.renderer, 180, 120, 30, (uint8_t)(p * 180 / 255));
    for (int dy = -sz; dy <= sz; dy++) {
      int hw = sz - abs(dy);
      SDL_RenderDrawLine(app.renderer, tx - hw, ty + dy, tx + hw, ty + dy);
    }
    // Orange-yellow outline
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, (uint8_t)p);
    SDL_RenderDrawLine(app.renderer, tx, ty - sz, tx + sz, ty);
    SDL_RenderDrawLine(app.renderer, tx + sz, ty, tx, ty + sz);
    SDL_RenderDrawLine(app.renderer, tx, ty + sz, tx - sz, ty);
    SDL_RenderDrawLine(app.renderer, tx - sz, ty, tx, ty - sz);
  }

  // Draw turret bullets (4x4 orange-yellow squares)
  for (int i = 0; i < TURRET_BULLET_MAX; i++) {
    if (!turret_bullets[i].active) continue;
    int bx = (int)turret_bullets[i].x;
    int by = (int)turret_bullets[i].y;
    // Glow (6x6)
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, 40);
    { SDL_Rect glow = { bx - 3, by - 3, 6, 6 }; SDL_RenderFillRect(app.renderer, &glow); }
    // Core (4x4)
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, 255);
    { SDL_Rect core = { bx - 2, by - 2, 4, 4 }; SDL_RenderFillRect(app.renderer, &core); }
  }

  // Draw turret vacuum pull arrows (inward arrows around range circle)
  {
    int vac_tier = upgrades_get_tier(UPG_TURRET_TESLA_COIL);
    if (vac_tier > 0) {
      float base_range = get_turret_range();
      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      for (int ti = 0; ti < TURRET_MAX; ti++) {
        if (!turrets[ti].active) continue;
        float trange = turrets[ti].is_mini ? base_range * MINI_TURRET_RANGE_MULT : base_range;
        float remaining = turrets[ti].lifetime;
        float alpha_mult = remaining < 1.0f ? remaining : 1.0f;
        int base_a = (int)(50.0f * alpha_mult);
        if (base_a < 5) continue;
        float tx = turrets[ti].x;
        float ty = turrets[ti].y;

        // 6 inward arrows animating from outer ring toward center
        float phase = 1.0f - fmodf(remaining * 2.0f, 1.0f);
        int num_arrows = 6;
        for (int a = 0; a < num_arrows; a++) {
          float angle = (float)a / num_arrows * 2.0f * (float)PI;
          float cos_a = cosf(angle);
          float sin_a = sinf(angle);
          float tip_r = trange * (1.0f - phase);
          float tail_r = tip_r + trange * 0.15f;
          if (tail_r > trange) tail_r = trange;

          SDL_SetRenderDrawColor(app.renderer, 150, 200, 255, (uint8_t)(base_a));
          SDL_RenderDrawLine(app.renderer,
            (int)(tx + cos_a * tail_r), (int)(ty + sin_a * tail_r),
            (int)(tx + cos_a * tip_r), (int)(ty + sin_a * tip_r));
        }
      }
    }
  }

  // Draw scythe arc zones (slow + linger)
  // Linger zones use same visual style as LingerZone_t (color fade + ring)
  // Slow zones use blue-grey with same alpha pattern
  for (int zi = 0; zi < SCYTHE_ZONE_MAX; zi++) {
    if (!scythe_zones[zi].active) continue;
    float fade = scythe_zones[zi].lifetime / scythe_zones[zi].max_lifetime;

    // Pick color: slow = blue-grey, linger = orange (matching other linger zones)
    aColor_t zc;
    if (scythe_zones[zi].is_linger) { zc = (aColor_t){255, 160, 50, 200}; }
    else { zc = (aColor_t){100, 120, 160, 200}; }

    // Same alpha calc as linger_draw(): fade * color.a * 0.4
    int za = (int)(fade * (float)zc.a * 0.4f);
    if (za < 0) za = 0;
    if (za > 255) za = 255;

    float zcx = scythe_zones[zi].cx;
    float zcy = scythe_zones[zi].cy;
    float zr = scythe_zones[zi].radius;
    float z_arc = scythe_zones[zi].arc_deg;
    float z_facing = scythe_zones[zi].facing_angle;
    float z_half = (z_arc * (float)PI / 180.0f) / 2.0f;
    float z_start = z_facing - z_half;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    if (z_arc >= 360.0f) {
      // Full circle (same as linger_draw)
      SDL_SetRenderDrawColor(app.renderer, zc.r, zc.g, zc.b, (uint8_t)za);
      draw_filled_circle_scanline((int)zcx, (int)zcy, (int)zr);
      // Outer ring (slightly brighter, same as linger_draw)
      int ring_alpha = za + 40;
      if (ring_alpha > 255) ring_alpha = 255;
      SDL_SetRenderDrawColor(app.renderer, zc.r, zc.g, zc.b, (uint8_t)ring_alpha);
      int segs = 24;
      for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 2.0f * (float)PI;
        float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
        SDL_RenderDrawLine(app.renderer,
          (int)(zcx + cosf(a1) * zr), (int)(zcy + sinf(a1) * zr),
          (int)(zcx + cosf(a2) * zr), (int)(zcy + sinf(a2) * zr));
      }
    } else {
      // Arc-shaped zone: filled triangle fan
      int segs = (int)(z_arc / 5.0f);
      if (segs < 8) segs = 8;
      if (segs > 48) segs = 48;
      float z_end = z_start + z_arc * (float)PI / 180.0f;
      for (int s = 0; s < segs; s++) {
        float a1 = z_start + (z_end - z_start) * (float)s / segs;
        float a2 = z_start + (z_end - z_start) * (float)(s + 1) / segs;
        aColor_t col = {zc.r, zc.g, zc.b, (uint8_t)za};
        a_DrawFilledTriangle((int)zcx, (int)zcy,
          (int)(zcx + cosf(a1) * zr), (int)(zcy + sinf(a1) * zr),
          (int)(zcx + cosf(a2) * zr), (int)(zcy + sinf(a2) * zr), col);
      }
      // Arc outline (slightly brighter, same as linger_draw ring)
      int ring_alpha = za + 40;
      if (ring_alpha > 255) ring_alpha = 255;
      SDL_SetRenderDrawColor(app.renderer, zc.r, zc.g, zc.b, (uint8_t)ring_alpha);
      for (int s = 0; s < segs; s++) {
        float a1 = z_start + (z_end - z_start) * (float)s / segs;
        float a2 = z_start + (z_end - z_start) * (float)(s + 1) / segs;
        SDL_RenderDrawLine(app.renderer,
          (int)(zcx + cosf(a1) * zr), (int)(zcy + sinf(a1) * zr),
          (int)(zcx + cosf(a2) * zr), (int)(zcy + sinf(a2) * zr));
      }
      // Edge lines
      SDL_RenderDrawLine(app.renderer, (int)zcx, (int)zcy,
        (int)(zcx + cosf(z_start) * zr), (int)(zcy + sinf(z_start) * zr));
      SDL_RenderDrawLine(app.renderer, (int)zcx, (int)zcy,
        (int)(zcx + cosf(z_end) * zr), (int)(zcy + sinf(z_end) * zr));
    }
  }

  // Draw scythe sweep arc
  if (scythe_sweep_timer > 0.0f) {
    float alpha_pct = scythe_sweep_timer / SCYTHE_SWEEP_DURATION;
    int alpha = (int)(180.0f * alpha_pct);

    float cx = player_get_x();
    float cy = player_get_y();
    float radius = get_scythe_radius();
    float arc_deg = scythe_whirlwind_arc > 0.0f ? 360.0f : get_scythe_arc();
    float facing = atan2f(scythe_sweep_dir_y, scythe_sweep_dir_x);
    float half_arc = (arc_deg * (float)PI / 180.0f) / 2.0f;
    float arc_start = facing - half_arc;

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    if (arc_deg >= 360.0f) {
      // Whirlwind: filled 360° sweep (same style as normal scythe arc but full circle)
      int segments = 48;
      float sweep_progress = 1.0f - alpha_pct;
      float draw_end = arc_start + 2.0f * (float)PI * sweep_progress;

      for (int i = 0; i < segments; i++) {
        float a1 = arc_start + (draw_end - arc_start) * (float)i / segments;
        float a2 = arc_start + (draw_end - arc_start) * (float)(i + 1) / segments;
        aColor_t col = {180, 255, 180, (uint8_t)(alpha / 2)};
        a_DrawFilledTriangle((int)cx, (int)cy,
          (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
          (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius), col);
      }

      // Bright edge at sweep front
      float edge_x = cx + cosf(draw_end) * radius;
      float edge_y = cy + sinf(draw_end) * radius;
      SDL_SetRenderDrawColor(app.renderer, 220, 255, 220, (uint8_t)alpha);
      SDL_RenderDrawLine(app.renderer, (int)cx, (int)cy, (int)edge_x, (int)edge_y);

      // Outer arc outline
      for (int i = 0; i < segments; i++) {
        float a1 = arc_start + (draw_end - arc_start) * (float)i / segments;
        float a2 = arc_start + (draw_end - arc_start) * (float)(i + 1) / segments;
        SDL_RenderDrawLine(app.renderer,
          (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
          (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius));
      }
    } else {
      // Animated arc sweep
      float sweep_progress = 1.0f - alpha_pct;
      float draw_end = arc_start + (arc_deg * (float)PI / 180.0f) * sweep_progress;

      int segments = (int)(arc_deg / 5.0f);
      if (segments < 8) segments = 8;
      if (segments > 48) segments = 48;

      // Filled arc triangles
      for (int i = 0; i < segments; i++) {
        float a1 = arc_start + (draw_end - arc_start) * (float)i / segments;
        float a2 = arc_start + (draw_end - arc_start) * (float)(i + 1) / segments;

        float x1 = cx + cosf(a1) * radius;
        float y1 = cy + sinf(a1) * radius;
        float x2 = cx + cosf(a2) * radius;
        float y2 = cy + sinf(a2) * radius;

        aColor_t col = {180, 255, 180, (uint8_t)(alpha / 2)};
        a_DrawFilledTriangle((int)cx, (int)cy, (int)x1, (int)y1, (int)x2, (int)y2, col);
      }

      // Bright edge line at sweep front
      float edge_x = cx + cosf(draw_end) * radius;
      float edge_y = cy + sinf(draw_end) * radius;
      SDL_SetRenderDrawColor(app.renderer, 220, 255, 220, (uint8_t)alpha);
      SDL_RenderDrawLine(app.renderer, (int)cx, (int)cy, (int)edge_x, (int)edge_y);

      // Outer arc outline
      for (int i = 0; i < segments; i++) {
        float a1 = arc_start + (draw_end - arc_start) * (float)i / segments;
        float a2 = arc_start + (draw_end - arc_start) * (float)(i + 1) / segments;
        SDL_RenderDrawLine(app.renderer,
          (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
          (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius));
      }
    }
  }

  SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
}

// ============================================================================
// Getters
// ============================================================================

const Weapon_t* weapons_get_slot(int slot)
{
  if (slot < 0 || slot >= weapon_count) return NULL;
  return &slots[slot];
}

float weapons_get_cooldown_progress(int slot)
{
  if (slot < 0 || slot >= weapon_count) return 1.0f;
  if (slots[slot].type == WEAPON_NONE) return 1.0f;
  float cd = get_slot_cooldown(slot);
  if (cd <= 0.0f) return 1.0f;
  return slots[slot].timer / cd;
}

float weapons_get_effective_cooldown(int slot)
{
  if (slot < 0 || slot >= weapon_count) return 0.0f;
  if (slots[slot].type == WEAPON_NONE) return 0.0f;
  return get_slot_cooldown(slot);
}

int weapons_get_count(void)
{
  return weapon_count;
}

int weapons_get_active_count(void)
{
  int n = 0;
  for (int i = 0; i < weapon_count; i++)
    if (slots[i].type != WEAPON_NONE && !slot_stolen[i]) n++;
  return n;
}

// ============================================================================
// Damage Tracking
// ============================================================================

void weapons_set_damage_source(WeaponType_t type)
{
  damage_source = type;
}

WeaponType_t weapons_get_damage_source(void)
{
  return damage_source;
}

void weapons_record_hit(void)
{
  if (damage_source > WEAPON_NONE && damage_source < WEAPON_TYPE_MAX) {
    if (weapon_total_hits[damage_source] == 0 && weapon_pickup_time[damage_source] == 0.0f) {
      weapon_pickup_time[damage_source] = director_get_elapsed();
    }
    weapon_total_hits[damage_source]++;
  }
}

int weapons_get_total_hits(WeaponType_t type)
{
  if (type <= WEAPON_NONE || type >= WEAPON_TYPE_MAX) return 0;
  return weapon_total_hits[type];
}

float weapons_get_dps(WeaponType_t type)
{
  if (type <= WEAPON_NONE || type >= WEAPON_TYPE_MAX) return 0.0f;
  float elapsed = director_get_elapsed() - weapon_pickup_time[type];
  if (elapsed < 1.0f) elapsed = 1.0f;
  return (float)weapon_total_hits[type] / elapsed;
}

float weapons_get_pickup_time(WeaponType_t type)
{
  if (type <= WEAPON_NONE || type >= WEAPON_TYPE_MAX) return 0.0f;
  return weapon_pickup_time[type];
}

const char* weapons_get_name(WeaponType_t type)
{
  switch (type) {
    case WEAPON_WAND:   return "WAND";
    case WEAPON_SPIN:   return "SPIN";
    case WEAPON_CHAIN:  return "CHAIN";
    case WEAPON_ORBIT:  return "ORBIT";
    case WEAPON_BOMB:   return "BOMB";
    case WEAPON_TURRET: return "TURRET";
    case WEAPON_TRAIL:          return "TRAIL";
    case WEAPON_SCYTHE:         return "SCYTHE";
    case SOURCE_FIRE_CONE:      return "FIRE CONE";
    case SOURCE_CONDUCTOR:      return "CONDUCTOR";
    case SOURCE_GRUNT_EXPLOSION: return "GRUNT BOOM";
    case SOURCE_SNAKE_POP:       return "SCALE SHATTER";
    default:                    return "";
  }
}

// ============================================================================
// Conductor Chain Lightning (fired on conductor debuff expiry)
// ============================================================================

void weapons_fire_conductor_chain(int source_enemy, int max_jumps)
{
  if (max_jumps <= 0) return;
  if (!enemy_is_alive(source_enemy)) return;

  // Find a free conductor chain slot
  int slot = -1;
  for (int i = 0; i < CONDUCTOR_CHAIN_MAX; i++) {
    if (!conductor_chains[i].active) { slot = i; break; }
  }
  if (slot < 0) return;

  ConductorChain_t* cc = &conductor_chains[slot];

  // Source enemy is the origin (not a target — chain jumps FROM it)
  float sx, sy;
  enemy_get_position(source_enemy, &sx, &sy);
  float sr = enemy_get_radius(source_enemy);
  float start_x = sx + sr;
  float start_y = sy + sr;

  // Build chain path: find nearest alive non-conductor enemies
  cc->targets[0] = source_enemy; // source is index 0 (already hit)
  cc->num_targets = 1;
  int max_e = enemy_get_max_count();

  int jumps_remaining = max_jumps;
  while (jumps_remaining > 0 && cc->num_targets < CHAIN_MAX_JUMPS) {
    int prev = cc->targets[cc->num_targets - 1];
    float prev_x, prev_y;
    enemy_get_position(prev, &prev_x, &prev_y);
    float prev_r = enemy_get_radius(prev);
    float cx = prev_x + prev_r;
    float cy = prev_y + prev_r;

    int best = -1;
    float best_dist = CONDUCTOR_CHAIN_RADIUS + 1.0f;

    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      if (enemy_is_conductor(e)) continue; // Skip enemies with active conductor

      // Check if already in this chain
      int already = 0;
      for (int k = 0; k < cc->num_targets; k++) {
        if (cc->targets[k] == e) { already = 1; break; }
      }
      if (already) continue;

      // Also check if targeted by another active conductor chain
      int targeted = 0;
      for (int c = 0; c < CONDUCTOR_CHAIN_MAX; c++) {
        if (!conductor_chains[c].active) continue;
        for (int k = 0; k < conductor_chains[c].num_targets; k++) {
          if (conductor_chains[c].targets[k] == e) { targeted = 1; break; }
        }
        if (targeted) break;
      }
      if (targeted) continue;

      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float ecx = ex + er;
      float ecy = ey + er;

      float dx = ecx - cx;
      float dy = ecy - cy;
      float d = sqrtf(dx * dx + dy * dy);

      if (d <= CONDUCTOR_CHAIN_RADIUS && d < best_dist) {
        best = e;
        best_dist = d;
      }
    }

    if (best < 0) break;
    cc->targets[cc->num_targets] = best;
    cc->num_targets++;
    jumps_remaining--;
  }

  // Need at least one target beyond the source
  if (cc->num_targets < 2) return;

  // Hit source enemy (it gets zapped too)
  enemy_hit(source_enemy, 0.0f, 0.0f);

  // Start propagation from jump 1 (source is 0)
  cc->current_jump = 1;
  cc->propagation_timer = CHAIN_DELAY;
  cc->active = 1;

  // Add visual segment from source to first target
  {
    int t = cc->targets[1];
    float tx, ty;
    enemy_get_position(t, &tx, &ty);
    float tr = enemy_get_radius(t);
    if (chain_segment_count < CHAIN_SEG_MAX) {
      chain_segments[chain_segment_count] = (ChainSegment_t){
        .x1 = start_x, .y1 = start_y,
        .x2 = tx + tr, .y2 = ty + tr,
        .timer = CHAIN_VISUAL_DURATION
      };
      chain_segment_count++;
    }
  }

  // Hit first target
  {
    int t = cc->targets[1];
    float tx, ty;
    enemy_get_position(t, &tx, &ty);
    float tr = enemy_get_radius(t);
    float tcx = tx + tr;
    float tcy = ty + tr;
    float dx = tcx - start_x;
    float dy = tcy - start_y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.1f) { dx /= len; dy /= len; }
    enemy_hit(t, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);
  }

  cc->current_jump = 2; // next jump to propagate

  // Play chain sound
  if (chain_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 60,
      .loops = 0,
      .fade_ms = 0,
      .interrupt = 0
    };
    a_AudioPlaySound(&chain_sound, &opts);
  }
}

void weapons_fire_conductor_chain_at(float x, float y, int max_jumps)
{
  if (max_jumps <= 0) return;

  // Find a free conductor chain slot
  int slot = -1;
  for (int i = 0; i < CONDUCTOR_CHAIN_MAX; i++) {
    if (!conductor_chains[i].active) { slot = i; break; }
  }
  if (slot < 0) return;

  ConductorChain_t* cc = &conductor_chains[slot];
  float start_x = x, start_y = y;

  // Find nearest enemy as first target
  int max_e = enemy_get_max_count();
  int first = -1;
  float first_dist = CONDUCTOR_CHAIN_RADIUS + 1.0f;
  for (int e = 0; e < max_e; e++) {
    if (!enemy_is_alive(e)) continue;
    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float dx = (ex + er) - x;
    float dy = (ey + er) - y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < first_dist) { first_dist = d; first = e; }
  }
  if (first < 0) return;

  cc->targets[0] = first;
  cc->num_targets = 1;

  // Build rest of chain path
  int jumps_remaining = max_jumps - 1;
  while (jumps_remaining > 0 && cc->num_targets < CHAIN_MAX_JUMPS) {
    int prev = cc->targets[cc->num_targets - 1];
    float prev_x, prev_y;
    enemy_get_position(prev, &prev_x, &prev_y);
    float prev_r = enemy_get_radius(prev);
    float cx = prev_x + prev_r;
    float cy = prev_y + prev_r;

    int best = -1;
    float best_dist = CONDUCTOR_CHAIN_RADIUS + 1.0f;
    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
      int already = 0;
      for (int k = 0; k < cc->num_targets; k++) {
        if (cc->targets[k] == e) { already = 1; break; }
      }
      if (already) continue;
      float ex, ey;
      enemy_get_position(e, &ex, &ey);
      float er = enemy_get_radius(e);
      float dx = (ex + er) - cx;
      float dy = (ey + er) - cy;
      float d = sqrtf(dx * dx + dy * dy);
      if (d <= CONDUCTOR_CHAIN_RADIUS && d < best_dist) { best = e; best_dist = d; }
    }
    if (best < 0) break;
    cc->targets[cc->num_targets] = best;
    cc->num_targets++;
    jumps_remaining--;
  }

  // Hit first target
  {
    int t = cc->targets[0];
    float tx, ty;
    enemy_get_position(t, &tx, &ty);
    float tr = enemy_get_radius(t);
    float tcx = tx + tr, tcy = ty + tr;
    float dx = tcx - start_x, dy = tcy - start_y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.1f) { dx /= len; dy /= len; }
    enemy_hit(t, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);

    if (chain_segment_count < CHAIN_SEG_MAX) {
      chain_segments[chain_segment_count] = (ChainSegment_t){
        .x1 = start_x, .y1 = start_y,
        .x2 = tcx, .y2 = tcy,
        .timer = CHAIN_VISUAL_DURATION
      };
      chain_segment_count++;
    }
  }

  cc->current_jump = 1;
  cc->propagation_timer = CHAIN_DELAY;
  cc->active = 1;

  if (chain_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 60, .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(&chain_sound, &opts);
  }
}

int weapons_is_in_crater(float x, float y, float* out_damage_mult)
{
  for (int i = 0; i < CRATER_MAX; i++) {
    if (!crater_zones[i].active) continue;
    float dx = x - crater_zones[i].x;
    float dy = y - crater_zones[i].y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < crater_zones[i].radius) {
      if (out_damage_mult) *out_damage_mult = crater_zones[i].damage_mult;
      return 1;
    }
  }
  return 0;
}

// ============================================================================
// Turret public API
// ============================================================================

int weapons_get_turret_bullet_count(void)
{
  return TURRET_BULLET_MAX;
}

void weapons_get_turret_bullet(int index, float* x, float* y, float* r)
{
  if (index < 0 || index >= TURRET_BULLET_MAX || !turret_bullets[index].active) {
    *x = 0; *y = 0; *r = 0;
    return;
  }
  *x = turret_bullets[index].x;
  *y = turret_bullets[index].y;
  *r = (float)TURRET_BULLET_SIZE;
}

void weapons_deactivate_turret_bullet(int index)
{
  if (index >= 0 && index < TURRET_BULLET_MAX)
    turret_bullets[index].active = 0;
}

void weapons_spawn_turret_slow_zone(float x, float y)
{
  int tier = upgrades_get_tier(UPG_TURRET_SLOW_ZONE);
  if (tier <= 0) return;
  static const float slow_vals[4] = { 1.0f, 0.70f, 0.60f, 0.50f };
  static const float durations[4] = { 0.0f, 1.8f, 2.4f, 3.6f };
  static const float radii[4]     = { 0.0f, 24.0f, 30.0f, 36.0f };

  for (int i = 0; i < TURRET_SLOW_MAX; i++) {
    if (!turret_slow_zones[i].active) {
      turret_slow_zones[i] = (TurretSlowZone_t){
        .x = x, .y = y,
        .radius = radii[tier],
        .lifetime = durations[tier],
        .max_lifetime = durations[tier],
        .slow_mult = slow_vals[tier],
        .active = 1
      };
      return;
    }
  }
}

int weapons_get_turret_slow(float x, float y, float* out_mult)
{
  // Turret Slow Zone upgrade: entire turret range is a slow field
  int turret_slow_tier = upgrades_get_tier(UPG_TURRET_SLOW_ZONE);
  if (turret_slow_tier > 0) {
    static const float slow_vals[4] = { 1.0f, 0.90f, 0.85f, 0.80f };
    float base_range = get_turret_range();
    for (int i = 0; i < TURRET_MAX; i++) {
      if (!turrets[i].active) continue;
      float range = turrets[i].is_mini ? base_range * MINI_TURRET_RANGE_MULT : base_range;
      float dx = x - turrets[i].x;
      float dy = y - turrets[i].y;
      if (sqrtf(dx * dx + dy * dy) < range) {
        if (out_mult) *out_mult = slow_vals[turret_slow_tier];
        return 1;
      }
    }
  }

  // Spin slow zones (still use the zone array)
  for (int i = 0; i < TURRET_SLOW_MAX; i++) {
    if (!turret_slow_zones[i].active) continue;
    float dx = x - turret_slow_zones[i].x;
    float dy = y - turret_slow_zones[i].y;
    if (sqrtf(dx * dx + dy * dy) < turret_slow_zones[i].radius) {
      if (out_mult) *out_mult = turret_slow_zones[i].slow_mult;
      return 1;
    }
  }

  // Scythe arc-shaped slow zones
  for (int i = 0; i < SCYTHE_ZONE_MAX; i++) {
    if (!scythe_zones[i].active) continue;
    if (scythe_zones[i].slow_mult >= 1.0f) continue;  // not a slow zone
    float dx = x - scythe_zones[i].cx;
    float dy = y - scythe_zones[i].cy;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > scythe_zones[i].radius) continue;
    if (scythe_zones[i].arc_deg < 360.0f) {
      float ea = atan2f(dy, dx);
      float z_half = (scythe_zones[i].arc_deg * (float)PI / 180.0f) / 2.0f;
      float z_start = scythe_zones[i].facing_angle - z_half;
      float z_end = scythe_zones[i].facing_angle + z_half;
      if (!angle_in_arc(ea, z_start, z_end)) continue;
    }
    if (out_mult) *out_mult = scythe_zones[i].slow_mult;
    return 1;
  }
  return 0;
}

int weapons_get_nearest_turret(float x, float y, float radius, float* out_x, float* out_y)
{
  float best_d2 = radius * radius;
  int found = 0;
  for (int i = 0; i < TURRET_MAX; i++) {
    if (!turrets[i].active) continue;
    float dx = turrets[i].x - x;
    float dy = turrets[i].y - y;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      if (out_x) *out_x = turrets[i].x;
      if (out_y) *out_y = turrets[i].y;
      found = 1;
    }
  }
  return found;
}

// ============================================================================
// Trail public API
// ============================================================================

int weapons_is_trail_active(void)
{
  return trail_active;
}

float weapons_get_trail_speed_mult(void)
{
  if (!trail_active) return 1.0f;
  int tier = upgrades_get_tier(UPG_TRAIL_BLAZING_SPEED);
  static const float mult[4] = { 1.0f, 1.15f, 1.25f, 1.40f };
  return mult[tier];
}

float weapons_get_trail_slow(float x, float y)
{
  int tier = upgrades_get_tier(UPG_TRAIL_SCORCHED_EARTH);
  if (tier <= 0) return 1.0f;
  static const float slow[4] = { 1.0f, 0.8f, 0.7f, 0.6f };
  float half_w = get_trail_width() * 0.5f;
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) continue;
    float dx = x - trail_segments[i].x;
    float dy = y - trail_segments[i].y;
    if (sqrtf(dx * dx + dy * dy) < half_w + 8.0f) {
      return slow[tier];
    }
  }
  return 1.0f;
}

float weapons_get_scythe_harvest_speed(void)
{
  return 1.0f;
}

int weapons_is_in_linger_zone(float x, float y)
{
  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) continue;
    float dx = x - linger_zones[i].x;
    float dy = y - linger_zones[i].y;
    if (sqrtf(dx * dx + dy * dy) < linger_zones[i].radius) return 1;
  }
  return 0;
}

int weapons_is_in_trail(float x, float y)
{
  float half_w = get_trail_width() * 0.5f + 8.0f;
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) continue;
    float dx = x - trail_segments[i].x;
    float dy = y - trail_segments[i].y;
    if (sqrtf(dx * dx + dy * dy) < half_w) return 1;
  }
  return 0;
}

int weapons_get_nearest_hazard(float x, float y, float radius, float* out_x, float* out_y)
{
  float best_d2 = radius * radius;
  int found = 0;
  // Check linger zones
  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) continue;
    float dx = linger_zones[i].x - x;
    float dy = linger_zones[i].y - y;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      if (out_x) *out_x = linger_zones[i].x;
      if (out_y) *out_y = linger_zones[i].y;
      found = 1;
    }
  }
  // Check trail segments
  for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
    if (!trail_segments[i].active) continue;
    float dx = trail_segments[i].x - x;
    float dy = trail_segments[i].y - y;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      if (out_x) *out_x = trail_segments[i].x;
      if (out_y) *out_y = trail_segments[i].y;
      found = 1;
    }
  }
  // Check napalm zones
  for (int i = 0; i < NAPALM_MAX; i++) {
    if (!napalm_zones[i].active) continue;
    float dx = napalm_zones[i].x - x;
    float dy = napalm_zones[i].y - y;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      if (out_x) *out_x = napalm_zones[i].x;
      if (out_y) *out_y = napalm_zones[i].y;
      found = 1;
    }
  }
  // Check crater zones
  for (int i = 0; i < CRATER_MAX; i++) {
    if (!crater_zones[i].active) continue;
    float dx = crater_zones[i].x - x;
    float dy = crater_zones[i].y - y;
    float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      if (out_x) *out_x = crater_zones[i].x;
      if (out_y) *out_y = crater_zones[i].y;
      found = 1;
    }
  }
  return found;
}

// ============================================================================
// Mimic Weapon Steal/Restore
// ============================================================================

int weapons_disable_slot(int slot)
{
  if (slot < 0 || slot >= weapon_count) return 0;
  if (slots[slot].type == WEAPON_NONE) return 0;

  stolen_weapons[slot] = slots[slot].type;
  slot_stolen[slot] = 1;
  slots[slot].type = WEAPON_NONE;
  slots[slot].label = "";
  slots[slot].timer = 0.0f;
  return 1;
}

static int restore_suppress_achievements = 0;

void weapons_restore_slot(int slot, WeaponType_t type)
{
  if (slot < 0 || slot >= weapon_count) return;
  if (!slot_stolen[slot]) return;

  switch (type) {
    case WEAPON_WAND:   slots[slot] = (Weapon_t){.type = type, .label = "W", .cooldown = WAND_COOLDOWN, .timer = WAND_COOLDOWN};   break;
    case WEAPON_SPIN:   slots[slot] = (Weapon_t){.type = type, .label = "S", .cooldown = SPIN_COOLDOWN, .timer = SPIN_COOLDOWN};   break;
    case WEAPON_CHAIN:  slots[slot] = (Weapon_t){.type = type, .label = "L", .cooldown = CHAIN_COOLDOWN, .timer = CHAIN_COOLDOWN}; break;
    case WEAPON_ORBIT:  slots[slot] = (Weapon_t){.type = type, .label = "O", .cooldown = ORBIT_COOLDOWN, .timer = ORBIT_COOLDOWN}; break;
    case WEAPON_BOMB:   slots[slot] = (Weapon_t){.type = type, .label = "B", .cooldown = BOMB_COOLDOWN, .timer = BOMB_COOLDOWN};   break;
    case WEAPON_TURRET: slots[slot] = (Weapon_t){.type = type, .label = "T", .cooldown = TURRET_COOLDOWN, .timer = TURRET_COOLDOWN}; break;
    case WEAPON_TRAIL:   slots[slot] = (Weapon_t){.type = type, .label = "F", .cooldown = TRAIL_COOLDOWN, .timer = TRAIL_COOLDOWN}; break;
    case WEAPON_SCYTHE:  slots[slot] = (Weapon_t){.type = type, .label = "X", .cooldown = SCYTHE_COOLDOWN, .timer = SCYTHE_COOLDOWN}; break;
    default: return;
  }
  stolen_weapons[slot] = WEAPON_NONE;
  slot_stolen[slot] = 0;

  if (!restore_suppress_achievements)
    achievements_notify_weapon_retrieve(type);
}

void weapons_restore_all_stolen(void)
{
  restore_suppress_achievements = 1;
  for (int i = 0; i < WEAPON_MAX_SLOTS; i++) {
    if (slot_stolen[i] && stolen_weapons[i] != WEAPON_NONE) {
      weapons_restore_slot(i, stolen_weapons[i]);
    }
  }
  restore_suppress_achievements = 0;
}

int weapons_is_slot_stolen(int slot)
{
  if (slot < 0 || slot >= WEAPON_MAX_SLOTS) return 0;
  return slot_stolen[slot];
}

int weapons_is_type_stolen(WeaponType_t type)
{
  for (int i = 0; i < WEAPON_MAX_SLOTS; i++) {
    if (slot_stolen[i] && stolen_weapons[i] == type) return 1;
  }
  return 0;
}

const char* weapons_get_stolen_label(int slot)
{
  if (slot < 0 || slot >= WEAPON_MAX_SLOTS || !slot_stolen[slot]) return "";
  switch (stolen_weapons[slot]) {
    case WEAPON_WAND:   return "W";
    case WEAPON_SPIN:   return "S";
    case WEAPON_CHAIN:  return "L";
    case WEAPON_ORBIT:  return "O";
    case WEAPON_BOMB:   return "B";
    case WEAPON_TURRET: return "T";
    case WEAPON_TRAIL:   return "F";
    case WEAPON_SCYTHE:  return "X";
    default:             return "?";
  }
}

// ============================================================================
// Achievement kill tracking (called from collision_resolve_deaths)
// ============================================================================

void weapons_notify_kill(WeaponType_t source)
{
  switch (source)
  {
    case WEAPON_CHAIN:
      if (chain_state.active || chain_kill_window > 0.0f)
        chain_activation_kills++;
      break;
    case WEAPON_ORBIT:
      // Only count kills while orbit is truly active (not ghost orbits/afterglow)
      if (orbit_state.is_active) {
        orbit_activation_kills++;
        achievements_notify_weapon_challenge(WEAPON_ORBIT, orbit_activation_kills);
      }
      break;
    case WEAPON_SCYTHE:
      if (scythe_sweep_window > 0.0f) {
        scythe_sweep_kills++;
        achievements_notify_weapon_challenge(WEAPON_SCYTHE, scythe_sweep_kills);
      }
      break;
    default:
      break;
  }
}

float weapons_get_slot_cooldown(int slot)
{
  if (slot < 0 || slot >= weapon_count) return 1.0f;
  return get_slot_cooldown(slot);
}

float weapons_get_spin_radius(void)
{
  return get_spin_radius();
}

float weapons_get_bomb_blast_radius(void)
{
  return get_bomb_blast_radius();
}

float weapons_get_bomb_flight_time(void)
{
  return get_bomb_flight_time();
}

float weapons_get_chain_radius(void)
{
  return get_chain_radius();
}

float weapons_get_turret_duration(void)  { return get_turret_duration(); }
float weapons_get_turret_fire_rate(void) { return get_turret_fire_rate(); }
float weapons_get_turret_range(void)     { return get_turret_range(); }
int   weapons_get_turret_spread(void)    { return get_turret_spread(); }
float weapons_get_trail_duration(void)   { return get_trail_duration(); }
float weapons_get_trail_persist(void)    { return get_trail_persist(); }
float weapons_get_trail_width(void)      { return get_trail_width(); }
int   weapons_get_orbit_orb_count(void)  { return get_orbit_orb_count(); }
float weapons_get_orbit_duration(void)   { return get_orbit_duration(); }
float weapons_get_orbit_radius(void)     { return get_orbit_radius(); }
float weapons_get_orbit_speed_mult(void) { return get_orbit_speed_mult(); }
float weapons_get_scythe_radius(void)    { return get_scythe_radius(); }
float weapons_get_scythe_arc(void)       { return get_scythe_arc(); }
float weapons_get_scythe_knockback(void) { return get_scythe_knockback(); }
