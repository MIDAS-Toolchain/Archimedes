#include <math.h>
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

// ============================================================================
// Weapon Definitions
// ============================================================================

#define WAND_COOLDOWN  1.0f
#define SPIN_COOLDOWN  2.0f
#define SPIN_RADIUS    101.0f
#define SPIN_VISUAL_DURATION 0.15f

// ============================================================================
// State
// ============================================================================

static Weapon_t slots[WEAPON_MAX_SLOTS];
static int weapon_count = 0;

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

// Spin double-pulse state
static int spin_extra_pulses = 0;       // Remaining extra pulses to fire
static float spin_pulse_timer = 0.0f;   // Countdown to next extra pulse

// Lingering effects system (shared by rare upgrades)
#define LINGER_MAX_ZONES 24
#define LINGER_TICK_RATE 0.75f

typedef struct {
  float x, y, radius, lifetime, max_lifetime, tick_timer;
  int active;
  aColor_t color;
  WeaponType_t source;
} LingerZone_t;

static LingerZone_t linger_zones[LINGER_MAX_ZONES];

// Orbit linger trail timer
static float orbit_trail_timer = 0.0f;
// Gravity Well slow trail timer
static float gw_trail_timer = 0.0f;

// Damage tracking
#define WEAPON_TYPE_MAX (SOURCE_GRUNT_EXPLOSION + 1)
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
#define SHATTER_MAX 32
typedef struct {
  float x, y, vx, vy, lifetime;
  int pierce, active;
  int hit_enemies[4];
  int hit_count;
} ShatterProj_t;
static ShatterProj_t shatter_projs[SHATTER_MAX];

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
typedef struct {
  float x, y;           // center of the streak
  float angle;          // direction of the streak
  float current_len, max_len;  // half-length (extends both directions from center)
  float grow_time, elapsed;
  float burn_time;
  float tick_timer;
  int tier;
  int active;
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
  float start_x, start_y;   // original position offset (dx, dy to displace)
  float dx, dy;              // total displacement to apply
} VacuumPull_t;

static float vacuum_visual_timer = 0.0f;
static float vacuum_visual_cx, vacuum_visual_cy;
static float vacuum_inner_r, vacuum_outer_r;
static VacuumPull_t vacuum_pulls[VACUUM_MAX_PULLS];
static int vacuum_pull_count = 0;
static float vacuum_tween_timer = 0.0f;

// Magnetic pull tween state (chain lightning upgrade)
#define MAGNETIC_TWEEN_DURATION 0.15f
#define MAGNETIC_MAX_PULLS 40
typedef struct {
  int enemy_index;
  float dx, dy;  // total displacement to apply
} MagneticPull_t;
static MagneticPull_t magnetic_pulls[MAGNETIC_MAX_PULLS];
static int magnetic_pull_count = 0;
static float magnetic_tween_timer = 0.0f;

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
    case WEAPON_TRAIL:  return WID_TRAIL;
    default:            return WID_COUNT;
  }
}

static float get_slot_cooldown(int slot)
{
  float base = slots[slot].cooldown;
  UpgradeId_t upg;

  switch (slots[slot].type) {
    case WEAPON_WAND:  upg = UPG_WAND_COOLDOWN;  break;
    case WEAPON_SPIN:  upg = UPG_SPIN_COOLDOWN;   break;
    case WEAPON_CHAIN: upg = UPG_CHAIN_COOLDOWN;  break;
    case WEAPON_ORBIT: break; // Orbit has no cooldown upgrade — uses duration instead
    case WEAPON_BOMB:  upg = UPG_BOMB_COOLDOWN;   break;
    case WEAPON_TURRET: upg = UPG_TURRET_COOLDOWN; break;
    case WEAPON_TRAIL:  upg = UPG_TRAIL_COOLDOWN;  break;
    default: return base;
  }

  WeaponId_t wid = wtype_to_wid(slots[slot].type);

  if (slots[slot].type == WEAPON_ORBIT)
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
  return d[upgrades_get_tier(UPG_TURRET_DURATION)];
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
      return;
    }
  }
}

