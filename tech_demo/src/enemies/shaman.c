#include "shaman.h"
#include "enemy_internal.h"
#include <stdlib.h>
#include <stdio.h>

extern aApp_t app;

// ============================================================================
// Shaman AI Constants
// ============================================================================

#define SHAMAN_FLEE_RADIUS        120.0f
#define SHAMAN_SAFE_DISTANCE      160.0f
#define SHAMAN_CORPSE_SCAN_RADIUS 400.0f
#define SHAMAN_CORPSE_SPEED_MULT  1.8f
#define SHAMAN_HEAL_RANGE_MIN     75.0f
#define SHAMAN_HEAL_RANGE_MAX     187.0f
#define SHAMAN_HEAL_SCAN_MIN      200.0f
#define SHAMAN_HEAL_SCAN_MAX      500.0f
#define SHAMAN_HEAL_COOLDOWN      5.5f
#define SHAMAN_CHANNEL_TIME       0.75f
#define SHAMAN_EAT_TIME           0.5f
#define SHAMAN_EAT_DIST           15.0f
#define SHAMAN_ORBIT_CHANGE_MIN   3.0f
#define SHAMAN_ORBIT_CHANGE_MAX   5.0f
#define SHAMAN_ADVANCE_SPEED_MIN  1.3f
#define SHAMAN_ADVANCE_SPEED_MAX  2.5f
#define SHAMAN_FLEE_OVERRIDE_PCT  0.6f

// ============================================================================
// Shaman Helpers
// ============================================================================

int shaman_corpse_is_claimed(int corpse_index)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_SHAMAN) continue;
    if (enemies[i].target_corpse != corpse_index) continue;
    if (enemies[i].state == ENEMY_STATE_SEEKING_CORPSE ||
        enemies[i].state == ENEMY_STATE_EATING) {
      return 1;
    }
  }
  return 0;
}

static int decode_snake_target(int heal_target)
{
  if (heal_target <= -2) return -(heal_target + 2);
  return -1;
}

static int ally_is_heal_targeted(int ally_index, int ignore_shaman)
{
  for (int i = 0; i < max_enemies; i++) {
    if (i == ignore_shaman) continue;
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_SHAMAN) continue;
    if (enemies[i].heal_target != ally_index) continue;
    if (enemies[i].state == ENEMY_STATE_SEEKING_ALLY ||
        enemies[i].state == ENEMY_STATE_HEALING) {
      return 1;
    }
  }
  return 0;
}

static int shaman_find_corpse(float x, float y, float radius,
                              float player_x, float player_y, float safe_dist)
{
  float best_dist = radius + 1.0f;
  int best = -1;
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].state != ENEMY_STATE_CORPSE) continue;
    if (enemies[i].corpse_consumed) continue;
    if (enemies[i].type == ENEMY_TYPE_SHAMAN) continue;
    if (shaman_corpse_is_claimed(i)) continue;

    float cx = enemies[i].x + ei_get_stats(&enemies[i])->radius;
    float cy = enemies[i].y + ei_get_stats(&enemies[i])->radius;

    float dp = ei_dist_between(cx, cy, player_x, player_y);
    if (dp < safe_dist) continue;

    if (weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) continue;

    float d = ei_dist_between(x, y, cx, cy);
    if (d <= radius && d < best_dist) {
      best_dist = d;
      best = i;
    }
  }
  return best;
}

