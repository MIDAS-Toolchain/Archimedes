/* 
 * src/aViewport.c
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
static void CreateButtonWidget( aWidget_t* w );
static void CreateSelectWidget( aWidget_t* w, aAUFNode_t* root );
static void CreateSliderWidget( aWidget_t* w, aAUFNode_t* root );
static void CreateInputWidget( aWidget_t* w, aAUFNode_t* root );
static void CreateOutputWidget( aWidget_t* w, aAUFNode_t* root );
static void CreateControlWidget( aWidget_t* w );
static void CreateContainerWidget( aWidget_t* w, aAUFNode_t* root );

static void DrawButtonWidget( aWidget_t* w );
static void DrawSelectWidget( aWidget_t* w );
static void DrawSliderWidget( aWidget_t* w );
static void DrawInputWidget( aWidget_t* w );
static void DrawOutputWidget( aWidget_t* w );
static void DrawControlWidget( aWidget_t* w );
static void DrawContainerWidget( aWidget_t* w );

static void DoInputWidget( void );
static void DoControlWidget( void );
static aWidget_t* GetCurrentWidget( void );
static int WithinRangePadded( int x, int y, aWidget_t* w );
static int WithinRange( int x, int y, aRectf_t rect );
static void ClearWidgetsState( void );
static void ContainerWidgetFree( aContainerWidget_t* con, aWidget_t* parent );
static void ReflowContainer( aWidget_t* container_widget, aContainerWidget_t* con );

static void WidgetColor( aWidget_t* w, aColor_t* c );

static aWidget_t widget_head;
static aWidget_t* widget_tail = NULL;

/* current AUF file being loaded — for error messages */
static const char* g_auf_filename = NULL;

static double slider_delay;
static double cursor_blink;
static int handle_input_widget;
static int handle_control_widget;
static aWidget_t* pending_press_widget;
static int pending_press_source; /* 0 = mouse, 1 = keyboard */
static aWidget_t* focused_container;
static aTimer_t* press_timer;
static int pending_press_on_release; /* 0 = on-press (timer), 1 = on-release (wait for release) */
static int last_mouse_x;
static int last_mouse_y;
static int mouse_moved;
static aWidget_t* last_hovered_widget = NULL;

static aSoundEffect_t* load_widget_sound( aAUFNode_t* node )
{
  if ( node == NULL || node->value_string == NULL || node->value_string[0] == '\0' )
    return NULL;

  aSoundEffect_t* snd = malloc( sizeof( aSoundEffect_t ) );
  if ( snd == NULL ) return NULL;

  memset( snd, 0, sizeof( aSoundEffect_t ) );
  if ( a_AudioLoadSound( node->value_string, snd ) < 0 )
  {
    free( snd );
    return NULL;
  }
  return snd;
}

static int resolve_color_ref( aAUFNode_t* node, aColor_t* out )
{
  if ( node == NULL || node->value_string == NULL ) return 0;

  char* dot = strrchr( node->value_string, '.' );
  if ( dot == NULL ) return 0;

  char widget_name[MAX_FILENAME_LENGTH];
  size_t len = dot - node->value_string;
  if ( len == 0 || len >= MAX_FILENAME_LENGTH ) return 0;
  memcpy( widget_name, node->value_string, len );
  widget_name[len] = '\0';

  const char* field = dot + 1;
  aWidget_t* ref = a_GetWidget( widget_name );
  if ( ref == NULL ) return 0;

  if ( strcmp( field, "fg" ) == 0 )      *out = ref->fg;
  else if ( strcmp( field, "bg" ) == 0 ) *out = ref->bg;
  else if ( strcmp( field, "ag" ) == 0 ) *out = ref->ag;
  else return 0;

  return 1;
}

static int resolve_int_ref( aAUFNode_t* node )
{
  if ( node == NULL ) return 0;

  if ( node->value_string != NULL )
  {
    /* calc() expression — resolve with widget references */
    if ( strncmp( node->value_string, "calc(", 5 ) == 0 )
    {
      aRectf_t dummy = {0};
      return (int)a_CalcResolveWithThis( node->value_string, dummy );
    }

    char* dot = strrchr( node->value_string, '.' );
    if ( dot != NULL )
    {
      char widget_name[MAX_FILENAME_LENGTH];
      size_t len = dot - node->value_string;
      if ( len > 0 && len < MAX_FILENAME_LENGTH )
      {
        memcpy( widget_name, node->value_string, len );
        widget_name[len] = '\0';

        const char* field = dot + 1;
        aWidget_t* ref = a_GetWidget( widget_name );
        if ( ref != NULL )
        {
          if ( strcmp( field, "padding" ) == 0 )        return ref->padding;
          if ( strcmp( field, "padding_x" ) == 0 ||
               strcmp( field, "padding-x" ) == 0 )      return ref->padding_x;
          if ( strcmp( field, "padding_y" ) == 0 ||
               strcmp( field, "padding-y" ) == 0 )      return ref->padding_y;
          if ( strcmp( field, "padding_left" ) == 0 ||
               strcmp( field, "padding-left" ) == 0 )   return ref->padding_left;
          if ( strcmp( field, "padding_right" ) == 0 ||
               strcmp( field, "padding-right" ) == 0 )  return ref->padding_right;
          if ( strcmp( field, "padding_top" ) == 0 ||
               strcmp( field, "padding-top" ) == 0 )    return ref->padding_top;
          if ( strcmp( field, "padding_bottom" ) == 0 ||
               strcmp( field, "padding-bottom" ) == 0 ) return ref->padding_bottom;
          if ( strcmp( field, "hidden" ) == 0 )         return ref->hidden;
          if ( strcmp( field, "boxed" ) == 0 )          return ref->boxed;
        }
      }
    }
  }

  return node->value_int;
}

static void resolve_padding( aWidget_t* w, int* left, int* right, int* top, int* bottom )
{
  if ( w == NULL ) return;

  int base = w->padding;
  int px   = w->padding_x;
  int py   = w->padding_y;

  /* most specific wins: direction > axis > base */
  *left   = w->padding_left   ? w->padding_left   : ( px ? px : base );
  *right  = w->padding_right  ? w->padding_right  : ( px ? px : base );
  *top    = w->padding_top    ? w->padding_top    : ( py ? py : base );
  *bottom = w->padding_bottom ? w->padding_bottom : ( py ? py : base );
}

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

/**
 * @brief Creates type-specific data for a Button widget.
 *
 * This function calculates and sets the width (`w`) and height (`h`) of the
 * given button widget based on the dimensions of its label text. It primarily
 * uses `a_CalcTextDimensions` for this purpose.
 *
 * @param w A pointer to the `aWidget_t` structure for the button.
 * @param root A aAUFNode_t object containing the configuration for the button.
 */
static void CreateButtonWidget( aWidget_t* w )
{
  a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  w->state = WI_BACKGROUND;
}

