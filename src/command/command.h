
#ifndef COMMAND_H
#define COMMAND_H

typedef enum {
    CMD_UNKNOWN,
    CMD_COUNT_NODES,
    CMD_ENABLE_HIDE,
    CMD_DISABLE_HIDE,
    CMD_SET_FLAG_HIDDEN,
    CMD_UNSET_FLAG_HIDDEN,
    CMD_SET_FLAG_SHOW_HIDDEN_CHILDREN,
    CMD_UNSET_FLAG_SHOW_HIDDEN_CHILDREN,
    CMD_INFO_NODE,
    // file
    CMD_EXPORT_MINDMAP,
    CMD_IMPORT_MINDMAP,
    
    CMD_ADD_NODE,
    CMD_EDIT_NODE,
    CMD_DELETE_NODE,
    CMD_FOLD_NODE,
    CMD_UNFOLD_NODE,
    CMD_NEW_TASK,
    // Add more command types as needed

    // shell
    CMD_SHELL_ABOVE,
    CMD_SHELL_PAUSE,
    CMD_SHELL_RESUME,
    CMD_SEND_COMMAND,

    // debug
    CMD_RESET_LAYOUT
    ,CMD_DEBUG_FIX_VIEW
    ,CMD_DEBUG_DELETE
} MindCommandType;

typedef struct {
    MindCommandType type;
    int arg_count;
    char *args;
} MindCommand;

MindCommand command_parse_command(char *command);

#endif // COMMAND_H