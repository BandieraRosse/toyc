#include "rf_input_bindings.h"
#include "string.h"
#include "stdio.h"

#ifdef TOYC_WINDOWS
/* SDL2 scancodes use USB HID keyboard usages for these keys. */
static const unsigned short default_physical[RF_ACTION_COUNT] = {
    26, 22, 4, 7, 80, 79, 82, 81, 40, 44, 56, 21,
    30, 31, 32, 33, 8, 9, 16, 41, 40, 82, 81, 80, 79,
    58, 53, 0, 42, 43, 225, 54, 55, 13, 15, 19,
    24, 18, 12, 25, 27, 28, 29, 17, 5, 45, 46,
    21, 41, 21, 69, 43, 43, 28, 23,
    0, 0, 40, 11, 29, 27,
    30,31,32,33,34,35,36,37,38,39,224
};
static const unsigned short default_secondary[RF_ACTION_COUNT] = {
    [RF_ACTION_CONSOLE] = 59,
    [RF_ACTION_FIRE_KEY] = 88,
    [RF_ACTION_CONFIRM] = 88,
    [RF_ACTION_SLOT_1] = 89,
    [RF_ACTION_SLOT_2] = 90,
    [RF_ACTION_SLOT_3] = 91,
    [RF_ACTION_SLOT_4] = 92,
    [RF_ACTION_MODIFIER] = 229,
    [RF_ACTION_CONTROL] = 228,
    [RF_ACTION_POSE_FINE_DECREASE] = 86,
    [RF_ACTION_POSE_FINE_INCREASE] = 87
};
#else
/* Wayland currently forwards evdev physical key numbers. */
static const unsigned short default_physical[RF_ACTION_COUNT] = {
    17, 31, 30, 32, 105, 106, 103, 108, 28, 57, 53, 19,
    2, 3, 4, 5, 18, 33, 50, 1, 28, 103, 108, 105, 106,
    59, 41, 0, 14, 15, 42, 51, 52, 36, 38, 25,
    22, 24, 23, 47, 45, 21, 44, 49, 48, 12, 13,
    19, 1, 19, 88, 15, 15, 21, 20,
    0, 0, 28, 35, 44, 45,
    2,3,4,5,6,7,8,9,10,11,29
};
static const unsigned short default_secondary[RF_ACTION_COUNT] = {
    [RF_ACTION_CONSOLE]=60,
    [RF_ACTION_CONTROL]=97, [RF_ACTION_MODIFIER]=54
};
#endif

void rf_input_bindings_defaults(struct rf_input_bindings *bindings)
{
    if (!bindings) return;
    for (int i = 0; i < RF_ACTION_COUNT; i++)
        bindings->physical[i] = default_physical[i];
    for (int i = 0; i < RF_ACTION_COUNT; i++)
        bindings->secondary[i] = default_secondary[i];
}

int rf_input_bind(struct rf_input_bindings *bindings,
                  enum rf_input_action action, unsigned int physical)
{
    if (!bindings || (unsigned int)action >= RF_ACTION_COUNT ||
        physical == 0 || physical >= RF_INPUT_PHYSICAL_KEY_COUNT) return -1;
    bindings->physical[action] = (unsigned short)physical;
    bindings->secondary[action] = 0;
    return 0;
}

unsigned int rf_input_binding(const struct rf_input_bindings *bindings,
                              enum rf_input_action action)
{
    if (!bindings || (unsigned int)action >= RF_ACTION_COUNT)
        return RF_INPUT_PHYSICAL_KEY_COUNT;
    return bindings->physical[action];
}

void rf_input_action_label(const struct rf_input_bindings *bindings,
                           enum rf_input_action action, char *label, unsigned capacity)
{
    unsigned key=rf_input_binding(bindings,action);
    const char *name=0;
    if (!label || !capacity) return;
#ifdef TOYC_WINDOWS
    if(key>=4 && key<=29) { snprintf(label,capacity,"%c",'A'+key-4);return; }
    if(key>=30 && key<=38) { snprintf(label,capacity,"%u",key-29);return; }
    if(key>=58 && key<=69) { snprintf(label,capacity,"F%u",key-57);return; }
    switch(key) {
        case 39:name="0";break;case 40:name="Enter";break;case 41:name="Esc";break;
        case 42:name="Backspace";break;case 43:name="Tab";break;case 44:name="Space";break;
        case 53:name="`";break;case 79:name="Right";break;case 80:name="Left";break;
        case 81:name="Down";break;case 82:name="Up";break;case 88:name="Num Enter";break;
        case 225:case 229:name="Shift";break;
        case 224:case 228:name="Ctrl";break;
    }
#else
    static const unsigned keys[]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    for(unsigned i=0;i<26;++i) if(key==keys[i]) {snprintf(label,capacity,"%c",'A'+i);return;}
    if(key>=2 && key<=10) {snprintf(label,capacity,"%u",key-1);return;}
    if(key>=59 && key<=68) {snprintf(label,capacity,"F%u",key-58);return;}
    switch(key) {
        case 11:name="0";break;case 1:name="Esc";break;case 28:name="Enter";break;
        case 14:name="Backspace";break;case 15:name="Tab";break;case 57:name="Space";break;
        case 41:name="`";break;case 103:name="Up";break;case 108:name="Down";break;
        case 105:name="Left";break;case 106:name="Right";break;case 88:name="F12";break;
        case 42:case 54:name="Shift";break;
        case 29:case 97:name="Ctrl";break;
    }
#endif
    if(name)snprintf(label,capacity,"%s",name);
    else snprintf(label,capacity,"Key %u",key);
}

