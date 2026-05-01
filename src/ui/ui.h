
#ifndef UI_H
#define UI_H

#include <__stdarg_va_list.h>
#include <stdint.h>

#include "../tree/tree_overlay.h"// domain node
#include "../ui/tty.h"

#define UI_CONTEXT_MAGIC 0x55494358u

typedef struct {
    enum {
        UO_NOP,                   // No operation

        // navigation
        UO_JOIN_SIBLING_AS_CHILD,
        UO_MOVE_FOCUS_UP,
        UO_MOVE_FOCUS_DOWN,
        UO_MOVE_FOCUS_PREV_SIBLING,
        UO_MOVE_FOCUS_NEXT_SIBLING,
        UO_MOVE_FOCUS_NEXT_LINE,    // ↓ next leaf if current is leaf, else next sibling
        UO_MOVE_FOCUS_PREVIOUS_LINE, // ↑ prev leaf if current is leaf, else prev sibling
        UO_MOVE_FOCUS_LEFT,
        UO_MOVE_FOCUS_RIGHT,
        UO_MOVE_FOCUS_BOTTOM,
        UO_MOVE_FOCUS_LAST_CHILD,
        UO_MOVE_FOCUS_TOP,
        UO_MOVE_FOCUS_HOME,
        UO_MOVE_FOCUS_TERM_ROOT,
        UO_MOVE_FOCUS_MOST_LEFT_UPPER,
        UO_MOVE_FOCUS_MOST_LEFT_LOWER,
        UO_MOVE_FOCUS_CURRENT_TASK,
        UO_MOVE_FOCUS_VIEWPORT_TOP,
        UO_MOVE_FOCUS_VIEWPORT_BOTTOM,
        UO_MOVE_TO_CHILD_POSITION, // move focus to child at position (param1)
        UO_MOVE_FOLD_BEGIN,
        UO_MOVE_FOLD_END,
        UO_MOVE_PARENT_PREV_SIBLING_BEGIN,
        UO_MOVE_PARENT_PREV_SIBLING_END,
        UO_MOVE_PARENT_NEXT_SIBLING_BEGIN,
        UO_MOVE_PARENT_NEXT_SIBLING_END,
        UO_INDEX_FROM_ROOT,
        UO_PREPARE_JUMP_TO_VISIBLE_TAG,

        // task
        UO_CREATE_CHILD_TASK,
        UO_CREATE_SIBLING_TASK,
        UO_FINISH_TASK,
        UO_AS_CURRENT_TASK,
        UO_CURRENT_TASK_JUMP_DEFINITION,
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
        UO_FOLD_NODE,             // zc: fold current nod3
        UO_UNFOLD_NODE,           // zo: unfold current node
        UO_FOLD_MORE,             // zm: fold more
        UO_FOLD_LEVEL_1,         // zM: fold to level 1
        UO_REDUCE_FOLDING,         // zr: reveal more
        UO_EXPAND_ALL_DESCENDANTS, // zR: expand all descendants (except .meta)

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
        UO_DO_APPEND_NODE_TEXT,
        UO_EDIT_NODE,             // rename node
        UO_EDIT_NODE_FRONT,
        UO_EDIT_NODE_END,
        UO_DO_EDIT_NODE,             // rename node
        UO_VI_EDIT_NODE,         // enter vi-like edit mode for current node
        UO_JOIN_TEXT_WITHOUT_SPACE, // (gJ) join text with next sibling without adding space

        // mode switch
        UO_COMMAND_MODE,        // switch to command mode
        UO_DO_COMMAND,          // execute command
        UO_SHELL_ABOVE,          // connect new controllable shell above

        // search
        UO_SEARCH,                // search mode
        UO_DO_SEARCH,             // perform search after getting search query
        UO_SEARCH_BACKWARD,       // get search backward string
        UO_DO_SEARCH_BACKWARD,    // perform search backward
        UO_SEARCH_NEXT,           // next search result
        UO_SEARCH_PREV,           // previous search result
        UO_SEARCH_NEXT_EXACT,     // next exact match for current node text
        UO_SEARCH_PREV_EXACT,     // previous exact match for current node text

        // action
        UO_OPEN_RESOURCE_LINK,   // open resource link (https:// or file://)
        UO_SEARCH_ENGINE,
        UO_ASK_AI,
        UO_KEYWORD_LOOKUP,      // 'K' launch keywordprg <keyword>
        UO_JUMP_KEYWORD_DEFINITION, // gd
        UO_JUMP_KEYWORD_GLOBAL_DEFINITION, // Ctrl-] jump to global definition
        UO_HIT_ENTER,            // user pressed Enter (\r) 
        UO_HIT_CTRL_J,            // user pressed Ctrl+J (\n)
        UO_HIT_SPACE,            // user pressed Space ( )

        // view
        UO_VIEW_UP,               // Ctrl-Y 
        UO_VIEW_DOWN,             // Ctrl-E 
        UO_NEXT_PAGE,            // Ctrl-F / PgDn
        UO_PREV_PAGE,            // Ctrl-B / PgUp
        UO_NEXT_HALF_PAGE,       // Ctrl-D
        UO_PREV_HALF_PAGE,       // Ctrl-U

        // jump history
        UO_JUMP_BACK,             // Ctrl+O jump back in history
        UO_JUMP_FORWARD,          // Ctrl+I (Tab) jump forward in history (after jumping back)
        UO_MARK_NODE,              // mark node with letter
        UO_JUMP_TO_MARK,            // jump to mark
        UO_JUMP_TO_UI_NODE_MARK,    // visible node mark

        // edit history
        UO_TO_EDIT_HISTORY,        // ';' jump to edit history (previously edited nodes)

        // headless exit operations
        UO_EXIT_SAVE,             // save and exit
        UO_EXIT_NO_SAVE,          // exit without saving
        UO_SAVE,                   // save
        UO_TOGGLE_ANCESTORS,       // Ctrl+G toggle ancestor breadcrumb
        UO_CANCEL_JUMP_TO_VISIBLE_TAG  // Esc during 't' tag selection
    } type;
    int param1;
    int param2;
    void *data;
} UserOperation;    

