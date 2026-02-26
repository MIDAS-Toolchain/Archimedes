#include "mimic_internal.h"
#include "drops.h"
#include "game_audio.h"
#include "progress.h"
#include <stdlib.h>
#include <math.h>

// ============================================================================
// Weapon glow colors (r, g, b) per WeaponType_t
// ============================================================================

static const int weapon_colors[][3] = {
  [WEAPON_NONE]   = {255, 255, 255},
  [WEAPON_WAND]   = {255, 255, 100},  // yellow
  [WEAPON_SPIN]   = {100, 255, 100},  // green
  [WEAPON_CHAIN]  = {100, 200, 255},  // cyan/blue
  [WEAPON_ORBIT]  = {200, 100, 255},  // purple
  [WEAPON_BOMB]   = {255, 160, 30},   // orange
  [WEAPON_TURRET] = {180, 180, 180},  // grey/silver
  [WEAPON_TRAIL]  = {255, 80, 40},    // red-orange
  [WEAPON_SCYTHE] = {180, 255, 180},  // pale green
};

// ============================================================================
// Spawn
// ============================================================================

int mimic_spawn(int hp, int spawn_index)
{
  // Pick random screen edge
  int edge = rand() % 4;
  float spawn_x, spawn_y;
  float sw = SCREEN_WIDTH;
  float sh = SCREEN_HEIGHT;
  float margin = 40.0f;

  switch (edge) {
    case 0: spawn_x = RANDF(margin, sw - margin); spawn_y = -MIMIC_BODY_SIZE; break;
    case 1: spawn_x = RANDF(margin, sw - margin); spawn_y = sh; break;
    case 2: spawn_x = -MIMIC_BODY_SIZE; spawn_y = RANDF(margin, sh - margin); break;
    default: spawn_x = sw; spawn_y = RANDF(margin, sh - margin); break;
  }

  int bonus = hp - MIMIC_BASE_HP;
  if (bonus < 0) bonus = 0;
  int idx = enemy_spawn(spawn_x, spawn_y, ENEMY_TYPE_MIMIC, 1.0f, bonus);
  if (idx < 0) return -1;

  Enemy_t* e = &enemies[idx];
  e->state = ENEMY_STATE_MM_SPAWNING;
  e->mm_stolen_weapon = -1;
  e->mm_stolen_slot = -1;
  e->mm_state_timer = MIMIC_ENTER_TIME;
  e->mm_weapon_cooldown = 0.0f;
  e->mm_spawn_index = spawn_index;
  e->mm_chomp_timer = 0.0f;
  e->mm_waddle_phase = 0.0f;
  e->mm_steal_orb_timer = 0.0f;
  e->mm_text_timer = 0.0f;
  e->mm_death_orb_timer = 0.0f;
  e->mm_dash_dir_x = 0.0f;
  e->mm_dash_dir_y = 0.0f;
  e->mm_steal_phase = 0;
  e->mm_orb_x = 0.0f;
  e->mm_orb_y = 0.0f;
  e->mm_orb_vx = 0.0f;
  e->mm_orb_vy = 0.0f;
  e->mm_hit_flash_timer = 0.0f;
  e->mm_spin_ai_state = 0;
  e->mm_spin_ai_timer = 0.0f;
  e->mm_spin_flank_x = 0.0f;
  e->mm_spin_flank_y = 0.0f;

  // Pick weapon to steal — choose random active weapon not already stolen by another mimic
  int count = weapons_get_count();
  if (count > 0) {
    // Build pool of stealable weapons
    int pool[WEAPON_MAX_SLOTS];
    int pool_count = 0;
    for (int i = 0; i < count; i++) {
      const Weapon_t* slot = weapons_get_slot(i);
      if (slot->type == WEAPON_NONE) continue;

      // Check if already stolen by another active mimic
      int already_stolen = 0;
      for (int j = 0; j < max_enemies; j++) {
        if (j == idx) continue;
        if (!enemies[j].active) continue;
        if (enemies[j].type != ENEMY_TYPE_MIMIC) continue;
        if (enemies[j].state == ENEMY_STATE_CORPSE) continue;
        if (enemies[j].mm_stolen_slot == i) {
          already_stolen = 1;
          break;
        }
      }
      if (!already_stolen) {
        pool[pool_count++] = i;
      }
    }

    if (pool_count > 0) {
      int chosen = pool[rand() % pool_count];
      e->mm_stolen_slot = chosen;
      e->mm_stolen_weapon = weapons_get_slot(chosen)->type;
    }
  }

  // Lazy-init weapon submodules
  mimic_wand_init();

  return idx;
}

