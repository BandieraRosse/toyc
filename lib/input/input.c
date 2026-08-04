/* Platform-independent input state and per-frame key edges. */

#include "toy_input.h"
#include "string.h"
#include "tlibc_compat.h"

void toy_input_init(struct toy_input *input)
{
    if (!input) return;
    memset(input, 0, sizeof(struct toy_input));
}

void toy_input_begin_frame(struct toy_input *input)
{
    if (!input) return;
    memset(input->key_pressed, 0, sizeof(input->key_pressed));
    memset(input->key_released, 0, sizeof(input->key_released));
    input->pointer_moved = 0;
}

void toy_input_apply(struct toy_input *input,
                     const struct toy_window_events *events)
{
    if (!input || !events) return;
    if (events->keyboard_focus_changed) {
        input->keyboard_focused = events->keyboard_focused;
        if (!events->keyboard_focused) {
            for (int key = 0; key < TOY_INPUT_KEY_COUNT; key++) {
                if (input->key_down[key]) input->key_released[key] = 1;
                input->key_down[key] = 0;
            }
        }
    }
    for (int i = 0; i < events->key_event_count; i++) {
        unsigned int key = events->key_events[i].key;
        int pressed = events->key_events[i].pressed;
        if (key >= TOY_INPUT_KEY_COUNT) continue;
        if (pressed) {
            if (!input->key_down[key]) input->key_pressed[key] = 1;
            input->key_down[key] = 1;
        } else {
            if (input->key_down[key]) input->key_released[key] = 1;
            input->key_down[key] = 0;
        }
    }
    if (events->pointer_moved) {
        input->pointer_x = events->pointer_x;
        input->pointer_y = events->pointer_y;
        input->pointer_moved = 1;
    }
}

int toy_input_down(const struct toy_input *input, unsigned int key)
{
    return input && key < TOY_INPUT_KEY_COUNT && input->key_down[key];
}

int toy_input_pressed(const struct toy_input *input, unsigned int key)
{
    return input && key < TOY_INPUT_KEY_COUNT && input->key_pressed[key];
}

int toy_input_released(const struct toy_input *input, unsigned int key)
{
    return input && key < TOY_INPUT_KEY_COUNT && input->key_released[key];
}
