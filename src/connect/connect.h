
#ifndef CONNECT_H
#define CONNECT_H

typedef struct {
    bool pause;
    char pane_id[64];
    char pipe_file[128];
    FILE *pipe_file_handle;
} ConnectContext;   

int current_tmux_pane_zoomed(bool *is_zoomed) ;

ConnectContext *connect_context_create();
void connect_context_destroy(ConnectContext *ctx);
ConnectContext *connect_create_shell_above(const char *shell_command);
bool connect_is_connected(ConnectContext *ctx);
int connect_send_command(ConnectContext *ctx, const char *command);

#endif 