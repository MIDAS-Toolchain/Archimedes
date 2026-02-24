#ifndef HOTBAR_H
#define HOTBAR_H

void hotbar_init(void);
void hotbar_cleanup(void);
void hotbar_add_slot(void);
void hotbar_draw(void);

// Returns slot index under (mx, my), or -1 if none
int hotbar_get_hovered_slot(int mx, int my);

// Get slot screen rect (returns 0 on success, -1 if invalid)
int hotbar_get_slot_rect(int slot, int* out_x, int* out_y, int* out_w, int* out_h);

int hotbar_get_slot_count(void);

#endif /* HOTBAR_H */
