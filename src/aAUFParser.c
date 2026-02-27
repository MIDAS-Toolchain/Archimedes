/*
 * aAUFParser.c:
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 ************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Archimedes.h"

static int ParserLineToRoot( aAUF_t* root, char** line, int nl_count );
static int ParserWidgetToNode( aAUFNode_t* node, char** line, int nl_count, int idx );
static int handle_parenthesis( aAUFNode_t* root, char* string, int str_len );
static int handle_char( aAUFNode_t* root, char* string, int str_len );
static int GetType( char* name );

static void handle_widget_definition( aAUFNode_t* node, const char* string );

static aAUFNode_t* g_container = NULL;
static aAUFNode_t* g_temp_container = NULL;

static int widget_count = 0;

aAUF_t* a_AUFParser( const char* filename )
{
  char** line;
  char* file_string;
  int file_size = 0;
  int newline_count = 0;
  widget_count = 0;
 
  aAUF_t* new_root = a_AUFCreation();

  file_string = a_ReadFile( filename, &file_size );

  newline_count = a_CountNewLines( file_string, file_size );

  line = a_ParseLinesInFile( file_string, file_size, newline_count );
  ParserLineToRoot( new_root, line, newline_count );
  
  free( file_string );
  a_FreeLine( line, newline_count );
  g_container = NULL;
  g_temp_container = NULL;

  return new_root;
}

static int ParserLineToRoot( aAUF_t* root, char** line, int nl_count )
{
  for ( int i = 0; i < nl_count; i++ )
  {
    if ( line[i] != NULL && widget_count < MAX_WIDGET_COUNT )
    {
      char* string = line[i];

      if ( string[0] == '[' && string[1] != '[' )
      {
        aAUFNode_t* new_AUF = a_AUFNodeCreation();
        widget_count++;
        handle_widget_definition( new_AUF, string );

        if ( a_AUFAddNode( root, new_AUF ) < 0 )
        {
          printf( "Failed to add %s to root\n", new_AUF->string );
        }

        if ( strcmp( new_AUF->string, "WT_CONTAINER" ) == 0 )
        {
          if ( g_container != NULL )
          {
            g_container = NULL;
            g_temp_container = NULL;
          }
        }

        int list_offset = ParserWidgetToNode( new_AUF, line, nl_count, i+1 );
        i = list_offset - 1;
      }
    }
  }

  return 0;
}

static int ParserWidgetToNode( aAUFNode_t* node, char** line, int nl_count, int idx )
{
  int count = 0;
  for ( int i = idx; i < nl_count; i++ )
  {
    if ( line[i] != NULL && count < MAX_WIDGET_COUNT )
    {
      char* string = line[i];
      int str_len  = strlen( string );

      switch ( string[0] )
      {
      
      case '[':
      if ( string[1] == '[' )
      {
      aAUFNode_t* child = a_AUFNodeCreation();
      widget_count++;
      handle_widget_definition( child, string );

      if ( g_container == NULL )
      {
      aAUFNode_t* container = a_AUFNodeCreation();
      container->string = a_STR_NDUP( "container", MAX_NAME_LENGTH );
      widget_count++;
      a_AUFNodeAddChild( node, container );
      g_container = container;
      g_temp_container = container;
      }
      
      else
      {
      while ( g_container->next != NULL )
      {
        g_container = g_container->next;
      }
      }
      
      g_temp_container->value_int++;
      a_AUFNodeAddChild( g_container, child );

      int next_i = ParserWidgetToNode( child, line, nl_count, i+1 );
      i = next_i - 1;
      }

      return i;
      
      case '(':
      handle_parenthesis( node, string, str_len );
      widget_count++;
      continue;

      default:
      handle_char( node, string, str_len );
      widget_count++;
      continue;
      }
    }

    count = i;
  }

  if ( widget_count >= MAX_WIDGET_COUNT )
  {
    LOG( "CRASH: Do you really have 256 widgets? Or are you in a infinite loop because did you forget to add a newline at the end of the file?" );
    printf("widget_count: %d\n", widget_count);
    exit(1);
  }

  return count;
}

static int handle_parenthesis( aAUFNode_t* root, char* string, int str_len )
{
  if ( root == NULL || string == NULL || str_len == 0 )
  {
    printf( "Handle parenthesis ran into a problem\
             root/string/str_len is NULL/0: %s, %d\n", __FILE__, __LINE__ );
    return 1;
  }

  aAUFNode_t* x_AUF = a_AUFNodeCreation();
  aAUFNode_t* y_AUF = a_AUFNodeCreation();
  x_AUF->string = a_ParseString( ',', string+1, str_len );
  char* str_y_start  = strchr( string, ',' );
  size_t str_y_len = strlen( str_y_start );
  y_AUF->string = a_ParseString( ')', str_y_start+1, str_y_len );
  
  char* x_value_start = strchr( string, ':' );
  size_t str_x_value_len = strlen( x_value_start );
  char* x_value = a_ParseString( ',', x_value_start+2, str_x_value_len );
  char* y_value_start = strchr( x_value_start, ',' );
  size_t str_y_value_len = strlen( y_value_start );
  char* y_value = a_ParseString( ')', y_value_start+1, str_y_value_len );

  if ( strchr( x_value, '.' ) || strchr( y_value, '.' ) )
  {
    x_AUF->value_double = atof( x_value );
    y_AUF->value_double = atof( y_value );
  }
  
  else
  {
    x_AUF->value_int = atoi( x_value );
    y_AUF->value_int = atoi( y_value );
  }

  if ( a_AUFNodeAddChild( root, x_AUF ) < 0 )
  {
    printf( "Failed to add %s to root\n", x_AUF->string );
    free(x_AUF);
    free(y_AUF);
    return 1;
  }
  
  if ( a_AUFNodeAddChild( root, y_AUF ) < 0 )
  {
    printf( "Failed to add %s to root\n", y_AUF->string );
    free(x_AUF);
    free(y_AUF);
    return 1;
  }
  
  return 0;
}

static int handle_char( aAUFNode_t* root, char* string, int str_len )
{
  char* str_end = strchr( string, ':' );
  size_t str_end_len = strlen( str_end );
  aAUFNode_t* new_AUF = a_AUFNodeCreation();
  char* num_value = NULL;
  int count = 0;

  new_AUF->string = a_ParseString( ':', string, str_len );
  switch ( str_end[1] )
  {
    case '"':
      new_AUF->value_string = a_ParseString( '"', str_end+2, str_end_len-1 );
  
      if ( a_AUFNodeAddChild( root, new_AUF ) < 0 )
      {
        printf( "Failed to add %s to root\n", new_AUF->string );
        return 1;
      }

      break;

    case '[':
      if ( str_end[2] == '"' )
      {
        count = 0;
        for ( size_t i = 3; i <= str_end_len; i++ )
        {
          char* str_value = a_ParseString( '"', str_end+i, str_end_len );
          if ( str_value != NULL )
          {
            size_t str_len = strlen( str_value );
            str_value[str_len] = '\0';
            i += str_len;
            
            if ( strchr( str_value, ',') ) continue;

            aAUFNode_t* new_num = a_AUFNodeCreation();
            
            new_num->string = malloc( sizeof( char ) * MAX_LINE_LENGTH );
            if ( new_num->string == NULL )
            {
              printf("Failed to allocate memory for new_num->string: %s, %d\n", __FILE__, __LINE__ );
              return 1;
            }
            
            sprintf( new_num->string, "%d", count );
           
            new_num->value_string = str_value;

            a_AUFNodeAddChild( new_AUF, new_num );
            count++;

          }

        }
        new_AUF->value_int = count;
        a_AUFNodeAddChild( root, new_AUF );
      }
  
      else
      {
        count = 0;
        for ( size_t i = 2; i <= str_end_len; i++ )
        {
          char* num_value = a_ParseStringDoubleDelim( ',', ']', str_end+i, str_end_len );
          if ( num_value != NULL )
          {
            size_t num_len = strlen( num_value );
            num_value[num_len] = '\0';
            i += num_len;
            
            aAUFNode_t* new_num = a_AUFNodeCreation();
            
            new_num->string = malloc( sizeof( char ) * MAX_LINE_LENGTH );
            if ( new_num->string == NULL )
            {
              printf("Failed to allocate memory for new_num->string: %s, %d\n", __FILE__, __LINE__ );
              return 1;
            }
            
            sprintf( new_num->string, "%d", count );
            
            if ( strchr( num_value, '.' ) )
            {
              new_num->value_double = atof( num_value );
            }

            else
            {
              new_num->value_int = atoi( num_value );
            }

            a_AUFNodeAddChild( new_AUF, new_num );
            count++;

          }

        }
        new_AUF->value_int = count;
        a_AUFNodeAddChild( root, new_AUF );
      }
      break;

    default:
      num_value = strchr( string, ':' );

      if ( strchr( num_value+1, '.' ) )
      {
        new_AUF->value_double = atof( num_value+1 );
      }

      else
      {
        new_AUF->value_int = atoi( num_value+1 );
      }
      
      if ( a_AUFNodeAddChild( root, new_AUF ) < 0 )
      {
        printf( "Failed to add %s to root\n", new_AUF->string );
        return 1;
      }

      break;
  }

  return 0;
}

static int GetType( char* type )
{
  if ( strcmp( type, "WT_BUTTON" ) == 0 )
  {
    return WT_BUTTON;
  }

  if ( strcmp( type, "WT_SELECT" ) == 0 )
  {
    return WT_SELECT;
  }

  if ( strcmp( type, "WT_SLIDER" ) == 0 )
  {
    return WT_SLIDER;
  }

  if ( strcmp( type, "WT_INPUT" ) == 0 )
  {
    return WT_INPUT;
  }

  if ( strcmp( type, "WT_CONTROL" ) == 0 )
  {
    return WT_CONTROL;
  }
  
  if ( strcmp( type, "WT_CONTAINER" ) == 0 )
  {
    return WT_CONTAINER;
  }

  if ( strcmp( type, "WT_OUTPUT" ) == 0 )
  {
    return WT_OUTPUT;
  }

  printf( "unknown widget type: '%s' | %s, %d\n", type, __FILE__, __LINE__ );

  return WT_UNKNOWN;
}

static void handle_widget_definition( aAUFNode_t* node, const char* string )
{
  const char* start = strstr( string, "WT_" );
  if ( !start ) return;

  const char* dot = strchr( start, '.' );
  if ( !dot ) return;

  const char* end = strchr( dot, ']' );
  if ( !end ) return;

  node->string = a_STR_NDUP( start, dot - start );
  node->value_string = a_STR_NDUP( dot + 1, ( end - ( dot + 1 ) ) );

  node->type = GetType( node->string );

}

int a_AUFSaveWidgets( const char* filename )
{

  return 0;
}

int a_FreeLine( char** line, const int nl_count )
{
  for ( int j = 0; j < nl_count; j++)
  {
    free( line[j] );
  }

  free( line );

  return 0;
}

void a_PrintAUFTree( aAUFNode_t* node, int depth )
{
  while ( node )
  {
    for ( int i = 0; i < depth; i++ ) printf(" ");
    if ( node->string )
    {
      printf("Widget [%s.%s]\n", node->string, node->value_string );
    }
    
    if (node->string && strcmp(node->string, "container") == 0)
    {
      printf( "--- Child Widgets Container: %s ---\n", node->child->string );
    }
    
    if ( node->child )
    {
      a_PrintAUFTree( node->child, depth+1 );
    }

    node = node->next;
  }
}

