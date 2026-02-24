/**
 * test_text.h - Text Rendering Test Scene
 *
 * Public interface for the text rendering test scene.
 */

#ifndef TEST_TEXT_H
#define TEST_TEXT_H

/**
 * @brief Handle logic for the text test scene
 * @param dt Delta time (unused)
 * @return 1 if should switch back to game, 0 to stay in test scene
 */
int test_text_logic( float dt );

/**
 * @brief Draw the text test scene
 * @param dt Delta time (unused)
 */
void test_text_draw( float dt );

/**
 * @brief Clean up test scene resources (FlexBox layouts)
 */
void test_text_cleanup( void );

#endif /* TEST_TEXT_H */
