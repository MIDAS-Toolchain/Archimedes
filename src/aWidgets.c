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
static void CreateControlWidget( aWidget_t* w );
static void CreateContainerWidget( aWidget_t* w, aAUFNode_t* root );

static void DrawButtonWidget( aWidget_t* w );
static void DrawSelectWidget( aWidget_t* w );
static void DrawSliderWidget( aWidget_t* w );
static void DrawInputWidget( aWidget_t* w );
static void DrawControlWidget( aWidget_t* w );
static void DrawContainerWidget( aWidget_t* w );

static void DoInputWidget( void );
static void DoControlWidget( void );
static aWidget_t* GetCurrentWidget( void );
static int WithinRange( int x, int y, aRectf_t rect );
static void ClearWidgetsState( void );
static void ContainerWidgetFree( aContainerWidget_t* con );

static void WidgetColor( aWidget_t* w, aColor_t* c );

static aWidget_t widget_head;
static aWidget_t* widget_tail = NULL;

static double slider_delay;
static double cursor_blink;
static int handle_input_widget;
static int handle_control_widget;

void a_DoWidget( void )
{
  slider_delay = MAX( slider_delay - a_GetDeltaTime(), 0 );

  cursor_blink += a_GetDeltaTime();

  ClearWidgetsState(); 
  
  if ( !handle_input_widget && !handle_control_widget )
  {
    aWidget_t* current = GetCurrentWidget();
    if ( current != NULL )
    {
      if ( app.mouse.button == 1 && app.mouse.state == 1 )
      {
        if ( current->action != NULL )
        {
          app.mouse.button = 0;
          app.mouse.pressed = 0;
          current->action();
        }

        current->state = WI_PRESSED;
        app.active_widget = current;
        return;
      }
      
      if ( app.mouse.motion && WithinRange( app.mouse.x, app.mouse.y, current->rect ) )
      {
        current->state = WI_HOVERING;
      }
    }

    /*if ( app.keyboard[SDL_SCANCODE_UP] )
    {
      app.keyboard[SDL_SCANCODE_UP] = 0;
      if ( app.active_widget->prev->hidden == 1 )
      {
        temp = app.active_widget;
        while ( temp != NULL && temp->hidden != 1 )
        {
          temp = temp->prev;
        }

        if ( temp != NULL )
        {
          app.active_widget = temp;

        }

      }

      else
      {
        app.active_widget = app.active_widget->prev;
      }

      if ( app.active_widget == &widget_head )
      {
        app.active_widget = widget_tail;
      }
    }

    if ( app.keyboard[SDL_SCANCODE_DOWN] )
    {
      app.keyboard[SDL_SCANCODE_DOWN] = 0;

      if ( app.active_widget->next != NULL )
      {
        if ( app.active_widget->next->hidden == 1 )
        {
          temp = app.active_widget;
          while ( temp != NULL && temp->hidden != 1 )
          {
            temp = temp->next;
          }

          if ( temp != NULL )
          {
            app.active_widget = temp;

          }
        }
  
        else
        {
          app.active_widget = app.active_widget->next;
        }
      }
  
      else
      {
        app.active_widget = widget_head.next;
      }

      if ( app.active_widget == NULL )
      {
        app.active_widget = widget_head.next;
      }
    }*/

    if ( app.keyboard[SDL_SCANCODE_LEFT] )
    {
      app.keyboard[SDL_SCANCODE_LEFT] = 0;
      ChangeWidgetValue( -1 );
    }
    
    if ( app.keyboard[SDL_SCANCODE_RIGHT] )
    {
      app.keyboard[SDL_SCANCODE_RIGHT] = 0;
      ChangeWidgetValue( 1 );
    }

    if ( app.keyboard[SDL_SCANCODE_SPACE] ||
         app.keyboard[SDL_SCANCODE_RETURN] )
    { 
      app.keyboard[A_SPACEBAR] = app.keyboard[A_RETURN] = 0;

      if ( app.active_widget->type == WT_INPUT )
      {
        cursor_blink = 0;
        handle_input_widget = 1;
        memset( app.input_text, 0, sizeof( app.input_text ) );
      }
      
      else if ( app.active_widget->type == WT_CONTROL )
      {
        app.last_key_pressed = -1;
        handle_control_widget = 1;
      }

      else if ( app.active_widget->action != NULL )
      {
        app.active_widget->action();
      }
    }
  }

  else if ( handle_input_widget )
  {
    DoInputWidget();
  }

  else if( handle_control_widget )
  {
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

static void LoadWidgets( const char* filename )
{
  aAUF_t* root;
  aAUFNode_t* node;

  root = a_AUFParser( filename );

  for ( node = root->head; node != NULL; node = node->next )
  {
    CreateWidget( node );
  }

  a_AUFFree( root );
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
    aAUFNode_t* temp_toggle_label   = a_AUFGetObjectItem( root, "toggle_label" );
    aAUFNode_t* temp_x       = a_AUFGetObjectItem( root, "x" );
    aAUFNode_t* temp_y       = a_AUFGetObjectItem( root, "y" );
    aAUFNode_t* temp_boxed   = a_AUFGetObjectItem( root, "boxed" );
    aAUFNode_t* temp_hidden  = a_AUFGetObjectItem( root, "hidden" );
    aAUFNode_t* temp_padding = a_AUFGetObjectItem( root, "padding" );
    aAUFNode_t* temp_texture = a_AUFGetObjectItem( root, "texture" );
    aAUFNode_t* temp_fg      = a_AUFGetObjectItem( root, "fg" );
    aAUFNode_t* temp_bg      = a_AUFGetObjectItem( root, "bg" );
    aAUFNode_t* temp_w   = a_AUFGetObjectItem( root, "w" );
    aAUFNode_t* temp_h   = a_AUFGetObjectItem( root, "h" );
    aAUFNode_t* temp_background = a_AUFGetObjectItem( root, "background" );
    aAUFNode_t* temp_pressed    = a_AUFGetObjectItem( root, "pressed" );
    aAUFNode_t* temp_hovering   = a_AUFGetObjectItem( root, "hovering" );
    aAUFNode_t* temp_disabled   = a_AUFGetObjectItem( root, "disabled" );
    aAUFNode_t* temp_text_x   = a_AUFGetObjectItem( root, "text_x" );
    aAUFNode_t* temp_text_y   = a_AUFGetObjectItem( root, "text_y" );
    aAUFNode_t* temp_button_drop_offset = a_AUFGetObjectItem( root, "button_drop_offset" );
    
    if ( root->value_string != NULL )
    {
      STRCPY( w->name, root->value_string );
    }
    
    if ( temp_label != NULL )
    {
      STRCPY( w->label, temp_label->value_string );
    }
    
    if ( temp_toggle_label != NULL )
    {
      w->toggle_label = temp_toggle_label->value_int;
    }

    w->type = root->type;

    if ( temp_x != NULL )
    {
      w->rect.x = temp_x->value_int;
    }
    
    if ( temp_y != NULL )
    {
      w->rect.y = temp_y->value_int;
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
    {
      w->padding = temp_padding->value_int;
    }
    
    if ( temp_texture != NULL )
    {
      w->texture = temp_texture->value_int;
    }
    
    if ( temp_w != NULL )
    {
      w->rect.w = temp_w->value_int;
    }
    
    if ( temp_h != NULL )
    {
      w->rect.h = temp_h->value_int;
    }
    
    w->action = NULL;
    
    if ( temp_fg != NULL )
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
    
    if ( temp_bg != NULL )
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
      
      case WT_CONTROL:
        CreateControlWidget( w );
        break;
      
      case WT_CONTAINER:
        CreateContainerWidget( w, root );
        break;

      default:
        break;
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
  
  if ( w->toggle_label )
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

  s->step = a_AUFGetObjectItem( root, "step" )->value_int;
  s->wait_on_change = a_AUFGetObjectItem( root, "wait_on_change" )->value_int;
  s->value = 0;

  if ( w->toggle_label )
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

  input->max_length = a_AUFGetObjectItem( root, "max_length" )->value_int;
  input->text = malloc(  input->max_length + 1 );

  STRNCPY( input->text, "...", MAX_INPUT_LENGTH );

  if ( w->toggle_label )
  {
    a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  }

  input->rect.x = w->rect.x + w->rect.w + 50;
  input->rect.y = w->rect.y;
  a_CalcTextDimensions( input->text, app.font_type,
                        &input->rect.w, &input->rect.h );
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
  if ( w->toggle_label )
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

  if ( node_flex != NULL )
  {
    w->flex = node_flex->value_int;
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

    i = 0;
    temp_x = w->rect.x;
    temp_y = w->rect.y;

    int max_component_x_plus_w = 0;
    int max_component_y_plus_h = 0;

    for ( node = node_container->child; node != NULL; node = node->next )
    {
      aAUFNode_t* node_label    = a_AUFGetObjectItem( node, "label" );
      aAUFNode_t* node_toggle_label    = a_AUFGetObjectItem( node, "toggle_label" );
      aAUFNode_t* node_x        = a_AUFGetObjectItem( node, "x" );
      aAUFNode_t* node_y        = a_AUFGetObjectItem( node, "y" );
      aAUFNode_t* node_boxed    = a_AUFGetObjectItem( node, "boxed" );
      aAUFNode_t* node_hidden   = a_AUFGetObjectItem( node, "hidden" );
      aAUFNode_t* node_padding  = a_AUFGetObjectItem( node, "padding" );
      aAUFNode_t* node_texture  = a_AUFGetObjectItem( node, "texture" );
      aAUFNode_t* node_fg       = a_AUFGetObjectItem( node, "fg" );
      aAUFNode_t* node_bg       = a_AUFGetObjectItem( node, "bg" );
      aAUFNode_t* node_w   = a_AUFGetObjectItem( node, "w" );
      aAUFNode_t* node_h   = a_AUFGetObjectItem( node, "h" );
      aAUFNode_t* node_background = a_AUFGetObjectItem( node, "background" );
      aAUFNode_t* node_pressed    = a_AUFGetObjectItem( node, "pressed" );
      aAUFNode_t* node_hovering   = a_AUFGetObjectItem( node, "hovering" );
      aAUFNode_t* node_disabled   = a_AUFGetObjectItem( node, "disabled" );
      aAUFNode_t* node_text_x   = a_AUFGetObjectItem( node, "text_x" );
      aAUFNode_t* node_text_y   = a_AUFGetObjectItem( node, "text_y" );
      aAUFNode_t* node_button_drop_offset = a_AUFGetObjectItem( node, "button_drop_offset" );
      
      aWidget_t* current = &container->components[i];

      if ( node->value_string != NULL )
      {
        STRCPY( current->name, node->value_string );

      }

      if ( node_label != NULL )
      {
        STRCPY( current->label, node_label->value_string );

      }

      if ( node_toggle_label != NULL )
      {
        current->toggle_label = node_toggle_label->value_int;
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
      {
        current->padding = node_padding->value_int;
      }

      current->action = NULL;

      aAUFNode_t* node_1;
      if ( node_fg != NULL )
      {
        int j;
        j = 0;
        for ( node_1 = node_fg->child; node_1 != NULL; node_1 = node_1->next )
        {
          fg[j++] = node_1->value_int;
        }
        current->fg.r = fg[0];
        current->fg.g = fg[1];
        current->fg.b = fg[2];
        current->fg.a = fg[3];
      }

      if ( node_bg != NULL )
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
      
      if ( current->toggle_label )
      {
        a_CalcTextDimensions( current->label, app.font_type,
                             &current->rect.w, &current->rect.h );
      }

      if ( w->flex == 1 || w->flex == 2 )
      {
        current->rect.x = temp_x;
        current->rect.y = temp_y;

        //temp_x += ( current->w + container->spacing );
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

      current->state = 0;

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

          current_widget_max_x_extent = MAX( current_widget_max_x_extent,
                                            ( input->rect.x + input->rect.w ) );
          current_widget_max_y_extent = MAX( current_widget_max_y_extent,
                                            ( input->rect.y + input->rect.h ) );
          break;

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

      if ( w->flex == 1 )
      {
        temp_x += ( widget_effective_w + container->spacing );
      }

      if ( w->flex == 2 )
      {
        temp_y += ( widget_effective_h + container->spacing );
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
      w->rect.w = max_component_x_plus_w - w->rect.x;
      w->rect.h = max_component_y_plus_h - w->rect.y;
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
      aRectf_t rect = (aRectf_t){
        .x = ( w->rect.x - w->padding ),
        .y = ( w->rect.y - w->padding ),
        .w = ( w->rect.w + ( 2 * w->padding ) + ( 2 * w->text_offset.x ) ),
        .h = ( w->rect.h + ( 2 * w->padding ) + ( 2 * w->text_offset.y ) ) };
      
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
        aRectf_t rect = (aRectf_t){
          .x = ( w->rect.x - w->padding ),
          .y = ( w->rect.y - w->padding ),
          .w = ( w->rect.w + ( 2 * w->padding ) ),
          .h = ( w->rect.h + ( 2 * w->padding ) ) };
        a_DrawFilledRect( rect, w->bg );
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
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - w->padding ),
                                  .y = ( w->rect.y - w->padding ),
                                  .w = ( w->rect.w + ( 2 * w->padding ) ),
                                  .h = ( w->rect.h + ( 2 * w->padding ) ) };
      
      a_DrawFilledRect( rect, w->bg );
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
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - w->padding ),
                                  .y = ( w->rect.y - w->padding ),
                                  .w = ( w->rect.w + ( 2 * w->padding ) ),
                                  .h = ( w->rect.h + ( 2 * w->padding ) ) };
      
      a_DrawFilledRect( rect, w->bg );
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
  float width, height;

  input = ( aInputWidget_t* )w->data;

  WidgetColor( w, &c );
  
  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - w->padding ),
                                  .y = ( w->rect.y - w->padding ),
                                  .w = ( w->rect.w + ( 2 * w->padding ) ),
                                  .h = ( w->rect.h + ( 2 * w->padding ) ) };
      
      a_DrawFilledRect( rect, w->bg );
    }

    aTextStyle_t style = { .type = app.font_type, .fg = c, .bg = {0,0,0,0}, .align = TEXT_ALIGN_LEFT, .wrap_width = 0, .scale = 1.0f, .padding = 0 };
    a_DrawText( w->label, w->rect.x, w->rect.y, style );

    a_DrawText( input->text, input->rect.x, input->rect.y, style );

    if ( handle_input_widget && app.active_widget == w &&
         ( (int)cursor_blink % (int)FPS_CAP < ( FPS_CAP / 2 ) ) )
    {
      a_CalcTextDimensions( input->text, app.font_type, &width, &height );
      aRectf_t cursor_rect = ( aRectf_t ){ .x = ( input->rect.x + width + 4 ),
                                         .y = ( input->rect.y + 14 ),
                                         .w = 32,
                                         .h = 32 };
      a_DrawFilledRect( cursor_rect, green );
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
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - w->padding ),
                                  .y = ( w->rect.y - w->padding ),
                                  .w = ( w->rect.w + ( 2 * w->padding ) ),
                                  .h = ( w->rect.h + ( 2 * w->padding ) ) };
      
      a_DrawFilledRect( rect, w->bg );
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
    aRectf_t rect = (aRectf_t){ 
      .x = ( w->rect.x - w->padding - 5 ),
      .y = ( w->rect.y - w->padding - 3 ),
      .w = ( w->rect.w + ( 2 * w->padding + 15 ) + ( 2 * w->text_offset.x ) ),
      .h = ( w->rect.h + ( 2 * w->padding + 10 ) + ( 2 * w->text_offset.y ) ) };
    
    if ( w->texture )
    {
      a_BlitRect( w->images[w->state], NULL, &rect, 1 );
    }

    else
    {
      if ( w->boxed == 1 )
      {
        a_DrawFilledRect( rect, w->bg );
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
      if ( widget_head.images[i] )
      {
        SDL_FreeSurface( widget_head.images[i]->surface );
        SDL_DestroyTexture( widget_head.images[i]->texture );
        free( widget_head.images[i]->filename );
      }
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
        
        case WT_CONTAINER:
          temp_container = (aContainerWidget_t*)current->data;
          ContainerWidgetFree( temp_container );
          break;
        
        default:
          break;
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
    widget_tail = &widget_head;
  }

  return 0;
}

static void ContainerWidgetFree( aContainerWidget_t* con )
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
      if ( WithinRange( app.mouse.x, app.mouse.y, current->rect ) )
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
              if ( WithinRange( app.mouse.x, app.mouse.y, component->rect ) )
              {
                return component;
              }
            }
          }

        }

        else
        {
          return current;
        }
      }
    }

    current = current->next;
  }

  return NULL;
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
  if ( app.active_widget != NULL )
  {
    if ( strcmp( w->name, app.active_widget->name ) == 0 )
    {
      c->g = 255;
      c->r = c->b = 0;
    }
    else
    {
      c->r = w->fg.r;
      c->g = w->fg.g;
      c->b = w->fg.b;
    }
  }
}

