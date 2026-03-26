/* 
 * src/aWidgets.c
 *
 * This file defines the functions used to create, draw,
 * and handle widget inputs.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include "Archimedes.h"

// Function prototypes with static linkage (internal to this compilation unit)
static void LoadWidgets( const char* filename );
static void ChangeWidgetValue( const int value );

static void CreateWidget( aAUFNode_t* root );

static void DoInputWidget( void );
static void DoControlWidget( void );

static aWidget_t widget_head;
static aWidget_t* widget_tail = NULL;

/* current AUF file being loaded — for error messages */
static const char* g_auf_filename = NULL;

static double slider_delay;
static double cursor_blink;
static aWidget_t* pending_press_widget;
static int pending_press_source; /* 0 = mouse, 1 = keyboard */
static aTimer_t* press_timer;
static int pending_press_on_release; /* 0 = on-press (timer), 1 = on-release (wait for release) */
static int last_mouse_x;
static int last_mouse_y;
static aWidget_t* last_hovered_widget = NULL;

void a_DoWidget( void )
{
  slider_delay = MAX( slider_delay - a_GetDeltaTime(), 0 );

  cursor_blink += a_GetDeltaTime();

  /* Handle pending press */
  if ( pending_press_widget != NULL )
  {
    pending_press_widget->state = WI_PRESSED;

    int fire = 0;

    if ( pending_press_on_release )
    {
      /* On-release: wait for input release */
      if ( pending_press_source == 0 )
      {
        fire = !app.mouse.pressed;
      }
      else
      {
        fire = !app.keyboard[SDL_SCANCODE_SPACE] &&
               !app.keyboard[SDL_SCANCODE_RETURN];
      }
    }
    else
    {
      /* On-press: wait for timer */
      fire = a_TimerOneshot( press_timer, 150 );
    }

    if ( fire )
    {
      if ( pending_press_widget->action != NULL )
      {
        pending_press_widget->action();
      }
      pending_press_widget = NULL;
    }
    return;
  }

  ClearWidgetsState();

  if ( !handle_input_widget && !handle_control_widget )
  {
    /* Track real mouse movement per frame */
    mouse_moved = ( app.mouse.x != last_mouse_x || app.mouse.y != last_mouse_y );
    last_mouse_x = app.mouse.x;
    last_mouse_y = app.mouse.y;

    /* Mouse click — always allowed */
    aWidget_t* current = GetCurrentWidget();
    if ( current != NULL )
    {
      if ( app.mouse.button == 1 && app.mouse.pressed == 1 )
      {
        current->state = WI_PRESSED;
        app.active_widget = current;

        if ( current->click_sound != NULL )
        {
          aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_UI_CLICK, .volume = -1,
                                   .loops = 0, .fade_ms = 0, .interrupt = 1 };
          a_AudioPlaySound( current->click_sound, &opts );
        }

        pending_press_widget = current;
        pending_press_source = 0;
        pending_press_on_release = current->on_release;
        app.mouse.button = 0;
        if ( !current->on_release )
        {
          app.mouse.pressed = 0;
          a_TimerStart( press_timer );
        }
        return;
      }

      /* Mouse hover — only if mouse actually moved */
      if ( mouse_moved && WithinRangePadded( app.mouse.x, app.mouse.y, current ) )
      {
        current->state = WI_HOVERING;

        if ( current != last_hovered_widget )
        {
          last_hovered_widget = current;
          if ( current->hover_sound != NULL )
          {
            aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_UI_HOVER, .volume = -1,
                                     .loops = 0, .fade_ms = 0, .interrupt = 0 };
            a_AudioPlaySound( current->hover_sound, &opts );
          }
        }
      }
    }

    /* If active_widget is a container, track it for keyboard nav */
    if ( focused_container == NULL &&
         app.active_widget != NULL && app.active_widget->type == WT_CONTAINER )
    {
      focused_container = app.active_widget;
    }

    /* Tab / Shift+Tab — cycle focus between top-level containers */
    if ( app.keyboard[SDL_SCANCODE_TAB] )
    {
      app.keyboard[SDL_SCANCODE_TAB] = 0;

      int direction = app.keyboard[SDL_SCANCODE_LSHIFT] ||
                      app.keyboard[SDL_SCANCODE_RSHIFT] ? -1 : 1;

      aWidget_t* candidates[64];
      int count = 0;
      for ( aWidget_t* w = widget_head.next; w != NULL && count < 64; w = w->next )
      {
        if ( w->type == WT_CONTAINER && w->hidden == 0 )
          candidates[count++] = w;
      }

      if ( count > 0 )
      {
        int current = -1;
        for ( int i = 0; i < count; i++ )
        {
          if ( candidates[i] == focused_container )
          {
            current = i;
            break;
          }
        }

        /* If no container focused yet, position so first Tab → 0, first Shift+Tab → last */
        if ( current == -1 )
          current = ( direction == 1 ) ? count - 1 : 0;

        int next = ( current + direction + count ) % count;

        if ( candidates[next] != focused_container )
        {
          focused_container = candidates[next];
          app.active_widget = candidates[next];

          aContainerWidget_t* con = ( aContainerWidget_t* )candidates[next]->data;
          if ( con->num_components > 0 )
          {
            if ( direction == 1 )
              con->focus_index = 0;
            else
              con->focus_index = con->num_components - 1;
          }
        }
      }
    }

    /* Keyboard navigation within focused container */
    if ( focused_container != NULL && focused_container->type == WT_CONTAINER )
    {
      aContainerWidget_t* con = ( aContainerWidget_t* )focused_container->data;

      /* Nav keys depend on flex direction */
      int prev_key, prev_alt, next_key, next_alt;
      int val_neg_key, val_neg_alt, val_pos_key, val_pos_alt;

      if ( focused_container->flex == 1 )
      {
        /* Horizontal: left/right navigate, up/down change value */
        prev_key = SDL_SCANCODE_LEFT;  prev_alt = SDL_SCANCODE_A;
        next_key = SDL_SCANCODE_RIGHT; next_alt = SDL_SCANCODE_D;
        val_neg_key = SDL_SCANCODE_UP;   val_neg_alt = SDL_SCANCODE_W;
        val_pos_key = SDL_SCANCODE_DOWN; val_pos_alt = SDL_SCANCODE_S;
      }
      else
      {
        /* Vertical / grid / manual: up/down navigate, left/right change value */
        prev_key = SDL_SCANCODE_UP;    prev_alt = SDL_SCANCODE_W;
        next_key = SDL_SCANCODE_DOWN;  next_alt = SDL_SCANCODE_S;
        val_neg_key = SDL_SCANCODE_LEFT;  val_neg_alt = SDL_SCANCODE_A;
        val_pos_key = SDL_SCANCODE_RIGHT; val_pos_alt = SDL_SCANCODE_D;
      }

      if ( app.keyboard[prev_key] || app.keyboard[prev_alt] )
      {
        app.keyboard[prev_key] = app.keyboard[prev_alt] = 0;

        int idx = con->focus_index;
        for ( int attempts = 0; attempts < con->num_components; attempts++ )
        {
          idx--;
          if ( idx < 0 ) idx = con->num_components - 1;
          if ( con->components[idx].hidden == 0 &&
               con->components[idx].type != WT_OUTPUT )
          {
            con->focus_index = idx;
            break;
          }
        }
      }

      if ( app.keyboard[next_key] || app.keyboard[next_alt] )
      {
        app.keyboard[next_key] = app.keyboard[next_alt] = 0;

        int idx = con->focus_index;
        for ( int attempts = 0; attempts < con->num_components; attempts++ )
        {
          idx++;
          if ( idx >= con->num_components ) idx = 0;
          if ( con->components[idx].hidden == 0 &&
               con->components[idx].type != WT_OUTPUT )
          {
            con->focus_index = idx;
            break;
          }
        }
      }

      /* Keep focused component in hover state */
      aWidget_t* focused = &con->components[con->focus_index];
      if ( focused->hidden == 0 )
      {
        focused->state = WI_HOVERING;

        if ( focused != last_hovered_widget )
        {
          last_hovered_widget = focused;
          if ( focused->hover_sound != NULL )
          {
            aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_UI_HOVER, .volume = -1,
                                     .loops = 0, .fade_ms = 0, .interrupt = 0 };
            a_AudioPlaySound( focused->hover_sound, &opts );
          }
        }
      }

      if ( focused->type == WT_SELECT || focused->type == WT_SLIDER )
      {
        if ( app.keyboard[val_neg_key] || app.keyboard[val_neg_alt] )
        {
          app.keyboard[val_neg_key] = app.keyboard[val_neg_alt] = 0;
          app.active_widget = focused;
          ChangeWidgetValue( -1 );
        }

        if ( app.keyboard[val_pos_key] || app.keyboard[val_pos_alt] )
        {
          app.keyboard[val_pos_key] = app.keyboard[val_pos_alt] = 0;
          app.active_widget = focused;
          ChangeWidgetValue( 1 );
        }
        
      }
      
      if ( focused->type == WT_INPUT || focused->type == WT_CONTROL )
      {
        if ( current != NULL )
        {
          if ( app.mouse.button == 1 && app.mouse.clicks == 2 &&
            WithinRange( app.mouse.x, app.mouse.y, current->rect ) )
          {
            if ( app.active_widget->type == WT_INPUT )
            {
              app.mouse.button = 0;
              app.mouse.clicks = 0;
              cursor_blink = 0;
              handle_input_widget = 1;
              memset( app.input_text, 0, sizeof( app.input_text ) );
              return;
            }
          }
        }
      }

      if ( app.keyboard[SDL_SCANCODE_SPACE] || app.keyboard[SDL_SCANCODE_RETURN] )
      {
        if ( focused->type == WT_OUTPUT )
        {
          /* Output widgets are read-only; ignore activation */
          app.keyboard[A_SPACEBAR] = app.keyboard[A_RETURN] = 0;
        }

        else if ( focused->type == WT_INPUT )
        {
          app.keyboard[A_SPACEBAR] = app.keyboard[A_RETURN] = 0;
          app.active_widget = focused;
          cursor_blink = 0;
          handle_input_widget = 1;
          memset( app.input_text, 0, sizeof( app.input_text ) );
        }

        else if ( focused->type == WT_CONTROL )
        {
          app.keyboard[A_SPACEBAR] = app.keyboard[A_RETURN] = 0;
          app.active_widget = focused;
          app.last_key_pressed = -1;
          handle_control_widget = 1;
        }

        else
        {
          focused->state = WI_PRESSED;

          if ( focused->click_sound != NULL )
          {
            aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_UI_CLICK, .volume = -1,
                                     .loops = 0, .fade_ms = 0, .interrupt = 1 };
            a_AudioPlaySound( focused->click_sound, &opts );
          }

          pending_press_widget = focused;
          pending_press_source = 1;
          pending_press_on_release = focused->on_release;
          if ( !focused->on_release )
          {
            app.keyboard[A_SPACEBAR] = app.keyboard[A_RETURN] = 0;
            a_TimerStart( press_timer );
          }
        }
      }
    }
  }

  else if ( handle_input_widget )
  {
    DoInputWidget();
    
    aInputWidget_t* curr_input = (aInputWidget_t*)app.active_widget->data;
    aRectf_t glpyh_rect = a_GetGlyphSize();
    
    aRectf_t current_rect = (aRectf_t){
      .x = ( curr_input->rect.x ),
      .y = ( curr_input->rect.y ),
      .w = ( glpyh_rect.w * curr_input->visible_length ),
      .h = ( glpyh_rect.h )
    };
    
    if ( app.keyboard[A_ESCAPE] == 1 )
    {
      app.keyboard[A_ESCAPE] = 0;

      handle_input_widget = 0;
    }

    if ( app.keyboard[A_LCTRL] && app.keyboard[A_V] )
    {
      app.keyboard[A_LCTRL] = app.keyboard[A_V] = 0;
      curr_input->text = SDL_GetClipboardText();
    }
    
    if ( app.keyboard[A_LCTRL] && app.keyboard[A_C] )
    {
      app.keyboard[A_LCTRL] = app.keyboard[A_V] = 0;
      SDL_SetClipboardText( curr_input->text );
    }
    
    if( app.mouse.button == 1 && 
      !WithinRange( app.mouse.x, app.mouse.y, current_rect ) )
    {
      app.mouse.button = 0;
      app.mouse.clicks = 0;
      handle_input_widget = 0;
      return;
    }

    if( app.mouse.button == 1 && app.mouse.clicks == 2 )
    {
      app.mouse.button = 0;
      app.mouse.clicks = 0;
      memset( curr_input->text, 0, MAX_NAME_LENGTH );
    }
  }

  else if( handle_control_widget )
  {
    if ( app.keyboard[A_ESCAPE] == 1 )
    {
      app.keyboard[A_ESCAPE] = 0;

      handle_control_widget = 0;
    }
    DoControlWidget();
  }
}

