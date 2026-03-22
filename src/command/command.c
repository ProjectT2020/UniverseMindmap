

#include <string.h>
#include <stdlib.h>

#include "utils/logging.h"
#include "command.h"

typedef struct {
    const char *name;
    MindCommandType type;
    int arg_count;
} CmdMapping;

static const CmdMapping cmd_mapping[] = {
    {"c", CMD_COUNT_NODES},
    {"count", CMD_COUNT_NODES},
    {"enable hide", CMD_ENABLE_HIDE},
    {"disable hide", CMD_DISABLE_HIDE},
    {"flag+hidden", CMD_SET_FLAG_HIDDEN},
    {"flag-hidden", CMD_UNSET_FLAG_HIDDEN},
    {"flag+show_hidden_children", CMD_SET_FLAG_SHOW_HIDDEN_CHILDREN},
    {"flag-show_hidden_children", CMD_UNSET_FLAG_SHOW_HIDDEN_CHILDREN},
    {"info node", CMD_INFO_NODE},
    {"shell above", CMD_SHELL_ABOVE},
    {"shell pause", CMD_SHELL_PAUSE},
    {"sp", CMD_SHELL_PAUSE},
    {"shell resume", CMD_SHELL_RESUME},
    {"sr", CMD_SHELL_RESUME},
    {"send command", CMD_SEND_COMMAND},
    {"sc", CMD_SEND_COMMAND},
    // {"shell right", CMD_SHELL_RIGHT},


    // file
    {"export", CMD_EXPORT_MINDMAP, 1},
    {"import", CMD_IMPORT_MINDMAP, 1},

    {"add", CMD_ADD_NODE},
    {"e", CMD_EDIT_NODE},
    {"edit", CMD_EDIT_NODE},
    {"delete", CMD_DELETE_NODE},
    {"fold", CMD_FOLD_NODE},
    {"unfold", CMD_UNFOLD_NODE},
    {"new task", CMD_NEW_TASK},
    // Add more command mappings as needed

    // debug
    {"reset", CMD_RESET_LAYOUT},
    {"fix view", CMD_DEBUG_FIX_VIEW},
    {"debug delete", CMD_DEBUG_DELETE},

    {NULL, CMD_UNKNOWN} // Sentinel value
};

MindCommand command_parse_command(char *command){
    MindCommand cmd;
    cmd.type = CMD_UNKNOWN;
    for(int i = 0; cmd_mapping[i].name != NULL; i++){
        if(strcmp(command, cmd_mapping[i].name) == 0){
            cmd.type = cmd_mapping[i].type;
            cmd.arg_count = cmd_mapping[i].arg_count;
            if(cmd.arg_count != 0){
                log_warn("command_parse_command: Command '%s' requires %d arguments, but none provided", 
                    cmd_mapping[i].name, cmd.arg_count);
                cmd.type = CMD_UNKNOWN;
            }
            break;
        }
        if(strstr(command, cmd_mapping[i].name) == command){
            // command starts with mapping name
            size_t name_len = strlen(cmd_mapping[i].name);
            if(command[name_len] == ' '){
                cmd.type = cmd_mapping[i].type;
                cmd.arg_count = cmd_mapping[i].arg_count;
                if(cmd.arg_count != 0){
                    // read arguments
                    char *args_start = command + name_len + 1;
                    cmd.args = args_start;
                    if(strlen(args_start) == 0){
                        // not enough arguments
                        cmd.type = CMD_UNKNOWN;
                    }
                    return cmd;
                }
                break;
            }
        }
    }
    return cmd;
}