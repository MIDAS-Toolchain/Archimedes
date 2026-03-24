#include <stdio.h>

#include <Archimedes.h>

#include "Bitmask_defines.h"
#include "Bitmask_structs.h"
#include "BitmaskGen.h"
#include "God.h"
#include "Tilemask.h"

static void DrawFPS( void );

static void sLogic( float );
static void sDraw( float );

static void load_img( void );

aSpriteSheet_t* sheet = NULL;
int g_sprite_w = 0;
int g_sprite_h = 0;

aCamera2D_t g_camera;
aRectf_t GOD;

TileMask_t* tilemask = NULL;
aTextStyle_t text_style;
aColor_t cell_select = { .r = 0, .g = 128, .b = 128, .a = 128 };

static int tile_grid = 0;
static int cell_grid = 0;
static int show_bitmask = 0;
static int debug = 0;

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
  
  tilemask = TileMaskGenerate( sheet->v_count, sheet->h_count );
  
  text_style = (aTextStyle_t){
    .type = FONT_CODE_PAGE_437,
    .fg = white,
    .bg = black,
    .align = TEXT_ALIGN_CENTER,
    .wrap_width = 0,
    .scale = 1.0f,
    .padding = 0
  };

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
  
  if ( app.keyboard[A_F3] == 1 )
  {
    app.keyboard[A_F3] = 0;
    debug = !debug;
  }

  if ( app.keyboard[A_T] == 1 )
  {
    app.keyboard[A_T] = 0;
    show_bitmask = !show_bitmask;
  }
  
  if ( app.keyboard[A_G] == 1 )
  {
    app.keyboard[A_G] = 0;
    tile_grid = !tile_grid;
  }
  
  if ( app.keyboard[A_C] == 1 )
  {
    app.keyboard[A_C] = 0;
    cell_grid = !cell_grid;
  }

  GodInput(&GOD, 16.0f, WORLD_WIDTH, WORLD_HEIGHT );
  a_CameraInput( &g_camera, WORLD_WIDTH, WORLD_HEIGHT );

  a_CameraUpdate( &g_camera, &GOD, WORLD_WIDTH, WORLD_HEIGHT );
  
  if ( sheet != NULL )
  {
    float world_x = ( app.mouse.x / g_camera.zoom ) + g_camera.x;
    float world_y = ( app.mouse.y / g_camera.zoom ) + g_camera.y;
    
    float sprite_x = ( (float)WORLD_WIDTH/2 ) - ((float)sheet->img_width/2);
    float sprite_y = ( (float)WORLD_HEIGHT/2 ) - ((float)sheet->img_height/2);
    
    if ( world_x >= sprite_x && world_y >= sprite_y &&
         world_x < ( sprite_x + sheet->img_width ) &&
         world_y < ( sprite_y + sheet->img_height ) )
    {
      float rel_x = world_x - sprite_x;
      float rel_y = world_y - sprite_y;

      int tile_x = (int)rel_x / DEFAULT_SPRITE_W;
      int tile_y = (int)rel_y / DEFAULT_SPRITE_H;


      int local_x = (int)rel_x % DEFAULT_SPRITE_W;
      int local_y = (int)rel_y % DEFAULT_SPRITE_W;

      int index = tile_y * (sheet->v_count) + tile_x;

      int cell_x = local_x / ( DEFAULT_SPRITE_W / 3 );
      int cell_y = local_y / ( DEFAULT_SPRITE_H / 3 );
      int cell_index = cell_y * 3 + cell_x;

      if ( app.mouse.button == 1 )
      {
        //tilemask[index].bitmask = 1;
        tilemask[index].neighbors[cell_index] = 1;

        TileMaskUpdate( tilemask, index );
      }
      
      if ( app.mouse.button == 3 )
      {
        tilemask[index].bitmask = 0;

        tilemask[index].neighbors[cell_index] = 0;

        TileMaskUpdate( tilemask, index );
      }
    }
  }

}

static void sDraw( float dt )
{
  DrawFPS();
  //a_DrawWidgets();

  if ( sheet != NULL )
  {
    float x = ( (float)WORLD_WIDTH/2 ) - ((float)sheet->img_width/2);
    float y = ( (float)WORLD_HEIGHT/2 ) - ((float)sheet->img_height/2);
    aRectf_t screen_sheet_rect = {0};
    aRectf_t sheet_rect = {
      .x = x,
      .y = y,
      .w = sheet->img_width,
      .h = sheet->img_height
    };
    a_WorldRectToCameraRect( &g_camera, sheet_rect, &screen_sheet_rect );

    a_BlitRect( sheet->sheet, NULL, &screen_sheet_rect, 1 );

    for ( int j = 0; j < sheet->v_count; j++ )
    {
      for ( int i = 0; i < sheet->h_count; i++ )
      {
        char buffer[MAX_LINE_LENGTH];

        int w = i * g_sprite_w;
        int h = j * g_sprite_h;

        aRectf_t screen_rect = {0};
        aRectf_t rect = (aRectf_t){
          .x = x + w,
          .y = y + h,
          .w = g_sprite_w,
          .h = g_sprite_h
        };
        
        a_WorldRectToCameraRect( &g_camera, rect, &screen_rect );
        
        if( tile_grid || debug )
        {
          a_DrawRect( screen_rect, red );
        }

        int tm_index = j * sheet->v_count + i;
        
        if ( show_bitmask || debug )
        {
          snprintf( buffer, MAX_NAME_LENGTH, "%d", tilemask[tm_index].bitmask );
          a_DrawText( buffer, screen_rect.x, screen_rect.y, text_style );
        }
        
        float x1, y1;
        int w1, h1;
        w1 = g_sprite_w / 3;
        h1 = g_sprite_h / 3;

        for( int k = 0; k < 9; k++ )
        {
          x1 = k / 3;
          y1 = k % 3;
          
          aRectf_t screen_small_rect = {0};
          aRectf_t small_rect = {
          .x = ( x1 * w1 ) + rect.x,
          .y = ( y1 * h1 ) + rect.y,
          .w = w1,
          .h = h1
          };

          int cell_index = y1 * 3 + x1;

          a_WorldRectToCameraRect( &g_camera, small_rect, &screen_small_rect );
          if ( cell_grid || debug )
          {
            a_DrawRect( screen_small_rect, green );
          }

          if ( tilemask[tm_index].neighbors[cell_index] )
          {
            a_DrawFilledRect( screen_small_rect, cell_select );

          }
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