static int shaman_find_lowest_hp_ally(float x, float y, int self_index, float* out_urgency)
{
  float best_ratio = 0.0f;
  int best = -1;
  float best_dist = 9999.0f;

  for (int i = 0; i < max_enemies; i++) {
    if (i == self_index) continue;
    if (!enemies[i].active) continue;
    if (enemies[i].type == ENEMY_TYPE_SHAMAN) continue;
    if (!enemy_is_alive(i)) continue;
    if (enemies[i].hit_count <= 0) continue;
    if (ally_is_heal_targeted(i, self_index)) continue;

    int total_hp = ei_get_total_hp(&enemies[i]);
    if (total_hp <= 0) continue;
    float ratio = (float)enemies[i].hit_count / (float)total_hp;
    if (ratio > 1.0f) ratio = 1.0f;

    float scan_r = SHAMAN_HEAL_SCAN_MIN +
      (SHAMAN_HEAL_SCAN_MAX - SHAMAN_HEAL_SCAN_MIN) * ratio;

    float ecx = enemies[i].x + ei_get_stats(&enemies[i])->radius;
    float ecy = enemies[i].y + ei_get_stats(&enemies[i])->radius;
    float d = ei_dist_between(x, y, ecx, ecy);
    if (d > scan_r) continue;

    if (ratio > best_ratio || (ratio == best_ratio && d < best_dist)) {
      best_ratio = ratio;
      best = i;
      best_dist = d;
    }
  }

  // Also scan active snakes
  for (int si = 0; si < 4; si++) {
    if (!snake_is_on_screen_active(si)) continue;
    float ratio = snake_get_damage_ratio(si);
    if (ratio <= 0.0f) continue;

    int encoded = -(si + 2);
    if (ally_is_heal_targeted(encoded, self_index)) continue;

    float scan_r = SHAMAN_HEAL_SCAN_MIN +
      (SHAMAN_HEAL_SCAN_MAX - SHAMAN_HEAL_SCAN_MIN) * ratio;

    float sx, sy;
    snake_get_tail_position(si, &sx, &sy);
    float d = ei_dist_between(x, y, sx, sy);
    if (d > scan_r) continue;

    if (ratio > best_ratio || (ratio == best_ratio && d < best_dist)) {
      best_ratio = ratio;
      best = encoded;
      best_dist = d;
    }
  }

  if (out_urgency) *out_urgency = best_ratio;
  return best;
}

// ============================================================================
// State Handlers
// ============================================================================

static void update_shaman_alive(Enemy_t* e, int index, float dt,
                                float player_x, float player_y)
{
  float speed = ei_get_effective_speed(e);
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;
  float dp = ei_dist_between(cx, cy, player_x, player_y);

  float urgency = 0.0f;
  int ally = -1;
  if (e->heal_cooldown_timer <= 0.0f) {
    ally = shaman_find_lowest_hp_ally(cx, cy, index, &urgency);
  }

  if (dp < SHAMAN_FLEE_RADIUS && (ally == -1 || urgency < SHAMAN_FLEE_OVERRIDE_PCT)) {
    e->state = ENEMY_STATE_FLEEING;
    return;
  }

  float turret_x, turret_y;
  if (weapons_get_nearest_turret(cx, cy, TURRET_RANGE, &turret_x, &turret_y)) {
    if (ally == -1 || urgency < SHAMAN_FLEE_OVERRIDE_PCT) {
      e->state = ENEMY_STATE_FLEEING;
      return;
    }
  }

  if (ally != -1) {
    e->heal_target = ally;
    e->state = ENEMY_STATE_SEEKING_ALLY;
    return;
  }

  int corpse = shaman_find_corpse(cx, cy, SHAMAN_CORPSE_SCAN_RADIUS,
                                  player_x, player_y, SHAMAN_FLEE_RADIUS);
  if (corpse >= 0) {
    e->target_corpse = corpse;
    e->state = ENEMY_STATE_SEEKING_CORPSE;
    return;
  }

  // Default: orbit at safe distance
  e->orbit_dir_timer -= dt;
  if (e->orbit_dir_timer <= 0.0f) {
    e->orbit_direction = -e->orbit_direction;
    e->orbit_dir_timer = RANDF(SHAMAN_ORBIT_CHANGE_MIN, SHAMAN_ORBIT_CHANGE_MAX);
  }

  float desired_dist = SHAMAN_SAFE_DISTANCE;
  float dx = cx - player_x;
  float dy = cy - player_y;
  float len = ei_normalize(&dx, &dy);

  float tx = -dy * (float)e->orbit_direction;
  float ty = dx * (float)e->orbit_direction;

  float radial_weight = (len - desired_dist) / desired_dist;
  if (radial_weight > 1.0f) radial_weight = 1.0f;
  if (radial_weight < -1.0f) radial_weight = -1.0f;

  float mx = tx - dx * radial_weight;
  float my = ty - dy * radial_weight;
  ei_normalize(&mx, &my);

  float tav_x, tav_y;
  ei_calc_turret_avoidance(cx, cy, TURRET_RANGE * 1.3f, 1.5f, &tav_x, &tav_y);

  float haz_x, haz_y;
  ei_calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);

  float sep_x, sep_y;
  ei_calc_separation(index, &sep_x, &sep_y, 1.0f);

  e->vx = (mx * speed) + sep_x * SEPARATION_WEIGHT + tav_x * speed + haz_x * speed;
  e->vy = (my * speed) + sep_y * SEPARATION_WEIGHT + tav_y * speed + haz_y * speed;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - ei_get_stats(e)->size) e->x = SCREEN_WIDTH - ei_get_stats(e)->size;
  if (e->y > SCREEN_HEIGHT - ei_get_stats(e)->size) e->y = SCREEN_HEIGHT - ei_get_stats(e)->size;
}

