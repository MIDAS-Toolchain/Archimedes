#include <stdio.h>

#include <Archimedes.h>

#include "Bitmask_defines.h"

static void DrawFPS( void );

static void sLogic( float );
static void sDraw( float );

static void load_img( void );

aSpriteSheet_t* sheet = NULL;

void Init_Stage( void )
{
  app.delegate.logic = sLogic;
  app.delegate.draw  = sDraw;

  app.g_viewport = (aRectf_t){ 500.0f, 500.0f, 50.0f, 50.0f };
  sheet = a_SpriteSheetCreate( "resources/assets/tilemap.png", 32, 32 );
  
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
    aPoint2f_t scale = a_ViewportCalculateScale();
    int view_x = (int)( app.g_viewport.x - app.g_viewport.w );
    int view_y = (int)( app.g_viewport.y - app.g_viewport.h );
    float x = ( 1024 / scale.x ) + view_x;
    float y = ( 1024 / scale.y ) + view_y;
    a_ViewportBlit( sheet->sheet, x, y );
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
    }
  }

  aWidget_t* w = a_GetWidget("img_load");
  w->hidden = 1;
}