void a_DrawWidgets( void )
{
  aWidget_t* w;
  for ( w = widget_head.next; w != NULL; w = w->next )
  {
    switch ( w->type )
    {
      case WT_BUTTON:
        DrawButtonWidget( w );
        break;
      
      case WT_SELECT:
        DrawSelectWidget( w );
        break;
      
      case WT_SLIDER:
        DrawSliderWidget( w );
        break;
      
      case WT_INPUT:
        DrawInputWidget( w );
        break;

      case WT_OUTPUT:
        DrawOutputWidget( w );
        break;

      case WT_CONTROL:
        DrawControlWidget( w );
        break;
      
      case WT_CONTAINER:
        DrawContainerWidget( w );
        break;

      default:
        break;
    }
  }
}

void a_WidgetsInit( const char* filename )
{
  if ( widget_tail != NULL )
  {
    a_WidgetCacheFree();
  }

  memset( &widget_head, 0, sizeof( aWidget_t ) );
  widget_tail = &widget_head;

  LoadWidgets( filename );
  
  slider_delay = 0;
  cursor_blink = 0;
  handle_input_widget = 0;
  handle_control_widget = 0;
  pending_press_widget = NULL;
  focused_container = NULL;
  app.active_widget = NULL;
  mouse_moved = 0;
  last_mouse_x = -1;
  last_mouse_y = -1;
  last_hovered_widget = NULL;

  if ( press_timer == NULL )
  {
    press_timer = a_TimerCreate();
  }
  a_TimerStop( press_timer );
}