static void update_shaman_fleeing(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  float speed = ei_get_effective_speed(e);
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  float dx = cx - player_x;
  float dy = cy - player_y;
  float dp = ei_normalize(&dx, &dy);

  float tx, ty;
  float flee_x = dx, flee_y = dy;
  int near_turret = weapons_get_nearest_turret(cx, cy, TURRET_RANGE * 1.3f, &tx, &ty);
  if (near_turret) {
    float tdx = cx - tx, tdy = cy - ty;
    ei_normalize(&tdx, &tdy);
    flee_x += tdx;
    flee_y += tdy;
    ei_normalize(&flee_x, &flee_y);
  }

  {
    float hx, hy;
    ei_calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &hx, &hy);
    flee_x += hx;
    flee_y += hy;
    ei_normalize(&flee_x, &flee_y);
  }

  e->vx = flee_x * speed;
  e->vy = flee_y * speed;
  e->x += e->vx * dt;
  e->y += e->vy * dt;

  float sz = ei_get_stats(e)->size;
  if (e->x < 0) e->x = 0;
  if (e->y < 0) e->y = 0;
  if (e->x > SCREEN_WIDTH - sz) e->x = SCREEN_WIDTH - sz;
  if (e->y > SCREEN_HEIGHT - sz) e->y = SCREEN_HEIGHT - sz;

  int turret_safe = !weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL);
  if (dp > SHAMAN_SAFE_DISTANCE && turret_safe) {
    e->state = ENEMY_STATE_ALIVE;
  }
}

static void update_seeking_corpse(Enemy_t* e, int index, float dt,
                                  float player_x, float player_y)
{
  (void)index;
  float speed = ei_get_effective_speed(e) * SHAMAN_CORPSE_SPEED_MULT;
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  float dp = ei_dist_between(cx, cy, player_x, player_y);
  if (dp < SHAMAN_FLEE_RADIUS ||
      weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_FLEEING;
    return;
  }

  int ci = e->target_corpse;
  if (ci < 0 || ci >= max_enemies || !enemies[ci].active ||
      enemies[ci].state != ENEMY_STATE_CORPSE || enemies[ci].corpse_consumed) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_ALIVE;
    return;
  }

  float tcx = enemies[ci].x + ei_get_stats(&enemies[ci])->radius;
  float tcy = enemies[ci].y + ei_get_stats(&enemies[ci])->radius;
  float dx = tcx - cx;
  float dy = tcy - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < SHAMAN_EAT_DIST) {
    e->eat_timer = SHAMAN_EAT_TIME;
    e->state = ENEMY_STATE_EATING;
    return;
  }

  float haz_x, haz_y;
  ei_calc_hazard_avoidance(cx, cy, 80.0f, 2.0f, &haz_x, &haz_y);

  if (dist > 0.1f) {
    e->vx = (dx / dist) * speed + haz_x * speed;
    e->vy = (dy / dist) * speed + haz_y * speed;
  }
  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