// ============================================================================
// State Handlers
// ============================================================================

static void update_spawning(Enemy_t* e, float dt, float player_x, float player_y)
{
  // Waddle toward the player (not center)
  float sz = ei_get_stats(e)->size;
  float cx = e->x + sz / 2.0f;
  float cy = e->y + sz / 2.0f;
  float dx = player_x - cx;
  float dy = player_y - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist > 1.0f) {
    float speed = 150.0f;
    e->vx = (dx / dist) * speed;
    e->vy = (dy / dist) * speed;
    e->x += e->vx * dt;
    e->y += e->vy * dt;
  }

  e->mm_state_timer -= dt;
  if (e->mm_state_timer <= 0.0f) {
    e->state = ENEMY_STATE_MM_STEALING;
    e->mm_state_timer = MIMIC_STEAL_WINDUP;
    e->mm_steal_phase = 0; // windup
    e->mm_steal_orb_timer = 0.0f;
    e->vx = 0.0f;
    e->vy = 0.0f;
  }
}

static void steal_finish(Enemy_t* e)
{
  // Steal the weapon
  if (e->mm_stolen_slot >= 0) {
    weapons_disable_slot(e->mm_stolen_slot);
    // Clear any in-flight player projectiles for the stolen weapon
    if (e->mm_stolen_weapon == WEAPON_WAND) {
      player_clear_bullets();
    }
  }
  game_audio_play_mimic_steal();
  e->mm_text_timer = MIMIC_TEXT_DURATION;
  e->state = ENEMY_STATE_MM_FIGHTING;
  e->mm_state_timer = 0.0f;
  e->mm_weapon_cooldown = 1.0f;
}

static void update_stealing(Enemy_t* e, float dt, float player_x, float player_y)
{
  float sz = ei_get_stats(e)->size;
  float cx = e->x + sz / 2.0f;
  float cy = e->y + sz / 2.0f;
  float margin = 20.0f;

  if (e->mm_steal_phase == 0) {
    // --- WINDUP: shake in place, mouth opens wide ---
    e->mm_state_timer -= dt;
    if (e->mm_state_timer <= 0.0f) {
      // Lock dash direction toward player
      float dx = player_x - cx;
      float dy = player_y - cy;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist > 0.1f) {
        e->mm_dash_dir_x = dx / dist;
        e->mm_dash_dir_y = dy / dist;
      } else {
        e->mm_dash_dir_x = 0.0f;
        e->mm_dash_dir_y = -1.0f;
      }
      e->mm_steal_phase = 1;
      e->mm_state_timer = MIMIC_STEAL_DASH_MAX;
    }
  } else if (e->mm_steal_phase == 1) {
    // --- DASH: lunge toward player, clamped on screen, no steal ---
    float new_x = e->x + e->mm_dash_dir_x * MIMIC_STEAL_DASH_SPEED * dt;
    float new_y = e->y + e->mm_dash_dir_y * MIMIC_STEAL_DASH_SPEED * dt;

    // Clamp to screen bounds
    if (new_x < margin) new_x = margin;
    if (new_x > SCREEN_WIDTH - sz - margin) new_x = SCREEN_WIDTH - sz - margin;
    if (new_y < margin) new_y = margin;
    if (new_y > SCREEN_HEIGHT - sz - margin) new_y = SCREEN_HEIGHT - sz - margin;

    e->x = new_x;
    e->y = new_y;
    e->vx = e->mm_dash_dir_x * MIMIC_STEAL_DASH_SPEED;
    e->vy = e->mm_dash_dir_y * MIMIC_STEAL_DASH_SPEED;

    cx = e->x + sz / 2.0f;
    cy = e->y + sz / 2.0f;

    float dx = player_x - cx;
    float dy = player_y - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    e->mm_state_timer -= dt;

    // Stop near player or on timeout → launch steal orb
    if (dist < MIMIC_STEAL_DASH_STOP || e->mm_state_timer <= 0.0f) {
      e->mm_steal_phase = 2;
      e->vx = 0.0f;
      e->vy = 0.0f;
      e->mm_steal_orb_timer = 0.0f;

      // Launch orb from chest toward player
      float odx = player_x - cx;
      float ody = player_y - cy;
      float odist = sqrtf(odx * odx + ody * ody);
      if (odist < 0.1f) { odx = 0; ody = -1; odist = 1; }
      e->mm_orb_x = cx;
      e->mm_orb_y = cy;
      e->mm_orb_vx = (odx / odist) * MIMIC_STEAL_ORB_SPEED;
      e->mm_orb_vy = (ody / odist) * MIMIC_STEAL_ORB_SPEED;
    }
  } else {
    // --- ORB LAUNCHED: projectile flies from mimic toward player ---
    e->mm_steal_orb_timer += dt;

    // Decelerate mimic body
    e->vx *= 0.9f;
    e->vy *= 0.9f;

    // Steer orb toward player (slight homing)
    float odx = player_x - e->mm_orb_x;
    float ody = player_y - e->mm_orb_y;
    float odist = sqrtf(odx * odx + ody * ody);
    if (odist > 0.1f) {
      float steer = 3.0f * dt;
      e->mm_orb_vx += (odx / odist) * MIMIC_STEAL_ORB_SPEED * steer;
      e->mm_orb_vy += (ody / odist) * MIMIC_STEAL_ORB_SPEED * steer;
      // Re-normalize to orb speed
      float spd = sqrtf(e->mm_orb_vx * e->mm_orb_vx + e->mm_orb_vy * e->mm_orb_vy);
      if (spd > 0.1f) {
        e->mm_orb_vx = (e->mm_orb_vx / spd) * MIMIC_STEAL_ORB_SPEED;
        e->mm_orb_vy = (e->mm_orb_vy / spd) * MIMIC_STEAL_ORB_SPEED;
      }
    }

    e->mm_orb_x += e->mm_orb_vx * dt;
    e->mm_orb_y += e->mm_orb_vy * dt;

    // Check if orb hit the player
    float hit_dx = e->mm_orb_x - player_x;
    float hit_dy = e->mm_orb_y - player_y;
    if (hit_dx * hit_dx + hit_dy * hit_dy <
        (MIMIC_STEAL_ORB_RADIUS + MIMIC_PLAYER_RADIUS) *
        (MIMIC_STEAL_ORB_RADIUS + MIMIC_PLAYER_RADIUS)) {
      steal_finish(e);
      return;
    }

    // Safety timeout
    if (e->mm_steal_orb_timer >= MIMIC_STEAL_ORB_MAX) {
      steal_finish(e);
    }
  }
}

