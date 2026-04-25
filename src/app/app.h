#ifndef APP_H
#define APP_H

#include "../ui/input_state.h"
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
typedef struct AppState {
    char *data_file_path;
    char *lock_file_path; // path to lock file for single instance enforcement
    int lock_file_fd;   // file descriptor for the lock file

    TreeOverlay *tree_overlay;
    TreeView *tree_view;
    TreeStorage *tree_storage;

    Wal *wal;
    Operate *operate;
    TreeNode current_node;         // current focus node

    // UI attributes
    bool fix_view;              // debug do not adjust view
    bool global_enable_hide; // global flag to enable hiding nodes (not show hidden  nodes)
    bool show_child_position;       // UI
    char *info_message;         // message to show in status bar
    bool mark_and_show_visible_nodes;
    uint64_t node_marks[26 * 26]; // mark -> node id

    // UI callbacks
    void *ui_ctx; // context for ui callback 
    void (*ui_center_view_on_current)(void *ui_ctx); 
    void (*ui_place_current_left)(void *ui_ctx); 
    void (*ui_place_current_right)(void *ui_ctx);
    void (*ui_view_move)(void *ui_ctx, int rows, int cols);
    void (*ui_view_down)(void *ui_ctx, int lines);
    void (*ui_view_up)(void *ui_ctx, int lines);
    void (*ui_view_next_page)(void *ui_ctx);
    void (*ui_view_prev_page)(void *ui_ctx);
    void (*ui_view_next_half_page)(void *ui_ctx);
    void (*ui_view_prev_half_page)(void *ui_ctx);
    void (*ui_view_half_screen_right)(void *ui_ctx);
    void (*ui_view_half_screen_left)(void *ui_ctx);
    void (*ui_reset_layout)(void *ui_ctx);
    void (*ui_render)(void *ui_ctx); 
    char* (*ui_get_search_query)(void *ui_ctx); 
    char* (*ui_get_search_backward_query)(void *ui_ctx); 

    
    uint64_t selected_node_id;  // current focused node ID
    int running;
    char *edit_buffer;      // node editing buffer
    size_t edit_buffer_size;
    
    
    // jump history
    Stack *jump_back_stack;     // backward history stack (stores NodeID)
    Stack *jump_forward_stack;  // forward history stack (stores NodeID)
    
    // connection
    ConnectContext *connect;             // connection context (e.g shell)

    // input handling
    InputState *input_state; 

    // searching
    char *search_query;
    int search_exact;           // whether to match exactly (0=contain, 1=exact)

    char *command;
    char *node_text;
    
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

void handle_edit_node(AppState *app);

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

void app_state_input_queue_add_key(AppState *app_state, char key);


TreeNode ui_first_visible_child(AppState *app, TreeNode n) ;
TreeNode ui_next_visible_sibling(AppState *app, TreeNode n) ;
TreeNode ui_previous_visible_sibling(AppState *app, TreeNode n) ;
TreeNode ui_last_visible_child(AppState *app, TreeNode parent);
TreeNode ui_parent_level_next_visible_sibling(AppState *app, TreeNode n);
TreeNode ui_parent_level_prev_visible_sibling(AppState *app, TreeNode n);
// layout and selection
void ui_move_focus_down(AppState *app) ;
void ui_move_focus_up(AppState *app) ;
void ui_move_focus_left(AppState *app) ;
void ui_move_focus_bottom(AppState *app);
void ui_move_focus_last_child(AppState *app);

void ui_message_fun(void *uc, const char *msg, va_list args);
void update_current_with_history(AppState *app, TreeNode new_position) ;

#endif // APP_H