aWidget_t* a_GetWidget( const char* name )
{
  aWidget_t* w;

  for ( w = widget_head.next; w != NULL; w = w->next )
  {
    if ( strcmp( w->name, name ) == 0 )
    {
      return w;
    }

    if ( w->type == WT_CONTAINER && w->data != NULL )
    {
      aContainerWidget_t* con = (aContainerWidget_t*)w->data;
      for ( int i = 0; i < con->num_components; i++ )
      {
        if ( strcmp( con->components[i].name, name ) == 0 )
          return &con->components[i];
      }
    }
  }

  SDL_LogMessage( SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN,
                  "No such widget: '%s'", name);

  return NULL;
}

aContainerWidget_t* a_GetContainerFromWidget( const char* name )
{
  aWidget_t* widget = NULL;
  aContainerWidget_t* container = NULL;

  widget = a_GetWidget( name );
  if ( widget == NULL )
  {
    printf("Failed to get widget: %s\n", name);
    return NULL;
  }

  container = ( aContainerWidget_t* )widget->data;
  if ( container == NULL )
  {
    printf( "Failed to find container in %s\n", name );
    return NULL;
  }

  return container;
}

aWidget_t a_WidgetGetHeadWidget( void )
{
  return widget_head;
}

aWidget_t* a_ContainerAddButton( const char* container_name,
                                  const char* button_name,
                                  const char* label,
                                  void (*action)(void) )
{
  aWidget_t* w = a_GetWidget( container_name );
  aContainerWidget_t* con = a_GetContainerFromWidget( container_name );

  if ( w == NULL || con == NULL ) return NULL;

  aWidget_t* new_components = realloc( con->components,
                                        ( con->num_components + 1 ) * sizeof( aWidget_t ) );
  if ( new_components == NULL ) return NULL;

  con->components = new_components;

  aWidget_t* btn = &con->components[con->num_components];
  memset( btn, 0, sizeof( aWidget_t ) );

  btn->type = WT_BUTTON;
  STRCPY( btn->name, button_name );
  STRCPY( btn->label, label );
  btn->hide_label = 0;
  btn->boxed = 1;
  btn->hidden = 0;
  btn->padding = w->padding;
  btn->state = WI_BACKGROUND;
  btn->action = action;
  btn->fg = w->fg;
  btn->bg = w->bg;
  btn->click_sound = w->click_sound;
  btn->hover_sound = w->hover_sound;

  CreateButtonWidget( btn );

  con->num_components++;

  ReflowContainer( w, con );

  return btn;
}

