#include <math.h>
#include "Archimedes.h"
#include "weapons.h"
#include "player_actions.h"
#include "enemy.h"

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
                                .cooldown = WAND_COOLDOWN, .timer = 0.0f };
      break;
    case WEAPON_SPIN:
      slots[slot] = (Weapon_t){ .type = WEAPON_SPIN, .label = "S",
                                .cooldown = SPIN_COOLDOWN, .timer = 0.0f };
      break;
    case WEAPON_CHAIN:
      slots[slot] = (Weapon_t){ .type = WEAPON_CHAIN, .label = "L",
                                .cooldown = CHAIN_COOLDOWN, .timer = 0.0f };
      break;
    case WEAPON_ORBIT:
      slots[slot] = (Weapon_t){ .type = WEAPON_ORBIT, .label = "O",
                                .cooldown = ORBIT_COOLDOWN, .timer = ORBIT_COOLDOWN };
      break;
    case WEAPON_BOMB:
      slots[slot] = (Weapon_t){ .type = WEAPON_BOMB, .label = "B",
                                .cooldown = BOMB_COOLDOWN, .timer = 0.0f };
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
  player_fire_at_nearest();
}

static void fire_spin(void)
{
  player_do_spin_attack(SPIN_RADIUS);
  spin_visual_timer = SPIN_VISUAL_DURATION;

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

  // Find best cluster target
  int first = enemy_find_cluster_target(CHAIN_RADIUS, px, py);
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
  // Track which enemies are already in the chain
  // Use a simple linear scan since CHAIN_MAX_JUMPS is small
  for (int jump = 1; jump < CHAIN_MAX_JUMPS; jump++) {
    int prev = chain_state.targets[jump - 1];
    float prev_x, prev_y;
    enemy_get_position(prev, &prev_x, &prev_y);
    float prev_r = enemy_get_radius(prev);
    float cx = prev_x + prev_r;
    float cy = prev_y + prev_r;

    int best = -1;
    float best_dist = CHAIN_RADIUS + 1.0f;

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
      if (d <= CHAIN_RADIUS && d < best_dist) {
        best = e;
        best_dist = d;
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
  orbit_state.active_timer = ORBIT_DURATION;
  orbit_state.angle = 0.0f;
  orbit_hit_count = 0;
}

static void fire_bomb(void)
{
  float px = player_get_x();
  float py = player_get_y();

  // Find densest cluster, predict where they'll be when the bomb lands
  float tx, ty;
  if (!enemy_find_cluster_position(BOMB_EXPLOSION_RADIUS, px, py, BOMB_FLIGHT_TIME, &tx, &ty))
    return;

  // Find empty flight slot
  for (int i = 0; i < BOMB_MAX_ACTIVE; i++) {
    if (!bomb_flights[i].in_flight) {
      bomb_flights[i] = (BombFlight_t){
        .start_x = px, .start_y = py,
        .target_x = tx, .target_y = ty,
        .flight_progress = 0.0f,
        .in_flight = 1
      };

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
      return;
    }
  }
}

// ============================================================================
// Bomb update
// ============================================================================

static void bomb_update(float dt)
{
  int max_e = enemy_get_max_count();

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

        if (dist < BOMB_EXPLOSION_RADIUS) {
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

static void orbit_update(float dt)
{
  if (!orbit_state.is_active) return;

  orbit_state.active_timer -= dt;
  if (orbit_state.active_timer <= 0.0f) {
    orbit_state.is_active = 0;
    return;
  }

  // Advance angle (1 rev/sec = 2*PI rad/s)
  orbit_state.angle += (2.0f * (float)PI) * dt;

  // Calculate orb world position
  float px = player_get_x();
  float py = player_get_y();
  float orb_x = px + cosf(orbit_state.angle) * ORBIT_RADIUS;
  float orb_y = py + sinf(orbit_state.angle) * ORBIT_RADIUS;

  // Tick down per-enemy hit cooldowns
  for (int i = 0; i < orbit_hit_count; i++) {
    orbit_hits[i].cooldown -= dt;
    if (orbit_hits[i].cooldown <= 0.0f) {
      orbit_hits[i] = orbit_hits[orbit_hit_count - 1];
      orbit_hit_count--;
      i--;
    }
  }

  // Check collisions with alive enemies
  int max_e = enemy_get_max_count();
  for (int e = 0; e < max_e; e++) {
    if (!enemy_is_alive(e)) continue;

    // Check if this enemy is on hit cooldown
    int on_cooldown = 0;
    for (int h = 0; h < orbit_hit_count; h++) {
      if (orbit_hits[h].enemy_index == e) { on_cooldown = 1; break; }
    }
    if (on_cooldown) continue;

    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float ecx = ex + er;
    float ecy = ey + er;

    float dx = orb_x - ecx;
    float dy = orb_y - ecy;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < (ORBIT_ORB_SIZE + er)) {
      // Knockback: player center -> enemy
      float kx = ecx - px;
      float ky = ecy - py;
      float klen = sqrtf(kx * kx + ky * ky);
      if (klen > 0.1f) { kx /= klen; ky /= klen; }
      enemy_hit(e, kx * ORBIT_KNOCKBACK, ky * ORBIT_KNOCKBACK);

      // Track hit cooldown
      if (orbit_hit_count < ORBIT_MAX_HIT_TRACK) {
        orbit_hits[orbit_hit_count].enemy_index = e;
        orbit_hits[orbit_hit_count].cooldown = ORBIT_HIT_COOLDOWN;
        orbit_hit_count++;
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

    // Add visual segment
    if (chain_segment_count < CHAIN_MAX_JUMPS) {
      chain_segments[chain_segment_count] = (ChainSegment_t){
        .x1 = pcx, .y1 = pcy,
        .x2 = ccx, .y2 = ccy,
        .timer = CHAIN_VISUAL_DURATION
      };
      chain_segment_count++;
    }

    chain_state.current_jump++;
    if (chain_state.current_jump >= chain_state.num_targets) {
      chain_state.active = 0;
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

  for (int i = 0; i < weapon_count; i++) {
    if (slots[i].type == WEAPON_NONE) continue;

    slots[i].timer += dt;

    if (slots[i].timer >= slots[i].cooldown) {
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
  // Draw spin visual
  if (spin_visual_timer > 0.0f) {
    float alpha_pct = spin_visual_timer / SPIN_VISUAL_DURATION;
    int alpha = (int)(200.0f * alpha_pct);
    if (alpha > 255) alpha = 255;

    float cx = player_get_x();
    float cy = player_get_y();

    float ring_progress = 1.0f - alpha_pct;
    float r = SPIN_RADIUS * (0.5f + 0.5f * ring_progress);
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
    float current_radius = BOMB_EXPLOSION_RADIUS * (0.3f + 0.7f * p);
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

  // Draw orbit orb
  if (orbit_state.is_active) {
    float px = player_get_x();
    float py = player_get_y();
    float orb_x = px + cosf(orbit_state.angle) * ORBIT_RADIUS - ORBIT_ORB_SIZE / 2.0f;
    float orb_y = py + sinf(orbit_state.angle) * ORBIT_RADIUS - ORBIT_ORB_SIZE / 2.0f;

    // Glow behind orb
    a_DrawFilledRect(
      (aRectf_t){orb_x - 2, orb_y - 2, ORBIT_ORB_SIZE + 4, ORBIT_ORB_SIZE + 4},
      (aColor_t){255, 160, 50, 80}
    );
    // Solid orb
    a_DrawFilledRect(
      (aRectf_t){orb_x, orb_y, ORBIT_ORB_SIZE, ORBIT_ORB_SIZE},
      (aColor_t){255, 160, 50, 255}
    );
  }
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
  if (slots[slot].cooldown <= 0.0f) return 1.0f;
  return slots[slot].timer / slots[slot].cooldown;
}

int weapons_get_count(void)
{
  return weapon_count;
}
