#include <stdio.h>
#include <stdlib.h>
#include "Archimedes.h"
#include "game_audio.h"

#define HIT_SOUND_COUNT 5

static aSoundEffect_t hit_sounds[HIT_SOUND_COUNT];
static int hit_sounds_loaded = 0;
static aSoundEffect_t die_sound;
static int die_loaded = 0;
static aSoundEffect_t fire_hit_sound;
static int fire_hit_loaded = 0;

#define COIN_SOUND_COUNT 10
static aSoundEffect_t coin_sounds[COIN_SOUND_COUNT];
static int coin_sounds_loaded = 0;
static int coin_last_played = -1;
static aSoundEffect_t levelup_sound;
static int levelup_loaded = 0;

void game_audio_init(void)
{
  if (a_AudioLoadSound("resources/soundEffects/die1.wav", &die_sound) == 0) {
    printf("Loaded die1.wav\n");
    die_loaded = 1;
  } else {
    printf("Failed to load die1.wav\n");
  }

  const char* hit_files[HIT_SOUND_COUNT] = {
    "resources/soundEffects/hit1.wav",
    "resources/soundEffects/hit2.wav",
    "resources/soundEffects/hit3.wav",
    "resources/soundEffects/hit4.wav",
    "resources/soundEffects/hit5.wav"
  };

  hit_sounds_loaded = 1;
  for (int i = 0; i < HIT_SOUND_COUNT; i++) {
    if (a_AudioLoadSound(hit_files[i], &hit_sounds[i]) < 0) {
      printf("Failed to load %s\n", hit_files[i]);
      hit_sounds_loaded = 0;
    }
  }
  if (hit_sounds_loaded) {
    printf("Loaded %d hit sounds\n", HIT_SOUND_COUNT);
  }

  if (a_AudioLoadSound("resources/soundEffects/spell_fire_01.wav", &fire_hit_sound) == 0) {
    fire_hit_loaded = 1;
  }

  const char* coin_files[COIN_SOUND_COUNT] = {
    "resources/soundEffects/coin1.wav",
    "resources/soundEffects/coin2.wav",
    "resources/soundEffects/coin3.wav",
    "resources/soundEffects/coin4.wav",
    "resources/soundEffects/coin5.wav",
    "resources/soundEffects/coin6.wav",
    "resources/soundEffects/coin7.wav",
    "resources/soundEffects/coin8.wav",
    "resources/soundEffects/coin9.wav",
    "resources/soundEffects/coin10.wav"
  };
  coin_sounds_loaded = 1;
  for (int i = 0; i < COIN_SOUND_COUNT; i++) {
    if (a_AudioLoadSound(coin_files[i], &coin_sounds[i]) < 0) {
      printf("Failed to load %s\n", coin_files[i]);
      coin_sounds_loaded = 0;
    }
  }

  if (a_AudioLoadSound("resources/soundEffects/levelup.wav", &levelup_sound) == 0) {
    levelup_loaded = 1;
  }
}

void game_audio_play_hit(void)
{
  if (!hit_sounds_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_ENEMY,
    .volume = 64,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&hit_sounds[rand() % HIT_SOUND_COUNT], &opts);
}

void game_audio_play_die(void)
{
  if (!die_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_ENEMY,
    .volume = 96,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&die_sound, &opts);
}

void game_audio_play_coin(void)
{
  if (!coin_sounds_loaded) return;
  // Pick random index, never repeat the last one
  int idx = rand() % (COIN_SOUND_COUNT - 1);
  if (idx >= coin_last_played) idx++;
  coin_last_played = idx;

  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_PLAYER,
    .volume = 48,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&coin_sounds[idx], &opts);
}

void game_audio_play_levelup(void)
{
  if (!levelup_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_PLAYER,
    .volume = 96,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&levelup_sound, &opts);
}

void game_audio_play_fire_hit(void)
{
  if (!fire_hit_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_PLAYER,
    .volume = 48,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&fire_hit_sound, &opts);
}
