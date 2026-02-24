/*
 * Audio Test - Demonstrates Archimedes SDL_mixer audio system
 *
 * This file showcases:
 * - Background music playback with looping
 * - Sound effect playback
 * - Channel-based audio management
 * - Volume control
 */

#include <stdio.h>
#include "Archimedes.h"
#include "audio_test.h"

// Global audio assets
static aMusic_t g_bgm;
static aSoundEffect_t g_laser_sound;
static aSoundEffect_t g_coin_sound;
static aSoundEffect_t g_explosion_sound;

static int audio_initialized = 0;

void audio_test_init(void)
{
  // Load background music (calm BGM)
  if (a_AudioLoadMusic("resources/music/calm_bgm.wav", &g_bgm) < 0) {
    printf("Failed to load calm_bgm.wav\n");
  } else {
    printf("Loaded calm_bgm.wav\n");
  }

  // Load sound effects
  if (a_AudioLoadSound("resources/soundEffects/laser1.wav", &g_laser_sound) < 0) {
    printf("Failed to load laser1.wav\n");
  } else {
    printf("Loaded laser1.wav\n");
  }

  if (a_AudioLoadSound("resources/soundEffects/coin.wav", &g_coin_sound) < 0) {
    printf("Failed to load coin.wav\n");
  } else {
    printf("Loaded coin.wav\n");
  }

  if (a_AudioLoadSound("resources/soundEffects/bang_02.wav", &g_explosion_sound) < 0) {
    printf("Failed to load bang_02.wav\n");
  } else {
    printf("Loaded bang_02.wav\n");
  }

  // Reserve first 4 channels for specific purposes
  a_AudioReserveChannels(4);

  // Start background music (loop forever, no fade)
  if (a_AudioPlayMusic(&g_bgm, -1, 0) < 0) {
    printf("Failed to play background music\n");
  } else {
    printf("Playing calm BGM (looping)\n");
  }

  // Set music volume to 75% (96 out of 128)
  a_AudioSetMusicVolume(96);

  audio_initialized = 1;
}

void audio_test_update(void)
{
  if (!audio_initialized) {
    return;
  }

  // Test controls (keyboard shortcuts)

  // P - Toggle music pause/resume
  static int p_pressed = 0;
  if (app.keyboard[SDL_SCANCODE_P] && !p_pressed) {
    p_pressed = 1;
    if (Mix_PausedMusic()) {
      a_AudioResumeMusic();
      printf("Music resumed\n");
    } else {
      a_AudioPauseMusic();
      printf("Music paused\n");
    }
  }
  if (!app.keyboard[SDL_SCANCODE_P]) {
    p_pressed = 0;
  }

  // L - Play laser sound (auto-channel)
  static int l_pressed = 0;
  if (app.keyboard[SDL_SCANCODE_L] && !l_pressed) {
    l_pressed = 1;
    a_AudioPlaySound(&g_laser_sound, NULL);
    printf("Laser sound played\n");
  }
  if (!app.keyboard[SDL_SCANCODE_L]) {
    l_pressed = 0;
  }

  // C - Play coin sound on dedicated channel (interrupt previous)
  static int c_pressed = 0;
  if (app.keyboard[SDL_SCANCODE_C] && !c_pressed) {
    c_pressed = 1;
    aAudioOptions_t opts = {
      .channel = AUDIO_CHANNEL_UI_CLICK,
      .volume = -1,
      .loops = 0,
      .fade_ms = 0,
      .interrupt = 1  // Interrupt previous coin sound
    };
    a_AudioPlaySound(&g_coin_sound, &opts);
    printf("Coin sound played (interrupt mode)\n");
  }
  if (!app.keyboard[SDL_SCANCODE_C]) {
    c_pressed = 0;
  }

  // E - Play explosion sound
  static int e_pressed = 0;
  if (app.keyboard[SDL_SCANCODE_E] && !e_pressed) {
    e_pressed = 1;
    a_AudioPlaySound(&g_explosion_sound, NULL);
    printf("Explosion sound played\n");
  }
  if (!app.keyboard[SDL_SCANCODE_E]) {
    e_pressed = 0;
  }

  // UP/DOWN - Adjust music volume
  static int up_pressed = 0;
  static int down_pressed = 0;

  if (app.keyboard[SDL_SCANCODE_UP] && !up_pressed) {
    up_pressed = 1;
    app.audio.music_volume = (app.audio.music_volume + 10 > 128) ? 128 : app.audio.music_volume + 10;
    a_AudioSetMusicVolume(app.audio.music_volume);
    printf("Music volume: %d/128\n", app.audio.music_volume);
  }
  if (!app.keyboard[SDL_SCANCODE_UP]) {
    up_pressed = 0;
  }

  if (app.keyboard[SDL_SCANCODE_DOWN] && !down_pressed) {
    down_pressed = 1;
    app.audio.music_volume = (app.audio.music_volume - 10 < 0) ? 0 : app.audio.music_volume - 10;
    a_AudioSetMusicVolume(app.audio.music_volume);
    printf("Music volume: %d/128\n", app.audio.music_volume);
  }
  if (!app.keyboard[SDL_SCANCODE_DOWN]) {
    down_pressed = 0;
  }

  // S - Stop music
  static int s_pressed = 0;
  if (app.keyboard[SDL_SCANCODE_S] && !s_pressed) {
    s_pressed = 1;
    a_AudioStopMusic(0);
    printf("Music stopped\n");
  }
  if (!app.keyboard[SDL_SCANCODE_S]) {
    s_pressed = 0;
  }

  // R - Restart music
  static int r_pressed = 0;
  if (app.keyboard[SDL_SCANCODE_R] && !r_pressed) {
    r_pressed = 1;
    a_AudioPlayMusic(&g_bgm, -1, 0);
    printf("Music restarted (looping)\n");
  }
  if (!app.keyboard[SDL_SCANCODE_R]) {
    r_pressed = 0;
  }
}

