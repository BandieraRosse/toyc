/* test_input.c — 输入批次、状态边沿和焦点清理回归
 * EXPECT: 0
 */

#include "toy_input.h"
#include "string.h"
#include "tlibc_compat.h"

int main(void)
{
    struct toy_input input;
    struct toy_window_events events;

    toy_input_init(&input);
    memset(&events, 0, sizeof(events));
    events.keyboard_focus_changed = 1;
    events.keyboard_focused = 1;
    events.key_event_count = 2;
    events.key_events[0].key = 30;
    events.key_events[0].pressed = 1;
    events.key_events[1].key = 31;
    events.key_events[1].pressed = 1;
    toy_input_apply(&input, &events);
    if (!toy_input_down(&input, 30) || !toy_input_pressed(&input, 30)) return 1;
    if (!toy_input_down(&input, 31) || !toy_input_pressed(&input, 31)) return 2;

    toy_input_begin_frame(&input);
    if (!toy_input_down(&input, 30) || toy_input_pressed(&input, 30)) return 3;
    memset(&events, 0, sizeof(events));
    events.key_event_count = 2;
    events.key_events[0].key = 30;
    events.key_events[0].pressed = 0;
    events.key_events[1].key = 400;
    events.key_events[1].pressed = 1;
    toy_input_apply(&input, &events);
    if (toy_input_down(&input, 30) || !toy_input_released(&input, 30)) return 4;

    toy_input_begin_frame(&input);
    memset(&events, 0, sizeof(events));
    events.keyboard_focus_changed = 1;
    events.keyboard_focused = 0;
    toy_input_apply(&input, &events);
    if (toy_input_down(&input, 31) || !toy_input_released(&input, 31)) return 5;
    return 0;
}