void a_ContainerClearComponents( const char* container_name )
{
  aWidget_t* w = a_GetWidget( container_name );
  aContainerWidget_t* con = a_GetContainerFromWidget( container_name );

  if ( w == NULL || con == NULL || con->num_components == 0 ) return;

  ContainerWidgetFree( con, w );

  con->components = NULL;
  con->num_components = 0;
  con->focus_index = 0;
}

static void LoadWidgets( const char* filename )
{
  aAUF_t* root;
  aAUFNode_t* node;

  g_auf_filename = filename;
  root = a_AUFParser( filename );

  for ( node = root->head; node != NULL; node = node->next )
  {
    CreateWidget( node );
  }

  a_AUFFree( root );
  g_auf_filename = NULL;
}

/**
 * @brief Handles input logic for an active input widget.
 *
 * This function is called when `handle_input_widget` is true. It appends
 * characters from `app.input_text` to the active input widget's text buffer,
 * respecting its `max_length`. It also handles backspace to delete characters
 * and Escape/Return keys to exit input mode and potentially trigger the
 * widget's action.
 */
static void DoInputWidget( void )
{
  aInputWidget_t* input;
  int i, l, n;
  input = ( aInputWidget_t* )app.active_widget->data;

  l = strlen( input->text );
  n = strlen( app.input_text );

  if ( n + l > input->max_length )
  {
    n = input->max_length - l;
  }

  for (i = 0; i < n; i++ )
  {
    input->text[l++] = app.input_text[i];
  }

  memset( app.input_text, 0, sizeof( app.input_text ) );

  if ( l > 0 && app.keyboard[SDL_SCANCODE_BACKSPACE] )
  {
    input->text[--l] = '\0';
    app.keyboard[SDL_SCANCODE_BACKSPACE] = 0;
  }

  if ( app.keyboard[SDL_SCANCODE_RETURN] || app.keyboard[SDL_SCANCODE_ESCAPE] )
  {
    app.keyboard[SDL_SCANCODE_RETURN] = app.keyboard[SDL_SCANCODE_ESCAPE] = 0;
    handle_input_widget = 0;
    if ( app.active_widget->action != NULL )
    {
      app.active_widget->action();
    }
  }
}

