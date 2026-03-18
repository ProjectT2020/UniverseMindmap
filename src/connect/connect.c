#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>   
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../utils/logging.h"
#include "connect.h"

int current_tmux_pane_zoomed(bool *is_zoomed) {
    // tmux display-message -p '#{window_zoomed_flag}'
    FILE *fd = popen("tmux display-message -p '#{window_zoomed_flag}'", "r");
    if (fd == NULL) {
        log_error("Failed to check if current tmux pane is zoomed");
        return -1;
    }
    char buffer[16];
    if (fgets(buffer, sizeof(buffer), fd) == NULL) {
        log_error("Failed to read tmux zoomed flag");
        pclose(fd);
        return -1;
    }
    pclose(fd);
    *is_zoomed = (buffer[0] == '1');
    return 0;
}

ConnectContext *connect_context_create() {
    ConnectContext *ctx = (ConnectContext *)calloc(1, sizeof(ConnectContext));
    return ctx;
}
void connect_context_destroy(ConnectContext *ctx) {
    if (ctx) {
        if (ctx->pipe_file_handle) {
            fclose(ctx->pipe_file_handle);
        }
        if(ctx->pipe_file[0] != '\0') {
            unlink(ctx->pipe_file);
        }
        free(ctx);
    }
}

static bool is_single_word_command(const char *command) {
    // Check if the command contains any whitespace
    for (const char *p = command; *p; p++) {
        if ('a' <= *p && *p <= 'z' || 'A' <= *p && *p <= 'Z' || '0' <= *p && *p <= '9' || *p == '-' || *p == '_') {
            continue; // valid character for a single word command
        } else {    
            return false;
        }
    }
    return true;
}

ConnectContext *connect_create_shell_above(const char *shell_command) {
    ConnectContext *ctx = connect_context_create();
    // create temp file to store new pane id
    char tmp_file_template[] = "/tmp/um_new_pane_id.XXXXXX";
    log_debug("Creating temporary file for new pane ID with template: %s", tmp_file_template);
    int fd = mkstemp(tmp_file_template);
    if (fd == -1) {
        log_error("Failed to create temporary file for pane ID");
        connect_context_destroy(ctx);
        return NULL;
    }
    close(fd);
    // store the template name in ctx->pane_id for later use
    strncpy(ctx->pane_id, tmp_file_template, sizeof(ctx->pane_id) - 1);
    ctx->pane_id[sizeof(ctx->pane_id) - 1] = '\0';
    // create new tmux pane and write its id to the temp file
    char command[256];
    if(!is_single_word_command(shell_command)) {
        shell_command = "sh";
    }
    snprintf(command, sizeof(command),
        "tmux split-window -v -b -t \"$TMUX_PANE\" -P -F \"#{pane_id}\" %s > %s",
        shell_command,
        tmp_file_template);
    log_debug("Running command to create new tmux pane: %s", command);
    int r = system(command);
    if (r != 0) {
        log_error("Failed to create new tmux pane");
        unlink(tmp_file_template);
        connect_context_destroy(ctx);
        return NULL;
    }
    // read the new pane id from the temp file
    FILE *file = fopen(tmp_file_template, "r");
    if (!file) {
        log_error("Failed to open temporary file for reading pane ID");
        unlink(tmp_file_template);
        connect_context_destroy(ctx);
        return NULL;
    }
    if (fgets(ctx->pane_id, sizeof(ctx->pane_id), file) == NULL) {
        log_error("Failed to read pane ID from temporary file");
        fclose(file);
        unlink(tmp_file_template);
        connect_context_destroy(ctx);
        return NULL;
    }
    // remove newline character if present
    size_t len = strlen(ctx->pane_id);
    if (len > 0 && ctx->pane_id[len - 1] == '\n') {
        ctx->pane_id[len - 1] = '\0';
    }
    fclose(file);
    // delete the temporary file
    unlink(tmp_file_template);
    log_info("Created new tmux pane with ID: %s", ctx->pane_id);

    // tmux pipe-pane -I -O -t %52 'cat /tmp/tp'
    // create pipe
    char pipe_file_template[] = "/tmp/um_tmux_pipe.XXXXXX";
    char *tmp_pipe = mktemp(pipe_file_template);
    if (tmp_pipe == NULL) {
        log_error("Failed to create temporary file for pipe");
        connect_context_destroy(ctx);
        return NULL;
    }
    r = mkfifo(tmp_pipe, 0666);
    if (r != 0) {
        log_error("Failed to create named pipe");
        unlink(pipe_file_template);
        connect_context_destroy(ctx);
        return NULL;
    }
    ctx->pipe_file[0] = '\0';
    strncpy(ctx->pipe_file, tmp_pipe, sizeof(ctx->pipe_file) - 1);
    ctx->pipe_file[sizeof(ctx->pipe_file) - 1] = '\0';
    log_debug("Created pipe file for pane input: %s", ctx->pipe_file);

    // set up pipe-pane to read from the pipe file
    char pipe_command[256];
    snprintf(pipe_command, sizeof(pipe_command),
        "tmux pipe-pane -I -t %s \"cat %s\"",
        ctx->pane_id, ctx->pipe_file);
    r = system(pipe_command);
    if (r != 0) {
        log_error("Failed to set up tmux pipe-pane");
        unlink(tmp_pipe);
        connect_context_destroy(ctx);
        return NULL;
    }
    ctx->pipe_file_handle = fopen(ctx->pipe_file, "w");
    if (!ctx->pipe_file_handle) {
        log_error("Failed to open pipe file for writing");
        unlink(tmp_pipe);
        connect_context_destroy(ctx);
        return NULL;
    }

    return ctx;
}

bool connect_is_connected(ConnectContext *ctx) {
    return ctx && ctx->pipe_file_handle;
}

/**
 * tmux pipe-pane -I -t <pane_id> "cat /tmp/um_tmux_pipe.XXXXXX"
 */
int connect_send_command(ConnectContext *ctx, const char *command) {
    if (!ctx || !ctx->pipe_file_handle) {
        log_error("Invalid connection context or pipe file handle");
        return -1;
    }
    fprintf(ctx->pipe_file_handle, "%s", command);
    fflush(ctx->pipe_file_handle);

    // send \r inmediately may not work for some programs (like Copilot CLI), so we need to send an additional Enter to trigger the execution
    // tmux send-keys -t <pane_id> Enter
    char enter_command[256];
    snprintf(enter_command, sizeof(enter_command),
        "tmux send-keys -t %s Enter",
        ctx->pane_id);
    usleep(200 * 1000); // wait a bit before sending Enter to ensure the command is fully written to the pipe
    system(enter_command); // send Enter key to execute the command

    log_debug("Sent command to pane %s: %s", ctx->pane_id, command);
    return 0;
}