/**
 * @brief Creates type-specific data for a Slider widget.
 *
 * This function allocates and initializes an `aSliderWidget_t` structure,
 * linking it to the `data` member of the base widget. It retrieves the
 * slider's `step` and `wait_on_change` properties from the aAUFNode_t root and
 * calculates the slider's position and dimensions relative to its label.
 *
 * @param w A pointer to the `aWidget_t` structure for the slider widget.
 */
static void CreateSelectWidget( aWidget_t* w, aAUFNode_t* root )
{
  aAUFNode_t* options, *node;
  int i, len, temp_w, temp_h;
  float width, height;
  char* temp_string;
  aSelectWidget_t* s;

  s = malloc( sizeof( aSelectWidget_t ) );
  memset( s, 0, sizeof( aSelectWidget_t ) );
  w->data = s;

  options = a_AUFGetObjectItem( root, "options" );

  s->num_options = options->value_int;
  s->value = 0;
  
  temp_w = temp_h = width = height = 0;

  if ( s->num_options > 0 )
  {
    i = 0;

    s->options = malloc( sizeof(char*) * s->num_options );

    for( node = options->child; node != NULL; node = node->next )
    {
      len = strlen( node->value_string ) + 1;

      s->options[i] = malloc( len );
      temp_string = malloc( len + 4 );

      snprintf(temp_string, ( len + 4 ), "< %s >", node->value_string );

      STRNCPY( s->options[i], node->value_string, len );
      a_CalcTextDimensions( temp_string, app.font_type, &width, &height );
      
      if ( width > temp_w )
      {
        temp_w = width; //Get the largest width
      }
      
      if ( height > temp_h )
      {
        temp_h = height;
      }
      
      free( temp_string );
      i++;
    }
  }
  
  if ( !w->hide_label )
  {
    a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  }

  s->rect.x = w->rect.x + 100;
  s->rect.y = w->rect.y;
  s->rect.w = temp_w + 100;
  s->rect.h = temp_h;
}


/**
 * @brief Creates type-specific data for a Slider widget.
 *
 * This function allocates and initializes an `aSliderWidget_t` structure,
 * linking it to the `data` member of the base widget. It retrieves the
 * slider's `step` and `wait_on_change` properties from the aAUFNode_t root and
 * calculates the slider's position and dimensions relative to its label.
 *
 * @param w A pointer to the `aWidget_t` structure for the slider widget.
 * @param root A aAUFNode_t object containing the configuration for the slider widget.
 */
static void CreateSliderWidget( aWidget_t* w, aAUFNode_t* root )
{
  aSliderWidget_t* s;
  s = malloc( sizeof( aSliderWidget_t ) );
  memset( s, 0, sizeof( aSliderWidget_t ) );
  w->data = s;

  aAUFNode_t* node_step = a_AUFGetObjectItem( root, "step" );
  aAUFNode_t* node_wait = a_AUFGetObjectItem( root, "wait_on_change" );

  s->step = ( node_step != NULL ) ? node_step->value_int : 1;
  s->wait_on_change = ( node_wait != NULL ) ? node_wait->value_int : 0;
  s->value = 0;

  if ( !w->hide_label )
  {
    a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  }

  s->rect.x = w->rect.x + w->rect.w + 50;
	s->rect.y = w->rect.y;
	s->rect.w = w->rect.w;
	s->rect.h = w->rect.h;
}

/**
 * @brief Creates type-specific data for an Input widget.
 *
 * This function allocates and initializes an `aInputWidget_t` structure,
 * linking it to the `data` member of the base widget. It retrieves the
 * `max_length` for the input text from the aAUFNode_t root and allocates a buffer
 * for the text. It also initializes a default text and calculates the
 * input field's dimensions.
 *
 * @param w A pointer to the `aWidget_t` structure for the input widget.
 * @param root A aAUFNode_t object containing the configuration for the input widget.
 */
static void CreateInputWidget( aWidget_t* w, aAUFNode_t* root )
{
  aInputWidget_t* input;

  input = malloc( sizeof( aInputWidget_t ) );
  memset( input, 0, sizeof( aInputWidget_t ) );

  w->data = input;

  input->max_length     = a_AUFGetObjectItem( root, "max_length" )->value_int;
  input->visible_length = a_AUFGetObjectItem( root, "visible_length" )->value_int;
  input->text_offset    = a_AUFGetObjectItem( root, "text_offset" )->value_int;
  input->text           = malloc(  input->max_length + 1 );

  STRNCPY( input->text, "...", MAX_INPUT_LENGTH );

  if ( !w->hide_label )
  {
    a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  }

  input->rect.x = w->rect.x + w->rect.w + input->text_offset;
  input->rect.y = w->rect.y;
  a_CalcTextDimensions( input->text, app.font_type,
                        &input->rect.w, &input->rect.h );
}

static void CreateOutputWidget( aWidget_t* w, aAUFNode_t* root )
{
  aOutputWidget_t* output;

  output = malloc( sizeof( aOutputWidget_t ) );
  memset( output, 0, sizeof( aOutputWidget_t ) );

  w->data = output;

  aAUFNode_t* node_max_length = a_AUFGetObjectItem( root, "max_length" );
  aAUFNode_t* node_visible    = a_AUFGetObjectItem( root, "visible_length" );
  aAUFNode_t* node_offset     = a_AUFGetObjectItem( root, "text_offset" );

  output->max_length     = ( node_max_length != NULL ) ? node_max_length->value_int : MAX_LINE_LENGTH;
  output->visible_length = ( node_visible != NULL ) ? node_visible->value_int : 16;
  output->text_offset    = ( node_offset != NULL ) ? node_offset->value_int : 0;
  output->text           = malloc( output->max_length + 1 );
  memset( output->text, 0, output->max_length + 1 );

  if ( !w->hide_label )
  {
    a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  }

  output->rect.x = w->rect.x + w->rect.w + output->text_offset;
  output->rect.y = w->rect.y;
  a_CalcTextDimensions( "A", app.font_type,
                        &output->rect.w, &output->rect.h );
}

/**
 * @brief Creates type-specific data for a Control (key binding) widget.
 *
 * This function allocates and initializes an `aControlWidget_t` structure,
 * linking it to the `data` member of the base widget. It primarily calculates
 * the dimensions of the base widget based on its label text.
 *
 * @param w A pointer to the `aWidget_t` structure for the control widget.
 * @param root A aAUFNode_t object containing the configuration for the control widget.
 */
static void CreateControlWidget( aWidget_t* w )
{
  aControlWidget_t* control;

  control = malloc( sizeof( aControlWidget_t ) );
  memset( control, 0, sizeof( aControlWidget_t ) );

  w->data = control;
  if ( !w->hide_label )
  {
    a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  }
}

