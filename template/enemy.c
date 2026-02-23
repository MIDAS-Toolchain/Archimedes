#include "enemy.h"
#include "drops.h"
#include "weapons.h"
#include "player_actions.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Constants
// ============================================================================

#define ENEMY_SPEED 100.0f
#define ENEMY_SIZE 16.0f
#define ENEMY_RADIUS 8.0f
#define ENEMY_HITS_TO_KILL 5

// Knockback
#define KNOCKBACK_DURATION 0.1f
#define KNOCKBACK_STRENGTH 20.0f
#define KILL_KNOCKBACK_MIN 1.5f
#define KILL_KNOCKBACK_RANGE 0.75f

// Blood particles
#define BLOOD_SPILL_DURATION 0.2f
#define BLOOD_PARTICLE_SIZE 3.0f
#define BLOOD_MIN_PARTICLES 30
#define BLOOD_MAX_PARTICLES 50
#define BLOOD_LIFETIME 12.0f
#define BLOOD_FADE_START 10.0f

// AI tuning
#define PREDICTION_TIME 0.4f
#define CHARGE_DISTANCE 200.0f
#define ATTACK_RANGE 120.0f
#define AGGRO_ATTACK_RANGE 200.0f
#define ATTACK_SPEED_MULT 2.0f
#define RETREAT_SPEED_MULT 1.5f
#define SEPARATION_RADIUS 30.0f
#define SEPARATION_WEIGHT 60.0f
#define ATTACK_SEPARATION_SCALE 0.3f
#define ATTACK_SEPARATION_WEIGHT 30.0f
#define FLANK_DISTANCE 120.0f
#define FLANK_ARRIVE_DIST 20.0f
#define RETREAT_ARRIVE_DIST 10.0f
#define PLAYER_MIN_DISTANCE 17.0f
#define OFFSCREEN_MARGIN 100.0f
#define CORPSE_LIFETIME 12.0f

// ============================================================================
// Internal State
// ============================================================================

static Enemy_t* enemies = NULL;
static BloodParticle_t* blood_particles = NULL;
static int max_enemies = 0;
static int max_blood_particles = 0;
static int first_kill_dropped = 0;

extern aApp_t app;
extern aSoundEffect_t hit_sounds[5];
extern int hit_sounds_loaded;
extern aSoundEffect_t die_sound;
extern int die_loaded;

// ============================================================================
// Utility Helpers
// ============================================================================

static float lerp(float a, float b, float t)
{
  return a + (b - a) * t;
}

/** Calculate distance between two points */
static float dist_between(float x1, float y1, float x2, float y2)
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  return sqrtf(dx * dx + dy * dy);
}

/** Normalize a vector in-place, returns the original length */
static float normalize(float* x, float* y)
{
  float len = sqrtf((*x) * (*x) + (*y) * (*y));
  if (len > 0.1f) {
    *x /= len;
    *y /= len;
  }
  return len;
}

/** Check if position is too far off screen */
static int is_offscreen(float x, float y)
{
  return x < -OFFSCREEN_MARGIN || x > SCREEN_WIDTH + OFFSCREEN_MARGIN ||
         y < -OFFSCREEN_MARGIN || y > SCREEN_HEIGHT + OFFSCREEN_MARGIN;
}

/**
 * Calculate separation force from nearby enemies.
 * Prevents clumping by pushing enemies away from each other.
 * scale_factor lets attacking enemies care less about separation.
 */
static void calc_separation(int self_index, float* out_x, float* out_y, float scale_factor)
{
  *out_x = 0.0f;
  *out_y = 0.0f;

  Enemy_t* self = &enemies[self_index];

  for (int j = 0; j < max_enemies; j++) {
    if (j == self_index || !enemies[j].active) continue;
    if (enemies[j].state == ENEMY_STATE_CORPSE || enemies[j].state == ENEMY_STATE_HIT_KNOCKBACK) continue;

    float dx = self->x - enemies[j].x;
    float dy = self->y - enemies[j].y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < SEPARATION_RADIUS && dist > 0.1f) {
      float strength = (SEPARATION_RADIUS - dist) / SEPARATION_RADIUS * scale_factor;
      *out_x += (dx / dist) * strength;
      *out_y += (dy / dist) * strength;
    }
  }
}

