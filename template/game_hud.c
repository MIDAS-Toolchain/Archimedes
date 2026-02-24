#include <stdio.h>
#include <math.h>
#include "Archimedes.h"
#include "game_hud.h"
#include "game_director.h"
#include "player_actions.h"
#include "enemy.h"
#include "xp.h"
#include "stats.h"

#define MAX_ENEMIES 50

void hud_draw_game(float time_remaining)
{
  // Draw countdown timer
  int minutes = (int)(time_remaining / 60.0f);
  int seconds = (int)time_remaining % 60;
  char timer_text[32];
  snprintf( timer_text, sizeof(timer_text), "%02d:%02d", minutes, seconds );

  aTextStyle_t timer_config = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_CENTER,
    .wrap_width = 0,
    .scale = 0.8f
  };
  a_DrawText( timer_text, SCREEN_WIDTH / 2, 25, timer_config );

  // Draw score (top left)
  {
    char score_text[32];
    snprintf( score_text, sizeof(score_text), "%d", stats_get_score() );
    aTextStyle_t score_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.6f
    };
    a_DrawText( score_text, 16, 10, score_style );

    // "SCORE" label below
    aTextStyle_t score_label = {
      .type = FONT_ENTER_COMMAND,
      .fg = {160, 160, 180, 180},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.3f
    };
    a_DrawText( "SCORE", 16, 30, score_label );
  }

  // Draw enemy count and spawn timer (top right)
  int current_enemy_count = enemy_get_count();
  char enemy_count_text[32];
  snprintf( enemy_count_text, sizeof(enemy_count_text), "Enemies: %d/%d", current_enemy_count, MAX_ENEMIES );

  aTextStyle_t enemy_count_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 255},
    .align = TEXT_ALIGN_RIGHT,
    .wrap_width = 0,
    .scale = 0.6f
  };
  a_DrawText( enemy_count_text, SCREEN_WIDTH - 20, 15, enemy_count_style );

  // Draw spawn timer (pauses at 0:00 if at max capacity)
  char spawn_timer_text[32];
  if ( current_enemy_count >= director_get_max_active() )
  {
    snprintf( spawn_timer_text, sizeof(spawn_timer_text), "Next spawn: --" );
  }
  else
  {
    float time_until_spawn = director_get_spawn_interval() - director_get_spawn_timer();
    if ( time_until_spawn < 0.0f ) time_until_spawn = 0.0f;
    int spawn_seconds = (int)time_until_spawn;
    int spawn_hundredths = (int)((time_until_spawn - spawn_seconds) * 100);
    snprintf( spawn_timer_text, sizeof(spawn_timer_text), "Next spawn: %d:%02d", spawn_seconds, spawn_hundredths );
  }

  a_DrawText( spawn_timer_text, SCREEN_WIDTH - 20, 40, enemy_count_style );

  // Draw player health bar (top center)
  {
    int bar_w = 200;
    int bar_h = 12;
    int bar_x = (SCREEN_WIDTH - bar_w) / 2;
    int bar_y = 8;

    float hp_pct = (float)player_get_hp() / (float)player_get_max_hp();
    int fill_w = (int)(bar_w * hp_pct);

    // Background
    a_DrawFilledRect(
      (aRectf_t){(float)bar_x, (float)bar_y, (float)bar_w, (float)bar_h},
      (aColor_t){40, 40, 40, 200}
    );

    // Red health fill
    if ( fill_w > 0 )
    {
      a_DrawFilledRect(
        (aRectf_t){(float)bar_x, (float)bar_y, (float)fill_w, (float)bar_h},
        (aColor_t){200, 30, 30, 255}
      );
    }

    // Heal pulse — green glow overlay on the health fill
    float heal_pct = player_get_heal_flash_progress();
    if ( heal_pct > 0.0f && fill_w > 0 )
    {
      int heal_alpha = (int)(180.0f * heal_pct);
      // Expand the bar slightly for a "pulse" effect
      int pulse_pad = (int)(3.0f * heal_pct);
      a_DrawFilledRect(
        (aRectf_t){(float)(bar_x - pulse_pad), (float)(bar_y - pulse_pad),
                   (float)(fill_w + pulse_pad * 2), (float)(bar_h + pulse_pad * 2)},
        (aColor_t){40, 255, 40, (uint8_t)heal_alpha}
      );
    }

    // Shield overlay on health bar: blue tint when shielded
    int shielded = player_has_shield();
    if ( shielded )
    {
      float shield_pulse = 0.5f + 0.5f * sinf( director_get_elapsed() * 4.0f );
      int shield_alpha = 30 + (int)(40.0f * shield_pulse);
      a_DrawFilledRect(
        (aRectf_t){(float)bar_x, (float)bar_y, (float)bar_w, (float)bar_h},
        (aColor_t){50, 150, 255, (uint8_t)shield_alpha}
      );
    }

    // Border — blue during shield, green during heal, white otherwise
    aColor_t border_color;
    if ( shielded )
    {
      float shield_pulse = 0.5f + 0.5f * sinf( director_get_elapsed() * 6.0f );
      border_color = (aColor_t){50, 150, 255, (uint8_t)(180 + (int)(75.0f * shield_pulse))};
    }
    else if ( heal_pct > 0.0f )
    {
      border_color = (aColor_t){40, 255, 40, (uint8_t)(150 + (int)(105.0f * heal_pct))};
    }
    else
    {
      border_color = (aColor_t){255, 255, 255, 150};
    }
    a_DrawRect(
      (aRectf_t){(float)bar_x, (float)bar_y, (float)bar_w, (float)bar_h},
      border_color
    );

    // HP text — show shield charges if active
    char hp_text[32];
    if ( shielded )
    {
      snprintf( hp_text, sizeof(hp_text), "%d/%d", player_get_hp(), player_get_max_hp() );
    }
    else
    {
      snprintf( hp_text, sizeof(hp_text), "%d/%d", player_get_hp(), player_get_max_hp() );
    }
    aColor_t hp_text_color = shielded
      ? (aColor_t){100, 200, 255, 255}
      : (heal_pct > 0.0f)
        ? (aColor_t){40, 255, 40, 255}
        : (aColor_t){255, 255, 255, 255};
    aTextStyle_t hp_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = hp_text_color,
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.4f
    };
    a_DrawText( hp_text, bar_x + bar_w / 2, bar_y - 1, hp_style );

    // Shield charge pips right of health bar
    if ( shielded )
    {
      int shield_hits = player_get_shield_hits();
      int pip_sz = 6;
      int pip_gap = 3;
      int pip_start_x = bar_x + bar_w + 6;
      int pip_y = bar_y + (bar_h - pip_sz) / 2;
      for ( int p = 0; p < 3; p++ )
      {
        int px = pip_start_x + p * (pip_sz + pip_gap);
        aColor_t pip_color = (p < shield_hits)
          ? (aColor_t){50, 180, 255, 220}
          : (aColor_t){50, 50, 80, 100};
        a_DrawFilledRect(
          (aRectf_t){(float)px, (float)pip_y, (float)pip_sz, (float)pip_sz},
          pip_color
        );
        a_DrawRect(
          (aRectf_t){(float)px, (float)pip_y, (float)pip_sz, (float)pip_sz},
          (aColor_t){255, 255, 255, 80}
        );
      }
    }
  }

  // Draw dash indicator (bottom left)
  {
    int dash_size = 44;
    int dash_x = 16;
    int dash_y = SCREEN_HEIGHT - dash_size - 16;
    float dash_progress = player_get_dash_cooldown_progress();
    int dash_ready = (dash_progress >= 1.0f);
    float failed_timer = player_get_dash_failed_timer();
    int failed_flash = (failed_timer > 0.0f);

    aRectf_t dash_rect = {(float)dash_x, (float)dash_y, (float)dash_size, (float)dash_size};

    // Background — pulse red on failed dash attempt
    aColor_t dash_bg;
    aColor_t dash_border;
    if (failed_flash) {
      float pulse = 0.5f + 0.5f * sinf(failed_timer * 20.0f);
      int red_alpha = (int)(180.0f * pulse);
      dash_bg = (aColor_t){180, 30, 30, (uint8_t)red_alpha};
      dash_border = (aColor_t){255, 60, 60, 255};
    } else if (dash_ready) {
      dash_bg = (aColor_t){40, 80, 120, 220};
      dash_border = (aColor_t){100, 200, 255, 200};
    } else {
      dash_bg = (aColor_t){40, 40, 40, 180};
      dash_border = (aColor_t){255, 255, 255, 60};
    }
    a_DrawFilledRect(dash_rect, dash_bg);
    a_DrawRect(dash_rect, dash_border);

    // "DASH" label
    aTextStyle_t dash_label_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = failed_flash ? (aColor_t){255, 80, 80, 255}
           : dash_ready  ? (aColor_t){100, 200, 255, 255}
                         : (aColor_t){150, 150, 150, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText("DASH", dash_x + dash_size / 2, dash_y + 6, dash_label_style);

    // "SHIFT" key hint below
    aTextStyle_t key_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = dash_ready ? (aColor_t){255, 255, 255, 255} : (aColor_t){100, 100, 100, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.35f
    };
    a_DrawText("SHIFT", dash_x + dash_size / 2, dash_y + 22, key_style);

    // Cooldown pie overlay (reuse same logic as hotbar)
    if (!dash_ready) {
      float remaining = 1.0f - dash_progress;
      float threshold = remaining * 2.0f * (float)PI;
      int cx = dash_x + dash_size / 2;
      int cy = dash_y + dash_size / 2;
      int r = dash_size / 2 - 2;
      int r2 = r * r;

      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 140);

      for (int pdy = -r; pdy <= r; pdy++) {
        for (int pdx = -r; pdx <= r; pdx++) {
          if (pdx * pdx + pdy * pdy > r2) continue;
          float angle = atan2f((float)pdx, (float)(-pdy));
          if (angle < 0.0f) angle += 2.0f * (float)PI;
          if (angle < threshold) {
            SDL_RenderDrawPoint(app.renderer, cx + pdx, cy + pdy);
          }
        }
      }

      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
    }

    // Floating "X.Xs" text above dash button on failed attempt
    if (failed_flash) {
      float cd_remaining = player_get_dash_cooldown_remaining();
      char cd_text[16];
      snprintf(cd_text, sizeof(cd_text), "%.1fs", cd_remaining);
      float float_y = (float)(dash_y - 14) - (0.6f - failed_timer) * 20.0f;
      float alpha_pct = failed_timer / 0.6f;
      int text_alpha = (int)(255.0f * alpha_pct);
      if (text_alpha > 255) text_alpha = 255;
      aTextStyle_t cd_style = {
        .type = FONT_ENTER_COMMAND,
        .fg = {255, 80, 80, (uint8_t)text_alpha},
        .align = TEXT_ALIGN_CENTER,
        .scale = 0.45f
      };
      a_DrawText(cd_text, dash_x + dash_size / 2, (int)float_y, cd_style);
    }
  }

  // Draw keyboard shortcuts (bottom right, 30% opacity white)
  aTextStyle_t shortcuts_style = {
    .type = FONT_ENTER_COMMAND,
    .fg = {255, 255, 255, 77},  // 30% opacity (255 * 0.3 = 77)
    .align = TEXT_ALIGN_RIGHT,
    .wrap_width = 0,
    .scale = 0.5f
  };

  int y_offset = SCREEN_HEIGHT - 80;
  a_DrawText("Ctrl+T - Text Test", SCREEN_WIDTH - 20, y_offset, shortcuts_style);
  y_offset += 20;
  a_DrawText("Ctrl+M - Audio Test", SCREEN_WIDTH - 20, y_offset, shortcuts_style);
  y_offset += 20;
  a_DrawText("ESC - Quit", SCREEN_WIDTH - 20, y_offset, shortcuts_style);

  // XP bar — full width at bottom of screen
  {
    int xp_bar_h = 6;
    int xp_bar_y = SCREEN_HEIGHT - xp_bar_h;

    float xp_pct = xp_get_level_progress();
    int xp_fill_w = (int)(SCREEN_WIDTH * xp_pct);

    // Background
    a_DrawFilledRect(
      (aRectf_t){0, (float)xp_bar_y, (float)SCREEN_WIDTH, (float)xp_bar_h},
      (aColor_t){30, 30, 30, 180}
    );

    // Green XP fill
    if ( xp_fill_w > 0 )
    {
      a_DrawFilledRect(
        (aRectf_t){0, (float)xp_bar_y, (float)xp_fill_w, (float)xp_bar_h},
        (aColor_t){80, 220, 80, 220}
      );
    }

    // "Lv.X" label above left edge
    char lv_text[16];
    snprintf( lv_text, sizeof(lv_text), "Lv.%d", xp_get_level() );
    aTextStyle_t lv_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 255, 180, 220},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.35f
    };
    a_DrawText( lv_text, 4, xp_bar_y - 12, lv_style );
  }
}