/**
 * @brief Creates type-specific data for a Container widget.
 *
 * This function allocates and initializes an `aContainerWidget_t` structure,
 * linking it to the `data` member of the base widget. It parses the "components"
 * array from the aAUFNode_t root, recursively creating and positioning child widgets
 * within the container. It supports different flexing modes (horizontal/vertical)
 * for component arrangement and calculates the overall dimensions of the container
 * based on its components.
 *
 * @param w A pointer to the `aWidget_t` structure for the container widget.
 * @param root A aAUFNode_t object containing the configuration for the container widget.
 */
static void CreateContainerWidget( aWidget_t* w, aAUFNode_t* root )
{
  aAUFNode_t *node;
  int i;
  int temp_x, temp_y;
  aContainerWidget_t* container;
  aInputWidget_t* input;
  aSliderWidget_t* slider;
  aSelectWidget_t* select;
  uint8_t fg[4] = {0};
  uint8_t bg[4] = {0};
  
  aAUFNode_t* node_flex      = a_AUFGetObjectItem( root, "flex" );
  aAUFNode_t* node_justify   = a_AUFGetObjectItem( root, "justify" );
  aAUFNode_t* node_align     = a_AUFGetObjectItem( root, "align" );
  aAUFNode_t* node_row       = a_AUFGetObjectItem( root, "row" );
  aAUFNode_t* node_col       = a_AUFGetObjectItem( root, "col" );
  aAUFNode_t* node_grid_x    = a_AUFGetObjectItem( root, "grid_x" );
  aAUFNode_t* node_grid_y    = a_AUFGetObjectItem( root, "grid_y" );
  aAUFNode_t* node_spaceing  = a_AUFGetObjectItem( root, "spacing" );
  aAUFNode_t* node_container = a_AUFGetObjectItem( root, "container" );

  container = ( aContainerWidget_t* )malloc( sizeof( aContainerWidget_t ) );
  if ( container == NULL )
  {
    printf("Failed to allocate memory for container\n");
    exit(1);
  }

  memset( container, 0, sizeof( aContainerWidget_t ) );

  w->data = container;
  container->rect = w->rect;

  if ( node_flex != NULL )
  {
    w->flex = node_flex->value_int;
  }

  if ( node_justify != NULL )
  {
    w->justify = node_justify->value_int;
  }

  if ( node_align != NULL )
  {
    w->align = node_align->value_int;
  }

  if ( node_row != NULL )
  {
    w->grid_size.x = node_row->value_int;
  }
  
  if ( node_col != NULL )
  {
    w->grid_size.y = node_col->value_int;
  }
  
  if ( node_grid_x != NULL )
  {
    w->grid_pos.x = node_grid_x->value_int;
  }
  
  if ( node_grid_y != NULL )
  {
    w->grid_pos.y = node_grid_y->value_int;
  }

  if ( node_spaceing != NULL )
  {
    container->spacing = node_spaceing->value_int;
  }

  w->action = NULL;

  if ( node_container != NULL )
  {
    container->num_components = node_container->value_int;

    container->components = ( aWidget_t* )malloc( sizeof( aWidget_t ) *
                                                 container->num_components );

    if ( container->components == NULL )
    {
      printf("Failed to allocate memory for components\n");
      exit(1);
    }

    memset( container->components, 0, sizeof( aWidget_t ) * container->num_components );

    i = 0;
    temp_x = w->rect.x;
    temp_y = w->rect.y;

    /* resolve calc() for w/h before capturing user dimensions */
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

    int user_w = w->rect.w;
    int user_h = w->rect.h;

    int max_component_x_plus_w = 0;
    int max_component_y_plus_h = 0;

    for ( node = node_container->child; node != NULL; node = node->next )
    {
      aAUFNode_t* node_label    = a_AUFGetObjectItem( node, "label" );
      aAUFNode_t* node_hide_label    = a_AUFGetObjectItem( node, "hide_label" );
      aAUFNode_t* node_x        = a_AUFGetObjectItem( node, "x" );
      aAUFNode_t* node_y        = a_AUFGetObjectItem( node, "y" );
      aAUFNode_t* node_boxed    = a_AUFGetObjectItem( node, "boxed" );
      aAUFNode_t* node_hidden   = a_AUFGetObjectItem( node, "hidden" );
      aAUFNode_t* node_padding        = a_AUFGetObjectItem( node, "padding" );
      aAUFNode_t* node_padding_x      = a_AUFGetObjectItem( node, "padding_x" );
      aAUFNode_t* node_padding_y      = a_AUFGetObjectItem( node, "padding_y" );
      aAUFNode_t* node_padding_left   = a_AUFGetObjectItem( node, "padding_left" );
      aAUFNode_t* node_padding_right  = a_AUFGetObjectItem( node, "padding_right" );
      aAUFNode_t* node_padding_top    = a_AUFGetObjectItem( node, "padding_top" );
      aAUFNode_t* node_padding_bottom = a_AUFGetObjectItem( node, "padding_bottom" );
      /* also accept hyphenated forms */
      if ( !node_padding_x )      node_padding_x      = a_AUFGetObjectItem( node, "padding-x" );
      if ( !node_padding_y )      node_padding_y      = a_AUFGetObjectItem( node, "padding-y" );
      if ( !node_padding_left )   node_padding_left   = a_AUFGetObjectItem( node, "padding-left" );
      if ( !node_padding_right )  node_padding_right  = a_AUFGetObjectItem( node, "padding-right" );
      if ( !node_padding_top )    node_padding_top    = a_AUFGetObjectItem( node, "padding-top" );
      if ( !node_padding_bottom ) node_padding_bottom = a_AUFGetObjectItem( node, "padding-bottom" );
      aAUFNode_t* node_click_sound = a_AUFGetObjectItem( node, "click_sound" );
      aAUFNode_t* node_hover_sound = a_AUFGetObjectItem( node, "hover_sound" );
      aAUFNode_t* node_texture  = a_AUFGetObjectItem( node, "texture" );
      aAUFNode_t* node_fg       = a_AUFGetObjectItem( node, "fg" );
      aAUFNode_t* node_bg       = a_AUFGetObjectItem( node, "bg" );
      aAUFNode_t* node_ag       = a_AUFGetObjectItem( node, "ag" );
      aAUFNode_t* node_w   = a_AUFGetObjectItem( node, "w" );
      aAUFNode_t* node_h   = a_AUFGetObjectItem( node, "h" );
      aAUFNode_t* node_background = a_AUFGetObjectItem( node, "background" );
      aAUFNode_t* node_pressed    = a_AUFGetObjectItem( node, "pressed" );
      aAUFNode_t* node_hovering   = a_AUFGetObjectItem( node, "hovering" );
      aAUFNode_t* node_disabled   = a_AUFGetObjectItem( node, "disabled" );
      aAUFNode_t* node_text_x   = a_AUFGetObjectItem( node, "text_x" );
      aAUFNode_t* node_text_y   = a_AUFGetObjectItem( node, "text_y" );
      aAUFNode_t* node_button_drop_offset = a_AUFGetObjectItem( node, "button_drop_offset" );
      aAUFNode_t* node_on_release = a_AUFGetObjectItem( node, "on_release" );

      aWidget_t* current = &container->components[i];

      if ( node->value_string != NULL )
      {
        STRCPY( current->name, node->value_string );

        /* reserved keywords */
        if ( strcasecmp( current->name, "SCREEN" ) == 0 || strcasecmp( current->name, "THIS" ) == 0 )
        {
          printf( "%s:%d\n  AUF error: '%s' is a reserved keyword and cannot be used as a widget name.\n",
                  g_auf_filename, node->line_number, current->name );
          exit( 1 );
        }

        /* check for duplicate against top-level widgets */
        for ( aWidget_t* check = widget_head.next; check != NULL; check = check->next )
        {
          if ( strcmp( check->name, current->name ) == 0 )
          {
            printf( "%s:%d\n  AUF error: duplicate widget name '%s'\n"
                    "  Widget names must be unique across all types.\n",
                    g_auf_filename, node->line_number, current->name );
            exit( 1 );
          }
        }

        /* check for duplicate against earlier siblings in this container */
        for ( int k = 0; k < i; k++ )
        {
          if ( strcmp( container->components[k].name, current->name ) == 0 )
          {
            printf( "%s:%d\n  AUF error: duplicate widget name '%s'\n"
                    "  Widget names must be unique across all types.\n",
                    g_auf_filename, node->line_number, current->name );
            exit( 1 );
          }
        }
      }

      if ( node_label != NULL )
      {
        STRCPY( current->label, node_label->value_string );

      }

      if ( node_hide_label != NULL )
      {
        current->hide_label = node_hide_label->value_int;
      }

      current->type = node->type;

      if ( node_boxed != NULL )
      {
        current->boxed = node_boxed->value_int;
      }

      if ( node_hidden != NULL )
      {
        current->hidden = node_hidden->value_int;
      }

      if ( node_padding != NULL )
        current->padding = resolve_int_ref( node_padding );
      if ( node_padding_x != NULL )
        current->padding_x = resolve_int_ref( node_padding_x );
      if ( node_padding_y != NULL )
        current->padding_y = resolve_int_ref( node_padding_y );
      if ( node_padding_left != NULL )
        current->padding_left = resolve_int_ref( node_padding_left );
      if ( node_padding_right != NULL )
        current->padding_right = resolve_int_ref( node_padding_right );
      if ( node_padding_top != NULL )
        current->padding_top = resolve_int_ref( node_padding_top );
      if ( node_padding_bottom != NULL )
        current->padding_bottom = resolve_int_ref( node_padding_bottom );

      /* load child sounds, or inherit from container */
      current->click_sound = load_widget_sound( node_click_sound );
      current->hover_sound = load_widget_sound( node_hover_sound );
      if ( current->click_sound == NULL ) current->click_sound = w->click_sound;
      if ( current->hover_sound == NULL ) current->hover_sound = w->hover_sound;

      current->action = NULL;

      aAUFNode_t* node_1;
      if ( node_fg != NULL )
      {
        if ( !resolve_color_ref( node_fg, &current->fg ) )
        {
          int j = 0;
          for ( node_1 = node_fg->child; node_1 != NULL; node_1 = node_1->next )
          {
            fg[j++] = node_1->value_int;
          }
          current->fg.r = fg[0];
          current->fg.g = fg[1];
          current->fg.b = fg[2];
          current->fg.a = fg[3];
        }
      }

      if ( node_bg != NULL )
      {
        if ( !resolve_color_ref( node_bg, &current->bg ) )
        {
          int j = 0;
          for ( node_1 = node_bg->child; node_1 != NULL; node_1 = node_1->next )
          {
            bg[j++] = node_1->value_int;
          }
          current->bg.r = bg[0];
          current->bg.g = bg[1];
          current->bg.b = bg[2];
          current->bg.a = bg[3];
        }
      }

      if ( node_ag != NULL )
      {
        if ( !resolve_color_ref( node_ag, &current->ag ) )
        {
          int ag[4] = {0};
          int j = 0;
          for ( node_1 = node_ag->child; node_1 != NULL; node_1 = node_1->next )
          {
            ag[j++] = node_1->value_int;
          }
          current->ag.r = ag[0];
          current->ag.g = ag[1];
          current->ag.b = ag[2];
          current->ag.a = ag[3];
        }
        current->has_ag = 1;
      }

      if ( !current->hide_label )
      {
        a_CalcTextDimensions( current->label, app.font_type,
                             &current->rect.w, &current->rect.h );
      }

      if ( w->flex == 1 || w->flex == 2 )
      {
        int cpl, cpr, cpt, cpb;
        resolve_padding( current, &cpl, &cpr, &cpt, &cpb );

        /* offset rect inward so the padding box starts at temp_x/temp_y */
        current->rect.x = temp_x + cpl;
        current->rect.y = temp_y + cpt;
      }

      else if ( w->flex == 3 )
      {
        if ( node_x != NULL && node_y != NULL )
        {
          current->rect.x = node_x->value_int + temp_x;
          current->rect.y = node_y->value_int + temp_y;
        }
      }

      else
      {
        if ( node_x != NULL )
        {
          current->rect.x = node_x->value_int;
        }

        if ( node_y != NULL)
        {
          current->rect.y = node_y->value_int;
        }

        if ( node_w != NULL )
        {
          current->rect.w = node_w->value_int;
        }

        if ( node_h != NULL)
        {
          current->rect.h = node_h->value_int;
        }
      }

      if ( node_texture != NULL )
      {
        current->texture = node_texture->value_int;
      }

      int widget_effective_w = current->rect.w; //size of current widget
      int widget_effective_h = current->rect.h;

      int current_widget_max_x_extent = current->rect.x + current->rect.w;
      int current_widget_max_y_extent = current->rect.y + current->rect.h;

      if ( current->texture )
      {
        if ( node_background != NULL )
        {
          current->images[WI_BACKGROUND] = a_ImageLoad( node_background->value_string );
        }

        if ( node_pressed != NULL )
        {
          current->images[WI_PRESSED] = a_ImageLoad( node_pressed->value_string );
        }

        if ( node_hovering != NULL )
        {
          current->images[WI_HOVERING] = a_ImageLoad( node_hovering->value_string );
        }

        if ( node_disabled != NULL )
        {
          current->images[WI_DISABLED] = a_ImageLoad( node_disabled->value_string );
        }
      }

      if ( node_text_x != NULL )
      {
        current->text_offset.x = node_text_x->value_int;

      }

      if ( node_text_y != NULL )
      {
        current->text_offset.y = node_text_y->value_int;
      }

      if ( node_button_drop_offset != NULL )
      {
        current->text_offset.z = node_button_drop_offset->value_int;
      }

      if ( node_on_release != NULL )
      {
        current->on_release = node_on_release->value_int;
      }

      current->state = 0;

      aRectf_t glyph_rect;
      
      switch ( current->type )
      {
        case WT_BUTTON:
          if ( w->flex == 1 || w->flex == 2 )
          {
            current_widget_max_x_extent = current->rect.x + current->rect.w;
            current_widget_max_y_extent = current->rect.y + current->rect.h;
          }
          
          else if ( w->flex == 3 )
          {
            
          }
          CreateButtonWidget( current );
          break;

        case WT_SELECT:
          CreateSelectWidget( current, node );
          select = (aSelectWidget_t*)current->data;

          current_widget_max_x_extent = MAX( current_widget_max_x_extent,
                                            ( select->rect.x + select->rect.w ) );
          current_widget_max_y_extent = MAX( current_widget_max_y_extent,
                                            ( select->rect.y + select->rect.h ) );
          break;

        case WT_SLIDER:
          CreateSliderWidget( current, node );
          slider = (aSliderWidget_t*)current->data;

          current_widget_max_x_extent = MAX( current_widget_max_x_extent,
                                            ( slider->rect.x + slider->rect.w ) );
          current_widget_max_y_extent = MAX( current_widget_max_y_extent,
                                            ( slider->rect.y + slider->rect.h ) );
          break;

        case WT_INPUT:
          CreateInputWidget( current, node );
          input = (aInputWidget_t*)current->data;
          glyph_rect = a_GetGlyphSize();

          current_widget_max_x_extent = MAX(
            current_widget_max_x_extent,
            ( ( input->rect.x + input->rect.w )
            + ( glyph_rect.w * input->visible_length ) ) );
          current_widget_max_y_extent = MAX(
            current_widget_max_y_extent,
            ( input->rect.y + input->rect.h ) );
          break;

        case WT_OUTPUT:
        {
          CreateOutputWidget( current, node );
          aOutputWidget_t* out = (aOutputWidget_t*)current->data;

          current_widget_max_x_extent = MAX( current_widget_max_x_extent,
                                            ( out->rect.x + out->rect.w ) );
          current_widget_max_y_extent = MAX( current_widget_max_y_extent,
                                            ( out->rect.y + out->rect.h ) );
          break;
        }

        case WT_CONTROL:
          CreateControlWidget( current );
          break;

        case WT_CONTAINER:
          CreateContainerWidget( current, node );
          break;

        default:
          break;
      }

      widget_effective_w = current_widget_max_x_extent - current->rect.x;
      widget_effective_h = current_widget_max_y_extent - current->rect.y;

      /* include padding in flex advancement and extent tracking */
      {
        int cpl, cpr, cpt, cpb;
        resolve_padding( current, &cpl, &cpr, &cpt, &cpb );

        if ( w->flex == 1 )
        {
          temp_x += ( cpl + widget_effective_w + cpr + container->spacing );
        }

        if ( w->flex == 2 )
        {
          temp_y += ( cpt + widget_effective_h + cpb + container->spacing );
        }

        current_widget_max_x_extent += cpr;
        current_widget_max_y_extent += cpb;
      }

      if ( current_widget_max_x_extent > max_component_x_plus_w )
      {
        max_component_x_plus_w = current_widget_max_x_extent;
      }

      if ( current_widget_max_y_extent > max_component_y_plus_h )
      {
        max_component_y_plus_h = current_widget_max_y_extent;
      }

      i++;
    }

    if ( w->flex == 1 || w->flex == 2 )
    {
      int content_w = max_component_x_plus_w - w->rect.x;
      int content_h = max_component_y_plus_h - w->rect.y;

      /* only auto-size dimensions the user didn't specify */
      if ( user_w == 0 ) w->rect.w = content_w;
      if ( user_h == 0 ) w->rect.h = content_h;

      /* justify: shift children along the main axis */
      /* 0 = start (default), 1 = center, 2 = end */
      if ( w->justify > 0 )
      {
        int content_size  = ( w->flex == 1 ) ? content_w : content_h;
        int container_size = ( w->flex == 1 ) ? w->rect.w : w->rect.h;

        int extra = container_size - content_size;
        if ( extra > 0 )
        {
          int offset = 0;
          if ( w->justify == 1 ) offset = extra / 2;
          else if ( w->justify == 2 ) offset = extra;

          for ( int j = 0; j < container->num_components; j++ )
          {
            if ( w->flex == 1 ) container->components[j].rect.x += offset;
            else                container->components[j].rect.y += offset;
          }
        }
      }

      /* align: position each child on the cross-axis */
      /* 0 = start (default), 1 = center, 2 = end */
      if ( w->align > 0 )
      {
        int cross_size = ( w->flex == 1 ) ? w->rect.h : w->rect.w;

        for ( int j = 0; j < container->num_components; j++ )
        {
          aWidget_t* c = &container->components[j];
          int child_size = ( w->flex == 1 ) ? c->rect.h : c->rect.w;
          int extra = cross_size - child_size;
          if ( extra > 0 )
          {
            int offset = 0;
            if ( w->align == 1 ) offset = extra / 2;
            else if ( w->align == 2 ) offset = extra;

            if ( w->flex == 1 ) c->rect.y = w->rect.y + offset;
            else                c->rect.x = w->rect.x + offset;
          }
        }
      }
    }
  }

  /* resolve deferred calc expressions (THIS / widget references) */
  if ( w->calc_x != NULL || w->calc_y != NULL )
  {
    int uses_this = ( w->calc_x != NULL && strstr( w->calc_x, "THIS" ) != NULL )
                 || ( w->calc_y != NULL && strstr( w->calc_y, "THIS" ) != NULL );

    if ( uses_this && w->rect.w == 0 && w->rect.h == 0 )
    {
      const char* missing = ( w->calc_x != NULL ) ? "THIS.w" : "THIS.h";
      printf( "%s:%d\n  AUF calc error: %s used but widget '%s' has no dimensions.\n"
              "  Either define (w,h) before (x,y), or use flex:1 for row / flex:2 for column.\n",
              g_auf_filename, root->line_number, missing, w->name );
      exit( 1 );
    }

    int old_x = w->rect.x;
    int old_y = w->rect.y;

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

    /* reposition children by the delta from old to new container position */
    int dx = w->rect.x - old_x;
    int dy = w->rect.y - old_y;

    container = (aContainerWidget_t*)w->data;
    container->rect = w->rect;

    if ( dx != 0 || dy != 0 )
    {
      for ( int j = 0; j < container->num_components; j++ )
      {
        aWidget_t* child = &container->components[j];
        child->rect.x += dx;
        child->rect.y += dy;
      }
    }
  }
}