/**
 * Move enemy toward a target at a given speed, with separation baked in.
 * This is the core movement pattern shared by ALIVE, REPOSITIONING, and ATTACKING.
 */
static void move_toward(Enemy_t* e, float target_x, float target_y,
                        float speed, float sep_x, float sep_y,
                        float sep_weight, float dt)
{
  float dx = (target_x - (e->x + ENEMY_RADIUS)) + sep_x * sep_weight;
  float dy = (target_y - (e->y + ENEMY_RADIUS)) + sep_y * sep_weight;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist > 0.1f) {
    e->vx = (dx / dist) * speed;
    e->vy = (dy / dist) * speed;
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }

  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

/**
 * Push enemy out of the player's collision radius.
 * Returns 1 if a collision occurred, 0 otherwise.
 */
static int resolve_player_collision(Enemy_t* e, float player_x, float player_y)
{
  float dx = (e->x + ENEMY_RADIUS) - player_x;
  float dy = (e->y + ENEMY_RADIUS) - player_y;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < PLAYER_MIN_DISTANCE && dist > 0.1f) {
    float overlap = PLAYER_MIN_DISTANCE - dist;
    e->x += (dx / dist) * overlap;
    e->y += (dy / dist) * overlap;
    return 1;
  }
  return 0;
}

/** Start a knockback tween from current position in bullet direction */
static void start_knockback(Enemy_t* e, float bullet_vx, float bullet_vy, float knockback_dist)
{
  float bx = bullet_vx;
  float by = bullet_vy;
  float speed = normalize(&bx, &by);

  e->knockback_start_x = e->x;
  e->knockback_start_y = e->y;

  if (speed > 0.1f) {
    e->knockback_target_x = e->x + bx * knockback_dist;
    e->knockback_target_y = e->y + by * knockback_dist;
  } else {
    e->knockback_target_x = e->x;
    e->knockback_target_y = e->y;
  }

  e->knockback_timer = 0.0f;
  e->state = ENEMY_STATE_HIT_KNOCKBACK;
}

/** Transition enemy into the REPOSITIONING state with random angle */
static void enter_reposition(Enemy_t* e)
{
  e->state = ENEMY_STATE_REPOSITIONING;
  e->target_angle = RANDF(0, 2.0f * PI);
  e->reposition_duration = RANDF(2.0f, 5.0f);
  e->stuck_timer = 0.0f;
}

// ============================================================================
// Initialization
// ============================================================================

void enemy_init(int max_enemy_count, int max_blood_count)
{
  max_enemies = max_enemy_count;
  max_blood_particles = max_blood_count;
  enemies = (Enemy_t*)calloc(max_enemies, sizeof(Enemy_t));
  blood_particles = (BloodParticle_t*)calloc(max_blood_particles, sizeof(BloodParticle_t));
  first_kill_dropped = 0;
}

void enemy_cleanup(void)
{
  free(enemies);
  enemies = NULL;
  free(blood_particles);
  blood_particles = NULL;
}

// ============================================================================
// Spawning
// ============================================================================

int enemy_spawn(float x, float y)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) {
      enemies[i] = (Enemy_t){
        .x = x, .y = y,
        .target_angle = RANDF(0, 2.0f * PI),
        .state = ENEMY_STATE_ALIVE,
        .active = 1,
        .last_distance_to_player = 9999.0f,
        .reposition_timer = RANDF(2.0f, 5.0f),
      };
      return i;
    }
  }
  return -1;
}

// ============================================================================
// Blood Particle System
// ============================================================================

static void spawn_blood_splatter(float x, float y, float bullet_vx, float bullet_vy)
{
  int count = BLOOD_MIN_PARTICLES + (rand() % (BLOOD_MAX_PARTICLES - BLOOD_MIN_PARTICLES + 1));

  float dir_x = bullet_vx;
  float dir_y = bullet_vy;
  normalize(&dir_x, &dir_y);

  for (int i = 0; i < count; i++) {
    for (int j = 0; j < max_blood_particles; j++) {
      if (!blood_particles[j].active) {
        float offset = RANDF(12.0f, 24.0f);
        blood_particles[j].x = x + ENEMY_RADIUS + dir_x * offset;
        blood_particles[j].y = y + ENEMY_RADIUS + dir_y * offset;

        // Rotate bullet direction by a random spread angle (120 degree cone)
        float speed = RANDF(150.0f, 400.0f);
        float angle = RANDF(-PI * 0.6f, PI * 0.6f);
        float c = cosf(angle);
        float s = sinf(angle);
        blood_particles[j].vx = (dir_x * c - dir_y * s) * speed;
        blood_particles[j].vy = (dir_x * s + dir_y * c) * speed;

        blood_particles[j].lifetime = 0.0f;
        blood_particles[j].active = 1;
        blood_particles[j].frozen = 0;
        break;
      }
    }
  }
}

