/**
 * player_actions.c - Player movement and combat
 *
 * Handles player character movement, shooting, and bullet physics.
 * Auto-fires at the nearest enemy once per second (magic wand style).
 */

#include <stdio.h>
#include <math.h>
#include "Archimedes.h"
#include "player_actions.h"
#include "upgrades.h"
#include "enemy.h"
#include "pickups.h"
#include "game_audio.h"
#include "stats.h"
#include "weapons.h"
#include "fire_particles.h"
#include "progress.h"
#include "snake.h"
#include "blood.h"

// ============================================================================
// Player State
// ============================================================================

// Player position and velocity (use float for smooth movement)
static float player_x = 100.0f;
static float player_y = 100.0f;
static float player_vx = 0.0f;  // Velocity X
static float player_vy = 0.0f;  // Velocity Y
static float player_speed = 200.0f; // pixels per second

// Health system
static int player_hp = 100;
static int player_max_hp = 100;
static EnemyType_t last_hit_enemy_type = ENEMY_TYPE_GRUNT;
static int last_hit_damage = 0;
static float invincibility_timer = 0.0f;
static float screen_flash_timer = 0.0f;
static float screen_shake_timer = 0.0f;
static float shield_absorb_flash_timer = 0.0f;
static float regen_timer = 0.0f;

#define PLAYER_IFRAME_DURATION   1.0f
#define SCREEN_FLASH_DURATION    0.3f
#define SCREEN_SHAKE_DURATION    0.2f
#define SCREEN_SHAKE_INTENSITY   5.0f
#define SHIELD_ABSORB_FLASH_DURATION 0.25f
#define HEAL_FLASH_DURATION      0.4f
#define HEAL_TEXT_DURATION        1.0f
#define HEAL_TEXT_RISE_SPEED     40.0f

// Hostile debuffs (from mimic)
static float hostile_slow_mult = 1.0f;
static float hostile_slow_timer = 0.0f;
static float hostile_kb_vx = 0.0f;
static float hostile_kb_vy = 0.0f;
static float hostile_kb_timer = 0.0f;
#define HOSTILE_KB_DURATION 0.15f

// Heal feedback
static float heal_flash_timer = 0.0f;
static float heal_text_timer = 0.0f;
static float heal_text_x = 0.0f;
static float heal_text_y = 0.0f;
static int heal_text_amount = 0;

// Revive (Second Wind global upgrade)
static int revive_used = 0;
#define REVIVE_IFRAME_DURATION 3.0f

// Death visual effects
static int   player_dying = 0;
static float death_vignette_timer = 0.0f;
static float death_kb_vx = 0.0f;
static float death_kb_vy = 0.0f;
static float last_hit_dx = 0.0f;  // direction FROM attacker TO player
static float last_hit_dy = -1.0f;
#define DEATH_KNOCKBACK_SPEED 480.0f
#define DEATH_KNOCKBACK_DRAG  1.5f  // per-second decay rate

// Dash system
#define DASH_COOLDOWN      3.0f
#define DASH_DURATION      0.15f
#define DASH_SPEED         800.0f
#define DASH_MAX_CHARGES   2

static float dash_cd_timers[DASH_MAX_CHARGES]; // counts up toward effective cd
static float dash_active_timer = 0.0f;    // remaining dash movement time
static float dash_failed_timer = 0.0f;    // flash timer when dash attempted on cooldown
static float dash_lockout_timer = 0.0f;   // prevents accidental double-dash from held key
#define DASH_FAILED_DURATION 0.6f
#define DASH_LOCKOUT_DURATION 0.3f
static float dash_dir_x = 0.0f;
static float dash_dir_y = 0.0f;

static float get_dash_cd(void) {
  return DASH_COOLDOWN - global_get_dash_cooldown_reduction();
}

static int get_max_charges(void) {
  return global_has_dash_extra_charge() ? 2 : 1;
}

static int get_ready_charge(void) {
  float cd = get_dash_cd();
  int max = get_max_charges();
  for (int i = 0; i < max; i++)
    if (dash_cd_timers[i] >= cd) return i;
  return -1;
}

// Dash trail (ghost afterimages)
#define DASH_TRAIL_MAX 8
typedef struct {
  float x, y;
  float alpha;
  int active;
} DashTrail_t;
static DashTrail_t dash_trail[DASH_TRAIL_MAX];
static float dash_trail_timer = 0.0f;

// Buff system
static Buff_t player_buffs[PICKUP_TYPE_COUNT];
static float player_facing_x = 0.0f;
static float player_facing_y = -1.0f; // Default facing up
static float cone_aim_x = 0.0f;
static float cone_aim_y = -1.0f;
#define CONE_LERP_SPEED 12.0f  // Radians-ish per second (fast but smooth)

// Fire cone ticking
#define FIRE_CONE_DURATION    BUFF_FIRE_CONE_DURATION
#define FIRE_CONE_TICK_RATE   0.2f
#define FIRE_CONE_MIN_RANGE   110.0f
#define FIRE_CONE_MAX_RANGE   160.0f
#define FIRE_CONE_GROWTH_INTERVAL 1.0f  // burst of growth every 1s
#define FIRE_CONE_HALF_ANGLE  0.5498f  // ~31.5 degrees (30deg * 1.05)
#define FIRE_CONE_DAMAGE      10

// Slow aura
#define SLOW_AURA_MIN_RADIUS  40.0f
#define SLOW_AURA_MAX_RADIUS  100.0f
static float slow_aura_elapsed = 0.0f;  // Total time aura has been active (grows continuously)

static float fire_cone_tick = 0.0f;
static float fire_cone_elapsed = 0.0f;  // Total time fire cone has been active (grows continuously)
static float shield_flash_timer = 0.0f;
static float shield_anim_timer = 0.0f;  // Ticks up while shield active (for pulse animation)
#define SHIELD_FLASH_DURATION 0.15f

// Per-enemy fire damage cooldown (0.75s between hits)
#define FIRE_HIT_COOLDOWN 0.75f

static float get_fire_cone_range(void)
{
  // Grows from min toward max over base duration, keeps growing beyond if stacked
  float pct = fire_cone_elapsed / FIRE_CONE_DURATION;
  return FIRE_CONE_MIN_RANGE + (FIRE_CONE_MAX_RANGE - FIRE_CONE_MIN_RANGE) * pct;
}

// ============================================================================
// Targeting
// ============================================================================

#define AUTO_FIRE_MAX_RANGE 9999.0f
#define MAX_ENEMY_SCAN 50

static float fire_cooldowns[MAX_ENEMY_SCAN];

// ============================================================================
// Audio
// ============================================================================

static aSoundEffect_t shot_sound;
static int audio_loaded = 0;
static aSoundEffect_t dash_sound;
static int dash_sound_loaded = 0;
static aSoundEffect_t damage_sound;
static int damage_sound_loaded = 0;
static aSoundEffect_t shield_hit_sound;
static int shield_hit_sound_loaded = 0;

// ============================================================================
// Bullet System
// ============================================================================

#define MAX_BULLETS 100

typedef struct {
  float x, y;       // Position
  float vx, vy;     // Velocity
  int active;       // Is this bullet alive?
  int pierce;       // Remaining pierce count (0 = destroy on hit)
  int hit_enemies[8]; // Enemy indices already hit by this bullet
  int hit_count;
  int ricochet;     // Remaining bounces (ricochet upgrade)
  int is_fragment;  // 1 = splinter fragment, don't trigger splinter again
  float lifetime;   // Seconds remaining; < 0 means infinite (normal bullet)
} Bullet_t;

static Bullet_t bullets[MAX_BULLETS];
static float bullet_speed = 600.0f; // pixels per second

// ============================================================================
// Initialization
// ============================================================================

