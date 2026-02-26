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
void game_audio_stop_all_beholder(void);

void game_audio_play_snake_die(void);
void game_audio_play_snake_gethit(void);
void game_audio_play_snake_losetail(void);

void game_audio_play_mimic_hit(void);
void game_audio_play_mimic_steal(void);
void game_audio_play_mimic_death(void);

// Weapon attack sounds (used by mimic weapon AIs)
void game_audio_play_spin_attack(void);
void game_audio_play_chain_attack(void);
void game_audio_play_bomb_throw(void);
void game_audio_play_bomb_explode(void);

#endif /* GAME_AUDIO_H */
