#include "enemy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Constants
#define ENEMY_SPEED 100.0f
#define ENEMY_SIZE 16.0f
#define ENEMY_RADIUS 8.0f
#define KNOCKBACK_DURATION 0.1f
#define KNOCKBACK_STRENGTH 20.0f  // Base knockback (halved)
#define BLOOD_SPILL_DURATION 0.2f
#define BLOOD_PARTICLE_SIZE 3.0f

// Enemy AI constants
static float prediction_time = 0.4f;          // How far ahead to predict player movement
static float charge_distance = 200.0f;        // Distance at which enemies charge directly
static float retarget_interval = 0.3f;        // How often they recalculate pursuit
static float separation_force_radius = 30.0f; // Distance to avoid other enemies

// Dynamic arrays
static Enemy_t* enemies = NULL;
static BloodParticle_t* blood_particles = NULL;
static int max_enemies = 0;
static int max_blood_particles = 0;

// App reference (from main.c)
extern aApp_t app;

// Audio (extern from main.c)
extern aSoundEffect_t hit_sounds[5];
extern int hit_sounds_loaded;
extern aSoundEffect_t die_sound;
extern int die_loaded;

// Linear interpolation
static float lerp(float a, float b, float t) {
  return a + (b - a) * t;
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

  printf("Enemy system initialized: %d enemies, %d blood particles\n", max_enemies, max_blood_particles);
}

void enemy_cleanup(void)
{
  if (enemies) {
    free(enemies);
    enemies = NULL;
  }
  if (blood_particles) {
    free(blood_particles);
    blood_particles = NULL;
  }
}

// ============================================================================
// Spawning
// ============================================================================

int enemy_spawn(float x, float y)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) {
      enemies[i].x = x;
      enemies[i].y = y;
      enemies[i].vx = 0.0f;
      enemies[i].vy = 0.0f;
      enemies[i].target_angle = RANDF(0, 2.0f * PI);
      enemies[i].next_retarget = retarget_interval;
      enemies[i].state = ENEMY_STATE_ALIVE;
      enemies[i].active = 1;
      enemies[i].hit_count = 0;
      enemies[i].death_timer = 0.0f;
      enemies[i].knockback_timer = 0.0f;
      enemies[i].last_distance_to_player = 9999.0f;
      enemies[i].stuck_timer = 0.0f;
      enemies[i].reposition_timer = RANDF(2.0f, 5.0f); // Random initial reposition time
      enemies[i].reposition_duration = 0.0f;
      enemies[i].attack_duration = 0.0f;
      return i;
    }
  }
  return -1; // No free slot
}

// ============================================================================
// Blood Particle System
// ============================================================================

