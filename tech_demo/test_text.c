/**
 * test_text.c - Text Rendering Test Scene
 *
 * Tests for text rendering features including:
 * - Em-dash and Unicode fallback (Issue #11)
 * - Background colors and padding (Issue #12)
 * - FlexBox layout system
 */

#include <stdio.h>
#include <stdlib.h>
#include "Archimedes.h"
#include "test_text.h"

// ============================================================================
// Static state for test scene
// ============================================================================

static int test_index = 0;  // Shared index for Test 5, 6, 7 (0-11)

// Test 6: 12 alpha values
static const int test6_alphas[12] = { 0, 25, 50, 75, 100, 125, 150, 175, 200, 225, 240, 255 };

// Test 7: Random colors for 4 padding boxes (randomized on -/+ press)
static int test7_fg_idx[4] = {0, 1, 2, 3};
static int test7_bg_idx[4] = {4, 5, 6, 7};

// Test 5: 12 color combinations (using custom palette)
static const char* test5_labels[12] = {
  "Cream on Ocean", "Coral on Dark", "Lime on Forest", "Gold on Maroon",
  "Cyan on Navy", "Pink on Purple", "Peach on Brown", "Sage on Teal",
  "Rose on Wine", "Sky on Deep", "Cream on Charcoal", "Amber on Rust"
};
static const aColor_t test5_fg[12] = {
  {0xeb,0xed,0xe9,255}, {0xcf,0x57,0x3c,255}, {0xa8,0xca,0x58,255}, {0xe8,0xc1,0x70,255},
  {0x73,0xbe,0xd3,255}, {0xdf,0x84,0xa5,255}, {0xd7,0xb5,0x94,255}, {0xa8,0xb5,0xb2,255},
  {0xda,0x86,0x3e,255}, {0x4f,0x8f,0xba,255}, {0xc7,0xcf,0xcc,255}, {0xde,0x9e,0x41,255}
};
static const aColor_t test5_bg[12] = {
  {0x3c,0x5e,0x8b,255}, {0x10,0x14,0x1f,255}, {0x19,0x33,0x2d,255}, {0x60,0x2c,0x2c,255},
  {0x1e,0x1d,0x39,255}, {0x40,0x27,0x51,255}, {0x4d,0x2b,0x32,255}, {0x25,0x56,0x2e,255},
  {0x75,0x24,0x38,255}, {0x15,0x1d,0x28,255}, {0x39,0x4a,0x50,255}, {0x88,0x4b,0x2b,255}
};

// FlexBox layout for test scene
static FlexBox_t* test_left_col = NULL;
static FlexBox_t* test_right_col = NULL;
static int test_layout_initialized = 0;

// Layout constants
#define TEST_TITLE_HEIGHT 30
#define TEST_CONTENT_HEIGHT 40
#define TEST_GROUP_GAP 15
#define TEST_TITLE_CONTENT_GAP 8

// ============================================================================
// Layout initialization/cleanup
// ============================================================================

static void init_test_layout( void )
{
  if ( test_layout_initialized ) return;

  int col_width = SCREEN_WIDTH / 2 - 30;

  // Left column: Tests 1-4
  test_left_col = a_FlexBoxCreate( 10, 100, col_width, SCREEN_HEIGHT - 120 );
  a_FlexSetDirection( test_left_col, FLEX_DIR_COLUMN );
  a_FlexSetGap( test_left_col, TEST_GROUP_GAP );

  // Add 4 test groups (each group = title + content height)
  for ( int i = 0; i < 4; i++ ) {
    a_FlexAddItem( test_left_col, col_width, TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP + TEST_CONTENT_HEIGHT, NULL );
  }

  // Right column: Tests 5-10
  test_right_col = a_FlexBoxCreate( SCREEN_WIDTH / 2 + 20, 100, col_width, SCREEN_HEIGHT - 120 );
  a_FlexSetDirection( test_right_col, FLEX_DIR_COLUMN );
  a_FlexSetGap( test_right_col, TEST_GROUP_GAP );

  // Add 6 test groups
  for ( int i = 0; i < 6; i++ ) {
    a_FlexAddItem( test_right_col, col_width, TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP + TEST_CONTENT_HEIGHT, NULL );
  }

  a_FlexLayout( test_left_col );
  a_FlexLayout( test_right_col );

  test_layout_initialized = 1;
}

void test_text_cleanup( void )
{
  if ( test_left_col ) {
    a_FlexBoxDestroy( &test_left_col );
  }
  if ( test_right_col ) {
    a_FlexBoxDestroy( &test_right_col );
  }
  test_layout_initialized = 0;
}

// ============================================================================
// Logic - handles input for test scene
// ============================================================================