void player_init(void)
{
  player_x = RANDF(200.0f, SCREEN_WIDTH - 200.0f);
  player_y = RANDF(200.0f, SCREEN_HEIGHT - 200.0f);
  player_max_hp = 100 + global_get_max_hp_bonus();
  player_hp = player_max_hp;
  invincibility_timer = 0.0f;
  screen_flash_timer = 0.0f;
  regen_timer = 0.0f;
  heal_flash_timer = 0.0f;
  heal_text_timer = 0.0f;
  player_dying = 0;
  revive_used = 0;
  death_vignette_timer = 0.0f;
  death_kb_vx = 0.0f;
  death_kb_vy = 0.0f;
  dash_cd_timers[0] = DASH_COOLDOWN;
  for (int i = 1; i < DASH_MAX_CHARGES; i++) dash_cd_timers[i] = 0.0f;
  dash_active_timer = 0.0f;

  // Clear buffs
  for (int i = 0; i < PICKUP_TYPE_COUNT; i++) {
    player_buffs[i].active = 0;
    player_buffs[i].duration = 0.0f;
    player_buffs[i].shield_hits = 0;
  }
  // Starting shield from global upgrade
  int start_shield = global_get_starting_shield();
  if (start_shield > 0) {
    player_buffs[PICKUP_SHIELD].active = 1;
    player_buffs[PICKUP_SHIELD].shield_hits = start_shield;
    player_buffs[PICKUP_SHIELD].duration = 999999.0f;
  }

  fire_cone_tick = 0.0f;
  fire_cone_elapsed = 0.0f;
  shield_flash_timer = 0.0f;
  shield_anim_timer = 0.0f;
  slow_aura_elapsed = 0.0f;
  player_facing_x = 0.0f;
  player_facing_y = -1.0f;
  cone_aim_x = 0.0f;
  cone_aim_y = -1.0f;
  for (int i = 0; i < MAX_ENEMY_SCAN; i++) fire_cooldowns[i] = 0.0f;

  // Clear all bullets
  for (int i = 0; i < MAX_BULLETS; i++) {
    bullets[i].active = 0;
  }

  // Load shooting sound effect
  if (a_AudioLoadSound("resources/soundEffects/foom_0.wav", &shot_sound) == 0) {
    printf("Loaded shooting sound: foom_0.wav\n");
    audio_loaded = 1;
  } else {
    printf("Failed to load shooting sound\n");
    audio_loaded = 0;
  }

  // Load dash sound
  if (a_AudioLoadSound("resources/soundEffects/dash.wav", &dash_sound) == 0) {
    dash_sound_loaded = 1;
  }

  // Load damage taken sound
  if (a_AudioLoadSound("resources/soundEffects/playerdamagetaken.wav", &damage_sound) == 0) {
    damage_sound_loaded = 1;
  }

  // Load shield hit sound
  if (a_AudioLoadSound("resources/soundEffects/shield_hit.wav", &shield_hit_sound) == 0) {
    shield_hit_sound_loaded = 1;
  }
}

// ============================================================================
// Find Nearest Enemy
// ============================================================================

static int find_nearest_enemy(float* out_x, float* out_y)
{
  float nearest_dist = AUTO_FIRE_MAX_RANGE;
  int nearest_index = -1;
  float center_x = player_x + 16;
  float center_y = player_y + 16;

  for (int i = 0; i < MAX_ENEMY_SCAN; i++)
  {
    if (!enemy_is_alive(i)) continue;

    float ex, ey;
    enemy_get_position(i, &ex, &ey);

    float er = enemy_get_radius(i);
    float dx = (ex + er) - center_x;
    float dy = (ey + er) - center_y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < nearest_dist)
    {
      nearest_dist = dist;
      nearest_index = i;
    }
  }

  // Also check snake heads
  float sx, sy;
  if (snake_find_nearest_head(center_x, center_y, &nearest_dist, &sx, &sy)) {
    // Snake is closer — target the head directly (no lead prediction)
    *out_x = sx;
    *out_y = sy;
    return 9999;  // Non-negative sentinel — callers only check >= 0
  }

  // Lead the shot: predict where the enemy will be when the bullet arrives
  if (nearest_index >= 0) {
    float ex, ey;
    enemy_get_position(nearest_index, &ex, &ey);
    float er = enemy_get_radius(nearest_index);
    float ecx = ex + er;
    float ecy = ey + er;

    float evx, evy;
    enemy_get_velocity(nearest_index, &evx, &evy);

    float travel_time = nearest_dist / (bullet_speed * weapons_get_wand_bullet_speed_mult());
    *out_x = ecx + evx * travel_time;
    *out_y = ecy + evy * travel_time;
  }

  return nearest_index;
}

// ============================================================================
// Shooting
// ============================================================================

static void player_shoot_at(float target_x, float target_y)
{
  float speed = bullet_speed * weapons_get_wand_bullet_speed_mult();

  // Pierce from upgrade tier
  int pierce = upgrades_get_tier(UPG_WAND_PIERCE);

  // Find first inactive bullet slot
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (!bullets[i].active)
    {
      float dx = target_x - (player_x + 16);
      float dy = target_y - (player_y + 16);
      float distance = sqrtf(dx * dx + dy * dy);

      if (distance > 0)
      {
        bullets[i].x = player_x + 16 - 12.5f;
        bullets[i].y = player_y + 16 - 12.5f;
        bullets[i].vx = (dx / distance) * speed;
        bullets[i].vy = (dy / distance) * speed;
        bullets[i].active = 1;
        bullets[i].pierce = pierce;
        bullets[i].hit_count = 0;
        bullets[i].ricochet = upgrades_get_tier(UPG_WAND_RICOCHET);
        bullets[i].is_fragment = 0;
        bullets[i].lifetime = -1.0f;  // infinite

        if (audio_loaded) {
          aAudioOptions_t opts = {
            .channel = AUDIO_CHANNEL_PLAYER,
            .volume = 64,
            .loops = 0,
            .fade_ms = 0,
            .interrupt = 1
          };
          a_AudioPlaySound(&shot_sound, &opts);
        }
      }
      break;
    }
  }
}

// ============================================================================
// Update
// ============================================================================