static void update_blood_particles(float dt)
{
  for (int i = 0; i < max_blood_particles; i++) {
    BloodParticle_t* p = &blood_particles[i];
    if (!p->active) continue;

    p->lifetime += dt;

    if (p->lifetime >= BLOOD_LIFETIME) {
      p->active = 0;
      continue;
    }

    // Freeze physics after spill animation completes
    if (!p->frozen && p->lifetime >= BLOOD_SPILL_DURATION) {
      p->frozen = 1;
      p->vx = 0.0f;
      p->vy = 0.0f;
    }

    if (!p->frozen) {
      p->x += p->vx * dt;
      p->y += p->vy * dt;
      p->vx *= 0.95f;
      p->vy *= 0.95f;
      p->vy += 200.0f * dt; // gravity
    }

    if (is_offscreen(p->x, p->y)) {
      p->active = 0;
    }
  }
}

static void draw_blood_particles(void)
{
  for (int i = 0; i < max_blood_particles; i++) {
    BloodParticle_t* p = &blood_particles[i];
    if (!p->active) continue;

    int alpha = 255;
    if (p->lifetime > BLOOD_FADE_START) {
      float fade = (p->lifetime - BLOOD_FADE_START) / (BLOOD_LIFETIME - BLOOD_FADE_START);
      alpha = (int)(255 * (1.0f - fade));
      if (alpha < 0) alpha = 0;
    }

    a_DrawFilledRect(
      (aRectf_t){p->x, p->y, BLOOD_PARTICLE_SIZE, BLOOD_PARTICLE_SIZE},
      (aColor_t){139, 0, 0, alpha}
    );
  }
}

// ============================================================================
// Hit and Knockback
// ============================================================================

void enemy_hit(int enemy_index, float bullet_vx, float bullet_vy)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return;
  if (!enemies[enemy_index].active) return;

  Enemy_t* e = &enemies[enemy_index];
  if (e->state == ENEMY_STATE_CORPSE) return;

  e->hit_count++;

  float knockback_dist = KNOCKBACK_STRENGTH;
  int killed = (e->hit_count >= ENEMY_HITS_TO_KILL);

  if (killed) {
    // Killing blow gets extra knockback
    knockback_dist *= KILL_KNOCKBACK_MIN + RANDF(0.0f, KILL_KNOCKBACK_RANGE);

    // Store bullet velocity for blood splatter direction after knockback
    e->vx = bullet_vx;
    e->vy = bullet_vy;

    if (die_loaded) {
      aAudioOptions_t opts = {
        .channel = AUDIO_CHANNEL_ENEMY, .volume = 96,
        .loops = 0, .fade_ms = 0, .interrupt = 0
      };
      a_AudioPlaySound(&die_sound, &opts);
    }
  } else {
    if (hit_sounds_loaded) {
      aAudioOptions_t opts = {
        .channel = AUDIO_CHANNEL_ENEMY, .volume = 64,
        .loops = 0, .fade_ms = 0, .interrupt = 0
      };
      a_AudioPlaySound(&hit_sounds[rand() % 5], &opts);
    }
  }

  start_knockback(e, bullet_vx, bullet_vy, knockback_dist);
}

// ============================================================================
// Collision
// ============================================================================

int enemy_check_collision(float x, float y, float radius)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active || enemies[i].state == ENEMY_STATE_CORPSE) continue;

    float dist = dist_between(x + radius, y + radius,
                              enemies[i].x + ENEMY_RADIUS, enemies[i].y + ENEMY_RADIUS);
    if (dist < (radius + ENEMY_RADIUS)) {
      return i;
    }
  }
  return -1;
}

// ============================================================================
// State Update Handlers
// ============================================================================