static void DrawButtonWidget( aWidget_t* w )
{
  aColor_t c;
  int offset = 0;
  
  WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    if ( w->texture == 1 )
    {
      int pl, pr, pt, pb;
      resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){
        .x = ( w->rect.x - pl ),
        .y = ( w->rect.y - pt ),
        .w = ( w->rect.w + pl + pr + ( 2 * w->text_offset.x ) ),
        .h = ( w->rect.h + pt + pb + ( 2 * w->text_offset.y ) ) };

      a_BlitRect( w->images[w->state], NULL, &rect, 1 );

      if ( w->state == WI_PRESSED )
      {
        offset = w->text_offset.z;
      }
      else
      {
        offset = w->text_offset.y;
      }

    }
    
    else
    {
      if ( w->boxed == 1 )
      {
        int pl, pr, pt, pb;
        resolve_padding( w, &pl, &pr, &pt, &pb );
        aRectf_t rect = (aRectf_t){
          .x = ( w->rect.x - pl ),
          .y = ( w->rect.y - pt ),
          .w = ( w->rect.w + pl + pr ),
          .h = ( w->rect.h + pt + pb ) };
        a_DrawFilledRect( rect, w->bg );

        aColor_t border = black;
        if ( w->state == WI_HOVERING || w->state == WI_PRESSED )
        {
          border = w->fg;
        }
        a_DrawRect( rect, border );
      }
    }

    aTextStyle_t style = { .type = app.font_type,
                           .fg = c, .bg = {0,0,0,0},
                           .align = TEXT_ALIGN_LEFT,
                           .wrap_width = 0,
                           .scale = 1.0f,
                           .padding = 0 };
    a_DrawText( w->label, w->rect.x + w->text_offset.x, w->rect.y + offset, style );
  }
}