static void update_eating(Enemy_t* e, int index, float dt,
                          float player_x, float player_y)
{
  (void)index;
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  float dp = ei_dist_between(cx, cy, player_x, player_y);
  if (dp < SHAMAN_FLEE_RADIUS ||
      weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_FLEEING;
    return;
  }

  int ci = e->target_corpse;
  if (ci < 0 || ci >= max_enemies || !enemies[ci].active ||
      enemies[ci].state != ENEMY_STATE_CORPSE || enemies[ci].corpse_consumed) {
    e->target_corpse = -1;
    e->state = ENEMY_STATE_ALIVE;
    return;
  }

  e->vx = 0.0f;
  e->vy = 0.0f;

  e->eat_timer -= dt;
  if (e->eat_timer <= 0.0f) {
    int heal_value = enemies[ci].base_hits_to_kill;
    enemies[ci].corpse_consumed = 1;
    e->stored_heal_value = heal_value;
    e->target_corpse = -1;
    e->heal_cooldown_timer = 0.0f;
    e->state = ENEMY_STATE_ALIVE;
  }
}

static void update_seeking_ally(Enemy_t* e, int index, float dt,
                                float player_x, float player_y)
{
  (void)index;
  (void)player_x;
  (void)player_y;
  float speed = ei_get_effective_speed(e);
  float radius = ei_get_stats(e)->radius;
  float cx = e->x + radius;
  float cy = e->y + radius;

  int snake_idx = decode_snake_target(e->heal_target);
  float urgency, tcx, tcy;

  if (snake_idx >= 0) {
    if (!snake_is_on_screen_active(snake_idx) ||
        snake_get_damage_ratio(snake_idx) <= 0.0f) {
      e->heal_target = -1;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }
    urgency = snake_get_damage_ratio(snake_idx);
    snake_get_tail_position(snake_idx, &tcx, &tcy);
  } else {
    int ti = e->heal_target;
    if (ti < 0 || ti >= max_enemies || !enemies[ti].active ||
        !enemy_is_alive(ti) || enemies[ti].hit_count <= 0) {
      e->heal_target = -1;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }
    int ally_total = ei_get_total_hp(&enemies[ti]);
    urgency = (ally_total > 0) ? (float)enemies[ti].hit_count / (float)ally_total : 0.0f;
    if (urgency > 1.0f) urgency = 1.0f;
    tcx = enemies[ti].x + ei_get_stats(&enemies[ti])->radius;
    tcy = enemies[ti].y + ei_get_stats(&enemies[ti])->radius;
  }

  float advance_mult = SHAMAN_ADVANCE_SPEED_MIN +
    (SHAMAN_ADVANCE_SPEED_MAX - SHAMAN_ADVANCE_SPEED_MIN) * urgency;
  speed *= advance_mult;

  float heal_range = SHAMAN_HEAL_RANGE_MIN +
    (SHAMAN_HEAL_RANGE_MAX - SHAMAN_HEAL_RANGE_MIN) * urgency;

  if (weapons_get_nearest_turret(cx, cy, TURRET_RANGE, NULL, NULL)) {
    if (urgency < SHAMAN_FLEE_OVERRIDE_PCT) {
      e->heal_target = -1;
      e->state = ENEMY_STATE_FLEEING;
      return;
    }
  }

  float dx = tcx - cx;
  float dy = tcy - cy;
  float dist = sqrtf(dx * dx + dy * dy);

  if (dist < heal_range) {
    e->heal_channel_timer = SHAMAN_CHANNEL_TIME;
    e->state = ENEMY_STATE_HEALING;
    return;
  }

  ei_steer_toward_avoiding_hazards(cx, cy, tcx, tcy, speed, dt, &e->vx, &e->vy);
  e->x += e->vx * dt;
  e->y += e->vy * dt;
}

