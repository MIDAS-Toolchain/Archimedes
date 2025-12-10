/* 
 * @file src/aInput.c
 *
 * This file defines the functions used to process input events from the game window.
 * By continuously polling for SDL events (keyboard, mouse, quit, text input), then dispatching
 * to internal static functions. It updates the global `app` state based on these events.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <SDL2/SDL_mouse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Archimedes.h" 

static void a_DoKeyDown( SDL_KeyboardEvent *event );
static void a_DoKeyUp( SDL_KeyboardEvent *event );
static void a_DoMouseDown( SDL_MouseButtonEvent* button );
static void a_DoMouseUp( SDL_MouseButtonEvent* button );
static void a_DoMouseWheel( SDL_MouseWheelEvent* wheel );
static void a_DoMouseMotion( SDL_MouseMotionEvent* motion );

void a_DoInput( void )
{
  SDL_Event event;

  while ( SDL_PollEvent( &event ) )
  {
    switch ( event.type )
    {
      case SDL_QUIT:
        app.running = 0;
        break;

      case SDL_KEYDOWN:
        a_DoKeyDown( &event.key );
        break;

      case SDL_KEYUP:
        a_DoKeyUp( &event.key );
        break;

      case SDL_MOUSEBUTTONDOWN:
        a_DoMouseDown( &event.button );
        break;

      case SDL_MOUSEBUTTONUP:
        a_DoMouseUp( &event.button );
        break;

      case SDL_MOUSEWHEEL:
        a_DoMouseWheel( &event.wheel );
        break;

      case SDL_TEXTINPUT:
        // Validate input length before copying to prevent buffer overflows.
        // Assumes MAX_INPUT_LENGTH is defined in Archimedes.h and includes space for null terminator.
        if (strlen(event.text.text) > 0 && strlen(event.text.text) < MAX_INPUT_LENGTH) {
          STRNCPY( app.input_text, event.text.text, MAX_INPUT_LENGTH );
        } else {
          printf("Warning: Invalid text input length: %zu (max allowed: %d)\n",
                 strlen(event.text.text), MAX_INPUT_LENGTH - 1);
        }
        break;

      case SDL_MOUSEMOTION:
        a_DoMouseMotion( &event.motion );
        break;

      default:
        // Silently ignore unknown events (normal behavior for unhandled types)
        break;
    }
  }
}

/**
 * @brief Handles SDL keyboard key press events and updates the global keyboard state.
 *
 * This internal function is called by `a_DoInput()` when an `SDL_KEYDOWN` event occurs.
 * It sets the corresponding scancode in the `app.keyboard` array to `1` and records the
 * `last_key_pressed`. Key repeat events are explicitly ignored to provide clean,
 * single-press detection for game logic.
 *
 * @param event Pointer to the `SDL_KeyboardEvent` structure containing key press details.
 *
 */
static void a_DoKeyDown( SDL_KeyboardEvent *event )
{
  // Defensive programming: Check for NULL pointer
  if (event == NULL) {
    printf("Error: NULL keyboard event passed to a_DoKeyDown.\n");
    return;
  }

  // Validate scancode range (critical bounds check to prevent array out-of-bounds access)
  if (event->keysym.scancode < 0 || event->keysym.scancode >= MAX_KEYBOARD_KEYS) {
    printf("Warning: Scancode %d is out of valid range [0, %d) in a_DoKeyDown. Ignoring event.\n",
           event->keysym.scancode, MAX_KEYBOARD_KEYS);
    return;
  }

  // Validate repeat flag (should be 0 for initial press, >0 for repeats)
  if (event->repeat > 0) { // SDL's repeat is 0 for first press, >0 for repeats
    // printf("Debug: Ignoring key repeat event for scancode %d (repeat: %d).\n", event->keysym.scancode, event->repeat);
    return; // Ignore key repeat events
  }

  // If all checks pass and it's a non-repeat press, update the keyboard state
  app.keyboard[event->keysym.scancode] = 1;
  app.last_key_pressed = event->keysym.scancode;
  // printf("Debug: Key Down: Scancode %d (repeat: %d)\n", event->keysym.scancode, event->repeat);
}

/**
 * @brief Handles SDL keyboard key release events and updates the global keyboard state.
 *
 * This internal function is called by `a_DoInput()` when an `SDL_KEYUP` event occurs.
 * It sets the corresponding scancode in the `app.keyboard` array to `0`.
 *
 * @param event Pointer to the `SDL_KeyboardEvent` structure containing key release details.
 *
 */
