#include <math.h>
#include "Archimedes.h"
#include "weapons.h"
#include "upgrades.h"
#include "player_actions.h"
#include "enemy.h"
#include "fire_particles.h"

// ============================================================================
// Weapon Definitions
// ============================================================================

#define WAND_COOLDOWN  1.0f
#define SPIN_COOLDOWN  2.0f
#define SPIN_RADIUS    92.0f
#define SPIN_VISUAL_DURATION 0.15f

// ============================================================================
// State
// ============================================================================

static Weapon_t slots[WEAPON_MAX_SLOTS];
static int weapon_count = 0;
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
static ChainSegment_t chain_segments[CHAIN_MAX_JUMPS];
static int chain_segment_count = 0;

// Orbit weapon state
static OrbitState_t orbit_state;
static OrbitHitEntry_t orbit_hits[ORBIT_MAX_HIT_TRACK];
static int orbit_hit_count = 0;

// Bomb weapon state
static BombFlight_t bomb_flights[BOMB_MAX_ACTIVE];
static BombExplosion_t bomb_explosions[BOMB_MAX_ACTIVE];

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
} LingerZone_t;

static LingerZone_t linger_zones[LINGER_MAX_ZONES];

// Orbit linger trail timer
static float orbit_trail_timer = 0.0f;
// Gravity Well slow trail timer
static float gw_trail_timer = 0.0f;

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

// Napalm zones (Bomb upgrade - spreading fire)
#define NAPALM_MAX 8
typedef struct {
  float x, y;
  float current_r, max_r;
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
  float damage_mult;
  int active;
} CraterZone_t;
static CraterZone_t crater_zones[CRATER_MAX];

// Track when aftershock should fire (after all pulses done)
static int aftershock_pending = 0;
static float aftershock_saved_dx = 0.0f;
static float aftershock_saved_dy = -1.0f;

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
  return best;
}

static const float cooldown_reduction[4] = { 0.0f, 0.15f, 0.30f, 0.45f };

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
    default: return base;
  }

  if (slots[slot].type == WEAPON_ORBIT) return base;

  int tier = upgrades_get_tier(upg);
  return base * (1.0f - cooldown_reduction[tier]);
}

static float get_spin_radius(void)
{
  return SPIN_RADIUS + 18.0f * upgrades_get_tier(UPG_SPIN_RADIUS);
}

static int get_chain_max_jumps(void)
{
  return 3 + upgrades_get_tier(UPG_CHAIN_EXTRA_JUMPS);
}

static float get_chain_radius(void)
{
  return CHAIN_RADIUS + 18.0f * upgrades_get_tier(UPG_CHAIN_RADIUS);
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
  return ORBIT_RADIUS + 15.0f * upgrades_get_tier(UPG_ORBIT_RADIUS);
}

static float get_bomb_blast_radius(void)
{
  return BOMB_EXPLOSION_RADIUS + 17.0f * upgrades_get_tier(UPG_BOMB_BLAST_RADIUS);
}