/**
 * @brief Handles input logic for an active control widget.
 *
 * This function is called when `handle_control_widget` is true. It captures
 * the last key pressed (excluding Escape), assigns its scancode value to the
 * active control widget, and then potentially triggers the widget's action.
 * After processing, it exits control mode.
 */
static void DoControlWidget( void )
{
  if ( app.last_key_pressed != -1 )
  {
    if ( app.last_key_pressed != SDL_SCANCODE_ESCAPE )
    {
      ( ( aControlWidget_t* )app.active_widget->data )->value =
        app.last_key_pressed;

      if ( app.active_widget->action != NULL )
      {
        app.active_widget->action();
      }
    }
    handle_control_widget = 0;

    app.keyboard[app.last_key_pressed] = 0;
  }
}

/**
 * @brief Changes the value of the active widget based on its type.
 *
 * This function is typically called in response to left/right arrow key presses.
 * It updates the `value` for `WT_SELECT` widgets (cycling through options) and
 * `WT_SLIDER` widgets (adjusting the slider value within bounds). It also
 * manages `slider_delay` for sliders and triggers the widget's action.
 *
 * @param value The amount by which to change the widget's value (e.g., -1 for left, 1 for right).
 */
static void ChangeWidgetValue( const int value )
{
  aSelectWidget_t* select;
  aSliderWidget_t* slider;

  switch (app.active_widget->type)
  {
    case WT_SELECT:
      select = ( aSelectWidget_t* ) app.active_widget->data;
      select->value += value;

      if ( select->value < 0 )
      {
        select->value = select->num_options - 1;
      }

      if ( select->value >= select->num_options )
      {
        select->value = 0;
      }

      if ( app.active_widget->action != NULL )
      {
        app.active_widget->action();
      }

      break;

    case WT_SLIDER:
      slider = ( aSliderWidget_t* )app.active_widget->data;
      
      if ( slider_delay == 0 || slider->wait_on_change )
      {
        if ( slider->wait_on_change )
        {
          app.keyboard[SDL_SCANCODE_LEFT] = 
            app.keyboard[SDL_SCANCODE_RIGHT] = 0;
        }

        slider->value = MIN(
          MAX( slider->value + ( slider->step * value ), 0 ), 100 );

        slider_delay = 1;

        if ( app.active_widget->action != NULL )
        {
          app.active_widget->action();
        }
      }
      break;
  
    default:
      break;
  }

  if ( app.active_widget->click_sound != NULL )
  {
    aAudioOptions_t opts = { .channel = AUDIO_CHANNEL_UI_CLICK, .volume = -1,
                             .loops = 0, .fade_ms = 0, .interrupt = 1 };
    a_AudioPlaySound( app.active_widget->click_sound, &opts );
  }
}

/**
 * @brief Creates a new widget and adds it to the global widget list.
 *
 * This function parses a AUF object (`aAUFNode_t* root`) representing a widget's
 * configuration. It allocates memory for a new `aWidget_t`, populates its
 * common properties (name, label, type, position, colors, etc.), and links
 * it into a global linked list of widgets. It then calls a specialized
 * creation function based on the widget's `type` to handle type-specific data.
 *
 * @param root A aAUFNode_t object containing the configuration for the widget to be created.
 */