static void a_DoKeyUp( SDL_KeyboardEvent *event )
{
  // Defensive programming: Check for NULL pointer
  if (event == NULL) {
    printf("Error: NULL keyboard event passed to a_DoKeyUp.\n");
    return;
  }

  // Validate scancode range (critical bounds check)
  if (event->keysym.scancode < 0 || event->keysym.scancode >= MAX_KEYBOARD_KEYS) {
    printf("Warning: Scancode %d is out of valid range [0, %d) in a_DoKeyUp. Ignoring event.\n",
           event->keysym.scancode, MAX_KEYBOARD_KEYS);
    return;
  }

  // Validate repeat flag (should be 0 for initial release, >0 for repeats)
  if (event->repeat > 0) { // SDL's repeat is 0 for first press, >0 for repeats
    // printf("Debug: Ignoring key repeat event for scancode %d (repeat: %d).\n", event->keysym.scancode, event->repeat);
    return; // Ignore key repeat events
  }

  // If all checks pass and it's a non-repeat release, update the keyboard state
  app.keyboard[event->keysym.scancode] = 0;
  // printf("Debug: Key Up: Scancode %d (repeat: %d)\n", event->keysym.scancode, event->repeat);
}

/**
 * @brief Handles SDL mouse button press events and updates the global mouse state.
 *
 * This internal function is called by `a_DoInput()` when an `SDL_MOUSEBUTTONDOWN` event occurs.
 * It updates the `app.mouse` structure with the button's `SDL_PRESSED` state,
 * the number of clicks (`button->clicks`), and the mouse cursor's `x` and `y` coordinates
 * relative to the window.
 *
 * @param button Pointer to the `SDL_MouseButtonEvent` structure containing button press details.
 *
 */
static void a_DoMouseDown( SDL_MouseButtonEvent* button )
{
  // Defensive programming: Check for NULL pointer
  if (button == NULL) {
    printf("Error: NULL mouse button event passed to a_DoMouseDown.\n");
    return;
  }

  // Update global mouse state (all checks passed or warnings logged)
  app.mouse.state  = button->state;
  app.mouse.button = button->button; // Current code sets button to 0 for DOWN events
  app.mouse.clicks = button->clicks;
  app.mouse.x = button->x;
  app.mouse.y = button->y;
  app.mouse.pressed = 1; // Explicitly mark as pressed
  //printf("Debug: Mouse Down: Button %d, State %d, Clicks %d, Pos (%d, %d)\n", button->button, button->state, button->clicks, button->x, button->y);
}

/**
 * @brief Handles SDL mouse button release events and updates the global mouse state.
 *
 * This internal function is called by `a_DoInput()` when an `SDL_MOUSEBUTTONUP` event occurs.
 * It updates the `app.mouse` structure with the button's `SDL_RELEASED` state,
 * the specific `button->button` ID, the number of clicks (`button->clicks`),
 * and the mouse cursor's `x` and `y` coordinates relative to the window.
 *
 * @param button Pointer to the `SDL_MouseButtonEvent` structure containing button release details.
 *
 */
static void a_DoMouseUp( SDL_MouseButtonEvent* button )
{
  // Defensive programming: Check for NULL pointer
  if (button == NULL) {
    printf("Error: NULL mouse button event passed to a_DoMouseUp.\n");
    return;
  }

  // Update global mouse state
  app.mouse.state  = button->state;
  app.mouse.button = button->button; // Current code sets actual button for UP events
  app.mouse.clicks = button->clicks;
  app.mouse.x = button->x;
  app.mouse.y = button->y;
  app.mouse.pressed = 0; // Explicitly mark as not pressed
  //printf("Debug: Mouse Up: Button %d, State %d, Clicks %d, Pos (%d, %d)\n", button->button, button->state, button->clicks, button->x, button->y);
}

/**
 * @brief Handles SDL mouse wheel events and updates the global wheel state.
 *
 * @param wheel Pointer to the `SDL_MouseWheelEvent` structure containing wheel scroll details.
 *
 */
static void a_DoMouseWheel( SDL_MouseWheelEvent* wheel )
{
  // Defensive programming: Check for NULL pointer
  if (wheel == NULL) {
    printf("Error: NULL mouse wheel event passed to a_DoMouseWheel.\n");
    return;
  }

  // Validate wheel direction values (typically -1 or 1, but be defensive)
  if (wheel->y < -10 || wheel->y > 10) { // Arbitrary range for unusual hardware
    printf("Warning: Extreme vertical wheel scroll value: %d in a_DoMouseWheel. Processing anyway.\n", wheel->y);
  }

  // Log horizontal wheel events for debugging (currently not used in app.mouse state)
  if (wheel->x != 0) {
    printf("Info: Horizontal wheel scroll detected: %d in a_DoMouseWheel. Not processed into app.mouse.wheel.\n", wheel->x);
  }

  // Update global wheel state
  app.mouse.wheel = wheel->y;
  // printf("Debug: Mouse Wheel: X=%d, Y=%d\n", wheel->x, wheel->y);
}

static void a_DoMouseMotion( SDL_MouseMotionEvent* motion )
{
  // Defensive programming: Check for NULL pointer (less critical for motion, but good practice)
  if (motion == NULL) {
    printf("Error: NULL mouse motion event passed to a_DoMouseMotion.\n");
    return;
  }

  // Update mouse coordinates directly
  app.mouse.x = motion->x;
  app.mouse.y = motion->y;

  app.mouse.motion = 1;
  // printf("Debug: Mouse Motion: Pos (%d, %d)\n", motion->x, motion->y);
}