int test_text_logic( float dt )
{
  (void)dt;  // unused
  static int minus_pressed = 0;
  static int plus_pressed = 0;
  static int ctrl_t_pressed = 0;

  // Ctrl+T to return to game
  if ( (app.keyboard[ SDL_SCANCODE_LCTRL ] || app.keyboard[ SDL_SCANCODE_RCTRL ]) &&
       app.keyboard[ SDL_SCANCODE_T ] == 1 && !ctrl_t_pressed )
  {
    test_text_cleanup();
    ctrl_t_pressed = 1;
    app.keyboard[ SDL_SCANCODE_T ] = 0;
    return 1;  // Signal to switch back to game
  }
  if ( app.keyboard[ SDL_SCANCODE_T ] == 0 )
  {
    ctrl_t_pressed = 0;
  }

  // ESC to quit (handled globally in main.c)

  // Unified controls: - and = keys control Test 5, 6, and 7
  if ( app.keyboard[ SDL_SCANCODE_MINUS ] == 1 && !minus_pressed )
  {
    test_index--;
    if ( test_index < 0 ) test_index = 11;
    // Randomize Test 7 colors
    for ( int i = 0; i < 4; i++ ) {
      test7_fg_idx[i] = rand() % 12;
      test7_bg_idx[i] = rand() % 12;
    }
    minus_pressed = 1;
  }
  if ( app.keyboard[ SDL_SCANCODE_MINUS ] == 0 ) minus_pressed = 0;

  if ( app.keyboard[ SDL_SCANCODE_EQUALS ] == 1 && !plus_pressed )
  {
    test_index++;
    if ( test_index > 11 ) test_index = 0;
    // Randomize Test 7 colors
    for ( int i = 0; i < 4; i++ ) {
      test7_fg_idx[i] = rand() % 12;
      test7_bg_idx[i] = rand() % 12;
    }
    plus_pressed = 1;
  }
  if ( app.keyboard[ SDL_SCANCODE_EQUALS ] == 0 ) plus_pressed = 0;

  return 0;  // Stay in test scene
}

// ============================================================================
// Draw - renders the test scene
// ============================================================================