void player_update(float dt)
{
  // Tick down iframes and screen flash
  if (invincibility_timer > 0.0f) invincibility_timer -= dt;
  if (screen_flash_timer > 0.0f) screen_flash_timer -= dt;
  if (screen_shake_timer > 0.0f) screen_shake_timer -= dt;
  if (shield_absorb_flash_timer > 0.0f) shield_absorb_flash_timer -= dt;
  if (heal_flash_timer > 0.0f) heal_flash_timer -= dt;

  // Health regen (global upgrade)
  {
    float regen_interval = global_get_regen_interval();
    if (regen_interval > 0.0f && player_hp > 0 && player_hp < player_max_hp) {
      regen_timer += dt;
      if (regen_timer >= regen_interval) {
        regen_timer -= regen_interval;
        player_heal(1);
      }
    }
  }
  if (dash_failed_timer > 0.0f) dash_failed_timer -= dt;
  if (heal_text_timer > 0.0f) {
    heal_text_timer -= dt;
    heal_text_y -= HEAL_TEXT_RISE_SPEED * dt;
  }

  if (dash_lockout_timer > 0.0f) dash_lockout_timer -= dt;

  // Death knockback — skip all input, just apply decaying velocity
  if (player_dying) {
    death_vignette_timer += dt;
    player_vx = death_kb_vx;
    player_vy = death_kb_vy;
    float decay = 1.0f - DEATH_KNOCKBACK_DRAG * dt;
    if (decay < 0.0f) decay = 0.0f;
    death_kb_vx *= decay;
    death_kb_vy *= decay;

    player_x += player_vx * dt;
    player_y += player_vy * dt;

    // Clamp to screen
    if (player_x < 0) player_x = 0;
    if (player_y < 0) player_y = 0;
    if (player_x > SCREEN_WIDTH - 32) player_x = SCREEN_WIDTH - 32;
    if (player_y > SCREEN_HEIGHT - 32) player_y = SCREEN_HEIGHT - 32;
    return; // No input, no dash, no shooting during death
  }

  // Tick dash cooldown (multi-charge)
  {
    float cd = get_dash_cd();
    int max = get_max_charges();
    // Only recharge one charge at a time (sequential, not parallel)
    for (int i = 0; i < max; i++) {
      if (dash_cd_timers[i] < cd) {
        dash_cd_timers[i] += dt;
        if (dash_cd_timers[i] > cd) dash_cd_timers[i] = cd;
        break;
      }
    }
  }

  // Move player with arrow keys (speed is pixels per second)
  float dx = 0.0f;
  float dy = 0.0f;

  if (app.keyboard[SDL_SCANCODE_UP]    || app.keyboard[SDL_SCANCODE_W]) dy -= 1.0f;
  if (app.keyboard[SDL_SCANCODE_DOWN]  || app.keyboard[SDL_SCANCODE_S]) dy += 1.0f;
  if (app.keyboard[SDL_SCANCODE_LEFT]  || app.keyboard[SDL_SCANCODE_A]) dx -= 1.0f;
  if (app.keyboard[SDL_SCANCODE_RIGHT] || app.keyboard[SDL_SCANCODE_D]) dx += 1.0f;

  // Normalize diagonal movement
  if (dx != 0.0f && dy != 0.0f)
  {
    float diagonal = 0.7071f; // 1/sqrt(2)
    dx *= diagonal;
    dy *= diagonal;
  }

  // Track facing direction for fire cone
  if (dx != 0.0f || dy != 0.0f) {
    player_facing_x = dx;
    player_facing_y = dy;
    float flen = sqrtf(player_facing_x * player_facing_x + player_facing_y * player_facing_y);
    if (flen > 0.01f) { player_facing_x /= flen; player_facing_y /= flen; }
  }

  // Check for dash trigger (shift key, must be moving)
  if ((app.keyboard[SDL_SCANCODE_LSHIFT] || app.keyboard[SDL_SCANCODE_RSHIFT] || app.keyboard[SDL_SCANCODE_SPACE]) &&
      (dx != 0.0f || dy != 0.0f) && dash_active_timer <= 0.0f && dash_lockout_timer <= 0.0f)
  {
    int charge = get_ready_charge();
    if (charge >= 0) {
      dash_dir_x = dx;
      dash_dir_y = dy;
      dash_active_timer = DASH_DURATION;
      dash_lockout_timer = DASH_LOCKOUT_DURATION;
      dash_cd_timers[charge] = 0.0f;
      invincibility_timer = DASH_DURATION;
      dash_trail_timer = 0.0f;
      for (int i = 0; i < DASH_TRAIL_MAX; i++) dash_trail[i].active = 0;

      if (dash_sound_loaded) {
        aAudioOptions_t opts = {
          .channel = AUDIO_CHANNEL_PLAYER,
          .volume = 80,
          .loops = 0,
          .fade_ms = 0,
          .interrupt = 0
        };
        a_AudioPlaySound(&dash_sound, &opts);
      }
    } else if (dash_failed_timer <= 0.0f) {
      dash_failed_timer = DASH_FAILED_DURATION;
    }
  }

  // During dash: override movement with dash velocity and spawn trail
  if (dash_active_timer > 0.0f) {
    dash_active_timer -= dt;
    player_vx = dash_dir_x * DASH_SPEED;
    player_vy = dash_dir_y * DASH_SPEED;

    // Spawn trail ghost every ~0.02s
    dash_trail_timer += dt;
    if (dash_trail_timer >= 0.02f) {
      dash_trail_timer = 0.0f;
      for (int i = 0; i < DASH_TRAIL_MAX; i++) {
        if (!dash_trail[i].active) {
          dash_trail[i].x = player_x;
          dash_trail[i].y = player_y;
          dash_trail[i].alpha = 180.0f;
          dash_trail[i].active = 1;
          break;
        }
      }
    }
  } else {
    // Normal movement (with speed buff multiplier)
    float effective_speed = player_speed * player_get_speed_multiplier();
    player_vx = dx * effective_speed;
    player_vy = dy * effective_speed;
  }

  // Fade out trail ghosts
  for (int i = 0; i < DASH_TRAIL_MAX; i++) {
    if (dash_trail[i].active) {
      dash_trail[i].alpha -= 600.0f * dt;
      if (dash_trail[i].alpha <= 0.0f) dash_trail[i].active = 0;
    }
  }

  // Update position
  player_x += player_vx * dt;
  player_y += player_vy * dt;

  // Hostile knockback (mimic spin)
  if (hostile_kb_timer > 0.0f) {
    float kb_pct = hostile_kb_timer / HOSTILE_KB_DURATION;
    player_x += hostile_kb_vx * kb_pct * dt;
    player_y += hostile_kb_vy * kb_pct * dt;
    hostile_kb_timer -= dt;
  }

  // Hostile slow tick
  if (hostile_slow_timer > 0.0f) hostile_slow_timer -= dt;

  // Clamp player to screen bounds (player is 32x32)
  if (player_x < 0) player_x = 0;
  if (player_y < 0) player_y = 0;
  if (player_x > SCREEN_WIDTH - 32) player_x = SCREEN_WIDTH - 32;
  if (player_y > SCREEN_HEIGHT - 32) player_y = SCREEN_HEIGHT - 32;

  // Update all active bullets
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (bullets[i].active)
    {
      // Tick lifetime for fragments
      if (bullets[i].lifetime >= 0.0f) {
        bullets[i].lifetime -= dt;
        if (bullets[i].lifetime <= 0.0f) {
          bullets[i].active = 0;
          continue;
        }
      }

      bullets[i].x += bullets[i].vx * dt;
      bullets[i].y += bullets[i].vy * dt;

      // Deactivate bullets that go off screen
      if (bullets[i].x < 0 || bullets[i].x > SCREEN_WIDTH ||
          bullets[i].y < 0 || bullets[i].y > SCREEN_HEIGHT)
      {
        bullets[i].active = 0;
      }
    }
  }
}

void player_clear_bullets(void)
{
  for (int i = 0; i < MAX_BULLETS; i++) {
    bullets[i].active = 0;
  }
}

// ============================================================================
// Collision - exposed for enemy system to use
// ============================================================================

int player_check_bullet_collision(float enemy_x, float enemy_y, float enemy_radius)
{
  return player_check_bullet_collision_ex(enemy_x, enemy_y, enemy_radius, -1);
}

int player_check_bullet_collision_ex(float enemy_x, float enemy_y, float enemy_radius, int enemy_index)
{
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (!bullets[i].active) continue;

    // Skip if this bullet already hit this enemy (pierce)
    if (enemy_index >= 0) {
      int already_hit = 0;
      for (int h = 0; h < bullets[i].hit_count; h++) {
        if (bullets[i].hit_enemies[h] == enemy_index) { already_hit = 1; break; }
      }
      if (already_hit) continue;
    }

    float dx = (bullets[i].x + 4) - (enemy_x + enemy_radius);
    float dy = (bullets[i].y + 4) - (enemy_y + enemy_radius);
    float dist = sqrtf(dx * dx + dy * dy);

    // Hit detected (bullet radius 6 * proj_size + enemy radius + 6px forgiveness)
    float bullet_r = 6.0f * weapons_get_wand_proj_size_mult();
    if (dist < (bullet_r + enemy_radius + 6.0f))
    {
      return i;
    }
  }
  return -1; // No hit
}

// ============================================================================
// Drawing
// ============================================================================

