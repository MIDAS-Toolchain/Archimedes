#include <stdlib.h>
#include "Archimedes.h"
#include "collision.h"
#include "enemy.h"
#include "player_actions.h"
#include "pickups.h"
#include "xp.h"
#include "stats.h"

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
      }
    }
  }
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
    }
  }
}
