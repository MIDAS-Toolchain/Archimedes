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
#include "fire_particles.h"

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
static float invincibility_timer = 0.0f;
static float screen_flash_timer = 0.0f;

#define PLAYER_IFRAME_DURATION   1.0f
#define SCREEN_FLASH_DURATION    0.3f
#define HEAL_FLASH_DURATION      0.4f
#define HEAL_TEXT_DURATION        1.0f
#define HEAL_TEXT_RISE_SPEED     40.0f

// Heal feedback
static float heal_flash_timer = 0.0f;
static float heal_text_timer = 0.0f;
static float heal_text_x = 0.0f;
static float heal_text_y = 0.0f;
static int heal_text_amount = 0;

// Dash system
#define DASH_COOLDOWN      3.0f
#define DASH_DURATION      0.15f
#define DASH_SPEED         800.0f

static float dash_cooldown_timer = 0.0f;  // counts up toward DASH_COOLDOWN
static float dash_active_timer = 0.0f;    // remaining dash movement time
static float dash_failed_timer = 0.0f;    // flash timer when dash attempted on cooldown
#define DASH_FAILED_DURATION 0.6f
static float dash_dir_x = 0.0f;
static float dash_dir_y = 0.0f;

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
#define FIRE_CONE_DURATION    4.0f
#define FIRE_CONE_TICK_RATE   0.2f
#define FIRE_CONE_RANGE       132.0f
#define FIRE_CONE_HALF_ANGLE  0.5498f  // ~31.5 degrees (30deg * 1.05)
#define FIRE_CONE_DAMAGE      10

// Speed buff
#define SPEED_BUFF_DURATION   3.0f
#define SPEED_BUFF_MULT       1.5f

// Shield buff
#define SHIELD_BUFF_DURATION  5.0f
#define SHIELD_BUFF_HITS      3

// Slow aura buff
#define SLOW_AURA_DURATION    6.0f
#define SLOW_AURA_MIN_RADIUS  40.0f
#define SLOW_AURA_MAX_RADIUS  100.0f
static float slow_aura_elapsed = 0.0f;  // Total time aura has been active (grows continuously)

static float fire_cone_tick = 0.0f;
static float shield_flash_timer = 0.0f;
#define SHIELD_FLASH_DURATION 0.15f

