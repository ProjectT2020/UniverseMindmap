#ifndef APP_H
#define APP_H

#include "../wal/wal.h"
#include "../ui/ui.h"
#include "../utils/stack.h"
#include "../tree/tree_storage.h"
#include "../tree/tree_overlay.h"
#include "../operate/operate.h"
#include "../connect/connect.h"

extern const char *APP_META_EDIT_HISTORY;

/**
 * application state structure
 */
typedef struct {
    char *data_file_path;
    char *lock_file_path; // path to lock file for single instance enforcement
    int lock_file_fd;   // file descriptor for the lock file

    TreeOverlay *tree_overlay;
    TreeView *tree_view;
    TreeStorage *tree_storage;

    Wal *wal;
    Operate *operate;
    UiContext *ui;
    
    uint64_t selected_node_id;  // current focused node ID
    int running;
    char *edit_buffer;      // node editing buffer
    size_t edit_buffer_size;
    
    
    // jump history
    Stack *jump_back_stack;     // backward history stack (stores NodeID)
    Stack *jump_forward_stack;  // forward history stack (stores NodeID)
    
    // connection
    ConnectContext *connect;             // connection context (e.g shell)
    
} AppState;

/**
 * initialize application
 * @param data_file data file path (can be NULL)
 * @return application state structure
 */
AppState* app_init(const char *data_file);

/**
 * shutdown application
 */
void app_shutdown(AppState *app);

/**
 * handle user input and execute commands
 * @param app application state
 * @param cmd user command (single character)
 */
void app_handle_command(AppState *app, char cmd);

void app_run(AppState *app);

void app_run_interactive(AppState *app);

void app_step(AppState *app, UserOperation uo);

void app_save(AppState *app);

void app_apply_event(AppState *app, UserOperation uo);

void handle_focus_down(AppState *app);


void handle_delete_node(AppState *app);
void handle_delete_subtree(AppState *app);

void handle_edit_node(AppState *app, TreeNode node, char *terminated_character);

void handle_undo(AppState *app);
void handle_redo(AppState *app);


// app metadata management
TreeNode app_metadata_key_node(Operate *operate, const char *key) ;

#ifdef APP_TESTING
int app_test_handle_jump_hierachy_definition(
    AppState *app,
    TreeNode subtree_root,
    const char *keywords,
    bool (*filter)(TreeNode node, void *ctx),
    void *filter_ctx
);
bool app_test_jump_definition_filter(TreeNode node, void *ctx);
#endif

#endif // APP_H