static void spawn_blood_splatter(float x, float y, float bullet_vx, float bullet_vy)
{
  int particle_count = 30 + (rand() % 21); // 30-50 particles

  // Normalize bullet direction
  float bullet_speed = sqrtf(bullet_vx * bullet_vx + bullet_vy * bullet_vy);
  float dir_x = (bullet_speed > 0.1f) ? bullet_vx / bullet_speed : 1.0f;
  float dir_y = (bullet_speed > 0.1f) ? bullet_vy / bullet_speed : 0.0f;

  for (int i = 0; i < particle_count; i++) {
    // Find inactive particle
    for (int j = 0; j < max_blood_particles; j++) {
      if (!blood_particles[j].active) {
        // Spawn particles OUTSIDE the enemy body in bullet direction
        float spawn_offset = RANDF(12.0f, 24.0f);
        blood_particles[j].x = x + ENEMY_RADIUS + dir_x * spawn_offset;
        blood_particles[j].y = y + ENEMY_RADIUS + dir_y * spawn_offset;

        // Particle velocity: bullet direction + wide spread for explosion effect
        float speed_base = RANDF(150.0f, 400.0f);
        float spread_angle = RANDF(-PI * 0.6f, PI * 0.6f); // 120° spread
        float cos_spread = cosf(spread_angle);
        float sin_spread = sinf(spread_angle);

        // Rotate bullet direction by spread angle
        blood_particles[j].vx = (dir_x * cos_spread - dir_y * sin_spread) * speed_base;
        blood_particles[j].vy = (dir_x * sin_spread + dir_y * cos_spread) * speed_base;

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
    if (blood_particles[i].active) {
      blood_particles[i].lifetime += dt;

      // Deactivate after 12 seconds
      if (blood_particles[i].lifetime >= 12.0f) {
        blood_particles[i].active = 0;
        continue;
      }

      // Freeze physics after 0.2s spill animation
      if (blood_particles[i].lifetime >= BLOOD_SPILL_DURATION) {
        blood_particles[i].frozen = 1;
        blood_particles[i].vx = 0.0f;
        blood_particles[i].vy = 0.0f;
      }

      // Only apply physics if not frozen
      if (!blood_particles[i].frozen) {
        // Apply velocity
        blood_particles[i].x += blood_particles[i].vx * dt;
        blood_particles[i].y += blood_particles[i].vy * dt;

        // Apply friction (slow down over time)
        blood_particles[i].vx *= 0.95f;
        blood_particles[i].vy *= 0.95f;

        // Apply gravity
        blood_particles[i].vy += 200.0f * dt;
      }

      // Deactivate if off screen
      if (blood_particles[i].x < -10 || blood_particles[i].x > SCREEN_WIDTH + 10 ||
          blood_particles[i].y < -10 || blood_particles[i].y > SCREEN_HEIGHT + 10) {
        blood_particles[i].active = 0;
      }
    }
  }
}

static void draw_blood_particles(void)
{
  for (int i = 0; i < max_blood_particles; i++) {
    if (blood_particles[i].active) {
      // Fade starts at 10 seconds, fades over 2 seconds
      int alpha = 255;
      if (blood_particles[i].lifetime > 10.0f) {
        float fade_progress = (blood_particles[i].lifetime - 10.0f) / 2.0f;
        alpha = (int)(255 * (1.0f - fade_progress));
        if (alpha < 0) alpha = 0;
      }

      a_DrawFilledRect(
        (aRectf_t){blood_particles[i].x, blood_particles[i].y, BLOOD_PARTICLE_SIZE, BLOOD_PARTICLE_SIZE},
        (aColor_t){139, 0, 0, alpha} // Dark red
      );
    }
  }
}

// ============================================================================
// Enemy Hit and Knockback
// ============================================================================

void enemy_hit(int enemy_index, float bullet_vx, float bullet_vy)
{
  if (enemy_index < 0 || enemy_index >= max_enemies || !enemies[enemy_index].active) {
    return;
  }

  Enemy_t* e = &enemies[enemy_index];

  // Ignore hits on corpses
  if (e->state == ENEMY_STATE_CORPSE) {
    return;
  }

  printf("Enemy %d hit!\n", enemy_index);
  e->hit_count++;

  // Calculate knockback direction
  float bullet_speed = sqrtf(bullet_vx * bullet_vx + bullet_vy * bullet_vy);
  float knockback_dist = KNOCKBACK_STRENGTH;

  // Check for death (5 hits = death)
  if (e->hit_count >= 5) {
    printf("💀 ENEMY %d DIED! 💀\n", enemy_index);

    // Killing blow gets 1.5x-2.25x knockback multiplier (50%-125% bonus)
    float kill_multiplier = 1.5f + RANDF(0.0f, 0.75f);
    knockback_dist *= kill_multiplier;
    printf("Killing blow knockback: %.1fx multiplier\n", kill_multiplier);

    // Start knockback tween
    e->knockback_start_x = e->x;
    e->knockback_start_y = e->y;
    if (bullet_speed > 0.1f) {
      e->knockback_target_x = e->x + (bullet_vx / bullet_speed) * knockback_dist;
      e->knockback_target_y = e->y + (bullet_vy / bullet_speed) * knockback_dist;
    } else {
      e->knockback_target_x = e->x;
      e->knockback_target_y = e->y;
    }
    e->knockback_timer = 0.0f;
    e->state = ENEMY_STATE_HIT_KNOCKBACK;

    // Spawn blood AFTER knockback completes (will be spawned in update when timer finishes)
    // Store bullet velocity for blood spawn
    e->vx = bullet_vx;
    e->vy = bullet_vy;

    // Play die sound
    if (die_loaded) {
      aAudioOptions_t die_opts = {
        .channel = AUDIO_CHANNEL_ENEMY,
        .volume = 96,  // 75%
        .loops = 0,
        .fade_ms = 0,
        .interrupt = 0
      };
      a_AudioPlaySound(&die_sound, &die_opts);
    }
  } else {
    // Normal hit - apply knockback
    e->knockback_start_x = e->x;
    e->knockback_start_y = e->y;
    if (bullet_speed > 0.1f) {
      e->knockback_target_x = e->x + (bullet_vx / bullet_speed) * knockback_dist;
      e->knockback_target_y = e->y + (bullet_vy / bullet_speed) * knockback_dist;
    } else {
      e->knockback_target_x = e->x;
      e->knockback_target_y = e->y;
    }
    e->knockback_timer = 0.0f;
    e->state = ENEMY_STATE_HIT_KNOCKBACK;

    // Play random hit sound
    if (hit_sounds_loaded) {
      int random_hit = rand() % 5;
      aAudioOptions_t hit_opts = {
        .channel = AUDIO_CHANNEL_ENEMY,
        .volume = 64,  // 50%
        .loops = 0,
        .fade_ms = 0,
        .interrupt = 0
      };
      a_AudioPlaySound(&hit_sounds[random_hit], &hit_opts);
    }
  }
}

// ============================================================================
// Collision
// ============================================================================

int enemy_check_collision(float x, float y, float radius)
{
  for (int i = 0; i < max_enemies; i++) {
    if (enemies[i].active && enemies[i].state != ENEMY_STATE_CORPSE) {
      float dx = (x + radius) - (enemies[i].x + ENEMY_RADIUS);
      float dy = (y + radius) - (enemies[i].y + ENEMY_RADIUS);
      float dist = sqrtf(dx * dx + dy * dy);

      if (dist < (radius + ENEMY_RADIUS)) {
        return i;
      }
    }
  }
  return -1;
}

// ============================================================================
// Update
// ============================================================================

void enemy_update(float dt, float player_x, float player_y, float player_vx, float player_vy)
{
  // Update blood particles first
  update_blood_particles(dt);

  // Update all enemies
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) {
      continue;
    }

    Enemy_t* e = &enemies[i];

    // Handle knockback state
    if (e->state == ENEMY_STATE_HIT_KNOCKBACK) {
      e->knockback_timer += dt;

      if (e->knockback_timer >= KNOCKBACK_DURATION) {
        // Knockback finished
        e->x = e->knockback_target_x;
        e->y = e->knockback_target_y;

        // Check if this was a killing blow
        if (e->hit_count >= 5) {
          // Transition to corpse
          e->state = ENEMY_STATE_CORPSE;
          e->death_timer = 0.0f;

          // Spawn blood NOW at knockback final position
          spawn_blood_splatter(e->x, e->y, e->vx, e->vy);
        } else {
          // Return to alive state
          e->state = ENEMY_STATE_ALIVE;
        }
      } else {
        // Tween position
        float t = e->knockback_timer / KNOCKBACK_DURATION;
        e->x = lerp(e->knockback_start_x, e->knockback_target_x, t);
        e->y = lerp(e->knockback_start_y, e->knockback_target_y, t);
      }
    }

    // Handle alive state (AI movement - smart pursuit)
    if (e->state == ENEMY_STATE_ALIVE) {
      // Calculate distance to player
      float to_player_x = player_x - (e->x + ENEMY_RADIUS);
      float to_player_y = player_y - (e->y + ENEMY_RADIUS);
      float dist_to_player = sqrtf(to_player_x * to_player_x + to_player_y * to_player_y);

      // If within attack range (120px), immediately ATTACK!
      float attack_range = 120.0f;
      if (dist_to_player <= attack_range) {
        e->state = ENEMY_STATE_ATTACKING;
        e->attack_duration = RANDF(1.0f, 4.0f); // Give up after 1-4 seconds (run out of energy)
        e->last_distance_to_player = dist_to_player;
        e->stuck_timer = 0.0f;

        // Save current position as flanking position
        e->flank_x = e->x;
        e->flank_y = e->y;
      }

      // Predict where player will be based on their velocity
      float predicted_x = player_x + player_vx * prediction_time;
      float predicted_y = player_y + player_vy * prediction_time;

      // Target position - direct pursuit
      float target_x, target_y;
      if (dist_to_player > charge_distance) {
        // Far away - predict and intercept
        target_x = predicted_x + RANDF(-30.0f, 30.0f);
        target_y = predicted_y + RANDF(-30.0f, 30.0f);
      } else {
        // Close - charge directly at player's current position
        target_x = player_x;
        target_y = player_y;
      }

      // Calculate separation from nearby enemies (avoid clumping)
      float sep_x = 0.0f;
      float sep_y = 0.0f;

      for (int j = 0; j < max_enemies; j++) {
        if (i != j && enemies[j].active && enemies[j].state == ENEMY_STATE_ALIVE) {
          float ex = e->x - enemies[j].x;
          float ey = e->y - enemies[j].y;
          float edist = sqrtf(ex * ex + ey * ey);

          if (edist < separation_force_radius && edist > 0.1f) {
            // Push away from nearby enemies
            float push_strength = (separation_force_radius - edist) / separation_force_radius;
            sep_x += (ex / edist) * push_strength;
            sep_y += (ey / edist) * push_strength;
          }
        }
      }

      // Combine pursuit direction with separation
      float dx = (target_x - (e->x + ENEMY_RADIUS)) + sep_x * 60.0f;
      float dy = (target_y - (e->y + ENEMY_RADIUS)) + sep_y * 60.0f;
      float combined_dist = sqrtf(dx * dx + dy * dy);

      // Set velocity toward target
      if (combined_dist > 0.1f) {
        e->vx = (dx / combined_dist) * ENEMY_SPEED;
        e->vy = (dy / combined_dist) * ENEMY_SPEED;
      } else {
        e->vx = 0.0f;
        e->vy = 0.0f;
      }

      // Apply velocity
      e->x += e->vx * dt;
      e->y += e->vy * dt;

      // PLAYER COLLISION - prevent enemies from passing through player (1px gap)
      float player_dx = (e->x + ENEMY_RADIUS) - player_x;
      float player_dy = (e->y + ENEMY_RADIUS) - player_y;
      float player_dist = sqrtf(player_dx * player_dx + player_dy * player_dy);

      float min_distance = 17.0f; // Enemy radius (8) + player radius (16) - 7 overlap = ~1px gap
      if (player_dist < min_distance && player_dist > 0.1f) {
        // Push enemy away from player to maintain 1px gap
        float overlap = min_distance - player_dist;
        e->x += (player_dx / player_dist) * overlap;
        e->y += (player_dy / player_dist) * overlap;
      }

      // Deactivate if too far off screen
      if (e->x < -100 || e->x > SCREEN_WIDTH + 100 ||
          e->y < -100 || e->y > SCREEN_HEIGHT + 100) {
        e->active = 0;
      }
    }

    // Handle REPOSITIONING state - committed flanking maneuver
    if (e->state == ENEMY_STATE_REPOSITIONING) {
      // Countdown repositioning duration
      e->reposition_duration -= dt;

      // Calculate distance to player
      float to_player_x = player_x - (e->x + ENEMY_RADIUS);
      float to_player_y = player_y - (e->y + ENEMY_RADIUS);
      float dist_to_player = sqrtf(to_player_x * to_player_x + to_player_y * to_player_y);

      // Target: flanking position at 120px radius around player
      float flank_distance = 120.0f;
      float target_x = player_x + cosf(e->target_angle) * flank_distance;
      float target_y = player_y + sinf(e->target_angle) * flank_distance;

      // Calculate distance to target position
      float to_target_x = target_x - (e->x + ENEMY_RADIUS);
      float to_target_y = target_y - (e->y + ENEMY_RADIUS);
      float dist_to_target = sqrtf(to_target_x * to_target_x + to_target_y * to_target_y);

      // When reaching position OR duration expired, return to ALIVE (will attack when in range)
      if (dist_to_target < 20.0f || e->reposition_duration <= 0.0f) {
        e->state = ENEMY_STATE_ALIVE;
        e->last_distance_to_player = dist_to_player;
        e->stuck_timer = 0.0f;
      }

      // Calculate separation from nearby enemies
      float sep_x = 0.0f;
      float sep_y = 0.0f;

      for (int j = 0; j < max_enemies; j++) {
        if (i != j && enemies[j].active && (enemies[j].state == ENEMY_STATE_ALIVE || enemies[j].state == ENEMY_STATE_REPOSITIONING || enemies[j].state == ENEMY_STATE_RETREAT)) {
          float ex = e->x - enemies[j].x;
          float ey = e->y - enemies[j].y;
          float edist = sqrtf(ex * ex + ey * ey);

          if (edist < separation_force_radius && edist > 0.1f) {
            float push_strength = (separation_force_radius - edist) / separation_force_radius;
            sep_x += (ex / edist) * push_strength;
            sep_y += (ey / edist) * push_strength;
          }
        }
      }

      // Move toward flanking position with separation
      float dx = (target_x - (e->x + ENEMY_RADIUS)) + sep_x * 60.0f;
      float dy = (target_y - (e->y + ENEMY_RADIUS)) + sep_y * 60.0f;
      float combined_dist = sqrtf(dx * dx + dy * dy);

      // Set velocity toward flank position
      if (combined_dist > 0.1f) {
        e->vx = (dx / combined_dist) * ENEMY_SPEED;
        e->vy = (dy / combined_dist) * ENEMY_SPEED;
      } else {
        e->vx = 0.0f;
        e->vy = 0.0f;
      }

      // Apply velocity
      e->x += e->vx * dt;
      e->y += e->vy * dt;

      // PLAYER COLLISION - maintain 1px gap even while repositioning
      float player_dx = (e->x + ENEMY_RADIUS) - player_x;
      float player_dy = (e->y + ENEMY_RADIUS) - player_y;
      float player_dist = sqrtf(player_dx * player_dx + player_dy * player_dy);

      float min_distance = 17.0f;
      if (player_dist < min_distance && player_dist > 0.1f) {
        float overlap = min_distance - player_dist;
        e->x += (player_dx / player_dist) * overlap;
        e->y += (player_dy / player_dist) * overlap;
      }

      // Deactivate if off screen
      if (e->x < -100 || e->x > SCREEN_WIDTH + 100 ||
          e->y < -100 || e->y > SCREEN_HEIGHT + 100) {
        e->active = 0;
      }
    }

    // Handle ATTACKING state - SPEED BOOST CHARGE!
    if (e->state == ENEMY_STATE_ATTACKING) {
      // Countdown attack duration
      e->attack_duration -= dt;

      // Calculate distance to player
      float to_player_x = player_x - (e->x + ENEMY_RADIUS);
      float to_player_y = player_y - (e->y + ENEMY_RADIUS);
      float dist_to_player = sqrtf(to_player_x * to_player_x + to_player_y * to_player_y);

      // If duration expired (failed to hit player), enter REPOSITIONING
      if (e->attack_duration <= 0.0f) {
        e->state = ENEMY_STATE_REPOSITIONING;
        e->target_angle = RANDF(0, 2.0f * PI);
        e->reposition_duration = RANDF(2.0f, 5.0f);
        e->stuck_timer = 0.0f;
      }

      // Target: DIRECTLY AT PLAYER - no prediction, pure aggression
      float target_x = player_x;
      float target_y = player_y;

      // Calculate separation (minimal during attack)
      float sep_x = 0.0f;
      float sep_y = 0.0f;

      for (int j = 0; j < max_enemies; j++) {
        if (i != j && enemies[j].active && (enemies[j].state == ENEMY_STATE_ALIVE || enemies[j].state == ENEMY_STATE_REPOSITIONING || enemies[j].state == ENEMY_STATE_ATTACKING || enemies[j].state == ENEMY_STATE_RETREAT)) {
          float ex = e->x - enemies[j].x;
          float ey = e->y - enemies[j].y;
          float edist = sqrtf(ex * ex + ey * ey);

          if (edist < separation_force_radius && edist > 0.1f) {
            // Less separation during attack - they're committed!
            float push_strength = (separation_force_radius - edist) / separation_force_radius * 0.3f;
            sep_x += (ex / edist) * push_strength;
            sep_y += (ey / edist) * push_strength;
          }
        }
      }

      // Calculate direction to player with minimal separation
      float dx = (target_x - (e->x + ENEMY_RADIUS)) + sep_x * 30.0f;
      float dy = (target_y - (e->y + ENEMY_RADIUS)) + sep_y * 30.0f;
      float combined_dist = sqrtf(dx * dx + dy * dy);

      // SPEED BOOST! 2x speed during attack
      float attack_speed = ENEMY_SPEED * 2.0f;

      // Set velocity toward player at BOOSTED speed
      if (combined_dist > 0.1f) {
        e->vx = (dx / combined_dist) * attack_speed;
        e->vy = (dy / combined_dist) * attack_speed;
      } else {
        e->vx = 0.0f;
        e->vy = 0.0f;
      }

      // Apply velocity
      e->x += e->vx * dt;
      e->y += e->vy * dt;

      // PLAYER COLLISION
      float player_dx = (e->x + ENEMY_RADIUS) - player_x;
      float player_dy = (e->y + ENEMY_RADIUS) - player_y;
      float player_dist = sqrtf(player_dx * player_dx + player_dy * player_dy);

      float min_distance = 17.0f;
      if (player_dist < min_distance && player_dist > 0.1f) {
        // COLLISION! Enter RETREAT state to move back quickly
        float overlap = min_distance - player_dist;
        e->x += (player_dx / player_dist) * overlap;
        e->y += (player_dy / player_dist) * overlap;

        e->state = ENEMY_STATE_RETREAT;
        e->stuck_timer = 0.0f;
      }

      // Deactivate if off screen
      if (e->x < -100 || e->x > SCREEN_WIDTH + 100 ||
          e->y < -100 || e->y > SCREEN_HEIGHT + 100) {
        e->active = 0;
      }
    }

    // Handle RETREAT state - move back to flanking position quickly
    if (e->state == ENEMY_STATE_RETREAT) {
      // Calculate distance to flanking position
      float to_flank_x = e->flank_x - e->x;
      float to_flank_y = e->flank_y - e->y;
      float dist_to_flank = sqrtf(to_flank_x * to_flank_x + to_flank_y * to_flank_y);

      // If reached flanking position (within 10px), reposition again
      if (dist_to_flank < 10.0f) {
        e->state = ENEMY_STATE_REPOSITIONING;
        e->target_angle = RANDF(0, 2.0f * PI);
        e->reposition_duration = RANDF(2.0f, 5.0f);
        e->stuck_timer = 0.0f;
      } else {
        // Move quickly back to flanking position (1.5x speed)
        float retreat_speed = ENEMY_SPEED * 1.5f;
        if (dist_to_flank > 0.1f) {
          e->vx = (to_flank_x / dist_to_flank) * retreat_speed;
          e->vy = (to_flank_y / dist_to_flank) * retreat_speed;
        } else {
          e->vx = 0.0f;
          e->vy = 0.0f;
        }

        // Apply velocity
        e->x += e->vx * dt;
        e->y += e->vy * dt;
      }

      // Deactivate if off screen
      if (e->x < -100 || e->x > SCREEN_WIDTH + 100 ||
          e->y < -100 || e->y > SCREEN_HEIGHT + 100) {
        e->active = 0;
      }
    }

    // Handle corpse state
    if (e->state == ENEMY_STATE_CORPSE) {
      e->death_timer += dt;
      if (e->death_timer >= 12.0f) {
        e->active = 0; // Remove completely
      }
    }
  }
}