static void update_fighting(Enemy_t* e, int index, float dt,
                            float player_x, float player_y,
                            float player_vx, float player_vy)
{
  // Weapon cooldown tick (progress upgrade slows mimic's cooldown recovery)
  if (e->mm_weapon_cooldown > 0.0f)
    e->mm_weapon_cooldown -= dt / progress_get_mimic_cooldown_mult();

  // Stunned (vacuum/implosion pull) — freeze movement, skip weapon AI
  if (e->stun_timer > 0.0f) {
    e->vx = 0.0f;
    e->vy = 0.0f;
    return;
  }

  // Contact damage (always, regardless of weapon AI)
  const EnemyStats_t* stats = ei_get_stats(e);
  float cx = e->x + stats->size / 2.0f;
  float cy = e->y + stats->size / 2.0f;
  if (ei_resolve_player_collision(e, player_x, player_y)) {
    player_take_damage(MIMIC_CONTACT_DAMAGE, cx, cy, ENEMY_TYPE_MIMIC);
  }

  // Dispatch to weapon-specific AI
  switch (e->mm_stolen_weapon) {
    case WEAPON_WAND:
      mimic_wand_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_SPIN:
      mimic_spin_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_BOMB:
      mimic_bomb_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_CHAIN:
      mimic_chain_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_TURRET:
      mimic_turret_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_TRAIL:
      mimic_trail_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_ORBIT:
      mimic_orbit_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    case WEAPON_SCYTHE:
      mimic_scythe_update(e, index, dt, player_x, player_y, player_vx, player_vy);
      return;
    default:
      break;
  }

  // Fallback: simple chase (for weapons not yet implemented)
  float dx = player_x - cx;
  float dy = player_y - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  float speed = ei_get_effective_speed(e);
  if (dist > 40.0f) {
    float sep_x, sep_y;
    ei_calc_separation(index, &sep_x, &sep_y, 1.0f);
    ei_move_toward(e, player_x - stats->size / 2.0f,
                   player_y - stats->size / 2.0f,
                   speed, sep_x, sep_y, 30.0f, dt);
  } else {
    e->vx = 0.0f;
    e->vy = 0.0f;
  }
}