int rf_action_down(const struct rf_input_bindings *bindings,
                   const struct rf_input_frame *frame,
                   enum rf_input_action action)
{
    unsigned int key = rf_input_binding(bindings, action);
    unsigned int secondary = bindings && (unsigned int)action < RF_ACTION_COUNT ?
                             bindings->secondary[action] : 0;
    return frame && key < RF_INPUT_PHYSICAL_KEY_COUNT &&
           (frame->physical_down[key] ||
            (secondary && frame->physical_down[secondary]));
}

int rf_action_pressed(const struct rf_input_bindings *bindings,
                      const struct rf_input_frame *frame,
                      const unsigned char *pending_physical,
                      enum rf_input_action action)
{
    unsigned int key = rf_input_binding(bindings, action);
    unsigned int secondary = bindings && (unsigned int)action < RF_ACTION_COUNT ?
                             bindings->secondary[action] : 0;
    return frame && key < RF_INPUT_PHYSICAL_KEY_COUNT &&
           (frame->physical_pressed[key] ||
            (pending_physical && pending_physical[key]) ||
            (secondary && (frame->physical_pressed[secondary] ||
                           (pending_physical && pending_physical[secondary]))));
}

void rf_action_consume(const struct rf_input_bindings *bindings,
                       struct rf_input_frame *frame,
                       unsigned char *pending_physical,
                       enum rf_input_action action)
{
    unsigned int key = rf_input_binding(bindings, action);
    unsigned int secondary = bindings && (unsigned int)action < RF_ACTION_COUNT ?
                             bindings->secondary[action] : 0;
    if (!frame || key >= RF_INPUT_PHYSICAL_KEY_COUNT) return;
    frame->physical_pressed[key] = 0;
    if (pending_physical) pending_physical[key] = 0;
    if (secondary) {
        frame->physical_pressed[secondary] = 0;
        if (pending_physical) pending_physical[secondary] = 0;
    }
}

int rf_input_bindings_logic_test(void)
{
    struct rf_input_bindings bindings;
    struct rf_input_frame frame;
    unsigned char pending[RF_INPUT_PHYSICAL_KEY_COUNT];
    unsigned int original;
    memset(&frame, 0, sizeof(frame));
    memset(pending, 0, sizeof(pending));
    rf_input_bindings_defaults(&bindings);
    for (int action = 0; action < RF_ACTION_COUNT; action++)
        if ((!bindings.physical[action] && action != RF_ACTION_MAP_EXPAND &&
              action != RF_ACTION_UI_MODE && action != RF_ACTION_MANAGED_TERMINAL) ||
            bindings.physical[action] >= RF_INPUT_PHYSICAL_KEY_COUNT)
            return -7;
    /* Both terminal shortcuts use the same edge and consumption path. */
    original=bindings.secondary[RF_ACTION_CONSOLE];
    frame.physical_pressed[original]=1;
    pending[original]=1;
    if(!rf_action_pressed(&bindings,&frame,pending,RF_ACTION_CONSOLE) ||
        rf_action_pressed(&bindings,&frame,pending,RF_ACTION_MANAGED_TERMINAL)) return -8;
    rf_action_consume(&bindings,&frame,pending,RF_ACTION_CONSOLE);
    if(rf_action_pressed(&bindings,&frame,pending,RF_ACTION_CONSOLE)) return -9;
#ifdef TOYC_WINDOWS
    frame.physical_pressed[88] = 1;
    if (!rf_action_pressed(&bindings, &frame, pending,
                           RF_ACTION_CONFIRM)) return -5;
    if (rf_input_bind(&bindings, RF_ACTION_CONFIRM, 510) < 0 ||
        rf_action_pressed(&bindings, &frame, pending,
                          RF_ACTION_CONFIRM)) return -6;
#endif
    original = rf_input_binding(&bindings, RF_ACTION_JUMP);
    if (original >= RF_INPUT_PHYSICAL_KEY_COUNT ||
        rf_input_bind(&bindings, RF_ACTION_JUMP, 511) < 0 ||
        rf_input_bind(&bindings, RF_ACTION_RELOAD,
                      RF_INPUT_PHYSICAL_KEY_COUNT) == 0)
        return -1;
    frame.physical_down[original] = 1;
    frame.physical_pressed[original] = 1;
    if (rf_action_down(&bindings, &frame, RF_ACTION_JUMP) ||
        rf_action_pressed(&bindings, &frame, pending, RF_ACTION_JUMP))
        return -2;
    frame.physical_down[511] = 1;
    pending[511] = 1;
    if (!rf_action_down(&bindings, &frame, RF_ACTION_JUMP) ||
        !rf_action_pressed(&bindings, &frame, pending, RF_ACTION_JUMP))
        return -3;
    rf_action_consume(&bindings, &frame, pending, RF_ACTION_JUMP);
    if (rf_action_pressed(&bindings, &frame, pending, RF_ACTION_JUMP) ||
        !frame.physical_down[511]) return -4;
    return 0;
}
