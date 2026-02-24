/*
 * Audio Test Header
 */

#ifndef AUDIO_TEST_H
#define AUDIO_TEST_H

/**
 * @brief Initialize audio test system
 *
 * Loads background music and sound effects, starts music playback
 */
void audio_test_init(void);

/**
 * @brief Update audio test (handle keyboard input)
 *
 * Call this in your game logic loop
 */
void audio_test_update(void);

/**
 * @brief Draw audio test UI
 *
 * Displays controls and current audio status
 */
void audio_test_draw(void);

/**
 * @brief Cleanup audio test resources
 *
 * Frees all loaded audio assets
 */
void audio_test_cleanup(void);

#endif // AUDIO_TEST_H