static void DrawSelectWidget( aWidget_t* w )
{
  aColor_t c;
  char text[128];
  aSelectWidget_t* s;
  s = ( aSelectWidget_t* ) w->data;

  WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - pl ),
                                  .y = ( w->rect.y - pt ),
                                  .w = ( w->rect.w + pl + pr ),
                                  .h = ( w->rect.h + pt + pb ) };

      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }

    aTextStyle_t style = { .type = app.font_type, .fg = c, .bg = {0,0,0,0}, .align = TEXT_ALIGN_LEFT, .wrap_width = 0, .scale = 1.0f, .padding = 0 };
    a_DrawText( w->label, w->rect.x, w->rect.y, style );
    sprintf( text, "< %s >", s->options[s->value] );

    a_DrawText( text, s->rect.x + 100, s->rect.y, style );
  }
}

static void DrawSliderWidget( aWidget_t* w )
{
  aColor_t c;
  aSliderWidget_t* slider;
  double width;

  slider = ( aSliderWidget_t* )w->data;

  WidgetColor( w, &c );
  
  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - pl ),
                                  .y = ( w->rect.y - pt ),
                                  .w = ( w->rect.w + pl + pr ),
                                  .h = ( w->rect.h + pt + pb ) };

      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }

    width = ( 1.0 * slider->value ) / 100;

    aTextStyle_t style = { .type = app.font_type, .fg = c, .bg = {0,0,0,0}, .align = TEXT_ALIGN_LEFT, .wrap_width = 0, .scale = 1.0f, .padding = 0 };
    a_DrawText( w->label, w->rect.x, w->rect.y, style );

    aRectf_t slider_bg_rect = (aRectf_t){ .x = slider->rect.x,
                                          .y = slider->rect.y,
                                          .w = slider->rect.w,
                                          .h = slider->rect.h };

    a_DrawRect( slider_bg_rect, white );
    aRectf_t slider_rect = (aRectf_t){ .x = ( slider->rect.x + 2 ),
                                       .y = ( slider->rect.y + 2 ),
                                       .w = ( ( slider->rect.w - 4 ) * width ),
                                       .h = ( slider->rect.h - 4 ) };
    a_DrawFilledRect( slider_rect, c );
  }

}

