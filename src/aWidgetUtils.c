/* 
 * src/aWidgetUtils.c
 *
 * This file defines the utility functions for aWidgets, Creation, and Draw.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include "Archimedes.h"

static aWidget_t* focused_container;
static int mouse_moved;

aWidget_t* a_WidgetGetCurrent( void )
{
  aWidget_t* current = app.ui_layers[app.ui_layer_index]->head;

  while ( current != NULL )
  {
    if ( current->hidden == 0 )
    {
      if ( a_WidgetWithinRangePadded( app.mouse.x, app.mouse.y, current ) )
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
              if ( a_WidgetWithinRangePadded( app.mouse.x, app.mouse.y, component ) )
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

int a_WidgetWithinRangePadded( int x, int y, aWidget_t* w )
{
  if ( w == NULL ) return 0;
  int pl, pr, pt, pb;
  _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );

  if ( x >= ( w->rect.x - pl ) && y >= ( w->rect.y - pt ) &&
       x <= ( w->rect.x + w->rect.w + pr ) &&
       y <= ( w->rect.y + w->rect.h + pb ) )
  {
    return 1;
  }

  return 0;
}

void a_WidgetsClearState( void )
{
  aWidget_t* current = app.ui_layers[app.ui_layer_index]->head;

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

void a_WidgetContainerFree( aContainerWidget_t* con, aWidget_t* parent )
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

void a_WidgetReflowContainer( aWidget_t* w,
                              aContainerWidget_t* con )
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
    _a_Internal_resolve_padding( current, &cpl, &cpr, &cpt, &cpb );

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

void a_WidgetColor( aWidget_t* w, aColor_t* c )
{
  c->r = w->fg.r;
  c->g = w->fg.g;
  c->b = w->fg.b;

  if ( app.ui_layers[app.ui_layer_index]->active_widget != NULL
    && strcmp( w->name, app.ui_layers[app.ui_layer_index]->active_widget->name ) == 0
    && w->has_ag )
  {
    c->r = w->ag.r;
    c->g = w->ag.g;
    c->b = w->ag.b;
  }
}

int _a_Internal_WithinRange( int x, int y, aRectf_t rect )
{
  if ( x >= rect.x && y >= rect.y &&
       x <= ( rect.x + rect.w ) && y <= ( rect.y + rect.h ) )
  {
    return 1;
  }

  return 0;
}

aSoundEffect_t* _a_Internal_load_widget_sound( aAUFNode_t* node )
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

int _a_Internal_resolve_color_ref( aAUFNode_t* node, aColor_t* out )
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

int _a_Internal_resolve_int_ref( aAUFNode_t* node )
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

void _a_Internal_resolve_padding( aWidget_t* w,
                                  int* left, int* right,
                                  int* top, int* bottom )
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