static void linger_update(float dt)
{
  int max_e = enemy_get_max_count();

  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) continue;

    linger_zones[i].lifetime -= dt;
    if (linger_zones[i].lifetime <= 0.0f) {
      linger_zones[i].active = 0;
      continue;
    }

    linger_zones[i].tick_timer += dt;
    if (linger_zones[i].tick_timer >= LINGER_TICK_RATE) {
      linger_zones[i].tick_timer -= LINGER_TICK_RATE;

      // Set damage source to the weapon that spawned this zone
      damage_source = linger_zones[i].source;

      // Damage enemies in radius
      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;

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
          // Fire particles on linger damage — direction outward from zone center
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

  // Init bomb state
  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    bomb_flights[i].in_flight = 0;
    bomb_explosions[i].active = 0;
  }

  // Init linger zones
  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    linger_zones[i].active = 0;
  }
  spin_extra_pulses = 0;
  orbit_trail_timer = 0.0f;
  gw_trail_timer = 0.0f;
  aftershock_wave.active = 0;
  aftershock_pending = 0;
  vacuum_visual_timer = 0.0f;
  vacuum_pull_count = 0;
  vacuum_tween_timer = 0.0f;
  magnetic_pull_count = 0;
  magnetic_tween_timer = 0.0f;
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
  for (int i = 0; i < TURRET_MAX; i++) turrets[i].active = 0;
  for (int i = 0; i < TURRET_BULLET_MAX; i++) turret_bullets[i].active = 0;
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
}

static void fire_wand(void)
{
  fire_wand_single();

  // Barrage: check if this shot triggers a burst
  int tier = upgrades_get_tier(UPG_WAND_BARRAGE);
  if (tier <= 0) return;

  wand_shot_count++;
  static const int every_n[4] = { 0, 3, 2, 1 };
  static const int burst[4]   = { 0, 3, 3, 5 };
  if (wand_shot_count % every_n[tier] == 0) {
    wand_burst_remaining = burst[tier] - 1; // -1 because first shot already fired
    wand_burst_timer = 0.05f;
  }
}