static void update_dying(Enemy_t* e, float dt, float player_x, float player_y)
{
  (void)player_x; (void)player_y;
  e->mm_death_orb_timer += dt;

  if (e->mm_death_orb_timer >= MIMIC_DEATH_ORB_TIME) {
    // Restore weapon
    if (e->mm_stolen_slot >= 0 && e->mm_stolen_weapon > 0) {
      weapons_restore_slot(e->mm_stolen_slot, (WeaponType_t)e->mm_stolen_weapon);
      e->mm_stolen_slot = -1;
      e->mm_stolen_weapon = -1;
    }

    // Transition to corpse
    e->state = ENEMY_STATE_CORPSE;
    e->death_timer = 0.0f;
    e->corpse_consumed = 1; // skip corpse lingering
    blood_spawn(e->x, e->y, e->vx, e->vy, ei_get_stats(e)->radius);
  }
}

// ============================================================================
// Main Update Dispatch
// ============================================================================

void mimic_update(Enemy_t* e, int index, float dt,
                  float player_x, float player_y,
                  float player_vx, float player_vy)
{
  // Cosmetic ticking
  e->mm_chomp_timer += dt;
  e->mm_waddle_phase += dt * MIMIC_WADDLE_SPEED;
  if (e->mm_text_timer > 0.0f) e->mm_text_timer -= dt;
  if (e->mm_hit_flash_timer > 0.0f) e->mm_hit_flash_timer -= dt;

  switch (e->state) {
    case ENEMY_STATE_MM_SPAWNING:
      update_spawning(e, dt, player_x, player_y);
      break;
    case ENEMY_STATE_MM_STEALING:
      update_stealing(e, dt, player_x, player_y);
      break;
    case ENEMY_STATE_MM_FIGHTING:
      update_fighting(e, index, dt, player_x, player_y, player_vx, player_vy);
      break;
    case ENEMY_STATE_MM_DYING:
      update_dying(e, dt, player_x, player_y);
      break;
    default:
      break;
  }
}

// ============================================================================
// Hit Handling
// ============================================================================

int mimic_on_hit(Enemy_t* e, float bullet_vx, float bullet_vy,
                 int killed, float knockback_dist)
{
  if (killed) {
    // Enter dying state instead of normal knockback→corpse
    game_audio_play_mimic_death();
    e->state = ENEMY_STATE_MM_DYING;
    e->mm_death_orb_timer = 0.0f;
    e->mm_text_timer = MIMIC_TEXT_DURATION; // "WEAPON RECOVERED!"
    e->vx = bullet_vx;
    e->vy = bullet_vy;
    return 1;
  }

  // Not killed — only knockback during fighting, ignore during spawning/stealing
  game_audio_play_mimic_hit();
  e->mm_hit_flash_timer = 0.12f;
  if (e->state == ENEMY_STATE_MM_FIGHTING) {
    // No knockback during dash/attack — mimic commits to the attack
    if ((e->mm_stolen_weapon == WEAPON_TRAIL ||
         e->mm_stolen_weapon == WEAPON_ORBIT ||
         e->mm_stolen_weapon == WEAPON_SCYTHE) &&
        e->mm_spin_ai_state != 0) { // not STRAFE
      return 1;
    }
    float reduced_knockback = knockback_dist * 0.5f; // 50% knockback resistance
    ei_start_knockback(e, bullet_vx, bullet_vy, reduced_knockback);
  }
  return 1;
}

// ============================================================================
// Death & Cleanup
// ============================================================================

void mimic_on_death(Enemy_t* e)
{
  // Restore weapon if still held
  if (e->mm_stolen_slot >= 0 && e->mm_stolen_weapon > 0) {
    weapons_restore_slot(e->mm_stolen_slot, (WeaponType_t)e->mm_stolen_weapon);
    e->mm_stolen_slot = -1;
    e->mm_stolen_weapon = -1;
  }
}

void mimic_restore_all_weapons(void)
{
  for (int i = 0; i < max_enemies; i++) {
    if (!enemies[i].active) continue;
    if (enemies[i].type != ENEMY_TYPE_MIMIC) continue;
    mimic_on_death(&enemies[i]);
  }
  // Also call weapons_restore_all_stolen() to handle edge cases
  weapons_restore_all_stolen();
  mimic_wand_cleanup();
  mimic_bomb_cleanup();
  mimic_chain_cleanup();
  mimic_turret_cleanup();
  mimic_trail_cleanup();
  mimic_orbit_cleanup();
  mimic_scythe_cleanup();
}

