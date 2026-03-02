/* 
 * @file src/aText.c
 *
 * This file defines the functions used to display text to the game window.
 *
 * Copyright (c) 2025 Jacob Kellum <jkellum819@gmail.com>
 *                    Mathew Storm <smattymat@gmail.com>
 */

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Archimedes.h"

static void initFont( const char* filename, const int font_type,
                      const int font_size );

static void initFontPNG( const char* filename, const int font_type,
                         const int glyph_width, const int glyph_height );

static int DrawTextWrapped( const char* text, const int x, const int y,
                            const aColor_t fg, const int font_type,
                            const int align, const int max_width,
                            const int draw );

static void DrawTextLine( const char* text, const int x, const int y,
                          const aColor_t fg, const int font_type,
                          const int align );

static int NextGlyph( const char* string, int* i, char* glyph_buffer );

// Input validation helpers
static int validate_text_parameters( const char* text, int font_type );
static int validate_color_parameters( const int r, const int g, const int b );
static int is_valid_utf8_sequence( const char* text, const int start_pos, int* sequence_length );

// Warning deduplication for missing glyphs
static uint8_t warned_about[FONT_MAX][MAX_GLYPHS];

static SDL_Color white_ = {255, 255, 255, 255};
//static TTF_Font* fonts[FONT_MAX];
//static SDL_Rect glyphs[FONT_MAX][MAX_GLYPHS];
//static SDL_Texture* font_textures[FONT_MAX];

//static char* characters = " !#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~¡¢£¥¦§ª«¬°±²µ¶·º»¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿŒœŸſƒΆΈΉΊΌΎΏΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩΪΫάέήίαβγδεζηθικλμνξοπρςστυφχψωϊϋόύώϕ•‼ⁿ₧€Ω←↑→↓↔↕↨∂∅∈∏∑∙√∞∟∩∫≈≡≤≥⌀⌂⌐⌠⌡⎮─│┌┐└┘├┤┬┴┼═║╒╓╔╕╖╗╘╙╚╛╜╝╞╟╠╡╢╣╤╥╦╧╨╩╪╫╬▀▄█▌▐░▒▓■▬▲►▼◄○◘◙☺☻☼♀♂♠♣♥♦♪♫♬✓";
#ifdef __EMSCRIPTEN__
// Minimal character set for web environment to avoid font atlas overflow
static char *characters = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
#else
// Full character set for native builds
static char *characters = "~`^$Ö&|_# POfileorTBFS:handWCpygt2015-6,JwsbuGNUL3.Emj@c/\"IV\\RMD8+v?x;=%!AYq()'kH[]KzQX4Z79*àéí¡Çóè·úïçüºòÉÒÍÀ°æåøÆÅØ<>öÄäßÜá¿ñÁÊûâîôÈêùœÙìëęąłćżńśźŻŚŁĆÖ";
#endif

aTextStyle_t a_default_text_style = {
  .type = FONT_CODE_PAGE_437,
  .fg = {255, 255, 255, 255},
  .bg = {0, 0, 0, 255},              // No background by default (alpha 0)
  .align = TEXT_ALIGN_LEFT,
  .wrap_width = 0,
  .scale = 1.0f,
  .padding = 0
};

void a_InitFonts( void )
{
#ifdef __EMSCRIPTEN__
  // Smaller fonts for web environment to prevent overlap and fit better
  initFont( "resources/fonts/EnterCommand.ttf", FONT_ENTER_COMMAND, 48 );
  initFont( "resources/fonts/JetBrains.ttf", FONT_LINUX, 32 );
  initFontPNG( "resources/fonts/CodePage437.png", FONT_CODE_PAGE_437, 9, 16 );
  initFontPNG( "resources/fonts/CodePage437.png", FONT_GAME, 9, 16 );
#else
  // Regular fonts for native builds
  initFont( "resources/fonts/EnterCommand.ttf", FONT_ENTER_COMMAND, 48 );
  initFont( "resources/fonts/JetBrains.ttf", FONT_LINUX, 32 );
  initFontPNG( "resources/fonts/CodePage437.png", FONT_CODE_PAGE_437, 9, 16 );
  initFontPNG( "resources/fonts/CodePage437.png", FONT_GAME, 9, 16 );
#endif

  app.font_scale = 1;
  app.font_type = FONT_CODE_PAGE_437;
}

