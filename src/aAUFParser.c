/*
 * aAUFParser.c:
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 ************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "Archimedes.h"

static int ParserLineToRoot( aAUF_t* root, char** line, int nl_count );
static int ParserWidgetToNode( aAUFNode_t* node, char** line, int nl_count, int idx );
static int handle_parenthesis( aAUFNode_t* root, char* string, int str_len );
static int handle_char( aAUFNode_t* root, char* string, int str_len );
static int GetType( char* name );

static void handle_widget_definition( aAUFNode_t* node, const char* string );

/* ── calc() expression evaluator ── */

static double calc_expr( const char* str, int* pos );

/* THIS context for deferred resolution */
static int calc_has_this = 0;
static int calc_this_available = 0;
static aRectf_t calc_this_rect;

/* error context — updated as we parse each line */
static const char* g_auf_filename = NULL;
static int g_auf_line = 0;

static void auf_error( const char* fmt, ... )
{
  va_list args;
  fprintf( stderr, "%s:%d\n  ", g_auf_filename, g_auf_line );
  va_start( args, fmt );
  vfprintf( stderr, fmt, args );
  va_end( args );
  fprintf( stderr, "\n" );
  exit( 1 );
}

static void calc_skip_ws( const char* str, int* pos )
{
  while ( str[*pos] && isspace( (unsigned char)str[*pos] ) ) (*pos)++;
}

static int calc_match( const char* str, const char* keyword, int len )
{
  for ( int i = 0; i < len; i++ )
  {
    if ( tolower( (unsigned char)str[i] ) != tolower( (unsigned char)keyword[i] ) )
      return 0;
  }
  return 1;
}