static void update_knockback(Enemy_t* e, float dt)
{
  e->knockback_timer += dt;

  if (e->knockback_timer >= KNOCKBACK_DURATION) {
    e->x = e->knockback_target_x;
    e->y = e->knockback_target_y;

    if (e->hit_count >= ENEMY_HITS_TO_KILL) {
      e->state = ENEMY_STATE_CORPSE;
      e->death_timer = 0.0f;
      spawn_blood_splatter(e->x, e->y, e->vx, e->vy);

      if (!first_kill_dropped) {
        // Pick a random weapon the player doesn't have
        WeaponType_t pool[5];
        int pool_count = 0;
        if (!weapons_has(WEAPON_WAND))  pool[pool_count++] = WEAPON_WAND;
        if (!weapons_has(WEAPON_SPIN))  pool[pool_count++] = WEAPON_SPIN;
        if (!weapons_has(WEAPON_CHAIN)) pool[pool_count++] = WEAPON_CHAIN;
        if (!weapons_has(WEAPON_ORBIT)) pool[pool_count++] = WEAPON_ORBIT;
        if (!weapons_has(WEAPON_BOMB))  pool[pool_count++] = WEAPON_BOMB;
        if (pool_count > 0) {
          drops_spawn(e->x, e->y, pool[rand() % pool_count]);
        }
        first_kill_dropped = 1;
      }
    } else if (e->aggro && !player_is_invincible()) {
      // Aggro'd enemies jump straight back into attack mode
      e->state = ENEMY_STATE_ATTACKING;
      e->attack_duration = RANDF(1.0f, 4.0f);
      e->flank_x = e->x;
      e->flank_y = e->y;
    } else {
      e->state = ENEMY_STATE_ALIVE;
    }
  } else {
    float t = e->knockback_timer / KNOCKBACK_DURATION;
    e->x = lerp(e->knockback_start_x, e->knockback_target_x, t);
    e->y = lerp(e->knockback_start_y, e->knockback_target_y, t);
  }
}