void a_CalcTextDimensions( const char* text, int font_type, float* w, float* h )
{
  int i, n;
  SDL_Rect* g;

  // Initialize output parameters
  if ( w ) *w = 0;
  if ( h ) *h = 0;

  // Validate input parameters
  int validation_result = validate_text_parameters( text, font_type );
  if ( validation_result != ARCH_TEXT_SUCCESS ) {
    // Return zero dimensions on error
    return;
  }
  
  if ( w == NULL || h == NULL ) {
    // Cannot return dimensions if output pointers are NULL
    return;
  }

  i = 0;

  while ( ( n = NextGlyph( text, &i, NULL ) ) != 0 )
  {
    int glyph_idx = a_GetGlyphOrFallback( font_type, n );
    g = &app.glyphs[font_type][glyph_idx];
    *w += g->w * app.font_scale;
    *h = MAX( g->h * app.font_scale, *h );
  }

}

/**
 * @brief Calculates the height of wrapped text within a maximum width.
 *
 * This function computes how tall a text block will be when word-wrapped
 * to fit within the specified maximum width. Useful for determining the
 * space needed for multi-line text in UI elements.
 *
 * @param text The text string to measure (UTF-8 encoded)
 * @param font_type The font type to use for measurement
 * @param max_width Maximum width in pixels before wrapping
 * @return Total height in pixels of the wrapped text
 */
int a_GetWrappedTextHeight( char* text, int font_type, int max_width )
{
  // Validate input parameters
  int validation_result = validate_text_parameters( text, font_type );
  if ( validation_result != ARCH_TEXT_SUCCESS ) {
    // Return zero height on error
    return 0;
  }
  
  if ( max_width <= 0 ) {
    // Invalid width
    return 0;
  }
  
  return DrawTextWrapped( text, 0, 0, white, font_type, TEXT_ALIGN_LEFT, max_width, 0 );
}

SDL_Texture* a_GetTextTexture( char* text, int font_type )
{
  SDL_Surface* surface;
  
  // Validate input parameters
  int validation_result = validate_text_parameters( text, font_type );
  if ( validation_result != ARCH_TEXT_SUCCESS ) {
    return NULL;
  }
  
  if (app.fonts[font_type] == NULL) {
    return NULL;
  }
  
  surface = TTF_RenderUTF8_Blended( app.fonts[font_type], text, white_ );

  SDL_Texture* texture = SDL_CreateTextureFromSurface( app.renderer, surface );
  SDL_FreeSurface( surface );

  return texture;
}

void a_DrawText( const char* content, int x, int y, aTextStyle_t style )
{
  // Validate input parameters
  int validation_result = validate_text_parameters( content, style.type );
  if ( validation_result != ARCH_TEXT_SUCCESS ) {
    // Silently fail for now to maintain API compatibility
    return;
  }

  validation_result = validate_color_parameters( style.fg.r, style.fg.g, style.fg.b );
  if ( validation_result != ARCH_TEXT_SUCCESS ) {
    // Silently fail for now to maintain API compatibility
    return;
  }

  // Temporarily set font scale if different from default
  double old_scale = app.font_scale;
  if ( style.scale != 1.0f && style.scale > 0.0f ) {
    app.font_scale = style.scale;
  }

  // Draw background if bg.a > 0
  if ( style.bg.a > 0 )
  {
    float text_w, text_h;

    if ( style.wrap_width > 0 )
    {
      text_w = (float)style.wrap_width;
      text_h = (float)a_GetWrappedTextHeight( (char*)content, style.type, style.wrap_width );
    }
    else
    {
      a_CalcTextDimensions( content, style.type, &text_w, &text_h );
    }

    // Calculate background rect with padding
    float bg_x = (float)x - style.padding;
    float bg_y = (float)y - style.padding;
    float bg_w = text_w + ( style.padding * 2 );
    float bg_h = text_h + ( style.padding * 2 );

    // Adjust for alignment
    if ( style.align == TEXT_ALIGN_CENTER )
    {
      bg_x -= text_w / 2;
    }
    else if ( style.align == TEXT_ALIGN_RIGHT )
    {
      bg_x -= text_w;
    }

    aRectf_t bg_rect = { .x = bg_x, .y = bg_y, .w = bg_w, .h = bg_h };
    a_DrawFilledRect( bg_rect, style.bg );
  }

  if ( style.wrap_width > 0 )
  {
    DrawTextWrapped( (char*)content, x, y, style.fg, style.type,
                     style.align, style.wrap_width, 1 );
  }
  else
  {
    DrawTextLine( (char*)content, x, y, style.fg, style.type, style.align );
  }

  // Restore original scale
  app.font_scale = old_scale;
}