void player_draw(aImage_t* img)
{
  // Draw dash trail ghosts (fading blue rectangles)
  for (int i = 0; i < DASH_TRAIL_MAX; i++) {
    if (dash_trail[i].active) {
      int a = (int)dash_trail[i].alpha;
      if (a > 255) a = 255;
      a_DrawFilledRect(
        (aRectf_t){dash_trail[i].x, dash_trail[i].y, 32, 32},
        (aColor_t){80, 80, 255, (uint8_t)a}
      );
    }
  }

  // Draw the player square (blink during damage iframes, but NOT during dash)
  int visible = 1;
  if (!player_dying && invincibility_timer > 0.0f && dash_active_timer <= 0.0f) {
    visible = ((int)(invincibility_timer * 10.0f)) % 2;
  }
  if (visible) {
    aColor_t pcolor = player_dying ? (aColor_t){220, 30, 30, 255} : (aColor_t){0, 0, 255, 255};
    a_DrawFilledRect((aRectf_t){player_x, player_y, 32, 32}, pcolor);
  }

  // Draw all active bullets at 25% size
  if (img != NULL && img->surface != NULL)
  {
    float img_w = (float)img->surface->w;
    float img_h = (float)img->surface->h;
    float proj_scale = 0.25f * weapons_get_wand_proj_size_mult();
    float scaled_w = img_w * proj_scale;
    float scaled_h = img_h * proj_scale;

    for (int i = 0; i < MAX_BULLETS; i++)
    {
      if (bullets[i].active)
      {
        if (bullets[i].is_fragment) {
          // Draw fragments as small colored dots
          float fx = bullets[i].x + 4;
          float fy = bullets[i].y + 4;
          int alpha = (bullets[i].lifetime > 0.0f)
            ? (int)(200.0f * (bullets[i].lifetime / 0.3f))
            : 200;
          if (alpha > 200) alpha = 200;
          a_DrawFilledRect(
            (aRectf_t){fx - 2, fy - 2, 5, 5},
            (aColor_t){200, 180, 255, (uint8_t)alpha}
          );
        } else {
          aRectf_t src = {0, 0, img_w, img_h};
          aRectf_t dest = {bullets[i].x, bullets[i].y, scaled_w, scaled_h};
          a_BlitRect(img, &src, &dest, 1);
        }
      }
    }
  }
}

// ============================================================================
// Getters (for enemy targeting)
// ============================================================================

float player_get_x(void)
{
  return player_x + 16; // Return center X
}

float player_get_y(void)
{
  return player_y + 16; // Return center Y
}

void player_get_bullet_velocity(int bullet_index, float* out_vx, float* out_vy)
{
  if (bullet_index >= 0 && bullet_index < MAX_BULLETS && bullets[bullet_index].active) {
    *out_vx = bullets[bullet_index].vx;
    *out_vy = bullets[bullet_index].vy;
  } else {
    *out_vx = 0;
    *out_vy = 0;
  }
}

void player_destroy_bullet(int bullet_index)
{
  if (bullet_index >= 0 && bullet_index < MAX_BULLETS) {
    bullets[bullet_index].active = 0;
  }
}

float player_get_vx(void)
{
  return player_vx;
}

float player_get_vy(void)
{
  return player_vy;
}

void player_fire_at_nearest(void)
{
  float target_x, target_y;
  if (find_nearest_enemy(&target_x, &target_y) >= 0) {
    player_shoot_at(target_x, target_y);
  }
}

void player_fire_fan_at_nearest(int count)
{
  float target_x, target_y;
  int nearest = find_nearest_enemy(&target_x, &target_y);
  if (nearest < 0) return;

  if (count <= 1) {
    player_shoot_at(target_x, target_y);
    return;
  }

  float cx = player_x + 16;
  float cy = player_y + 16;
  float dx = target_x - cx;
  float dy = target_y - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) return;

  float base_angle = atan2f(dy, dx);
  float spread = 0.15f;
  float total_spread = spread * 2.0f;

  // Collect up to `count` unique targets: nearest enemies within the fan cone
  float fan_targets_x[8];
  float fan_targets_y[8];
  int fan_count = 0;

  // First slot is always the primary target
  fan_targets_x[0] = target_x;
  fan_targets_y[0] = target_y;
  fan_count = 1;

  // Find other enemies within the fan spread to aim extra bullets at
  for (int i = 0; i < MAX_ENEMY_SCAN && fan_count < count; i++) {
    if (i == nearest || !enemy_is_alive(i)) continue;

    float ex, ey;
    enemy_get_position(i, &ex, &ey);
    float er = enemy_get_radius(i);
    float ecx = ex + er;
    float ecy = ey + er;

    float edx = ecx - cx;
    float edy = ecy - cy;
    float enemy_angle = atan2f(edy, edx);

    // Check if this enemy is within the fan cone
    float angle_diff = enemy_angle - base_angle;
    // Normalize to [-PI, PI]
    while (angle_diff > PI) angle_diff -= 2.0f * PI;
    while (angle_diff < -PI) angle_diff += 2.0f * PI;

    if (angle_diff >= -spread && angle_diff <= spread) {
      // Predict position like find_nearest_enemy does
      float evx, evy;
      enemy_get_velocity(i, &evx, &evy);
      float edist = sqrtf(edx * edx + edy * edy);
      float travel_time = edist / (bullet_speed * weapons_get_wand_bullet_speed_mult());
      fan_targets_x[fan_count] = ecx + evx * travel_time;
      fan_targets_y[fan_count] = ecy + evy * travel_time;
      fan_count++;
    }
  }

  // Fire: aim at found targets first, then spread remaining bullets evenly
  for (int i = 0; i < count; i++) {
    if (i < fan_count) {
      player_shoot_at(fan_targets_x[i], fan_targets_y[i]);
    } else {
      float t = (count > 1) ? (float)i / (float)(count - 1) : 0.5f;
      float angle = base_angle - spread + total_spread * t;
      float tx = cx + cosf(angle) * dist;
      float ty = cy + sinf(angle) * dist;
      player_shoot_at(tx, ty);
    }
  }
}

// Spawn a ricochet bullet from hit position toward nearest enemy
static void spawn_ricochet(float hit_x, float hit_y, int hit_enemy, int remaining_bounces, int pierce)
{
  // Find nearest alive enemy within 80px (not the one we just hit)
  float best_dist = 80.0f;
  int best_target = -1;
  float best_ex = 0, best_ey = 0;

  for (int e = 0; e < MAX_ENEMY_SCAN; e++) {
    if (e == hit_enemy) continue;
    if (!enemy_is_alive(e)) continue;
    float ex, ey;
    enemy_get_position(e, &ex, &ey);
    float er = enemy_get_radius(e);
    float dx = (ex + er) - hit_x;
    float dy = (ey + er) - hit_y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < best_dist) {
      best_dist = d;
      best_target = e;
      best_ex = ex + er;
      best_ey = ey + er;
    }
  }

  if (best_target < 0) return;

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      float dx = best_ex - hit_x;
      float dy = best_ey - hit_y;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist < 0.1f) return;

      float speed = bullet_speed * weapons_get_wand_bullet_speed_mult();

      bullets[i].x = hit_x - 4;
      bullets[i].y = hit_y - 4;
      bullets[i].vx = (dx / dist) * speed;
      bullets[i].vy = (dy / dist) * speed;
      bullets[i].active = 1;
      bullets[i].pierce = pierce;
      bullets[i].hit_count = 0;
      bullets[i].ricochet = remaining_bounces - 1;
      bullets[i].is_fragment = 0;
      bullets[i].lifetime = -1.0f;
      return;
    }
  }
}

// Spawn splinter fragments from behind the enemy (relative to bullet travel)
static void spawn_splinters(float bul_vx, float bul_vy, int enemy_index)
{
  int tier = upgrades_get_tier(UPG_WAND_SPLINTER);
  if (tier <= 0) return;

  static const int frag_count[4] = { 0, 2, 3, 4 };
  int count = frag_count[tier];
  int frag_pierce = (tier >= 3) ? 1 : 0;

  float base_angle = atan2f(bul_vy, bul_vx);
  float speed = sqrtf(bul_vx * bul_vx + bul_vy * bul_vy) * 0.5f;
  if (speed < 100.0f) speed = 100.0f;

  // Spawn point: far side of enemy (behind it along bullet travel direction)
  float ex, ey;
  enemy_get_position(enemy_index, &ex, &ey);
  float er = enemy_get_radius(enemy_index);
  float ecx = ex + er, ecy = ey + er;
  float bul_len = sqrtf(bul_vx * bul_vx + bul_vy * bul_vy);
  float nx = (bul_len > 0.1f) ? bul_vx / bul_len : 0.0f;
  float ny = (bul_len > 0.1f) ? bul_vy / bul_len : 0.0f;
  float spawn_x = ecx + nx * (er + 4.0f);
  float spawn_y = ecy + ny * (er + 4.0f);

  // Spread angles: for 2 fragments ±45°, for 3 ±45° and ±90° pick 3, for 4 ±45° and ±90°
  float offsets[4];
  if (count == 2) {
    offsets[0] = 0.785f;   // +45°
    offsets[1] = -0.785f;  // -45°
  } else if (count == 3) {
    offsets[0] = 0.785f;
    offsets[1] = -0.785f;
    offsets[2] = 1.571f;   // +90°
  } else {
    offsets[0] = 0.785f;
    offsets[1] = -0.785f;
    offsets[2] = 1.571f;
    offsets[3] = -1.571f;
  }

  for (int f = 0; f < count; f++) {
    for (int i = 0; i < MAX_BULLETS; i++) {
      if (!bullets[i].active) {
        float angle = base_angle + offsets[f];
        bullets[i].x = spawn_x - 4;
        bullets[i].y = spawn_y - 4;
        bullets[i].vx = cosf(angle) * speed;
        bullets[i].vy = sinf(angle) * speed;
        bullets[i].active = 1;
        bullets[i].pierce = frag_pierce;
        bullets[i].hit_count = 0;
        bullets[i].ricochet = 0;
        bullets[i].is_fragment = 1;
        bullets[i].lifetime = 0.3f;
        break;
      }
    }
  }
}

