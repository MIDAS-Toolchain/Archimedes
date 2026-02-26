#ifndef MENU_H
#define MENU_H

#include "Archimedes.h"

// What the menu produced this frame
typedef enum {
  MENU_NONE,       // Nothing happened
  MENU_NAVIGATE,   // Selection changed (sound already played)
  MENU_CONFIRM,    // Enter/Space pressed on current selection (sound already played)
  MENU_BACK,       // ESC/Backspace pressed
} MenuResult_t;

// Navigation axis
typedef enum {
  MENU_AXIS_VERTICAL,    // UP/DOWN + W/S
  MENU_AXIS_HORIZONTAL,  // LEFT/RIGHT + A/D
} MenuAxis_t;

// Sound config — pointers to loaded sounds (NULL = no sound)
typedef struct {
  aSoundEffect_t* move_sound;
  int              move_volume;
  aSoundEffect_t* click_sound;
  int              click_volume;
} MenuSounds_t;

// Hit rect for non-FlexBox menus
typedef struct {
  int x, y, w, h;
} MenuRect_t;

// The menu state
typedef struct {
  int             selected;   // Current selection index
  int             count;      // Number of items
  MenuAxis_t      axis;       // Navigation direction
  MenuSounds_t    sounds;     // Sound config
  FlexBox_t*      flex;       // If non-NULL, hit rects come from FlexBox items
  MenuRect_t*     rects;      // If flex is NULL, use caller-provided rects
} Menu_t;

// Call once per frame before any menu_update calls
void menu_update_mouse(void);

// Whether the mouse moved this frame (for custom hover outside Menu_t)
int menu_mouse_moved(void);

// Process input for a menu. Returns what happened.
MenuResult_t menu_update(Menu_t* menu);

// Convenience: build a MenuSounds_t
MenuSounds_t menu_sounds(aSoundEffect_t* move, int move_vol,
                         aSoundEffect_t* click, int click_vol);

// Utility: point-in-rect test
int menu_point_in_rect(int px, int py, int rx, int ry, int rw, int rh);

#endif /* MENU_H */