static void spawn_aftershock_wave(int tier, float tdx, float tdy)
{
  static const float as_range[4] = { 0.0f, 150.0f, 220.0f, 300.0f };
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
        vp->dx = -ndx * pull;
        vp->dy = -ndy * pull;
      }
    }
  }

  // Schedule extra pulses from double-pulse upgrade
  int pulse_tier = upgrades_get_tier(UPG_SPIN_DOUBLE_PULSE);
  int has_extra_pulses = (pulse_tier > 0);

  // First pulse: no knockback if extra pulses follow (so they can hit too)
  float spin_kb = has_extra_pulses ? 0.0f : 300.0f * get_spin_knockback_mult();
  player_do_spin_attack(radius, spin_kb);
  spin_visual_timer = SPIN_VISUAL_DURATION;

  // Spin linger zone (rare upgrade)
  int spin_linger_tier = upgrades_get_tier(UPG_SPIN_LINGER_ZONE);
  if (spin_linger_tier > 0) {
    static const float spin_linger_dur[4] = { 0.0f, 1.5f, 2.25f, 3.0f };
    linger_spawn(player_get_x(), player_get_y(), radius,
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
            .slow_mult = slow_vals[slow_tier],
            .active = 1
          };
          break;
        }
      }
    }
  }

  if (has_extra_pulses) {
    spin_extra_pulses = pulse_tier; // 1/2/3 extra pulses
    spin_pulse_timer = 0.1f;
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

  // Resolve full chain path
  chain_state.targets[0] = first;
  chain_state.num_targets = 1;

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

    if (best < 0) break;
    chain_state.targets[chain_state.num_targets] = best;
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
          if (impl_force > 0.0f) {
            // Implosion: hit without knockback, queue tween pull
            enemy_hit(e, 0.0f, 0.0f);
            if (implosion_pull_count < IMPLOSION_MAX_PULLS) {
              float len = dist > 0.1f ? dist : 1.0f;
              float pull_dist = dist * 0.7f; // pull 70% of the way to center
              VacuumPull_t* vp = &implosion_pulls[implosion_pull_count++];
              vp->enemy_index = e;
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
      snake_check_hit_aoe(ix, iy, blast_r);
      if (bomb_cond_tier > 0)
        snake_apply_conductor_aoe(ix, iy, blast_r, 2.0f, bomb_cond_tier);

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
            .active = 1
          };
          break;
        }
      }

      // Bomb linger fire (rare upgrade)
      {
        int bomb_linger_tier = upgrades_get_tier(UPG_BOMB_LINGER_FIRE);
        if (bomb_linger_tier > 0) {
          static const float bomb_linger_dur[4] = { 0.0f, 2.25f, 3.0f, 4.5f };
          linger_spawn(ix, iy, blast_r * 0.6f,
                       bomb_linger_dur[bomb_linger_tier], (aColor_t){255, 100, 30, 200});
        }
      }

      // Cluster Bomb: spawn mini-bombs that scatter and pop
      {
        int cluster_tier = upgrades_get_tier(UPG_BOMB_CLUSTER);
        if (cluster_tier > 0) {
          static const int mini_count[4] = { 0, 4, 5, 6 };
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
                .tick_timer = 0.0f,
                .tier = napalm_tier,
                .active = 1
              };
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

    turrets[slot] = (Turret_t){
      .x = tx, .y = ty,
      .lifetime = get_turret_duration(),
      .fire_timer = 0.0f,
      .tesla_timer = TESLA_INTERVAL,
      .active = 1
    };
  }
}

