#include <stdio.h>

#include <Archimedes.h>
#include <Daedalus.h>

#include "Bitmask_defines.h"
#include "Bitmask_structs.h"

TileMask_t* TileMaskGenerate( int width, int height )
{
  TileMask_t* new_tilemask = malloc( sizeof(TileMask_t) * ( width * height ) );
  if ( new_tilemask == NULL ) return NULL;

  for ( int i = 0; i < ( width * height ); i++ )
  {
    new_tilemask[i].pos     = (aPoint2i_t){0};
    new_tilemask[i].bitmask = 0;
  }

  return new_tilemask;
}

void TileMaskUpdate( TileMask_t* mask, int index )
{
  
}

