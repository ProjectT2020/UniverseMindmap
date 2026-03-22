
#ifndef UI_H
#define UI_H

#include "../tree/tree_overlay.h"// domain node
#include "../ui/tty.h"

typedef struct {
    enum {
        UO_NOP,                   // No operation
        UO_JOIN_SIBLING_AS_CHILD,
        UO_MOVE_FOCUS_UP,
        UO_MOVE_FOCUS_DOWN,
        UO_MOVE_FOCUS_PREV_SIBLING,
        UO_MOVE_FOCUS_NEXT_SIBLING,
        UO_MOVE_FOCUS_LEFT,
        UO_MOVE_FOCUS_RIGHT,
        UO_MOVE_FOCUS_BOTTOM,
        UO_MOVE_FOCUS_LAST_CHILD,
        UO_MOVE_FOCUS_TOP,
        UO_MOVE_FOCUS_HOME,
        UO_MOVE_FOCUS_CURRENT_TASK,
        UO_MOVE_TO_CHILD_POSITION, // move focus to child at position (param1)
        UO_MOVE_FOLD_BEGIN,
        UO_MOVE_FOLD_END,
        UO_MOVE_PARENT_PREV_SIBLING_BEGIN,
        UO_MOVE_PARENT_PREV_SIBLING_END,
        UO_MOVE_PARENT_NEXT_SIBLING_BEGIN,
        UO_MOVE_PARENT_NEXT_SIBLING_END,

        // task
        UO_CREATE_CHILD_TASK,
        UO_CREATE_SIBLING_TASK,
        UO_FINISH_TASK,
        UO_NEXT_TASK,
        UO_PREV_TASK,

        // modification
        UO_ADD_CHILD_NODE,
        UO_ADD_CHILD_TO_TAIL,
        UO_ADD_SIBLING_ABOVE,
        UO_ADD_SIBLING_BELOW,
        UO_DELETE_SUBTREE,        // delete subtree
        UO_CUT_SUBTREE,        // cut subtree
        UO_CUT_NODE,            // cut node but keep children (promote children)
        UO_INSERT_PARENT_LEFT,     // insert new parent node to the left of current node, and make current node a child of the new parent
        UO_MARK_AS_DEFINITION,       // mark current node as definition (surround with [], for jumping)
        UO_UNMARK_AS_DEFINITION,     // unmark current node as definition (remove surrounding [])

        // folding
        UO_TOGGLE_FOLD_NODE,
        UO_FOLD_NODE,             // zc: fold current nod3
        UO_UNFOLD_NODE,           // zo: unfold current node
        UO_FOLD_MORE,             // zm: fold more
        UO_FOLD_LEVEL_1,         // zM: fold to level 1
        UO_REDUCE_FOLDING,         // zr: reveal more

        // view
        UO_CENTER_VIEW,           // z. : center view on current node
        UO_PLACE_LEFT,            // zs : show current node at left edge of view
        UO_PLACE_RIGHT,            // ze : show current node at right edge of view
        UO_VIEW_HALF_SCREEN_LEFT,   // zH: move view half screen left 
        UO_VIEW_HALF_SCREEN_RIGHT,   // zL: move view half screen right

        // editing
        UO_UNDO,                  // u
        UO_REDO,                  // Ctrl+R
        UO_COPY_SUBTREE,          // y
        UO_PASTE_SIBLING_BELOW,   // p
        UO_PASTE_SIBLING_ABOVE,   // P
        UO_PASTE_AS_CHILD,        // gp
        UO_COPY_TEXT_TO_SYSTEM_CLIPBOARD,
        UO_COPY_SUBTREE_TO_SYSTEM_CLIPBOARD,
        UO_PASTE_FROM_SYSTEM_CLIPBOARD_AS_SIBLINGS,
        UO_PASTE_FROM_SYSTEM_CLIPBOARD_AS_CHILDREN,
        UO_APPEND_NODE_TEXT,
        UO_EDIT_NODE,             // rename node
        UO_JOIN_TEXT_WITHOUT_SPACE, // (gJ) join text with next sibling without adding space

        // mode switch
        UO_COMMAND_MODE,        // switch to command mode
        UO_SHELL_ABOVE,          // connect new controllable shell above

        // search
        UO_SEARCH,                // search mode
        UO_SEARCH_BACKWARD,       // search backward
        UO_SEARCH_NEXT,           // next search result
        UO_SEARCH_PREV,           // previous search result

        // action
        UO_OPEN_RESOURCE_LINK,   // open resource link (https:// or file://)
        UO_SEARCH_ENGINE,
        UO_ASK_AI,
        UO_KEYWORD_LOOKUP,      // 'K' launch keywordprg <keyword>
        UO_JUMP_KEYWORD_DEFINITION, // Ctrl-] jump to keyword definition
        UO_HIT_ENTER,            // user pressed Enter (\r) 
        UO_HIT_CTRL_J,            // user pressed Ctrl+J (\n)

        // view
        UO_VIEW_UP,               // Ctrl-Y 
        UO_VIEW_DOWN,             // Ctrl-E 
        UO_NEXT_PAGE,            // Ctrl-F / PgDn
        UO_PREV_PAGE,            // Ctrl-B / PgUp

        // jump history
        UO_JUMP_BACK,             // Ctrl+O jump back in history
        UO_JUMP_FORWARD,          // Ctrl+I (Tab) jump forward in history (after jumping back)
        UO_MARK_NODE,              // mark node with letter
        UO_JUMP_TO_MARK,            // jump to mark
        UO_JUMP_TO_UI_NODE_MARK,    // visible node mark

        // headless exit operations
        UO_EXIT_SAVE,             // save and exit
        UO_EXIT_NO_SAVE,          // exit without saving
        UO_SAVE                   // save 
    } type;
    int param1;
    int param2;
    void *data;
} UserOperation;    

