#ifndef SNAKE_H
#define SNAKE_H

void snake_init(void);
void snake_cleanup(void);
void snake_spawn(int segment_count, int hp_per_segment);
void snake_update(float dt, float px, float py, float pvx, float pvy);
void snake_draw(void);

// Collision — called by weapon systems
int  snake_check_hit_aoe(float x, float y, float radius);
int  snake_check_hit_no_knockback(float x, float y, float radius);
int  snake_check_bullet_hit(float bx, float by, float bullet_radius);
void snake_check_wand_bullets(void);

// Debuffs
void snake_set_conductor(int snake_index, float duration, int jumps);
void snake_set_stun(int snake_index, float duration, float damage_mult);
void snake_apply_conductor_aoe(float x, float y, float radius, float duration, int jumps);

// Queries
int   snake_get_active_count(void);
int   snake_get_max_count(void);
int   snake_is_alive(int snake_index);
void  snake_get_head_position(int snake_index, float* out_x, float* out_y);
float snake_get_head_radius(int snake_index);
void  snake_displace(int snake_index, float dx, float dy);

// Targeting — returns 1 if a snake head is closer than *best_dist, updates out params
int  snake_find_nearest_head(float px, float py, float* best_dist,
                             float* out_x, float* out_y);

// Scale shatter projectiles (update/draw from main loop)
void snake_shatter_update(float dt);
void snake_shatter_draw(void);

// Shaman integration
int   snake_is_on_screen_active(int snake_index);
float snake_get_damage_ratio(int snake_index);
void  snake_get_tail_position(int snake_index, float* out_x, float* out_y);
int   snake_apply_heal(int snake_index, int heal_amount);

#endif /* SNAKE_H */