static void initFontPNG( const char* filename, const int font_type,
                         const int glyph_width, const int glyph_height )
{
  SDL_Surface* surface, *font_surf;
  SDL_Rect dest, rect;
  int i;

  memset( &app.glyphs[font_type], 0, sizeof( SDL_Rect ) * MAX_GLYPHS );
  memset( app.glyph_exists[font_type], 0, sizeof( app.glyph_exists[font_type] ) );
  app.fallback_glyph[font_type] = '-' - 1;  // PNG fonts use ASCII-1 indexing

  font_surf = IMG_Load( filename );
  if( font_surf == NULL )
  {
    printf( "Failed to open font surface %s, %s", filename, SDL_GetError() );
    exit(1);
  }

  surface = SDL_CreateRGBSurface( 0, FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 32, 0, 0, 0, 0xff );

  SDL_SetColorKey( surface, SDL_TRUE, SDL_MapRGBA( surface->format, 0, 0, 0, 0 ) );

  dest.x = dest.y = 0;
  rect.x = rect.y = 0;
  rect.w = dest.w = glyph_width;
  rect.h = dest.h = glyph_height;
  i = 0;

  while ( rect.x < font_surf->w )
  {
    if ( dest.x + dest.w >= FONT_TEXTURE_SIZE )
    {
      dest.x = 0;
      dest.y += dest.h + 1;
      if ( dest.y + dest.h >= FONT_TEXTURE_SIZE )
      {
        SDL_LogMessage( SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_CRITICAL, "Out of glyph space in %dx%d font atlas texture map.", FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE );
        exit(1);
      }
    }

    SDL_BlitSurface( font_surf, &rect, surface, &dest );

    app.glyphs[font_type][i] = dest;
    app.glyph_exists[font_type][i] = 1;  // Mark glyph as existing
    i++;

    dest.x += dest.w;
    rect.x += rect.w;
  }

  app.font_textures[font_type] = SDL_CreateTextureFromSurface( app.renderer, surface );
  SDL_FreeSurface( surface );
  SDL_FreeSurface( font_surf );
}

