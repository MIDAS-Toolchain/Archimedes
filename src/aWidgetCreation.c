/* 
 * src/aWidgetCreation.c
 *
 * This file defines the functions used to create widgets.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include "Archimedes.h"

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
void _a_Internal_WidgetCreateButton( aWidget_t* w )
{
  a_CalcTextDimensions( w->label, app.font_type, &w->rect.w, &w->rect.h );
  w->state = WI_BACKGROUND;
}

/**
 * @brief Creates type-specific data for a Select widget.
 *
 * This function allocates and initializes an `aSelectWidget_t` structure,
 * linking it to the `data` member of the base widget.
 *
 * @param w A pointer to the `aWidget_t` structure for the select widget.
 */
void _a_Internal_WidgetCreateSelect( aWidget_t* w, aAUFNode_t* root )
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
 */
void _a_Internal_WidgetCreateSlider( aWidget_t* w, aAUFNode_t* root )
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
void _a_Internal_WidgetCreateInput( aWidget_t* w, aAUFNode_t* root )
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

void _a_Internal_WidgetCreateOutput( aWidget_t* w, aAUFNode_t* root )
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
void _a_Internal_WidgetCreateControl( aWidget_t* w )
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
void _a_Internal_WidgetCreateContainer( aWidget_t* w, aAUFNode_t* root )
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

void _a_Internal_WidgetCreateModal( aWidget_t* w, aAUFNode_t* root )
{

}

void _a_Internal_WidgetCreateDragableBox( aWidget_t* w, aAUFNode_t* root )
{

}

void _a_Internal_WidgetCreateDropDown( aWidget_t* w, aAUFNode_t* root )
{

}