// ============================================================================
// Drawing
// ============================================================================

void mimic_draw(Enemy_t* e, int r, int g, int b, int alpha)
{
  const EnemyStats_t* stats = ei_get_stats(e);
  float sz = stats->size;
  float cx = e->x + sz / 2.0f;
  float cy = e->y + sz / 2.0f;

  // Hit flash: lerp color toward white
  if (e->mm_hit_flash_timer > 0.0f) {
    float flash_pct = e->mm_hit_flash_timer / 0.12f;
    r = r + (int)((255 - r) * flash_pct);
    g = g + (int)((255 - g) * flash_pct);
    b = b + (int)((255 - b) * flash_pct);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
  }
  aColor_t color = {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)alpha};

  // Waddle rotation (visual only — slight horizontal squish)
  float waddle = sinf(e->mm_waddle_phase) * 2.0f;
  // Violent shake during windup
  if (e->state == ENEMY_STATE_MM_STEALING && e->mm_steal_phase == 0) {
    float intensity = 1.0f - (e->mm_state_timer / MIMIC_STEAL_WINDUP);
    waddle = sinf(e->mm_waddle_phase * 20.0f) * (3.0f + intensity * 5.0f);
  }

  // Mouth open/close animation
  float chomp_cycle = fmodf(e->mm_chomp_timer, MIMIC_CHOMP_RATE) / MIMIC_CHOMP_RATE;
  float mouth_open = 0.3f + 0.7f * fabsf(sinf(chomp_cycle * (float)PI));
  // Mouth wide open during dash/grab and dying
  if (e->state == ENEMY_STATE_MM_DYING) {
    mouth_open = 1.0f;
  }
  if (e->state == ENEMY_STATE_MM_STEALING) {
    if (e->mm_steal_phase == 0) {
      // Windup: mouth gradually opens
      float t = 1.0f - (e->mm_state_timer / MIMIC_STEAL_WINDUP);
      mouth_open = 0.3f + 0.7f * t;
    } else {
      // Dashing or grabbed: wide open
      mouth_open = 1.0f;
    }
  }

  // Chest body: bottom half (base)
  float base_h = sz * 0.55f;
  float lid_h = sz * 0.45f;
  float base_y = cy + (sz * 0.5f - base_h);
  a_DrawFilledRect(
    (aRectf_t){cx - sz / 2.0f + waddle, base_y, sz, base_h},
    color
  );

  // Chest lid (top half) — rotates open by mouth_open amount
  float lid_gap = mouth_open * lid_h * 0.6f; // gap between lid and base
  float lid_y = base_y - lid_h - lid_gap;
  a_DrawFilledRect(
    (aRectf_t){cx - sz / 2.0f + waddle, lid_y, sz, lid_h},
    color
  );

  // Lid top arc (rounded) — small rect on top for "curved lid" look
  a_DrawFilledRect(
    (aRectf_t){cx - sz * 0.4f + waddle, lid_y - 3, sz * 0.8f, 4},
    color
  );

  // Lock/clasp (small detail on front)
  aColor_t clasp_color = {200, 170, 50, (uint8_t)alpha}; // gold
  a_DrawFilledRect(
    (aRectf_t){cx - 2 + waddle, base_y - 1, 4, 4},
    clasp_color
  );

  // Weapon glow inside mouth (when holding a weapon and mouth is open)
  if (e->mm_stolen_weapon > 0 && mouth_open > 0.3f &&
      e->state == ENEMY_STATE_MM_FIGHTING) {
    int wi = e->mm_stolen_weapon;
    if (wi >= 0 && wi <= WEAPON_TRAIL) {
      float glow_alpha = mouth_open * 120.0f;
      float pulse = 0.7f + 0.3f * sinf(e->mm_waddle_phase * 3.0f);
      int ga = (int)(glow_alpha * pulse);
      if (ga > 180) ga = 180;
      aColor_t glow = {
        (uint8_t)weapon_colors[wi][0],
        (uint8_t)weapon_colors[wi][1],
        (uint8_t)weapon_colors[wi][2],
        (uint8_t)ga
      };
      float glow_y = base_y - lid_gap * 0.5f;
      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      a_DrawFilledRect(
        (aRectf_t){cx - sz * 0.3f + waddle, glow_y - 3, sz * 0.6f, 6},
        glow
      );
      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
    }
  }

  // Weapon-specific visuals (Phase 2)
  if (e->state == ENEMY_STATE_MM_FIGHTING) {
    switch (e->mm_stolen_weapon) {
      case WEAPON_WAND: mimic_wand_draw(e); break;
      case WEAPON_SPIN: mimic_spin_draw(e); break;
      case WEAPON_BOMB: mimic_bomb_draw(e); break;
      case WEAPON_CHAIN: mimic_chain_draw(e); break;
      case WEAPON_TURRET: mimic_turret_draw(e); break;
      case WEAPON_TRAIL: mimic_trail_draw(e); break;
      case WEAPON_ORBIT: mimic_orbit_draw(e); break;
      case WEAPON_SCYTHE: mimic_scythe_draw(e); break;
      default: break;
    }
  }

  // "!" telegraph during windup
  if (e->state == ENEMY_STATE_MM_STEALING && e->mm_steal_phase == 0 &&
      e->mm_stolen_weapon > 0) {
    int wi = e->mm_stolen_weapon;
    float pulse = 0.7f + 0.3f * sinf(e->mm_waddle_phase * 10.0f);
    int ta = (int)(255.0f * pulse);
    aColor_t warn_color = {
      (uint8_t)weapon_colors[wi][0],
      (uint8_t)weapon_colors[wi][1],
      (uint8_t)weapon_colors[wi][2],
      (uint8_t)ta
    };
    aTextStyle_t warn_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = warn_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 1.5f
    };
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    a_DrawText("!", (int)cx, (int)(e->y - 30), warn_style);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Steal orb projectile (phase 2: orb flies mimic→player)
  if (e->state == ENEMY_STATE_MM_STEALING && e->mm_steal_phase == 2 &&
      e->mm_stolen_weapon > 0) {
    int wi = e->mm_stolen_weapon;
    aColor_t orb_color = {
      (uint8_t)weapon_colors[wi][0],
      (uint8_t)weapon_colors[wi][1],
      (uint8_t)weapon_colors[wi][2],
      220
    };
    a_DrawFilledCircle((int)e->mm_orb_x, (int)e->mm_orb_y, 6, orb_color);
    // Outer glow
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    aColor_t glow = {orb_color.r, orb_color.g, orb_color.b, 80};
    a_DrawFilledCircle((int)e->mm_orb_x, (int)e->mm_orb_y, 12, glow);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }

  // Death orb return animation (during DYING state)
  if (e->state == ENEMY_STATE_MM_DYING && e->mm_stolen_weapon > 0) {
    float t = e->mm_death_orb_timer / MIMIC_DEATH_ORB_TIME;
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t);

    float px = player_get_x() + 16.0f;
    float py = player_get_y() + 16.0f;
    float orb_x = cx + (px - cx) * t;
    float orb_y = cy + (py - cy) * t;

    int wi = e->mm_stolen_weapon;
    aColor_t orb_color = {
      (uint8_t)weapon_colors[wi][0],
      (uint8_t)weapon_colors[wi][1],
      (uint8_t)weapon_colors[wi][2],
      220
    };
    a_DrawFilledCircle((int)orb_x, (int)orb_y, 6, orb_color);
  }

  // "WEAPON STOLEN!" / "WEAPON RECOVERED!" text
  if (e->mm_text_timer > 0.0f) {
    float text_alpha = e->mm_text_timer / MIMIC_TEXT_DURATION;
    if (text_alpha > 1.0f) text_alpha = 1.0f;
    int ta = (int)(text_alpha * 255.0f);

    const char* text;
    aColor_t text_color;
    if (e->state == ENEMY_STATE_MM_DYING || e->mm_stolen_weapon <= 0) {
      text = "WEAPON RECOVERED!";
      text_color = (aColor_t){50, 255, 50, (uint8_t)ta};
    } else {
      text = "WEAPON STOLEN!";
      text_color = (aColor_t){255, 50, 50, (uint8_t)ta};
    }

    // Pulse scale
    float pulse = 1.0f + 0.1f * sinf(e->mm_text_timer * 8.0f);
    aTextStyle_t style = {
      .type = FONT_ENTER_COMMAND,
      .fg = text_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 1.0f * pulse
    };
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    a_DrawText(text, (int)cx, (int)(e->y - 35), style);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
  }
}
