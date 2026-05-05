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
    INPUT_STATE_TYPE_JUMP_TO_VISIBLE_TAG,
};

typedef struct {
    enum input_state_type type;
    char prefix;
    int prefix_count;
    char key_buffer[64]; // for storing keys in prefix states
    Queue *uo_queue;
    bool mark_and_show_visible_nodes; // for jump to visible tag state, whether to mark the node and show it in the UI
} InputState;

InputState* input_state_create();

UserOperation input_convert(InputState *input_state, unsigned short key, unsigned short keyCode,
    const char *text, bool isControlDown);
#endif