static void initFont( const char* filename, const int font_type, const int font_size )
{
  SDL_Surface* surface, *text;
  SDL_Rect dest;
  int i, n;
  char glyph_buffer[MAX_GLYPH_SIZE];

  // Clear glyph existence tracking for this font
  memset( app.glyph_exists[font_type], 0, sizeof( app.glyph_exists[font_type] ) );
  app.fallback_glyph[font_type] = '-';  // Default fallback, will be validated

  app.fonts[font_type] = TTF_OpenFont( filename, font_size );
  if( app.fonts[font_type] == NULL )
  {
    printf( "Failed to open font %s, %s", filename, TTF_GetError() );
    exit(1);
  }

  surface = SDL_CreateRGBSurface( 0, FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 32, 0, 0, 0, 0xff );

  SDL_SetColorKey( surface, SDL_TRUE, SDL_MapRGBA( surface->format, 0, 0, 0, 0 ) );

  dest.x = dest.y = 0;
  i = 0;
  while ( ( n = NextGlyph( characters, &i, glyph_buffer ) ) != 0 )
  {
    if ( n >= MAX_GLYPHS )
    {
      printf( "Glyphs '%s' index exceeds array size (%d >= %d)\n", glyph_buffer, n, MAX_GLYPHS );
      exit(1);
    }

    text = TTF_RenderUTF8_Blended( app.fonts[font_type], glyph_buffer, white_ );

    TTF_SizeText( app.fonts[font_type], glyph_buffer, &dest.w, &dest.h );

    if ( dest.x + dest.w >= FONT_TEXTURE_SIZE )
    {
      dest.x = 0;
      dest.y += dest.h + 1;

      if ( dest.y + dest.h >= FONT_TEXTURE_SIZE )
      {
        SDL_LogMessage( SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_CRITICAL, "Out of glyph space in %dx%d font atlas texture map.", FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE );
        exit(1);
      }
    }

    SDL_BlitSurface( text, NULL, surface, &dest );
    app.glyphs[font_type][n] = dest;
    app.glyph_exists[font_type][n] = 1;  // Mark glyph as existing

    SDL_FreeSurface( text );
    dest.x += dest.w;
  }

  // Validate fallback glyph exists
  if ( !app.glyph_exists[font_type]['-'] ) {
    printf( "WARNING: Fallback glyph '-' not in font atlas for font_type %d\n", font_type );
    // Try space as backup fallback
    if ( app.glyph_exists[font_type][' '] ) {
      app.fallback_glyph[font_type] = ' ';
    }
  }

  app.font_textures[font_type] = SDL_CreateTextureFromSurface( app.renderer, surface );
  SDL_FreeSurface( surface );
}

static int DrawTextWrapped( const char* text, const int x, const int y,
                            const aColor_t fg, const int font_type,
                            const int align, const int max_width,
                            const int draw )
{
  char word[MAX_WORD_LENGTH], line[MAX_LINE_LENGTH], glyph_buffer[MAX_GLYPH_SIZE];
  int i, n, word_width, line_width, len;
  int word_len, line_len, glyph_len;
  int new_x = x, new_y = y;

  i = 0;

  memset( word, 0, MAX_WORD_LENGTH );
  memset( line, 0, MAX_LINE_LENGTH );

  n = 0;

  line_width = word_width = 0;
  len = strlen( text );

  while ( ( n = NextGlyph( text, &i, glyph_buffer ) ) != 0 )
  {
    // Use fallback for missing glyphs
    int glyph_idx = a_GetGlyphOrFallback( font_type, n );
    word_width += app.glyphs[font_type][glyph_idx].w * app.font_scale;

    // If glyph doesn't exist, replace buffer with fallback character
    if ( glyph_idx != (int)n )
    {
      glyph_buffer[0] = (char)app.fallback_glyph[font_type];
      glyph_buffer[1] = '\0';
    }

    if ( n != ' ' )
    {
      // Safe string concatenation with bounds checking
      word_len = strlen( word );
      glyph_len = strlen( glyph_buffer );
      if ( word_len + glyph_len < MAX_WORD_LENGTH - 1 ) {
        strncat( word, glyph_buffer, MAX_WORD_LENGTH - word_len - 1 );
      }
      // If overflow would occur, silently truncate to prevent crash
    }

    if ( n == ' ' || i == len )
    {
      if ( line_width + word_width >= max_width )
      {
        if ( draw )
        {
          DrawTextLine( line, new_x, new_y, fg, font_type, align );
        }

        memset( line, 0, MAX_LINE_LENGTH );

        new_y += app.glyphs[font_type][' '].h * app.font_scale;
        line_width = 0;
      }
      
      else if ( line_width != 0 )
      {
        // Safe string concatenation with bounds checking
        line_len = strlen( line );
        if ( line_len + 1 < MAX_LINE_LENGTH - 1 ) {
          strncat( line, " ", MAX_LINE_LENGTH - line_len - 1 );
        }
      }

      // Safe string concatenation with bounds checking
      line_len = strlen( line );
      word_len = strlen( word );
      if ( line_len + word_len < MAX_LINE_LENGTH - 1 ) {
        strncat( line, word, MAX_LINE_LENGTH - line_len - 1 );
      }
      // If overflow would occur, silently truncate to prevent crash

      line_width += word_width;

      memset( word, 0, MAX_WORD_LENGTH );

      word_width = 0;
    }
  }

  if ( draw )
  {
    DrawTextLine( line, new_x, new_y, fg, font_type, align );
  }

  return new_y + app.glyphs[font_type][' '].h * app.font_scale;
}

