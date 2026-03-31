
#ifndef OPERATE_H
#define OPERATE_H

#pragma once
#include "operate_type.h"
#include "../utils/stack.h"
#include "../event/event.h"
#include "ui/ui.h"

typedef enum {
    SEARCH_DIRECTION_FORWARD = 0,
    SEARCH_DIRECTION_BACKWARD = 1
} SearchDirection;

typedef struct {
    TreeOverlay *overlay;
    UiContext *ui;
    Wal *wal;
    Stack *undo_stack;
    Stack *redo_stack;
    
    TreeNode clipboard;            // node to be copied
    enum {  // clipboard state
        CLIPBOARD_EMPTY = 0,
        CLIPBOARD_COPY = 1,
        CLIPBOARD_CUT = 2
    } clipboard_mode;

    char search_query[1024];
    int search_direction;       // 0= forward, 1= backward
} Operate;

Operate* operate_create(Wal *wal, TreeOverlay *overlay);
void operate_destroy(Operate *operate);

int operate_commit_event(Operate *operate, Event *e);

int operate_undo(Operate *operate);
int operate_redo(Operate *operate);
int operate_copy_subtree(Operate *operate, TreeNode node) ;
int operate_delete_subtree(Operate *operate, TreeNode node);

int operate_import_mindmap(Operate *operate, const char *filepath);
int operate_export_mindmap(Operate *operate, const char *filepath);
int operate_export_mindmap_to_clipboard_txt(Operate *operate, TreeNode current_node);

int operate_count_subtree_nodes(Operate *operate, TreeNode node);
int operate_copy_paste_as_first_child(Operate *operate, TreeNode parent);
int operate_copy_paste_as_last_child(Operate *operate, TreeNode parent);

TreeNode operate_search_next(Operate *operate, TreeNode start_node);
TreeNode operate_search_prev(Operate *operate, TreeNode start_node);
TreeNode operate_search_next_in_subtree(Operate *operate, TreeNode start_node, const char *search_term);

int operate_edit_node(Operate *operate, TreeNode node);
int operate_reduce_folding(Operate *operate, TreeNode current, int fold_level);
int operate_fold_more(Operate *operate, TreeNode current, int fold_level);
int operate_fold_node(Operate *operate, TreeNode node);

// external
int operate_ask_ai(Operate *operate, TreeNode node, enum query_scope scope);
int operate_output_ai_message();

#endif // OPERATE_H