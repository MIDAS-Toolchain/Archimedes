#include "menu.h"
#include <SDL2/SDL.h>

// ============================================================================
// Mouse movement tracking
// ============================================================================

static int last_mouse_x = -1;
static int last_mouse_y = -1;
static int mouse_moved  = 0;

void menu_update_mouse(void)
{
  int mx = app.mouse.x;
  int my = app.mouse.y;
  mouse_moved = (mx != last_mouse_x || my != last_mouse_y);
  last_mouse_x = mx;
  last_mouse_y = my;
}

int menu_mouse_moved(void)
{
  return mouse_moved;
}

// ============================================================================
// Utility
// ============================================================================

int menu_point_in_rect(int px, int py, int rx, int ry, int rw, int rh)
{
  return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

MenuSounds_t menu_sounds(aSoundEffect_t* move, int move_vol,
                         aSoundEffect_t* click, int click_vol)
{
  MenuSounds_t s;
  s.move_sound   = move;
  s.move_volume  = move_vol;
  s.click_sound  = click;
  s.click_volume = click_vol;
  return s;
}

// ============================================================================
// Internal helpers
// ============================================================================

static void play_move(const MenuSounds_t* s)
{
  if (s->move_sound)
  {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume  = s->move_volume,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(s->move_sound, &opts);
  }
}

static void play_click(const MenuSounds_t* s)
{
  if (s->click_sound)
  {
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_AUTO,
      .volume  = s->click_volume,
      .loops = 0, .fade_ms = 0, .interrupt = 0
    };
    a_AudioPlaySound(s->click_sound, &opts);
  }
}

// Get hit rect for item i (from FlexBox or caller-provided rects)
static int get_rect(const Menu_t* menu, int i, int* ox, int* oy, int* ow, int* oh)
{
  if (menu->flex)
  {
    const FlexItem_t* item = a_FlexGetItem(menu->flex, i);
    if (!item) return 0;
    *ox = item->calc_x;
    *oy = item->calc_y;
    *ow = item->w;
    *oh = item->h;
    return 1;
  }
  if (menu->rects)
  {
    *ox = menu->rects[i].x;
    *oy = menu->rects[i].y;
    *ow = menu->rects[i].w;
    *oh = menu->rects[i].h;
    return 1;
  }
  return 0;
}

// ============================================================================
// menu_update
// ============================================================================

MenuResult_t menu_update(Menu_t* menu)
{
  if (menu->count <= 0) return MENU_NONE;

  // --- ESC / Backspace → MENU_BACK ---
  if (app.keyboard[SDL_SCANCODE_ESCAPE] == 1 ||
      app.keyboard[SDL_SCANCODE_BACKSPACE] == 1)
  {
    app.keyboard[SDL_SCANCODE_ESCAPE]    = 0;
    app.keyboard[SDL_SCANCODE_BACKSPACE] = 0;
    return MENU_BACK;
  }

  // --- Keyboard navigation ---
  {
    int prev_key, prev_alt, next_key, next_alt;

    if (menu->axis == MENU_AXIS_VERTICAL)
    {
      prev_key = SDL_SCANCODE_UP;    prev_alt = SDL_SCANCODE_W;
      next_key = SDL_SCANCODE_DOWN;  next_alt = SDL_SCANCODE_S;
    }
    else
    {
      prev_key = SDL_SCANCODE_LEFT;  prev_alt = SDL_SCANCODE_A;
      next_key = SDL_SCANCODE_RIGHT; next_alt = SDL_SCANCODE_D;
    }

    if (app.keyboard[prev_key] == 1 || app.keyboard[prev_alt] == 1)
    {
      app.keyboard[prev_key] = 0;
      app.keyboard[prev_alt] = 0;
      menu->selected = (menu->selected - 1 + menu->count) % menu->count;
      play_move(&menu->sounds);
      return MENU_NAVIGATE;
    }
    if (app.keyboard[next_key] == 1 || app.keyboard[next_alt] == 1)
    {
      app.keyboard[next_key] = 0;
      app.keyboard[next_alt] = 0;
      menu->selected = (menu->selected + 1) % menu->count;
      play_move(&menu->sounds);
      return MENU_NAVIGATE;
    }
  }

  // --- Enter / Space → MENU_CONFIRM ---
  if (app.keyboard[SDL_SCANCODE_RETURN] == 1 ||
      app.keyboard[SDL_SCANCODE_SPACE] == 1)
  {
    app.keyboard[SDL_SCANCODE_RETURN] = 0;
    app.keyboard[SDL_SCANCODE_SPACE]  = 0;
    play_click(&menu->sounds);
    return MENU_CONFIRM;
  }

  // --- Mouse hover ---
  if (mouse_moved)
  {
    int mx = app.mouse.x, my = app.mouse.y;
    for (int i = 0; i < menu->count; i++)
    {
      int rx, ry, rw, rh;
      if (get_rect(menu, i, &rx, &ry, &rw, &rh) &&
          menu_point_in_rect(mx, my, rx, ry, rw, rh))
      {
        if (menu->selected != i)
        {
          menu->selected = i;
          play_move(&menu->sounds);
        }
        break;
      }
    }
  }

  // --- Mouse click ---
  if (app.mouse.pressed && app.mouse.button == SDL_BUTTON_LEFT)
  {
    int mx = app.mouse.x, my = app.mouse.y;
    for (int i = 0; i < menu->count; i++)
    {
      int rx, ry, rw, rh;
      if (get_rect(menu, i, &rx, &ry, &rw, &rh) &&
          menu_point_in_rect(mx, my, rx, ry, rw, rh))
      {
        app.mouse.pressed = 0;
        menu->selected = i;
        play_click(&menu->sounds);
        return MENU_CONFIRM;
      }
    }
    app.mouse.pressed = 0;
  }

  return MENU_NONE;
}
