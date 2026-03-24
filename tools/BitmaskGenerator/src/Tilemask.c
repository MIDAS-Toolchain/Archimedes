#include <stdio.h>

#include <Archimedes.h>
#include <Daedalus.h>

#include "Bitmask_defines.h"
#include "Bitmask_structs.h"

TileMask_t* TileMaskGenerate( int width, int height )
{
  TileMask_t* new_tilemask = malloc( sizeof(TileMask_t) * ( width * height ) );
  if ( new_tilemask == NULL ) return NULL;

  memset( new_tilemask, 0, ( sizeof( TileMask_t ) * ( width * height ) ) );

  return new_tilemask;
}

void TileMaskUpdate( TileMask_t* mask, int index )
{
  uint8_t bitmask=0;
  
  for ( int i = 0; i < 8; i++ )
  {
    uint8_t temp_mask;

    int neighor_index = ( i < 4 ) ? i : i + 1;
    
    temp_mask = mask[index].neighbors[neighor_index];

    bitmask |= temp_mask<<i;
  }

  mask[index].bitmask |= bitmask;
}