static void CreateWidget( aAUFNode_t* root )
{
  aWidget_t* w;
  aAUFNode_t* node;
  int type, i;
  uint8_t fg[4] = {0};
  uint8_t bg[4] = {0};

  type = root->type;

  if ( type != 0 )
  {
    w = malloc( sizeof( aWidget_t ) );
    memset( w, 0, sizeof( aWidget_t ) );

    widget_tail->next = w;
    w->prev = widget_tail;
    widget_tail = w;

    aAUFNode_t* temp_label   = a_AUFGetObjectItem( root, "label" );
    aAUFNode_t* temp_hide_label   = a_AUFGetObjectItem( root, "hide_label" );
    aAUFNode_t* temp_x       = a_AUFGetObjectItem( root, "x" );
    aAUFNode_t* temp_y       = a_AUFGetObjectItem( root, "y" );
    aAUFNode_t* temp_boxed   = a_AUFGetObjectItem( root, "boxed" );
    aAUFNode_t* temp_hidden  = a_AUFGetObjectItem( root, "hidden" );
    aAUFNode_t* temp_padding        = a_AUFGetObjectItem( root, "padding" );
    aAUFNode_t* temp_padding_x      = a_AUFGetObjectItem( root, "padding_x" );
    aAUFNode_t* temp_padding_y      = a_AUFGetObjectItem( root, "padding_y" );
    aAUFNode_t* temp_padding_left   = a_AUFGetObjectItem( root, "padding_left" );
    aAUFNode_t* temp_padding_right  = a_AUFGetObjectItem( root, "padding_right" );
    aAUFNode_t* temp_padding_top    = a_AUFGetObjectItem( root, "padding_top" );
    aAUFNode_t* temp_padding_bottom = a_AUFGetObjectItem( root, "padding_bottom" );
    /* also accept hyphenated forms (padding-left, etc.) */
    if ( !temp_padding_x )      temp_padding_x      = a_AUFGetObjectItem( root, "padding-x" );
    if ( !temp_padding_y )      temp_padding_y      = a_AUFGetObjectItem( root, "padding-y" );
    if ( !temp_padding_left )   temp_padding_left   = a_AUFGetObjectItem( root, "padding-left" );
    if ( !temp_padding_right )  temp_padding_right  = a_AUFGetObjectItem( root, "padding-right" );
    if ( !temp_padding_top )    temp_padding_top    = a_AUFGetObjectItem( root, "padding-top" );
    if ( !temp_padding_bottom ) temp_padding_bottom = a_AUFGetObjectItem( root, "padding-bottom" );
    aAUFNode_t* temp_click_sound = a_AUFGetObjectItem( root, "click_sound" );
    aAUFNode_t* temp_hover_sound = a_AUFGetObjectItem( root, "hover_sound" );
    aAUFNode_t* temp_texture = a_AUFGetObjectItem( root, "texture" );
    aAUFNode_t* temp_fg      = a_AUFGetObjectItem( root, "fg" );
    aAUFNode_t* temp_bg      = a_AUFGetObjectItem( root, "bg" );
    aAUFNode_t* temp_ag      = a_AUFGetObjectItem( root, "ag" );
    aAUFNode_t* temp_w   = a_AUFGetObjectItem( root, "w" );
    aAUFNode_t* temp_h   = a_AUFGetObjectItem( root, "h" );
    aAUFNode_t* temp_background = a_AUFGetObjectItem( root, "background" );
    aAUFNode_t* temp_pressed    = a_AUFGetObjectItem( root, "pressed" );
    aAUFNode_t* temp_hovering   = a_AUFGetObjectItem( root, "hovering" );
    aAUFNode_t* temp_disabled   = a_AUFGetObjectItem( root, "disabled" );
    aAUFNode_t* temp_text_x   = a_AUFGetObjectItem( root, "text_x" );
    aAUFNode_t* temp_text_y   = a_AUFGetObjectItem( root, "text_y" );
    aAUFNode_t* temp_button_drop_offset = a_AUFGetObjectItem( root, "button_drop_offset" );
    aAUFNode_t* temp_on_release = a_AUFGetObjectItem( root, "on_release" );
    
    if ( root->value_string != NULL )
    {
      STRCPY( w->name, root->value_string );

      /* reserved keywords */
      if ( strcasecmp( w->name, "SCREEN" ) == 0 || strcasecmp( w->name, "THIS" ) == 0 )
      {
        printf( "%s:%d\n  AUF error: '%s' is a reserved keyword and cannot be used as a widget name.\n",
                g_auf_filename, root->line_number, w->name );
        exit( 1 );
      }

      /* check for duplicate widget name */
      for ( aWidget_t* check = widget_head.next; check != NULL; check = check->next )
      {
        if ( check != w && strcmp( check->name, w->name ) == 0 )
        {
          printf( "%s:%d\n  AUF error: duplicate widget name '%s'\n"
                  "  Widget names must be unique across all types.\n",
                  g_auf_filename, root->line_number, w->name );
          exit( 1 );
        }
      }
    }
    
    if ( temp_label != NULL )
    {
      STRCPY( w->label, temp_label->value_string );
    }
    
    if ( temp_hide_label != NULL )
    {
      w->hide_label = temp_hide_label->value_int;
    }

    w->type = root->type;

    if ( temp_x != NULL )
    {
      if ( temp_x->value_string != NULL )
      {
        w->calc_x = malloc( strlen( temp_x->value_string ) + 1 );
        if ( w->calc_x == NULL )
        {
          printf( "Failed to allocate memory for calc_x\n" );
          exit( 1 );
        }
        strcpy( w->calc_x, temp_x->value_string );
      }
      else
      {
        w->rect.x = temp_x->value_int;
      }
    }

    if ( temp_y != NULL )
    {
      if ( temp_y->value_string != NULL )
      {
        w->calc_y = malloc( strlen( temp_y->value_string ) + 1 );
        if ( w->calc_y == NULL )
        {
          printf( "Failed to allocate memory for calc_y\n" );
          exit( 1 );
        }
        strcpy( w->calc_y, temp_y->value_string );
      }
      else
      {
        w->rect.y = temp_y->value_int;
      }
    }
    
    if ( temp_boxed != NULL )
    {
      w->boxed = temp_boxed->value_int;
    }
    
    if ( temp_hidden != NULL )
    {
      w->hidden = temp_hidden->value_int;
    }
    
    if ( temp_padding != NULL )
      w->padding = resolve_int_ref( temp_padding );
    if ( temp_padding_x != NULL )
      w->padding_x = resolve_int_ref( temp_padding_x );
    if ( temp_padding_y != NULL )
      w->padding_y = resolve_int_ref( temp_padding_y );
    if ( temp_padding_left != NULL )
      w->padding_left = resolve_int_ref( temp_padding_left );
    if ( temp_padding_right != NULL )
      w->padding_right = resolve_int_ref( temp_padding_right );
    if ( temp_padding_top != NULL )
      w->padding_top = resolve_int_ref( temp_padding_top );
    if ( temp_padding_bottom != NULL )
      w->padding_bottom = resolve_int_ref( temp_padding_bottom );

    w->click_sound = load_widget_sound( temp_click_sound );
    w->hover_sound = load_widget_sound( temp_hover_sound );

    if ( temp_texture != NULL )
    {
      w->texture = temp_texture->value_int;
    }
    
    if ( temp_w != NULL )
    {
      if ( temp_w->value_string != NULL )
      {
        w->calc_w = malloc( strlen( temp_w->value_string ) + 1 );
        if ( w->calc_w == NULL )
        {
          printf( "Failed to allocate memory for calc_w\n" );
          exit( 1 );
        }
        strcpy( w->calc_w, temp_w->value_string );
      }
      else
      {
        w->rect.w = temp_w->value_int;
      }
    }

    if ( temp_h != NULL )
    {
      if ( temp_h->value_string != NULL )
      {
        w->calc_h = malloc( strlen( temp_h->value_string ) + 1 );
        if ( w->calc_h == NULL )
        {
          printf( "Failed to allocate memory for calc_h\n" );
          exit( 1 );
        }
        strcpy( w->calc_h, temp_h->value_string );
      }
      else
      {
        w->rect.h = temp_h->value_int;
      }
    }
    
    w->action = NULL;
    
    if ( temp_fg != NULL )
    {
      if ( !resolve_color_ref( temp_fg, &w->fg ) )
      {
        i = 0;
        for ( node = temp_fg->child; node != NULL; node = node->next )
        {
          fg[i++] = node->value_int;
        }
        w->fg.r = fg[0];
        w->fg.g = fg[1];
        w->fg.b = fg[2];
        w->fg.a = fg[3];
      }
    }

    if ( temp_bg != NULL )
    {
      if ( !resolve_color_ref( temp_bg, &w->bg ) )
      {
        i = 0;
        for ( node = temp_bg->child; node != NULL; node = node->next )
        {
          bg[i++] = node->value_int;
        }
        w->bg.r = bg[0];
        w->bg.g = bg[1];
        w->bg.b = bg[2];
        w->bg.a = bg[3];
      }
    }

    if ( temp_ag != NULL )
    {
      if ( !resolve_color_ref( temp_ag, &w->ag ) )
      {
        int ag[4] = {0};
        i = 0;
        for ( node = temp_ag->child; node != NULL; node = node->next )
        {
          ag[i++] = node->value_int;
        }
        w->ag.r = ag[0];
        w->ag.g = ag[1];
        w->ag.b = ag[2];
        w->ag.a = ag[3];
      }
      w->has_ag = 1;
    }

    if ( w->texture )
    {
      if ( temp_background != NULL )
      {
        w->images[WI_BACKGROUND] = a_ImageLoad( temp_background->value_string );
      }
      
      if ( temp_pressed != NULL )
      {
        w->images[WI_PRESSED] = a_ImageLoad( temp_pressed->value_string );
      }
      
      if ( temp_hovering != NULL )
      {
        w->images[WI_HOVERING] = a_ImageLoad( temp_hovering->value_string );
      }
      
      if ( temp_disabled != NULL )
      {
        w->images[WI_DISABLED] = a_ImageLoad( temp_disabled->value_string );
      }
    }

    if ( temp_text_x != NULL )
    {
      w->text_offset.x = temp_text_x->value_int;
    }
    
    if ( temp_text_y != NULL )
    {
      w->text_offset.y = temp_text_y->value_int;
    }

    if ( temp_button_drop_offset != NULL )
    {
      w->text_offset.z = temp_button_drop_offset->value_int;
    }

    if ( temp_on_release != NULL )
    {
      w->on_release = temp_on_release->value_int;
    }

    w->state = 0;
    w->data = NULL;

    switch ( w->type )
    {
      case WT_BUTTON:
        CreateButtonWidget( w );
        break;

      case WT_SELECT:
        CreateSelectWidget( w, root );
        break;
      
      case WT_SLIDER:
        CreateSliderWidget( w, root );
        break;
      
      case WT_INPUT:
        CreateInputWidget( w, root );
        break;

      case WT_OUTPUT:
        CreateOutputWidget( w, root );
        break;

      case WT_CONTROL:
        CreateControlWidget( w );
        break;
      
      case WT_CONTAINER:
        CreateContainerWidget( w, root );
        break;

      default:
        break;
    }

    /* Resolve deferred calc() expressions (THIS / widget references) */
    if ( w->type != WT_CONTAINER &&
         ( w->calc_x != NULL || w->calc_y != NULL ||
           w->calc_w != NULL || w->calc_h != NULL ) )
    {
      /* resolve w/h first so THIS.w / THIS.h are available for x/y */
      if ( w->calc_w != NULL )
      {
        w->rect.w = (int)a_CalcResolveWithThis( w->calc_w, w->rect );
        free( w->calc_w );
        w->calc_w = NULL;
      }
      if ( w->calc_h != NULL )
      {
        w->rect.h = (int)a_CalcResolveWithThis( w->calc_h, w->rect );
        free( w->calc_h );
        w->calc_h = NULL;
      }

      int uses_this = ( w->calc_x != NULL && strstr( w->calc_x, "THIS" ) != NULL )
                   || ( w->calc_y != NULL && strstr( w->calc_y, "THIS" ) != NULL );

      if ( uses_this && w->rect.w == 0 && w->rect.h == 0 )
      {
        printf( "%s:%d\n  AUF calc error: THIS.w/THIS.h used but widget '%s' has no dimensions.\n"
                "  Either define (w,h) before (x,y), or use flex:1 for row / flex:2 for column.\n",
                g_auf_filename, root->line_number, w->name );
        exit( 1 );
      }

      if ( w->calc_x != NULL )
      {
        w->rect.x = (int)a_CalcResolveWithThis( w->calc_x, w->rect );
        free( w->calc_x );
        w->calc_x = NULL;
      }
      if ( w->calc_y != NULL )
      {
        w->rect.y = (int)a_CalcResolveWithThis( w->calc_y, w->rect );
        free( w->calc_y );
        w->calc_y = NULL;
      }
    }
  }
}

