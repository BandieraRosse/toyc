#ifndef TOYC_TOY_INPUT_H
#define TOYC_TOY_INPUT_H

#include "toy_window.h"

struct toy_input {
    unsigned char key_down[TOY_INPUT_KEY_COUNT];
    unsigned char key_pressed[TOY_INPUT_KEY_COUNT];
    unsigned char key_released[TOY_INPUT_KEY_COUNT];
    int keyboard_focused;
    int pointer_x;
    int pointer_y;
    int pointer_moved;
    int relative_x;
    int relative_y;
    int pointer_locked;
    unsigned int mouse_buttons;
};

void toy_input_init(struct toy_input *input);
void toy_input_begin_frame(struct toy_input *input);
void toy_input_apply(struct toy_input *input,
                     const struct toy_window_events *events);
int toy_input_down(const struct toy_input *input, unsigned int key);
int toy_input_pressed(const struct toy_input *input, unsigned int key);
int toy_input_released(const struct toy_input *input, unsigned int key);

#endif
