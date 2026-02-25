#ifndef GAME_AUDIO_H
#define GAME_AUDIO_H

void game_audio_init(void);
void game_audio_play_hit(void);
void game_audio_play_die(void);
void game_audio_play_coin(void);
void game_audio_play_fire_hit(void);
void game_audio_play_levelup(void);
void game_audio_start_music(void);
void game_audio_restart_music(void);

int  game_audio_play_beholder_windup(void);
void game_audio_stop_beholder_windup(int channel);
void game_audio_play_beholder_beam(void);
void game_audio_stop_beholder_beam(void);
void game_audio_play_beholder_shield_hit(void);
void game_audio_play_beholder_shield_break(void);
void game_audio_play_beholder_health_hit(void);
void game_audio_play_beholder_death(void);

#endif /* GAME_AUDIO_H */