static void DrawInputWidget( aWidget_t* w )
{
  aColor_t c;
  aInputWidget_t* input;
  float text_width, text_height;
  aRectf_t glyph_rect;

  input = ( aInputWidget_t* )w->data;
  
  a_CalcTextDimensions( input->text, app.font_type, &text_width, &text_height );
  glyph_rect = a_GetGlyphSize();

  WidgetColor( w, &c );
  
  if ( w->hidden != 1 )
  {
    
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - pl ),
        .y = ( w->rect.y - pt ),
        .w = ( w->rect.w + ( glyph_rect.w * input->visible_length ) + pl + pr ),
        .h = ( w->rect.h + pt + pb ) };
      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }
    
    aRectf_t text_rect = (aRectf_t){ 
      .x = ( input->rect.x ),
      .y = ( input->rect.y ),
      .w = ( ( glyph_rect.w * input->visible_length ) ),
      .h = ( glyph_rect.h )
    };
     
    a_DrawRect( text_rect, black );

    int scroll_offset = 0;

    if ( text_width > text_rect.w )
    {
      scroll_offset = text_width - text_rect.w;
    }

    aTextStyle_t style = { .type       = app.font_type,
                           .fg         = c,
                           .bg         = {0,0,0,255},
                           .align      = TEXT_ALIGN_LEFT,
                           .wrap_width = 0,
                           .scale      = 1.0f,
                           .padding    = 0 };
    a_DrawText( w->label, w->rect.x, w->rect.y, style );

    a_SetClipRect( text_rect );
    a_DrawText( input->text, input->rect.x - scroll_offset, input->rect.y, style );
    
    uint32_t ticks = SDL_GetTicks();
    uint8_t is_visible = ( ticks % 1000 ) < 500;

    if ( handle_input_widget && is_visible &&
    strncmp( w->name, app.active_widget->name, MAX_FILENAME_LENGTH ) == 0 )
    {
      aRectf_t cursor_rect = ( aRectf_t ){ .x = ( input->rect.x + text_width ),
                                           .y = ( input->rect.y ),
                                           .w = 9,
                                           .h = 16 };
      a_DrawFilledRect( cursor_rect, green );
    }
  }
  a_DisableClipRect();
}

static void DrawOutputWidget( aWidget_t* w )
{
  aColor_t c;
  aOutputWidget_t* output;
  float label_w, label_h, text_w, text_h;

  output = ( aOutputWidget_t* )w->data;

  WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    label_w = 0;
    label_h = 0;
    text_w  = 0;
    text_h  = 0;

    if ( !w->hide_label && w->label[0] != '\0' )
    {
      a_CalcTextDimensions( w->label, app.font_type, &label_w, &label_h );
    }

    if ( output->text[0] != '\0' )
    {
      a_CalcTextDimensions( output->text, app.font_type, &text_w, &text_h );
    }

    float text_x = w->rect.x + label_w + output->text_offset;
    float total_w = label_w + output->text_offset + text_w;
    float total_h = ( label_h > text_h ) ? label_h : text_h;

    if ( total_h == 0 )
    {
      float dummy;
      a_CalcTextDimensions( "A", app.font_type, &dummy, &total_h );
    }

    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){
        .x = w->rect.x - pl,
        .y = w->rect.y - pt,
        .w = total_w + pl + pr,
        .h = total_h + pt + pb };
      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }

    aTextStyle_t style = { .type       = app.font_type,
                           .fg         = c,
                           .bg         = {0,0,0,0},
                           .align      = TEXT_ALIGN_LEFT,
                           .wrap_width = 0,
                           .scale      = 1.0f,
                           .padding    = 0 };

    if ( label_w > 0 )
    {
      a_DrawText( w->label, w->rect.x, w->rect.y, style );
    }

    if ( text_w > 0 )
    {
      a_DrawText( output->text, text_x, w->rect.y, style );
    }
  }
}

