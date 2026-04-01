/* 
 * src/aWidgetDraw.c
 *
 * This file defines the functions used to draw widgets.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include "Archimedes.h"

void _a_Internal_WidgetDrawButton( aWidget_t* w, int cc_index )
{
  aColor_t c;
  int offset = 0;
  
  a_WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    if ( w->texture == 1 )
    {
      int pl, pr, pt, pb;
      _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
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
        _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
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

void _a_Internal_WidgetDrawSelect( aWidget_t* w, int cc_index )
{
  aColor_t c;
  char text[128];
  aSelectWidget_t* s;
  s = ( aSelectWidget_t* ) w->data;

  a_WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - pl ),
                                  .y = ( w->rect.y - pt ),
                                  .w = ( w->rect.w + pl + pr ),
                                  .h = ( w->rect.h + pt + pb ) };

      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }

    aTextStyle_t style = { 
      .type = app.font_type, 
      .fg = c, 
      .bg = {0,0,0,0}, 
      .align = TEXT_ALIGN_LEFT, 
      .wrap_width = 0, 
      .scale = 1.0f, 
      .padding = 0 
    };
    
    a_DrawText( w->label, w->rect.x, w->rect.y, style );
    sprintf( text, "< %s >", s->options[s->value] );

    a_DrawText( text, s->rect.x + 100, s->rect.y, style );
  }
}

void _a_Internal_WidgetDrawSlider( aWidget_t* w, int cc_index )
{
  aColor_t c;
  aSliderWidget_t* slider;
  double width;

  slider = ( aSliderWidget_t* )w->data;

  a_WidgetColor( w, &c );
  
  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
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

void _a_Internal_WidgetDrawInput( aWidget_t* w, int cc_index )
{
  aColor_t c;
  aInputWidget_t* input;
  float text_width, text_height;
  aRectf_t glyph_rect;

  input = ( aInputWidget_t* )w->data;
  
  a_CalcTextDimensions( input->text, app.font_type, &text_width, &text_height );
  glyph_rect = a_GetGlyphSize();

  a_WidgetColor( w, &c );
  
  if ( w->hidden != 1 )
  {
    
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
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

    if ( app.ui_layers[cc_index]->handle_input_widget && is_visible &&
    strncmp( w->name, app.ui_layers[cc_index]->active_widget->name, MAX_FILENAME_LENGTH ) == 0 )
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

void _a_Internal_WidgetDrawOutput( aWidget_t* w, int cc_index )
{
  aColor_t c;
  aOutputWidget_t* output;
  float label_w, label_h, text_w, text_h;

  output = ( aOutputWidget_t* )w->data;

  a_WidgetColor( w, &c );

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
      _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
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

void _a_Internal_WidgetDrawControl( aWidget_t* w, int cc_index )
{
  aColor_t c;
  aControlWidget_t* control;
  char text[32];

  control = ( aControlWidget_t* )w->data;

  a_WidgetColor( w, &c );

  if ( w->hidden != 1 )
  {
    if ( w->boxed == 1 )
    {
      int pl, pr, pt, pb;
      _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
      aRectf_t rect = (aRectf_t){ .x = ( w->rect.x - pl ),
                                  .y = ( w->rect.y - pt ),
                                  .w = ( w->rect.w + pl + pr ),
                                  .h = ( w->rect.h + pt + pb ) };

      a_DrawFilledRect( rect, w->bg );
      a_DrawRect( rect, black );
    }

    aTextStyle_t style = { .type = app.font_type, .fg = c, .bg = {0,0,0,0}, .align = TEXT_ALIGN_LEFT, .wrap_width = 0, .scale = 1.0f, .padding = 0 };
    a_DrawText( w->label, w->rect.x, w->rect.y, style );

    if ( app.ui_layers[cc_index]->handle_control_widget &&
         app.ui_layers[cc_index]->active_widget == w )
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
void _a_Internal_WidgetDrawContainer( aWidget_t* w, int cc_index )
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
        _a_Internal_resolve_padding( comp, &cpl, &cpr, &cpt, &cpb );
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
    _a_Internal_resolve_padding( w, &pl, &pr, &pt, &pb );
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
            _a_Internal_WidgetDrawButton( &current, cc_index );
            break;

          case WT_SLIDER:
            _a_Internal_WidgetDrawSlider( &current, cc_index );
            break;

          case WT_INPUT:
            _a_Internal_WidgetDrawInput( &current, cc_index );
            break;

          case WT_OUTPUT:
            _a_Internal_WidgetDrawOutput( &current, cc_index );
            break;

          case WT_SELECT:
            _a_Internal_WidgetDrawSelect( &current, cc_index );
            break;

          case WT_CONTROL:
            _a_Internal_WidgetDrawControl( &current, cc_index );
            break;

          default:
            break;
        } 
      }
    }
  }
}

void _a_Internal_WidgetDrawModal( aWidget_t* w, int cc_index )
{

}

void _a_Internal_WidgetDrawDragableBox( aWidget_t* w, int cc_index )
{

}

void _a_Internal_WidgetDrawDropDown( aWidget_t* w, int cc_index )
{

}

