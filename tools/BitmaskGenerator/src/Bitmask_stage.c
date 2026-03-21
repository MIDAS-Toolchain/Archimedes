#include <stdio.h>

#include <Archimedes.h>

#include "Bitmask_defines.h"
#include "God.h"

static void DrawFPS( void );

static void sLogic( float );
static void sDraw( float );

static void load_img( void );

aSpriteSheet_t* sheet = NULL;
int g_sprite_w = 0;
int g_sprite_h = 0;

aCamera2D_t g_camera;
aRectf_t GOD;

void Init_Stage( void )
{
  app.delegate.logic = sLogic;
  app.delegate.draw  = sDraw;
  
  sheet = a_SpriteSheetCreate( "resources/assets/tilemap.png", 32, 32 );
  g_sprite_w = DEFAULT_SPRITE_W;
  g_sprite_h = DEFAULT_SPRITE_H;

  g_camera = a_Camera2DInit(SCREEN_WIDTH, SCREEN_HEIGHT);
  GOD = (aRectf_t){ .x = (float)WORLD_WIDTH  / 2,
                    .y = (float)WORLD_HEIGHT / 2,
                    .w = 32, .h = 32 };

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

  GodInput(&GOD, 16.0f, WORLD_WIDTH, WORLD_HEIGHT );
  a_CameraInput( &g_camera, WORLD_WIDTH, WORLD_HEIGHT );

  a_CameraUpdate( &g_camera, &GOD, WORLD_WIDTH, WORLD_HEIGHT );
}

static void sDraw( float dt )
{
  DrawFPS();
  //a_DrawWidgets();

  if ( sheet != NULL )
  {
    float x = ( (float)WORLD_WIDTH/2 ) - ((float)sheet->img_width/2);
    float y = ( (float)WORLD_HEIGHT/2 ) - ((float)sheet->img_height/2);
    
    aRectf_t sheet_rect = {
      .x = (x-g_camera.x)  * g_camera.zoom,
      .y = (y-g_camera.y)  * g_camera.zoom,
      .w = sheet->img_width * g_camera.zoom,
      .h = sheet->img_height * g_camera.zoom
    };

    a_BlitRect( sheet->sheet, NULL, &sheet_rect, 1 );

    for ( int j = 0; j < sheet->v_count; j++ )
    {
      for ( int i = 0; i < sheet->h_count; i++ )
      {
        int w = i * g_sprite_w;
        int h = j * g_sprite_h;

        aRectf_t rect = (aRectf_t){
          .x = ((x-g_camera.x ) + w) * g_camera.zoom,
          .y = ((y-g_camera.y ) + h) * g_camera.zoom,
          .w = g_sprite_w * g_camera.zoom-1,
          .h = g_sprite_h * g_camera.zoom-1
        };

        a_DrawRect( rect, red );
        
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