// ============================================================================
// Drawing
// ============================================================================

void enemy_draw(void)
{
  // Draw blood particles FIRST (under everything)
  draw_blood_particles();

  // Draw all enemies
  for (int i = 0; i < max_enemies; i++) {
    if (enemies[i].active) {
      Enemy_t* e = &enemies[i];

      // Calculate color based on hit count (green -> yellow -> red)
      int red = 0, green = 255, blue = 0;
      if (e->hit_count >= 1) {
        red = (int)((e->hit_count / 5.0f) * 255.0f);
        green = (int)(255.0f - (e->hit_count / 5.0f) * 255.0f);
        if (red > 255) red = 255;
        if (green < 0) green = 0;
      }

      // Calculate alpha fade for corpses
      int alpha = 255;
      if (e->state == ENEMY_STATE_CORPSE && e->death_timer > 10.0f) {
        float fade_progress = (e->death_timer - 10.0f) / 2.0f;
        alpha = (int)(255 * (1.0f - fade_progress));
        if (alpha < 0) alpha = 0;
      }

      a_DrawFilledRect(
        (aRectf_t){e->x, e->y, ENEMY_SIZE, ENEMY_SIZE},
        (aColor_t){red, green, blue, alpha}
      );
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
  return ENEMY_STATE_CORPSE; // Invalid = corpse (safe default)
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