static void DrawTextLine( const char* text, const int x, const int y,
                          const aColor_t fg, const int font_type, const int align )
{
  int i, n, len, c;
  float w, h;
  int new_x = x, new_y = y;
  SDL_Rect* glyph, dest;

  if ( align != TEXT_ALIGN_LEFT )
  {
    a_CalcTextDimensions( text, font_type, &w, &h );

    if ( align == TEXT_ALIGN_CENTER )
    {
      new_x -= ( w / 2 );
    }
    else if ( align == TEXT_ALIGN_RIGHT )
    {
      new_x -= w;
    }
  }

  SDL_SetTextureColorMod( app.font_textures[font_type], fg.r, fg.g, fg.b );

  i = 0;
  len = strlen( text );

  if ( font_type == FONT_GAME )
  {
    for ( int j = 0; j < len; j++ )
    {
      c = text[j] - 1;
      glyph = &app.glyphs[font_type][c];

      dest.x = new_x;
      dest.y = new_y;
      dest.w = glyph->w * app.font_scale;
      dest.h = glyph->h * app.font_scale;

      SDL_RenderCopy( app.renderer, app.font_textures[font_type], glyph, &dest );

      new_x += glyph->w * app.font_scale;

    }
  }
  
  else if ( font_type == FONT_CODE_PAGE_437 )
  {
    for ( int j = 0; j < len; j++ )
    {
      c = text[j] - 1;
      glyph = &app.glyphs[font_type][c];

      dest.x = new_x;
      dest.y = new_y;
      dest.w = glyph->w * app.font_scale;
      dest.h = glyph->h * app.font_scale;

      SDL_RenderCopy( app.renderer, app.font_textures[font_type], glyph, &dest );

      new_x += glyph->w * app.font_scale;

    }
  }
  
  else
  {
    while ( ( n = NextGlyph( text, &i, NULL ) ) != 0 )
    {
      int glyph_idx = a_GetGlyphOrFallback( font_type, n );
      glyph = &app.glyphs[font_type][glyph_idx];

      dest.x = new_x;
      dest.y = new_y;
      dest.w = glyph->w * app.font_scale;
      dest.h = glyph->h * app.font_scale;

      SDL_RenderCopy( app.renderer, app.font_textures[font_type], glyph, &dest );

      new_x += glyph->w * app.font_scale;
    }
  }
}

void a_DrawGlyph( const char* glyph, int x, int y, int w, int h,
                  aColor_t fg, aColor_t bg, int font_type )
{
  if ( !glyph || !glyph[0] ) return;

  if ( bg.a > 0 )
    a_DrawFilledRect( (aRectf_t){ x, y, w, h }, bg );

  int c = glyph[0] - 1;
  SDL_Rect* src = &app.glyphs[font_type][c];
  SDL_Rect dest = { x, y, w, h };

  SDL_SetTextureColorMod( app.font_textures[font_type], fg.r, fg.g, fg.b );
  SDL_RenderCopy( app.renderer, app.font_textures[font_type], src, &dest );
}

