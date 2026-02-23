#include <stdio.h>

#include <Archimedes.h>

static void DrawFPS( void );

static void sLogic( float );
static void sDraw( float );

void Init_Stage( void )
{
  app.delegate.logic = sLogic;
  app.delegate.draw  = sDraw;
  
  a_WidgetsInit( "resources/load_img_menu.auf" );
  app.active_widget = a_GetWidget( "img_load" );
}

static void sLogic( float dt )
{ 
  a_DoInput();

  a_DoWidget();
  
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
  a_DrawWidgets();
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

  a_DrawText( buffer, 600, 100, text_style );
}