static void DrawControlWidget( aWidget_t* w )
{
  aColor_t c;
  aControlWidget_t* control;
  char text[32];

  control = ( aControlWidget_t* )w->data;

  WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - pl ),
                                  .y = ( w->rect.y - pt ),
                                  .w = ( w->rect.w + pl + pr ),
                                  .h = ( w->rect.h + pt + pb ) };

      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }

    aTextStyle_t style = { .type = app.font_type, .fg = c, .bg = {0,0,0,0}, .align = TEXT_ALIGN_LEFT, .wrap_width = 0, .scale = 1.0f, .padding = 0 };
    a_DrawText( w->label, w->rect.x, w->rect.y, style );

    if ( handle_control_widget && app.active_widget == w )
    {
      a_DrawText( "...", control->x, control->y, style );
    }

    else
    {
      sprintf( text, "%s", SDL_GetScancodeName( control->value ) );
      a_DrawText( text, control->x, control->y, style );
    }
  }
}

/**
 * @brief Draws a Container widget and all its visible components on the screen.
 *
 * This function handles the rendering of a container widget. It first draws
 * a background box for the container itself if it's `boxed`. Then, it iterates
 * through all the components (child widgets) within the container and recursively
 * calls their respective drawing functions, ensuring that only visible components
 * are drawn.
 *
 * @param w A pointer to the `aWidget_t` structure representing the container widget to draw.
 */