int player_bullet_on_hit(int bullet_index, int enemy_index)
{
  if (bullet_index < 0 || bullet_index >= MAX_BULLETS) return 0;
  Bullet_t* b = &bullets[bullet_index];

  // Track this enemy as hit
  if (b->hit_count < 8) {
    b->hit_enemies[b->hit_count++] = enemy_index;
  }

  float hit_x = b->x + 4;
  float hit_y = b->y + 4;

  // Splinter on every hit (not fragments) — spawns behind the enemy
  if (!b->is_fragment) {
    spawn_splinters(b->vx, b->vy, enemy_index);
  }

  // Ricochet takes priority: if we can bounce, do that instead of piercing
  if (b->ricochet > 0) {
    spawn_ricochet(hit_x, hit_y, enemy_index, b->ricochet, b->pierce);
    b->active = 0;
    return 1; // Original bullet dies, ricochet bullet inherits pierce
  }

  // Pierce: pass through if we have charges left
  if (b->pierce > 0) {
    b->pierce--;
    return 0; // Bullet survives
  }

  b->active = 0;
  return 1; // Bullet destroyed
}

int player_do_spin_attack(float radius, float knockback_force)
{
  float cx = player_x + 16;
  float cy = player_y + 16;
  int hit_count = 0;

  for (int i = 0; i < MAX_ENEMY_SCAN; i++) {
    if (!enemy_is_active(i)) continue;

    EnemyState_t state = enemy_get_state(i);
    if (state == ENEMY_STATE_CORPSE || state == ENEMY_STATE_HIT_KNOCKBACK) continue;

    float ex, ey;
    enemy_get_position(i, &ex, &ey);

    float er = enemy_get_radius(i);
    float dx = (ex + er) - cx;
    float dy = (ey + er) - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < radius) {
      hit_count++;
      if (knockback_force > 0.0f) {
        float knockback_vx = (dist > 0.1f) ? (dx / dist) * knockback_force : knockback_force;
        float knockback_vy = (dist > 0.1f) ? (dy / dist) * knockback_force : 0.0f;
        enemy_hit(i, knockback_vx, knockback_vy);
      } else {
        enemy_hit(i, 0.0f, 0.0f);
      }

      // Electric Spin: apply conductor (2s, chains = tier)
      int electric_tier = upgrades_get_tier(UPG_SPIN_ELECTRIC);
      if (electric_tier > 0) {
        enemy_set_conductor(i, 2.0f, electric_tier);
      }
    }
  }
  hit_count += snake_check_hit_aoe(cx, cy, radius);
  {
    int electric_tier = upgrades_get_tier(UPG_SPIN_ELECTRIC);
    if (electric_tier > 0)
      snake_apply_conductor_aoe(cx, cy, radius, 2.0f, electric_tier);
  }
  return hit_count;
}

// ============================================================================
// Health System
// ============================================================================

int player_take_damage(int amount, float from_x, float from_y, EnemyType_t attacker_type)
{
  if (invincibility_timer > 0.0f) return 0;

  // Record attacker info for killed-by display
  last_hit_enemy_type = attacker_type;
  last_hit_damage = amount;

  // Record hit direction (from attacker toward player)
  float cx = player_x + 16;
  float cy = player_y + 16;
  float hdx = cx - from_x;
  float hdy = cy - from_y;
  float hlen = sqrtf(hdx * hdx + hdy * hdy);
  if (hlen > 0.1f) { last_hit_dx = hdx / hlen; last_hit_dy = hdy / hlen; }

  // Shield absorbs hit — still grant iframes so enemies back off
  if (player_shield_absorb()) {
    invincibility_timer = PLAYER_IFRAME_DURATION;
    shield_absorb_flash_timer = SHIELD_ABSORB_FLASH_DURATION;
    if (shield_hit_sound_loaded) {
      aAudioOptions_t opts = {
        .channel = AUDIO_CHANNEL_AUTO,
        .volume = 96,
        .loops = 0,
        .fade_ms = 0,
        .interrupt = 0
      };
      a_AudioPlaySound(&shield_hit_sound, &opts);
    }
    return 0;
  }

  int toughness = global_get_toughness();
  if (toughness > 0) {
    amount -= toughness;
    if (amount < 1) amount = 1;
  }

  // Mimic damage reduction (applied centrally since mimic has many damage sources)
  if (attacker_type == ENEMY_TYPE_MIMIC) {
    int mimic_red = progress_get_dmg_reduction(ENEMY_TYPE_MIMIC);
    if (mimic_red > 0) {
      amount -= mimic_red;
      if (amount < 1) amount = 1;
    }
  }

  player_hp -= amount;
  if (player_hp < 0) player_hp = 0;

  // Revive (Second Wind) — restore 25% HP + extended iframes
  if (player_hp == 0 && global_has_revive() && !revive_used) {
    revive_used = 1;
    player_hp = player_max_hp / 4;
    if (player_hp < 1) player_hp = 1;
    invincibility_timer = REVIVE_IFRAME_DURATION;
  } else {
    invincibility_timer = PLAYER_IFRAME_DURATION;
  }

  screen_flash_timer = SCREEN_FLASH_DURATION;
  screen_shake_timer = SCREEN_SHAKE_DURATION;

  // Retaliation — AoE knockback + stun on HP loss
  {
    float cx = player_x + 16.0f;
    float cy = player_y + 16.0f;
    float half_screen = (float)SCREEN_WIDTH * 0.5f;
    int has_kb = global_has_retaliation_kb();
    float stun_dur = global_get_retaliation_stun();
    if (has_kb || stun_dur > 0.0f) {
      enemy_aoe_knockback_stun(cx, cy, half_screen,
                               has_kb ? 40.0f : 0.0f,
                               stun_dur);
    }

    // Thorns — hit back the enemy that damaged you
    if (global_get_thorns_tier() > 0) {
      if (attacker_type == ENEMY_TYPE_SNAKE)
        snake_check_hit_aoe(from_x, from_y, 16.0f);
      else
        enemy_hit_nearest(from_x, from_y);
    }
  }

  if (damage_sound_loaded) {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume = 96,
      .loops = 0,
      .fade_ms = 0,
      .interrupt = 0
    };
    a_AudioPlaySound(&damage_sound, &opts);
  }

  return 1;
}

void player_heal(int amount)
{
  int before = player_hp;
  player_hp += amount;
  if (player_hp > player_max_hp) player_hp = player_max_hp;
  stats_record_heal(player_hp - before);

  // Trigger heal feedback
  heal_flash_timer = HEAL_FLASH_DURATION;
  heal_text_timer = HEAL_TEXT_DURATION;
  heal_text_x = player_x;
  heal_text_y = player_y - 20.0f;
  heal_text_amount = amount;
}

EnemyType_t player_get_killer_type(void)
{
  return last_hit_enemy_type;
}

int player_get_killer_damage(void)
{
  return last_hit_damage;
}