int a_WidgetCacheFree( void )
{
  if ( widget_head.next == NULL )
  {
    printf( "No Widgets loaded in cache\n" );
    return 1;
  }

  else
  {
    for ( int i = 0; i < MAX_WIDGET_IMAGE; i++ )
    {
      widget_head.images[i] = NULL;
    }

    aWidget_t* current = widget_head.next;
    aWidget_t* next = NULL;

    aSelectWidget_t* temp_select = NULL;
    aInputWidget_t* temp_input = NULL;
    aContainerWidget_t* temp_container = NULL;
    
    while ( current != NULL )
    {
      next = current->next;
      if ( current->action != NULL )
      {
        current->action = NULL;
      }

      switch ( current->type )
      {
        case WT_SELECT:
          temp_select = (aSelectWidget_t*)current->data;
          
          for ( int i = 0; i < temp_select->num_options; i++ )
          {
            free( temp_select->options[i] );
          }

          free( temp_select->options );
          break;
        
        case WT_INPUT:
          temp_input = (aInputWidget_t*)current->data;
          free( temp_input->text );
          break;

        case WT_OUTPUT:
        {
          aOutputWidget_t* temp_output = (aOutputWidget_t*)current->data;
          free( temp_output->text );
          break;
        }

        case WT_CONTAINER:
          temp_container = (aContainerWidget_t*)current->data;
          ContainerWidgetFree( temp_container, current );
          break;
        
        default:
          break;
      }
      
      if ( current->calc_x != NULL )
      {
        free( current->calc_x );
        current->calc_x = NULL;
      }

      if ( current->calc_y != NULL )
      {
        free( current->calc_y );
        current->calc_y = NULL;
      }

      if ( current->click_sound != NULL )
      {
        a_AudioFreeSound( current->click_sound );
        free( current->click_sound );
        current->click_sound = NULL;
      }

      if ( current->hover_sound != NULL )
      {
        a_AudioFreeSound( current->hover_sound );
        free( current->hover_sound );
        current->hover_sound = NULL;
      }

      if ( current->data != NULL )
      {
        free( current->data );
        current->data = NULL;
      }

      free( current );
      current = next;
    }
    
    memset( &widget_head, 0, sizeof(aWidget_t) );
    widget_tail = NULL;
  }

  focused_container = NULL;
  pending_press_widget = NULL;

  return 0;
}

void a_OutputWidgetSetText( aWidget_t* w, const char* text )
{
  if ( w == NULL || w->data == NULL || text == NULL )
  {
    return;
  }

  aOutputWidget_t* output = ( aOutputWidget_t* )w->data;
  memset( output->text, 0, output->max_length + 1 );
  strncpy( output->text, text, output->max_length );
}

const char* a_OutputWidgetGetText( aWidget_t* w )
{
  if ( w == NULL || w->data == NULL )
  {
    return NULL;
  }

  aOutputWidget_t* output = ( aOutputWidget_t* )w->data;
  return output->text;
}

