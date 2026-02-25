#include <math.h>
#include <stdlib.h>
#include "Archimedes.h"
#include "xp.h"
#include "player_actions.h"
#include "enemy.h"
#include "game_audio.h"
#include "stats.h"
#include "progress.h"

#define MAX_XP_ORBS       300
#define XP_MAGNET_RADIUS  144.0f
#define XP_MAGNET_SPEED   220.0f
#define XP_COLLECT_DIST    15.0f
#define XP_ORB_VISUAL_R     3
#define XP_MAX_LEVEL       15

static const int xp_table[XP_MAX_LEVEL] = {
  5, 10, 16, 23, 31, 40, 50, 62, 76, 92, 110, 130, 152, 176, 202
};

typedef struct {
  float x, y;
  int value;
  int active;
  float pulse_age;
} XpOrb_t;

static XpOrb_t orbs[MAX_XP_ORBS];
static int current_xp = 0;
static int current_level = 0;
static int pending_level_up = 0;
static int pending_upgrade_points = 0;

static int xp_needed_for_level(int level)
{
  int base;
  if (level < XP_MAX_LEVEL)
    base = xp_table[level];
  else
    base = (int)floorf(5.0f + 1.5f * (float)(level + 1) * (float)(level + 1));
  int reduction = global_get_xp_reduction();
  base -= reduction;
  if (base < 3) base = 3;
  return base;
}

void xp_init(void)
{
  for (int i = 0; i < MAX_XP_ORBS; i++)
    orbs[i].active = 0;
  current_xp = 0;
  current_level = 0;
  pending_level_up = 0;
  pending_upgrade_points = 0;
}

void xp_reset(void)
{
  xp_init();
}

void xp_spawn_orbs(float death_x, float death_y, int enemy_type)
{
  int count = 1;
  if (enemy_type == ENEMY_TYPE_DASHER) count = 2;
  else if (enemy_type == ENEMY_TYPE_BRUTE) count = 5;
  else if (enemy_type == ENEMY_TYPE_SHAMAN) count = 3;
  else if (enemy_type == ENEMY_TYPE_BEHOLDER) count = 5;

  for (int i = 0; i < count; i++) {
    for (int j = 0; j < MAX_XP_ORBS; j++) {
      if (!orbs[j].active) {
        orbs[j].x = death_x + RANDF(-10.0f, 10.0f);
        orbs[j].y = death_y + RANDF(-10.0f, 10.0f);
        orbs[j].value = 1;
        orbs[j].active = 1;
        orbs[j].pulse_age = RANDF(0.0f, 6.28f);
        break;
      }
    }
  }
}

void xp_update(float dt)
{
  float px = player_get_x() + 16.0f;
  float py = player_get_y() + 16.0f;

  for (int i = 0; i < MAX_XP_ORBS; i++) {
    if (!orbs[i].active) continue;

    orbs[i].pulse_age += dt;

    float dx = px - orbs[i].x;
    float dy = py - orbs[i].y;
    float dist = sqrtf(dx * dx + dy * dy);

    // Collect (not while dead)
    if (dist < XP_COLLECT_DIST && player_is_alive()) {
      orbs[i].active = 0;
      current_xp += orbs[i].value;
      stats_add_score(1);
      game_audio_play_coin();

      // Check for level-up(s)
      int needed = xp_needed_for_level(current_level);
      while (current_xp >= needed) {
        current_xp -= needed;
        current_level++;
        pending_upgrade_points += current_level;
        player_increase_max_hp(10);
        pending_level_up = 1;
        game_audio_play_levelup();
        needed = xp_needed_for_level(current_level);
      }
      continue;
    }

    // Magnet pull (not while dead)
    float magnet_r = XP_MAGNET_RADIUS + global_get_xp_magnet_bonus();
    if (dist < magnet_r && dist > 0.1f && player_is_alive()) {
      float pull = XP_MAGNET_SPEED * (1.0f - dist / magnet_r);
      float nx = dx / dist;
      float ny = dy / dist;
      orbs[i].x += nx * pull * dt;
      orbs[i].y += ny * pull * dt;
    }
  }
}

void xp_draw(void)
{
  for (int i = 0; i < MAX_XP_ORBS; i++) {
    if (!orbs[i].active) continue;

    int ox = (int)orbs[i].x;
    int oy = (int)orbs[i].y;

    float pulse = sinf(orbs[i].pulse_age * 6.0f);
    int alpha = 180 + (int)(75.0f * pulse);
    if (alpha > 255) alpha = 255;
    if (alpha < 140) alpha = 140;

    // Glow diamond (slightly larger, transparent)
    int ga = alpha / 4;
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app.renderer, 180, 255, 180, (uint8_t)ga);
    {
      int half = 4;
      for (int dy = -half; dy <= half; dy++) {
        int w = half - abs(dy);
        SDL_Rect row = { ox - w, oy + dy, w * 2 + 1, 1 };
        SDL_RenderFillRect(app.renderer, &row);
      }
    }

    // Core diamond
    SDL_SetRenderDrawColor(app.renderer, 200, 255, 200, (uint8_t)alpha);
    {
      int half = XP_ORB_VISUAL_R;
      for (int dy = -half; dy <= half; dy++) {
        int w = half - abs(dy);
        SDL_Rect row = { ox - w, oy + dy, w * 2 + 1, 1 };
        SDL_RenderFillRect(app.renderer, &row);
      }
    }
  }
}

int xp_get_level(void)
{
  return current_level;
}

float xp_get_level_progress(void)
{
  int needed = xp_needed_for_level(current_level);
  if (needed <= 0) return 0.0f;
  return (float)current_xp / (float)needed;
}

int xp_get_current(void)
{
  return current_xp;
}

int xp_get_needed(void)
{
  return xp_needed_for_level(current_level);
}

int xp_check_level_up(void)
{
  if (pending_level_up) {
    pending_level_up = 0;
    return 1;
  }
  return 0;
}

int xp_get_pending_upgrade_points(void)
{
  return pending_upgrade_points;
}
