#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <Archimedes.h>
#include <Daedalus.h>

#include "Bitmask_defines.h"
#include "Bitmask_structs.h"

dArray_t* FindPngFiles( const char* base_dir, dArray_t* array )
{
  struct dirent* dir_p;
  DIR* dir = opendir( base_dir );

  if ( !dir ) return NULL;

  while ( ( dir_p = readdir( dir ) ) != NULL )
  {
    if( dir_p->d_type == DT_DIR )
    {
      if ( strcmp(dir_p->d_name, ".") != 0 &&
        strcmp( dir_p->d_name, ".." ) != 0 &&
        strcmp( dir_p->d_name, ".git" ) != 0 )
      {
        char new_path[1024];
        snprintf( new_path, sizeof(new_path), "%s/%s", base_dir, dir_p->d_name );
        FindPngFiles( new_path, array );
      }
    }
    else
    {
      char* dot = strchr( dir_p->d_name, '.' );
      if ( dot && strcmp( dot, ".png" ) == 0 )
      {
        char new_path[1024];
        snprintf( new_path, sizeof(new_path), "%s/%s", base_dir, dir_p->d_name );
        d_ArrayAppend( array, new_path );
      }
    }
  }

  closedir( dir );

  return array;
}

