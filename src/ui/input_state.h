#ifndef INPUT_H
#define INPUT_H

#include "../utils/queue.h"
#include "ui.h"

enum input_state_type{
    INPUT_STATE_DEFAULT = 0,
    INPUT_STATE_PREFIX = 1,
    INPUT_STATE_TYPE_SEARCH_QUERY = 2,
    INPUT_STATE_TYPE_SEARCH_BACKWARD_QUERY = 3,
    INPUT_STATE_TYPE_GET_NAME = 4,
    INPUT_STATE_TYPE_GET_APPEND_TEXT = 5,
    INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT,
    INPUT_STATE_TYPE_GET_NAME_INSERT_END,
    INPUT_STATE_TYPE_GET_COMMAND,
    INPUT_STATE_TYPE_COMPUTED_INPUT, // for inputs that are computed from other inputs, e.g. add child to tail when user presses TAB in get name input state
};

typedef struct {
    enum input_state_type type;
    char prefix;
    Queue *uo_queue;
} InputState;

InputState* input_state_create();

UserOperation input_convert(InputState *input_state, char key, unsigned short keyCode,
    const char *text, bool isControlDown);
#endif