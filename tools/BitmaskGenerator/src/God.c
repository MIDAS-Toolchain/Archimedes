#include <stdio.h>

#include <Archimedes.h>
#include <Daedalus.h>

#include "Bitmask_defines.h"
#include "Bitmask_structs.h"

void GodInput( aRectf_t* god, float speed,
               const int max_width, const int max_height )
{
  if ( app.keyboard[A_W] == 1 )
  {
    app.keyboard[A_W] = 0;
    god->y -= speed;
  }
  
  if ( app.keyboard[A_A] == 1 )
  {
    app.keyboard[A_A] = 0;
    god->x -= speed;
  }
  
  if ( app.keyboard[A_S] == 1 )
  {
    app.keyboard[A_S] = 0;
    god->y += speed;
  }
  
  if ( app.keyboard[A_D] == 1 )
  {
    app.keyboard[A_D] = 0;
    god->x += speed;
  }

  if ( god->x < 0 ) god->x = 0;
  if ( god->y < 0 ) god->y = 0;
  if ( god->x > max_width  - god->w )
  {
    god->x = max_width  - god->w;
  }
  if ( god->y > max_height - god->h )
  {
    god->y = max_height - god->h;
  }
}

