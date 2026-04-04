#ifndef LOGGING_H
#define LOGGING_H

#include <stdbool.h>
#include <stdarg.h>

typedef enum {
    LOG_TO_FILE_ONLY,   // log to file only
    LOG_TO_STDERR_ONLY, // log to stderr only
    LOG_TO_BOTH         // log to both file and stderr
} LogOutputMode;

extern int debuging;
extern char status_message[1024];

// init logging, default to ~/.cache/universe-mindmap/um.log
int init_logging();

// log_file_path: NULL log to stderr, otherwise log to the specified file
int logging_init(char *log_file_path);

void logging_set_output_mode(LogOutputMode mode);

void logging_also_to_stderr(bool enable);

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_warn(const char *fmt, ...);

void log_register_ui_message_fun(void (*fun)(void *ctx, const char *fmt, va_list args), void *ctx) ;
void log_ui_message(const char *fmt, ...) ;

#endif // LOGGING_H