#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <locale.h>
#include <getopt.h>
#include <string.h>

#include "event/event.h"
#include "wal/wal.h"
#include "ui/ui.h"
#include "utils/logging.h"
#include "app/app.h"

void usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] [database_file]\n", prog_name);
    printf("Options:\n");
    printf("  --debug, -d        Enable debug logging\n");
    printf("  --disable-wal, -w  Disable Write-Ahead Logging\n");
    printf("  --help, -h         Show this help message\n");
}

int main(int argc, char *argv[]) {
    // to support UTF-8 output in terminal
    setlocale(LC_ALL, "");
    
    // parse command line arguments
    int debug_mode = 0;
    int disable_wal_option = 0;
    
    static struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {"disable-wal", no_argument, 0, 'w'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "dwh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                debug_mode = 1;
                break;
            case 'w':
                disable_wal_option = 1;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }
    
    // init database file path
    const char *db_file = "universe-mindmap.umt";
    if (optind < argc) {
        db_file = argv[optind];
    }
    
    // logging configuration
    init_logging();
    if (debug_mode) {
        debuging = 1;
    }
    log_debug("universe-mindmap application starting...");

    if(disable_wal_option){
        log_debug("WAL disabled by command line option");
    }

    // app configuration
    AppState* app_state = app_init(db_file);

    // config wal
    if (disable_wal_option) {
        log_debug("WAL disabled by command line option");
        disable_wal = 1;
        app_state->wal->sync_count_interval = 0;
        app_state->wal->sync_time_interval = 0;
    } else {
        app_state->wal->sync_count_interval = 1; // sync every record
        app_state->wal->sync_time_interval = 0; // disable time interval
    }

    app_run(app_state);
    app_shutdown(app_state);


    return 0;
}