static void turret_update(float dt)
{
  float fire_rate = get_turret_fire_rate();
  int spread_count = get_turret_spread();
  int overcharge_tier = upgrades_get_tier(UPG_TURRET_OVERCHARGE);
  int max_e = enemy_get_max_count();

  // Update turrets
  for (int i = 0; i < TURRET_MAX; i++) {
    if (!turrets[i].active) continue;

    turrets[i].lifetime -= dt;
    if (turrets[i].lifetime <= 0.0f) {
      turrets[i].active = 0;

      // Overcharge: AoE on expiry
      if (overcharge_tier > 0) {
        static const float aoe_r[4] = { 0.0f, 50.0f, 70.0f, 100.0f };
        float radius = aoe_r[overcharge_tier];
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
        // Explosion visual + sound (reuse bomb explosion slots)
        for (int j = 0; j < BOMB_MAX_ACTIVE; j++) {
          if (!bomb_explosions[j].active) {
            bomb_explosions[j] = (BombExplosion_t){
              .x = turrets[i].x, .y = turrets[i].y,
              .timer = BOMB_VISUAL_DURATION,
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
      continue;
    }

    // Fire at nearest enemy within range
    turrets[i].fire_timer -= dt;
    if (turrets[i].fire_timer <= 0.0f) {
      turrets[i].fire_timer += fire_rate;

      int best = -1;
      float tr = get_turret_range();
      float best_d2 = tr * tr;
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
          float total_angle = 0.5236f; // 30 degrees
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
          .active = 1
        };
      }
    }

    // Tesla Coil: periodic chain lightning from turret
    int tesla_tier = upgrades_get_tier(UPG_TURRET_TESLA_COIL);
    if (tesla_tier > 0) {
      turrets[i].tesla_timer -= dt;
      if (turrets[i].tesla_timer <= 0.0f) {
        turrets[i].tesla_timer += TESLA_INTERVAL;
        static const int max_jumps[4] = { 0, 2, 3, 5 };
        int jumps = max_jumps[tesla_tier];
        int hit_list[5];
        int hit_count = 0;
        float trange = get_turret_range();
        float trange2 = trange * trange;
        float prev_x = turrets[i].x;
        float prev_y = turrets[i].y;

        for (int j = 0; j < jumps; j++) {
          int best_e = -1;
          float best_d2 = trange2;
          for (int e = 0; e < max_e; e++) {
            if (!enemy_is_alive(e)) continue;
            // Skip already hit
            int already = 0;
            for (int h = 0; h < hit_count; h++) {
              if (hit_list[h] == e) { already = 1; break; }
            }
            if (already) continue;
            float ex2, ey2;
            enemy_get_position(e, &ex2, &ey2);
            float er2 = enemy_get_radius(e);
            float tdx = (ex2 + er2) - prev_x;
            float tdy = (ey2 + er2) - prev_y;
            float td2 = tdx * tdx + tdy * tdy;
            if (td2 < best_d2) { best_d2 = td2; best_e = e; }
          }

          // Also check snake heads
          float snake_d = sqrtf(best_d2);
          float shx, shy;
          int snake_hit = snake_find_nearest_head(prev_x, prev_y, &snake_d, &shx, &shy);

          if (best_e < 0 && !snake_hit) break;

          float ecx2, ecy2;
          if (snake_hit) {
            // Snake head is closest — damage snake via AoE at head pos
            snake_check_hit_aoe(shx, shy, 10.0f);
            ecx2 = shx;
            ecy2 = shy;
          } else {
            // Hit the enemy
            enemy_hit(best_e, 0.0f, 0.0f);
            if (tesla_tier >= 3) {
              enemy_set_stun(best_e, 0.3f, 1.0f);
            }
            float ex2, ey2;
            enemy_get_position(best_e, &ex2, &ey2);
            float er2 = enemy_get_radius(best_e);
            ecx2 = ex2 + er2;
            ecy2 = ey2 + er2;
          }
          for (int a = 0; a < TESLA_MAX_ARCS; a++) {
            if (!tesla_arcs[a].active) {
              tesla_arcs[a] = (TeslaArc_t){
                .x1 = prev_x, .y1 = prev_y,
                .x2 = ecx2, .y2 = ecy2,
                .timer = TESLA_ARC_DURATION,
                .active = 1
              };
              break;
            }
          }

          if (best_e >= 0) hit_list[hit_count++] = best_e;
          prev_x = ecx2;
          prev_y = ecy2;
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

    // Homing: steer toward nearest alive enemy
    float bx = turret_bullets[i].x;
    float by = turret_bullets[i].y;
    float best_dist = 200.0f;
    float best_ex = 0, best_ey = 0;
    int found = 0;
    for (int e = 0; e < max_e; e++) {
      if (!enemy_is_alive(e)) continue;
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
    // Shatter: spawn projectiles from each orb before deactivating
    int shatter_tier = upgrades_get_tier(UPG_ORBIT_SHATTER);
    if (shatter_tier > 0) {
      static const int shatter_count[4] = { 0, 3, 4, 6 };
      int count = shatter_count[shatter_tier];
      int s_pierce = (shatter_tier >= 3) ? 1 : 0;
      float px = player_get_x();
      float py = player_get_y();
      float radius = get_orbit_radius();
      int orb_count = get_orbit_orb_count();

      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
        float ox = px + cosf(orb_angle) * radius;
        float oy = py + sinf(orb_angle) * radius;

        for (int s = 0; s < count; s++) {
          float sa = (2.0f * (float)PI * s) / count;
          for (int si = 0; si < SHATTER_MAX; si++) {
            if (!shatter_projs[si].active) {
              shatter_projs[si] = (ShatterProj_t){
                .x = ox, .y = oy,
                .vx = cosf(sa) * 300.0f,
                .vy = sinf(sa) * 300.0f,
                .lifetime = 0.5f,
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

  // Check collisions for each orb
  int max_e = enemy_get_max_count();
  for (int orb = 0; orb < orb_count; orb++) {
    float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
    float orb_x = px + cosf(orb_angle) * radius;
    float orb_y = py + sinf(orb_angle) * radius;

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

        // Conductor: orb hits apply conductor (2s, chains = tier)
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

  // Orbit linger trail: spawn small zones at orb positions periodically
  int linger_tier = upgrades_get_tier(UPG_ORBIT_LINGER_TRAIL);
  if (linger_tier > 0) {
    orbit_trail_timer += dt;
    if (orbit_trail_timer >= 0.3f) {
      orbit_trail_timer -= 0.3f;
      static const float trail_dur[4] = { 0.0f, 0.75f, 1.5f, 2.25f };
      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
        float ox = px + cosf(orb_angle) * radius;
        float oy = py + sinf(orb_angle) * radius;
        linger_spawn(ox, oy, 20.0f, trail_dur[linger_tier], (aColor_t){255, 160, 50, 200});
      }
    }
  }

  // Gravity Well: orbs leave trailing slow zones on the ground
  int gw_tier = upgrades_get_tier(UPG_ORBIT_GRAVITY_WELL);
  if (gw_tier > 0) {
    static const float gw_interval[4] = { 0.0f, 0.4f, 0.3f, 0.25f };
    static const float gw_dur[4]      = { 0.0f, 1.5f, 2.0f, 2.5f };
    static const float gw_radius_t[4] = { 0.0f, 18.0f, 22.0f, 28.0f };
    gw_trail_timer += dt;
    if (gw_trail_timer >= gw_interval[gw_tier]) {
      gw_trail_timer -= gw_interval[gw_tier];
      float dmg_mult = (gw_tier >= 3) ? 1.3f : 1.0f;
      for (int orb = 0; orb < orb_count; orb++) {
        float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
        float ox = px + cosf(orb_angle) * radius;
        float oy = py + sinf(orb_angle) * radius;
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
// Chain propagation update
// ============================================================================

static void chain_update(float dt)
{
  if (!chain_state.active) return;

  chain_state.propagation_timer -= dt;

  if (chain_state.propagation_timer <= 0.0f) {
    int idx = chain_state.current_jump;
    int cur = chain_state.targets[idx];

    // Skip dead enemies (can happen during bounce back)
    if (!enemy_is_alive(cur)) {
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

      // For visual segment: use previous target if alive, else use current as both endpoints
      float pcx = ccx, pcy = ccy;
      if (enemy_is_alive(prev)) {
        enemy_get_position(prev, &prev_x, &prev_y);
        float pr = enemy_get_radius(prev);
        pcx = prev_x + pr;
        pcy = prev_y + pr;
      }

      // Knockback direction: previous enemy -> current enemy
      float dx = ccx - pcx;
      float dy = ccy - pcy;
      float len = sqrtf(dx * dx + dy * dy);
      if (len > 0.1f) { dx /= len; dy /= len; }
      enemy_hit(cur, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);

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

      // Magnetic Pull: queue tweened pull toward centroid
      {
        int mp_tier = upgrades_get_tier(UPG_CHAIN_MAGNETIC_PULL);
        if (mp_tier > 0 && chain_state.num_targets >= 2) {
          static const float pull_pct[4] = { 0.0f, 0.3f, 0.6f, 0.9f };

          // Calculate centroid
          float centroid_x = 0, centroid_y = 0;
          int valid = 0;
          for (int t = 0; t < chain_state.num_targets; t++) {
            int ti = chain_state.targets[t];
            if (!enemy_is_active(ti)) continue;
            float tex, tey;
            enemy_get_position(ti, &tex, &tey);
            float ter = enemy_get_radius(ti);
            centroid_x += tex + ter;
            centroid_y += tey + ter;
            valid++;
          }
          if (valid > 0) {
            centroid_x /= (float)valid;
            centroid_y /= (float)valid;

            magnetic_pull_count = 0;
            magnetic_tween_timer = MAGNETIC_TWEEN_DURATION;

            for (int t = 0; t < chain_state.num_targets; t++) {
              if (magnetic_pull_count >= MAGNETIC_MAX_PULLS) break;
              int ti = chain_state.targets[t];
              if (!enemy_is_active(ti)) continue;
              float tex, tey;
              enemy_get_position(ti, &tex, &tey);
              float ter = enemy_get_radius(ti);
              float tcx = tex + ter;
              float tcy = tey + ter;
              MagneticPull_t* mp = &magnetic_pulls[magnetic_pull_count++];
              mp->enemy_index = ti;
              mp->dx = (centroid_x - tcx) * pull_pct[mp_tier];
              mp->dy = (centroid_y - tcy) * pull_pct[mp_tier];
            }
          }
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
      static const float pull_range[4] = { 0.0f, 40.0f, 55.0f, 70.0f };
      static const float pull_speed[4] = { 0.0f, 30.0f, 50.0f, 70.0f };
      float range = pull_range[mirage_tier];
      float speed_val = pull_speed[mirage_tier];

      for (int e = 0; e < max_e; e++) {
        if (!enemy_is_alive(e)) continue;
        float ex, ey;
        enemy_get_position(e, &ex, &ey);
        float er = enemy_get_radius(e);
        float ecx = ex + er, ecy = ey + er;

        // Find nearest active segment
        float best_dist = range + er;
        float best_sx = 0, best_sy = 0;
        int found_seg = 0;
        for (int s = 0; s < TRAIL_MAX_SEGMENTS; s++) {
          if (!trail_segments[s].active) continue;
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
// Update
// ============================================================================

void weapons_update(float dt)
{
  // Wand barrage burst timer
  if (wand_burst_remaining > 0) {
    wand_burst_timer -= dt;
    if (wand_burst_timer <= 0.0f) {
      damage_source = WEAPON_WAND;
      fire_wand_single();
      wand_burst_remaining--;
      wand_burst_timer = 0.05f;
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
      if (!enemy_is_alive(vp->enemy_index)) continue;
      enemy_displace(vp->enemy_index, vp->dx * delta_pct, vp->dy * delta_pct);
    }
  }

  // Magnetic pull tween: smoothly pull chained enemies toward centroid
  if (magnetic_tween_timer > 0.0f) {
    float prev_t = magnetic_tween_timer;
    magnetic_tween_timer -= dt;
    if (magnetic_tween_timer < 0.0f) magnetic_tween_timer = 0.0f;

    float old_pct = 1.0f - (prev_t / MAGNETIC_TWEEN_DURATION);
    float new_pct = 1.0f - (magnetic_tween_timer / MAGNETIC_TWEEN_DURATION);
    float delta_pct = new_pct - old_pct;

    for (int i = 0; i < magnetic_pull_count; i++) {
      MagneticPull_t* mp = &magnetic_pulls[i];
      if (!enemy_is_alive(mp->enemy_index)) continue;
      enemy_displace(mp->enemy_index, mp->dx * delta_pct, mp->dy * delta_pct);
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
      if (!enemy_is_alive(vp->enemy_index)) continue;
      enemy_displace(vp->enemy_index, vp->dx * delta_pct, vp->dy * delta_pct);
    }
  }
  if (implosion_visual_timer > 0.0f) {
    implosion_visual_timer -= dt;
  }

  // Spin double-pulse: fire extra pulses at 0.1s intervals
  damage_source = WEAPON_SPIN;
  if (spin_extra_pulses > 0) {
    spin_pulse_timer -= dt;
    if (spin_pulse_timer <= 0.0f) {
      float radius = get_spin_radius();
      spin_extra_pulses--;
      // Only knockback on the final pulse
      float kb = (spin_extra_pulses == 0) ? 300.0f * get_spin_knockback_mult() : 0.0f;
      player_do_spin_attack(radius, kb);
      spin_visual_timer = SPIN_VISUAL_DURATION;
      // Refresh spin slow zone on each extra pulse
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
                .slow_mult = slow_vals[slow_tier],
                .active = 1
              };
              break;
            }
          }
        }
      }
      if (spin_extra_pulses > 0) {
        spin_pulse_timer = 0.1f;
      }
    }
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

  // Update bombs
  damage_source = WEAPON_BOMB;
  bomb_update(dt);

  // Update turrets
  damage_source = WEAPON_TURRET;
  turret_update(dt);

  // Update trail
  damage_source = WEAPON_TRAIL;
  trail_update(dt);

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
    float mini_radius_base = 25.0f;
    int cluster_tier = upgrades_get_tier(UPG_BOMB_CLUSTER);
    float mini_radius = (cluster_tier >= 3) ? 35.0f : mini_radius_base;

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

  // Update napalm streaks (spreading fire lines)
  damage_source = WEAPON_BOMB;
  {
    int max_e = enemy_get_max_count();
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

      // Tick damage — check if enemy is within the streak rectangle
      napalm_zones[i].tick_timer += dt;
      if (napalm_zones[i].tick_timer >= 0.3f) {
        napalm_zones[i].tick_timer -= 0.3f;

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
        default: break;
      }
      slots[i].timer = 0.0f;
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

      int sx = (int)trail_segments[i].x;
      int sy = (int)trail_segments[i].y;

      // Draw filled rect
      SDL_SetRenderDrawColor(app.renderer, (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
      SDL_Rect seg_rect = { sx - half_w, sy - half_h, (int)tw, 8 };
      SDL_RenderFillRect(app.renderer, &seg_rect);

      // 1px bright core (ember effect)
      if (ratio > 0.4f) {
        int core_a = (int)(120.0f * ratio);
        SDL_SetRenderDrawColor(app.renderer, 255, 220, 80, (uint8_t)core_a);
        SDL_Rect core = { sx - half_w + 2, sy - 1, (int)tw - 4, 2 };
        SDL_RenderFillRect(app.renderer, &core);
      }
    }

    // Draw ember bursts
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

    // Heat Mirage shimmer: faint dots near trail segments
    if (upgrades_get_tier(UPG_TRAIL_HEAT_MIRAGE) > 0) {
      for (int i = 0; i < TRAIL_MAX_SEGMENTS; i++) {
        if (!trail_segments[i].active) continue;
        int sx = (int)trail_segments[i].x;
        int sy = (int)trail_segments[i].y;
        float ratio = trail_segments[i].lifetime / trail_segments[i].max_lifetime;
        int sa = (int)(40.0f * ratio);
        if (sa < 5) continue;
        SDL_SetRenderDrawColor(app.renderer, 255, 200, 100, (uint8_t)sa);
        // 2-3 shimmer dots per segment
        for (int d = 0; d < 2; d++) {
          int ox = (rand() % 20) - 10;
          int oy = (rand() % 20) - 10;
          SDL_Rect dot = { sx + ox, sy + oy, 2, 1 };
          SDL_RenderFillRect(app.renderer, &dot);
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
    float current_radius = get_bomb_blast_radius() * (0.3f + 0.7f * p);
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

  // Draw orbit orbs
  if (orbit_state.is_active) {
    float px = player_get_x();
    float py = player_get_y();
    float radius = get_orbit_radius();
    int orb_count = get_orbit_orb_count();
    float growth = get_orbit_growth_factor();
    float orb_sz = ORBIT_ORB_SIZE * growth;

    for (int orb = 0; orb < orb_count; orb++) {
      float orb_angle = orbit_state.angle + (2.0f * (float)PI * orb) / orb_count;
      float orb_x = px + cosf(orb_angle) * radius - orb_sz / 2.0f;
      float orb_y = py + sinf(orb_angle) * radius - orb_sz / 2.0f;

      // Glow behind orb
      a_DrawFilledRect(
        (aRectf_t){orb_x - 2, orb_y - 2, orb_sz + 4, orb_sz + 4},
        (aColor_t){255, 160, 50, 80}
      );
      // Solid orb
      a_DrawFilledRect(
        (aRectf_t){orb_x, orb_y, orb_sz, orb_sz},
        (aColor_t){255, 160, 50, 255}
      );
    }
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
    float za = 40.0f * (turret_slow_zones[i].lifetime / 3.0f);
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

    // Faint range circle
    int ra = (int)(20.0f * alpha_mult);
    if (ra > 0) {
      float trange = get_turret_range();
      SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, (uint8_t)ra);
      int segs = 32;
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

    // Diamond body (8px)
    int sz = 4;
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

  // Draw turret bullets (3x3 orange-yellow squares)
  for (int i = 0; i < TURRET_BULLET_MAX; i++) {
    if (!turret_bullets[i].active) continue;
    int bx = (int)turret_bullets[i].x;
    int by = (int)turret_bullets[i].y;
    // Glow (5x5)
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, 40);
    { SDL_Rect glow = { bx - 2, by - 2, 5, 5 }; SDL_RenderFillRect(app.renderer, &glow); }
    // Core (3x3)
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, 255);
    { SDL_Rect core = { bx - 1, by - 1, 3, 3 }; SDL_RenderFillRect(app.renderer, &core); }
  }

  // Draw tesla coil arcs (white-blue lightning)
  for (int i = 0; i < TESLA_MAX_ARCS; i++) {
    if (!tesla_arcs[i].active) continue;
    float alpha_pct = tesla_arcs[i].timer / TESLA_ARC_DURATION;
    int alpha = (int)(220.0f * alpha_pct);
    if (alpha > 255) alpha = 255;

    int x1 = (int)tesla_arcs[i].x1, y1 = (int)tesla_arcs[i].y1;
    int x2 = (int)tesla_arcs[i].x2, y2 = (int)tesla_arcs[i].y2;

    // Main arc line (bright white-blue)
    SDL_SetRenderDrawColor(app.renderer, 180, 220, 255, (uint8_t)alpha);
    SDL_RenderDrawLine(app.renderer, x1, y1, x2, y2);
    // Offset line for thickness
    SDL_SetRenderDrawColor(app.renderer, 120, 180, 255, (uint8_t)(alpha / 2));
    SDL_RenderDrawLine(app.renderer, x1 + 1, y1, x2 + 1, y2);
    SDL_RenderDrawLine(app.renderer, x1, y1 + 1, x2, y2 + 1);
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

int weapons_get_count(void)
{
  return weapon_count;
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
    case SOURCE_FIRE_CONE:      return "FIRE CONE";
    case SOURCE_CONDUCTOR:      return "CONDUCTOR";
    case SOURCE_GRUNT_EXPLOSION: return "GRUNT BOOM";
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
  static const float durations[4] = { 0.0f, 1.5f, 2.0f, 3.0f };
  static const float radii[4]     = { 0.0f, 20.0f, 25.0f, 30.0f };

  for (int i = 0; i < TURRET_SLOW_MAX; i++) {
    if (!turret_slow_zones[i].active) {
      turret_slow_zones[i] = (TurretSlowZone_t){
        .x = x, .y = y,
        .radius = radii[tier],
        .lifetime = durations[tier],
        .slow_mult = slow_vals[tier],
        .active = 1
      };
      return;
    }
  }
}

int weapons_get_turret_slow(float x, float y, float* out_mult)
{
  for (int i = 0; i < TURRET_SLOW_MAX; i++) {
    if (!turret_slow_zones[i].active) continue;
    float dx = x - turret_slow_zones[i].x;
    float dy = y - turret_slow_zones[i].y;
    if (sqrtf(dx * dx + dy * dy) < turret_slow_zones[i].radius) {
      if (out_mult) *out_mult = turret_slow_zones[i].slow_mult;
      return 1;
    }
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
  return found;
}