static void update_healing(Enemy_t* e, int index, float dt,
                           float player_x, float player_y)
{
  (void)index;
  (void)player_x;
  (void)player_y;

  int snake_idx = decode_snake_target(e->heal_target);

  if (snake_idx >= 0) {
    if (!snake_is_on_screen_active(snake_idx)) {
      e->heal_target = -1;
      e->stored_heal_value = 0;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }

    e->vx = 0.0f;
    e->vy = 0.0f;

    e->heal_channel_timer -= dt;
    if (e->heal_channel_timer <= 0.0f) {
      int amount = e->stored_heal_value > 0 ? e->stored_heal_value : 1;
      int reduction = progress_get_heal_reduction();
      amount -= reduction;
      if (amount < 1) amount = 1;

      snake_apply_heal(snake_idx, amount);
      stats_record_shaman_heal(amount);

      e->stored_heal_value = 0;
      e->heal_target = -1;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_FLEEING;
    }
  } else {
    int ti = e->heal_target;
    if (ti < 0 || ti >= max_enemies || !enemies[ti].active ||
        enemies[ti].state == ENEMY_STATE_CORPSE) {
      e->heal_target = -1;
      e->stored_heal_value = 0;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_ALIVE;
      return;
    }

    e->vx = 0.0f;
    e->vy = 0.0f;

    e->heal_channel_timer -= dt;
    if (e->heal_channel_timer <= 0.0f) {
      int amount = e->stored_heal_value > 0 ? e->stored_heal_value : 1;
      int reduction = progress_get_heal_reduction();
      amount -= reduction;
      if (amount < 1) amount = 1;
      int before = enemies[ti].hit_count;
      enemies[ti].hit_count -= amount;
      if (enemies[ti].hit_count < 0) enemies[ti].hit_count = 0;
      int actual_healed = before - enemies[ti].hit_count;

      enemies[ti].heal_flash_timer = 0.15f;
      stats_record_shaman_heal(actual_healed);

      e->stored_heal_value = 0;
      e->heal_target = -1;
      e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
      e->state = ENEMY_STATE_FLEEING;
    }
  }
}

// ============================================================================
// Update Dispatch
// ============================================================================

void shaman_update(Enemy_t* e, int index, float dt,
                   float player_x, float player_y)
{
  switch (e->state) {
    case ENEMY_STATE_ALIVE:          update_shaman_alive(e, index, dt, player_x, player_y);    break;
    case ENEMY_STATE_FLEEING:        update_shaman_fleeing(e, index, dt, player_x, player_y);   break;
    case ENEMY_STATE_SEEKING_CORPSE: update_seeking_corpse(e, index, dt, player_x, player_y);   break;
    case ENEMY_STATE_EATING:         update_eating(e, index, dt, player_x, player_y);            break;
    case ENEMY_STATE_SEEKING_ALLY:   update_seeking_ally(e, index, dt, player_x, player_y);     break;
    case ENEMY_STATE_HEALING:        update_healing(e, index, dt, player_x, player_y);          break;
    default: break;
  }
}

// ============================================================================
// Cooldown Tick
// ============================================================================

void shaman_tick_cooldown(Enemy_t* e, float dt)
{
  if (e->state == ENEMY_STATE_HEALING || e->state == ENEMY_STATE_CORPSE) return;
  if (e->heal_cooldown_timer > 0.0f) e->heal_cooldown_timer -= dt;
}

// ============================================================================
// Count
// ============================================================================

int shaman_get_count(void)
{
  int count = 0;
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_SHAMAN) continue;
    if (enemies[i].state == ENEMY_STATE_CORPSE) continue;
    count++;
  }
  return count;
}

void shaman_init_fields(Enemy_t* e)
{
  e->heal_cooldown_timer = SHAMAN_HEAL_COOLDOWN;
  e->orbit_dir_timer = RANDF(SHAMAN_ORBIT_CHANGE_MIN, SHAMAN_ORBIT_CHANGE_MAX);
}

// ============================================================================
// Drawing
// ============================================================================

