#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h> // for getenv
#include <limits.h> // for PATH_MAX
#include <sys/stat.h> // for mkdir

#include "logging.h"

int debuging = 0;
char status_message[1024] = "";

static FILE *log_file = NULL;
static LogOutputMode output_mode = LOG_TO_FILE_ONLY; // default to file only
static void (*ui_message_fun)(void *ctx, const char *fmt, va_list args) = NULL;
static void *ui_message_fun_ctx = NULL;

int logging_init(char *log_file_path){
    if(log_file_path == NULL){
        log_file = stderr;
        return 0;
    }
    log_file = fopen(log_file_path, "a");
    if(log_file == NULL){
        fprintf(stderr, "Failed to open log file at %s\n", log_file_path);
        return -1;
    }
    return 0;
}

/**
 * return: 0 on success, -1 on failure
 */
int init_logging(){
    // ~/.cache/universe-mindmap/um.log
    const char *home = getenv("HOME");
    if(home == NULL){
        fprintf(stderr, "HOME environment variable not set, cannot initialize logging\n");
        return -1;
    }
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s/.cache/universe-mindmap", home);
    mkdir(log_path, 0700); // ignore EEXIST
    snprintf(log_path, sizeof(log_path), "%s/.cache/universe-mindmap/um.log", home);
    log_file = fopen(log_path, "a");
    if(log_file == NULL){
        fprintf(stderr, "Failed to open log file at %s\n", log_path);
        return -1;
    }
    return 0;
}

void logging_set_output_mode(LogOutputMode mode) {
    output_mode = mode;
}

void logging_also_to_stderr(bool enable) {
    if (enable) {
        output_mode = LOG_TO_BOTH;
    } else {
        output_mode = LOG_TO_FILE_ONLY;
    }
}

void log_debug(const char *fmt, ...) {
    if(!debuging) return;
    if(log_file == NULL && output_mode != LOG_TO_STDERR_ONLY) {
        fprintf(stderr, "Logging not initialized. Call init_logging() first.\n");
        return;
    }
	static char buf[1024];
	if (!debuging) return;
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
    
    if(output_mode == LOG_TO_FILE_ONLY || output_mode == LOG_TO_BOTH) {
        if(log_file) {
            fprintf(log_file, "[DEBUG]: %s\n", buf);
            fflush(log_file);
        }
    }
    if(output_mode == LOG_TO_STDERR_ONLY || output_mode == LOG_TO_BOTH) {
        fprintf(stderr, "[DEBUG]: %s\n", buf);
    }
}

void log_info(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(status_message, sizeof(status_message), fmt, args);
	va_end(args);
    
    if(output_mode == LOG_TO_FILE_ONLY || output_mode == LOG_TO_BOTH) {
        if(log_file) {
            fprintf(log_file, "[INFO]: %s\n", status_message);
            fflush(log_file);
        }
    }
    if(output_mode == LOG_TO_STDERR_ONLY || output_mode == LOG_TO_BOTH) {
        fprintf(stderr, "[INFO]: %s\n", status_message);
    }
}

void log_warn(const char *fmt, ...) {
    static char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    if(output_mode == LOG_TO_FILE_ONLY || output_mode == LOG_TO_BOTH) {
        if(log_file) {
            fprintf(log_file, "[WARN]: %s\n", buf);
            fflush(log_file);
        }
    }
    if(output_mode == LOG_TO_STDERR_ONLY || output_mode == LOG_TO_BOTH) {
        fprintf(stderr, "[WARN]: %s\n", buf);
    }
}

void log_error(const char *fmt, ...) {
    static char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    // Always log errors to stderr
    fprintf(stderr, "[ERROR]: %s\n", buf);

    if(output_mode == LOG_TO_FILE_ONLY || output_mode == LOG_TO_BOTH) {
        if(log_file) {
            fprintf(log_file, "[ERROR]: %s\n", buf);
            fflush(log_file);
        }
    }
}

void log_register_ui_message_fun(void (*fun)(void *ctx, const char *fmt, va_list args), void *ctx) {
    ui_message_fun = fun;
    ui_message_fun_ctx = ctx;
}

void log_ui_message(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if(ui_message_fun && ui_message_fun_ctx) {
        ui_message_fun(ui_message_fun_ctx, fmt, args);
    }

    va_end(args);
}