static double calc_factor( const char* str, int* pos )
{
  calc_skip_ws( str, pos );

  if ( str[*pos] == '(' )
  {
    (*pos)++;
    double val = calc_expr( str, pos );
    calc_skip_ws( str, pos );
    if ( str[*pos] == ')' ) (*pos)++;
    return val;
  }

  if ( isalpha( (unsigned char)str[*pos] ) )
  {
    /* SCREEN.w / SCREEN.h */
    if ( calc_match( str + *pos, "SCREEN.w", 8 ) && !isalnum( (unsigned char)str[*pos + 8] ) )
    {
      *pos += 8;
      return (double)SCREEN_WIDTH;
    }
    if ( calc_match( str + *pos, "SCREEN.h", 8 ) && !isalnum( (unsigned char)str[*pos + 8] ) )
    {
      *pos += 8;
      return (double)SCREEN_HEIGHT;
    }
    if ( calc_match( str + *pos, "SCREEN.", 7 ) )
    {
      auf_error( "AUF calc error: unknown SCREEN property '%c'\n"
                "  valid: SCREEN.w, SCREEN.h", str[*pos + 7] );
    }

    /* THIS.w / THIS.h / THIS.x / THIS.y */
    if ( calc_match( str + *pos, "THIS.w", 6 ) && !isalnum( (unsigned char)str[*pos + 6] ) )
    {
      *pos += 6;
      if ( calc_this_available ) return calc_this_rect.w;
      calc_has_this = 1;
      return 0;
    }
    if ( calc_match( str + *pos, "THIS.h", 6 ) && !isalnum( (unsigned char)str[*pos + 6] ) )
    {
      *pos += 6;
      if ( calc_this_available ) return calc_this_rect.h;
      calc_has_this = 1;
      return 0;
    }
    if ( calc_match( str + *pos, "THIS.x", 6 ) && !isalnum( (unsigned char)str[*pos + 6] ) )
    {
      *pos += 6;
      if ( calc_this_available ) return calc_this_rect.x;
      calc_has_this = 1;
      return 0;
    }
    if ( calc_match( str + *pos, "THIS.y", 6 ) && !isalnum( (unsigned char)str[*pos + 6] ) )
    {
      *pos += 6;
      if ( calc_this_available ) return calc_this_rect.y;
      calc_has_this = 1;
      return 0;
    }

    /* widget_name.property reference */
    {
      int start = *pos;
      while ( str[*pos] && ( isalnum( (unsigned char)str[*pos] ) || str[*pos] == '_' ) )
        (*pos)++;

      /* expect .property after the name */
      if ( str[*pos] == '.' )
      {
        (*pos)++;
        int prop_start = *pos;
        while ( str[*pos] && ( isalnum( (unsigned char)str[*pos] ) || str[*pos] == '_'
                || ( str[*pos] == '-' && !isdigit( (unsigned char)str[*pos + 1] ) ) ) )
          (*pos)++;

        /* extract widget name */
        int name_len = prop_start - 1 - start;
        char widget_buf[256];
        if ( name_len >= 256 ) name_len = 255;
        memcpy( widget_buf, str + start, name_len );
        widget_buf[name_len] = '\0';

        /* extract property name */
        int prop_len = *pos - prop_start;
        char prop_buf[256];
        if ( prop_len >= 256 ) prop_len = 255;
        memcpy( prop_buf, str + prop_start, prop_len );
        prop_buf[prop_len] = '\0';

        /* During initial parse, widgets don't exist yet — defer */
        if ( !calc_this_available )
        {
          calc_has_this = 1;
          return 0;
        }

        aWidget_t* ref = a_GetWidget( widget_buf );
        if ( ref == NULL )
        {
          auf_error( "AUF calc error: widget '%s' not found (must be defined before this widget)", widget_buf );
        }

        if ( strcmp( prop_buf, "w" ) == 0 )              return ref->rect.w;
        if ( strcmp( prop_buf, "h" ) == 0 )              return ref->rect.h;
        if ( strcmp( prop_buf, "x" ) == 0 )              return ref->rect.x;
        if ( strcmp( prop_buf, "y" ) == 0 )              return ref->rect.y;
        if ( strcmp( prop_buf, "padding" ) == 0 )        return ref->padding;
        if ( strcmp( prop_buf, "padding_x" ) == 0 ||
             strcmp( prop_buf, "padding-x" ) == 0 )      return ref->padding_x;
        if ( strcmp( prop_buf, "padding_y" ) == 0 ||
             strcmp( prop_buf, "padding-y" ) == 0 )      return ref->padding_y;
        if ( strcmp( prop_buf, "padding_left" ) == 0 ||
             strcmp( prop_buf, "padding-left" ) == 0 )   return ref->padding_left;
        if ( strcmp( prop_buf, "padding_right" ) == 0 ||
             strcmp( prop_buf, "padding-right" ) == 0 )  return ref->padding_right;
        if ( strcmp( prop_buf, "padding_top" ) == 0 ||
             strcmp( prop_buf, "padding-top" ) == 0 )    return ref->padding_top;
        if ( strcmp( prop_buf, "padding_bottom" ) == 0 ||
             strcmp( prop_buf, "padding-bottom" ) == 0 ) return ref->padding_bottom;
        if ( strcmp( prop_buf, "hidden" ) == 0 )         return ref->hidden;
        if ( strcmp( prop_buf, "boxed" ) == 0 )          return ref->boxed;

        auf_error( "AUF calc error: unknown property '%s' on widget '%s'\n"
                  "  valid: w, h, x, y, padding, padding-x/y, padding-left/right/top/bottom, hidden, boxed",
                  prop_buf, widget_buf );
      }

      int len = *pos - start;
      char buf[256];
      if ( len >= 256 ) len = 255;
      memcpy( buf, str + start, len );
      buf[len] = '\0';

      auf_error( "AUF calc error: unknown constant '%s'\n"
                "  known: SCREEN.w, SCREEN.h, THIS.w/h/x/y, or widget_name.property\n"
                "  did you forget a property? e.g. %s.w, %s.padding", buf, buf, buf );
    }
  }

  char* end;
  double val = strtod( str + *pos, &end );
  if ( end == str + *pos )
  {
    auf_error( "AUF calc error: expected a number but got '%.20s'", str + *pos );
  }
  *pos = (int)( end - str );
  return val;
}

static double calc_term( const char* str, int* pos )
{
  double val = calc_factor( str, pos );
  calc_skip_ws( str, pos );

  while ( str[*pos] == '*' || str[*pos] == '/' )
  {
    char op = str[(*pos)++];
    double rhs = calc_factor( str, pos );
    if ( op == '*' ) val *= rhs;
    else
    {
      if ( rhs == 0 ) { auf_error( "AUF calc error: division by zero" ); }
      val /= rhs;
    }
    calc_skip_ws( str, pos );
  }
  return val;
}

static double calc_expr( const char* str, int* pos )
{
  calc_skip_ws( str, pos );

  double val = calc_term( str, pos );
  calc_skip_ws( str, pos );

  while ( str[*pos] == '+' || str[*pos] == '-' )
  {
    char op = str[(*pos)++];
    double rhs = calc_term( str, pos );
    if ( op == '+' ) val += rhs;
    else             val -= rhs;
    calc_skip_ws( str, pos );
  }
  return val;
}