// Per-enemy fire damage cooldown (1s between hits)
#define FIRE_HIT_COOLDOWN 1.0f

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
  player_x = 100.0f;
  player_y = 100.0f;
  player_max_hp = 100;
  player_hp = player_max_hp;
  invincibility_timer = 0.0f;
  screen_flash_timer = 0.0f;
  heal_flash_timer = 0.0f;
  heal_text_timer = 0.0f;
  dash_cooldown_timer = DASH_COOLDOWN; // ready immediately
  dash_active_timer = 0.0f;

  // Clear buffs
  for (int i = 0; i < PICKUP_TYPE_COUNT; i++) {
    player_buffs[i].active = 0;
    player_buffs[i].duration = 0.0f;
    player_buffs[i].shield_hits = 0;
  }
  fire_cone_tick = 0.0f;
  shield_flash_timer = 0.0f;
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

  // Lead the shot: predict where the enemy will be when the bullet arrives
  if (nearest_index >= 0) {
    float ex, ey;
    enemy_get_position(nearest_index, &ex, &ey);
    float er = enemy_get_radius(nearest_index);
    float ecx = ex + er;
    float ecy = ey + er;

    float evx, evy;
    enemy_get_velocity(nearest_index, &evx, &evy);

    float travel_time = nearest_dist / bullet_speed;
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
  // Projectile speed scaling from upgrade
  static const float proj_speed_mult[4] = { 1.0f, 1.25f, 1.50f, 1.75f };
  float speed = bullet_speed * proj_speed_mult[upgrades_get_tier(UPG_WAND_PROJ_SPEED)];

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
  if (heal_flash_timer > 0.0f) heal_flash_timer -= dt;
  if (dash_failed_timer > 0.0f) dash_failed_timer -= dt;
  if (heal_text_timer > 0.0f) {
    heal_text_timer -= dt;
    heal_text_y -= HEAL_TEXT_RISE_SPEED * dt;
  }

  // Tick dash cooldown
  if (dash_cooldown_timer < DASH_COOLDOWN) dash_cooldown_timer += dt;

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
  if ((app.keyboard[SDL_SCANCODE_LSHIFT] || app.keyboard[SDL_SCANCODE_RSHIFT]) &&
      (dx != 0.0f || dy != 0.0f) &&
      dash_cooldown_timer < DASH_COOLDOWN && dash_active_timer <= 0.0f &&
      dash_failed_timer <= 0.0f)
  {
    dash_failed_timer = DASH_FAILED_DURATION;
  }

  if ((app.keyboard[SDL_SCANCODE_LSHIFT] || app.keyboard[SDL_SCANCODE_RSHIFT]) &&
      dash_cooldown_timer >= DASH_COOLDOWN && dash_active_timer <= 0.0f &&
      (dx != 0.0f || dy != 0.0f))
  {
    dash_dir_x = dx;
    dash_dir_y = dy;
    dash_active_timer = DASH_DURATION;
    dash_cooldown_timer = 0.0f;
    invincibility_timer = DASH_DURATION; // iframes for the dash duration
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

  // Clamp player to screen bounds (player is 32x32)
  if (player_x < 0) player_x = 0;
  if (player_y < 0) player_y = 0;
  if (player_x > SCREEN_WIDTH - 32) player_x = SCREEN_WIDTH - 32;
  if (player_y > SCREEN_HEIGHT - 32) player_y = SCREEN_HEIGHT - 32;

  // Homing upgrade: steer bullets toward nearest enemy
  int homing_tier = upgrades_get_tier(UPG_WAND_HOMING);
  float homing_turn_rate = (float)homing_tier * 1.5f;  // rad/s

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

      // Homing: steer toward nearest alive enemy
      if (homing_turn_rate > 0.0f && !bullets[i].is_fragment) {
        float bcx = bullets[i].x + 4;
        float bcy = bullets[i].y + 4;
        float best_dist = 300.0f;  // max homing range
        float best_ex = 0, best_ey = 0;
        int found = 0;

        for (int e = 0; e < MAX_ENEMY_SCAN; e++) {
          if (!enemy_is_alive(e)) continue;
          float ex, ey;
          enemy_get_position(e, &ex, &ey);
          float er = enemy_get_radius(e);
          float edx = (ex + er) - bcx;
          float edy = (ey + er) - bcy;
          float ed = sqrtf(edx * edx + edy * edy);
          if (ed < best_dist) {
            best_dist = ed;
            best_ex = ex + er;
            best_ey = ey + er;
            found = 1;
          }
        }

        if (found) {
          float bul_angle = atan2f(bullets[i].vy, bullets[i].vx);
          float target_angle = atan2f(best_ey - bcy, best_ex - bcx);
          float diff = target_angle - bul_angle;
          // Normalize to [-PI, PI]
          while (diff > PI) diff -= 2.0f * (float)PI;
          while (diff < -PI) diff += 2.0f * (float)PI;

          float max_turn = homing_turn_rate * dt;
          if (diff > max_turn) diff = max_turn;
          else if (diff < -max_turn) diff = -max_turn;

          float new_angle = bul_angle + diff;
          float spd = sqrtf(bullets[i].vx * bullets[i].vx + bullets[i].vy * bullets[i].vy);
          bullets[i].vx = cosf(new_angle) * spd;
          bullets[i].vy = sinf(new_angle) * spd;
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

    // Hit detected (bullet radius 6 + enemy radius + 6px forgiveness)
    if (dist < (6.0f + enemy_radius + 6.0f))
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
  if (invincibility_timer > 0.0f && dash_active_timer <= 0.0f && screen_flash_timer > 0.0f) {
    visible = ((int)(invincibility_timer * 10.0f)) % 2;
  }
  if (visible) {
    a_DrawFilledRect((aRectf_t){player_x, player_y, 32, 32}, (aColor_t){0, 0, 255, 255});
  }

  // Draw all active bullets at 25% size
  if (img != NULL && img->surface != NULL)
  {
    float img_w = (float)img->surface->w;
    float img_h = (float)img->surface->h;
    float scaled_w = img_w * 0.25f;
    float scaled_h = img_h * 0.25f;

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
      float travel_time = edist / bullet_speed;
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

      static const float proj_speed_mult[4] = { 1.0f, 1.25f, 1.50f, 1.75f };
      float speed = bullet_speed * proj_speed_mult[upgrades_get_tier(UPG_WAND_PROJ_SPEED)];

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

// Spawn splinter fragments from hit position
static void spawn_splinters(float hit_x, float hit_y, float bul_vx, float bul_vy)
{
  int tier = upgrades_get_tier(UPG_WAND_SPLINTER);
  if (tier <= 0) return;

  static const int frag_count[4] = { 0, 2, 3, 4 };
  int count = frag_count[tier];
  int frag_pierce = (tier >= 3) ? 1 : 0;

  float base_angle = atan2f(bul_vy, bul_vx);
  float speed = sqrtf(bul_vx * bul_vx + bul_vy * bul_vy) * 0.5f;
  if (speed < 100.0f) speed = 100.0f;

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
        bullets[i].x = hit_x - 4;
        bullets[i].y = hit_y - 4;
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

  if (b->pierce > 0) {
    b->pierce--;
    return 0; // Bullet survives
  }

  // Bullet is dying — trigger on-death effects

  // Get hit position for ricochet/splinter
  float hit_x = b->x + 4;
  float hit_y = b->y + 4;

  // Ricochet: bounce to nearby enemy
  if (b->ricochet > 0) {
    spawn_ricochet(hit_x, hit_y, enemy_index, b->ricochet, upgrades_get_tier(UPG_WAND_PIERCE));
  }

  // Splinter: spray fragments (only non-fragments)
  if (!b->is_fragment) {
    spawn_splinters(hit_x, hit_y, b->vx, b->vy);
  }

  b->active = 0;
  return 1; // Bullet destroyed
}

void player_do_spin_attack(float radius, int knockback)
{
  float cx = player_x + 16;
  float cy = player_y + 16;

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
      if (knockback) {
        float knockback_vx = (dist > 0.1f) ? (dx / dist) * 300.0f : 300.0f;
        float knockback_vy = (dist > 0.1f) ? (dy / dist) * 300.0f : 0.0f;
        enemy_hit(i, knockback_vx, knockback_vy);
      } else {
        enemy_hit(i, 0.0f, 0.0f);
      }

      // Electric Spin: set conductor flag on hit enemies
      int electric_tier = upgrades_get_tier(UPG_SPIN_ELECTRIC);
      if (electric_tier > 0) {
        static const float conductor_dur[4] = { 0.0f, 2.0f, 3.0f, 4.0f };
        enemy_set_conductor(i, conductor_dur[electric_tier]);
      }
    }
  }
}

// ============================================================================
// Health System
// ============================================================================

int player_take_damage(int amount)
{
  if (invincibility_timer > 0.0f) return 0;

  // Shield absorbs hit — still grant iframes so enemies back off
  if (player_shield_absorb()) {
    invincibility_timer = PLAYER_IFRAME_DURATION;
    return 0;
  }

  player_hp -= amount;
  if (player_hp < 0) player_hp = 0;

  invincibility_timer = PLAYER_IFRAME_DURATION;
  screen_flash_timer = SCREEN_FLASH_DURATION;

  return 1;
}

void player_heal(int amount)
{
  player_hp += amount;
  if (player_hp > player_max_hp) player_hp = player_max_hp;

  // Trigger heal feedback
  heal_flash_timer = HEAL_FLASH_DURATION;
  heal_text_timer = HEAL_TEXT_DURATION;
  heal_text_x = player_x;
  heal_text_y = player_y - 20.0f;
  heal_text_amount = amount;
}

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
}

void player_clear_screen_flashes(void)
{
  screen_flash_timer = 0.0f;
  heal_flash_timer = 0.0f;
  heal_text_timer = 0.0f;
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
  if (dash_cooldown_timer >= DASH_COOLDOWN) return 1.0f;
  return dash_cooldown_timer / DASH_COOLDOWN;
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
  if (dash_cooldown_timer >= DASH_COOLDOWN) return 0.0f;
  return DASH_COOLDOWN - dash_cooldown_timer;
}

// ============================================================================
// Buff System
// ============================================================================

void player_apply_buff(PickupType_t type)
{
  Buff_t* b = &player_buffs[type];
  switch (type) {
    case PICKUP_FIRE_CONE:
      b->active = 1;
      b->duration = FIRE_CONE_DURATION;
      fire_cone_tick = 0.0f;
      break;
    case PICKUP_SPEED:
      b->active = 1;
      b->duration = SPEED_BUFF_DURATION;
      break;
    case PICKUP_SHIELD:
      b->active = 1;
      b->duration = SHIELD_BUFF_DURATION;
      b->shield_hits = SHIELD_BUFF_HITS;
      break;
    case PICKUP_SLOW_AURA:
      if (b->active) {
        // Already have aura — extend duration, keep current size
        b->duration += SLOW_AURA_DURATION;
      } else {
        b->active = 1;
        b->duration = SLOW_AURA_DURATION;
      }
      break;
    default: break;
  }
}

void player_update_buffs(float dt)
{
  // Tick shield flash
  if (shield_flash_timer > 0.0f) shield_flash_timer -= dt;

  for (int i = 0; i < PICKUP_TYPE_COUNT; i++) {
    if (!player_buffs[i].active) continue;

    // Shield has no time limit — only expires when all hits are consumed
    if (i == PICKUP_SHIELD) continue;

    player_buffs[i].duration -= dt;
    if (player_buffs[i].duration <= 0.0f) {
      player_buffs[i].active = 0;
      if (i == PICKUP_SLOW_AURA) slow_aura_elapsed = 0.0f;
      continue;
    }

    // Track total time slow aura has been active
    if (i == PICKUP_SLOW_AURA) slow_aura_elapsed += dt;
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

      for (int i = 0; i < MAX_ENEMY_SCAN; i++) {
        if (!enemy_is_alive(i)) continue;
        if (fire_cooldowns[i] > 0.0f) continue; // still immune

        float ex, ey;
        enemy_get_position(i, &ex, &ey);
        float er = enemy_get_radius(i);
        float dx = (ex + er) - cx;
        float dy = (ey + er) - cy;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist > FIRE_CONE_RANGE || dist < 0.1f) continue;

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

    int tip1_x = (int)(cx + l1x * FIRE_CONE_RANGE);
    int tip1_y = (int)(cy + l1y * FIRE_CONE_RANGE);
    int tip2_x = (int)(cx + l2x * FIRE_CONE_RANGE);
    int tip2_y = (int)(cy + l2y * FIRE_CONE_RANGE);

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
    float pulse = 120.0f + 60.0f * sinf(player_buffs[PICKUP_SHIELD].duration * 6.0f);
    int sa = (int)(pulse * (float)hits / (float)SHIELD_BUFF_HITS);
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
    for (int p = 0; p < SHIELD_BUFF_HITS; p++) {
      int pip_sz = 5;
      int pip_gap = 3;
      int total_w = SHIELD_BUFF_HITS * pip_sz + (SHIELD_BUFF_HITS - 1) * pip_gap;
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
    // Outline ring
    int edge_alpha = (int)(100.0f * pulse);
    SDL_SetRenderDrawColor(app.renderer, 150, 220, 255, (uint8_t)edge_alpha);
    for (int a = 0; a < 360; a += 2) {
      float rad = (float)a * (float)PI / 180.0f;
      int px2 = (int)(cx + cosf(rad) * aura_r);
      int py2 = (int)(cy + sinf(rad) * aura_r);
      SDL_RenderDrawPoint(app.renderer, px2, py2);
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
        case PICKUP_SPEED:     max_dur = SPEED_BUFF_DURATION; break;
        case PICKUP_SLOW_AURA: max_dur = SLOW_AURA_DURATION; break;
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

  if (player_buffs[PICKUP_SHIELD].shield_hits <= 0) {
    player_buffs[PICKUP_SHIELD].active = 0;
  }
  return 1;
}

float player_get_speed_multiplier(void)
{
  float mult = 1.0f;
  if (player_buffs[PICKUP_SPEED].active) mult = SPEED_BUFF_MULT;
  // Brute slow aura slows the player
  if (enemy_brute_slow_aura_check(player_x + 16, player_y + 16)) {
    mult *= 0.5f;
  }
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
  // Grows from min toward max over SLOW_AURA_DURATION, keeps growing beyond if stacked
  float pct = slow_aura_elapsed / SLOW_AURA_DURATION;
  return SLOW_AURA_MIN_RADIUS + (SLOW_AURA_MAX_RADIUS - SLOW_AURA_MIN_RADIUS) * pct;
}