void test_text_draw( float dt )
{
  (void)dt;

  // Initialize layout if needed
  init_test_layout();

  // Get layout positions
  int col_width = SCREEN_WIDTH / 2 - 30;
  const FlexItem_t* left_items[4];
  const FlexItem_t* right_items[6];
  for ( int i = 0; i < 4; i++ ) left_items[i] = a_FlexGetItem( test_left_col, i );
  for ( int i = 0; i < 6; i++ ) right_items[i] = a_FlexGetItem( test_right_col, i );

  // Title
  aTextStyle_t title_config = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 0, 255},
    .bg = {0, 0, 0, 0},
    .align = TEXT_ALIGN_CENTER,
    .wrap_width = 0,
    .scale = 0.8f,
    .padding = 0
  };
  a_DrawText( "TEXT RENDERING TEST", SCREEN_WIDTH / 2, 20, title_config );

  // Instructions
  aTextStyle_t info_config = {
    .type = FONT_ENTER_COMMAND,
    .fg = {200, 200, 200, 255},
    .bg = {0, 0, 0, 0},
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = 0,
    .scale = 0.8f,
    .padding = 0
  };
  a_DrawText( "Ctrl+T: Back to game | ESC: Quit", 10, 70, info_config );

  // ========================================================================
  // LEFT COLUMN: Glyph/Unicode tests
  // ========================================================================
  aTextStyle_t test_config = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .bg = {0, 0, 0, 0},
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = col_width,
    .scale = 0.8f,
    .padding = 0
  };

  // Test 1
  int y1 = left_items[0]->calc_y;
  a_DrawText( "Test 1: Em-dash (should show - as fallback)", 10, y1, info_config );
  a_DrawText( "Your Aces shimmer with stolen luck\xe2\x80\x94the House's punishment backfired.", 10, y1 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, test_config );

  // Test 2
  int y2 = left_items[1]->calc_y;
  a_DrawText( "Test 2: Various Unicode chars", 10, y2, info_config );
  a_DrawText( "Curly quotes: \xe2\x80\x9cHello\xe2\x80\x9d Ellipsis: \xe2\x80\xa6 En-dash: \xe2\x80\x93", 10, y2 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, test_config );

  // Test 3
  int y3 = left_items[2]->calc_y;
  a_DrawText( "Test 3: Normal ASCII", 10, y3, info_config );
  a_DrawText( "The quick brown fox jumps over the lazy dog.", 10, y3 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, test_config );

  // Test 4
  int y4 = left_items[3]->calc_y;
  a_DrawText( "Test 4: Wrapped text", 10, y4, info_config );
  aTextStyle_t wrap_config = {
    .type = FONT_ENTER_COMMAND,
    .fg = {100, 255, 100, 255},
    .bg = {0, 0, 0, 0},
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = col_width,
    .scale = 0.8f,
    .padding = 0
  };
  a_DrawText( "This is a longer piece of text that should wrap across multiple lines. "
              "Previously, text with em-dashes would silently truncate here.", 10, y4 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, wrap_config );

  // ========================================================================
  // RIGHT COLUMN: Background/Padding tests (Issue #12)
  // ========================================================================
  int right_x = SCREEN_WIDTH / 2 + 20;

  // Test 5
  int y5 = right_items[0]->calc_y;
  char test5_header[64];
  snprintf( test5_header, sizeof(test5_header), "Test 5: [-/+] %s", test5_labels[test_index] );
  a_DrawText( test5_header, right_x, y5, info_config );
  aTextStyle_t bg_solid = {
    .type = FONT_ENTER_COMMAND,
    .fg = test5_fg[test_index],
    .bg = test5_bg[test_index],
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = 0,
    .scale = 0.8f,
    .padding = 4
  };
  a_DrawText( test5_labels[test_index], right_x, y5 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, bg_solid );

  // Test 6
  int y6 = right_items[1]->calc_y;
  char test6_label[64];
  snprintf( test6_label, sizeof(test6_label), "Test 6: [-/+] Alpha=%d", test6_alphas[test_index] );
  a_DrawText( test6_label, right_x, y6, info_config );
  aTextStyle_t bg_semi = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .bg = {0, 0, 0, (uint8_t)test6_alphas[test_index]},
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = 0,
    .scale = 0.8f,
    .padding = 8
  };
  a_DrawText( "Text with alpha bg", right_x, y6 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, bg_semi );

  // Test 7: Different padding values (random colors from palette)
  int y7 = right_items[2]->calc_y;
  a_DrawText( "Test 7: [-/+] Padding 0, 4, 8, 16", right_x, y7, info_config );
  aTextStyle_t pad0 = { .type = FONT_ENTER_COMMAND, .fg = test5_fg[test7_fg_idx[0]], .bg = test5_bg[test7_bg_idx[0]], .scale = 0.8f, .padding = 0 };
  aTextStyle_t pad4 = { .type = FONT_ENTER_COMMAND, .fg = test5_fg[test7_fg_idx[1]], .bg = test5_bg[test7_bg_idx[1]], .scale = 0.8f, .padding = 4 };
  aTextStyle_t pad8 = { .type = FONT_ENTER_COMMAND, .fg = test5_fg[test7_fg_idx[2]], .bg = test5_bg[test7_bg_idx[2]], .scale = 0.8f, .padding = 8 };
  aTextStyle_t pad16 = { .type = FONT_ENTER_COMMAND, .fg = test5_fg[test7_fg_idx[3]], .bg = test5_bg[test7_bg_idx[3]], .scale = 0.8f, .padding = 16 };
  int y7c = y7 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP;
  a_DrawText( "p=0", right_x, y7c, pad0 );
  a_DrawText( "p=4", right_x + 80, y7c, pad4 );
  a_DrawText( "p=8", right_x + 170, y7c, pad8 );
  a_DrawText( "p=16", right_x + 270, y7c, pad16 );

  // Test 8: Color combos
  int y8 = right_items[3]->calc_y;
  a_DrawText( "Test 8: Color combos", right_x, y8, info_config );
  aTextStyle_t red_white = { .type = FONT_ENTER_COMMAND, .fg = {255,255,255,255}, .bg = {200,50,50,255}, .scale = 0.8f, .padding = 6 };
  aTextStyle_t green_black = { .type = FONT_ENTER_COMMAND, .fg = {0,0,0,255}, .bg = {50,200,50,255}, .scale = 0.8f, .padding = 6 };
  aTextStyle_t yellow_dark = { .type = FONT_ENTER_COMMAND, .fg = {50,50,50,255}, .bg = {255,220,100,255}, .scale = 0.8f, .padding = 6 };
  int y8c = y8 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP;
  a_DrawText( "Error!", right_x, y8c, red_white );
  a_DrawText( "Success", right_x + 100, y8c, green_black );
  a_DrawText( "Warning", right_x + 220, y8c, yellow_dark );

  // Test 9: Wrapped with background
  int y9 = right_items[4]->calc_y;
  a_DrawText( "Test 9: Wrapped + background", right_x, y9, info_config );
  aTextStyle_t wrap_bg = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .bg = {40, 40, 80, 200},
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = col_width - 20,
    .scale = 0.8f,
    .padding = 8
  };
  a_DrawText( "This tooltip has a background and wraps nicely. Great for hints!", right_x, y9 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, wrap_bg );

  // Test 10: Centered with background
  int y10 = right_items[5]->calc_y;
  a_DrawText( "Test 10: Center-aligned bg", right_x, y10, info_config );
  aTextStyle_t center_bg = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .bg = {100, 50, 150, 255},
    .align = TEXT_ALIGN_CENTER,
    .wrap_width = 0,
    .scale = 0.8f,
    .padding = 10
  };
  a_DrawText( "CENTERED", right_x + col_width / 2, y10 + TEST_TITLE_HEIGHT + TEST_TITLE_CONTENT_GAP, center_bg );
}
