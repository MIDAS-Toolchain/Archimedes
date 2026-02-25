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
static aMusic_t bgm;
static int bgm_loaded = 0;

static aSoundEffect_t beholder_windup_sound;
static int beholder_windup_loaded = 0;
static aSoundEffect_t beholder_beam_sound;
static int beholder_beam_loaded = 0;
static aSoundEffect_t beholder_shield_hit_sound;
static int beholder_shield_hit_loaded = 0;
static aSoundEffect_t beholder_shield_break_sound;
static int beholder_shield_break_loaded = 0;
static aSoundEffect_t beholder_health_hit_sound;
static int beholder_health_hit_loaded = 0;
static aSoundEffect_t beholder_death_sound;
static int beholder_death_loaded = 0;
#define AUDIO_CHANNEL_BEHOLDER_BEAM 5

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

  if (a_AudioLoadMusic("resources/music/calm_bgm.wav", &bgm) == 0) {
    bgm_loaded = 1;
  }

  if (a_AudioLoadSound("resources/soundEffects/beholderwindup.wav", &beholder_windup_sound) == 0) {
    beholder_windup_loaded = 1;
  }
  if (a_AudioLoadSound("resources/soundEffects/beholderbeam.wav", &beholder_beam_sound) == 0) {
    beholder_beam_loaded = 1;
  }
  if (a_AudioLoadSound("resources/soundEffects/beholdershieldhit.wav", &beholder_shield_hit_sound) == 0) {
    beholder_shield_hit_loaded = 1;
  }
  if (a_AudioLoadSound("resources/soundEffects/beholdershieldbreak.wav", &beholder_shield_break_sound) == 0) {
    beholder_shield_break_loaded = 1;
  }
  if (a_AudioLoadSound("resources/soundEffects/beholderhealthhit.wav", &beholder_health_hit_sound) == 0) {
    beholder_health_hit_loaded = 1;
  }
  if (a_AudioLoadSound("resources/soundEffects/beholderdeath.wav", &beholder_death_sound) == 0) {
    beholder_death_loaded = 1;
  }
}

void game_audio_play_hit(void)
{
  if (!hit_sounds_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_ENEMY,
    .volume = 33,
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
    .channel = AUDIO_CHANNEL_AUTO,
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
    .channel = AUDIO_CHANNEL_AUTO,
    .volume = 48,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&fire_hit_sound, &opts);
}

void game_audio_start_music(void)
{
  if (!bgm_loaded) return;
  a_AudioPlayMusic(&bgm, -1, 0);
  a_AudioSetMusicVolume(app.audio.music_volume);
}

void game_audio_restart_music(void)
{
  if (!bgm_loaded) return;
  a_AudioPlayMusic(&bgm, -1, 0);
  a_AudioSetMusicVolume(app.audio.music_volume);
}

int game_audio_play_beholder_windup(void)
{
  if (!beholder_windup_loaded) return -1;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_AUTO,
    .volume = 80,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  return a_AudioPlaySound(&beholder_windup_sound, &opts);
}

void game_audio_stop_beholder_windup(int channel)
{
  if (channel >= 0) a_AudioHaltChannel(channel);
}

void game_audio_play_beholder_beam(void)
{
  if (!beholder_beam_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_BEHOLDER_BEAM,
    .volume = 72,
    .loops = 0, .fade_ms = 0, .interrupt = 1
  };
  a_AudioPlaySound(&beholder_beam_sound, &opts);
}

void game_audio_stop_beholder_beam(void)
{
  a_AudioHaltChannel(AUDIO_CHANNEL_BEHOLDER_BEAM);
}

void game_audio_play_beholder_shield_hit(void)
{
  if (!beholder_shield_hit_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_ENEMY,
    .volume = 40,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&beholder_shield_hit_sound, &opts);
}

void game_audio_play_beholder_shield_break(void)
{
  if (!beholder_shield_break_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_AUTO,
    .volume = 48,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&beholder_shield_break_sound, &opts);
}

void game_audio_play_beholder_health_hit(void)
{
  if (!beholder_health_hit_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_ENEMY,
    .volume = 50,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&beholder_health_hit_sound, &opts);
}

void game_audio_play_beholder_death(void)
{
  if (!beholder_death_loaded) return;
  aAudioOptions_t opts = {
    .channel = AUDIO_CHANNEL_AUTO,
    .volume = 80,
    .loops = 0, .fade_ms = 0, .interrupt = 0
  };
  a_AudioPlaySound(&beholder_death_sound, &opts);
}
