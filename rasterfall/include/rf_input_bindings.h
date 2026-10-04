#ifndef RASTERFALL_RF_INPUT_BINDINGS_H
#define RASTERFALL_RF_INPUT_BINDINGS_H

#include "rf_core_input.h"

/* Physical IDs are SDL scancodes on Windows and evdev codes on Linux.
 * Action IDs never depend on either platform's keyboard layout. */
enum rf_input_action {
    RF_ACTION_FORWARD, RF_ACTION_BACK, RF_ACTION_STRAFE_LEFT,
    RF_ACTION_STRAFE_RIGHT, RF_ACTION_LOOK_LEFT, RF_ACTION_LOOK_RIGHT,
    RF_ACTION_LOOK_UP, RF_ACTION_LOOK_DOWN, RF_ACTION_FIRE_KEY,
    RF_ACTION_JUMP, RF_ACTION_SHOVE_KEY, RF_ACTION_RELOAD,
    RF_ACTION_SLOT_1, RF_ACTION_SLOT_2, RF_ACTION_SLOT_3, RF_ACTION_SLOT_4,
    RF_ACTION_INTERACT, RF_ACTION_FLAG, RF_ACTION_COMMAND_MODE,
    RF_ACTION_CANCEL, RF_ACTION_CONFIRM, RF_ACTION_UI_UP,
    RF_ACTION_UI_DOWN, RF_ACTION_UI_LEFT, RF_ACTION_UI_RIGHT,
    RF_ACTION_TERMINAL, RF_ACTION_CONSOLE, RF_ACTION_MANAGED_TERMINAL,
    RF_ACTION_BACKSPACE, RF_ACTION_POSE_PAGE, RF_ACTION_MODIFIER,
    RF_ACTION_POSE_PREV_FIELD, RF_ACTION_POSE_NEXT_FIELD,
    RF_ACTION_POSE_DECREASE, RF_ACTION_POSE_INCREASE,
    RF_ACTION_POSE_EXPORT, RF_ACTION_POSE_AXES, RF_ACTION_POSE_ANCHORS,
    RF_ACTION_POSE_IK, RF_ACTION_POSE_ANIMATION, RF_ACTION_POSE_AXIS_X,
    RF_ACTION_POSE_AXIS_Y, RF_ACTION_POSE_AXIS_Z,
    RF_ACTION_POSE_PREV_BONE, RF_ACTION_POSE_NEXT_BONE,
    RF_ACTION_POSE_FINE_DECREASE, RF_ACTION_POSE_FINE_INCREASE,
    RF_ACTION_POSE_RESET, RF_ACTION_POSE_EXIT, RF_ACTION_RESTART,
    RF_ACTION_DESKTOP, RF_ACTION_SCOREBOARD, RF_ACTION_TEXT_NEXT_FIELD,
    RF_ACTION_RTS_FOLLOW, RF_ACTION_RTS_TELEPORT,
    RF_ACTION_UI_MODE, RF_ACTION_MAP_EXPAND, /* Retired; keep action IDs stable. */
    RF_ACTION_COMMS_FOCUS,
    RF_ACTION_COMMS_HIDE, RF_ACTION_COMMS_ANSWER, RF_ACTION_RTS_STOP,
    RF_ACTION_RTS_GROUP_1, RF_ACTION_RTS_GROUP_2, RF_ACTION_RTS_GROUP_3,
    RF_ACTION_RTS_GROUP_4, RF_ACTION_RTS_GROUP_5, RF_ACTION_RTS_GROUP_6,
    RF_ACTION_RTS_GROUP_7, RF_ACTION_RTS_GROUP_8, RF_ACTION_RTS_GROUP_9,
    RF_ACTION_RTS_GROUP_0, RF_ACTION_CONTROL,
    RF_ACTION_COUNT
};

struct rf_input_bindings {
    unsigned short physical[RF_ACTION_COUNT];
    unsigned short secondary[RF_ACTION_COUNT];
};

void rf_input_bindings_defaults(struct rf_input_bindings *bindings);
int rf_input_bind(struct rf_input_bindings *bindings,
                  enum rf_input_action action, unsigned int physical);
/* Changes the active Game Runtime mapping after defaults are installed. */
int rf_game_bind_action(enum rf_input_action action, unsigned int physical);
unsigned int rf_input_binding(const struct rf_input_bindings *bindings,
                              enum rf_input_action action);
/* Printable name of the current primary physical binding, never a UI literal. */
void rf_input_action_label(const struct rf_input_bindings *bindings,
                           enum rf_input_action action, char *label, unsigned capacity);
int rf_action_down(const struct rf_input_bindings *bindings,
                   const struct rf_input_frame *frame,
                   enum rf_input_action action);
int rf_action_pressed(const struct rf_input_bindings *bindings,
                      const struct rf_input_frame *frame,
                      const unsigned char *pending_physical,
                      enum rf_input_action action);
void rf_action_consume(const struct rf_input_bindings *bindings,
                       struct rf_input_frame *frame,
                       unsigned char *pending_physical,
                       enum rf_input_action action);
int rf_input_bindings_logic_test(void);

#endif