static void linger_spawn(float x, float y, float radius, float duration, aColor_t color)
{
  for (int i = 0; i < LINGER_MAX_ZONES; i++) {
    if (!linger_zones[i].active) {
      linger_zones[i] = (LingerZone_t){
        .x = x, .y = y, .radius = radius,
        .lifetime = duration, .max_lifetime = duration,
        .tick_timer = 0.0f, .active = 1, .color = color
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
    }
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
    int r2 = r * r;
    int zx = (int)linger_zones[i].x;
    int zy = (int)linger_zones[i].y;

    SDL_SetRenderDrawColor(app.renderer,
      linger_zones[i].color.r, linger_zones[i].color.g, linger_zones[i].color.b,
      (uint8_t)alpha);

    for (int pdy = -r; pdy <= r; pdy++) {
      for (int pdx = -r; pdx <= r; pdx++) {
        if (pdx * pdx + pdy * pdy <= r2)
          SDL_RenderDrawPoint(app.renderer, zx + pdx, zy + pdy);
      }
    }

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
  chain_state.current_jump = 0;
  chain_segment_count = 0;

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

  // Init shatter projectiles
  for (int i = 0; i < SHATTER_MAX; i++) shatter_projs[i].active = 0;

  // Init cluster minis
  for (int i = 0; i < CLUSTER_MAX; i++) cluster_minis[i].active = 0;

  // Init napalm zones
  for (int i = 0; i < NAPALM_MAX; i++) napalm_zones[i].active = 0;

  // Init crater zones
  for (int i = 0; i < CRATER_MAX; i++) crater_zones[i].active = 0;
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
    default:
      return -1;
  }

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

static void fire_wand(void)
{
  static const int multishot_count[4] = { 1, 2, 3, 5 };
  int count = multishot_count[upgrades_get_tier(UPG_WAND_MULTISHOT)];

  if (count <= 1) {
    player_fire_at_nearest();
  } else {
    player_fire_fan_at_nearest(count);
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
  player_do_spin_attack(radius, !has_extra_pulses);
  spin_visual_timer = SPIN_VISUAL_DURATION;

  // Spin linger zone (rare upgrade)
  int spin_linger_tier = upgrades_get_tier(UPG_SPIN_LINGER_ZONE);
  if (spin_linger_tier > 0) {
    static const float spin_linger_dur[4] = { 0.0f, 1.5f, 2.25f, 3.0f };
    linger_spawn(player_get_x(), player_get_y(), radius,
                 spin_linger_dur[spin_linger_tier], (aColor_t){255, 200, 50, 200});
  }

  if (has_extra_pulses) {
    spin_extra_pulses = pulse_tier; // 1/2/3 extra pulses
    spin_pulse_timer = 0.1f;
  }

  // Aftershock: schedule directional wave after all pulses complete
  // Lock targeting direction NOW (before spin knockback shuffles enemies)
  int aftershock_tier = upgrades_get_tier(UPG_SPIN_AFTERSHOCK);
  if (aftershock_tier > 0) {
    float adx = 0.0f, ady = -1.0f;
    float apx = player_get_x(), apy = player_get_y();
    int atgt = find_nearest_enemy(apx, apy, &adx, &ady);
    if (atgt >= 0) {
      float alen = sqrtf(adx * adx + ady * ady);
      if (alen > 0.1f) { adx /= alen; ady /= alen; }
    }
    aftershock_saved_dx = adx;
    aftershock_saved_dy = ady;

    if (has_extra_pulses) {
      aftershock_pending = 1;
    } else {
      spawn_aftershock_wave(aftershock_tier, adx, ady);
    }
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
  int chain_max = get_chain_max_jumps();

  // Find best cluster target
  int first = enemy_find_cluster_target(chain_radius, px, py);
  if (first < 0) return;

  if (chain_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_PLAYER,
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
  for (int jump = 1; jump < chain_max; jump++) {
    int prev = chain_state.targets[jump - 1];
    float prev_x, prev_y;
    enemy_get_position(prev, &prev_x, &prev_y);
    float prev_r = enemy_get_radius(prev);
    float cx = prev_x + prev_r;
    float cy = prev_y + prev_r;

    int best = -1;
    float best_dist = chain_radius + 1.0f;
    int best_is_conductor = 0;

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

      // Conductors have wider chain radius
      float effective_radius = chain_radius;
      int is_cond = enemy_is_conductor(e);
      if (is_cond) {
        effective_radius *= (1.0f + enemy_get_conductor_bonus_radius(e));
      }

      if (d <= effective_radius) {
        // Prefer conductors (pick conductor even if further)
        if (is_cond && !best_is_conductor) {
          best = e;
          best_dist = d;
          best_is_conductor = 1;
        } else if (is_cond == best_is_conductor && d < best_dist) {
          best = e;
          best_dist = d;
          best_is_conductor = is_cond;
        }
      }
    }

    if (best < 0) break;
    chain_state.targets[chain_state.num_targets] = best;
    chain_state.num_targets++;
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
  chain_state.active = (chain_state.num_targets > 1) ? 1 : 0;
  // If only 1 target, chain is done immediately (no propagation needed)
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
  if (!enemy_find_cluster_position(blast_r, px, py, BOMB_FLIGHT_TIME, &tx, &ty))
    return;

  int bomb_count = 1 + upgrades_get_tier(UPG_BOMB_MULTI_BOMB);

  for (int b = 0; b < bomb_count; b++) {
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
      .channel = AUDIO_CHANNEL_PLAYER,
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

    bomb_flights[i].flight_progress += dt / BOMB_FLIGHT_TIME;

    if (bomb_flights[i].flight_progress >= 1.0f) {
      // Impact — apply AoE damage
      float ix = bomb_flights[i].target_x;
      float iy = bomb_flights[i].target_y;
      bomb_flights[i].in_flight = 0;

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
          // Knockback: from impact center outward
          float len = dist > 0.1f ? dist : 1.0f;
          float kx = dx / len;
          float ky = dy / len;
          enemy_hit(e, kx * BOMB_KNOCKBACK, ky * BOMB_KNOCKBACK);
        }
      }

      if (bomb_explode_sound_loaded) {
        aAudioOptions_t opts = {
          .channel = AUDIO_CHANNEL_PLAYER,
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

      // Napalm: spreading fire zone
      {
        int napalm_tier = upgrades_get_tier(UPG_BOMB_NAPALM);
        if (napalm_tier > 0) {
          static const float np_max_pct[4] = { 0.0f, 1.0f, 1.2f, 1.5f };
          static const float np_grow[4] = { 0.0f, 2.0f, 1.5f, 1.5f };
          for (int ni = 0; ni < NAPALM_MAX; ni++) {
            if (!napalm_zones[ni].active) {
              napalm_zones[ni] = (NapalmZone_t){
                .x = ix, .y = iy,
                .current_r = blast_r * 0.3f,
                .max_r = blast_r * np_max_pct[napalm_tier],
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
                .damage_mult = dmg_mult,
                .active = 1
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

  // Advance angle (1 rev/sec = 2*PI rad/s)
  orbit_state.angle += (2.0f * (float)PI) * dt;

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
      }
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
              .damage_mult = dmg_mult,
              .active = 1
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
    int prev = chain_state.targets[idx - 1];

    float cur_x, cur_y, prev_x, prev_y;
    enemy_get_position(cur, &cur_x, &cur_y);
    enemy_get_position(prev, &prev_x, &prev_y);
    float cr = enemy_get_radius(cur);
    float pr = enemy_get_radius(prev);
    float ccx = cur_x + cr;
    float ccy = cur_y + cr;
    float pcx = prev_x + pr;
    float pcy = prev_y + pr;

    // Knockback direction: previous enemy -> current enemy
    float dx = ccx - pcx;
    float dy = ccy - pcy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.1f) { dx /= len; dy /= len; }
    enemy_hit(cur, dx * CHAIN_KNOCKBACK, dy * CHAIN_KNOCKBACK);

    // Chain linger arc (rare upgrade): spawn small zone at target
    {
      int chain_linger_tier = upgrades_get_tier(UPG_CHAIN_LINGER_ARC);
      if (chain_linger_tier > 0) {
        static const float chain_linger_dur[4] = { 0.0f, 0.75f, 1.5f, 2.25f };
        linger_spawn(ccx, ccy, 30.0f, chain_linger_dur[chain_linger_tier],
                     (aColor_t){200, 220, 255, 200});
      }
    }

    // Add visual segment
    if (chain_segment_count < CHAIN_MAX_JUMPS) {
      chain_segments[chain_segment_count] = (ChainSegment_t){
        .x1 = pcx, .y1 = pcy,
        .x2 = ccx, .y2 = ccy,
        .timer = CHAIN_VISUAL_DURATION
      };
      chain_segment_count++;
    }

    // Static Field: stun each chained enemy
    {
      int sf_tier = upgrades_get_tier(UPG_CHAIN_STATIC_FIELD);
      if (sf_tier > 0) {
        static const float stun_dur[4] = { 0.0f, 0.3f, 0.5f, 0.8f };
        float dmg_mult = (sf_tier >= 3) ? 1.5f : 1.0f;
        enemy_set_stun(cur, stun_dur[sf_tier], dmg_mult);
      }
    }

    chain_state.current_jump++;
    if (chain_state.current_jump >= chain_state.num_targets) {
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
  }
}

// ============================================================================
// Update
// ============================================================================

void weapons_update(float dt)
{
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

  // Spin double-pulse: fire extra pulses at 0.1s intervals
  if (spin_extra_pulses > 0) {
    spin_pulse_timer -= dt;
    if (spin_pulse_timer <= 0.0f) {
      float radius = get_spin_radius();
      spin_extra_pulses--;
      // Only knockback on the final pulse
      player_do_spin_attack(radius, spin_extra_pulses == 0);
      spin_visual_timer = SPIN_VISUAL_DURATION;
      if (spin_extra_pulses > 0) {
        spin_pulse_timer = 0.1f;
      } else if (aftershock_pending) {
        // All pulses done — fire aftershock wave (using saved direction)
        aftershock_pending = 0;
        int as_tier = upgrades_get_tier(UPG_SPIN_AFTERSHOCK);
        spawn_aftershock_wave(as_tier, aftershock_saved_dx, aftershock_saved_dy);
      }
    }
  }

  // Update chain visual segment timers
  for (int i = 0; i < chain_segment_count; i++) {
    chain_segments[i].timer -= dt;
  }

  // Update chain propagation
  chain_update(dt);

  // Update orbit
  orbit_update(dt);

  // Update bombs
  bomb_update(dt);

  // Update lingering effects
  linger_update(dt);

  // Update aftershock directional wave
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
    }
  }

  // Update shatter projectiles
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
    }
  }

  // Update cluster mini-bombs
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

        // Mini explosion visual
        linger_spawn(ix, iy, mini_radius * 0.6f, 0.2f, (aColor_t){255, 160, 60, 200});
      }
    }
  }

  // Update napalm zones (spreading fire)
  {
    int max_e = enemy_get_max_count();
    for (int i = 0; i < NAPALM_MAX; i++) {
      if (!napalm_zones[i].active) continue;
      napalm_zones[i].elapsed += dt;

      // Grow radius
      if (napalm_zones[i].elapsed < napalm_zones[i].grow_time) {
        float t = napalm_zones[i].elapsed / napalm_zones[i].grow_time;
        napalm_zones[i].current_r = napalm_zones[i].max_r * (0.3f + 0.7f * t);
      } else {
        napalm_zones[i].current_r = napalm_zones[i].max_r;
      }

      // Burn time after grow phase
      float total_dur = napalm_zones[i].grow_time + napalm_zones[i].burn_time;
      if (napalm_zones[i].elapsed >= total_dur) {
        napalm_zones[i].active = 0;
        continue;
      }

      // Tick damage
      napalm_zones[i].tick_timer += dt;
      if (napalm_zones[i].tick_timer >= 0.3f) {
        napalm_zones[i].tick_timer -= 0.3f;

        // T3 intensifying damage: extra hit as fire ages
        int extra_hits = 0;
        if (napalm_zones[i].tier >= 3) {
          float age_pct = napalm_zones[i].elapsed / total_dur;
          extra_hits = (age_pct > 0.5f) ? 1 : 0;
        }

        for (int e = 0; e < max_e; e++) {
          if (!enemy_is_alive(e)) continue;
          float ex, ey;
          enemy_get_position(e, &ex, &ey);
          float er = enemy_get_radius(e);
          float dx = (ex + er) - napalm_zones[i].x;
          float dy = (ey + er) - napalm_zones[i].y;
          float dist = sqrtf(dx * dx + dy * dy);
          if (dist < napalm_zones[i].current_r + er) {
            enemy_hit(e, 0.0f, 0.0f);
            for (int h = 0; h < extra_hits; h++) enemy_hit(e, 0.0f, 0.0f);
          }
        }
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
      switch (slots[i].type) {
        case WEAPON_WAND: fire_wand(); break;
        case WEAPON_SPIN: fire_spin(); break;
        case WEAPON_CHAIN: fire_chain(); break;
        case WEAPON_ORBIT: fire_orbit(); break;
        case WEAPON_BOMB: fire_bomb(); break;
        default: break;
      }
      slots[i].timer = 0.0f;
    }
  }
}

// ============================================================================
// Draw
// ============================================================================

void weapons_draw(void)
{
  // Draw lingering effect zones (below other weapon visuals)
  linger_draw();

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
    int sr2 = sr * sr;
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 30, 80);
    for (int pdy = -sr; pdy <= sr; pdy++) {
      for (int pdx = -sr; pdx <= sr; pdx++) {
        if (pdx * pdx + pdy * pdy <= sr2)
          SDL_RenderDrawPoint(app.renderer, (int)cx + pdx, (int)cy + pdy);
      }
    }

    // Bomb dot — height curve: small at peak (progress=0.5), full at start/end
    float height_factor = sinf(p * (float)PI);
    float bomb_size = 6.0f * (1.0f - height_factor * 0.7f);
    int bomb_alpha = 255 - (int)(height_factor * 150.0f);
    if (bomb_alpha < 50) bomb_alpha = 50;

    int br = (int)(bomb_size / 2.0f);
    if (br < 1) br = 1;
    int br2 = br * br;
    SDL_SetRenderDrawColor(app.renderer, 255, 180, 50, (uint8_t)bomb_alpha);
    for (int pdy = -br; pdy <= br; pdy++) {
      for (int pdx = -br; pdx <= br; pdx++) {
        if (pdx * pdx + pdy * pdy <= br2)
          SDL_RenderDrawPoint(app.renderer, (int)cx + pdx, (int)cy + pdy);
      }
    }
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
    int er2 = er * er;

    // Filled explosion circle
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 30, (uint8_t)alpha);
    for (int pdy = -er; pdy <= er; pdy++) {
      for (int pdx = -er; pdx <= er; pdx++) {
        if (pdx * pdx + pdy * pdy <= er2)
          SDL_RenderDrawPoint(app.renderer, ex + pdx, ey + pdy);
      }
    }

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

  // Draw napalm zones (growing orange-red circles)
  for (int i = 0; i < NAPALM_MAX; i++) {
    if (!napalm_zones[i].active) continue;
    float total_dur = napalm_zones[i].grow_time + napalm_zones[i].burn_time;
    float fade = 1.0f - (napalm_zones[i].elapsed / total_dur);
    int alpha = (int)(fade * 60.0f);
    if (alpha < 0) alpha = 0;

    int r = (int)napalm_zones[i].current_r;
    int r2 = r * r;
    int nx = (int)napalm_zones[i].x;
    int ny = (int)napalm_zones[i].y;

    SDL_SetRenderDrawColor(app.renderer, 255, 80, 20, (uint8_t)alpha);
    for (int pdy = -r; pdy <= r; pdy++) {
      for (int pdx = -r; pdx <= r; pdx++) {
        if (pdx * pdx + pdy * pdy <= r2)
          SDL_RenderDrawPoint(app.renderer, nx + pdx, ny + pdy);
      }
    }

    // Outer ring
    int ring_alpha = alpha + 30;
    if (ring_alpha > 255) ring_alpha = 255;
    SDL_SetRenderDrawColor(app.renderer, 255, 100, 30, (uint8_t)ring_alpha);
    int segs = 24;
    for (int s = 0; s < segs; s++) {
      float a1 = (float)s / segs * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        nx + (int)(cosf(a1) * (float)r), ny + (int)(sinf(a1) * (float)r),
        nx + (int)(cosf(a2) * (float)r), ny + (int)(sinf(a2) * (float)r));
    }
  }

  // Draw crater zones (dark translucent circles with pulsing edge)
  for (int i = 0; i < CRATER_MAX; i++) {
    if (!crater_zones[i].active) continue;

    // Fade based on remaining lifetime (need max lifetime to compute ratio)
    // Craters start at 5/7/8s — estimate max from current value on first frame
    // Use a simple approach: fade in last 2 seconds
    float fade = 1.0f;
    if (crater_zones[i].lifetime < 2.0f) {
      fade = crater_zones[i].lifetime / 2.0f;
    }

    int r = (int)crater_zones[i].radius;
    int r2 = r * r;
    int cx = (int)crater_zones[i].x;
    int cy = (int)crater_zones[i].y;

    // Darkened ground (fades with lifetime)
    int ground_alpha = (int)(35.0f * fade);
    if (ground_alpha < 0) ground_alpha = 0;
    SDL_SetRenderDrawColor(app.renderer, 60, 50, 40, (uint8_t)ground_alpha);
    for (int pdy = -r; pdy <= r; pdy++) {
      for (int pdx = -r; pdx <= r; pdx++) {
        if (pdx * pdx + pdy * pdy <= r2)
          SDL_RenderDrawPoint(app.renderer, cx + pdx, cy + pdy);
      }
    }

    // Pulsing ring at edge (fades with lifetime)
    float pulse = (40.0f + 20.0f * sinf(crater_zones[i].lifetime * 3.0f)) * fade;
    SDL_SetRenderDrawColor(app.renderer, 100, 80, 60, (uint8_t)pulse);
    int segs = 24;
    for (int s = 0; s < segs; s++) {
      float a1 = (float)s / segs * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / segs * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        cx + (int)(cosf(a1) * (float)r), cy + (int)(sinf(a1) * (float)r),
        cx + (int)(cosf(a2) * (float)r), cy + (int)(sinf(a2) * (float)r));
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

int weapons_get_count(void)
{
  return weapon_count;
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