void a_DrawGlyph_special( int glyph_index, aRectf_t rect,
                  aColor_t fg, aColor_t bg, int font_type )
{
  if ( glyph_index > MAX_GLYPHS) return;

  if ( bg.a > 0 )
    a_DrawFilledRect( (aRectf_t){ rect.x, rect.y, rect.w, rect.h }, bg );

  SDL_Rect* src = &app.glyphs[font_type][glyph_index];
  SDL_Rect dest = { rect.x, rect.y, rect.w, rect.h };

  SDL_SetTextureColorMod( app.font_textures[font_type], fg.r, fg.g, fg.b );
  SDL_RenderCopy( app.renderer, app.font_textures[font_type], src, &dest );
}

static unsigned int decode_utf8_codepoint( const char* string, const int start_pos, const int length )
{
  unsigned int codepoint = 0;
  
  if ( length == 1 ) {
    // ASCII
    codepoint = (unsigned char)string[start_pos];
  }
  else if ( length == 2 ) {
    // 2-byte UTF-8
    codepoint  = ( (unsigned char)string[start_pos]     & 0x1f ) << 6;
    codepoint |= ( (unsigned char)string[start_pos + 1] & 0x3f );
  }
  else if ( length == 3 ) {
    // 3-byte UTF-8
    codepoint  = ( (unsigned char)string[start_pos]     & 0x0f ) << 12;
    codepoint |= ( (unsigned char)string[start_pos + 1] & 0x3f ) << 6;
    codepoint |= ( (unsigned char)string[start_pos + 2] & 0x3f );
  }
  else if ( length == 4 ) {
    // 4-byte UTF-8
    codepoint  = ( (unsigned char)string[start_pos]     & 0x07 ) << 18;
    codepoint |= ( (unsigned char)string[start_pos + 1] & 0x3f ) << 12;
    codepoint |= ( (unsigned char)string[start_pos + 2] & 0x3f ) << 6;
    codepoint |= ( (unsigned char)string[start_pos + 3] & 0x3f );
  }
  
  return codepoint;
}

static int NextGlyph( const char* string, int* i, char* glyph_buffer )
{
  int len;
  unsigned int bit;
  const char *p;
  int validation_result;

  if ( string == NULL || i == NULL ) {
    return 0;
  }

  bit = ( unsigned char ) string[*i];

  if ( bit < ' ' )
  {
    return 0;
  }

  // Validate UTF-8 sequence before processing
  validation_result = is_valid_utf8_sequence( string, *i, &len );
  if ( validation_result != ARCH_TEXT_SUCCESS ) {
    // Skip invalid UTF-8 sequences by advancing one byte
    (*i)++;
    return 0;
  }

  // Use the validated length and decode the UTF-8 codepoint
  bit = decode_utf8_codepoint( string, *i, len );

  // Check if codepoint is within bounds for our glyph array
  if ( bit >= MAX_GLYPHS ) {
    // Return fallback for unsupported characters (don't return 0 which stops parsing)
    if ( glyph_buffer != NULL ) {
      glyph_buffer[0] = '-';
      glyph_buffer[1] = '\0';
    }
    *i = *i + len;
    return '-';  // Continue parsing with fallback character
  }

  if ( glyph_buffer != NULL )
  {
    p = string + *i;

    memset( glyph_buffer, 0, MAX_GLYPH_SIZE );

    // Safe copy with bounds checking
    if ( len <= MAX_GLYPH_SIZE - 1 ) {
      memcpy( glyph_buffer, p, len );
    }
  }
  *i = *i + len;
  
  return bit;
}

// ============================================================================
// Input Validation Helper Functions
// ============================================================================

/**
 * @brief Validates text and font parameters
 * @param text The text string to validate
 * @param font_type The font type to validate
 * @return ARCH_TEXT_SUCCESS on success, error code on failure
 */
static int validate_text_parameters( const char* text, int font_type )
{
  if ( text == NULL ) {
    return ARCH_TEXT_ERROR_NULL_POINTER;
  }
  
  if ( font_type < 0 || font_type >= FONT_MAX ) {
    return ARCH_TEXT_ERROR_INVALID_FONT;
  }
  
  return ARCH_TEXT_SUCCESS;
}

