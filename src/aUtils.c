/* 
 * src/aUtils.c
 *
 * This file defines the utility functions used in Archimedes.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Archimedes.h"

char* a_ReadFile( const char* filename, int* file_size )
{
  long fileSize;
  char* fileString;
  FILE* file;

  file = fopen( filename, "r" );
  if ( file == NULL )
  {
    printf( "Error loading file: %s\n", filename );
    exit(EXIT_FAILURE);
  }

  fseek( file, 0, SEEK_END );
  fileSize = ftell( file );
  rewind( file );

  fileString= ( char* )malloc( fileSize + 1 );
  if ( fileString == NULL )
  {
    printf( "Error allocating memory for file string: %s\n", filename );
    fclose( file );
    return NULL;
  }

  if ( fread( fileString, fileSize, 1, file ) != 1 )
  {
    printf("Failed to read vertex file: %s\n", filename);
    fclose(file);
    free(fileString);
    return NULL;
  }

  fclose(file);

  fileString[fileSize] = '\0';
  
  *file_size = fileSize;

  return fileString;
}

char* a_STR_NDUP( const char* str, size_t len )
{
  char* new_string = malloc( len + 1 );
  
  if ( new_string )
  {
    memcpy( new_string, str, len );
    new_string[len] = '\0';
  }

  return new_string;
}

char* a_ParseString( char delimiter, char* str, int str_len )
{
  char* return_str = (char*)malloc( sizeof(char) * str_len );
  if ( return_str == NULL )
  {
    printf( "Failed to allocate memory for return_str\n" );
    return NULL;
  }

  for ( int i = 0; i < str_len; i++ )
  {
    if ( str[i] != delimiter )
    {
      return_str[i] = str[i];

    }

    else
    {
      return_str[i] = '\0';
      return return_str;
    }
  }

  return NULL;
}

char* a_ParseStringDoubleDelim( char delimiter1, char delimiter2, char* str, int str_len )
{
  char* return_str = (char*)malloc( sizeof(char) * str_len );
  if ( return_str == NULL )
  {
    printf( "Failed to allocate memory for return_str\n" );
    return NULL;
  }

  for ( int i = 0; i < str_len; i++ )
  {
    if ( str[i] != delimiter1 && str[i] != delimiter2 )
    {
      return_str[i] = str[i];

    }

    else
    {
      return_str[i] = '\0';
      return return_str;
    }
  }

  return NULL;
}

int a_CountNewLines( const char* file_string, const int file_size )
{
  int nl_count = 0;

  for ( int i = 0; i < file_size; i++ )
  {
    if ( file_string[i] == '\n' )
    {
      nl_count++;
    }
  }

  return nl_count;
}

char** a_ParseLinesInFile( const char* file_string, const int file_size,
                                const int nl_count )
{
  char** new_line = ( char** )malloc( sizeof( char* ) * nl_count );

  if ( new_line == NULL )
  {
    printf("Failed to allocate memory for line\n");
    return NULL;
  }
  
  int line_start  = 0;
  int line_end    = 0;
  int line_length = 0;
  int line_count  = 0;

  for ( int i = 0; i < file_size; i++ )
  {
    if ( file_string[i] == '\n' )
    {
      line_end = i;
      line_length = line_end - line_start;
      
      if ( line_length == 0 )
      {
        line_start = line_end + 1;

        new_line[line_count] = NULL;

        line_count++;
        continue;
      }
      
      new_line[line_count] = ( char* )malloc( sizeof(char) * ( line_length + 1 ) );
      if ( new_line[line_count] == NULL )
      {
        for ( int j = 0; j < line_count; j++)
        {
          free( new_line[j] );
        }

        free( new_line );
        return NULL;
      }

      for( int j = 0; j < line_length; j++ )
      {
        new_line[line_count][j] = file_string[j+line_start];
      }

      new_line[line_count][line_length] = '\0';

      line_start = line_end + 1;
      
      line_count++;

    }
  }

  return new_line;
}