static void update_alive(Enemy_t* e, int index, float dt,
                         float player_x, float player_y,
                         float player_vx, float player_vy)
{
  float dist = dist_between(e->x + ENEMY_RADIUS, e->y + ENEMY_RADIUS, player_x, player_y);

  // Close enough? Switch to attack charge (but not while player is invincible)
  // Aggro'd enemies re-engage from further away
  float engage_range = e->aggro ? AGGRO_ATTACK_RANGE : ATTACK_RANGE;
  if (dist <= engage_range && !player_is_invincible()) {
    e->state = ENEMY_STATE_ATTACKING;
    e->aggro = 1;
    e->attack_duration = RANDF(1.0f, 4.0f);
    e->flank_x = e->x;
    e->flank_y = e->y;
    return;
  }

  // Pick a target: predict player position when far, charge direct when close
  float target_x, target_y;
  if (dist > CHARGE_DISTANCE) {
    target_x = player_x + player_vx * PREDICTION_TIME + RANDF(-30.0f, 30.0f);
    target_y = player_y + player_vy * PREDICTION_TIME + RANDF(-30.0f, 30.0f);
  } else {
    target_x = player_x;
    target_y = player_y;
  }

  float sep_x, sep_y;
  calc_separation(index, &sep_x, &sep_y, 1.0f);
  move_toward(e, target_x, target_y, ENEMY_SPEED, sep_x, sep_y, SEPARATION_WEIGHT, dt);
  resolve_player_collision(e, player_x, player_y);

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_repositioning(Enemy_t* e, int index, float dt,
                                 float player_x, float player_y)
{
  e->reposition_duration -= dt;

  // Target: a point on a circle around the player
  float target_x = player_x + cosf(e->target_angle) * FLANK_DISTANCE;
  float target_y = player_y + sinf(e->target_angle) * FLANK_DISTANCE;

  float dist_to_target = dist_between(e->x + ENEMY_RADIUS, e->y + ENEMY_RADIUS,
                                      target_x, target_y);

  // Arrived or timed out? Aggro'd enemies jump back to attacking, others resume pursuit
  if (dist_to_target < FLANK_ARRIVE_DIST || e->reposition_duration <= 0.0f) {
    if (e->aggro && !player_is_invincible()) {
      e->state = ENEMY_STATE_ATTACKING;
      e->attack_duration = RANDF(1.0f, 4.0f);
      e->flank_x = e->x;
      e->flank_y = e->y;
    } else {
      e->state = ENEMY_STATE_ALIVE;
    }
    return;
  }

  float sep_x, sep_y;
  calc_separation(index, &sep_x, &sep_y, 1.0f);
  move_toward(e, target_x, target_y, ENEMY_SPEED, sep_x, sep_y, SEPARATION_WEIGHT, dt);
  resolve_player_collision(e, player_x, player_y);

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_attacking(Enemy_t* e, int index, float dt,
                             float player_x, float player_y)
{
  e->attack_duration -= dt;

  // Player became invincible? Give up the attack and reposition
  if (player_is_invincible()) {
    enter_reposition(e);
    return;
  }

  // Ran out of energy? Give up and reposition
  if (e->attack_duration <= 0.0f) {
    enter_reposition(e);
    return;
  }

  float sep_x, sep_y;
  calc_separation(index, &sep_x, &sep_y, ATTACK_SEPARATION_SCALE);
  move_toward(e, player_x, player_y,
              ENEMY_SPEED * ATTACK_SPEED_MULT,
              sep_x, sep_y, ATTACK_SEPARATION_WEIGHT, dt);

  // If we hit the player, deal damage and bounce back
  if (resolve_player_collision(e, player_x, player_y)) {
    player_take_damage(20);
    e->state = ENEMY_STATE_RETREAT;
  }

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_retreat(Enemy_t* e, float dt)
{
  float dist = dist_between(e->x, e->y, e->flank_x, e->flank_y);

  if (dist < RETREAT_ARRIVE_DIST) {
    enter_reposition(e);
    return;
  }

  // Move back to flanking position at 1.5x speed (no separation needed)
  float dx = e->flank_x - e->x;
  float dy = e->flank_y - e->y;
  normalize(&dx, &dy);

  e->vx = dx * ENEMY_SPEED * RETREAT_SPEED_MULT;
  e->vy = dy * ENEMY_SPEED * RETREAT_SPEED_MULT;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  if (is_offscreen(e->x, e->y)) e->active = 0;
}

static void update_corpse(Enemy_t* e, float dt)
{
  e->death_timer += dt;
  if (e->death_timer >= CORPSE_LIFETIME) {
    e->active = 0;
  }
}

// ============================================================================
// Update (main loop)
// ============================================================================

void enemy_update(float dt, float player_x, float player_y,
                  float player_vx, float player_vy)
{
  update_blood_particles(dt);

  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;

    Enemy_t* e = &enemies[i];

    switch (e->state) {
      case ENEMY_STATE_HIT_KNOCKBACK: update_knockback(e, dt);                                  break;
      case ENEMY_STATE_ALIVE:         update_alive(e, i, dt, player_x, player_y, player_vx, player_vy); break;
      case ENEMY_STATE_REPOSITIONING: update_repositioning(e, i, dt, player_x, player_y);       break;
      case ENEMY_STATE_ATTACKING:     update_attacking(e, i, dt, player_x, player_y);           break;
      case ENEMY_STATE_RETREAT:       update_retreat(e, dt);                                     break;
      case ENEMY_STATE_CORPSE:        update_corpse(e, dt);                                      break;
    }
  }
}

// ============================================================================
// Drawing
// ============================================================================

void enemy_draw(void)
{
  draw_blood_particles();

  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;

    Enemy_t* e = &enemies[i];

    // Color shifts green -> yellow -> red based on damage taken
    int r = 0, g = 255, b = 0;
    if (e->hit_count >= 1) {
      float damage_pct = e->hit_count / (float)ENEMY_HITS_TO_KILL;
      r = (int)(damage_pct * 255.0f);
      g = (int)(255.0f - damage_pct * 255.0f);
      if (r > 255) r = 255;
      if (g < 0) g = 0;
    }

    // Corpses fade out in their final 2 seconds
    int alpha = 255;
    if (e->state == ENEMY_STATE_CORPSE && e->death_timer > BLOOD_FADE_START) {
      float fade = (e->death_timer - BLOOD_FADE_START) / (CORPSE_LIFETIME - BLOOD_FADE_START);
      alpha = (int)(255 * (1.0f - fade));
      if (alpha < 0) alpha = 0;
    }

    a_DrawFilledRect(
      (aRectf_t){e->x, e->y, ENEMY_SIZE, ENEMY_SIZE},
      (aColor_t){r, g, b, alpha}
    );

    // State indicator letter (debug visualization)
    const char* label = NULL;
    switch (e->state) {
      case ENEMY_STATE_ATTACKING:      label = "C"; break;
      case ENEMY_STATE_REPOSITIONING:  label = "F"; break;
      case ENEMY_STATE_RETREAT:        label = "R"; break;
      case ENEMY_STATE_HIT_KNOCKBACK:  label = "!"; break;
      default: break;
    }

    if (label) {
      aTextStyle_t style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {0, 0, 0, (uint8_t)alpha},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.6f
      };
      int lx = (int)(e->x + ENEMY_SIZE / 2);
      int ly = (int)(e->y - 4);

      // Faux-bold: draw at slight offsets then draw main on top
      a_DrawText(label, lx - 1, ly, style);
      a_DrawText(label, lx + 1, ly, style);
      a_DrawText(label, lx, ly - 1, style);
      a_DrawText(label, lx, ly + 1, style);
      a_DrawText(label, lx, ly, style);
    }
  }
}