static int validate_color_parameters( int r, int g, int b )
{
  if ( r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ) {
    return ARCH_TEXT_ERROR_INVALID_FONT; // Reuse for parameter validation
  }
  
  return ARCH_TEXT_SUCCESS;
}

/**
 * @brief Validates UTF-8 sequence at given position
 * @param text The text string to check
 * @param start_pos Starting position in the string
 * @param sequence_length Output parameter for sequence length
 * @return ARCH_TEXT_SUCCESS if valid, error code if invalid
 */
static int is_valid_utf8_sequence( const char* text, int start_pos, int* sequence_length )
{
  if ( text == NULL || sequence_length == NULL ) {
    return ARCH_TEXT_ERROR_NULL_POINTER;
  }
  
  unsigned char byte = (unsigned char)text[start_pos];
  
  // ASCII (0xxxxxxx)
  if ( byte < 0x80 ) {
    *sequence_length = 1;
    return ARCH_TEXT_SUCCESS;
  }
  
  // 2-byte sequence (110xxxxx 10xxxxxx)
  if ( (byte & 0xE0) == 0xC0 ) {
    *sequence_length = 2;
    if ( text[start_pos + 1] == '\0' || (text[start_pos + 1] & 0xC0) != 0x80 ) {
      return ARCH_TEXT_ERROR_INVALID_UTF8;
    }
    return ARCH_TEXT_SUCCESS;
  }
  
  // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
  if ( (byte & 0xF0) == 0xE0 ) {
    *sequence_length = 3;
    if ( text[start_pos + 1] == '\0' || (text[start_pos + 1] & 0xC0) != 0x80 ||
         text[start_pos + 2] == '\0' || (text[start_pos + 2] & 0xC0) != 0x80 ) {
      return ARCH_TEXT_ERROR_INVALID_UTF8;
    }
    return ARCH_TEXT_SUCCESS;
  }
  
  // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
  if ( (byte & 0xF8) == 0xF0 ) {
    *sequence_length = 4;
    if ( text[start_pos + 1] == '\0' || (text[start_pos + 1] & 0xC0) != 0x80 ||
         text[start_pos + 2] == '\0' || (text[start_pos + 2] & 0xC0) != 0x80 ||
         text[start_pos + 3] == '\0' || (text[start_pos + 3] & 0xC0) != 0x80 ) {
      return ARCH_TEXT_ERROR_INVALID_UTF8;
    }
    return ARCH_TEXT_SUCCESS;
  }
  
  // Invalid UTF-8 start byte
  return ARCH_TEXT_ERROR_INVALID_UTF8;
}

// ============================================================================
// Glyph Registry API
// ============================================================================

int a_GlyphExists(int font_type, unsigned int codepoint)
{
  if ( font_type < 0 || font_type >= FONT_MAX ) {
    return 0;
  }
  if ( codepoint >= MAX_GLYPHS ) {
    return 0;
  }
  return app.glyph_exists[font_type][codepoint];
}

int a_GetGlyphOrFallback(int font_type, unsigned int codepoint)
{
  if ( font_type < 0 || font_type >= FONT_MAX ) {
    return '-';  // Safety fallback
  }

  // If glyph exists, return it
  if ( codepoint < MAX_GLYPHS && app.glyph_exists[font_type][codepoint] ) {
    return codepoint;
  }

  // Glyph doesn't exist - log warning once and return fallback
  if ( codepoint < MAX_GLYPHS && !warned_about[font_type][codepoint] ) {
    warned_about[font_type][codepoint] = 1;
    SDL_LogMessage( SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN,
                    "Missing glyph U+%04X in font_type %d, using fallback",
                    codepoint, font_type );
  }

  return app.fallback_glyph[font_type];
}

aRectf_t a_GetGlyphSize( void )
{
  SDL_Rect rect = app.glyphs[app.font_type][0];
  aRectf_t rt_rect = { .x = 0, .y = 0, .w = rect.w, .h = rect.h };
  
  return rt_rect;
}