void player_start_death(void)
{
  player_dying = 1;

  // Knockback: push away from the last thing that hit us
  death_kb_vx = last_hit_dx * DEATH_KNOCKBACK_SPEED;
  death_kb_vy = last_hit_dy * DEATH_KNOCKBACK_SPEED;

  // Blood splatters in knockback direction (3 bursts with slight spread)
  float cx = player_x + 16;
  float cy = player_y + 16;
  float base_angle = atan2f(last_hit_dy, last_hit_dx);
  float spread = 0.35f; // ~20 degrees spread between each
  for (int d = -1; d <= 1; d++) {
    float angle = base_angle + (float)d * spread;
    blood_spawn(cx, cy, cosf(angle), sinf(angle), 16.0f);
  }
}

int player_is_dying(void)
{
  return player_dying;
}

static int get_hp_danger_tier(void);

void player_draw_screen_flash(void)
{
  // Damage flash (red)
  if (screen_flash_timer > 0.0f) {
    float alpha_pct = screen_flash_timer / SCREEN_FLASH_DURATION;
    int alpha = (int)(100.0f * alpha_pct);
    if (alpha > 0) {
      a_DrawFilledRect(
        (aRectf_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT},
        (aColor_t){255, 0, 0, (uint8_t)alpha}
      );
    }
  }

  // Shield absorb flash (blue)
  if (shield_absorb_flash_timer > 0.0f) {
    float alpha_pct = shield_absorb_flash_timer / SHIELD_ABSORB_FLASH_DURATION;
    int alpha = (int)(80.0f * alpha_pct);
    if (alpha > 0) {
      a_DrawFilledRect(
        (aRectf_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT},
        (aColor_t){50, 150, 255, (uint8_t)alpha}
      );
    }
  }

  // Heal flash (green)
  if (heal_flash_timer > 0.0f) {
    float alpha_pct = heal_flash_timer / HEAL_FLASH_DURATION;
    int alpha = (int)(60.0f * alpha_pct);
    if (alpha > 0) {
      a_DrawFilledRect(
        (aRectf_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT},
        (aColor_t){40, 220, 40, (uint8_t)alpha}
      );
    }
  }

  // Floating heal text (+10)
  if (heal_text_timer > 0.0f) {
    float alpha_pct = heal_text_timer / HEAL_TEXT_DURATION;
    int alpha = (int)(255.0f * alpha_pct);
    if (alpha > 255) alpha = 255;

    char heal_str[16];
    snprintf(heal_str, sizeof(heal_str), "+%d", heal_text_amount);
    aTextStyle_t style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {40, 255, 40, (uint8_t)alpha},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.7f
    };
    a_DrawText(heal_str, (int)heal_text_x, (int)heal_text_y, style);
  }

  // Low-health vignette — dark border that thickens at lower HP
  int danger = get_hp_danger_tier();
  if (danger > 0) {
    static const int vignette_depth[4]  = { 0, 60, 90, 120 };
    static const int vignette_alpha[4]  = { 0, 40, 70, 100 };
    int depth     = vignette_depth[danger];
    int max_alpha = vignette_alpha[danger];

    // Grow vignette 50% during death slowdown (1.5s)
    if (player_dying) {
      float t = death_vignette_timer / 1.5f;
      if (t > 1.0f) t = 1.0f;
      float scale = 1.0f + 0.5f * t;
      depth     = (int)((float)depth * scale);
      max_alpha = (int)((float)max_alpha * scale);
      if (max_alpha > 255) max_alpha = 255;
    }

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

    // Top edge
    for (int i = 0; i < depth; i++) {
      int a = max_alpha - (max_alpha * i / depth);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, (uint8_t)a);
      SDL_RenderDrawLine(app.renderer, 0, i, SCREEN_WIDTH - 1, i);
    }
    // Bottom edge
    for (int i = 0; i < depth; i++) {
      int a = max_alpha - (max_alpha * i / depth);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, (uint8_t)a);
      SDL_RenderDrawLine(app.renderer, 0, SCREEN_HEIGHT - 1 - i, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1 - i);
    }
    // Left edge
    for (int i = 0; i < depth; i++) {
      int a = max_alpha - (max_alpha * i / depth);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, (uint8_t)a);
      SDL_RenderDrawLine(app.renderer, i, 0, i, SCREEN_HEIGHT - 1);
    }
    // Right edge
    for (int i = 0; i < depth; i++) {
      int a = max_alpha - (max_alpha * i / depth);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, (uint8_t)a);
      SDL_RenderDrawLine(app.renderer, SCREEN_WIDTH - 1 - i, 0, SCREEN_WIDTH - 1 - i, SCREEN_HEIGHT - 1);
    }

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }
}

void player_clear_screen_flashes(void)
{
  screen_flash_timer = 0.0f;
  screen_shake_timer = 0.0f;
  shield_absorb_flash_timer = 0.0f;
  heal_flash_timer = 0.0f;
  heal_text_timer = 0.0f;
}

void player_get_screen_shake(int *sx, int *sy)
{
  if (screen_shake_timer > 0.0f) {
    float t = screen_shake_timer / SCREEN_SHAKE_DURATION;
    *sx = (int)(RANDF(-SCREEN_SHAKE_INTENSITY, SCREEN_SHAKE_INTENSITY) * t);
    *sy = (int)(RANDF(-SCREEN_SHAKE_INTENSITY, SCREEN_SHAKE_INTENSITY) * t);
  } else {
    *sx = 0;
    *sy = 0;
  }
}

float player_get_heal_flash_progress(void)
{
  if (heal_flash_timer <= 0.0f) return 0.0f;
  return heal_flash_timer / HEAL_FLASH_DURATION;
}

int player_get_hp(void)
{
  return player_hp;
}

int player_get_max_hp(void)
{
  return player_max_hp;
}

int player_is_alive(void)
{
  return player_hp > 0;
}

// Returns 0 (>75%), 1 (<=75%), 2 (<=50%), 3 (<=25%)
static int get_hp_danger_tier(void)
{
  float pct = (float)player_hp / (float)player_max_hp;
  if (pct <= 0.25f) return 3;
  if (pct <= 0.50f) return 2;
  if (pct <= 0.75f) return 1;
  return 0;
}

int player_get_danger_tier(void)
{
  return get_hp_danger_tier();
}

int player_is_invincible(void)
{
  return invincibility_timer > 0.0f;
}

// ============================================================================
// Dash System
// ============================================================================

void player_dash(void)
{
  // Triggered via shift in player_update — this is here if external trigger needed
}

float player_get_dash_cooldown_progress(void)
{
  // Return progress of the charge closest to ready
  float cd = get_dash_cd();
  int max = get_max_charges();
  float best = 0.0f;
  for (int i = 0; i < max; i++) {
    float p = (cd > 0.0f) ? dash_cd_timers[i] / cd : 1.0f;
    if (p > best) best = p;
  }
  if (best > 1.0f) best = 1.0f;
  return best;
}

int player_is_dashing(void)
{
  return dash_active_timer > 0.0f;
}

float player_get_dash_failed_timer(void)
{
  return dash_failed_timer;
}

float player_get_dash_cooldown_remaining(void)
{
  // Return smallest remaining time across all charges
  float cd = get_dash_cd();
  int max = get_max_charges();
  float best_remain = cd;
  for (int i = 0; i < max; i++) {
    float remain = cd - dash_cd_timers[i];
    if (remain < 0.0f) remain = 0.0f;
    if (remain < best_remain) best_remain = remain;
  }
  return best_remain;
}

int player_get_dash_max_charges(void)
{
  return get_max_charges();
}

int player_get_dash_ready_charges(void)
{
  float cd = get_dash_cd();
  int max = get_max_charges();
  int ready = 0;
  for (int i = 0; i < max; i++) {
    if (dash_cd_timers[i] >= cd) ready++;
  }
  return ready;
}

float player_get_dash_charge_progress(int charge_index)
{
  if (charge_index < 0 || charge_index >= DASH_MAX_CHARGES) return 0.0f;
  float cd = get_dash_cd();
  if (cd <= 0.0f) return 1.0f;
  float p = dash_cd_timers[charge_index] / cd;
  return p > 1.0f ? 1.0f : p;
}

// ============================================================================
// Buff System
// ============================================================================