void shaman_draw(Enemy_t* e, int r, int g, int b, int alpha)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  aColor_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)alpha};

  // Cross/plus shape
  float sz = stats->size;
  float cx = e->x + sz / 2.0f;
  float cy = e->y + sz / 2.0f;
  float arm_w = sz * 0.3f;
  float arm_l = sz / 2.0f;
  // Vertical bar
  a_DrawFilledRect(
    (aRectf_t){cx - arm_w / 2.0f, cy - arm_l, arm_w, sz},
    color
  );
  // Horizontal bar
  a_DrawFilledRect(
    (aRectf_t){cx - arm_l, cy - arm_w / 2.0f, sz, arm_w},
    color
  );

  // Heal beam (drawn when channeling)
  if (e->state == ENEMY_STATE_HEALING && e->heal_target != -1) {
    float scx = e->x + sz / 2.0f;
    float scy = e->y + sz / 2.0f;
    float tcx = 0, tcy = 0;
    int beam_valid = 0;

    int beam_snake = decode_snake_target(e->heal_target);
    if (beam_snake >= 0) {
      if (snake_is_on_screen_active(beam_snake)) {
        snake_get_tail_position(beam_snake, &tcx, &tcy);
        beam_valid = 1;
      }
    } else if (e->heal_target >= 0 && e->heal_target < max_enemies &&
               enemies[e->heal_target].active) {
      Enemy_t* target = &enemies[e->heal_target];
      float tsz = ei_get_stats(target)->size;
      tcx = target->x + tsz / 2.0f;
      tcy = target->y + tsz / 2.0f;
      beam_valid = 1;
    }

    if (beam_valid) {
      float pulse = 150.0f + 105.0f * sinf(e->heal_channel_timer * 8.0f * 2.0f * (float)PI);
      int beam_alpha = (int)pulse;
      if (beam_alpha > 255) beam_alpha = 255;
      if (beam_alpha < 100) beam_alpha = 100;

      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.renderer, 50, 255, 50, (uint8_t)beam_alpha);
      SDL_RenderDrawLine(app.renderer, (int)scx, (int)scy, (int)tcx, (int)tcy);
      float dx = tcx - scx;
      float dy = tcy - scy;
      float len = sqrtf(dx * dx + dy * dy);
      if (len > 0.1f) {
        float px = -dy / len;
        float py = dx / len;
        SDL_RenderDrawLine(app.renderer, (int)(scx + px), (int)(scy + py), (int)(tcx + px), (int)(tcy + py));
        SDL_RenderDrawLine(app.renderer, (int)(scx - px), (int)(scy - py), (int)(tcx - px), (int)(tcy - py));
      }

      // Green dots traveling along beam
      float progress = 1.0f - (e->heal_channel_timer / SHAMAN_CHANNEL_TIME);
      for (int d = 0; d < 3; d++) {
        float t = progress * 0.5f + (float)d * 0.2f;
        t = t - (int)t;
        float dotx = scx + (tcx - scx) * t;
        float doty = scy + (tcy - scy) * t;
        a_DrawFilledRect(
          (aRectf_t){dotx - 2, doty - 2, 4, 4},
          (aColor_t){100, 255, 100, 220}
        );
      }
      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
    }
  }

  // Eating sparks
  if (e->state == ENEMY_STATE_EATING) {
    float scx = e->x + sz / 2.0f;
    float scy = e->y + sz / 2.0f;
    float eat_progress = 1.0f - (e->eat_timer / SHAMAN_EAT_TIME);
    int num_sparks = 4;
    for (int s = 0; s < num_sparks; s++) {
      float angle = (float)s / num_sparks * 2.0f * (float)PI + eat_progress * 6.0f;
      float sr = 8.0f + 4.0f * eat_progress;
      float sx = scx + cosf(angle) * sr;
      float sy = scy + sinf(angle) * sr;
      a_DrawFilledRect(
        (aRectf_t){sx - 1.5f, sy - 1.5f, 3, 3},
        (aColor_t){50, 255, 50, 180}
      );
    }
  }
}