UserOperation * uo_create(UserOperation uo);
void uo_destroy(UserOperation *uo);

typedef struct UiContext UiContext;

typedef void (*UiGetNameFn)(UiContext *ctx, char *buffer, size_t buffer_size);

typedef struct AppState AppState;
typedef struct UiContext {
    uint32_t magic;            // runtime type/lifetime guard
    int width;
    int height;
    int offset_x;
    int offset_y;
    TreeOverlay *overlay;          // underlying tree overlay (data model)
    AppState *app;                 // application state

    // view
    int view_x;                 // view position x
    int view_y;                 // view position y
    bool show_ancestors_in_one_line;

    int current_text_x;        // current node text render position X (for cursor positioning during editing)
    int current_text_y;        // current node text render position Y
    int mark; // visible node index
    int mark_page; // mark page

    // input
    UserOperation last_input;   // previous user input, used to distinguish whether Tab(Ctrl+I) means “New Node” or “Jump Forward.”

    // callbacks for headless testing
    UiGetNameFn get_name_fn;

} UiContext;

UiContext* ui_context_create(int width, int height);
void ui_context_destroy(UiContext *ctx);
void ui_set_root_node(UiContext *ctx, TreeNode root);
void ui_set_overlay(UiContext *ctx, TreeOverlay *overlay);

// input 
UserOperation ui_poll_user_input(UiContext *ctx) ;
char* ui_get_name(void *ui_ctx, char *terminated_character);
char* ui_get_name_append(void *ui_ctx, const char *old_name, char *terminated_character);
char* ui_get_command(void *ui_ctx);
char* ui_get_search_query(void *ui_ctx);
char *ui_get_search_backward_query(void *ui_ctx);
void ui_set_get_name_callback(UiContext *ctx, UiGetNameFn fn);

// DFS traversal
TreeNode ui_dfs_next(TreeOverlay *overlay, TreeNode n);
TreeNode ui_dfs_prev(TreeOverlay *overlay, TreeNode n);

void ui_render(void *ui_ctx);


// view
void ui_center_view_on_current(void *ui_ctx);
void ui_place_current_left(void *ui_ctx);
void ui_place_current_right(void *ui_ctx);
void ui_view_move(void *ui_ctx, int rows, int cols);
void ui_view_down(void *ui_ctx, int lines);
void ui_view_up(void *ui_ctx, int lines);
void ui_view_next_page(void *ui_ctx);
void ui_view_prev_page(void *ui_ctx);

void ui_reset_layout(void *ui_ctx);


int mind_node_height(TreeOverlay *ov, TreeNode n) ;
int ui_tag_index_to_tag(int tag_index, char *tag0, char *tag1);
int ui_tag_to_index(char tag0, char tag1);
int ui_tag_index_to_tag_left_hand(int tag_index, char *tag0, char *tag1);
int ui_tag_to_index_left_hand(char tag0, char tag1);

#endif // UI_H