static void DrawContainerWidget( aWidget_t* w )
{
  aContainerWidget_t* container;

  container = ( aContainerWidget_t* )w->data;

  if ( w->hidden != 1 )
  {
    /* Recalculate container extent from children to handle dynamic content */
    float max_x = w->rect.x + w->rect.w;
    float max_y = w->rect.y + w->rect.h;

    for ( int i = 0; i < container->num_components; i++ )
    {
      aWidget_t* comp = &container->components[i];
      if ( comp->hidden == 1 ) continue;

      if ( comp->type == WT_OUTPUT )
      {
        aOutputWidget_t* out = ( aOutputWidget_t* )comp->data;
        float lw = 0, lh = 0, tw = 0, th = 0;

        if ( !comp->hide_label && comp->label[0] != '\0' )
        {
          a_CalcTextDimensions( comp->label, app.font_type, &lw, &lh );
        }
        if ( out->text[0] != '\0' )
        {
          a_CalcTextDimensions( out->text, app.font_type, &tw, &th );
        }

        int cpl, cpr, cpt, cpb;
        resolve_padding( comp, &cpl, &cpr, &cpt, &cpb );
        float ext_x = comp->rect.x + lw + out->text_offset + tw + cpr;
        float ext_y = comp->rect.y + ( ( lh > th ) ? lh : th ) + cpb;

        if ( ext_x > max_x ) max_x = ext_x;
        if ( ext_y > max_y ) max_y = ext_y;
      }
    }

    float cont_w = max_x - w->rect.x;
    float cont_h = max_y - w->rect.y;

    /* respect user-specified dimensions as a minimum */
    if ( w->rect.w > cont_w ) cont_w = w->rect.w;
    if ( w->rect.h > cont_h ) cont_h = w->rect.h;

    int pl, pr, pt, pb;
    resolve_padding( w, &pl, &pr, &pt, &pb );
    aRectf_t rect = (aRectf_t){
      .x = ( w->rect.x - pl - 5 ),
      .y = ( w->rect.y - pt - 3 ),
      .w = ( cont_w + pl + pr + 15 + ( 2 * w->text_offset.x ) ),
      .h = ( cont_h + pt + pb + 10 + ( 2 * w->text_offset.y ) ) };
    
    if ( w->texture )
    {
      a_BlitRect( w->images[w->state], NULL, &rect, 1 );
    }

    else
    {
      if ( w->boxed == 1 )
      {
        a_DrawFilledRect( rect, w->bg );
        a_DrawRect( rect, black );
      }
    }

    //a_DrawText( w->label, w->x, w->y, c.r, c.g, c.b, app.font_type, TEXT_ALIGN_LEFT, 0 );

    for ( int i = 0; i < container->num_components; i++ )
    {
      aWidget_t current;
      current = container->components[i];
      
      if ( current.hidden != 1 )
      {
        switch ( current.type ) {
          case WT_BUTTON:
            DrawButtonWidget( &current );
            break;

          case WT_SLIDER:
            DrawSliderWidget( &current );
            break;

          case WT_INPUT:
            DrawInputWidget( &current );
            break;

          case WT_OUTPUT:
            DrawOutputWidget( &current );
            break;

          case WT_SELECT:
            DrawSelectWidget( &current );
            break;

          case WT_CONTROL:
            DrawControlWidget( &current );
            break;

          default:
            break;
        } 
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

static void ReflowContainer( aWidget_t* w, aContainerWidget_t* con )
{
  if ( con == NULL || con->num_components == 0 ) return;

  int temp_x = w->rect.x;
  int temp_y = w->rect.y;

  int max_component_x_plus_w = 0;
  int max_component_y_plus_h = 0;

  for ( int i = 0; i < con->num_components; i++ )
  {
    aWidget_t* current = &con->components[i];

    int cpl, cpr, cpt, cpb;
    resolve_padding( current, &cpl, &cpr, &cpt, &cpb );

    if ( w->flex == 1 || w->flex == 2 )
    {
      current->rect.x = temp_x + cpl;
      current->rect.y = temp_y + cpt;
    }

    int widget_effective_w = current->rect.w;
    int widget_effective_h = current->rect.h;

    int current_widget_max_x_extent = current->rect.x + current->rect.w;
    int current_widget_max_y_extent = current->rect.y + current->rect.h;

    if ( w->flex == 1 )
      temp_x += ( cpl + widget_effective_w + cpr + con->spacing );

    if ( w->flex == 2 )
      temp_y += ( cpt + widget_effective_h + cpb + con->spacing );

    current_widget_max_x_extent += cpr;
    current_widget_max_y_extent += cpb;

    if ( current_widget_max_x_extent > max_component_x_plus_w )
      max_component_x_plus_w = current_widget_max_x_extent;

    if ( current_widget_max_y_extent > max_component_y_plus_h )
      max_component_y_plus_h = current_widget_max_y_extent;
  }

  if ( w->flex == 1 || w->flex == 2 )
  {
    int content_w = max_component_x_plus_w - w->rect.x;
    int content_h = max_component_y_plus_h - w->rect.y;

    /* auto-size the container to fit its content */
    if ( content_w > w->rect.w ) w->rect.w = content_w;
    if ( content_h > w->rect.h ) w->rect.h = content_h;
    con->rect.w = w->rect.w;
    con->rect.h = w->rect.h;

    if ( w->justify > 0 )
    {
      int content_size   = ( w->flex == 1 ) ? content_w : content_h;
      int container_size = ( w->flex == 1 ) ? w->rect.w : w->rect.h;

      int extra = container_size - content_size;
      if ( extra > 0 )
      {
        int offset = 0;
        if ( w->justify == 1 ) offset = extra / 2;
        else if ( w->justify == 2 ) offset = extra;

        for ( int j = 0; j < con->num_components; j++ )
        {
          if ( w->flex == 1 ) con->components[j].rect.x += offset;
          else                con->components[j].rect.y += offset;
        }
      }
    }

    if ( w->align > 0 )
    {
      int cross_size = ( w->flex == 1 ) ? w->rect.h : w->rect.w;

      for ( int j = 0; j < con->num_components; j++ )
      {
        aWidget_t* c = &con->components[j];
        int child_size = ( w->flex == 1 ) ? c->rect.h : c->rect.w;
        int extra = cross_size - child_size;
        if ( extra > 0 )
        {
          int offset = 0;
          if ( w->align == 1 ) offset = extra / 2;
          else if ( w->align == 2 ) offset = extra;

          if ( w->flex == 1 ) c->rect.y = w->rect.y + offset;
          else                c->rect.x = w->rect.x + offset;
        }
      }
    }
  }
}

static void ContainerWidgetFree( aContainerWidget_t* con, aWidget_t* parent )
{
  aSelectWidget_t* temp_select = NULL;
  aSliderWidget_t* temp_slider = NULL;
  aInputWidget_t* temp_input = NULL;
  aControlWidget_t* temp_control = NULL;

  for ( int i = 0; i < con->num_components; i++ )
  {
    aWidget_t* current = &con->components[i];

    if ( current->action != NULL )
    {
      current->action = NULL;
    }

    /* release image references (cache owns the images) */
    for ( int j = 0; j < MAX_WIDGET_IMAGE; j++ )
    {
      current->images[j] = NULL;
    }

    switch ( current->type )
    {
      case WT_SELECT:
        temp_select = (aSelectWidget_t*)current->data;

        for ( int j = 0; j < temp_select->num_options; j++ )
        {
          free( temp_select->options[j] );
        }

        free( temp_select->options );
        free( temp_select );
        break;

      case WT_INPUT:
        temp_input = (aInputWidget_t*)current->data;
        free( temp_input->text );
        free( temp_input );
        break;

      case WT_OUTPUT:
      {
        aOutputWidget_t* temp_output = (aOutputWidget_t*)current->data;
        free( temp_output->text );
        free( temp_output );
        break;
      }

      case WT_BUTTON:
        break;

      case WT_SLIDER:
        temp_slider = (aSliderWidget_t*)current->data;
        free( temp_slider );
        break;

      case WT_CONTROL:
        temp_control = (aControlWidget_t*)current->data;
        free( temp_control );
        break;

      default:
        break;
    }

    if ( current->calc_x != NULL ) { free( current->calc_x ); current->calc_x = NULL; }
    if ( current->calc_y != NULL ) { free( current->calc_y ); current->calc_y = NULL; }
    if ( current->calc_w != NULL ) { free( current->calc_w ); current->calc_w = NULL; }
    if ( current->calc_h != NULL ) { free( current->calc_h ); current->calc_h = NULL; }

    /* free child sounds only if they were loaded by the child (not inherited) */
    if ( current->click_sound != NULL &&
         ( parent == NULL || current->click_sound != parent->click_sound ) )
    {
      a_AudioFreeSound( current->click_sound );
      free( current->click_sound );
      current->click_sound = NULL;
    }

    if ( current->hover_sound != NULL &&
         ( parent == NULL || current->hover_sound != parent->hover_sound ) )
    {
      a_AudioFreeSound( current->hover_sound );
      free( current->hover_sound );
      current->hover_sound = NULL;
    }
  }

  free( con->components );
}

static aWidget_t* GetCurrentWidget( void )
{
  aWidget_t* current = &widget_head;

  while ( current != NULL )
  {
    if ( current->hidden == 0 )
    {
      if ( WithinRangePadded( app.mouse.x, app.mouse.y, current ) )
      {
        if ( current->type == WT_CONTAINER )
        {
          aContainerWidget_t* container =
            ( aContainerWidget_t* )current->data;

          for ( int i = 0; i < container->num_components; i++ )
          {
            aWidget_t* component = &container->components[i];

            if ( component->hidden == 0 && component->type != WT_OUTPUT )
            {
              if ( WithinRangePadded( app.mouse.x, app.mouse.y, component ) )
              {
                if ( mouse_moved )
                {
                  container->focus_index = i;
                  focused_container = current;
                }
                return component;
              }
            }
          }

        }

        else if ( current->type != WT_OUTPUT )
        {
          return current;
        }
      }
    }

    current = current->next;
  }

  return NULL;
}


static int WithinRangePadded( int x, int y, aWidget_t* w )
{
  if ( w == NULL ) return 0;
  int pl, pr, pt, pb;
  resolve_padding( w, &pl, &pr, &pt, &pb );

  if ( x >= ( w->rect.x - pl ) && y >= ( w->rect.y - pt ) &&
       x <= ( w->rect.x + w->rect.w + pr ) &&
       y <= ( w->rect.y + w->rect.h + pb ) )
  {
    return 1;
  }

  return 0;
}

static int WithinRange( int x, int y, aRectf_t rect )
{
  if ( x >= rect.x && y >= rect.y &&
       x <= ( rect.x + rect.w ) && y <= ( rect.y + rect.h ) )
  {
    return 1;
  }

  return 0;
}

static void ClearWidgetsState( void )
{
  aWidget_t* current = &widget_head;

  while ( current != NULL )
  {
    if ( current->hidden == 0 )
    {
      if ( current->type == WT_CONTAINER )
      {
        aContainerWidget_t* container =
          ( aContainerWidget_t* )current->data;

        for ( int i = 0; i < container->num_components; i++ )
        {
          aWidget_t* component = &container->components[i];

          if ( component->hidden == 0 )
          {
            component->state = 0;
          }
        }
      }
      else
      {
        current->state = 0;
      }
    }

    current = current->next;
  }
}

static void WidgetColor( aWidget_t* w, aColor_t* c )
{
  c->r = w->fg.r;
  c->g = w->fg.g;
  c->b = w->fg.b;

  if ( app.active_widget != NULL
    && strcmp( w->name, app.active_widget->name ) == 0
    && w->has_ag )
  {
    c->r = w->ag.r;
    c->g = w->ag.g;
    c->b = w->ag.b;
  }
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