// ============================================================================
// Getters
// ============================================================================

void enemy_get_position(int enemy_index, float* out_x, float* out_y)
{
  if (enemy_index >= 0 && enemy_index < max_enemies && enemies[enemy_index].active) {
    *out_x = enemies[enemy_index].x;
    *out_y = enemies[enemy_index].y;
  } else {
    *out_x = 0.0f;
    *out_y = 0.0f;
  }
}

EnemyState_t enemy_get_state(int enemy_index)
{
  if (enemy_index >= 0 && enemy_index < max_enemies && enemies[enemy_index].active) {
    return enemies[enemy_index].state;
  }
  return ENEMY_STATE_CORPSE;
}

int enemy_is_active(int enemy_index)
{
  if (enemy_index >= 0 && enemy_index < max_enemies) {
    return enemies[enemy_index].active;
  }
  return 0;
}

int enemy_get_count(void)
{
  int count = 0;
  for (int i = 0; i < max_enemies; i++) {
    if (enemies[i].active && enemies[i].state != ENEMY_STATE_CORPSE) {
      count++;
    }
  }
  return count;
}

int enemy_get_max_count(void)
{
  return max_enemies;
}

int enemy_is_alive(int enemy_index)
{
  if (enemy_index < 0 || enemy_index >= max_enemies) return 0;
  if (!enemies[enemy_index].active) return 0;
  EnemyState_t s = enemies[enemy_index].state;
  return (s != ENEMY_STATE_CORPSE && s != ENEMY_STATE_HIT_KNOCKBACK);
}

int enemy_find_cluster_target(float radius, float player_x, float player_y)
{
  int best = -1;
  int best_neighbors = -1;
  float best_dist = 9999.0f;

  for (int i = 0; i < max_enemies; i++) {
    if (!enemy_is_alive(i)) continue;

    // Count neighbors within chain radius
    int neighbors = 0;
    for (int j = 0; j < max_enemies; j++) {
      if (j == i || !enemy_is_alive(j)) continue;
      float d = dist_between(
        enemies[i].x + ENEMY_RADIUS, enemies[i].y + ENEMY_RADIUS,
        enemies[j].x + ENEMY_RADIUS, enemies[j].y + ENEMY_RADIUS
      );
      if (d <= radius) neighbors++;
    }

    float d_player = dist_between(
      enemies[i].x + ENEMY_RADIUS, enemies[i].y + ENEMY_RADIUS,
      player_x, player_y
    );

    if (neighbors > best_neighbors ||
        (neighbors == best_neighbors && d_player < best_dist)) {
      best = i;
      best_neighbors = neighbors;
      best_dist = d_player;
    }
  }

  return best;
}

int enemy_find_cluster_position(float radius, float player_x, float player_y,
                                float lead_time, float* out_x, float* out_y)
{
  int idx = enemy_find_cluster_target(radius, player_x, player_y);
  if (idx < 0) return 0;
  *out_x = enemies[idx].x + ENEMY_RADIUS + enemies[idx].vx * lead_time;
  *out_y = enemies[idx].y + ENEMY_RADIUS + enemies[idx].vy * lead_time;
  return 1;
}
