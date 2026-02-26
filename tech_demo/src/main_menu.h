#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include "Archimedes.h"
#include "menu.h"

// Initialize main menu (load sounds, etc.)
void main_menu_init(void);

// Scene logic + draw (called from main loop dispatch)
void scene_main_menu_logic(float dt);
void scene_main_menu_draw(float dt);

// Re-create the flex layout (called when returning from pause)
void mainmenu_begin(void);

// Controls panel — shared between main menu and pause
void draw_hotkeys(int cx, int top_y);

// Menu sounds — shared across all menus
extern aSoundEffect_t menu_move_sound;
extern int             menu_move_loaded;
extern aSoundEffect_t menu_click_sound;
extern int             menu_click_loaded;

#endif /* MAIN_MENU_H */
