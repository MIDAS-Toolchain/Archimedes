#include <math.h>
#include <stdlib.h>
#include "Archimedes.h"
#include "fire_particles.h"

#define FIRE_PARTICLE_SIZE   3.0f
#define FIRE_MIN_PARTICLES   20
#define FIRE_MAX_PARTICLES   35
#define FIRE_LIFETIME        0.4f
#define FIRE_SPILL_DURATION  0.25f  // How long sparks fly before dying out

typedef struct {
  float x, y;
  float vx, vy;
  float lifetime;
  int active;
} FireParticle_t;

static FireParticle_t* particles = NULL;
static int max_particles = 0;

void fire_particles_init(int max_count)
{
  max_particles = max_count;
  particles = (FireParticle_t*)calloc(max_particles, sizeof(FireParticle_t));
}

void fire_particles_cleanup(void)
{
  free(particles);
  particles = NULL;
  max_particles = 0;
}

void fire_particles_spawn(float x, float y, float dir_x, float dir_y)
{
  int count = FIRE_MIN_PARTICLES + (rand() % (FIRE_MAX_PARTICLES - FIRE_MIN_PARTICLES + 1));

  float len = sqrtf(dir_x * dir_x + dir_y * dir_y);
  if (len > 0.1f) { dir_x /= len; dir_y /= len; }

  for (int i = 0; i < count; i++) {
    for (int j = 0; j < max_particles; j++) {
      if (!particles[j].active) {
        // Spawn at hit point with slight offset along direction
        float offset = RANDF(4.0f, 14.0f);
        particles[j].x = x + dir_x * offset;
        particles[j].y = y + dir_y * offset;

        // Explosive burst: mostly along direction with wide spread
        float speed = RANDF(200.0f, 500.0f);
        float angle = RANDF(-PI * 0.6f, PI * 0.6f);
        float c = cosf(angle);
        float s = sinf(angle);
        particles[j].vx = (dir_x * c - dir_y * s) * speed;
        particles[j].vy = (dir_x * s + dir_y * c) * speed;

        particles[j].lifetime = 0.0f;
        particles[j].active = 1;
        break;
      }
    }
  }
}

void fire_particles_update(float dt)
{
  for (int i = 0; i < max_particles; i++) {
    FireParticle_t* p = &particles[i];
    if (!p->active) continue;

    p->lifetime += dt;

    if (p->lifetime >= FIRE_LIFETIME) {
      p->active = 0;
      continue;
    }

    // Sparks fly out fast and decelerate hard
    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->vx *= 0.88f;
    p->vy *= 0.88f;
    // Gravity pulls sparks down
    p->vy += 300.0f * dt;
  }
}

void fire_particles_draw(void)
{
  for (int i = 0; i < max_particles; i++) {
    FireParticle_t* p = &particles[i];
    if (!p->active) continue;

    float life_pct = p->lifetime / FIRE_LIFETIME;

    // Bright start, quick fade
    int alpha = (int)(255.0f * (1.0f - life_pct * life_pct));
    if (alpha < 0) alpha = 0;

    // White-hot -> yellow -> orange -> red as they cool
    int r, g, b;
    if (life_pct < 0.15f) {
      // White-hot spark
      r = 255; g = 255; b = 200;
    } else if (life_pct < 0.4f) {
      // Bright yellow
      r = 255; g = 230; b = 50;
    } else {
      // Orange to red
      float t = (life_pct - 0.4f) / 0.6f;
      r = 255;
      g = (int)(180.0f * (1.0f - t));
      b = 0;
    }

    // Sparks shrink as they die
    float size = FIRE_PARTICLE_SIZE * (1.0f - life_pct * 0.7f);
    if (size < 1.0f) size = 1.0f;

    a_DrawFilledRect(
      (aRectf_t){p->x - size / 2.0f, p->y - size / 2.0f, size, size},
      (aColor_t){r, g, b, alpha}
    );
  }
}
