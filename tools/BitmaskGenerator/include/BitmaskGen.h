#ifndef __BM_GENERATOR_H__
#define __BM_GENERATOR_H__

#include "Bitmask_structs.h"

Bitmask_t* BitmaskGenerate( TileMask_t* tilemask_arr );
aImage_t* BitmaskGetSprite( Bitmask_t* mask );

#endif

