#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <spawn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#include "../wal/wal.h"
#include "../ui/ui.h"
#include "../operate/operate.h"
#include "../utils/logging.h"
#include "../utils/uri_template.h"
#include "../command/command.h" 
#include "../ui/tty.h"

#include "../tree/tree_overlay.h"
#include "../tree/tree_view.h"
#include "../tree/tree_storage.h"
#include "../utils/os_specific.h"

#include "app.h"

static const char *APP_METADATA_NODE_NAME = ".metadata";
static const char *RECYCLE_BIN_NAME = "recycle_bin";
static const char *APP_META_BOOKMARK_NAME = "bookmarks";
static const char *APP_META_CURRENT_TASK = "current_task";
const char *APP_META_EDIT_HISTORY = "edit_history";
static const char *APP_META_MULTI_TASKING = "multi_tasking";
static const char *CONTEXT_META_NAME = ".meta";
static const char *CONTEXT_META_SHELL = "shell";
static const char *CONTEXT_META_WIKI_PREFIX = "wiki_prefix"; // Metadata key for wiki URL prefix
static const char *CONTEXT_META_SEARCH_TEMPLATE_PREFIX = "search_template_"; 
static const char *CONTEXT_META_CODE_PROJECT_ROOT = "project_root"; // Metadata key for code project root path
static const char *CONTEXT_META_ASK_AI_CMD = "ask_ai";
static const char *CONTEXT_META_PAGE = "page"; // page URL
static const char *CONTEXT_WIKI_TERM = "wiki"; // Parent node with text 'wiki' denotes its children as Wiki terms
static const char *CONTEXT_CODE_RESOURCE = "code"; // Parent node with text 'code' denotes its children as source code
static const char *APP_TASK_STACK_NAME = "[Task Stack]"; // A special node to hold task list

static TreeNode app_ensure_metadata_node(Operate *operate); ;
static void update_current_with_history(AppState *app, TreeNode new_position) ;
static void handle_add_child_to_tail(AppState *app, TreeNode node) ;

