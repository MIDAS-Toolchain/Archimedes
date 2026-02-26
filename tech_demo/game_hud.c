#include <stdio.h>
#include <math.h>
#include "Archimedes.h"
#include "game_hud.h"
#include "game_director.h"
#include "player_actions.h"
#include "enemy.h"
#include "xp.h"
#include "stats.h"
#include "progress.h"
#include "pickups.h"

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
      .scale = 0.9f
    };
    a_DrawText( score_text, 16, 8, score_style );

    // "SCORE" label below
    aTextStyle_t score_label = {
      .type = FONT_ENTER_COMMAND,
      .fg = {160, 160, 180, 180},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.4f
    };
    a_DrawText( "SCORE", 16, 36, score_label );

    // High score below current score
    char best_text[32];
    snprintf( best_text, sizeof(best_text), "%d", stats_get_best_score() );
    aTextStyle_t best_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 220, 80, 255},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.6f
    };
    a_DrawText( best_text, 16, 54, best_style );

    aTextStyle_t best_label = {
      .type = FONT_ENTER_COMMAND,
      .fg = {200, 180, 60, 150},
      .align = TEXT_ALIGN_LEFT,
      .scale = 0.35f
    };
    a_DrawText( "BEST", 16, 76, best_label );
  }

  // Draw difficulty stats (top right)
  {
    aTextStyle_t label_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {160, 160, 180, 180},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.4f
    };
    aTextStyle_t value_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {255, 255, 255, 255},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.5f
    };

    int rx = SCREEN_WIDTH - 16;
    int ry = 8;
    int line_h = 22;
    char buf[32];

    int current_enemy_count = enemy_get_count();
    int max_active = director_get_max_active();
    float speed_mult = director_get_speed_mult();
    int hp_bonus = director_get_hp_bonus();
    float spawn_interval = director_get_spawn_interval();

    snprintf( buf, sizeof(buf), "%d / %d", current_enemy_count, max_active );
    a_DrawText( "ENEMIES", rx, ry, label_style );
    ry += 13;
    a_DrawText( buf, rx, ry, value_style );
    ry += line_h;

    snprintf( buf, sizeof(buf), "x%.1f", speed_mult );
    a_DrawText( "SPEED", rx, ry, label_style );
    ry += 13;
    a_DrawText( buf, rx, ry, value_style );
    ry += line_h;

    snprintf( buf, sizeof(buf), "+%d", hp_bonus );
    a_DrawText( "EXTRA HP", rx, ry, label_style );
    ry += 13;
    a_DrawText( buf, rx, ry, value_style );
    ry += line_h;

    snprintf( buf, sizeof(buf), "%.1fs", spawn_interval );
    a_DrawText( "SPAWN RATE", rx, ry, label_style );
    ry += 13;
    a_DrawText( buf, rx, ry, value_style );
  }

  // Draw player health bar (top center)
  {
    int danger = player_get_danger_tier();
    static const int bar_widths[4]  = { 200, 220, 240, 260 };
    static const int bar_heights[4] = { 12,  14,  16,  18  };
    int bar_w = bar_widths[danger];
    int bar_h = bar_heights[danger];
    int bar_x = (SCREEN_WIDTH - bar_w) / 2;
    int bar_y = 8;

    // Critical shake at tier 3 (<=25% HP)
    if (danger >= 3) {
      float elapsed = director_get_elapsed();
      bar_x += (int)(2.0f * sinf(elapsed * 23.0f));
      bar_y += (int)(1.5f * sinf(elapsed * 31.0f));
    }

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

    // Critical red pulse overlay at tier 3
    if (danger >= 3 && fill_w > 0) {
      float pulse = sinf(director_get_elapsed() * 8.0f);
      int pulse_alpha = (int)(60.0f * (0.5f + 0.5f * pulse));
      a_DrawFilledRect(
        (aRectf_t){(float)bar_x, (float)bar_y, (float)fill_w, (float)bar_h},
        (aColor_t){255, 0, 0, (uint8_t)pulse_alpha}
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

    // Border — red at critical, blue during shield, green during heal, white otherwise
    aColor_t border_color;
    if (danger >= 3) {
      float pulse = sinf(director_get_elapsed() * 8.0f);
      border_color = (aColor_t){255, 50, 50, (uint8_t)(180 + (int)(75.0f * (0.5f + 0.5f * pulse)))};
    }
    else if ( shielded )
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

    // HP text
    char hp_text[32];
    snprintf( hp_text, sizeof(hp_text), "%d/%d", player_get_hp(), player_get_max_hp() );
    aColor_t hp_text_color = shielded
      ? (aColor_t){100, 200, 255, 255}
      : (heal_pct > 0.0f)
        ? (aColor_t){40, 255, 40, 255}
        : (danger >= 3)
          ? (aColor_t){255, 100, 100, 255}
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
      int shield_max = BUFF_SHIELD_PLAYER_CAP + global_get_shield_hits_bonus();
      for ( int p = 0; p < shield_max; p++ )
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
    int dash_w = 110;
    int dash_h = 54;
    int dash_x = 16;
    int dash_y = SCREEN_HEIGHT - dash_h - 40;
    float dash_progress = player_get_dash_cooldown_progress();
    int dash_ready = (dash_progress >= 1.0f);
    float failed_timer = player_get_dash_failed_timer();
    int failed_flash = (failed_timer > 0.0f);

    aRectf_t dash_rect = {(float)dash_x, (float)dash_y, (float)dash_w, (float)dash_h};

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
      .scale = 0.55f
    };
    a_DrawText("DASH", dash_x + dash_w / 2, dash_y + 6, dash_label_style);

    // "SHIFT" key hint below
    aTextStyle_t key_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = dash_ready ? (aColor_t){255, 255, 255, 255} : (aColor_t){100, 100, 100, 255},
      .align = TEXT_ALIGN_CENTER,
      .scale = 0.45f
    };
    a_DrawText("SHIFT / SPACE", dash_x + dash_w / 2, dash_y + 28, key_style);

    // Cooldown pie overlay (reuse same logic as hotbar)
    if (!dash_ready) {
      float remaining = 1.0f - dash_progress;
      float threshold = remaining * 2.0f * (float)PI;
      int cx = dash_x + dash_w / 2;
      int cy = dash_y + dash_h / 2;
      int r = (dash_w < dash_h ? dash_w : dash_h) / 2 - 2;
      int r2 = r * r;

      SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 140);

      // Scanline pie: for each row, find the x-range inside circle AND inside the pie sector
      for (int pdy = -r; pdy <= r; pdy++) {
        int row_dx = (int)sqrtf((float)(r2 - pdy * pdy));
        // For this row, find which x pixels are inside the pie sector (angle < threshold)
        // We scan the row but use atan2f only at the endpoints to find the filled span
        int row_start = cx + row_dx + 1; // sentinel: no pixels
        int row_end = cx - row_dx - 1;
        for (int pdx = -row_dx; pdx <= row_dx; pdx++) {
          float angle = atan2f((float)pdx, (float)(-pdy));
          if (angle < 0.0f) angle += 2.0f * (float)PI;
          if (angle < threshold) {
            int px = cx + pdx;
            if (px < row_start) row_start = px;
            if (px > row_end) row_end = px;
          }
        }
        if (row_end >= row_start) {
          SDL_Rect row = { row_start, cy + pdy, row_end - row_start + 1, 1 };
          SDL_RenderFillRect(app.renderer, &row);
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
      a_DrawText(cd_text, dash_x + dash_w / 2, (int)float_y, cd_style);
    }

    // Dash charge pips (only when player has 2 charges)
    if (player_get_dash_max_charges() > 1) {
      int pip_w = 12;
      int pip_h = 6;
      int pip_gap = 4;
      int total_pip_w = 2 * pip_w + pip_gap;
      int pip_base_x = dash_x + (dash_w - total_pip_w) / 2;
      int pip_base_y = dash_y + dash_h + 4;

      for (int ci = 0; ci < 2; ci++) {
        float prog = player_get_dash_charge_progress(ci);
        int cpx = pip_base_x + ci * (pip_w + pip_gap);
        int ready = (prog >= 1.0f);

        // Background
        a_DrawFilledRect(
          (aRectf_t){(float)cpx, (float)pip_base_y, (float)pip_w, (float)pip_h},
          (aColor_t){40, 40, 40, 180}
        );

        // Fill (partial progress)
        if (prog > 0.0f) {
          int fill = (int)((float)pip_w * prog);
          if (fill < 1) fill = 1;
          aColor_t fill_color = ready
            ? (aColor_t){80, 180, 255, 220}
            : (aColor_t){60, 60, 90, 180};
          a_DrawFilledRect(
            (aRectf_t){(float)cpx, (float)pip_base_y, (float)fill, (float)pip_h},
            fill_color
          );
        }

        // Border
        aColor_t bdr = ready
          ? (aColor_t){100, 200, 255, 200}
          : (aColor_t){80, 80, 100, 120};
        a_DrawRect(
          (aRectf_t){(float)cpx, (float)pip_base_y, (float)pip_w, (float)pip_h},
          bdr
        );
      }
    }
  }

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

    // Level + XP info at bottom right
    char lv_text[16];
    snprintf( lv_text, sizeof(lv_text), "Lv.%d", xp_get_level() );
    aTextStyle_t lv_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {180, 255, 180, 255},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.65f
    };
    a_DrawText( lv_text, SCREEN_WIDTH - 10, xp_bar_y - 32, lv_style );

    char xp_text[32];
    int needed = xp_get_needed();
    int current = xp_get_current();
    snprintf( xp_text, sizeof(xp_text), "%d / %d XP", current, needed );
    aTextStyle_t xp_style = {
      .type = FONT_ENTER_COMMAND,
      .fg = {140, 220, 140, 200},
      .align = TEXT_ALIGN_RIGHT,
      .scale = 0.45f
    };
    a_DrawText( xp_text, SCREEN_WIDTH - 10, xp_bar_y - 15, xp_style );
  }
}
