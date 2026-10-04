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
    memset(input->physical_pressed, 0, sizeof(input->physical_pressed));
    memset(input->physical_released, 0, sizeof(input->physical_released));
    input->pointer_moved = 0;
    input->relative_x = 0;
    input->relative_y = 0;
    input->wheel_y = 0;
}

void toy_input_apply(struct toy_input *input,
                     const struct toy_window_events *events)
{
    if (!input || !events) return;
    if (events->keyboard_focus_changed || events->keyboard_focus_valid) {
        input->keyboard_focused = events->keyboard_focused;
        if (!events->keyboard_focused) {
            for (int key = 0; key < TOY_INPUT_KEY_COUNT; key++) {
                if (input->key_down[key]) input->key_released[key] = 1;
                input->key_down[key] = 0;
            }
            for (int key = 0; key < TOY_PHYSICAL_KEY_COUNT; key++) {
                if (input->physical_down[key]) input->physical_released[key] = 1;
                input->physical_down[key] = 0;
            }
        }
    }
    for (int i = 0; i < events->key_event_count; i++) {
        unsigned int key = events->key_events[i].key;
        unsigned int physical = events->key_events[i].physical_key;
        int pressed = events->key_events[i].pressed;
        if (physical > 0 && physical < TOY_PHYSICAL_KEY_COUNT) {
            if (pressed) {
                if (!input->physical_down[physical])
                    input->physical_pressed[physical] = 1;
                input->physical_down[physical] = 1;
            } else {
                if (input->physical_down[physical])
                    input->physical_released[physical] = 1;
                input->physical_down[physical] = 0;
            }
        }
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
    if (events->relative_moved) {
        input->relative_x += events->relative_x;
        input->relative_y += events->relative_y;
    }
    if (events->pointer_lock_changed)
        input->pointer_locked = events->pointer_locked;
    input->mouse_buttons = events->mouse_buttons;
    input->wheel_y += events->wheel_y;
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
