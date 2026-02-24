#include <math.h>
#include <stdlib.h>
#include "Archimedes.h"
#include "blood.h"

#define BLOOD_SPILL_DURATION 0.2f
#define BLOOD_PARTICLE_SIZE  3.0f
#define BLOOD_MIN_PARTICLES  30
#define BLOOD_MAX_PARTICLES  50
#define BLOOD_LIFETIME       6.0f
#define BLOOD_FADE_START     4.0f
#define OFFSCREEN_MARGIN     100.0f

typedef struct {
  float x, y;
  float vx, vy;
  float lifetime;
  int active;
  int frozen;
} BloodParticle_t;

static BloodParticle_t* particles = NULL;
static int max_particles = 0;

static float normalize(float* x, float* y)
{
  float len = sqrtf((*x) * (*x) + (*y) * (*y));
  if (len > 0.1f) {
    *x /= len;
    *y /= len;
  }
  return len;
}

static int is_offscreen(float x, float y)
{
  return x < -OFFSCREEN_MARGIN || x > SCREEN_WIDTH + OFFSCREEN_MARGIN ||
         y < -OFFSCREEN_MARGIN || y > SCREEN_HEIGHT + OFFSCREEN_MARGIN;
}

void blood_init(int max_count)
{
  max_particles = max_count;
  particles = (BloodParticle_t*)calloc(max_particles, sizeof(BloodParticle_t));
}

void blood_cleanup(void)
{
  free(particles);
  particles = NULL;
  max_particles = 0;
}

void blood_spawn(float x, float y, float dir_x, float dir_y, float entity_radius)
{
  int count = BLOOD_MIN_PARTICLES + (rand() % (BLOOD_MAX_PARTICLES - BLOOD_MIN_PARTICLES + 1));

  normalize(&dir_x, &dir_y);

  for (int i = 0; i < count; i++) {
    for (int j = 0; j < max_particles; j++) {
      if (!particles[j].active) {
        float offset = RANDF(12.0f, 24.0f);
        particles[j].x = x + entity_radius + dir_x * offset;
        particles[j].y = y + entity_radius + dir_y * offset;

        float speed = RANDF(150.0f, 400.0f);
        float angle = RANDF(-PI * 0.6f, PI * 0.6f);
        float c = cosf(angle);
        float s = sinf(angle);
        particles[j].vx = (dir_x * c - dir_y * s) * speed;
        particles[j].vy = (dir_x * s + dir_y * c) * speed;

        particles[j].lifetime = 0.0f;
        particles[j].active = 1;
        particles[j].frozen = 0;
        break;
      }
    }
  }
}

void blood_update(float dt)
{
  for (int i = 0; i < max_particles; i++) {
    BloodParticle_t* p = &particles[i];
    if (!p->active) continue;

    p->lifetime += dt;

    if (p->lifetime >= BLOOD_LIFETIME) {
      p->active = 0;
      continue;
    }

    if (!p->frozen && p->lifetime >= BLOOD_SPILL_DURATION) {
      p->frozen = 1;
      p->vx = 0.0f;
      p->vy = 0.0f;
    }

    if (!p->frozen) {
      p->x += p->vx * dt;
      p->y += p->vy * dt;
      p->vx *= 0.95f;
      p->vy *= 0.95f;
      p->vy += 200.0f * dt;
    }

    if (is_offscreen(p->x, p->y)) {
      p->active = 0;
    }
  }
}

void blood_draw(void)
{
  for (int i = 0; i < max_particles; i++) {
    BloodParticle_t* p = &particles[i];
    if (!p->active) continue;

    int alpha = 255;
    if (p->lifetime > BLOOD_FADE_START) {
      float fade = (p->lifetime - BLOOD_FADE_START) / (BLOOD_LIFETIME - BLOOD_FADE_START);
      alpha = (int)(255 * (1.0f - fade));
      if (alpha < 0) alpha = 0;
    }

    a_DrawFilledRect(
      (aRectf_t){p->x, p->y, BLOOD_PARTICLE_SIZE, BLOOD_PARTICLE_SIZE},
      (aColor_t){139, 0, 0, alpha}
    );
  }
}
