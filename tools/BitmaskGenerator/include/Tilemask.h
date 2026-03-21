#ifndef __BM_GENERATOR_H__
#define __BM_GENERATOR_H__

#include "Bitmask_structs.h"

TileMask_t* TileMaskGenerate( int width, int height );
void TileMaskUpdate( TileMask_t* mask, int index );

#endif