typedef struct UiContext UiContext;

typedef void (*UiGetNameFn)(UiContext *ctx, char *buffer, size_t buffer_size);

typedef struct UiContext {
    int width;
    int height;
    int offset_x;
    int offset_y;
    bool global_enable_hide; // global flag to enable hiding nodes (not show hidden  nodes)
    TreeNode current_node;         // current focus node
    TreeOverlay *overlay;          // underlying tree overlay (data model)

    // view
    int view_x;                 // view position x
    int view_y;                 // view position y
    bool fix_view;              // debug do not adjust view
    bool show_ancestors_in_one_line;

    int current_text_x;        // current node text render position X (for cursor positioning during editing)
    int current_text_y;        // current node text render position Y
    bool show_child_position;
    bool mark_and_show_visible_nodes;
    int mark; // visible node index
    int mark_page; // mark page
    uint64_t node_marks[26 * 26]; // mark -> node id

    // input
    UserOperation last_input;   // previous user input, used to distinguish whether Tab(Ctrl+I) means “New Node” or “Jump Forward.”

    // callbacks for headless testing
    UiGetNameFn get_name_fn;

    // searching
    char search_query[256];     // search string
    int search_exact;           // whether to match exactly (0=contain, 1=exact)

    char info_message[512];    // message to show in status bar 
} UiContext;

UiContext* ui_context_create(int width, int height);
void ui_context_destroy(UiContext *ctx);
void ui_set_root_node(UiContext *ctx, TreeNode root);
void ui_set_overlay(UiContext *ctx, TreeOverlay *overlay);

// input 
UserOperation ui_poll_user_input(UiContext *ctx) ;
char* ui_get_name(UiContext *ctx, char *terminated_character);
const char* ui_get_name_append(UiContext *ctx, const char *old_name, char *terminated_character);
char* ui_get_command(UiContext *ctx);
char* ui_get_search_query(UiContext *ctx);
char *ui_get_search_backward_query(UiContext *ctx);
void ui_set_get_name_callback(UiContext *ctx, UiGetNameFn fn);

// DFS traversal
TreeNode ui_dfs_next(TreeOverlay *overlay, TreeNode n);
TreeNode ui_dfs_prev(TreeOverlay *overlay, TreeNode n);

void ui_render(UiContext *ctx);

// visible nodes traversal (skip hidden nodes if global_enable_hide is true)
TreeNode ui_first_visible_child(UiContext *ui, TreeNode n) ;
TreeNode ui_next_visible_sibling(UiContext *ui, TreeNode n) ;
TreeNode ui_previous_visible_sibling(UiContext *ui, TreeNode n) ;
TreeNode ui_last_visible_child(UiContext *ui, TreeNode parent);
TreeNode ui_parent_level_next_visible_sibling(UiContext *ui, TreeNode n);
TreeNode ui_parent_level_prev_visible_sibling(UiContext *ui, TreeNode n);

// layout and selection
void ui_move_focus_down(UiContext *ui) ;
void ui_move_focus_up(UiContext *ui) ;
void ui_move_focus_left(UiContext *ui) ;
void ui_move_focus_right(UiContext *ui) ;
void ui_move_focus_child_position(UiContext *ui, int pos);
void ui_move_focus_top(UiContext *ui);
void ui_move_focus_bottom(UiContext *ui);
void ui_move_focus_last_child(UiContext *ui);

// view
void ui_center_view_on_current(UiContext *ui);
void ui_place_current_left(UiContext *ui);
void ui_place_current_right(UiContext *ui);
void ui_view_move(UiContext *ui, int rows, int cols);
void ui_view_down(UiContext *ui, int lines);
void ui_view_up(UiContext *ui, int lines);
void ui_view_next_page(UiContext *ui);
void ui_view_prev_page(UiContext *ui);

void ui_info_set_message(UiContext *ctx, const char *msg, ...);

void ui_reset_layout(UiContext *ui);

#endif // UI_H