static double auf_parse_numeric( const char* str, int* deferred )
{
  if ( deferred ) *deferred = 0;
  while ( isspace( (unsigned char)*str ) ) str++;

  if ( strncmp( str, "calc(", 5 ) == 0 )
  {
    calc_has_this = 0;
    int pos = 5;
    double val = calc_expr( str, &pos );

    if ( calc_has_this && deferred )
    {
      *deferred = 1;
    }

    return val;
  }

  return atof( str );
}

double a_CalcResolveWithThis( const char* expr, aRectf_t this_rect )
{

  calc_this_available = 1;
  calc_this_rect = this_rect;

  double val = auf_parse_numeric( expr, NULL );

  calc_this_available = 0;
  return val;
}

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
  g_auf_filename = filename;
  g_auf_line = 0;

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
      g_auf_line = i + 1;

      if ( string[0] == '[' && string[1] != '[' )
      {
        aAUFNode_t* new_AUF = a_AUFNodeCreation();
        new_AUF->line_number = i + 1;
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
  int i;
  for ( i = idx; i < nl_count; i++ )
  {
    if ( line[i] == NULL ) continue;
    if ( widget_count >= MAX_WIDGET_COUNT )
    {
      LOG( "CRASH: Do you really have 256 widgets? Or are you in a infinite loop because did you forget to add a newline at the end of the file?" );
      printf("widget_count: %d\n", widget_count);
      exit(1);
    }

    char* string = line[i];
    int str_len  = strlen( string );
    g_auf_line = i + 1;

    switch ( string[0] )
    {

    case '[':
      if ( string[1] == '[' )
      {
        aAUFNode_t* child = a_AUFNodeCreation();
        child->line_number = i + 1;
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
    {
      /* check if parentheses are balanced; if not, join next lines */
      int depth = 0;
      for ( int c = 0; c < str_len; c++ )
      {
        if ( string[c] == '(' ) depth++;
        else if ( string[c] == ')' ) depth--;
      }

      if ( depth > 0 )
      {
        int total_len = str_len;
        int end = i;

        while ( depth > 0 && end + 1 < nl_count )
        {
          end++;
          if ( line[end] == NULL ) continue;
          int ln = strlen( line[end] );
          total_len += ln;
          for ( int c = 0; c < ln; c++ )
          {
            if ( line[end][c] == '(' ) depth++;
            else if ( line[end][c] == ')' ) depth--;
          }
        }

        if ( depth > 0 )
        {
          auf_error( "AUF format error: unbalanced parentheses (never closed)\n"
                    "  starts at: %.40s", string );
        }

        char* joined = malloc( total_len + 1 );
        if ( joined == NULL )
        {
          printf( "Failed to allocate memory for multi-line join\n" );
          exit( 1 );
        }
        memcpy( joined, string, str_len );
        int pos = str_len;
        for ( int j = i + 1; j <= end; j++ )
        {
          if ( line[j] == NULL ) continue;
          int ln = strlen( line[j] );
          memcpy( joined + pos, line[j], ln );
          pos += ln;
        }
        joined[pos] = '\0';

        handle_parenthesis( node, joined, pos );
        widget_count++;
        free( joined );
        i = end;
        continue;
      }

      handle_parenthesis( node, string, str_len );
      widget_count++;
      continue;
    }

    default:
      handle_char( node, string, str_len );
      widget_count++;
      continue;
    }
  }

  return i;
}

static int handle_parenthesis( aAUFNode_t* root, char* string, int str_len )
{
  if ( root == NULL || string == NULL || str_len == 0 )
  {
    printf( "Handle parenthesis ran into a problem\
             root/string/str_len is NULL/0: %s, %d\n", __FILE__, __LINE__ );
    return 1;
  }

  /* validate format: (key,key):(value,value) */
  char* colon = strchr( string, ':' );
  if ( colon == NULL )
  {
    auf_error( "AUF format error: missing ':' separator\n"
              "  expected (key,key):(value,value)\n"
              "  got: %.60s", string );
  }

  char* first_comma = strchr( string, ',' );
  if ( first_comma == NULL || first_comma > colon )
  {
    auf_error( "AUF format error: missing ',' between keys\n"
              "  expected (key,key):(value,value)\n"
              "  got: %.60s", string );
  }

  char* value_comma = strchr( colon, ',' );
  if ( value_comma == NULL )
  {
    auf_error( "AUF format error: missing ',' between values\n"
              "  expected (key,key):(value,value)\n"
              "  got: %.60s", string );
  }

  aAUFNode_t* x_AUF = a_AUFNodeCreation();
  aAUFNode_t* y_AUF = a_AUFNodeCreation();
  x_AUF->string = a_ParseString( ',', string+1, str_len );
  char* str_y_start  = first_comma;
  size_t str_y_len = strlen( str_y_start );
  y_AUF->string = a_ParseString( ')', str_y_start+1, str_y_len );

  /* validate key pair */
  int valid_keys = ( strcmp( x_AUF->string, "x" ) == 0 && strcmp( y_AUF->string, "y" ) == 0 )
                || ( strcmp( x_AUF->string, "w" ) == 0 && strcmp( y_AUF->string, "h" ) == 0 )
                || ( strcmp( x_AUF->string, "text_x" ) == 0 && strcmp( y_AUF->string, "text_y" ) == 0 )
                || ( strcmp( x_AUF->string, "row" ) == 0 && strcmp( y_AUF->string, "col" ) == 0 )
                || ( strcmp( x_AUF->string, "grid_x" ) == 0 && strcmp( y_AUF->string, "grid_y" ) == 0 );

  if ( !valid_keys )
  {
    auf_error( "AUF format error: unknown key pair '(%s,%s)'\n"
              "  valid pairs: (x,y), (w,h), (text_x,text_y), (row,col), (grid_x,grid_y)",
              x_AUF->string, y_AUF->string );
  }

  char* x_value_start = colon;
  size_t str_x_value_len = strlen( x_value_start );
  char* x_value = a_ParseString( ',', x_value_start+2, str_x_value_len );

  char* y_value_start = value_comma;
  char* y_close = strrchr( y_value_start, ')' );
  size_t y_len = ( y_close != NULL ) ? (size_t)( y_close - ( y_value_start + 1 ) ) : strlen( y_value_start + 1 );
  char* y_value = a_STR_NDUP( y_value_start + 1, y_len );

  int x_deferred = 0, y_deferred = 0;
  double xv = auf_parse_numeric( x_value, &x_deferred );
  double yv = auf_parse_numeric( y_value, &y_deferred );

  if ( x_deferred )
  {
    x_AUF->value_string = a_STR_NDUP( x_value, strlen( x_value ) );
  }
  else
  {
    x_AUF->value_int = (int)xv;
    x_AUF->value_double = xv;
  }

  if ( y_deferred )
  {
    y_AUF->value_string = a_STR_NDUP( y_value, strlen( y_value ) );
  }
  else
  {
    y_AUF->value_int = (int)yv;
    y_AUF->value_double = yv;
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
  if ( str_end == NULL )
  {
    auf_error( "AUF format error: missing ':' separator\n"
              "  got: %.60s", string );
  }
  aAUFNode_t* new_AUF = a_AUFNodeCreation();
  int count = 0;

  new_AUF->string = a_ParseString( ':', string, str_len );

  /* skip whitespace after the colon */
  char* val = str_end + 1;
  while ( *val == ' ' || *val == '\t' ) val++;
  size_t val_len = strlen( val );

  switch ( *val )
  {
    case '"':
      new_AUF->value_string = a_ParseString( '"', val+1, val_len-1 );

      if ( a_AUFNodeAddChild( root, new_AUF ) < 0 )
      {
        printf( "Failed to add %s to root\n", new_AUF->string );
        return 1;
      }

      break;

    case '[':
      if ( val[1] == '"' )
      {
        count = 0;
        for ( size_t i = 2; i <= val_len; i++ )
        {
          char* str_value = a_ParseString( '"', val+i, val_len );
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
        for ( size_t i = 1; i <= val_len; i++ )
        {
          char* num_value = a_ParseStringDoubleDelim( ',', ']', val+i, val_len );
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

            {
              double v = auf_parse_numeric( num_value, NULL );
              if ( v != (int)v )
              {
                new_num->value_double = v;
              }
              else
              {
                new_num->value_int = (int)v;
              }
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
      {
        int deferred = 0;
        double v = auf_parse_numeric( val, &deferred );

        if ( deferred )
        {
          /* calc() with widget references — store expression for later resolution */
          new_AUF->value_string = a_STR_NDUP( val, strlen( val ) );
        }
        else
        {
          if ( v != (int)v )
          {
            new_AUF->value_double = v;
          }
          else
          {
            new_AUF->value_int = (int)v;
          }

          /* store raw value if it contains letters (e.g. widget_name.bg reference) */
          int has_alpha = 0;
          for ( const char* p = val; *p; p++ )
          {
            if ( isalpha( (unsigned char)*p ) ) { has_alpha = 1; break; }
          }
          if ( has_alpha )
          {
            new_AUF->value_string = a_STR_NDUP( val, strlen( val ) );
          }
        }
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

