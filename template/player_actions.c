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
#include "enemy.h"

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

// ============================================================================
// Targeting
// ============================================================================

#define AUTO_FIRE_MAX_RANGE 9999.0f
#define MAX_ENEMY_SCAN 50

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
  player_hp = 100;
  invincibility_timer = 0.0f;
  screen_flash_timer = 0.0f;
  heal_flash_timer = 0.0f;
  heal_text_timer = 0.0f;
  dash_cooldown_timer = DASH_COOLDOWN; // ready immediately
  dash_active_timer = 0.0f;

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
      *out_x = ex + er;
      *out_y = ey + er;
    }
  }

  return nearest_index;
}

// ============================================================================
// Shooting
// ============================================================================

static void player_shoot_at(float target_x, float target_y)
{
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
        bullets[i].vx = (dx / distance) * bullet_speed;
        bullets[i].vy = (dy / distance) * bullet_speed;
        bullets[i].active = 1;

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
  if (heal_text_timer > 0.0f) {
    heal_text_timer -= dt;
    heal_text_y -= HEAL_TEXT_RISE_SPEED * dt;
  }

  // Tick dash cooldown
  if (dash_cooldown_timer < DASH_COOLDOWN) dash_cooldown_timer += dt;

  // Move player with arrow keys (speed is pixels per second)
  float dx = 0.0f;
  float dy = 0.0f;

  if (app.keyboard[SDL_SCANCODE_UP])    dy -= 1.0f;
  if (app.keyboard[SDL_SCANCODE_DOWN])  dy += 1.0f;
  if (app.keyboard[SDL_SCANCODE_LEFT])  dx -= 1.0f;
  if (app.keyboard[SDL_SCANCODE_RIGHT]) dx += 1.0f;

  // Normalize diagonal movement
  if (dx != 0.0f && dy != 0.0f)
  {
    float diagonal = 0.7071f; // 1/sqrt(2)
    dx *= diagonal;
    dy *= diagonal;
  }

  // Check for dash trigger (shift key, must be moving)
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
    // Normal movement
    player_vx = dx * player_speed;
    player_vy = dy * player_speed;
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

  // Update all active bullets
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (bullets[i].active)
    {
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
  for (int i = 0; i < MAX_BULLETS; i++)
  {
    if (bullets[i].active)
    {
      float dx = (bullets[i].x + 4) - (enemy_x + enemy_radius);
      float dy = (bullets[i].y + 4) - (enemy_y + enemy_radius);
      float dist = sqrtf(dx * dx + dy * dy);

      // Hit detected (bullet radius 4 + enemy radius + 4px forgiveness)
      if (dist < (4.0f + enemy_radius + 4.0f))
      {
        // Don't destroy here - let caller handle it after getting velocity
        return i; // Return bullet index that hit
      }
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
        aRectf_t src = {0, 0, img_w, img_h};
        aRectf_t dest = {bullets[i].x, bullets[i].y, scaled_w, scaled_h};

        // scale = 1 because dest already has the scaled size
        a_BlitRect(img, &src, &dest, 1);
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

void player_do_spin_attack(float radius)
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
      // Hit with knockback direction away from player
      float knockback_vx = (dist > 0.1f) ? (dx / dist) * 300.0f : 300.0f;
      float knockback_vy = (dist > 0.1f) ? (dy / dist) * 300.0f : 0.0f;
      enemy_hit(i, knockback_vx, knockback_vy);
    }
  }
}

// ============================================================================
// Health System
// ============================================================================

int player_take_damage(int amount)
{
  if (invincibility_timer > 0.0f) return 0;

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