void player_apply_buff(PickupType_t type)
{
  Buff_t* b = &player_buffs[type];
  switch (type) {
    case PICKUP_FIRE_CONE:
      if (b->active) {
        b->duration += FIRE_CONE_DURATION + global_get_buff_duration_bonus();
      } else {
        b->active = 1;
        b->duration = FIRE_CONE_DURATION + global_get_buff_duration_bonus();
        fire_cone_elapsed = 0.0f;
        fire_cone_tick = 0.0f;
      }
      break;
    case PICKUP_SPEED:
      b->active = 1;
      b->duration = BUFF_SPEED_DURATION + global_get_buff_duration_bonus();
      break;
    case PICKUP_SHIELD: {
      int before = b->shield_hits;
      b->active = 1;
      b->shield_hits += BUFF_SHIELD_HITS + global_get_shield_hits_bonus();
      int cap = BUFF_SHIELD_PLAYER_CAP + global_get_shield_hits_bonus();
      if (cap < before) cap = before; // don't reduce below pre-pickup count
      if (b->shield_hits > cap)
        b->shield_hits = cap;
      break;
    }
    case PICKUP_SLOW_AURA:
      if (b->active) {
        b->duration += BUFF_SLOW_AURA_DURATION + global_get_buff_duration_bonus();
      } else {
        b->active = 1;
        b->duration = BUFF_SLOW_AURA_DURATION + global_get_buff_duration_bonus();
      }
      break;
    default: break;
  }
}

void player_update_buffs(float dt)
{
  // Tick shield flash
  if (shield_flash_timer > 0.0f) shield_flash_timer -= dt;
  if (player_buffs[PICKUP_SHIELD].active) shield_anim_timer += dt;

  for (int i = 0; i < PICKUP_TYPE_COUNT; i++) {
    if (!player_buffs[i].active) continue;

    // Shield has no time limit — only expires when all hits are consumed
    if (i == PICKUP_SHIELD) continue;

    player_buffs[i].duration -= dt;
    if (player_buffs[i].duration <= 0.0f) {
      player_buffs[i].active = 0;
      if (i == PICKUP_SLOW_AURA) slow_aura_elapsed = 0.0f;
      if (i == PICKUP_FIRE_CONE) fire_cone_elapsed = 0.0f;
      continue;
    }

    // Track total time buffs have been active
    if (i == PICKUP_SLOW_AURA) slow_aura_elapsed += dt;
    if (i == PICKUP_FIRE_CONE) fire_cone_elapsed += dt;
  }

  // Tick down per-enemy fire cooldowns
  for (int i = 0; i < MAX_ENEMY_SCAN; i++) {
    if (fire_cooldowns[i] > 0.0f) fire_cooldowns[i] -= dt;
  }

  // Fire cone: find nearest enemy, aim cone at them, damage enemies in cone
  if (player_buffs[PICKUP_FIRE_CONE].active) {
    float cx = player_x + 16;
    float cy = player_y + 16;

    // Find nearest enemy and lerp cone aim toward them
    float target_x, target_y;
    int nearest = find_nearest_enemy(&target_x, &target_y);
    if (nearest >= 0) {
      float aim_dx = target_x - cx;
      float aim_dy = target_y - cy;
      float aim_dist = sqrtf(aim_dx * aim_dx + aim_dy * aim_dy);
      if (aim_dist > 0.1f) {
        float goal_x = aim_dx / aim_dist;
        float goal_y = aim_dy / aim_dist;
        // Lerp cone_aim toward goal
        float t = CONE_LERP_SPEED * dt;
        if (t > 1.0f) t = 1.0f;
        cone_aim_x += (goal_x - cone_aim_x) * t;
        cone_aim_y += (goal_y - cone_aim_y) * t;
        // Re-normalize
        float len = sqrtf(cone_aim_x * cone_aim_x + cone_aim_y * cone_aim_y);
        if (len > 0.01f) { cone_aim_x /= len; cone_aim_y /= len; }
      }
    }
    player_facing_x = cone_aim_x;
    player_facing_y = cone_aim_y;

    fire_cone_tick += dt;
    if (fire_cone_tick >= FIRE_CONE_TICK_RATE) {
      fire_cone_tick -= FIRE_CONE_TICK_RATE;
      weapons_set_damage_source(SOURCE_FIRE_CONE);

      for (int i = 0; i < MAX_ENEMY_SCAN; i++) {
        if (!enemy_is_alive(i)) continue;
        if (fire_cooldowns[i] > 0.0f) continue; // still immune

        float ex, ey;
        enemy_get_position(i, &ex, &ey);
        float er = enemy_get_radius(i);
        float dx = (ex + er) - cx;
        float dy = (ey + er) - cy;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist > get_fire_cone_range() || dist < 0.1f) continue;

        // Check angle: dot product with cone aim direction
        float ndx = dx / dist;
        float ndy = dy / dist;
        float dot = ndx * cone_aim_x + ndy * cone_aim_y;
        float cone_threshold = cosf(FIRE_CONE_HALF_ANGLE);

        if (dot >= cone_threshold) {
          enemy_hit(i, dx, dy);
          fire_cooldowns[i] = FIRE_HIT_COOLDOWN;
          game_audio_play_fire_hit();
          // Spawn fire particles at enemy, directed away from player
          float ex_center = ex + er;
          float ey_center = ey + er;
          fire_particles_spawn(ex_center, ey_center, ndx, ndy);
        }
      }
      weapons_set_damage_source(WEAPON_NONE);
    }
  }
}

