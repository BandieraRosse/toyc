#ifndef RASTERFALL_RF_CORE_INPUT_H
#define RASTERFALL_RF_CORE_INPUT_H

#define RF_INPUT_KEY_COUNT 256

/* Frame-owned input view.  It contains sampled values only; it has no
 * platform handles and can be copied or retained by Game for one frame. */
struct rf_input_frame {
    unsigned char key_down[RF_INPUT_KEY_COUNT];
    unsigned char key_pressed[RF_INPUT_KEY_COUNT];
    unsigned char key_released[RF_INPUT_KEY_COUNT];
    int keyboard_focused;
    int pointer_x;
    int pointer_y;
    int pointer_moved;
    int relative_x;
    int relative_y;
    int pointer_locked;
    unsigned int mouse_buttons;
};

int rf_input_down(const struct rf_input_frame *input, unsigned int key);
int rf_input_pressed(const struct rf_input_frame *input, unsigned int key);
int rf_input_released(const struct rf_input_frame *input, unsigned int key);

/* Temporary source-compatibility aliases for the existing key mapping. */
#define toy_input_down rf_input_down
#define toy_input_pressed rf_input_pressed
#define toy_input_released rf_input_released

#endif