void audio_test_draw(void)
{
  if (!audio_initialized) {
    return;
  }

  // Draw controls help text
  aTextStyle_t text_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .bg = {0, 0, 0, 0},
    .align = TEXT_ALIGN_LEFT,
    .wrap_width = 0,
    .scale = 0.6f,
    .padding = 0
  };

  int y = 20;
  int line_height = 30;

  a_DrawText("AUDIO TEST CONTROLS:", 20, y, text_style);
  y += line_height;

  a_DrawText("P - Pause/Resume Music", 40, y, text_style);
  y += line_height;

  a_DrawText("L - Play Laser Sound", 40, y, text_style);
  y += line_height;

  a_DrawText("C - Play Coin Sound (interrupt mode)", 40, y, text_style);
  y += line_height;

  a_DrawText("E - Play Explosion Sound", 40, y, text_style);
  y += line_height;

  a_DrawText("UP/DOWN - Adjust Music Volume", 40, y, text_style);
  y += line_height;

  a_DrawText("S - Stop Music", 40, y, text_style);
  y += line_height;

  a_DrawText("R - Restart Music", 40, y, text_style);
  y += line_height;

  a_DrawText("ESC - Quit", 40, y, text_style);
  y += line_height * 2;

  // Display current music volume
  char volume_text[64];
  snprintf(volume_text, sizeof(volume_text), "Music Volume: %d/128", app.audio.music_volume);
  a_DrawText(volume_text, 20, y, text_style);
  y += line_height;

  // Display music status
  const char* status;
  if (Mix_PausedMusic()) {
    status = "PAUSED";
  } else if (a_AudioIsMusicPlaying()) {
    status = "PLAYING";
  } else {
    status = "STOPPED";
  }
  char status_text[64];
  snprintf(status_text, sizeof(status_text), "Music Status: %s", status);
  a_DrawText(status_text, 20, y, text_style);
}

void audio_test_cleanup(void)
{
  if (!audio_initialized) {
    return;
  }

  // Free all audio resources
  a_AudioFreeSound(&g_laser_sound);
  a_AudioFreeSound(&g_coin_sound);
  a_AudioFreeSound(&g_explosion_sound);
  a_AudioFreeMusic(&g_bgm);

  printf("Audio test cleanup complete\n");
  audio_initialized = 0;
}
