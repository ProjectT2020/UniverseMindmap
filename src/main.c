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
#include "operate/operate.h"
#include "app/app.h"

void usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] [database_file]\n", prog_name);
    printf("Options:\n");
    printf("  --debug, -d        Enable debug logging\n");
    printf("  --disable-wal, -w  Disable Write-Ahead Logging\n");
    printf("  --help, -h         Show this help message\n");
}

void loop(AppState *app, UiContext *ui) {
    ui_adapter_enable_raw_mode();
    
    int i = 0;
    ui_render(ui);
    while (app->running) {
        log_debug("[app_run_interactive] ------------- New Loop Iteration -----------%d", i++);
        UserOperation uo = ui_poll_user_input(ui);
        app_apply_event(app, uo);
        ui_render(ui);
    }
    
    ui_adapter_disable_raw_mode();
}

int main(int argc, char *argv[]) {
    // to support UTF-8 output in terminal
    setlocale(LC_ALL, "");
    
    // parse command line arguments
    int debug_mode = 0;
    int disable_wal_option = 0;
    
    static struct option long_options[] = {
        {"output-mq", no_argument, 0, 'o'},
        {"debug", no_argument, 0, 'd'},
        {"disable-wal", no_argument, 0, 'w'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "odwh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                operate_output_ai_message();
                return 0;
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
    AppState* app = app_init(db_file);
    // initialize UI context
    int width, height;
    ui_adapter_get_terminal_size(&width, &height);
    UiContext *ui = ui_context_create(width, height);
    ui->app = app;
    ui->overlay = app->tree_overlay;
    log_register_ui_message_fun(ui_message_fun, ui);
    app->ui_ctx = ui;
    app->ui_center_view_on_current = ui_center_view_on_current;
    app->ui_place_current_left = ui_place_current_left;
    app->ui_place_current_right = ui_place_current_right;
    app->ui_view_move = ui_view_move;
    app->ui_view_down = ui_view_down;
    app->ui_view_up = ui_view_up;
    app->ui_view_next_page = ui_view_next_page;
    app->ui_view_prev_page = ui_view_prev_page;
    app->ui_reset_layout = ui_reset_layout;
    app->ui_render = ui_render;
    app->ui_get_search_query = ui_get_search_query;
    app->ui_get_search_backward_query = ui_get_search_backward_query;
    

    // config wal
    if (disable_wal_option) {
        log_debug("WAL disabled by command line option");
        disable_wal = 1;
        app->wal->sync_count_interval = 0;
        app->wal->sync_time_interval = 0;
    } else {
        app->wal->sync_count_interval = 1; // sync every record
        app->wal->sync_time_interval = 0; // disable time interval
    }

    loop(app, ui);
    app_shutdown(app);


    return 0;
}
