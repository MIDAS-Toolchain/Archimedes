#include <stdlib.h>
#include <math.h>
#include "Archimedes.h"
#include "collision.h"
#include "enemy.h"
#include "player_actions.h"
#include "pickups.h"
#include "xp.h"
#include "stats.h"
#include "upgrades.h"
#include "weapons.h"
#include "progress.h"
#include "snake.h"

static int* was_alive = NULL;
static int max_enemies = 0;

void collision_init(int count)
{
  max_enemies = count;
  was_alive = (int*)calloc(max_enemies, sizeof(int));
}

void collision_cleanup(void)
{
  free(was_alive);
  was_alive = NULL;
  max_enemies = 0;
}

void collision_begin_death_tracking(void)
{
  for (int j = 0; j < max_enemies; j++)
  {
    was_alive[j] = enemy_is_active(j) && enemy_get_state(j) != ENEMY_STATE_CORPSE;
  }
}

void collision_check_bullets(void)
{
  weapons_set_damage_source(WEAPON_WAND);
  for (int j = 0; j < max_enemies; j++)
  {
    EnemyState_t state = enemy_get_state(j);
    if (enemy_is_active(j) && state != ENEMY_STATE_CORPSE && state != ENEMY_STATE_HIT_KNOCKBACK)
    {
      float enemy_x, enemy_y;
      enemy_get_position(j, &enemy_x, &enemy_y);
      int bullet_hit = player_check_bullet_collision_ex(enemy_x, enemy_y, enemy_get_radius(j), j);

      if (bullet_hit >= 0)
      {
        float bullet_vx, bullet_vy;
        player_get_bullet_velocity(bullet_hit, &bullet_vx, &bullet_vy);
        enemy_hit(j, bullet_vx, bullet_vy);
        player_bullet_on_hit(bullet_hit, j);

        // Wand Conductor: bullet hits apply conductor (2s, chains = tier)
        int wand_cond_tier = upgrades_get_tier(UPG_WAND_CONDUCTOR);
        if (wand_cond_tier > 0) {
          enemy_set_conductor(j, 2.0f, wand_cond_tier);
        }
      }
    }
  }

  // Snake wand bullet collision
  snake_check_wand_bullets();

  weapons_set_damage_source(WEAPON_NONE);
}

void collision_check_turret_bullets(void)
{
  weapons_set_damage_source(WEAPON_TURRET);
  int count = weapons_get_turret_bullet_count();
  for (int b = 0; b < count; b++) {
    float bx, by, br;
    weapons_get_turret_bullet(b, &bx, &by, &br);
    if (br <= 0) continue;
    for (int j = 0; j < max_enemies; j++) {
      EnemyState_t state = enemy_get_state(j);
      if (!enemy_is_active(j) || state == ENEMY_STATE_CORPSE ||
          state == ENEMY_STATE_HIT_KNOCKBACK) continue;
      float ex, ey;
      enemy_get_position(j, &ex, &ey);
      float er = enemy_get_radius(j);
      float dx = bx - (ex + er), dy = by - (ey + er);
      float threshold = er + br;
      if (dx * dx + dy * dy < threshold * threshold) {
        enemy_hit(j, dx * 0.1f, dy * 0.1f);
        weapons_deactivate_turret_bullet(b);
        weapons_spawn_turret_slow_zone(ex + er, ey + er);

        // Turret Conductor: hits apply conductor (2s, chains = tier)
        int turret_cond_tier = upgrades_get_tier(UPG_TURRET_CONDUCTOR);
        if (turret_cond_tier > 0) {
          enemy_set_conductor(j, 2.0f, turret_cond_tier);
        }
        break;
      }
    }
  }

  // Snake turret bullet collision
  {
    int tcount = weapons_get_turret_bullet_count();
    for (int b = 0; b < tcount; b++) {
      float tbx, tby, tbr;
      weapons_get_turret_bullet(b, &tbx, &tby, &tbr);
      if (tbr <= 0) continue;
      if (snake_check_bullet_hit(tbx, tby, tbr)) {
        weapons_deactivate_turret_bullet(b);
      }
    }
  }

  weapons_set_damage_source(WEAPON_NONE);
}

void collision_resolve_deaths(void)
{
  for (int j = 0; j < max_enemies; j++)
  {
    if (was_alive[j] && enemy_get_state(j) == ENEMY_STATE_CORPSE)
    {
      float ex, ey;
      enemy_get_position(j, &ex, &ey);
      float er = enemy_get_radius(j);
      float death_x = ex + er;
      float death_y = ey + er;

      // 10% chance to drop a power pickup
      if ((rand() % 100) < 10)
      {
        pickups_spawn_random(death_x, death_y);
        pickups_notify_kill(1);
      }
      else
      {
        pickups_notify_kill(0);
      }

      xp_spawn_orbs(death_x, death_y, (int)enemy_get_type(j));
      stats_record_kill(enemy_get_type(j));
      stats_record_weapon_kill(enemy_get_last_hit_source(j));
    }
  }

  // Second pass: grunt death explosions (non-recursive)
  int explosion_tier = progress_get_grunt_explosion_tier();
  if (explosion_tier > 0) {
    float explosion_radius = progress_get_grunt_explosion_radius();
    weapons_set_damage_source(SOURCE_GRUNT_EXPLOSION);
    for (int j = 0; j < max_enemies; j++) {
      if (!was_alive[j]) continue;
      if (enemy_get_state(j) != ENEMY_STATE_CORPSE) continue;
      if (enemy_get_type(j) != ENEMY_TYPE_GRUNT) continue;

      float ex, ey;
      enemy_get_position(j, &ex, &ey);
      float er = enemy_get_radius(j);
      float dx = ex + er, dy = ey + er;

      // Spawn visual effect
      enemy_spawn_explosion(dx, dy, explosion_radius);

      for (int k = 0; k < max_enemies; k++) {
        if (k == j) continue;
        if (!enemy_is_active(k)) continue;
        EnemyState_t ks = enemy_get_state(k);
        if (ks == ENEMY_STATE_CORPSE || ks == ENEMY_STATE_HIT_KNOCKBACK) continue;

        float kx, ky;
        enemy_get_position(k, &kx, &ky);
        float kr = enemy_get_radius(k);
        float cx = kx + kr, cy = ky + kr;
        float ddx = cx - dx, ddy = cy - dy;
        float dist2 = ddx * ddx + ddy * ddy;

        if (dist2 < explosion_radius * explosion_radius) {
          float dist = sqrtf(dist2);
          float nvx = dist > 0.1f ? ddx / dist * 200.0f : 0.0f;
          float nvy = dist > 0.1f ? ddy / dist * 200.0f : 0.0f;
          enemy_hit(k, nvx, nvy);
        }
      }

      // Snake grunt explosion check
      snake_check_hit_aoe(dx, dy, explosion_radius);
    }
    weapons_set_damage_source(WEAPON_NONE);
  }
}