void player_draw_buffs(void)
{
  float cx = player_x + 16;
  float cy = player_y + 16;

  // Fire cone visual: translucent orange cone
  if (player_buffs[PICKUP_FIRE_CONE].active) {
    float pulse = 80.0f + 70.0f * sinf(player_buffs[PICKUP_FIRE_CONE].duration * 8.0f);
    int alpha = (int)pulse;
    if (alpha > 200) alpha = 200;

    // Two lines from center at ±30° from facing direction
    float cos_a = cosf(FIRE_CONE_HALF_ANGLE);
    float sin_a = sinf(FIRE_CONE_HALF_ANGLE);

    // Rotate facing by +angle and -angle
    float l1x = player_facing_x * cos_a - player_facing_y * sin_a;
    float l1y = player_facing_x * sin_a + player_facing_y * cos_a;
    float l2x = player_facing_x * cos_a + player_facing_y * sin_a;
    float l2y = -player_facing_x * sin_a + player_facing_y * cos_a;

    float cone_r = get_fire_cone_range();
    int tip1_x = (int)(cx + l1x * cone_r);
    int tip1_y = (int)(cy + l1y * cone_r);
    int tip2_x = (int)(cx + l2x * cone_r);
    int tip2_y = (int)(cy + l2y * cone_r);

    aColor_t cone_color = {255, 130, 30, (uint8_t)alpha};
    a_DrawFilledTriangle((int)cx, (int)cy, tip1_x, tip1_y, tip2_x, tip2_y, cone_color);
  }

  // Speed visual: use existing dash trail system (lighter blue-white)
  if (player_buffs[PICKUP_SPEED].active) {
    // Draw a subtle yellow glow around player
    float pulse = 30.0f + 20.0f * sinf(player_buffs[PICKUP_SPEED].duration * 10.0f);
    a_DrawFilledRect(
      (aRectf_t){player_x - 3, player_y - 3, 38, 38},
      (aColor_t){255, 230, 50, (uint8_t)pulse}
    );
  }

  // Shield visual: pulsing blue outline + charge pips
  if (player_buffs[PICKUP_SHIELD].active) {
    int hits = player_buffs[PICKUP_SHIELD].shield_hits;
    float pulse = 120.0f + 60.0f * sinf(shield_anim_timer * 6.0f);
    int shield_cap = BUFF_SHIELD_PLAYER_CAP + global_get_shield_hits_bonus();
    int start = global_get_starting_shield();
    if (start > shield_cap) shield_cap = start;
    int sa = (int)(pulse * (float)hits / (float)shield_cap);
    if (sa > 220) sa = 220;

    // Outer glow ring
    a_DrawRect(
      (aRectf_t){player_x - 3, player_y - 3, 38, 38},
      (aColor_t){50, 150, 255, (uint8_t)(sa / 2)}
    );
    // Inner ring
    a_DrawRect(
      (aRectf_t){player_x - 2, player_y - 2, 36, 36},
      (aColor_t){50, 150, 255, (uint8_t)sa}
    );

    // Shield charge pips: small filled squares below player
    for (int p = 0; p < shield_cap; p++) {
      int pip_sz = 5;
      int pip_gap = 3;
      int total_w = shield_cap * pip_sz + (shield_cap - 1) * pip_gap;
      int pip_x = (int)(player_x + 16) - total_w / 2 + p * (pip_sz + pip_gap);
      int pip_y = (int)(player_y + 36);
      aColor_t pip_color = (p < hits)
        ? (aColor_t){50, 180, 255, 220}
        : (aColor_t){50, 50, 80, 100};
      a_DrawFilledRect(
        (aRectf_t){(float)pip_x, (float)pip_y, (float)pip_sz, (float)pip_sz},
        pip_color
      );
    }

    // Flash white on absorb
    if (shield_flash_timer > 0.0f) {
      float flash_a = 200.0f * (shield_flash_timer / SHIELD_FLASH_DURATION);
      a_DrawFilledRect(
        (aRectf_t){player_x - 4, player_y - 4, 40, 40},
        (aColor_t){255, 255, 255, (uint8_t)flash_a}
      );
    }
  }

  // Slow aura visual: expanding translucent circle around player
  if (player_buffs[PICKUP_SLOW_AURA].active) {
    float aura_r = player_get_slow_aura_radius();
    float cx = player_x + 16;
    float cy = player_y + 16;
    float pulse = 0.7f + 0.3f * sinf(player_buffs[PICKUP_SLOW_AURA].duration * 4.0f);
    int alpha = (int)(50.0f * pulse);

    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    // Filled circle using horizontal lines
    int ri = (int)aura_r;
    for (int dy = -ri; dy <= ri; dy++) {
      int half_w = (int)sqrtf((float)(ri * ri - dy * dy));
      SDL_SetRenderDrawColor(app.renderer, 100, 200, 255, (uint8_t)alpha);
      SDL_RenderDrawLine(app.renderer,
        (int)cx - half_w, (int)cy + dy,
        (int)cx + half_w, (int)cy + dy);
    }
    // Outline ring (line segments)
    int edge_alpha = (int)(100.0f * pulse);
    SDL_SetRenderDrawColor(app.renderer, 150, 220, 255, (uint8_t)edge_alpha);
    int ring_segs = 24;
    for (int s = 0; s < ring_segs; s++) {
      float a1 = (float)s / ring_segs * 2.0f * (float)PI;
      float a2 = (float)(s + 1) / ring_segs * 2.0f * (float)PI;
      SDL_RenderDrawLine(app.renderer,
        (int)(cx + cosf(a1) * aura_r), (int)(cy + sinf(a1) * aura_r),
        (int)(cx + cosf(a2) * aura_r), (int)(cy + sinf(a2) * aura_r));
    }
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Draw buff indicators above hotbar area (small colored squares)
  int indicator_y = SCREEN_HEIGHT - 70;
  int indicator_x = SCREEN_WIDTH / 2 - (PICKUP_TYPE_COUNT * 16) / 2;
  aTextStyle_t ind_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .scale = 0.3f
  };

  for (int i = 0; i < PICKUP_TYPE_COUNT; i++) {
    if (!player_buffs[i].active) continue;

    aColor_t c;
    const char* label;
    switch (i) {
      case PICKUP_FIRE_CONE: c = (aColor_t){255, 130, 30, 200}; label = "F"; break;
      case PICKUP_SPEED:     c = (aColor_t){255, 230, 50, 200}; label = "Z"; break;
      case PICKUP_SHIELD:    c = (aColor_t){50, 150, 255, 200};  label = "S"; break;
      case PICKUP_SLOW_AURA: c = (aColor_t){100, 200, 255, 200}; label = "C"; break;
      default: c = (aColor_t){200, 200, 200, 200}; label = "?"; break;
    }

    // Shrink/fade as duration runs out (shield is permanent — always full)
    float pct;
    if (i == PICKUP_SHIELD) {
      pct = 1.0f;
    } else {
      float max_dur;
      switch (i) {
        case PICKUP_FIRE_CONE: max_dur = FIRE_CONE_DURATION; break;
        case PICKUP_SPEED:     max_dur = BUFF_SPEED_DURATION; break;
        case PICKUP_SLOW_AURA: max_dur = BUFF_SLOW_AURA_DURATION; break;
        default: max_dur = 5.0f; break;
      }
      pct = player_buffs[i].duration / max_dur;
      if (pct > 1.0f) pct = 1.0f;
    }
    int sz = 8 + (int)(4.0f * pct);
    c.a = (uint8_t)(120 + (int)(80.0f * pct));

    int ix = indicator_x + i * 20;
    a_DrawFilledRect(
      (aRectf_t){(float)ix, (float)indicator_y, (float)sz, (float)sz},
      c
    );
    a_DrawText(label, ix + sz / 2, indicator_y - 2, ind_style);
  }
}

int player_has_shield(void)
{
  return player_buffs[PICKUP_SHIELD].active && player_buffs[PICKUP_SHIELD].shield_hits > 0;
}

int player_get_shield_hits(void)
{
  if (!player_buffs[PICKUP_SHIELD].active) return 0;
  return player_buffs[PICKUP_SHIELD].shield_hits;
}

int player_shield_absorb(void)
{
  if (!player_has_shield()) return 0;

  player_buffs[PICKUP_SHIELD].shield_hits--;
  shield_flash_timer = SHIELD_FLASH_DURATION;
  stats_record_shield_hit_lost();

  if (player_buffs[PICKUP_SHIELD].shield_hits <= 0) {
    player_buffs[PICKUP_SHIELD].active = 0;
  }
  return 1;
}

float player_get_speed_multiplier(void)
{
  float mult = 1.0f;
  if (player_buffs[PICKUP_SPEED].active) mult = BUFF_SPEED_MULT;
  // Brute slow aura slows the player
  if (enemy_brute_slow_aura_check(player_x + 16, player_y + 16)) {
    mult *= 0.5f;
  }
  // Trail blazing speed
  mult *= weapons_get_trail_speed_mult();
  // Scythe harvest speed
  mult *= weapons_get_scythe_harvest_speed();
  // Hostile slow (mimic spin)
  if (hostile_slow_timer > 0.0f) mult *= hostile_slow_mult;
  return mult;
}

float player_get_facing_x(void)
{
  return player_facing_x;
}

float player_get_facing_y(void)
{
  return player_facing_y;
}

void player_increase_max_hp(int amount)
{
  player_max_hp += amount;
  player_hp += amount;
  if (player_hp > player_max_hp) player_hp = player_max_hp;
}

float player_get_slow_aura_radius(void)
{
  if (!player_buffs[PICKUP_SLOW_AURA].active) return 0.0f;
  // Grows from min toward max over BUFF_SLOW_AURA_DURATION, keeps growing beyond if stacked
  float pct = slow_aura_elapsed / BUFF_SLOW_AURA_DURATION;
  return SLOW_AURA_MIN_RADIUS + (SLOW_AURA_MAX_RADIUS - SLOW_AURA_MIN_RADIUS) * pct;
}

float player_get_buff_duration(int pickup_type)
{
  if (pickup_type < 0 || pickup_type >= PICKUP_TYPE_COUNT) return 0.0f;
  if (!player_buffs[pickup_type].active) return 0.0f;
  return player_buffs[pickup_type].duration;
}

void player_apply_knockback(float from_x, float from_y, float force)
{
  float cx = player_x + 16.0f;
  float cy = player_y + 16.0f;
  float dx = cx - from_x;
  float dy = cy - from_y;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 0.1f) { dx = 0; dy = -1; dist = 1; }
  hostile_kb_vx = (dx / dist) * force;
  hostile_kb_vy = (dy / dist) * force;
  hostile_kb_timer = HOSTILE_KB_DURATION;
}

void player_apply_slow(float mult, float duration)
{
  hostile_slow_mult = mult;
  hostile_slow_timer = duration;
}
