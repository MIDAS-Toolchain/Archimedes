#ifndef XP_H
#define XP_H

void xp_init(void);
void xp_reset(void);
void xp_update(float dt);
void xp_draw(void);

// Called from collision.c when an enemy dies
void xp_spawn_orbs(float death_x, float death_y, int enemy_type);

// Leveling queries
int   xp_get_level(void);
float xp_get_level_progress(void);  // 0.0-1.0 for XP bar
int   xp_get_current(void);         // current XP toward next level
int   xp_get_needed(void);          // XP needed for next level

// Returns and clears the pending level-up flag
int xp_check_level_up(void);

// Upgrade points earned this run (sum of level numbers on each level-up)
int xp_get_pending_upgrade_points(void);

#endif /* XP_H */