TreeNode app_metadata_key_node(Operate *operate, const char *key) {
    TreeOverlay *ov = operate->overlay;
    TreeNode metanode = app_ensure_metadata_node(operate);
    TreeNode child = tree_node_first_child(ov, metanode);
    while(!tree_node_is_null(child)){
        const char *text = tree_node_text(child);
        if(strcmp(text, key) == 0){
            return child;
        }
        child = tree_node_next_sibling(ov, child);
    }
    Event *event = event_create_add_first_child(
        tree_node_id(metanode),
        key
    );
    int r = operate_commit_event(operate, event);
    if(r != 0){
        log_error("failed to get metadata key: %s", key);
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode new_node = tree_find_by_id(ov, event->new_node_id);
    event_destroy(event);
    return new_node;
}


static TreeNode app_metadata_value_node(AppState *app, const char *key){
    TreeOverlay *ov = app->tree_overlay;
    TreeNode metanode = app_ensure_metadata_node(app->operate);
    TreeNode child = tree_node_first_child(ov, metanode);
    while(!tree_node_is_null(child)){
        const char *text = tree_node_text(child);
        if(strcmp(text, key) == 0){
            TreeNode value_node = tree_node_first_child(ov, child);
            if(tree_node_is_null(value_node)){
                break;
            }else{
                return value_node;
            }
        }
        child = tree_node_next_sibling(ov, child);
    }

    Event *event = event_create_add_first_child(
        tree_node_id(metanode),
        key
    );
    operate_commit_event(app->operate, event);
    Event *event_value = event_create_add_first_child(
        event->new_node_id,
        ""
    );
    operate_commit_event(app->operate, event_value);
    TreeNode value_node = tree_find_by_id(ov, event_value->new_node_id);

    event_destroy(event);
    event_destroy(event_value);

    return value_node;
}

static const char *app_metatdata_get(AppState *app, const char *key) {
    TreeNode value_node = app_metadata_value_node(app, key);
    return tree_node_text(value_node);
}

static TreeNode app_metadata_dict_keynode(AppState *app, TreeNode dict, const char *key){
    TreeOverlay *ov = app->tree_overlay;
    TreeNode child = tree_node_first_child(ov, dict);
    while(!tree_node_is_null(child)){
        const char *text = tree_node_text(child);
        if(strcmp(text, key) == 0){
            return child;
        }
        child = tree_node_next_sibling(ov, child);
    }

    Event *event = event_create_add_first_child(
        tree_node_id(dict),
        key
    );
    operate_commit_event(app->operate, event);
    TreeNode key_node = tree_find_by_id(ov, event->new_node_id);
    event_destroy(event);
    return key_node;
}

static TreeNode app_metadata_dict_valuenode(AppState *app, TreeNode dict, const char *key){
    TreeNode key_node = app_metadata_dict_keynode(app, dict, key);
    if(tree_node_is_null(key_node)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode value_node = tree_node_first_child(app->tree_overlay, key_node);
    if(tree_node_is_null(value_node)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    return value_node;
}

static int app_metadata_dict_set(AppState *app, TreeNode dict, const char *key, const char *value) {
    TreeNode keynode = app_metadata_dict_keynode(app, dict, key);
    TreeNode value_node = tree_node_first_child(app->tree_overlay, keynode);

    if(tree_node_is_null(value_node)){ // insert
        Event *event = event_create_add_first_child(
            tree_node_id(keynode),
            value
        );
        int r = operate_commit_event(app->operate, event);
        event_destroy(event);
        return r;
    }else{// update
        Event *event = event_create_update_text(
            tree_node_id(value_node),
            value
        );
        int r = operate_commit_event(app->operate, event);
        event_destroy(event);
        return r;
    }
}

static bool isspace(const char c){
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool empty_text(const char *text){
    if( text == NULL ){
        return true;
    }
    int text_len =  strlen(text);
    for(int i = 0; i < text_len; i++){
        if(!isspace(text[i])){
            return false;
        }
    }
    return true;
}

/**
 * return: value node of the key in the context
 *         TREE_NODE_NULL if not found
 */
static TreeNode context_metadata_get(AppState *app, TreeNode context, const char *key){
    if(tree_node_is_null(context)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode first_child = tree_node_first_child(app->tree_overlay, context);
    if(tree_node_is_null(first_child)){
        goto look_up_parent;
    }
    if(!strcmp(tree_node_text(first_child), CONTEXT_META_NAME) == 0){
        goto look_up_parent;
    }
    TreeNode meta_dict = first_child;
    TreeNode meta_key = tree_node_first_child(app->tree_overlay, meta_dict);
    while(!tree_node_is_null(meta_key)){
        if(strcmp(tree_node_text(meta_key), key) == 0){
            TreeNode value_node = tree_node_first_child(app->tree_overlay, meta_key);
            if(tree_node_is_null(value_node)){
                goto look_up_parent;
            }else{
                if(empty_text(tree_node_text(value_node))){
                    goto look_up_parent;
                }
                return value_node;
            }
        }
        meta_key = tree_node_next_sibling(app->tree_overlay, meta_key);
    }
    

look_up_parent:
    return context_metadata_get(app, tree_node_parent(app->tree_overlay, context), key);
}

static TreeNode ensure_node_metadata(Operate *operate, TreeOverlay *ov, TreeNode node);

static TreeNode node_metadata_ensure_key(AppState *app, TreeNode node, const char *key){
    TreeNode meta_node = ensure_node_metadata(app->operate, app->tree_overlay, node);
    return app_metadata_dict_keynode(app,  meta_node, key);
}

/**
 * return: TREE_NODE_NULL if not found
 */
static TreeNode node_metadata_get(AppState *app, TreeNode node, const char *key){
    if(tree_node_is_null(node)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode meta_node = tree_node_first_child(app->tree_overlay, node);
    if(tree_node_is_null(meta_node)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    if(strcmp(tree_node_text(meta_node), CONTEXT_META_NAME) != 0){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode meta_key = tree_node_first_child(app->tree_overlay, meta_node);
    while(!tree_node_is_null(meta_key)){
        if(strcmp(tree_node_text(meta_key), key) == 0){
            TreeNode value_node = tree_node_first_child(app->tree_overlay, meta_key);
            return value_node;
        }
        meta_key = tree_node_next_sibling(app->tree_overlay, meta_key);
    }
    return (TreeNode){.kind = TREE_NODE_NULL};
}

static TreeNode ensure_node_metadata(Operate *operate, TreeOverlay *ov, TreeNode node){
    if(tree_node_is_null(node)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode meta_node = tree_node_first_child(ov, node);
    if(tree_node_is_null(meta_node) || strcmp(tree_node_text(meta_node), CONTEXT_META_NAME) != 0){
        Event *event = event_create_add_first_child(
            tree_node_id(node),
            CONTEXT_META_NAME
        );
        operate_commit_event(operate, event);
        meta_node = tree_find_by_id(ov, event->new_node_id);
        event_destroy(event);
    }
    return meta_node;
}

static TreeNode node_metadata_set(AppState *app, TreeNode node, const char *key, const char *value){
    if(tree_node_is_null(node)){
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    TreeNode meta_node = ensure_node_metadata(app->operate, app->tree_overlay, node);

    int r = app_metadata_dict_set(app, meta_node, key, value);
    if(r != 0){
        log_error("failed to set node metadata: %s", key);
    }
    return meta_node;
}


static int app_load_current(AppState *app) {
    const char *node_id_str = app_metatdata_get(app, "current_node_id");
    if (node_id_str == NULL || strlen(node_id_str) == 0) {
        log_info("No current_node_id metadata found, defaulting to root");
        return -1;
    }
    uint64_t node_id = strtoull(node_id_str, NULL, 10);
    TreeNode node = tree_find_by_id(app->tree_overlay, node_id);
    if (tree_node_is_null(node)) {
        log_warn("Loaded current_node_id %lu not found in tree, defaulting to root", node_id);
        return -1;
    }
    app->ui->current_node = node;
    log_info("Loaded current_node_id: %lu", node_id);
    return 0;
}

AppState* app_init(const char *data_file) {
    static char lock_file_path[1024];
    snprintf(lock_file_path, sizeof(lock_file_path), "%s.lock", data_file);
    FILE *lock_file = fopen(lock_file_path, "w");
    if (lock_file) {
        fprintf(lock_file, "%d", getpid());
        fclose(lock_file);
        log_debug("Created lock file: %s", lock_file_path);
    } else {
        log_error("Failed to create lock file: %s. Exiting.", lock_file_path);
        exit(1);
    }
    // flock
    int fd = open(lock_file_path, O_RDWR);
    if (fd == -1) {
        log_error("Failed to open lock file for locking: %s. Exiting.", lock_file_path);
        exit(1);
    }
    int result = flock(fd, LOCK_EX | LOCK_NB);
    if (result != 0) {
        log_error("Failed to acquire lock on file: %s. Another instance may be running. Exiting.", lock_file_path);
        close(fd);
        exit(1);
    }
    log_debug("Acquired lock on file: %s", lock_file_path);

    AppState *app = (AppState*)malloc(sizeof(AppState));
    app->data_file_path = strdup(data_file);
    app->lock_file_path = strdup(lock_file_path);
    app->lock_file_fd = fd;

    // load or initialize tree storage, view and overlay
    bool file_exists = (access(data_file, F_OK) == 0);
    if(file_exists){
        app->tree_storage = tree_storage_open(data_file, 1);
        app->tree_view = tree_view_open(app->tree_storage);
        app->tree_overlay = tree_overlay_open(app->tree_view, data_file);
    }else{
        app->tree_storage = NULL;
        app->tree_view = NULL;
        app->tree_overlay = tree_overlay_create_empty(data_file);
    }


    // load or initialize WAL
    char *wal_path = calloc(strlen(data_file) + 5, sizeof(char));
    sprintf(wal_path, "%s.wal", data_file);
    app->wal = wal_open(wal_path);
    if(app->wal == NULL){
        log_error("Failed to open WAL file: %s", wal_path);
        free(wal_path);
        exit(1);
    }
    app->wal->checkpoint_lsn = tree_storage_get_last_saved_lsn(app->tree_storage);
    app->wal->next_lsn = app->wal->checkpoint_lsn + 1;
    free(wal_path);
    
    // replay WAL to restore latest state
    uint64_t last_lsn = 0;
    if (app->wal) {
        wal_replay(app->wal, 
            &last_lsn, 
            tree_overlay_on_replay,
            app->tree_overlay);
        log_debug("DB snapshot lsn=%lu, WAL latest lsn=%lu\n",  
            app->wal->checkpoint_lsn, last_lsn);
    }

    
    // initialize UI context
    int width, height;
    ui_adapter_get_terminal_size(&width, &height);
    app->ui = ui_context_create(width, height);
    app->ui->overlay = app->tree_overlay;
    app->ui->current_node = app->tree_overlay->root;
    log_register_ui_message_fun(ui_message_fun, app->ui);
    log_debug("[app_init] Set current_node to root: id=%lu, kind=%d", 
              tree_node_id(app->ui->current_node), app->ui->current_node.kind);
    
    // initialize operation management
    app->operate = operate_create(app->wal, app->tree_overlay);
    app->operate->ui = app->ui;

    // app state init
    app->selected_node_id = 0;  // root node
    app->running = 1;
    app->edit_buffer_size = 1024;
    app->edit_buffer = (char*)calloc(1, app->edit_buffer_size);
    
    // initialize jump history stack (browser-style: back and forward)
    app->jump_back_stack = stack_create(1024);
    app->jump_forward_stack = stack_create(1024);
    
    // metadata: current node
    app_load_current(app);
    
    return app;
}

void app_shutdown(AppState *app) {
    if (!app) return;
    
    // Save current tree state if storage exists
    if (app->tree_overlay && app->tree_storage) {
        // TODO: Implement proper snapshot saving with tree_storage API
    }
    
    // close resources
    if (app->tree_storage) tree_storage_close(app->tree_storage);
    if (app->ui) ui_context_destroy(app->ui);
    if (app->jump_back_stack) stack_destroy(app->jump_back_stack);
    if (app->jump_forward_stack) stack_destroy(app->jump_forward_stack);
    if (app->edit_buffer) free(app->edit_buffer);
    if (app->lock_file_path) {
        flock(app->lock_file_fd, LOCK_UN); // release lock
        close(app->lock_file_fd); // close file descriptor
        // remove lock file
        if (remove(app->lock_file_path) != 0) {
            log_error("Failed to remove lock file: %s", app->lock_file_path);
        } else {
            log_debug("Removed lock file: %s", app->lock_file_path);
        }
    }
    free(app);
}

/**
 * headless testing
 */
void app_step(AppState *app, UserOperation uo) {
    app_apply_event(app, uo);
}

/**
 * main loop
 */
void app_run_interactive(AppState *app) {
    ui_adapter_enable_raw_mode();
    
    int i = 0;
    ui_render(app->ui);
    while (app->running) {
        log_debug("[app_run_interactive] ------------- New Loop Iteration -----------%d", i++);
        UserOperation uo = ui_poll_user_input(app->ui);
        app_apply_event(app, uo);
        ui_render(app->ui);
    }
    
    ui_adapter_disable_raw_mode();
}

/**
 * main entry point for interactive mode
 */
void app_run(AppState *app) {
    app_run_interactive(app);
}

bool app_is_current_task(AppState *app, TreeNode node) {
    TreeNode current_task_node = app_metadata_value_node(app, APP_META_CURRENT_TASK);
    if(tree_node_is_null(current_task_node)){
        return false;
    }
    uint64_t current_task_id = strtoull(tree_node_text(current_task_node), NULL, 10);
    return current_task_id == tree_node_id(node);
}

static void handle_add_child_node(AppState *app) {
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    log_debug("[handle_add_child] Before add: current_node id=%lu, kind=%d", 
              tree_node_id(current), current.kind);

    bool is_current_task = app_is_current_task(app, current);

    Event *event = event_create_add_first_child(
        tree_node_id(current),
        "Unnamed Child"
    );
    operate_commit_event(app->operate, event);
    ui->current_node = tree_find_by_id(app->tree_overlay, event->new_node_id);
    event_destroy(event);
    ui_render(app->ui);// render to get text position
    
    char terminated_character = 0;
    handle_edit_node(app, ui->current_node, &terminated_character);
    if(terminated_character == '\t'){
        // if terminated by tab, immediately add a child node and edit it
        handle_add_child_to_tail(app, current);
    }
    
    if(is_current_task){
        // update current task
        uint64_t current_task_node_id = tree_node_id(ui->current_node);
        TreeNode context_current_task_value = context_metadata_get(app, current, APP_META_CURRENT_TASK);
        static char current_task_id_str[32];
        sprintf(current_task_id_str, "%llu", current_task_node_id);
        event = event_create_update_text(
            tree_node_id(context_current_task_value),
            current_task_id_str
        );
        int r = operate_commit_event(app->operate, event);
        if(r != 0){
            log_error("Failed to update current task metadata");
            return;
        }
        event_destroy(event);
        TreeNode app_current_task_value_node = app_metadata_value_node(app, APP_META_CURRENT_TASK); 
        event = event_create_update_text(
            tree_node_id(app_current_task_value_node),
            current_task_id_str
        );
        int r2 = operate_commit_event(app->operate, event);
        if(r2 != 0){
            log_error("Failed to update app current task metadata");
            return;
        }
        event_destroy(event);
    }
}


static void handle_as_current_task(AppState *app, TreeNode node);

void handle_add_child_to_tail(AppState *app, TreeNode node) {
    UiContext *ui = app->ui;
    TreeNode current = node;
    log_debug("[handle_add_child_to_tail] Before add: current_node id=%lu, kind=%d", 
              tree_node_id(current), current.kind);

    bool is_current_task = app_is_current_task(app, current);

    Event *event = event_create_add_last_child(
        tree_node_id(current),
        "Unnamed Child"
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_error("handle_add_child_to_tail: Failed to commit add_last_child event");
        ui_info_set_message(ui, "Failed to add child node");
        event_destroy(event);
        return;
    }
    ui->current_node = tree_find_by_id(app->tree_overlay, event->new_node_id);
    TreeNode child_node = ui->current_node;
    
    ui_render(app->ui);// render to get text position

    char terminated_character = 0;
    handle_edit_node(app, child_node, &terminated_character);
    operate_edit_history_record(app->operate, event);
    event_destroy(event);
    if(terminated_character == '\t'){
        // if terminated by tab, immediately add a child node and edit it
        handle_add_child_to_tail(app, ui->current_node);
    }

    const char *child_text = tree_node_text(child_node);// must not use ui->current_node here because current node text may be changed by handle_edit_node
    if(is_current_task && child_text != NULL && child_text[0] != '.') {
        handle_as_current_task(app, child_node);
    }
}

static void handle_add_sibling_above(AppState *app) {
    log_debug("[handle_add_sibling_above] Adding sibling above current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode first_child = tree_node_first_child(app->tree_overlay, parent);
    Event *event;
    if(tree_node_id(current) == tree_node_id(first_child)){
        event = event_create_add_first_child(
            tree_node_id(parent),
            "Unnamed Sibling"
        );
    }else{
        TreeNode prev_sibling = tree_node_prev_sibling(app->tree_overlay, current);
        if(tree_node_is_null(prev_sibling)){
            log_error("handle_add_sibling_above: Previous sibling is null unexpectedly");
            return;
        }
        event = event_create_add_sibling(
            tree_node_id(prev_sibling),
            "Unnamed Sibling"
        );
    }
    int r = operate_commit_event(app->operate, event);
    if(r == 0){// commit success
        ui->current_node = tree_find_by_id(app->tree_overlay, event->new_node_id);
        ui_render(app->ui);

        char terminated_character = 0;
        handle_edit_node(app, ui->current_node, &terminated_character);
        operate_edit_history_record(app->operate, event);
        event_destroy(event);
        if(terminated_character == '\t'){
            handle_add_child_to_tail(app, ui->current_node);
        }
    }
}

static void handle_add_sibling_below(AppState *app) {
    log_debug("[handle_add_sibling_below] Adding sibling below current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    
    Event *event = event_create_add_sibling(
        tree_node_id(current),
        "Unnamed Sibling"
    );
    int r = operate_commit_event(app->operate, event);
    if(r == 0){// commit success
        ui->current_node = tree_find_by_id(app->tree_overlay, event->new_node_id);
        ui_render(app->ui);

        char terminated_character = 0;
        handle_edit_node(app, ui->current_node, &terminated_character);
        operate_edit_history_record(app->operate, event);
        event_destroy(event);
        if(terminated_character == '\t'){
            handle_add_child_to_tail(app, ui->current_node);
        }
    }

}

void handle_edit_node(AppState *app, TreeNode node, char *terminated_character){
    UiContext *ui = app->ui;
    TreeNode current = node;
    char *name = ui_get_name(ui, terminated_character);
    if(*terminated_character == '\e'){
        // cancelled
        free(name);
        return;
    }
    Event *event = event_create_update_text(
        tree_node_id(current),
        name
    );
    int r = operate_commit_event(app->operate, event);
    // update current to reflect underlying node change
    // kind may changed to TREE_NODE_MUTABLE
    ui->current_node = tree_find_by_id(app->tree_overlay, event->node_id);
    if(r != 0){
        log_warn("handle_edit_node: Failed to commit update text event");
    }
    free(name);

}

void handle_vi_edit_node(AppState *app) {
    log_debug("[handle_vi_edit_node] Entering vi-like edit mode for current node");
    operate_edit_node(app->operate, app->ui->current_node);
    app->ui->current_node = tree_find_by_id(app->tree_overlay, tree_node_id(app->ui->current_node));
    log_debug("[handle_vi_edit_node] After edit: current_node id=%lu, kind=%d", 
              tree_node_id(app->ui->current_node), app->ui->current_node.kind);
}

void handle_mark_as_definition(AppState *app) {
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    const char *old_name = tree_node_text(current);

    char *new_name = calloc(strlen(old_name) + 2 + 1, sizeof(char)); // +1 NULL terminated
    sprintf(new_name, "[%s]", old_name);
    
    Event *event = event_create_update_text(
        tree_node_id(current),
        new_name
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("handle_mark_as_definition: Failed to commit update text event");
    } 
    ui->current_node = tree_find_by_id(app->tree_overlay, event->node_id);
    free(new_name);
}

void handle_unmark_as_definition(AppState *app) {
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    const char *old_name = tree_node_text(current);

    int old_name_len = strlen(old_name);
    if(old_name_len < 2 || old_name[0] != '[' || old_name[old_name_len - 1] != ']'){
        log_debug("handle_unmark_as_definition: Current node text is not marked as definition, skipping unmarking");
        return;
    }

    char *new_name = calloc(old_name_len - 1, sizeof(char));
    strncpy(new_name, old_name + 1, old_name_len - 2);
    
    Event *event = event_create_update_text(
        tree_node_id(current),
        new_name
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("handle_unmark_as_definition: Failed to commit update text event");
    } 
    ui->current_node = tree_find_by_id(app->tree_overlay, event->node_id);
    free(new_name);
}


static void handle_append_node_text(AppState *app) {
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    const char *old_name = tree_node_text(current);
    char terminated_character = 0;
    const char *new_name = ui_get_name_append(ui, old_name, &terminated_character);
    if(terminated_character == '\e'){
        // cancelled
        return;
    }
    Event *event = event_create_update_text(
        tree_node_id(current),
        new_name
    );
    int r = operate_commit_event(app->operate, event);
    // update current to reflect underlying node change
    // kind may changed to TREE_NODE_MUTABLE
    ui->current_node = tree_find_by_id(app->tree_overlay, event->node_id);
    if(r != 0){
        log_warn("handle_append_node_text: Failed to commit update text event");
    } 
}

void handle_join_sibling_as_child(AppState *app) {
    log_debug("[handle_join_sibling_as_child] Joining sibling as child");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode next_sibling = tree_node_next_sibling(app->tree_overlay, current);
    if(!tree_node_is_null(next_sibling)){
        TreeNode next_next_sibling = tree_node_next_sibling(app->tree_overlay, next_sibling);
        Event *event = event_create_move_to_children_tail(
            tree_node_id(next_sibling),
            tree_node_id(current),
            tree_node_id(parent),
            tree_node_id(next_next_sibling)// for undo
        );
        int r = operate_commit_event(app->operate, event);
        if(r != 0){
            log_warn("Failed to join sibling as child");
            return;
        }
    }
}

void handle_fold_node(AppState *app) {
    log_debug("[handle_fold_node] Toggling fold state of current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;

    int r = operate_fold_node(app->operate, current);
    if (r != 0) {
        log_warn("Fold/unfold operation failed");
    }
}

void handle_unfold_node(AppState *app) {
    log_debug("[handle_unfold_node] Toggling unfold state of current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;

    if(!tree_node_collapsed(current)){
        log_debug("Current node is not collapsed, no need to unfold");
        return;
    }

    Event *event = event_create_expand_node(
        tree_node_id(current)
    );
    int r = operate_commit_event(app->operate, event);
    if (r != 0) {
        log_warn("Fold/unfold operation failed");
    }
}

void handle_fold_children(AppState *app) {
    log_debug("[handle_fold_children] Folding all children of current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;

    TreeNode child = tree_node_first_child(app->tree_overlay, current);
    while(!tree_node_is_null(child)){
        if(tree_node_is_null(tree_node_first_child(app->tree_overlay, child))){
            // no children, skip
            child = tree_node_next_sibling(app->tree_overlay, child);
            continue;
        }
        Event *event = event_create_collapse_node(
            tree_node_id(child)
        );
        int r = operate_commit_event(app->operate, event);
        if (r != 0) {
            log_warn("Fold child operation failed for node id=%lu", tree_node_id(child));
        }
        child = tree_node_next_sibling(app->tree_overlay, child);
    }
    
    Event *unfold_event = event_create_expand_node(
        tree_node_id(current)
    );
    int r0 = operate_commit_event(app->operate, unfold_event);
    if (r0 != 0) {
        log_warn("Unfold parent operation failed for node id=%lu", tree_node_id(current));
    }
    ui_center_view_on_current(app->ui);
}

void handle_reduce_folding(AppState *app) {
    log_debug("[handle_reduce_folding] Reducing folding (expanding one more level) of current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    // get current foldlevel
    TreeNode fold_level_node = node_metadata_get(app, current, "fold_level");
    int fold_level = 0;
    if(!tree_node_is_null(fold_level_node) && !tree_node_collapsed(current)){
        fold_level = atoi(tree_node_text(fold_level_node));
    }
    fold_level++;

    operate_reduce_folding(app->operate, current, fold_level);

    char *fold_level_str = calloc(20, sizeof(char));
    sprintf(fold_level_str, "%d", fold_level);
    node_metadata_set(app, current, "fold_level", fold_level_str);
    free(fold_level_str);

    ui_center_view_on_current(app->ui);
}

void handle_fold_level1(AppState *app){
    log_debug("[handle_fold_level1] Setting fold level 1 of current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    
    handle_unfold_node(app);
    TreeNode child = tree_node_first_child(app->tree_overlay, current);
    while(!tree_node_is_null(child)){
        operate_fold_node(app->operate, child);
        child = tree_node_next_sibling(app->tree_overlay, child);
    }

    // node_metadata_set(app, current, "fold_level", "1");
    ui_center_view_on_current(app->ui);
}

void handle_fold_and_move_to_child(AppState *app) {
    while(true){
        TreeNode current = app->ui->current_node;
        TreeNode child = tree_node_first_child(app->tree_overlay, current);
        if(tree_node_is_null(child)){
            goto end;
        }
        handle_fold_level1(app);
        app->ui->show_child_position = true;
        ui_render(app->ui);
        char next = getchar();
        int pos = 0;
        if('0' <= next && next <= '9'){
            pos = next - '0';
        } else if('a' <= next && next <= 'z'){
            pos = next - 'a' + 10; // 10-35 for a-z
        } else if('A' <= next && next <= 'Z'){
            pos = next - 'A' + 36; // 36-61 for A-Z
        } else if(next == 0x1b){ // ESC
            goto end;
        } else {
            goto end;
        }
        ui_move_focus_child_position(app->ui, pos);
    };
end:
    app->ui->show_child_position = false;
    return;
}

void handle_fold_more(AppState *app) {
    log_debug("[handle_fold_more] Folding more (increasing fold level) of current node");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    handle_unfold_node(app);// ensure current node is unfolded before folding more
    // get current foldlevel
    TreeNode fold_level_node = node_metadata_get(app, current, "fold_level");
    int fold_level = 1;
    if(!tree_node_is_null(fold_level_node)){
        fold_level = atoi(tree_node_text(fold_level_node));
    }
    if(fold_level < 1){
        log_debug("Current fold level is %d, cannot fold more", fold_level);
        return;
    }
    if(fold_level > 1){
        fold_level--;
    }

    operate_fold_more(app->operate, current, fold_level);

    char *fold_level_str = calloc(20, sizeof(char));
    sprintf(fold_level_str, "%d", fold_level);
    node_metadata_set(app, current, "fold_level", fold_level_str);
    free(fold_level_str);

    ui_center_view_on_current(app->ui);
}

void handle_undo(AppState *app) {
    log_debug("[handle_undo] Performing undo operation");
    Event *last_event = NULL;
    int r = operate_undo(app->operate, &last_event);
    if (r != 0) {
        log_warn("Undo operation failed");
    }else if(last_event != NULL){
        uint64_t event_node_id = 0;
        switch(last_event->type){
            case EVENT_UPDATE_TEXT:
            case EVENT_MOVE_SUBTREE: {
                    event_node_id = last_event->node_id;
                break;
            }
            default:
                break;
        }
        if(event_node_id != 0){
            TreeNode node = tree_find_by_id(app->tree_overlay, event_node_id);
            if(!tree_node_is_null(node)){
                app->ui->current_node = node;
            }
        }
        event_destroy(last_event);
    }
}

void handle_redo(AppState *app) {
    log_debug("[handle_redo] Performing redo operation");
    int r = operate_redo(app->operate);
    if (r != 0) {
        log_warn("Redo operation failed");
    }
}

void handle_copy_subtree(AppState *app) {
    log_debug("[handle_copy_subtree] Copying current subtree to clipboard");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    int r = operate_copy_subtree(app->operate, current);
    if (r != 0) {
        log_warn("Copy subtree operation failed");
        return;
    }

    log_debug("[handle_copy_subtree] Copied node id=%lu to clipboard", tree_node_id(current));
}

void handle_paste_as_child(AppState *app) {
    log_debug("[handle_paste_as_child] Pasting clipboard content as child");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;

    switch(app->operate->clipboard_mode ){
        case CLIPBOARD_EMPTY:{
            log_warn("Clipboard is empty, cannot paste");
            return;
        }
        case CLIPBOARD_CUT:{
            TreeNode old_parent = app_metadata_key_node(app->operate, RECYCLE_BIN_NAME);
            TreeNode content_node = tree_node_first_child(app->tree_overlay, old_parent);
            if(tree_node_is_null(content_node)){
                log_error("handle_paste_as_child: Clipboard content node not found");
                return;
            }
            TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, content_node);

            TreeNode new_next_sibling = (TreeNode){ .kind = TREE_NODE_NULL }; // insert as last child
            Event *event = event_create_move_subtree(
                tree_node_id(app->operate->clipboard), // node to move
                tree_node_id(old_parent),
                tree_node_id(old_next_sibling),
                tree_node_id(current), // move to current's children
                tree_node_id(new_next_sibling) // new next sibling
            );
            operate_commit_event(app->operate, event);
            app->operate->clipboard_mode = CLIPBOARD_EMPTY;
            break;
        }
        case CLIPBOARD_COPY:{
            TreeNode recycle_bin = app_metadata_key_node(app->operate, RECYCLE_BIN_NAME);
            int r = operate_copy_paste_as_first_child(app->operate, recycle_bin);
            if (r != 0) {
                log_warn("Paste (copy) operation failed");
                return;
            }
            TreeNode copied_node = tree_node_first_child(app->tree_overlay, recycle_bin);
            Event *event = event_create_move_subtree(
                tree_node_id(copied_node), // node to move
                tree_node_id(recycle_bin),
                tree_node_id(tree_node_next_sibling(app->tree_overlay, copied_node)),
                tree_node_id(current), // move to current's children
                tree_node_id((TreeNode){ .kind = TREE_NODE_NULL }) // new next sibling (insert as last child)
            );
            operate_commit_event(app->operate, event);
            break;
        }
        default:{
            log_error("handle_paste_as_child: Unknown clipboard mode %d", app->operate->clipboard_mode);
            return;
        }
    }
    log_debug("[handle_paste_as_child] Paste operation completed, max node id=%d",
        app->tree_overlay->max_node_id);
}

void handle_paste_as_sibling_below(AppState *app) {
    log_debug("[handle_paste_as_sibling_below] Pasting clipboard content as sibling below");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;

    switch(app->operate->clipboard_mode ){
        case CLIPBOARD_EMPTY:{
            log_warn("Clipboard is empty, cannot paste");
            return;
        }
        case CLIPBOARD_CUT:{
            TreeNode old_parent = app_metadata_key_node(app->operate, RECYCLE_BIN_NAME);
            TreeNode content_node = tree_node_first_child(app->tree_overlay, old_parent);
            if(tree_node_is_null(content_node)){
                log_error("handle_paste_as_sibling_below: Clipboard content node not found");
                return;
            }
            TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, content_node);

            TreeNode new_next_sibling = tree_node_next_sibling(app->tree_overlay, current);
            Event *event = event_create_move_subtree(
                tree_node_id(app->operate->clipboard), // node to move
                tree_node_id(old_parent),
                tree_node_id(old_next_sibling),
                tree_node_id(tree_node_parent(app->tree_overlay, current)), // move to current's parent
                tree_node_id(new_next_sibling) // new next sibling
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_warn("handle_paste_as_sibling_below: Failed to commit move subtree event");
                app->operate->clipboard_mode = CLIPBOARD_EMPTY;
                return;
            }
            app->operate->clipboard_mode = CLIPBOARD_EMPTY;
            break;
        }
        case CLIPBOARD_COPY:{
            TreeNode recycle_bin = app_metadata_key_node(app->operate, RECYCLE_BIN_NAME);
            int r = operate_copy_paste_as_first_child(app->operate, recycle_bin);
            if (r != 0) {
                log_warn("handle_paste_as_sibling_below: Failed to copy subtree for paste");
                return;
            }
            TreeNode copied_node = tree_node_first_child(app->tree_overlay, recycle_bin);
            TreeNode new_next_sibling = tree_node_next_sibling(app->tree_overlay, current);
            Event *event = event_create_move_subtree(
                tree_node_id(copied_node), // node to move
                tree_node_id(recycle_bin),
                tree_node_id(tree_node_next_sibling(app->tree_overlay, copied_node)),
                tree_node_id(tree_node_parent(app->tree_overlay, current)), // move to current's parent
                tree_node_id(new_next_sibling) // new next sibling
            );
            operate_commit_event(app->operate, event);
            break;
        }
        default:{
            log_error("handle_paste_as_sibling_below: Unknown clipboard mode %d", app->operate->clipboard_mode);
            return;
        }
    }
    log_debug("[handle_paste_as_sibling_below] Paste operation completed, max node id=%d",
        app->tree_overlay->max_node_id);
}

void handle_paste_as_sibling_above(AppState *app) {
    log_debug("[handle_paste_as_sibling_above] Pasting clipboard content as sibling above");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;

    switch(app->operate->clipboard_mode ){
        case CLIPBOARD_EMPTY:{
            log_warn("Clipboard is empty, cannot paste");
            return;
        }
        case CLIPBOARD_CUT:{
            TreeNode old_parent = app_metadata_key_node(app->operate, RECYCLE_BIN_NAME);
            TreeNode content_node = tree_node_first_child(app->tree_overlay, old_parent);
            if(tree_node_is_null(content_node)){
                log_error("handle_paste_as_sibling_above: Clipboard content node not found");
                return;
            }
            TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, content_node);
            TreeNode new_next_sibling = current; // insert before current
            Event *event = event_create_move_subtree(
                tree_node_id(app->operate->clipboard), // node to move
                tree_node_id(old_parent),
                tree_node_id(old_next_sibling),
                tree_node_id(tree_node_parent(app->tree_overlay, current)), // move to current's parent
                tree_node_id(new_next_sibling) // new next sibling
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_warn("handle_paste_as_sibling_above: Failed to commit move subtree event");
                app->operate->clipboard_mode = CLIPBOARD_EMPTY;
                return;
            }
            app->operate->clipboard_mode = CLIPBOARD_EMPTY;
            break;
        }
        case CLIPBOARD_COPY:{
            TreeNode recycle_bin = app_metadata_key_node(app->operate, RECYCLE_BIN_NAME);
            int r = operate_copy_paste_as_first_child(app->operate, recycle_bin);
            if (r != 0) {
                log_warn("handle_paste_as_sibling_above: Failed to copy subtree for paste");
                return;
            }
            TreeNode copied_node = tree_node_first_child(app->tree_overlay, recycle_bin);
            TreeNode new_next_sibling = current; // insert before current
            Event *event = event_create_move_subtree(
                tree_node_id(copied_node), // node to move
                tree_node_id(recycle_bin),
                tree_node_id(tree_node_next_sibling(app->tree_overlay, copied_node)),
                tree_node_id(tree_node_parent(app->tree_overlay, current)), // move to current's parent
                tree_node_id(new_next_sibling) // new next sibling
            );
            operate_commit_event(app->operate, event);  
            break;
        }
        default:{
            log_error("handle_paste_as_sibling_above: Unknown clipboard mode %d", app->operate->clipboard_mode);
            return;
        }
    }
    log_debug("[handle_paste_as_sibling_above] Paste operation completed, max node id=%d",
        app->tree_overlay->max_node_id);
}

static void update_current_before_delete(AppState *app, TreeNode deleted_node) {
    UiContext *ui = app->ui;
    TreeNode old_prev_sibling = tree_node_prev_sibling(app->tree_overlay, deleted_node);
    TreeNode old_parent = tree_node_parent(app->tree_overlay, deleted_node);
    TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, deleted_node);
    if(!tree_node_is_null(old_next_sibling)){
        ui->current_node = old_next_sibling;
    } else if(!tree_node_is_null(old_prev_sibling)){
        ui->current_node = old_prev_sibling;
    }else{
        ui->current_node = old_parent;
    }
}

void handle_delete_subtree(AppState *app) {
    log_warn("[handle_delete_subtree] Deleting current subtree");
    TreeNode old_current = app->ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, old_current);
    if(tree_node_is_null(parent)){
        log_warn("Cannot delete root node");
        return; 
    }
    if(strcmp(tree_node_text(parent), RECYCLE_BIN_NAME) != 0){
        log_debug("can't delete directly, move to recycle bin first");
        ui_info_set_message(app->ui, "Can't delete directly, move to recycle bin first.");
        return;
    }

    update_current_before_delete(app, old_current);

    int r = operate_delete_subtree(app->operate, old_current);
    if(r != 0){
        log_warn("Delete subtree operation failed");
        return;
    }
}

void handle_cut_subtree(AppState *app) {
    log_debug("[handle_cut_subtree] Deleting current subtree");
    UiContext *ui = app->ui;
    TreeNode current = ui->current_node;
    TreeNode old_prev_sibling = tree_node_prev_sibling(app->tree_overlay, current);

    TreeNode old_parent = tree_node_parent(app->tree_overlay, current);
    TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, current);
    TreeNode new_parent = app_metadata_key_node(app->operate, "recycle_bin");
    TreeNode new_next_sibling = tree_node_first_child(app->tree_overlay, new_parent);
    Event *event = event_create_move_subtree(
        tree_node_id(current),
        tree_node_id(old_parent),
        tree_node_id(old_next_sibling),
        tree_node_id(new_parent),
        tree_node_id(new_next_sibling)
    );
    int r = operate_commit_event(app->operate, event);
    if (r != 0) {
        log_warn("Delete subtree operation failed");
        return;
    }
    app->operate->clipboard_mode = CLIPBOARD_CUT;
    app->operate->clipboard = tree_find_by_id(app->tree_overlay, tree_node_id(current));

    // Move focus to sibling or parent
    if (!tree_node_is_null(old_next_sibling)) {
        ui->current_node = old_next_sibling;
    } else if(!tree_node_is_null(old_prev_sibling)){
        ui->current_node = old_prev_sibling;
    }else{
        ui->current_node = old_parent;
    }
}

void handle_cut_node(AppState *app){
    TreeNode curr = app->ui->current_node;
    TreeNode child = tree_node_first_child(app->tree_overlay, curr);
    if(strcmp(tree_node_text(child), ".meta") == 0){
        child = tree_node_next_sibling(app->tree_overlay, child);
    }
    // move all children above current
    while(!tree_node_is_null(child)){
        // child will be invalid after move, so get next first
        TreeNode next_child = tree_node_next_sibling(app->tree_overlay, child);
        TreeNode old_parent = tree_node_parent(app->tree_overlay, child);
        TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, child);
        TreeNode new_parent = tree_node_parent(app->tree_overlay, curr);
        TreeNode new_next_sibling = curr;
        Event *event = event_create_move_subtree(
            tree_node_id(child),
            tree_node_id(old_parent),
            tree_node_id(old_next_sibling),
            tree_node_id(new_parent),
            tree_node_id(new_next_sibling)
        );
        int r = operate_commit_event(app->operate, event);
        if (r != 0) {
            log_warn("Cut child node operation failed for node id=%lu", tree_node_id(child));
            return;
        }
        child = next_child;
    }
    handle_cut_subtree(app);
}

static void handle_copy_text_to_system_clipboard(AppState *app) {
    const char *text = tree_node_text(app->ui->current_node);
    log_debug("[handle_copy_text_to_system_clipboard] Copying text to system clipboard: %s", text);
    #if defined(__APPLE__)
    // if macOS, use pbcopy
    FILE *pbcopy = popen("pbcopy", "w");
    if (pbcopy == NULL) {
        log_error("Failed to open pbcopy");
        return;
    }
    fwrite(text, sizeof(char), strlen(text), pbcopy);
    pclose(pbcopy);
    ui_info_set_message(app->ui, "Copied to system clipboard");
    #elif defined(__linux__)
    log_debug("handle_copy_text_to_system_clipboard: Linux clipboard copy not implemented yet");
    #else
    log_debug("handle_copy_text_to_system_clipboard: Clipboard copy not implemented for this OS");
    #endif

}

static void handle_copy_subtree_to_system_clipboard(AppState *app) {
    log_debug("[handle_copy_subtree_to_system_clipboard] Copying subtree to system clipboard");
    operate_export_mindmap_to_clipboard_txt(app->operate, app->ui->current_node);
    ui_info_set_message(app->ui, "Subtree copied to system clipboard");
}

static void handle_join_text_without_space(AppState *app) {
    log_debug("[handle_join_text_without_space] Joining text with next sibling without space"); 
    TreeNode current = app->ui->current_node;
    TreeNode next_sibling = tree_node_next_sibling(app->tree_overlay, current);
    if(tree_node_is_null(next_sibling)){
        log_warn("No next sibling to join with");
        return;
    }
    if(!tree_node_is_null(tree_node_first_child(app->tree_overlay, next_sibling))){
        log_info("Next sibling has children, cannot join text without space");
        ui_info_set_message(app->ui, "Next sibling has children, cannot join text without space");
        return;
    }
    // update text of current node
    const char *current_text = tree_node_text(current);
    const char *next_text = tree_node_text(next_sibling);
    char *joined_text = (char*)malloc(strlen(current_text) + strlen(next_text) + 1);
    strcpy(joined_text, current_text);
    strcat(joined_text, next_text);
    Event *edit_event = event_create_update_text(
        tree_node_id(current),
        joined_text
    );
    int r = operate_commit_event(app->operate, edit_event);
    free(joined_text);
    if(r != 0){
        log_warn("Failed to update text for join text without space");
        return;
    }
    app->ui->current_node = 
        tree_find_by_id(app->tree_overlay, edit_event->node_id);

    // delete next sibling (move to recycle bin)
    TreeNode old_parent = tree_node_parent(app->tree_overlay, next_sibling);
    TreeNode old_next_sibling = tree_node_next_sibling(app->tree_overlay, next_sibling);
    TreeNode new_parent = app_metadata_key_node(app->operate, "recycle_bin");
    TreeNode new_next_sibling = tree_node_first_child(app->tree_overlay, new_parent);
    Event *delete_event = event_create_move_subtree(
        tree_node_id(next_sibling),
        tree_node_id(old_parent),
        tree_node_id(old_next_sibling),
        tree_node_id(new_parent),
        tree_node_id(new_next_sibling)
    );
    int r2 = operate_commit_event(app->operate, delete_event);
    if(r2 != 0){
        log_warn("Failed to delete next sibling after joining text without space");
        return;
    }
    log_debug("[handle_join_text_without_space] Join text without space completed for node id=%lu", tree_node_id(current));
}

static void handle_move_focus_prev_sibling(AppState *app) {
    log_debug("[handle_move_focus_prev_sibling] Moving focus to previous visible sibling");
    TreeNode prev = ui_previous_visible_sibling(app->ui, app->ui->current_node);
    if(!tree_node_is_null(prev)){
        update_current_with_history(app, prev);
    }
    log_debug("[handle_move_focus_prev_sibling] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_focus_next_sibling(AppState *app) {
    log_debug("[handle_move_focus_next_sibling] Moving focus to next visible sibling");
    TreeNode next = ui_next_visible_sibling(app->ui, app->ui->current_node);
    if(!tree_node_is_null(next)){
        update_current_with_history(app, next);
        log_debug("[handle_move_focus_next_sibling] After: current_node id=%lu", tree_node_id(app->ui->current_node));
    }
}

static void handle_move_focus_up(AppState *app) {
    TreeNode node = app->ui->current_node;
    while(!tree_node_is_null(node) && 
        tree_node_is_null(ui_previous_visible_sibling(app->ui, node)) ){
        node = tree_node_parent(app->tree_overlay, node);
    }
    if(tree_node_is_null(node)){
        log_info("No previous visible sibling found, staying at current node");
        ui_info_set_message(app->ui, "No previous visible sibling found");
        return;
    }else{
        if(tree_node_id(node) == tree_node_id(app->ui->current_node)){
            app->ui->current_node = ui_previous_visible_sibling(app->ui, node);
        }else{
            TreeNode prev = ui_previous_visible_sibling(app->ui, node);
            assert(!tree_node_is_null(prev));
            update_current_with_history(app, prev);
        }
        log_debug("[handle_move_focus_up] After: current_node id=%lu", tree_node_id(app->ui->current_node));
    }
}


static void handle_move_focus_down(AppState *app) {
    // todo: record in jump history
    log_debug("[handle_move_focus_down] Before: current_node id=%lu", tree_node_id(app->ui->current_node));
    TreeNode node = app->ui->current_node;
    while(!tree_node_is_null(node) && 
        tree_node_is_null(ui_next_visible_sibling(app->ui, node)) ){
        node = tree_node_parent(app->tree_overlay, node);
    }
    if(tree_node_is_null(node)){
        log_info("No next visible sibling found, staying at current node");
        ui_info_set_message(app->ui, "No next visible sibling found");
        return;
    }else{
        if(tree_node_id(node) == tree_node_id(app->ui->current_node)){
            app->ui->current_node = ui_next_visible_sibling(app->ui, node);
        }else{
            TreeNode next = ui_next_visible_sibling(app->ui, node);
            assert(!tree_node_is_null(next));
            update_current_with_history(app, next);
        }
        log_debug("[handle_move_focus_down] After: current_node id=%lu", tree_node_id(app->ui->current_node));
    }
}

static void handle_move_focus_left(AppState *app) {
    log_debug("[handle_move_focus_left] Before: current_node id=%lu", tree_node_id(app->ui->current_node));
    TreeNode parent = tree_node_parent(app->tree_overlay, app->ui->current_node);
    log_debug("[ui_move_focus_left] parent id=%lu, kind=%d", tree_node_id(parent), parent.kind);
    if (!tree_node_is_null(parent)) {
        update_current_with_history(app, parent);
    } 
    log_debug("[handle_move_focus_left] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_focus_right(AppState *app) {
    log_debug("[handle_move_focus_right] Before: current_node id=%lu", tree_node_id(app->ui->current_node));
    TreeNode current = app->ui->current_node;
    if(tree_node_collapsed(current)){
        Event *event = event_create_expand_node(
            tree_node_id(current)
        );
        int r = operate_commit_event(app->operate, event);
        if (r != 0) {
            log_warn("Auto-unfold before move right failed");
        }
    }
    ui_move_focus_right(app->ui);
    log_debug("[handle_move_focus_right] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_to_child_position(AppState *app, int position) {
    log_debug("[handle_move_to_child_position] Before: current_node id=%lu", tree_node_id(app->ui->current_node));
    if(tree_node_collapsed(app->ui->current_node)){
        Event *event = event_create_expand_node(
            tree_node_id(app->ui->current_node)
        );
        int r = operate_commit_event(app->operate, event);
        if (r != 0) {
            log_warn("Auto-unfold before move to child position failed");
        }
    }
    ui_move_focus_child_position(app->ui, position);
    app->ui->show_child_position = false;
    log_debug("[handle_move_to_child_position] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_focus_top(AppState *app) {
    log_debug("[handle_move_focus_top] Moving focus to top (root) node");
    ui_move_focus_top(app->ui);
    log_debug("[handle_move_focus_top] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}


static void handle_move_focus_bottom(AppState *app) {
    log_debug("[handle_move_focus_bottom] Moving focus to bottom (deepest) node");
    ui_move_focus_bottom(app->ui);
    log_debug("[handle_move_focus_bottom] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void do_move_fold_begin(AppState *app, TreeNode node) {
    if(tree_node_is_null(node)){
        log_info("Node is null, cannot move focus to fold begin");
        return;
    }
    TreeNode parent = tree_node_parent(app->tree_overlay, node);
    TreeNode top_sibling = tree_node_first_child(app->tree_overlay, parent);
    if(tree_node_id(node) == tree_node_id(top_sibling)){
        if(tree_node_is_null(parent)){
            log_info("Parent is null, cannot move focus to fold begin");
            return;
        }
        do_move_fold_begin(app, parent);
    }else{
        update_current_with_history(app, top_sibling);
    }
}

static void handle_move_fold_begin(AppState *app) {
    log_debug("[handle_move_fold_begin] Moving focus to first visible child of current node");
    do_move_fold_begin(app, app->ui->current_node);
    log_debug("[handle_move_fold_begin] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void do_move_fold_end(AppState *app, TreeNode node) {
    if(tree_node_is_null(node)){
        log_info("Node is null, cannot move focus to fold end");
        return;
    }
    TreeNode parent = tree_node_parent(app->tree_overlay, node);
    TreeNode bottom_sibling = tree_node_last_child(app->tree_overlay, parent);
    if(tree_node_id(node) == tree_node_id(bottom_sibling)){
        if(tree_node_is_null(parent)){
            log_info("Parent is null, cannot move focus to fold end");
            return;
        }
        do_move_fold_end(app, parent);
    }else{
        update_current_with_history(app, bottom_sibling);
    }
}

static void handle_move_fold_end(AppState *app) {
    log_debug("[handle_move_fold_end] Moving focus to last visible child of current node");
    do_move_fold_end(app, app->ui->current_node);
    log_debug("[handle_move_fold_end] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_parent_prev_sibling_begin(AppState *app) {
    log_debug("[handle_move_parent_prev_sibling_begin] Moving focus to first visible child of parent previous sibling");
    TreeNode current = app->ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode prev_sibling = ui_parent_level_prev_visible_sibling(app->ui, parent);
    if(tree_node_is_null(prev_sibling)){
        log_info("Parent node has no previous visible sibling, cannot move to parent previous sibling");
        return;
    }else{
        TreeNode sibling_begin = ui_first_visible_child(app->ui, prev_sibling);
        update_current_with_history(app, sibling_begin);
        log_info("Moved focus to first visible child of parent previous sibling");
    }
}

static void handle_move_parent_next_sibling_end(AppState *app) {
    log_debug("[handle_move_parent_next_sibling_end] Moving focus to last visible child of parent next sibling");
    TreeNode current = app->ui->current_node;

    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode next_sibling = ui_parent_level_next_visible_sibling(app->ui, parent);
    if(tree_node_is_null(next_sibling)){
        log_info("Parent node has no next visible sibling, cannot move to parent next sibling");
        return;
    }else{
        TreeNode sibling_end = ui_last_visible_child(app->ui, next_sibling);
        update_current_with_history(app, sibling_end);
        log_info("Moved focus to last visible child of parent next sibling");
    }
}

static void handle_move_parent_prev_sibling_end(AppState *app) {
    log_debug("[handle_move_parent_prev_sibling_end] Moving focus to last visible child of parent previous sibling");
    TreeNode current = app->ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode prev_sibling = ui_parent_level_prev_visible_sibling(app->ui, parent);
    if(tree_node_is_null(prev_sibling)){
        log_info("Parent node has no previous visible sibling, cannot move to parent previous sibling");
        return;
    }else{
        TreeNode sibling_end = ui_last_visible_child(app->ui, prev_sibling);
        update_current_with_history(app, sibling_end);
        log_info("Moved focus to last visible child of parent previous sibling");
    }
}

static void handle_move_parent_next_sibling_begin(AppState *app) {
    log_debug("[handle_move_parent_next_sibling_begin] Moving focus to first visible child of parent next sibling");
    TreeNode current = app->ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode next_sibling = ui_parent_level_next_visible_sibling(app->ui, parent);
    if(tree_node_is_null(next_sibling)){
        log_info("Parent node has no next visible sibling, cannot move to parent next sibling");
        return;
    }else{
        TreeNode sibling_begin = ui_first_visible_child(app->ui, next_sibling);
        update_current_with_history(app, sibling_begin);
        log_info("Moved focus to first visible child of parent next sibling");  
    }
}

static void handle_move_focus_last_child(AppState *app) {
    log_debug("[handle_move_focus_last_child] Moving focus to last child of current node");
    ui_move_focus_last_child(app->ui);
    log_debug("[handle_move_focus_last_child] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_focus_home(AppState *app) {
    
    TreeNode node = app->ui->current_node;
    if(!tree_node_has_parent(app->tree_overlay,node)){
        log_info("Already at home node, no need to move focus");
        return;
    }
    while (!tree_node_is_null(tree_node_parent(app->tree_overlay, node))) {
        node = tree_node_parent(app->tree_overlay, node);
    } 
    update_current_with_history(app, node);
    log_debug("[handle_move_focus_home] Moved focus to home (top-level) node id=%lu", tree_node_id(app->ui->current_node));
    
}

static bool is_term_definition_node(TreeNode node) {
    if(tree_node_is_null(node)){
        return false;
    }
    const char *text = tree_node_text(node);
    int len = strlen(text);
    if(len < 3 || text[0] != '[' || text[len - 1] != ']'){
        return false;
    }
    return true;
}

static void handle_move_focus_term_root(AppState *app){
    TreeNode node = app->ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, node);
    while(!tree_node_is_null(parent)){
        if(is_term_definition_node(parent)){
            update_current_with_history(app, parent);
            log_debug("[handle_move_focus_term_root] Moved focus to term root node id=%lu", tree_node_id(app->ui->current_node));
            return;
        }
        parent = tree_node_parent(app->tree_overlay, parent);
    }
    log_info("No term root found, cannot move focus to term root");
    ui_info_set_message(app->ui, "Term root not found");
}

static void handle_move_focus_most_left_upper(AppState *app){
    TreeNode node = app->ui->current_node;
    while(true){
        TreeNode left_child = tree_node_first_child(app->tree_overlay, node);
        if(tree_node_is_null(left_child)){
            break;
        }
        node = left_child;
    }
    update_current_with_history(app, node);
    log_debug("[handle_move_focus_most_left_upper] Moved focus to most left upper node id=%lu", tree_node_id(app->ui->current_node));
}

static TreeNode app_node_primary_leaf(TreeOverlay *overlay, TreeNode node){
    if(tree_node_is_null(node)){
        return node;
    }
    TreeNode child = tree_node_first_child(overlay, node);
    while(!tree_node_is_null(child)){
        node = child;
        child = tree_node_first_child(overlay, node);
    }
    return node;
}

static TreeNode app_node_last_leaf(TreeOverlay *overlay, TreeNode node){
    if(tree_node_is_null(node)){
        return node;
    }
    TreeNode child = tree_node_last_child(overlay, node);
    while(!tree_node_is_null(child)){
        node = child;
        child = tree_node_last_child(overlay, node);
    }
    return node;
}

static void handle_move_focus_most_left_lower(AppState *app){
    TreeNode node = app->ui->current_node;
    while(true){
        TreeNode right_child = tree_node_last_child(app->tree_overlay, node);
        if(tree_node_is_null(right_child)){
            break;
        }
        node = right_child;
    }
    update_current_with_history(app, node);
    log_debug("[handle_move_focus_most_left_lower] Moved focus to most left lower node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_move_focus_current_task(AppState *app) {
    TreeNode current_task_node = app_metadata_value_node(app, APP_META_CURRENT_TASK );
    if(tree_node_is_null(current_task_node)){
        log_info("No current task set, cannot move focus to current task");
        ui_info_set_message(app->ui, "No current task set");
        return;
    }
    uint64_t node_id = strtoull(tree_node_text(current_task_node), NULL, 10);
    TreeNode node = tree_find_by_id(app->tree_overlay, node_id);
    if(tree_node_is_null(node)){
        log_warn("Current task node id=%lu not found", node_id);
        ui_info_set_message(app->ui, "Current task node not found");
        return;
    }
    update_current_with_history(app, node);
    log_debug("[handle_move_focus_current_task] Moved focus to current task node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_jump_back(AppState *app) {
    log_debug("[handle_jump_back] Jumping back in history");
    if(stack_is_empty(app->jump_back_stack)){
        log_info("Jump back stack is empty, cannot jump back");
        return;
    }
    TreeNode current = app->ui->current_node;
    stack_push(app->jump_forward_stack, (void*)(uintptr_t)tree_node_id(current));

    uint64_t node_id = (uint64_t)(uintptr_t)stack_pop(app->jump_back_stack);
    TreeNode node = tree_find_by_id(app->tree_overlay, node_id);
    if(tree_node_is_null(node)){
        log_warn("Jump back target node id=%lu not found", node_id);
        return;
    }
    app->ui->current_node = node;
    log_debug("[handle_jump_back] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_jump_forward(AppState *app) {
    log_debug("[handle_jump_forward] Jumping forward in history");
    if(stack_is_empty(app->jump_forward_stack)){
        log_info("Jump forward stack is empty, cannot jump forward");
        return;
    }
    TreeNode current = app->ui->current_node;
    stack_push(app->jump_back_stack, (void*)(uintptr_t)tree_node_id(current));

    uint64_t node_id = (uint64_t)(uintptr_t)stack_pop(app->jump_forward_stack);
    TreeNode node = tree_find_by_id(app->tree_overlay, node_id);
    if(tree_node_is_null(node)){
        log_warn("Jump forward target node id=%lu not found", node_id);
        return;
    }
    app->ui->current_node = node;
    log_debug("[handle_jump_forward] After: current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_mark_node(AppState *app, UserOperation uo) {
    TreeNode bookmarks = app_metadata_key_node(app->operate, APP_META_BOOKMARK_NAME); 
    TreeNode current = app->ui->current_node;
    uint64_t current_id = tree_node_id(current);
    static char current_id_str[32];
    sprintf(current_id_str, "%llu", current_id);

    assert(uo.param1 >= '0' && uo.param1 <= '9' || (uo.param1 >= 'a' && uo.param1 <= 'z') || (uo.param1 >= 'A' && uo.param1 <= 'Z'));
    char mark_key[2];
    mark_key[0] = uo.param1;
    mark_key[1] = '\0';
    int r = app_metadata_dict_set(app, bookmarks, mark_key, current_id_str);
    if(r != 0){
        log_warn("Failed to set bookmark for key %c", mark_key);
    }else{
        log_debug("Set bookmark for key %c to node id=%lu", mark_key, current_id);
    }
    ui_info_set_message(app->ui, "Set bookmark '%c' for current node", mark_key[0]);
}

static void handle_jump_to_mark(AppState *app, UserOperation uo) {
    TreeNode bookmarks = app_metadata_key_node(app->operate, APP_META_BOOKMARK_NAME); 
    char mark_key[2];
    mark_key[0] = uo.param1;
    mark_key[1] = '\0';
    TreeNode node_id_node = app_metadata_dict_valuenode(app, bookmarks, mark_key);
    if(tree_node_is_null(node_id_node)){
        log_warn("No bookmark found for key %c", mark_key);
        return;
    }
    uint64_t node_id = strtoull(tree_node_text(node_id_node), NULL, 10);
    TreeNode node = tree_find_by_id(app->tree_overlay, node_id);
    if(tree_node_is_null(node)){
        log_warn("Bookmark target node id=%lu not found", node_id);
        return;
    }
    update_current_with_history(app, node);
    log_debug("Jumped to bookmark %c at node id=%lu", mark_key, node_id);
    ui_info_set_message(app->ui, "Jumped to bookmark '%c'", mark_key[0]);
}

static void handle_jump_to_ui_node_mark(AppState *app, UserOperation uo) {
    int mark_idx = uo.param1;
    uint64_t node_id = app->ui->node_marks[mark_idx];
    if(node_id == 0){
        log_warn("No UI node mark found for index %d", mark_idx);
        return;
    }
    TreeNode node = tree_find_by_id(app->tree_overlay, node_id);
    if(tree_node_is_null(node)){
        log_warn("UI node mark target node id=%lu not found", node_id);
        return;
    }
    update_current_with_history(app, node);
    app->ui->mark_and_show_visible_nodes = false;
    log_debug("Jumped to UI node mark %d at node id=%lu", mark_idx, node_id);
    ui_info_set_message(app->ui, "Jumped to UI node mark %d", mark_idx);
}

static void handle_index_from_root(AppState *app) {
    handle_move_focus_home(app);
    while(true){
        handle_fold_level1(app);
        app->ui->show_child_position = true;
        ui_render(app->ui);
        char next = getchar();
        int pos = 0;
        if('0' <= next && next <= '9'){
            pos = next - '0';
        } else if('a' <= next && next <= 'z'){
            pos = next - 'a' + 10; // 10-35 for a-z
        } else if('A' <= next && next <= 'Z'){
            pos = next - 'A' + 36; // 36-61 for A-Z
        } else {
            if(next == 0x1b){ // ESC key
                log_info("Index navigation cancelled");
                break;
            } 
            log_info("Unknown input sequence: f%c\n", next);
            break;
        }
        handle_move_to_child_position(app, pos);
    }
    app->ui->show_child_position = false;
    log_debug("[handle_index_from_root] Finished index navigation, current_node id=%lu", tree_node_id(app->ui->current_node));
}

static void handle_to_edit_history(AppState *app) {
    log_debug("[handle_to_edit_history] Moving focus to edit history node");
    uint64_t normal_mode_node_id = tree_node_id(app->ui->current_node);
    TreeNode edit_history_node = app_metadata_key_node(app->operate, APP_META_EDIT_HISTORY);
    bool edit_history_node_fold = tree_node_is_collapsed( edit_history_node);

    TreeNode last_edit_node = operate_edit_history_last_record(app->operate, edit_history_node);
    if(tree_node_is_null(last_edit_node)){
        log_info("Edit history is empty, cannot move focus to edit history");
        log_ui_message("Edit history is empty");
        return;
    }
    app->ui->current_node = last_edit_node;
    app->operate->mode = OPERATION_MODE_EDIT_HISTORY;
    app->operate->edit_history_node_fold = edit_history_node_fold;
    app->operate->normal_mode_node_id = normal_mode_node_id;
}

static void handle_send_command(AppState *app){
    if (app->connect == NULL) {
        log_warn("handle_send_command: No active connection to send command");
        ui_info_set_message(app->ui, "No active connection to send command");
        return;
    }
    if(app->connect->pause){
        ui_info_set_message(app->ui, "Connection is paused, cannot send command");
        return;
    }
    const char *command = tree_node_text(app->ui->current_node);
    int r = connect_send_command(app->connect, command);
    if (r != 0) {
        log_warn("handle_send_command: Failed to send command through connection");
        ui_info_set_message(app->ui, "Failed to send command through connection");
    }
    else {
        log_debug("handle_send_command: Sent command through connection: %s", command);
        ui_info_set_message(app->ui, "Sent command through connection: %s", command);
    }
}

static void handle_create_child_task(AppState *app) {
    handle_move_focus_current_task(app);
    handle_add_child_to_tail(app, app->ui->current_node);
}

static void handle_create_sibling_task(AppState *app) {
    handle_move_focus_current_task(app);
    handle_add_sibling_below(app);
}

static TreeNode task_find_current(AppState *app, TreeNode parent);

bool tree_node_not_started_with_dot(TreeNode node, void *ctx) {
    if(ctx != NULL){
        log_error("tree_node_not_started_with_dot: ctx should be null");
    }
    const char *text = tree_node_text(node);
    return text[0] != '.';
}

/**
 * check: is root task
 * do finish:
 *      mark end_time in metadata
 *      move to finished_tasks
 * find: next task
 * set current: 
 */
static void handle_finish_task(AppState *app){
    handle_move_focus_current_task(app);
    TreeNode current = app->ui->current_node;
    const char *task_name = tree_node_text(current);
    if(strcmp( APP_TASK_STACK_NAME, task_name) == 0){
        log_info("This is the root task, Enjoy your life!");
        ui_info_set_message(app->ui, "This is the root task, Enjoy your life!");
        return;
    }
    char time_str[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
    node_metadata_set(app, current, "end_time__", time_str); // align with start_time

    TreeNode next_current = tree_node_next_sibling_with_filter(
        app->tree_overlay, current, tree_node_not_started_with_dot, NULL);
    if(tree_node_is_null(next_current)){
        next_current = tree_node_prev_sibling_with_filter(app->tree_overlay, current, tree_node_not_started_with_dot, NULL);
        if(tree_node_is_null(next_current)  || strcmp(tree_node_text(next_current), CONTEXT_META_NAME) == 0){
            next_current = tree_node_parent(app->tree_overlay, current);
        }
    }
    
    if(tree_node_is_null(next_current)){
        log_error("Failed to find next current after finishing task, this should not happen");
        ui_info_set_message(app->ui, "Failed to find next current after finishing task, this should not happen");
        return;
    }
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    TreeNode finished_tasks = node_metadata_ensure_key(app, parent, "finished_tasks");
    Event *event = event_create_move_subtree(
        tree_node_id(current),
        tree_node_id(parent),
        tree_node_id(tree_node_next_sibling(app->tree_overlay, current)),
        tree_node_id(finished_tasks),
        tree_node_id(tree_node_first_child(app->tree_overlay, finished_tasks))
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("Failed to move finished task to finished_tasks");
        return;
    }

    next_current = task_find_current(app, next_current);
    handle_as_current_task(app, next_current);
    
    log_info("Finished task '%s'", task_name);
    ui_info_set_message(app->ui, "Finished task '%s'", task_name);

}

static TreeNode task_find_current(AppState *app, TreeNode parent){
    if(tree_node_is_null(parent)){
        log_error("task_find_current: parent node is null");
        return parent;
    }
    TreeNode child = tree_node_first_child_with_filter(app->tree_overlay, parent, tree_node_not_started_with_dot, NULL);
    if(tree_node_is_null(child)){
        return parent;
    }else{
        if(strcmp(tree_node_text(child), CONTEXT_META_NAME) == 0){
            child = tree_node_next_sibling_with_filter(app->tree_overlay, child, tree_node_not_started_with_dot, NULL);
            if(tree_node_is_null(child)){
                return parent;
            }
        }
    }
    return task_find_current(app, child);
}

static TreeNode task_find_next(AppState *app, TreeNode task_node) {
    TreeNode parent = tree_node_parent(app->tree_overlay, task_node);
    TreeNode sibling = tree_node_next_sibling_with_filter(app->tree_overlay, task_node, tree_node_not_started_with_dot, NULL);
    if(tree_node_is_null(sibling)){
        if(strcmp(tree_node_text(parent), APP_TASK_STACK_NAME) == 0){
            sibling = tree_node_first_child_with_filter(app->tree_overlay, parent, tree_node_not_started_with_dot, NULL);
            if(strcmp(tree_node_text(sibling), CONTEXT_META_NAME) == 0){
                sibling = tree_node_next_sibling_with_filter(app->tree_overlay, sibling, tree_node_not_started_with_dot, NULL);
            }
        }else{
            sibling = task_find_next(app, parent);
        }
    }
    return task_find_current(app, sibling);
}

static bool is_task_stack_task(TreeOverlay *tree_overlay, TreeNode node) {
    if(tree_node_is_null(node)){
        return false;
    }
    TreeNode parent = tree_node_parent(tree_overlay, node);
    while(!tree_node_is_null(parent)){
        if(strcmp(tree_node_text(parent), APP_TASK_STACK_NAME) == 0){
            return true;
        } 
        else if(strcmp(tree_node_text(parent), CONTEXT_META_NAME) == 0){ 
            return false;
        }
        parent = tree_node_parent(tree_overlay, parent);
    }
    return false;
}

static void handle_next_task(AppState *app) {
    if(!is_task_stack_task(app->tree_overlay, app->ui->current_node)){
        handle_move_focus_current_task(app);
    }
    TreeNode sibling = task_find_next(app, app->ui->current_node);    
    update_current_with_history(app, sibling);
}

static TreeNode task_find_prev(AppState *app, TreeNode task_node) {
    if(strcmp(tree_node_text(task_node), APP_TASK_STACK_NAME) == 0){
        return task_find_current(app, task_node);
    }
    TreeNode parent = tree_node_parent(app->tree_overlay, task_node);
    TreeNode sibling = tree_node_prev_sibling_with_filter(app->tree_overlay, task_node, tree_node_not_started_with_dot, NULL);
    if(tree_node_is_null(sibling) || strcmp(tree_node_text(sibling), CONTEXT_META_NAME) == 0){
        TreeNode child = tree_node_last_child_with_filter(app->tree_overlay, parent, tree_node_not_started_with_dot, NULL);
        if(tree_node_id(child) == tree_node_id(task_node)){
            return task_find_prev(app, parent);
        }
        sibling = child;
    }

    return task_find_current(app, sibling);
}

static void handle_prev_task(AppState *app) {
    TreeNode prev_task = task_find_prev(app, app->ui->current_node);
    if(tree_node_id(prev_task) == tree_node_id(app->ui->current_node)){
        log_info("No previous task found");
        ui_info_set_message(app->ui, "No previous task found");
        return;
    }
    update_current_with_history(app, prev_task);
    TreeNode current_task_value = app_metadata_value_node(app, APP_META_CURRENT_TASK);
    static char current_task_id_str[32];
    sprintf(current_task_id_str, "%llu", tree_node_id(prev_task));
    Event *event = event_create_update_text(
        tree_node_id(current_task_value),
        current_task_id_str
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("Failed to update current task after moving to previous task");
    }
    event_destroy(event);
    log_info("Moved to previous task '%s'", tree_node_text(prev_task));
    ui_info_set_message(app->ui, "Moved to previous task '%s'", tree_node_text(prev_task));
}

static void handle_as_current_task(AppState *app, TreeNode node) {
    TreeNode current = node;
    const char *task_name = tree_node_text(current);
    TreeNode current_task_value = app_metadata_value_node(app, APP_META_CURRENT_TASK);

    int r = operate_begin_transaction(app->operate);
    if(r != 0){
        log_warn("Failed to begin transaction for setting current task");
        return;
    }

    TreeNode begin_time_node = node_metadata_get(app, current, "begin_time");
    if(tree_node_is_null(begin_time_node)){
        // datetime format 2026-04-01T23:30:45+08:00
        char begin_time_str[32];
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(begin_time_str, sizeof(begin_time_str), "%Y-%m-%dT%H:%M:%S%z", tm_info);
        node_metadata_set(app, current, "begin_time", begin_time_str);
    }


    static char current_task_id_str[32];
    sprintf(current_task_id_str, "%llu", tree_node_id(current));
    Event *event = event_create_update_text(
        tree_node_id(current_task_value),
        current_task_id_str
    );
    r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("Failed to update current task");
    }
    event_destroy(event);

    r = operate_commit_transaction(app->operate);
    if(r != 0){
        log_error("Failed to commit transaction for setting current task");
        exit(1);
    }
    if(tree_node_id(current) != tree_node_id(app->ui->current_node)){
        update_current_with_history(app, current);
    }
    ui_info_set_message(app->ui, "Set current task to '%s'", task_name);
}

static void handle_ask_ai(AppState *app, UserOperation uo) {
    if(!connect_is_connected(app->connect)){
        log_info("handle_ask_ai: No active connection, cannot ask AI");
        ui_info_set_message(app->ui, "No active connection, cannot ask AI");
        return;
    }
    const char *ask_ai_cmd = "ollama run gpt-oss:120b-cloud";
    TreeNode ask_ai_cmd_node = context_metadata_get(app, app->ui->current_node, CONTEXT_META_ASK_AI_CMD);
    if(!tree_node_is_null(ask_ai_cmd_node)){
        const char *custom_cmd = tree_node_text(ask_ai_cmd_node);
        if(custom_cmd != NULL && strlen(custom_cmd) > 0){
            ask_ai_cmd = custom_cmd;
        }
    }


    switch(uo.param1){
        case QUERY_SCOPE_CURRENT_NODE:
        case QUERY_SCOPE_SUBTREE:
            operate_ask_ai(app->operate, app->ui->current_node, uo.param1);
            break;
        default:
            log_debug("[handle_ask_ai] Asking AI with unknown param %c, treating as continue", uo.param1);
    }
    const char *exe_path = os_get_executable_path();
    char command[4096];
    sprintf(command, "%s --output-mq | %s", exe_path, ask_ai_cmd);
    int r = connect_send_command(app->connect, command);
    if (r != 0) {
        log_warn("handle_ask_ai: Failed to send ask AI command through connection");
        ui_info_set_message(app->ui, "Failed to send ask AI command through connection");
    }
     else {
        log_debug("handle_ask_ai: Sent ask AI command through connection: %s", command);
        ui_info_set_message(app->ui, "Sent ask AI command through connection: %s", command);
    }
}

static void handle_shell_above(AppState *app) {
    log_debug("[handle_shell_above] Executing shell above");
    TreeNode shell = context_metadata_get(app, app->ui->current_node, CONTEXT_META_SHELL);
    const char *shell_cmd = tree_node_is_null(shell) ?
        "sh" : tree_node_text(shell);
    
    app->connect = connect_create_shell_above(shell_cmd);
    if(app->connect == NULL){
        log_warn("[handle_shell_above]: Failed to create shell above connection");
    }             
    ui_info_set_message(app->ui, "Connected to shell above, pane=%s", app->connect->pane_id);
}

TreeNode app_ensure_task_stack(AppState *app) {
    TreeNode root = app->tree_overlay->root;
    TreeNode task_stack = tree_node_first_child(app->tree_overlay, root);
    while(!tree_node_is_null(task_stack)){
        if(strcmp(tree_node_text(task_stack), APP_TASK_STACK_NAME) == 0){
            return task_stack;
        }
        task_stack = tree_node_next_sibling(app->tree_overlay, task_stack);
    }
    TreeNode first_child = tree_node_first_child(app->tree_overlay, root);
    Event *event = event_create_add_sibling(
        tree_node_id(first_child),
        APP_TASK_STACK_NAME
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("app_ensure_task_stack: Failed to create task stack node");
        return (TreeNode){.kind = TREE_NODE_NULL};
    }
    return tree_find_by_id(app->tree_overlay, event->new_node_id);
}

void handle_add_new_task(AppState *app) {
    TreeNode task_stack = app_ensure_task_stack(app);
    if(tree_node_is_null(task_stack)){
        log_error("handle_add_new_task: Failed to ensure task stack");
        return;
    }
    Event *event = event_create_add_first_child(
        tree_node_id(task_stack),
        "New Task"
    );
    int r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("handle_add_new_task: Failed to add new task node");
        return;
    }
    static char current_task_id_str[32];
    sprintf(current_task_id_str, "%llu", event->new_node_id);
    TreeNode current = tree_find_by_id(app->tree_overlay, event->new_node_id);
    event_destroy(event);
    update_current_with_history(app, current);
    ui_render(app->ui);

    TreeNode current_task_value = app_metadata_value_node(app, APP_META_CURRENT_TASK);
    event = event_create_update_text(
        tree_node_id(current_task_value),
        current_task_id_str
    );
    r = operate_commit_event(app->operate, event);
    if(r != 0){
        log_warn("handle_add_new_task: Failed to update current task text");
    }

    char tc;
    handle_edit_node(app, current, &tc);

    node_metadata_set(app, current, APP_META_CURRENT_TASK, current_task_id_str);

    log_info("Added new task with node id=%s", current_task_id_str);
}

static void handle_command_mode(AppState *app) {
    log_debug("[handle_command_mode] Switching to command mode");
    char *command = ui_get_command(app->ui);
    MindCommand cmd = command_parse_command(command);
    switch(cmd.type){
        case CMD_COUNT_NODES:{
            uint64_t count = operate_count_subtree_nodes(app->operate, app->ui->current_node);
            ui_info_set_message(app->ui, "Subtree node count: %lu", count);
            log_info("Subtree node count: %lu", count);
            break;
        }
        case CMD_ENABLE_HIDE:
            app->ui->global_enable_hide = true;
            ui_info_set_message(app->ui, "Global hide enabled");
            log_info("Global hide enabled");
            break;
        case CMD_DISABLE_HIDE:
            app->ui->global_enable_hide = false;
            ui_info_set_message(app->ui, "Global hide disabled");
            log_info("Global hide disabled");
            break;
        case CMD_SET_FLAG_HIDDEN:{
            Event *event = event_create_set_hidden(
                tree_node_id(app->ui->current_node),
                true
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_warn("handle_command_mode: Failed to set hidden flag");
            }
            break;
        }
        case CMD_UNSET_FLAG_HIDDEN:{
            Event *event = event_create_set_hidden(
                tree_node_id(app->ui->current_node),
                false
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_warn("handle_command_mode: Failed to unset hidden flag");
            }
            break;
        }
        case CMD_INFO_NODE:{
            TreeNode n = app->ui->current_node;
            bool is_hidden = tree_node_hidden(n);
            static char info_buffer[4096];
            sprintf(info_buffer, "Node ID: %llu\nHeight: %llu\nDescendents: %llu\nName: %s, Hidden: %s", 
                tree_node_id(n),
                tree_node_layout_height(app->tree_overlay, n),
                tree_node_descendents(n),
                tree_node_text(n),
                is_hidden ? "true" : "false"
            );
            ui_info_set_message(app->ui, "%s", info_buffer);
            log_info("Node info:\n%s", info_buffer);
            break;
        }
        case CMD_SET_FLAG_SHOW_HIDDEN_CHILDREN:{
            Event *event = event_create_set_show_hidden_children(
                tree_node_id(app->ui->current_node),
                true
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_warn("handle_command_mode: Failed to set show_hidden_children flag");
            }
            break;
        }
        case CMD_UNSET_FLAG_SHOW_HIDDEN_CHILDREN:{
            Event *event = event_create_set_show_hidden_children(
                tree_node_id(app->ui->current_node),
                false
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_warn("handle_command_mode: Failed to unset show_hidden_children flag");
            }
            break;
        }
        case CMD_EXPORT_MINDMAP:{
            int r = operate_export_mindmap(app->operate, cmd.args);
            if(r != 0){
                log_warn("handle_command_mode: Failed to export mindmap to %s", cmd.args);
            }
            break;
        }
        case CMD_IMPORT_MINDMAP:{
            log_debug("handle_command_mode: Importing mindmap - %s", cmd.args);
            int r = operate_import_mindmap(app->operate, cmd.args);
            if(r != 0){
                log_warn("handle_command_mode: Failed to import mindmap from %s", cmd.args);
            }
            break;
        }
        case CMD_EDIT_NODE:{
            log_debug("handle_command_mode: Editing current node");
            operate_edit_node(app->operate, app->ui->current_node);
            app->ui->current_node = tree_find_by_id(app->tree_overlay, tree_node_id(app->ui->current_node));
            break;
        }
        case CMD_NEW_TASK:{
            handle_add_new_task(app);
            break;
        }
        case CMD_RESET_LAYOUT:{
            log_debug("handle_command_mode: Resetting tree layout");
            ui_reset_layout(app->ui);
            break;
        }
        case CMD_DEBUG_FIX_VIEW:{
            log_debug("handle_command_mode: Debug fix view");
            app->ui->fix_view = true;
            break;
        }
        case CMD_DEBUG_DELETE:{
            log_debug("handle_command_mode: Debug delete node id=%s", cmd.args);
            TreeNode current = app->ui->current_node;
            TreeNode prev_sibling = tree_node_prev_sibling(app->tree_overlay, current);
            TreeNode next_sibling = tree_node_next_sibling(app->tree_overlay, current);
            if(tree_node_is_null(prev_sibling)){
                TreeNode parent = tree_node_parent(app->tree_overlay, current);
                tree_node_set_first_child(app->tree_overlay, &parent, next_sibling);
            }else{
                tree_node_set_next_sibling(app->tree_overlay, &prev_sibling, next_sibling);
            }
            break;
        }
        case CMD_SHELL_ABOVE:{
            handle_shell_above(app);
            break;
        }
        case CMD_SEND_COMMAND:{
            handle_send_command(app);
            break;
        }
        case CMD_SHELL_PAUSE:{
            if(app->connect->pause){
                log_warn("handle_command_mode: Shell is already paused");
                ui_info_set_message(app->ui, "Shell is already paused");
                break;
            }
            app->connect->pause = true;

            bool zoomed;
            int r = current_tmux_pane_zoomed(&zoomed);
            if(!zoomed){
                system("tmux resize-pane -Z");
            }
            break;
        }
        case CMD_SHELL_RESUME:{
            if(!app->connect->pause){
                log_warn("handle_command_mode: Cannot resume shell because it is not paused");
                ui_info_set_message(app->ui, "Cannot resume shell because it is not paused");
                break;
            }
            app->connect->pause = false;

            bool zoomed;
            int r = current_tmux_pane_zoomed(&zoomed);
            if(zoomed){
                system("tmux resize-pane -Z");
            }
            break;
        }
        default:{
            log_warn("handle_command_mode: Unhandled command type %d", cmd.type);
            break;
        }
    }
    free(command);
}

static void update_current_with_history(AppState *app, TreeNode new_position) {
    uint64_t current_node_id = tree_node_id(app->ui->current_node);

    stack_push(app->jump_back_stack, (void*)(uintptr_t)current_node_id);

    while (!stack_is_empty(app->jump_forward_stack)) {
        stack_pop(app->jump_forward_stack);
    }

    app->ui->current_node = new_position;
    
}

void handle_search(AppState *app){
    log_debug("[handle_search] Initiating search");
    app->operate->search_direction = SEARCH_DIRECTION_FORWARD;
    char *query = ui_get_search_query(app->ui);
    snprintf(app->operate->search_query, sizeof(app->operate->search_query), "%s", query);
    if(query == NULL || strlen(query) == 0){
        log_debug("handle_search: Empty search query, aborting");
        return;
    }
    log_debug("handle_search: Searching for query '%s'", query);
    TreeNode result = operate_search_next(app->operate, app->ui->current_node);
    if(tree_node_is_null(result)){
        ui_info_set_message(app->ui, "No more matches for '%s'", query);
        log_info("No more matches found for query '%s'", query);
    }else{
        update_current_with_history(app, result);
        ui_info_set_message(app->ui, "Found match for '%s' at node id=%lu", query, tree_node_id(result));
        log_info("Found match for query '%s' at node id=%lu", query, tree_node_id(result));
    }
    free(query);
}

void handle_search_backward(AppState *app){
    log_debug("[handle_search_backward] Initiating backward search");
    app->operate->search_direction = SEARCH_DIRECTION_BACKWARD;
    char *query = ui_get_search_backward_query(app->ui);
    snprintf(app->operate->search_query, sizeof(app->operate->search_query), "%s", query);
    if(query == NULL || strlen(query) == 0){
        log_debug("handle_search_backward: Empty search query, aborting");
        return;
    }
    log_debug("handle_search_backward: Searching backward for query '%s'", query);
    TreeNode result = operate_search_next(app->operate, app->ui->current_node);
    if(tree_node_is_null(result)){
        ui_info_set_message(app->ui, "No previous matches for '%s'", query);
        log_info("No previous matches found for query '%s'", query);
    }else{
        app->ui->current_node = result;
        ui_info_set_message(app->ui, "Found match for '%s' at node id=%lu", query, tree_node_id(result));
        log_info("Found match for query '%s' at node id=%lu", query, tree_node_id(result));
    }
    free(query);
}

void handle_search_next(AppState *app){
    Operate *operate = app->operate;
    log_debug("[handle_search_next] Searching next for query '%s'", operate->search_query);
    if(strlen(operate->search_query) == 0){
        log_debug("handle_search_next: No previous search query, aborting");
        return;
    }
    TreeNode result = operate_search_next(app->operate, app->ui->current_node);
    if(tree_node_is_null(result)){
        ui_info_set_message(app->ui, "No more matches for '%s'", operate->search_query);
        log_info("No more matches found for query '%s'", operate->search_query);
    }else{
        update_current_with_history(app, result);
        ui_info_set_message(app->ui, "Found match for '%s' at node id=%lu", operate->search_query, tree_node_id(result));
        log_info("Found match for query '%s' at node id=%lu", operate->search_query, tree_node_id(result));
    }
}

void handle_search_prev(AppState *app){
    Operate *operate = app->operate;
    log_debug("[handle_search_prev] Searching previous for query '%s'", operate->search_query);
    if(strlen(operate->search_query) == 0){
        log_debug("handle_search_prev: No previous search query, aborting");
        return;
    }
    TreeNode result = operate_search_prev(app->operate, app->ui->current_node);
    if(tree_node_is_null(result)){
        ui_info_set_message(app->ui, "No previous matches for '%s'", operate->search_query);
        log_info("No previous matches found for query '%s'", operate->search_query);
    }else{
        update_current_with_history(app, result);
        ui_info_set_message(app->ui, "Found match for '%s' at node id=%lu", operate->search_query, tree_node_id(result));
        log_info("Found match for query '%s' at node id=%lu", operate->search_query, tree_node_id(result));
    }
}

static void handle_search_engine(AppState *app) {
    TreeNode parent = tree_node_parent(app->tree_overlay, app->ui->current_node);
    const char *parent_text = tree_node_text(parent);
    char search_template_meta_key[64];
    snprintf(search_template_meta_key, sizeof(search_template_meta_key), "%s%s", CONTEXT_META_SEARCH_TEMPLATE_PREFIX, parent_text);
    TreeNode search_template_node = context_metadata_get(app, app->ui->current_node, search_template_meta_key);
    char search_template[64];
    snprintf(search_template, sizeof(search_template), "{%s}", parent_text);
    if(!tree_node_is_null(search_template_node)){
        if(strstr(tree_node_text(search_template_node), search_template) == NULL){
            log_warn("handle_search_engine: Search template for parent node '%s' does not contain variable '%s'", parent_text, search_template);
            ui_info_set_message(app->ui, "Search template for parent node '%s' does not contain variable '%s', using default search engine", parent_text, search_template);
            goto default_search_engine;
        }
        UriTemplateVar vars[] = {
            {
                .name = parent_text,
                .value = tree_node_text(app->ui->current_node)
            }
        };
        char rendered[2048];
        int r = uri_template_expand(tree_node_text(search_template_node), vars, sizeof(vars)/sizeof(vars[0]),
         rendered, sizeof(rendered));
        if(r != 0){
            log_warn("handle_search_engine: Failed to expand search template for parent node '%s'", parent_text);
            ui_info_set_message(app->ui, "Failed to expand search template for parent node '%s', using default search engine", parent_text);
            goto default_search_engine;
        }
        log_debug("[handle_search_engine] Opening URL from search template: %s", rendered);
        pid_t pid;
        char *argv[] = {
            "open",
            rendered,
            NULL
        };
        r = posix_spawnp(&pid, "open", NULL, NULL, argv, NULL);
        if (r != 0) {
            log_error("handle_search_engine: Failed to spawn process to open URL from search template");
            ui_info_set_message(app->ui, "Failed to open URL from search template");
        }
        log_debug("[handle_search_engine] Spawned process with PID: %d to open URL from search template", pid);
        return;
    }

default_search_engine:
    const char *url_format = "https://www.google.com/search?q=%.*s";
    const char *query = tree_node_text(app->ui->current_node);
    int query_len = strlen(query);
    int left_bracket_count = 0;
    for(int i = 0; 
        query[i] == '[' && query[query_len - 1 - i] == ']' && i < query_len / 2
        ; i++){
        left_bracket_count++; 
    }
    const char *trimmed_query = query + left_bracket_count;
    int trimmed_query_len = query_len - 2 * left_bracket_count;
    char url[2048];
    snprintf(url, sizeof(url), url_format, trimmed_query_len, trimmed_query);
    log_debug("[handle_search_engine] Opening URL: %.*s", trimmed_query_len, trimmed_query);
    pid_t pid;
    char *argv[] = {
        "open",
        "-a",
        "Firefox",
        "--",
        (char *)url,
        NULL
    };
    int r = posix_spawnp(&pid, "open", NULL, NULL, argv, NULL);
    if (r != 0) {
        log_error("handle_search_engine: Failed to spawn process to open URL");
        return;
    }
    log_debug("[handle_search_engine] Spawned process with PID: %d", pid);
}

static void handle_open_resource_link(AppState *app){
    TreeNode current = app->ui->current_node;
    TreeNode parent = tree_node_parent(app->tree_overlay, current);
    const char *parent_text = tree_node_text(parent);
    const char *URL = tree_node_text(app->ui->current_node);
    pid_t pid;
    char **spawn_argv;
    
    // MediaWiki style without [[]]: resource_type:resource_path
    char *colon = strchr(URL, ':');
    if(colon == NULL){
        goto not_MediaWiki_style_ref;
    }
    // no space allowed in resource type
    if(strchr(URL, ' ') != NULL && strchr(URL, ' ') < colon){
        goto not_MediaWiki_style_ref;
    }
    int resource_type_len = colon - URL;
    if(resource_type_len <= 0){
        goto not_MediaWiki_style_ref;
    }
    char resource_template_meta_key[64];
    snprintf(resource_template_meta_key, sizeof(resource_template_meta_key), "source_%.*s", resource_type_len, URL);
    char *resource_key = resource_template_meta_key + strlen("source_");
    TreeNode resource_template_node = context_metadata_get(app, current, resource_template_meta_key);
    if(tree_node_is_null(resource_template_node)){
        log_warn("No resource template found for resource type '%.*s'", resource_type_len, URL);
        goto not_MediaWiki_style_ref;
    }
    const char *resource_id = colon + 1;
    const char *resource_template = tree_node_text(resource_template_node);
    char *man_section = "";
    if(strcmp(resource_key, "section") == 0){
        log_warn("Resource type 'section' is reserved for man sections and cannot be used in resource links");
        ui_info_set_message(app->ui, "Resource type 'section' is reserved for man sections and cannot be used in resource links");
        return;
    }
    if(strcmp(resource_key, "man") == 0){
        // validate man page format: name.section
        char *dot = strchr(resource_id, '.');
        if(dot == NULL){
            log_warn("Invalid man page reference '%s', expected format 'name.section'", resource_id);
            ui_info_set_message(app->ui, "Invalid man page reference '%s', expected format 'name.section'", resource_id);
            return;
        }
        man_section = dot + 1;
        for(char *p = man_section; *p; p++){
            if('0' <= *p && *p <= '9'){
                continue;
            }else{
                log_warn("Invalid man page section '%s' in reference '%s', expected numeric section", man_section, resource_id);
                ui_info_set_message(app->ui, "Invalid man page section '%s' in reference '%s', expected numeric section", man_section, resource_id);
                return;
            }
        }
    }

    UriTemplateVar vars[] = {
        {
            .name = resource_key,
            .value = resource_id
        },
        {
            .name = "section",
            .value = man_section
        }
    };
    char rendered[4096];
    int r = uri_template_expand(resource_template, vars, sizeof(vars)/sizeof(vars[0]),
     rendered, sizeof(rendered));
    if(r != 0){
        log_warn("Failed to expand resource template for resource type '%.*s'", resource_type_len, URL);
        ui_info_set_message(app->ui, "Failed to expand resource template for resource type '%.*s'", resource_type_len, URL);
        return;
    }
    // open the rendered URL with default application
    char *argv[] = {
        "open",
        rendered,
        NULL
    };
    r = posix_spawnp(&pid, "open", NULL, NULL, argv, NULL);
    if (r != 0) {
        log_error("handle_open_resource_link: Failed to spawn process to open resource link");
        return;
    }
    log_debug("[handle_open_resource_link] Spawned process with PID: %d to open resource link", pid);
    return;

not_MediaWiki_style_ref:

// parent node determines the type of resource link
    snprintf(resource_template_meta_key, sizeof(resource_template_meta_key), "source_%s", parent_text);
    resource_key = resource_template_meta_key + strlen("source_");
    resource_template_node = context_metadata_get(app, current, resource_template_meta_key);
    if(tree_node_is_null(resource_template_node)){
        goto special_parent_type;
    }
    resource_template = tree_node_text(resource_template_node);
    char template_var_name[64];
    snprintf(template_var_name, sizeof(template_var_name), "{%s}", parent_text);
    if(strstr(resource_template, template_var_name) == NULL){
        log_warn("handle_open_resource_link: Resource template for parent node '%s' does not contain variable '%s'", parent_text, template_var_name);
        ui_info_set_message(app->ui, "Resource template for parent node '%s' does not contain variable '%s'", parent_text, template_var_name);
        goto special_parent_type;
    }
    resource_id = tree_node_text(current);
    UriTemplateVar utv[] = {
        {
            .name = resource_key,
            .value = resource_id
        }
    };
    r = uri_template_expand(resource_template, utv, sizeof(utv)/sizeof(utv[0]), rendered, sizeof(rendered));
    if(r == 0){
        char *argv[] = {
            "open",
            rendered,
            NULL
        };
        r = posix_spawnp(&pid, "open", NULL, NULL, argv, NULL);
        if (r != 0) {
            log_error("handle_open_resource_link: Failed to spawn process to open resource link with parent-based template");
            return;
        }
        log_debug("[handle_open_resource_link] Spawned process with PID: %d to open resource link with parent-based template", pid);
        return;
    }else{
        log_error("handle_open_resource_link: Failed to expand resource template based on parent node '%s'", parent_text);
        ui_info_set_message(app->ui, "Failed to expand resource template based on parent node '%s'", parent_text);
        return;
    }


// code / vi
special_parent_type:
    if(strcmp(parent_text, CONTEXT_CODE_RESOURCE) == 0){
        const char *code_path_with_line = tree_node_text(current);
        int code_path_with_line_len = strlen(code_path_with_line);
        char code_path_buf[4096];        
        code_path_buf[0] = '\0';
        char line_part[64];
        line_part[0] = '\0';
        int line_number = 0;
        const char *code_path = code_path_with_line;
        const char *colon = strrchr(code_path_with_line, ':');
        if (colon) {
            size_t len = colon - code_path_with_line;
            if(code_path_with_line_len - len > sizeof(line_part)){
                log_warn("Line part is too long in code resource link, cannot parse line number");
                ui_info_set_message(app->ui, "Line part is too long in code resource link, cannot parse line number");
                return;
            }
            strncpy(code_path_buf, code_path_with_line, len);
            code_path_buf[len] = '\0';
            code_path = code_path_buf;

            strcpy(line_part, colon + 1);
        } 

        TreeNode code_project_root = context_metadata_get(app, app->ui->current_node, CONTEXT_META_CODE_PROJECT_ROOT);
        const char *project_root = tree_node_is_null(code_project_root) ? "." : tree_node_text(code_project_root);
        // check project root exists
        if(access(project_root, F_OK) != 0){
            log_warn("Project root '%s' does not exist, cannot open code resource link", project_root);
            ui_info_set_message(app->ui, "Project root '%s' does not exist, cannot open code resource link", project_root);
            return;
        }
        static char code_full_path[4096];
        snprintf(code_full_path, sizeof(code_full_path), "%s/%s", project_root, code_path);
        if(access(code_full_path, F_OK) != 0){
            log_warn("Code resource '%s' does not exist under project root '%s', cannot open code resource link", code_path, project_root);
            ui_info_set_message(app->ui, "Code resource '%s' does not exist under project root '%s', cannot open code resource link", code_path, project_root);
            return;
        }
        static char code_full_path_with_line[4096];
        snprintf(code_full_path_with_line, sizeof(code_full_path_with_line), "%s/%s:%s", project_root, code_path, line_part);
        char *argv[] = {
            "code",
            (char*) project_root,
            "-g",
            code_full_path_with_line,
            NULL
        };
        spawn_argv = argv;
        int r = posix_spawnp(&pid, "code", NULL, NULL, spawn_argv, NULL);
        if (r != 0) {
            log_error("handle_open_resource_link: Failed to spawn process to open code resource link in code editor");
            return;
        }
    }
    else if(strcmp(parent_text, "vi") == 0){
         const char *code_path_with_line = tree_node_text(current);
        int code_path_with_line_len = strlen(code_path_with_line);
        char code_path_buf[4096];        
        code_path_buf[0] = '\0';
        char line_part[64];
        line_part[0] = '\0';
        int line_number = 0;
        const char *code_path = code_path_with_line;
        const char *colon = strrchr(code_path_with_line, ':');
        if (colon) {
            size_t len = colon - code_path_with_line;
            if(code_path_with_line_len - len > sizeof(line_part)){
                log_warn("Line part is too long in code resource link, cannot parse line number");
                ui_info_set_message(app->ui, "Line part is too long in code resource link, cannot parse line number");
                return;
            }
            strncpy(code_path_buf, code_path_with_line, len);
            code_path_buf[len] = '\0';
            code_path = code_path_buf;

            strcpy(line_part, colon + 1);
        } 

        TreeNode code_project_root = context_metadata_get(app, app->ui->current_node, CONTEXT_META_CODE_PROJECT_ROOT);
        const char *project_root = tree_node_is_null(code_project_root) ? "." : tree_node_text(code_project_root);
        // check project root exists
        if(access(project_root, F_OK) != 0){
            log_warn("Project root '%s' does not exist, cannot open code resource link", project_root);
            ui_info_set_message(app->ui, "Project root '%s' does not exist, cannot open code resource link", project_root);
            return;
        }
        static char code_full_path[4096];
        snprintf(code_full_path, sizeof(code_full_path), "%s/%s", project_root, code_path);
        if(access(code_full_path, F_OK) != 0){
            log_warn("Code resource '%s' does not exist under project root '%s', cannot open code resource link", code_path, project_root);
            ui_info_set_message(app->ui, "Code resource '%s' does not exist under project root '%s', cannot open code resource link", code_path, project_root);
            return;
        } 
        static char vim_line_arg[64];
        snprintf(vim_line_arg, sizeof(vim_line_arg), "+%s", line_part);
        pid_t pid = fork();
        if(pid == 0){
            // does not work in VS Code debug terminal for some reason, works fine in normal terminal, need to investigate
            chdir(project_root);
            execlp("vim", "vim", vim_line_arg, code_full_path, NULL);
            log_error("handle_open_resource_link: Failed to exec vim to open code resource link");
            exit(1);
        }
        // wait for child process 
        int status;
        waitpid(pid, &status, 0);
        log_info("handle_open_resource_link: Vim process exited with status %d", status);
    }
    else if(URL[0] == '#'){
        TreeNode page_node = context_metadata_get(app, current, CONTEXT_META_PAGE);
        if(tree_node_is_null(page_node)){
            log_warn("Current node is a page anchor but no page metadata found, cannot open resource link");
            ui_info_set_message(app->ui, "Current node is a page anchor but no page metadata found, cannot open resource link");
            return;
        }
        const char *page = tree_node_text(page_node);
        const char *anchor = URL + 1;
        static char url[2048];
        snprintf(url, sizeof(url), "%s#%s", page, anchor);
        log_debug("[handle_open_resource_link] Detected page anchor link, opening URL: %s", url);
        char *argv[] = {
            "open",
            "-a",
            "Firefox",
            "--",
            (char *)url,
            NULL
        };
        spawn_argv = argv;
        int r = posix_spawnp(&pid, "open", NULL, NULL, spawn_argv, NULL);
        if (r != 0) {
            log_error("handle_open_resource_link: Failed to spawn process to open page anchor link");
            return;
        }

    }
    else if(URL[0] == '[' && URL[strlen(URL) - 1] == ']'){
        // RFC; JSR; JEP; PEP; etc. links
        char *space = strchr(URL, ' ');
        if(space == NULL){
            log_warn("handle_open_resource_link: Detected potential RFC/JSR/JEP/PEP link but no space found to separate type and number, cannot open resource link");
            ui_info_set_message(app->ui, "Detected potential RFC/JSR/JEP/PEP link but no space found to separate type and number, cannot open resource link");
            return;
        }
        const char *item = URL + 1;
        int key_len = space - item;
        if(key_len <= 0){
            log_warn("handle_open_resource_link: Detected potential RFC/JSR/JEP/PEP link but no valid key found, cannot open resource link");
            ui_info_set_message(app->ui, "Detected potential RFC/JSR/JEP/PEP link but no valid key found, cannot open resource link");
            return;
        }
        char value[64];
        strncpy(value, space + 1, sizeof(value));
        value[strlen(value) - 1] = '\0'; // remove trailing ']'
        char key[64];
        strncpy(key, item, key_len);
        key[key_len] = '\0';
        if(value[0] != '\0'){
            TreeNode page_node = context_metadata_get(app, current, CONTEXT_META_PAGE);
            if(tree_node_is_null(page_node)){
                log_warn("Current node is a %s link but no page metadata found, cannot open resource link", key);
                ui_info_set_message(app->ui, "Current node is a %s link but no page metadata found, cannot open resource link", key);
                return;
            }
            const char *page = tree_node_text(page_node);
            static char url[2048];
            UriTemplateVar vars[] = {
                { key, value }
            };
            uri_template_expand(page, vars, 1, url, sizeof(url));
            char *argv[] = {
                "open",
                "-a",
                "Firefox",
                "--",
                (char *)url,
                NULL
            };
            spawn_argv = argv; 
            int r = posix_spawnp(&pid, "open", NULL, NULL, spawn_argv, NULL);
            if (r != 0) {
                log_error("handle_open_resource_link: Failed to spawn process to open %s link", key);
                return; 
            }
            log_info("handle_open_resource_link: Spawned process with PID: %d to open %s link", pid, key);
        }
    }
    else if(!tree_node_is_null(parent)){
        if(strcmp(parent_text, CONTEXT_WIKI_TERM) == 0){
            const char *term = tree_node_text(current);
            TreeNode wiki_prefix = context_metadata_get(app, app->ui->current_node, CONTEXT_META_WIKI_PREFIX);
            const char *url_format;
            if(tree_node_is_null(wiki_prefix)){
                // use default wiki prefix
                url_format = "https://en.wikipedia.org/wiki/";
            } else {
                url_format = tree_node_text(wiki_prefix);
            }
            // concat url
            static char url[2048];
            snprintf(url, sizeof(url), "%s%s", url_format, term);
            URL = url;
            log_debug("[handle_open_resource_link] Detected wiki term '%s', opening URL: %s", term, URL);
        }
        log_debug("[handle_open_resource_link] Opening URL: %s", URL);
        char *argv[] = {
            "open",
            "-a",
            "Firefox",
            "--",
            (char *)URL,
            NULL
        };
        spawn_argv = argv;
        int r = posix_spawnp(&pid, "open", NULL, NULL, spawn_argv, NULL);
        if (r != 0) {
            log_error("handle_open_resource_link: Failed to spawn process to open URL");
            return;
        }
    }
    log_debug("[handle_open_resource_link] Spawned process with PID: %d", pid);
}

/**
 * return: 0: found; 1: not found; -1: error
 */
static int handle_jump_hierachy_definition(AppState *app, TreeNode subtree_root, const char *keywords,
    bool (*filter)(TreeNode node, void *ctx), void *filter_ctx
) {
    char *next_key_word = strchr(keywords, '|');
    int keyword_len = next_key_word ? (next_key_word - keywords) : strlen(keywords);
    char search_term[256];
    snprintf(search_term, sizeof(search_term), "[%.*s]", keyword_len, keywords);
    log_debug("handle_jump_hierachy_definition: Searching for keyword '%s' in subtree rooted at node id=%lu", search_term, tree_node_id(subtree_root));
    TreeNode result = operate_search_next_in_subtree(app->operate, subtree_root, search_term, filter, filter_ctx);
    if(tree_node_is_null(result)){
        log_info("handle_jump_hierachy_definition: No match found for keyword '%s' in subtree rooted at node id=%lu", search_term, tree_node_id(subtree_root));
        return 1;
    }
    if(next_key_word){
        return handle_jump_hierachy_definition(app, result, next_key_word + 1, filter, filter_ctx);
    }else{
        update_current_with_history(app, result);
        return 0;
    }
    
}

typedef struct {
    uint64_t app_metadata_node_id;
} JumpDefinitionFilterContext;

static bool jump_definition_filter(TreeNode node, void *ctx){
    JumpDefinitionFilterContext *filter_ctx = (JumpDefinitionFilterContext*) ctx;
    uint64_t metadata_node_id = filter_ctx->app_metadata_node_id;
    return tree_node_id(node) != metadata_node_id;
}

static void handle_jump_keyword_definition(AppState *app){
    TreeNode current = app->ui->current_node;
    const char *current_text = tree_node_text(current);
    char *keys_text = strdup(current_text);
    char *hash_tag = strchr(keys_text, '#'); 
    if(hash_tag != NULL){
        hash_tag[0] = '\0';
        char *key = keys_text;
        hash_tag++;
        char *keys[2] = {
            key,
            hash_tag
        };
        TreeNode r = operate_search_hierachy_keys(app->operate, current, (const char **)keys, 2);
        free(keys_text);
        if(tree_node_is_null(r)){
            ui_info_set_message(app->ui, "No definition found for '%s'", current_text);
            log_info("No definition found for '%s'", current_text);
        }else{
            update_current_with_history(app, r);
            ui_info_set_message(app->ui, "Jumped to definition for '%s'", tree_node_text(app->ui->current_node));
            log_info("Jumped to definition for '%s'", tree_node_text(app->ui->current_node));
        }
        return;
    }
    free(keys_text);

    JumpDefinitionFilterContext filter_ctx = {
        .app_metadata_node_id = tree_node_id(app_ensure_metadata_node(app->operate))
    };
    int r = handle_jump_hierachy_definition(app, app->tree_overlay->root, tree_node_text(app->ui->current_node), jump_definition_filter, &filter_ctx);
    if(r == 0){
        app->ui->current_node = tree_find_by_id(app->tree_overlay, tree_node_id(app->ui->current_node));
        ui_info_set_message(app->ui, "Jumped to definition for '%s'", tree_node_text(app->ui->current_node));
        log_info("Jumped to definition for '%s'", tree_node_text(app->ui->current_node));
    }else if(r == 1){
        ui_info_set_message(app->ui, "No definition found for '%s'", tree_node_text(app->ui->current_node));
        log_info("No definition found for '%s'", tree_node_text(app->ui->current_node));
    }else{
        log_error("Error occurred while searching for definition for '%s'", tree_node_text(app->ui->current_node));
    }
}

static TreeNode app_ensure_metadata_node(Operate *operate) {
    TreeOverlay *ov = operate->overlay;
    TreeNode root_node_metadata = ensure_node_metadata(operate, ov, ov->root);

    // Check if metadata node exists
    TreeNode child = tree_node_first_child(ov, root_node_metadata);
    while(!tree_node_is_null(child)){
        const char *text = tree_node_text(child);
        if (strcmp(text, APP_METADATA_NODE_NAME) == 0) {
            // Metadata node already exists
            return child;
        }
        child = tree_node_next_sibling(ov, child);
    }
    // Create metadata node
    Event *event = event_create_add_first_child(
        tree_node_id(root_node_metadata),
        APP_METADATA_NODE_NAME
    );
    operate_commit_event(operate, event);
    log_debug("app_ensure_metadata_node: Created metadata node");
    return tree_find_by_id(ov, event->new_node_id);
}


static void app_save_metadata(AppState *app, const char *key, const char *value) {
    log_debug("app_save_metadata: Saving metadata key=%s, value=%s", key, value);
    TreeOverlay *ov = app->tree_overlay;
    TreeNode metanode = app_ensure_metadata_node(app->operate);
    TreeNode child = tree_node_first_child(ov, metanode);
    while(!tree_node_is_null(child)){
        const char *text = tree_node_text(child);
        if(strcmp(text, key) == 0){
            TreeNode value_node = tree_node_first_child(ov, child);
            if(tree_node_is_null(child)){
                log_error("failed to save metadata, %s=%s", key, value);
                return;
            }
            Event *event = event_create_update_text(
                tree_node_id(value_node),
                value
            );
            int r = operate_commit_event(app->operate, event);
            if(r != 0){
                log_error("failed to save metadata, %s=%s", key, value);
            }
            return;    
        }
        child = tree_node_next_sibling(ov, child);
    }
    Event *event = event_create_add_first_child(
        tree_node_id(metanode),
        key
    );
    operate_commit_event(app->operate, event);
    TreeNode kv_node = tree_find_by_id(ov, event->new_node_id);
    event_destroy(event);
    Event *event_value = event_create_add_first_child(
        tree_node_id(kv_node),
        value
    );
    operate_commit_event(app->operate, event_value);
    event_destroy(event_value);
}

static void app_save_current(AppState *app) {
    log_debug("app_save_current: Saving current tree state to storage");
    if (!app || !app->tree_overlay) {
        log_error("app_save_current: Invalid app state or missing tree overlay");
        return;
    }
    TreeNode current = app->ui->current_node;
    char tree_node_id_str[32];
    sprintf(tree_node_id_str, "%llu", tree_node_id(current));
    app_save_metadata(app, "current_node_id", tree_node_id_str);
}

/**
 * save the tree to storage, and perform WAL truncation to remove persisted entries from WAL
 */
void app_save(AppState *app) {
    log_debug("app_save: Saving tree to storage");
    if (!app || !app->tree_overlay) {
        log_error("app_save: Invalid app state or missing tree overlay");
        return;
    }

    app_save_current(app);

    int ret = tree_overlay_save(app->tree_overlay, app->data_file_path);
    if(ret != 0){
        log_error("Failed to save tree overlay to storage");
        return;
    }
    
    // For now, just handle WAL truncation
    uint64_t last_lsn = app->tree_overlay->last_applied_lsn;
    
    // Truncate WAL records already covered by the checkpoint.
    if (wal_truncate_commited(app->wal, last_lsn) != 0) {
        log_error("Failed to truncate WAL");
        return;
    }
    
    log_info("Tree saved to storage successfully, saved_lsn=%lu", last_lsn);
}

void handle_exit_save(AppState *app) {
    app_save(app);
    app->running = 0;
}

void handle_move_focus_prev_sibling_in_edit_history_mode(AppState *app) {
    TreeNode current = app->ui->current_node;
    TreeNode n = tree_node_parent(app->tree_overlay, current);
    TreeNode edit_history_root = app_metadata_key_node(app->operate, APP_META_EDIT_HISTORY);
    uint64_t edit_history_root_id = tree_node_id(edit_history_root);
    while(!tree_node_is_null(n) && tree_node_id(n) != edit_history_root_id){
        TreeNode sibling = tree_node_prev_sibling(app->tree_overlay, n);
        if(tree_node_is_null(sibling)){
            n = tree_node_parent(app->tree_overlay, n);
        }else{
            app->ui->current_node = app_node_last_leaf(app->tree_overlay, sibling);
            return;
        }
    }
    log_ui_message("No previous sibling found in edit history, staying at current node id=%lu", tree_node_id(app->ui->current_node));
}

void handle_move_focus_next_sibling_in_edit_history_mode(AppState *app) {
    TreeNode current = app->ui->current_node;
    TreeNode n = tree_node_parent(app->tree_overlay, current);
    TreeNode edit_history_root = app_metadata_key_node(app->operate, APP_META_EDIT_HISTORY);
    uint64_t edit_history_root_id = tree_node_id(edit_history_root);
    while(!tree_node_is_null(n) && tree_node_id(n) != edit_history_root_id){
        TreeNode sibling = tree_node_next_sibling(app->tree_overlay, n);
        if(tree_node_is_null(sibling)){
            n = tree_node_parent(app->tree_overlay, n);
        }else{
            app->ui->current_node = app_node_primary_leaf(app->tree_overlay, sibling);
            return;
        }
    }
    log_ui_message("No next sibling found in edit history, staying at current node id=%lu", tree_node_id(app->ui->current_node));
}

static bool is_id_str(const char *str) {
    if(str == NULL || *str == '\0'){
        return false;
    }
    for(const char *p = str; *p; p++){
        if(*p < '0' || *p > '9'){
            return false;
        }
    }
    return true;
}

void handle_back_to_normal_operation_mode(AppState *app) {
    app->operate->mode = OPERATION_MODE_NORMAL;
    app->ui->current_node = tree_find_by_id(app->tree_overlay, app->operate->normal_mode_node_id);
    if(app->operate->edit_history_node_fold){
        TreeNode edit_history_node = app_metadata_key_node(app->operate, APP_META_EDIT_HISTORY);
        tree_node_set_collapse(app->tree_overlay, &edit_history_node, true);
    }
    log_ui_message("Switched back to normal operation mode");
}

void handle_jump_to_underlying_id(AppState *app) {
    TreeNode current = app->ui->current_node;
    const char *current_text = tree_node_text(current);
    if(!is_id_str(current_text)){
        log_ui_message("Current node text is not a valid id, cannot jump to underlying id");
        return;
    }
    uint64_t underlying_id = strtoull(current_text, NULL, 10);
    TreeNode target = tree_find_by_id(app->tree_overlay, underlying_id);
    if(tree_node_is_null(target)){
        log_ui_message("Underlying id %lu not found in the tree, cannot jump", underlying_id);
        return;
    }
    app->operate->normal_mode_node_id = tree_node_id(target);
    handle_back_to_normal_operation_mode(app);
    log_ui_message("Jumped to underlying id %lu", underlying_id);
}


void app_apply_event_edit_history_mode(AppState *app, UserOperation uo) {
    log_debug("app_apply_event_edit_history_mode: Processing UserOperation type=%d in edit history mode", uo.type);
    switch (uo.type) {
        case UO_MOVE_FOCUS_PREV_SIBLING:
            handle_move_focus_prev_sibling_in_edit_history_mode(app);
            break;
        case UO_MOVE_FOCUS_NEXT_SIBLING:
            handle_move_focus_next_sibling_in_edit_history_mode(app);
            break;
        case UO_HIT_SPACE:
            handle_jump_to_underlying_id(app);
            break;
        default:
            handle_back_to_normal_operation_mode(app);
    }
}

void app_apply_event(AppState *app, UserOperation uo) {
    log_debug("app_apply_event: Processing UserOperation type=%d", uo.type);
    if(app->operate->mode == OPERATION_MODE_EDIT_HISTORY){
        app_apply_event_edit_history_mode(app, uo);
        return;
    }
    switch (uo.type) {
    case UO_NOP:
        // No operation
        break;

        // modification
    case UO_ADD_CHILD_NODE:
        handle_add_child_node(app);
        break;
    case UO_ADD_CHILD_TO_TAIL:
        handle_add_child_to_tail(app, app->ui->current_node);
        break;
    case UO_ADD_SIBLING_ABOVE:
        handle_add_sibling_above(app);
        break;
    case UO_ADD_SIBLING_BELOW:
        handle_add_sibling_below(app);
        break;
    case UO_EDIT_NODE:{
        char tc;
        handle_edit_node(app, app->ui->current_node, &tc);
        operate_edit_history_record(app->operate, &(Event){.type = EVENT_UPDATE_TEXT, 
            .node_id = tree_node_id(app->ui->current_node)});
        if(tc == '\t'){
            handle_add_child_to_tail(app, app->ui->current_node);
        }
        break;
    }
    case UO_VI_EDIT_NODE:
        handle_vi_edit_node(app);
        break;
    case UO_MARK_AS_DEFINITION:
        handle_mark_as_definition(app);
        break;
    case UO_UNMARK_AS_DEFINITION:
        handle_unmark_as_definition(app);
        break;
    case UO_APPEND_NODE_TEXT:
        handle_append_node_text(app);
        break;
    case UO_JOIN_SIBLING_AS_CHILD:
        handle_join_sibling_as_child(app);
        break;
    case UO_FOLD_NODE:
        handle_fold_node(app);
        break;
    case UO_UNFOLD_NODE:
        handle_unfold_node(app);
        break;
    // case UO_FOLD_CHILDREN:
    //     handle_fold_children(app);
    case UO_FOLD_MORE:
        handle_fold_more(app);
        break;
    case UO_FOLD_LEVEL_1:
        handle_fold_and_move_to_child(app);
        break;
    case UO_REDUCE_FOLDING:
        handle_reduce_folding(app);
        break;

    // edit
    case UO_UNDO:
        handle_undo(app);
        break;
    case UO_REDO:
        handle_redo(app);
        break;
    case UO_COPY_SUBTREE:
        handle_copy_subtree(app);
        break;
    case UO_PASTE_AS_CHILD:
        handle_paste_as_child(app);
        break;
    case UO_PASTE_SIBLING_BELOW:
        handle_paste_as_sibling_below(app);
        break;
    case UO_PASTE_SIBLING_ABOVE:
        handle_paste_as_sibling_above(app);
        break;
    case UO_DELETE_SUBTREE:
        handle_delete_subtree(app);
        break;
    case UO_CUT_SUBTREE:
        handle_cut_subtree(app);
        break;
    case UO_CUT_NODE:
        handle_cut_node(app);
        break;
    case UO_COPY_TEXT_TO_SYSTEM_CLIPBOARD:
        handle_copy_text_to_system_clipboard(app);
        break;
    case UO_INSERT_PARENT_LEFT:
        log_debug("Insert parent left operation - to be implemented");
        break;
    case UO_COPY_SUBTREE_TO_SYSTEM_CLIPBOARD:{
        handle_copy_subtree_to_system_clipboard(app);
        break;
    }
    case UO_JOIN_TEXT_WITHOUT_SPACE:
        handle_join_text_without_space(app);
        break;


        // navigation
    case UO_MOVE_FOCUS_PREV_SIBLING:
        handle_move_focus_prev_sibling(app);
        break;
    case UO_MOVE_FOCUS_NEXT_SIBLING:
        handle_move_focus_next_sibling(app);
        break;
    case UO_MOVE_FOCUS_UP:
        handle_move_focus_up(app);
        break;
    case UO_MOVE_FOCUS_DOWN:
        handle_move_focus_down(app);
        break;
    case UO_MOVE_FOCUS_LEFT:
        handle_move_focus_left(app);
        break;
    case UO_MOVE_FOCUS_RIGHT:
        handle_move_focus_right(app);
        break;  
    case UO_MOVE_TO_CHILD_POSITION:{
        handle_move_to_child_position(app, uo.param1);
        break;
    }
    case UO_MOVE_FOCUS_TOP:
        handle_move_focus_top(app);
        break;  
    case UO_MOVE_FOCUS_BOTTOM:
        handle_move_focus_bottom(app);
        break; 
    case UO_MOVE_FOLD_BEGIN:
        handle_move_fold_begin(app);
        break;
    case UO_MOVE_FOLD_END:
        handle_move_fold_end(app);
        break;
    case UO_MOVE_PARENT_PREV_SIBLING_BEGIN:
        handle_move_parent_prev_sibling_begin(app);
        break;
    case UO_MOVE_PARENT_NEXT_SIBLING_BEGIN:
        handle_move_parent_next_sibling_begin(app);
        break;
    case UO_MOVE_PARENT_NEXT_SIBLING_END:
        handle_move_parent_next_sibling_end(app);
        break;
    case UO_MOVE_PARENT_PREV_SIBLING_END:
        handle_move_parent_prev_sibling_end(app);
        break;
    case UO_MOVE_FOCUS_LAST_CHILD:
        handle_move_focus_last_child(app);
        break;
    case UO_JUMP_BACK:
        handle_jump_back(app);
        break;
    case UO_JUMP_FORWARD:
        handle_jump_forward(app);
        break;
    case UO_MOVE_FOCUS_HOME:
        handle_move_focus_home(app);
        break;
    case UO_MOVE_FOCUS_TERM_ROOT:
        handle_move_focus_term_root(app);
        break;
    case UO_MOVE_FOCUS_MOST_LEFT_UPPER:
        handle_move_focus_most_left_upper(app);
        break;
    case UO_MOVE_FOCUS_MOST_LEFT_LOWER:
        handle_move_focus_most_left_lower(app);
        break;
    case UO_MOVE_FOCUS_CURRENT_TASK:
        handle_move_focus_current_task(app);
        break;
    case UO_MARK_NODE:
        handle_mark_node(app, uo);
        break;
    case UO_JUMP_TO_MARK:
        handle_jump_to_mark(app, uo);
        break;
    case UO_JUMP_TO_UI_NODE_MARK:
        handle_jump_to_ui_node_mark(app, uo);
        break;
    case UO_INDEX_FROM_ROOT:
        handle_index_from_root(app);
        break;

    // edit history
    case UO_TO_EDIT_HISTORY:{
        handle_to_edit_history(app);
        break;
    }
    
        // view
    case UO_CENTER_VIEW:
        ui_center_view_on_current(app->ui);
        break;
    case UO_PLACE_LEFT:
        ui_place_current_left(app->ui);
        break;
    case UO_PLACE_RIGHT:
        ui_place_current_right(app->ui);
        break;
    case UO_VIEW_HALF_SCREEN_LEFT:
        ui_view_move(app->ui, 0, - app->ui->width / 2);
        break;
    case UO_VIEW_HALF_SCREEN_RIGHT:
        ui_view_move(app->ui, 0, app->ui->width / 2);
        break;
    case UO_VIEW_DOWN:
        ui_view_down(app->ui, 1);
        break;
    case UO_VIEW_UP:
        ui_view_up(app->ui, 1);
        break;
    case UO_NEXT_PAGE:
        ui_view_next_page(app->ui);
        break;
    case UO_PREV_PAGE:
        ui_view_prev_page(app->ui);
        break;

        // mode switch
    case UO_COMMAND_MODE:
        handle_command_mode(app);
        break;
    case UO_SHELL_ABOVE:
        handle_shell_above(app);
        break;

    case UO_SEARCH:
        handle_search(app);
        break;
    case UO_SEARCH_BACKWARD:
        handle_search_backward(app);
        break;
    case UO_SEARCH_NEXT:
        handle_search_next(app);
        break;
    case UO_SEARCH_PREV:
        handle_search_prev(app);
        break;
    
        // external
    case UO_SEARCH_ENGINE:
        handle_search_engine(app);
        break;
    case UO_OPEN_RESOURCE_LINK:
        handle_open_resource_link(app);
        break;
    case UO_JUMP_KEYWORD_DEFINITION:
        handle_jump_keyword_definition(app);
        break;

    // task
    case UO_CREATE_CHILD_TASK:
        handle_create_child_task(app);
        break;
    case UO_CREATE_SIBLING_TASK:
        handle_create_sibling_task(app);
        break;
    case UO_FINISH_TASK:
        handle_finish_task(app);
        break;
    case UO_NEXT_TASK:
        handle_next_task(app);
        break;
    case UO_PREV_TASK:
        handle_prev_task(app);
        break;
    case UO_AS_CURRENT_TASK:
        handle_as_current_task(app, app->ui->current_node);
        break;

    // external resources
    case UO_ASK_AI:
        handle_ask_ai(app, uo);
        break;
    case UO_HIT_CTRL_J:{
        bool zoomed;
        int r = current_tmux_pane_zoomed(&zoomed);
        if(app->connect && !app->connect->pause && !zoomed){
            handle_send_command(app);
        }else{
            ui_info_set_message(app->ui, "Cannot send command: %s", app->connect == NULL ? "no active connection" : (app->connect->pause ? "connection is paused" : "tmux pane is zoomed"));
        }
        break;
    }
    case UO_HIT_ENTER:{
        handle_add_sibling_below(app);
        break;
    }

        // save / exit
    case UO_EXIT_SAVE:
        handle_exit_save(app);
        break;
    default:
        log_warn("Unhandled UserOperation type: %d", uo.type);
        break;
    }
}
