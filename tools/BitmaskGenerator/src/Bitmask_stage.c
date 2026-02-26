#include <stdio.h>

#include <Archimedes.h>

#include "Bitmask_defines.h"

static void DrawFPS( void );

static void sLogic( float );
static void sDraw( float );

static void load_img( void );

aSpriteSheet_t* sheet = NULL;
int g_sprite_w = 0;
int g_sprite_h = 0;

void Init_Stage( void )
{
  app.delegate.logic = sLogic;
  app.delegate.draw  = sDraw;
  
  float ratio = SCREEN_WIDTH/SCREEN_HEIGHT;
  float view_h = 50.0f;
  float view_w = view_h * ratio;
  app.g_viewport = (aRectf_t){ 500.0f, 500.0f, view_h, view_w };
  
  sheet = a_SpriteSheetCreate( "resources/assets/tilemap.png", 32, 32 );
  g_sprite_w = DEFAULT_SPRITE_W;
  g_sprite_h = DEFAULT_SPRITE_H;
  
  a_WidgetsInit( "resources/load_img_menu.auf" );
  app.active_widget = a_GetWidget( "img_load" );

  aContainerWidget_t* container = a_GetContainerFromWidget("img_load");
  for ( int i = 0; i < container->num_components; i++ )
  {
    aWidget_t w = container->components[i];
    if ( strncmp( w.name, "load", MAX_NAME_LENGTH ) == 0 )
    {
      container->components[i].action = load_img;
    }
  }
}

static void sLogic( float dt )
{ 
  a_DoInput();

  //a_DoWidget();

  a_ViewportInput( &app.g_viewport, WORLD_WIDTH, WORLD_HEIGHT );
  
  if ( app.keyboard[A_ESCAPE] == 1 )
  {
    app.keyboard[A_ESCAPE] = 0;
    app.running = 0;
    return;
  }
  
  if ( app.keyboard[A_F1] == 1 )
  {
    app.keyboard[A_F1] = 0;
    a_WidgetsInit( "resources/load_img_menu.auf" );
  }
}

static void sDraw( float dt )
{
  DrawFPS();
  //a_DrawWidgets();

  if ( sheet != NULL )
  {
    float x = ( (float)WORLD_WIDTH/2 ) + ((float)sheet->img_width/2);
    float y = ( (float)WORLD_HEIGHT/2 ) + ((float)sheet->img_height/2);
    a_ViewportBlit( sheet->sheet, x, y );
    for ( int j = 1; j <= sheet->v_count; j++ )
    {
      for ( int i = 1; i <= sheet->h_count; i++ )
      {
        int w = i * g_sprite_w;
        int h = j * g_sprite_h;
        aRectf_t rect = (aRectf_t){
          .x = (x-sheet->img_width ) + w,
          .y = (y-sheet->img_height ) + h,
          .w = g_sprite_w,
          .h = g_sprite_h
        };
        a_ViewportDrawRect( rect, red );
        
        for( int k = 0; k < 9; k++ )
        {
          int x1, y1, w1, h1;
          aRectf_t small_rect;
          x1 = k / 3;
          y1 = k % 3;
        }
      }
    }
  }
}

static void DrawFPS( void )
{
  char buffer[MAX_LINE_LENGTH];
  snprintf(buffer, MAX_NAME_LENGTH, "%f", app.time.avg_FPS );

  aTextStyle_t text_style = {
    .type = FONT_CODE_PAGE_437,
    .fg = white,
    .bg = black,
    .align = TEXT_ALIGN_CENTER,
    .wrap_width = 0,
    .scale = 1.0f,
    .padding = 0
  };

  a_DrawText( buffer, 40, 0, text_style );
}

static void load_img( void )
{
  int sprite_w = 0, sprite_h = 0;
  
  aContainerWidget_t* container = a_GetContainerFromWidget("img_load");
  
  for ( int i = 0; i < container->num_components; i++ )
  {
    aWidget_t w = container->components[i];
    if ( strncmp( w.name, "sprite_w", MAX_NAME_LENGTH ) == 0 )
    {
      aInputWidget_t* inp = (aInputWidget_t*)w.data;
      if ( strncmp( inp->text, "...", MAX_NAME_LENGTH ) != 0 )
      {
        sprite_w = atoi( inp->text );
      }
    }
    
    if ( strncmp( w.name, "sprite_h", MAX_NAME_LENGTH ) == 0 )
    {
      aInputWidget_t* inp = (aInputWidget_t*)w.data;
      if ( strncmp( inp->text, "...", MAX_NAME_LENGTH ) != 0 )
      {
        sprite_h = atoi( inp->text );
      }
    }
    
    if ( strncmp( w.name, "filename", MAX_NAME_LENGTH ) == 0 )
    {
      aInputWidget_t* inp = (aInputWidget_t*)w.data;
      if ( sprite_w == 0 || sprite_h == 0 )
      {
        sprite_w = DEFAULT_SPRITE_W;
        sprite_h = DEFAULT_SPRITE_H;
      }
      
      sheet = a_SpriteSheetCreate( inp->text, sprite_w, sprite_h );
      g_sprite_w = sprite_w;
      g_sprite_h = sprite_h;
    }
  }

  aWidget_t* w = a_GetWidget("img_load");
  w->hidden = 1;
}

