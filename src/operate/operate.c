#include <stdint.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h> // close()
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

#include "../event/event.h"
#include "../wal/wal.h"
#include "../tree/tree_overlay.h"
#include "../utils/logging.h"
#include "../utils/stack.h"
#include "../utils/queue.h"
#include "../utils/os_specific.h"
#include "../app/app.h"

#include "operate.h"

Operate* operate_create(Wal *wal, TreeOverlay *overlay) {
    Operate *operate = (Operate*)calloc(1, sizeof(Operate));
    operate->wal = wal;
    operate->overlay = overlay;
    operate->undo_stack = stack_create(1024);
    operate->redo_stack = stack_create(1024);
    return operate;
}

void operate_destroy(Operate *operate) {
    if (!operate) return;
    if (operate->undo_stack) stack_destroy(operate->undo_stack);
    if (operate->redo_stack) stack_destroy(operate->redo_stack);
    free(operate);
}

/**
 * return: 0=success, -1=failure
 */
int operate_begin_transaction(Operate *operate) {
    if (!operate) return -1;
    Event *e = event_create_begin_transaction();
    int r = wal_append(operate->wal, e);
    event_destroy(e);
    return r;
}

/**
 * return: 0=success, -1=failure
 */
int operate_commit_transaction(Operate *operate) {
    if (!operate) return -1;
    Event *e = event_create_commit_transaction();
    int r = wal_append(operate->wal, e);
    event_destroy(e);
    return r;
}

/**
 * return: 0=success, -1=failure
 */
int operate_commit_event(Operate *operate, Event *e) {
    if (!operate || !e) return -1;
    // fill in LSN 
    e->lsn = operate->wal->next_lsn;
    // write WAL entry here if needed (omitted for brevity)
    
    // Apply the event to the tree overlay FIRST
    // This allows event fields (e.g., new_node_id) to be populated
    int r = tree_overlay_apply_event(operate->overlay, e);

    if(r == 0){// successfully applied, update WAL and stacks
        // Write the event to the WAL AFTER applying
        // Now e->new_node_id contains the actual generated node ID
        wal_append(operate->wal, e);

        stack_push(operate->undo_stack, e);

        // Clear redo stack on new event
        while (!stack_is_empty(operate->redo_stack)){
            Event *redo_e = (Event *)stack_pop(operate->redo_stack);
            event_destroy(redo_e);
        }

        return 0; // success
    }else {
        return -1; // failed to apply event
    }

}

int operate_fold_node(Operate *operate, TreeNode node){
    if (!operate) return -1;
    Event *event = event_create_collapse_node(tree_node_id(node));
    int r = operate_commit_event(operate, event);
    if (r != 0) {
        log_warn("Fold/unfold operation failed");
        return -1;
    }
    return 0;
}

int operate_tree_apply_wal_append(TreeOverlay *overlay, Wal *wal, Event *e) {
    if (!overlay || !wal || !e) return -1;
    int r = tree_overlay_apply_event(overlay, e);
    if(r == 0){
        // Successfully applied, update WAL state
        wal_append(wal, e);
    }
    return r;
}

int operate_undo(Operate *operate, Event **out_event){
    if (!operate) return -1;
    Event *e = (Event *)stack_pop(operate->undo_stack);
    *out_event = e;
    if (!e) {
        log_info("operate_undo: No more events to undo");
        log_ui_message("operate_undo: No more events to undo");
        return -1;
    }

    // Generate inverse event for undo
    Event *inverse_e = event_invert(operate->overlay->last_applied_lsn + 1, e);
    if (inverse_e) {
        // Apply inverse event
        operate_tree_apply_wal_append(operate->overlay, operate->wal, inverse_e);
        // Push original event to redo stack
        stack_push(operate->redo_stack, e);
        // Clean up inverse event
        event_destroy(inverse_e);
    } else {
        // If we cannot generate an inverse, push back the original event
        log_ui_message("operate_undo: Cannot undo event of type %d", e->type);
        stack_push(operate->undo_stack, e);
        return -1;
    }
    return 0;
}

int operate_redo(Operate *operate){
    if (!operate) return -1;
    Event *e = (Event *)stack_pop(operate->redo_stack);
    if (!e) return -1;

    // Re-apply the event with next LSN
    e->lsn = operate->overlay->last_applied_lsn + 1;
    operate_tree_apply_wal_append(operate->overlay, operate->wal, e);
    // Push event back to undo stack
    stack_push(operate->undo_stack, e);
    return 0;
}

int operate_copy_subtree(Operate *operate, TreeNode node) {
    operate->clipboard = node;
    operate->clipboard_mode = CLIPBOARD_COPY;
    return 0;
}

int operate_delete_subtree(Operate *operate, TreeNode node){

    TreeNode child = tree_node_first_child(operate->overlay, node);
    while(!tree_node_is_null(child)){
        int r = operate_delete_subtree(operate, child);
        if(r != 0){
            log_warn("operate_delete_subtree: Failed to delete child node id=%" PRIu64, tree_node_id(child));
            return -1;
        }
        child = tree_node_first_child(operate->overlay, node);
    }

    TreeNode parent = tree_node_parent(operate->overlay, node);
    TreeNode sibling = tree_node_next_sibling(operate->overlay, node);
    Event *e = event_create_delete_node(
        tree_node_id(node),
        tree_node_id(parent),
        tree_node_id(sibling),
        tree_node_text(node)
    );
    int r = operate_commit_event(operate, e);
    if(r != 0){
        log_warn("operate_delete_subtree: Failed to commit DELETE_NODE event for node id=%" PRIu64, tree_node_id(node));
        return -1;
    }

}

int operate_copy_paste_as_first_child(Operate *operate, TreeNode parent){
    TreeNode first_child = tree_node_first_child(operate->overlay, parent);
    Event *e = event_create_copy_subtree(
        tree_node_id(operate->clipboard), // source node id
        tree_node_id(parent), // new parent id 
        tree_node_id(first_child) // new next sibling id (first child)
    );
    int r = operate_commit_event(operate, e);
    if(r != 0){
        log_warn("operate_copy_paste: Failed to commit COPY_SUBTREE event");    
        return -1;
    }
    
    log_debug("operate_copy_paste: Successfully pasted subtree (new node id=%" PRIu64 ")", e->new_node_id);
    return 0;
}

int operate_copy_paste_as_last_child(Operate *operate, TreeNode parent){
    Event *e = event_create_copy_subtree(
        tree_node_id(operate->clipboard), // source node id
        tree_node_id(parent), // new parent id 
        0 // next sibling id = 0 means append as last child
    );
    int r = operate_commit_event(operate, e);
    if(r != 0){
        log_warn("operate_copy_paste: Failed to commit COPY_SUBTREE event");    
        return -1;
    }
    
    log_debug("operate_copy_paste: Successfully pasted subtree (new node id=%" PRIu64 ")", e->new_node_id);
    return 0;
}


static  int count_visitor(TreeNode n, void *ctx){
    int *count_ptr = (int *)ctx;
    (*count_ptr)++;
    return 0;
};

int operate_count_subtree_nodes(Operate *operate, TreeNode node){
    int count = 0;
    tree_traverse(operate->overlay, node, count_visitor, &count);
    return count;
}

/**
 * read node of multiple lines
 * if next few lines not start with tab, consider them as part of current node text, separated by space
 */
ssize_t read_node(FILE *file, char *buf, size_t buf_sz)
{
    if (!file || !buf || buf_sz == 0)
        return -1;

    char line[1024];
    size_t used = 0;
    int first_line = 1;

    while (1) {
        long pos = ftell(file);

        if (!fgets(line, sizeof(line), file)) {
            if (first_line)
                return -1;   // EOF before any data
            break;           // EOF after some lines
        }

        /* Check if this line starts with tab */
        if (line[0] == '\t' && !first_line) {
            fseek(file, pos, SEEK_SET);  // rollback
            break;
        }

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        /* Space separator between lines */
        size_t need = len + (first_line ? 0 : 1) + 1;
        if (used + need > buf_sz)
            return -2;

        if (!first_line)
            buf[used++] = ' ';

        memcpy(buf + used, line, len);
        used += len;
        buf[used] = '\0';

        first_line = 0;
    }

    return (ssize_t)used;
}


int operate_import_mindmap_from_txt(Operate *operate, const char *filepath){
    FILE *file = fopen(filepath, "r");
    if(!file){
        log_error("operate_import_mindmap_from_txt: Failed to open file %s", filepath);
        return -1;
    }

    char line[4096];
    Stack *node_stack = stack_create(1024);
    TreeNode root = operate->ui->current_node;
    if(root.kind == TREE_NODE_DISK){
        overlay_materialize(operate->overlay, &root);
    }
    stack_push(node_stack, root.mut);
    int prev_indent = -1;

    while(read_node(file, line, sizeof(line)) >= 0){
        // Count leading spaces for indentation
        int indent = 0;
        while(line[indent] == '\t'){
            indent++;
        }
        char *text = line + indent;
        // Remove trailing newline
        char *newline = strchr(text, '\n');
        if(newline) *newline = '\0';

        if(prev_indent < indent - 1){
            log_error("operate_import_mindmap_from_txt: Invalid indentation at line: %s", line);
            fclose(file);
            stack_destroy(node_stack);
            return -1;
        }else if(prev_indent == indent - 1){
            // ok, child node
            MutableNode *parent = (MutableNode *)stack_peek(node_stack);
            if(!parent){
                log_error("operate_import_mindmap_from_txt: Invalid parent node at indent %d", indent);
                fclose(file);
                stack_destroy(node_stack);
                return -1;
            }
            Event *e = event_create_add_last_child(
                tree_node_id((TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = parent }),
                text
            );
            int r = operate_commit_event(operate, e);
            if(r != 0){
                log_error("operate_import_mindmap_from_txt: Failed to commit ADD_LAST_CHILD event for text '%s'", text);
                fclose(file);
                stack_destroy(node_stack);
                return -1;
            }
            // Push new node onto stack
            TreeNode new_node = tree_find_by_id(operate->overlay, e->new_node_id);
            stack_push(node_stack, new_node.mut);
        }else {
            for(int i = 0; i < (prev_indent - indent); i++){
                stack_pop(node_stack);
            }
            MutableNode *sibling = (MutableNode *)stack_pop(node_stack);
            if(!sibling){
                log_error("operate_import_mindmap_from_txt: Invalid sibling node at indent %d", indent);
                fclose(file);
                stack_destroy(node_stack);
                return -1;
            }
            Event *e = event_create_add_sibling(
                tree_node_id((TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = sibling }),
                text
            );
            int r = operate_commit_event(operate, e);
            if(r != 0){
                log_error("operate_import_mindmap_from_txt: Failed to commit ADD_SIBLING event for text '%s'", text);
                fclose(file);
                stack_destroy(node_stack);
                return -1;
            }
            // Push new node onto stack
            TreeNode new_node = tree_find_by_id(operate->overlay, e->new_node_id);
            if(tree_node_is_null(new_node)){
                log_error("operate_import_mindmap_from_txt: Newly added sibling node is null for text '%s'", text);
                fclose(file);
                stack_destroy(node_stack);
                return -1;
            }
            stack_push(node_stack, new_node.mut);
        }
        prev_indent = indent;
    }

    fclose(file);
    stack_destroy(node_stack);
    log_debug("operate_import_mindmap_from_txt: Successfully imported mindmap from %s", filepath);
    return 0;
}

typedef struct {
    FILE *file;
} ExportContext;
// return: 0 to continue, non-zero to stop
int export_visitor(TreeNode n, int64_t depth, void *ctx){
    ExportContext *e_ctx = (ExportContext *)ctx;
    FILE *f = e_ctx->file;
    for(int i = 0; i < depth; i++){
        fputc('\t', f);
    }
    fputs(tree_node_text(n), f);
    fputc('\n', f);
    return 0;
};
int operate_export_mindmap_to_txt(Operate *operate, const char *filepath){
    FILE *file = fopen(filepath, "w");
    if(!file){
        log_error("operate_export_mindmap_to_txt: Failed to open file %s for writing", filepath);
        return -1;
    }

    ExportContext e_ctx = {
        .file = file
    };
    TreeNode root = operate->ui->current_node;
    tree_traverse_with_depth(operate->overlay, root, 0, export_visitor, &e_ctx, false);

    fclose(file);
    log_debug("operate_export_mindmap_to_txt: Successfully exported mindmap to %s", filepath);
    return 0;
}

int operate_export_mindmap_to_clipboard_txt(Operate *operate, TreeNode current_node){
    #if defined(__APPLE__)
    ExportContext e_ctx = {
        .file = popen("pbcopy", "w") // open memory stream for clipboard export
    };
    if(!e_ctx.file){
        log_error("operate_export_mindmap_to_clipboard_txt: Failed to open memory stream for clipboard export");
        return -1;
    }
    tree_traverse_with_depth(operate->overlay, current_node, 0, export_visitor, &e_ctx, false);
    pclose(e_ctx.file);
    
    #else
    log_error("operate_export_mindmap_to_clipboard_txt: Clipboard export not supported on this platform");
    return -1;
    #endif

    return 0;
}

int operate_import_mindmap(Operate *operate, const char *filepath){
    if(strstr(filepath, ".txt") != NULL){
        int r = operate_import_mindmap_from_txt(operate, filepath);
        if( r != 0){
            log_error("operate_import_mindmap: Failed to import mindmap from %s", filepath);
            return -1;
        }
        return 0;
    }
    // Implementation of mindmap import logic goes here
    // This is a placeholder implementation
    log_debug("operate_import_mindmap: Importing mindmap from %s", filepath);
}
int operate_export_mindmap(Operate *operate, const char *filepath){
    if(strstr(filepath, ".txt") != NULL){
        int r = operate_export_mindmap_to_txt(operate, filepath);
        if(r != 0){
            log_error("operate_export_mindmap: Failed to export mindmap to %s", filepath);
            return -1;
        }
        return 0;
    }
    log_debug("to be implemented: operate_export_mindmap to %s", filepath);
}

TreeNode operate_search_next(Operate *operate, TreeNode start_node){
    TreeOverlay *ov = operate->overlay;
    const char *query = operate->search_query;

    TreeNode current = start_node;

    while (true) {
        TreeNode next_node;
        if(operate->search_direction == SEARCH_DIRECTION_FORWARD){
            next_node = tree_node_dfs_next(ov, current);
        }else{
            next_node = tree_node_dfs_prev(ov, current);
        }

        // If we've looped back to the start node, stop searching
        if (tree_node_is_null(next_node) || tree_node_id(next_node) == tree_node_id(start_node)) {
            break;
        }

        const char *node_text = tree_node_text(next_node);
        if (strstr(node_text, query) != NULL) {
            return next_node; // Found partial match
        }

        current = next_node;
    }

    // No match found
    return (TreeNode){ .kind = TREE_NODE_NULL };
}

TreeNode operate_search_next_in_subtree(Operate *operate, TreeNode start_node, const char *search_term,
    bool (*filter)(TreeNode node, void *ctx), void *filter_ctx
){
    TreeOverlay *ov = operate->overlay;

    TreeNode current = tree_node_first_child_with_filter(ov, start_node, filter, filter_ctx);
    uint64_t start_id = tree_node_id(start_node);

    bool from_child = false;
    while (tree_node_id(current) != start_id) {
        TreeNode next_node;
        if(from_child){
            next_node = tree_node_next_sibling_with_filter(ov, current, filter, filter_ctx);
            if(tree_node_is_null(next_node)){
                current = tree_node_parent(ov, current);
                from_child = true;
                continue;
            }else{
                from_child = false;
            }
        }else{
            next_node = tree_node_first_child_with_filter(ov, current, filter, filter_ctx);
            if(tree_node_is_null(next_node)){
                next_node = tree_node_next_sibling_with_filter(ov, current, filter, filter_ctx);
                if(tree_node_is_null(next_node)){
                    current = tree_node_parent(ov, current);
                    from_child = true;
                    continue;
                }
            }
        }

        const char *node_text = tree_node_text(next_node);
        if (strcmp(node_text, search_term) == 0) {
            return next_node; // Found exact match
        }

        current = next_node;
    }

    // No match found
    return (TreeNode){ .kind = TREE_NODE_NULL };
}


TreeNode operate_search_prev(Operate *operate, TreeNode start_node){
    TreeOverlay *ov = operate->overlay;
    const char *query = operate->search_query;

    TreeNode current = start_node;
    TreeNode last_match = (TreeNode){ .kind = TREE_NODE_NULL };

    while (true) {
        TreeNode prev_node;
        if(operate->search_direction == SEARCH_DIRECTION_FORWARD){
            prev_node = tree_node_dfs_prev(ov, current);
        }else{
            prev_node = tree_node_dfs_next(ov, current);
        }

        // If we've looped back to the start node, stop searching
        if (tree_node_is_null(prev_node) || tree_node_id(prev_node) == tree_node_id(start_node)) {
            break;
        }

        const char *node_text = tree_node_text(prev_node);
        if (strstr(node_text, query) != NULL) {
            return prev_node; // Found partial match
        }

        current = prev_node;
    }

    return (TreeNode){ .kind = TREE_NODE_NULL };// not found
}

int operate_edit_node(Operate *operate, TreeNode node){
    const char *old_text = tree_node_text(node);
    // open tmp file with old_text
    char tmp_filename[] = "/tmp/umind_edit_XXXXXX";
    int fd = mkstemp(tmp_filename);
    if(fd == -1){
        log_error("operate_edit_node: Failed to create temporary file for editing");
        return -1;
    }
    FILE *tmp_file = fdopen(fd, "w+");
    if(!tmp_file){
        log_error("operate_edit_node: Failed to open temporary file for editing");
        close(fd);
        return -1;
    }
    fputs(old_text, tmp_file);
    fflush(tmp_file);
    fclose(tmp_file);
    // Launch editor
    const char *editor = getenv("EDITOR");
    if(!editor){
        editor = "vi"; // default to vi
    }
    char command[512];
    snprintf(command, sizeof(command), "%s %s", editor, tmp_filename);
    int ret = system(command);
    if(ret != 0){
        log_error("operate_edit_node: Editor exited with non-zero status");
        remove(tmp_filename);
        return -1;
    }
    // Read edited content
    tmp_file = fopen(tmp_filename, "r");
    if(!tmp_file){
        log_error("operate_edit_node: Failed to reopen temporary file for reading");
        remove(tmp_filename);
        return -1;
    }

    char line[4096];
    char *r = fgets(line, sizeof(line), tmp_file);
    if(!r){
        log_error("operate_edit_node: Failed to read edited content");
        fclose(tmp_file);
        remove(tmp_filename);
        return -1;
    }
    int len = strlen(line);
    if(len > 0 && line[len - 1] == '\n'){
        line[len - 1] = '\0';
    }
    Event *e = event_create_update_text(
        tree_node_id(node),
        line
    );
    int ir = operate_commit_event(operate, e);
    if(ir != 0){
        log_error("operate_edit_node: Failed to commit UPDATE_TEXT event");
        return -1;
    }

    TreeNode current = tree_find_by_id(operate->overlay, e->node_id);
    event_destroy(e);

    int prev_indent = 0;
    do {
        r = fgets(line, sizeof(line), tmp_file);
        if(r){
            len = strlen(line);
            // remove trailing newline
            if(len > 0 && line[len - 1] == '\n'){
                line[len - 1] = '\0';
            }
            int indent = 0;
            while(line[indent] == '\t'){
                indent++;  
            }
            char *text = line + indent;
            if(indent == prev_indent){
                // add sibling
                e = event_create_add_sibling(
                    tree_node_id(current),
                    text
                );
            }else if(indent == prev_indent + 1){
                // add child
                e = event_create_add_last_child(
                    tree_node_id(current),
                    text
                );
            }else if(indent < prev_indent){
                // add sibling to ancestor
                TreeNode ancestor = current;
                for(int i = 0; i < (prev_indent - indent); i++){
                    ancestor = tree_node_parent(operate->overlay, ancestor);
                    if(tree_node_is_null(ancestor)){
                        log_error("operate_edit_node: Invalid indentation in edited content");
                        break;
                    }
                }
                e = event_create_add_sibling(
                    tree_node_id(ancestor),
                    text
                );
            }else{
                log_error("operate_edit_node: Invalid indentation in edited content");
                break;
            }

            ir = operate_commit_event(operate, e);
            current = tree_find_by_id(operate->overlay, e->new_node_id);
            event_destroy(e);
            if(ir != 0){
                log_error("operate_edit_node: Failed to commit APPEND_TEXT event");
                return -1;
            }
            prev_indent = indent;
        }else{
            break;
        }
    }while(true);

    fclose(tmp_file);
    remove(tmp_filename);
    
}

typedef struct {
    TreeNode node;
    int depth;
} FoldLevelContext;
static FoldLevelContext* fold_level_context_create(TreeNode node, int depth){
    FoldLevelContext *ctx = (FoldLevelContext *)malloc(sizeof(FoldLevelContext));
    ctx->node = node;
    ctx->depth = depth;
    return ctx;
}

static uint64_t get_meta_node_id(Operate *operate, TreeNode current){
    TreeNode meta_node = tree_node_first_child(operate->overlay, current);
    if(!tree_node_is_null(meta_node) && strcmp(tree_node_text(meta_node), ".meta") == 0){
        return tree_node_id(meta_node);
    }
    return 0;
}

int operate_reduce_folding(Operate *operate, TreeNode current, int fold_level){
    uint64_t meta_node_id = get_meta_node_id(operate, current);
    // BFS
    Queue *queue = create_queue(1024);
    queue_enqueue(queue, fold_level_context_create(current, 0));

    while(!queue_is_empty(queue)){
        FoldLevelContext *ctx = queue_dequeue(queue);
        TreeNode node = ctx->node;
        int depth = ctx->depth;
        free(ctx);


        if(depth >= fold_level){
            // set fold
            if(!tree_node_is_collapsed(node) && !tree_node_is_null(tree_node_first_child(operate->overlay, node))){
                Event *event = event_create_collapse_node(tree_node_id(node));
                int r = operate_commit_event(operate, event);
                if(r != 0){
                    log_warn("operate_reduce_folding: Failed to commit COLLAPSE_NODE event for node id=%" PRIu64, tree_node_id(node));
                    return -1;
                }
            }
        }else{
            // set unfold
            if(tree_node_is_collapsed(node) && meta_node_id != tree_node_id(node)){
                Event *event = event_create_expand_node(tree_node_id(node));
                int r = operate_commit_event(operate, event);
                if(r != 0){
                    log_warn("operate_reduce_folding: Failed to commit EXPAND_NODE event for node id=%" PRIu64, tree_node_id(node));
                    return -1;
                }
            }
        }

        if(depth >= fold_level){
            continue;
        }

        TreeNode child = tree_node_first_child(operate->overlay, node);
        while(!tree_node_is_null(child)){
            queue_enqueue(queue, fold_level_context_create(child, depth + 1));
            child = tree_node_next_sibling(operate->overlay, child);
        }

    }
    queue_destroy(queue);
    return 0;
}

int operate_fold_more(Operate *operate, TreeNode current, int fold_level){
    uint64_t meta_node_id = get_meta_node_id(operate, current);
    Queue *queue = create_queue(1024);
    queue_enqueue(queue, fold_level_context_create(current, 0));

    while(!queue_is_empty(queue)){
        FoldLevelContext *ctx = queue_dequeue(queue);
        TreeNode node = ctx->node;
        int depth = ctx->depth;
        free(ctx);
        
        // set fold
        if(depth == fold_level 
            && !tree_node_is_collapsed(node) 
            && !tree_node_is_null(tree_node_first_child(operate->overlay, node))
            && meta_node_id != tree_node_id(node)
            ){
            Event *event = event_create_collapse_node(tree_node_id(node));
            int r = operate_commit_event(operate, event);
            if(r != 0){
                log_warn("operate_fold_more: Failed to commit COLLAPSE_NODE event for node id=%" PRIu64, tree_node_id(node));
                queue_destroy(queue);
                return -1;
            }
        }

        if(depth >= fold_level){
            continue;
        }

        TreeNode child = tree_node_first_child(operate->overlay, node);
        while(!tree_node_is_null(child)){
            queue_enqueue(queue, fold_level_context_create(child, depth + 1));
            child = tree_node_next_sibling(operate->overlay, child);
        }

    }
    queue_destroy(queue);
    return 0;
}


static key_t operate_get_ai_message_queue_key() {
    const char *exe_path = os_get_executable_path();
    if (!exe_path) {
        log_info("operate_get_ai_message_queue_key: Failed to read exe path");
        return -1;
    }

    key_t key = ftok(exe_path, 'A');
    if (key == -1) {
        log_info("operate_get_ai_message_queue_key: Failed to generate key for message queue");
        return -1;
    }
    return key;
}

struct msgbuf {
    long mtype;
    char mtext[4096];
};

int operate_ask_ai(Operate *operate, TreeNode node, enum query_scope scope){
    key_t key = operate_get_ai_message_queue_key();
    if (key == -1) {
        return 1;
    }
    int msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        log_info("operate_ask_ai: Failed to create message queue for AI assistant");
        return 1;
    }
    // clear message queue before sending new message
    msgctl(msgid, IPC_RMID, NULL);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        log_info("operate_ask_ai: Failed to create message queue for AI assistant");
        return 1;
    }

    struct msgbuf message;
    message.mtype = 1;
    switch(scope){
        case QUERY_SCOPE_CURRENT_NODE:
            const char *node_text = tree_node_text(node);
            snprintf(message.mtext, sizeof(message.mtext), "%s", node_text);
            break;
        case QUERY_SCOPE_SUBTREE: {
            char *mem = NULL;
            size_t len = 0;
            FILE *f = open_memstream(&mem, &len);
            ExportContext e_ctx = {
                .file = f
            };
            TreeNode root = operate->ui->current_node;
            tree_traverse_with_depth(operate->overlay, root, 0, export_visitor, &e_ctx, true);
            fclose(f);
            snprintf(message.mtext, sizeof(message.mtext), "%s", mem);
            free(mem);
            break;
        }
        case QUERY_SCOPE_WHOLE_TREE:
            log_debug("operate_ask_ai: Sending whole tree to AI assistant - to be implemented");
            break;
        default:
            log_info("operate_ask_ai: Invalid query scope");
            return 1;
    }

    message.mtext[sizeof(message.mtext) - 1] = '\0'; // Ensure null-termination
    size_t msg_len = strlen(message.mtext);
    if (msgsnd(msgid, &message, msg_len, 0) == -1) {
        log_info("operate_ask_ai: Failed to send message to AI assistant");
        return 1;
    }

    log_debug("operate_ask_ai: Successfully sent message to AI assistant");

    return 0;
}

int operate_output_ai_message() {
    key_t key = operate_get_ai_message_queue_key();
    if (key == -1) {
        return 1;
    }

    int msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        log_info("operate_output_ai_message: Failed to access message queue for AI assistant");
        return 1;
    }

    struct msgbuf message;
    ssize_t len = msgrcv(msgid, &message, sizeof(message.mtext), 0, IPC_NOWAIT);
    if (len == -1) {
        log_info("operate_output_ai_message: No message received from AI assistant");
        return 1;
    }
    message.mtext[len] = '\0'; // Null-terminate the received message
    message.mtext[sizeof(message.mtext) - 1] = '\0'; // Ensure null-termination

    printf("%s", message.mtext);

    return 0;
}

static bool is_meta(Operate *operate, TreeNode node){
    if(tree_node_is_null(node)){
        return false;
    }
    const char *text = tree_node_text(node);
    if(strcmp(text, ".meta") == 0){
        return true;
    }
    TreeNode parent = tree_node_parent(operate->overlay, node);
    return is_meta(operate, parent);
}

TreeNode operate_edit_history_last_record(Operate *operate, TreeNode edit_history_node){
    TreeNode last_record = tree_node_first_child(operate->overlay, edit_history_node);
    while(!tree_node_is_null(last_record)){
        TreeNode child = tree_node_first_child(operate->overlay, last_record);
        if(tree_node_is_null(child)){
            break;
        }
        last_record = child;
    }
    return last_record;
}

/**
 * don't free the returned string, it's a static buffer 
 */
static char *tree_node_id_str(TreeNode node){
    static char buf[32];
    if(tree_node_is_null(node)){
        snprintf(buf, sizeof(buf), "null");
    }else{
        snprintf(buf, sizeof(buf), "%" PRIu64, tree_node_id(node));
    }
    return buf;
}

static uint64_t str2id(const char *str){
    uint64_t id;
    sscanf(str, "%" SCNu64, &id);
    return id;
}

int operate_edit_history_record(Operate *operate, Event *event){
    TreeNode node = (TreeNode){ .kind = TREE_NODE_NULL };
    switch(event->type){
        case EVENT_ADD_LAST_CHILD:
        case EVENT_ADD_SIBLING:
            node = tree_find_by_id(operate->overlay, event->new_node_id);
            break;
        case EVENT_MOVE_SUBTREE:
        case EVENT_UPDATE_TEXT:
            node = tree_find_by_id(operate->overlay, event->node_id);
            break;
        default:
            log_info("operate_edit_history_record: Unsupported event type for editing: %d", event->type);
            return -1;
    }
    if(tree_node_is_null(node)){
        log_error("operate_edit_history_record: Failed to find node for event type %d", event->type);
        log_ui_message("Failed to find node for event type %d", event->type);
        return -1;
    }
    if(is_meta(operate, node)){
        // do not need to record history for meta node changes
        return 0;
    }

    // edit history stack add
    operate_begin_transaction(operate);
    TreeNode edit_history_node = app_metadata_key_node(operate, APP_META_EDIT_HISTORY);
    assert(!tree_node_is_null(edit_history_node));
    TreeNode last_record = operate_edit_history_last_record(operate, edit_history_node);
    char * node_id_str = tree_node_id_str(node);
    if(tree_node_is_null(last_record)) {
        Stack *stack = stack_create(1024);
        TreeNode n = node;
        while(!tree_node_is_null(n)){
            TreeNode *pn = (TreeNode *)malloc(sizeof(TreeNode));
            *pn = n;
            stack_push(stack, pn);
            n = tree_node_parent(operate->overlay, n);
        }
        TreeNode current_parent = edit_history_node;
        while(!stack_is_empty(stack)){
            TreeNode *pn = (TreeNode *)stack_pop(stack);
            Event *e = event_create_add_last_child(
                tree_node_id(current_parent),
                tree_node_text(*pn)
            );
            int r = operate_commit_event(operate, e);
            if(r != 0){
                log_error("operate_edit_history_record: Failed to commit ADD_LAST_CHILD event for edit history");
                stack_destroy(stack);
                return -1;
            }
            current_parent = tree_find_by_id(operate->overlay, e->new_node_id);
            event_destroy(e);
            free(pn);
        }
        stack_destroy(stack);
        Event *record_event = event_create_add_last_child(
            tree_node_id(current_parent),
            node_id_str
        );
        int r = operate_commit_event(operate, record_event);
        if(r != 0){
            log_error("operate_edit_history_record: Failed to commit ADD_LAST_CHILD event for edit history record");
            return -1;
        }
        event_destroy(record_event);
    }else{
        uint64_t last_record_node_id = str2id(tree_node_text(last_record));
        Stack *last_record_path_stack = stack_create(1024);
        TreeNode n = tree_find_by_id(operate->overlay, last_record_node_id);
        while(!tree_node_is_null(n)){
            TreeNode *pn = (TreeNode *)malloc(sizeof(TreeNode));
            *pn = n;
            stack_push(last_record_path_stack, pn);
            n = tree_node_parent(operate->overlay, n);
        }

        uint64_t new_record_node_id = tree_node_id(node);
        Stack *new_record_path_stack = stack_create(1024);
        TreeNode n2 = node;
        while(!tree_node_is_null(n2)){
            TreeNode *pn = (TreeNode *)malloc(sizeof(TreeNode));
            *pn = n2;
            stack_push(new_record_path_stack, pn);
            n2 = tree_node_parent(operate->overlay, n2);
        }

        Stack *common_ancestor_stack = stack_create(1024);
        while(!stack_is_empty(new_record_path_stack) && !stack_is_empty(last_record_path_stack)){
            TreeNode *pn1 = (TreeNode *)stack_peek(new_record_path_stack);
            TreeNode *pn2 = (TreeNode *)stack_peek(last_record_path_stack);
            if(tree_node_id(*pn1) == tree_node_id(*pn2) 
                && tree_node_id(*pn1) != new_record_node_id
                && tree_node_id(*pn2) != last_record_node_id
                ){
                stack_push(common_ancestor_stack, pn1);
                stack_pop(new_record_path_stack);
                stack_pop(last_record_path_stack);
            }else{
                break;
            }
        }
        stack_destroy(last_record_path_stack);
        
        // new_record_path_stack: |(stack bottom) new record node -> ... -> (top) | common ancestor node (excluded)
        // common_ancestor_stack: |(stack bottom) root -> ... -> common ancestor node
        // find current_parent
        TreeNode current_parent = (TreeNode ){.kind = TREE_NODE_NULL};
        if(stack_is_empty(common_ancestor_stack)){
            current_parent = edit_history_node;
        }else{
            TreeNode n = edit_history_node;
            while(!stack_is_empty(common_ancestor_stack)){
                n = tree_node_first_child(operate->overlay, n);
                stack_pop(common_ancestor_stack);
            }
            current_parent = n;
        }

        while(!stack_is_empty(new_record_path_stack)){
            TreeNode *pn = (TreeNode *)stack_pop(new_record_path_stack);
            Event *e = event_create_add_first_child(
                tree_node_id(current_parent),
                tree_node_text(*pn)
            );
            int r = operate_commit_event(operate, e);
            if(r != 0){
                log_error("operate_edit_history_record: Failed to commit ADD_FIRST_CHILD event for edit history");
                stack_destroy(new_record_path_stack);
                stack_destroy(common_ancestor_stack);
                return -1;
            }
            current_parent = tree_find_by_id(operate->overlay, e->new_node_id);
            event_destroy(e);
        }
        stack_destroy(new_record_path_stack);
        while(!stack_is_empty(common_ancestor_stack)){
            TreeNode *pn = (TreeNode *)stack_pop(common_ancestor_stack);
            free(pn);
        }
        stack_destroy(common_ancestor_stack);
        Event *record_event = event_create_add_last_child(
            tree_node_id(current_parent),
            node_id_str
        );
        int r = operate_commit_event(operate, record_event);
        if(r != 0){
            log_error("operate_edit_history_record: Failed to commit UPDATE_TEXT event for edit history record");
            event_destroy(record_event);
            return -1;
        }
    }
    operate_commit_transaction(operate);
}

bool is_search_match(const char *node_text, const char *query){
    if(strcmp(node_text, query) == 0){
        return true; // exact match
    }
    int num_sqare_brackets = 0;
    for(const char *p = node_text; *p; p++){
        if(*p == '['){
            num_sqare_brackets++;
        }else{
            break;
        }
    }
    if(num_sqare_brackets > 0){
        const char *text_without_left_brackets = node_text + num_sqare_brackets;
        const char *s = strstr(text_without_left_brackets, query);
        int len_without_left_brackets = strlen(text_without_left_brackets);
        int len_query = strlen(query);
        if( len_query + num_sqare_brackets == len_without_left_brackets
        && s == text_without_left_brackets){
            return true; // match after removing square brackets
        }
    }
    return false;
}

/**
 * return: 
 */
TreeNode operate_search_subtree(Operate *operate, TreeNode start_node, const char **keys, int nkeys, TreeNode skip_node){
    const char *text = tree_node_text(start_node);
    if(text[0] == '.' && strcmp(text, ".metadata") != 0){
        // skip meta nodes
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    TreeNode child;
    if(is_search_match(text, (char *)keys[0])){
        if(nkeys == 1){
            return start_node;
        }
        child = tree_node_first_child(operate->overlay, start_node);
        while(!tree_node_is_null(child)){
            TreeNode r = operate_search_subtree(operate, child, keys+1, nkeys - 1, (TreeNode){.kind = TREE_NODE_NULL});
            if(!tree_node_is_null(r)){
                return r;
            }else{
                child = tree_node_next_sibling(operate->overlay, child);
            }
        }
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    // if(nkeys == 0){
        // only match leaf node
        // return (TreeNode){ .kind = TREE_NODE_NULL };
    // }
    
    child = tree_node_first_child(operate->overlay, start_node);
    while(!tree_node_is_null(child)){
        if(tree_node_id(child) != tree_node_id(skip_node)){
            TreeNode r = operate_search_subtree(operate, child, keys, nkeys, (TreeNode){.kind = TREE_NODE_NULL});
            if(!tree_node_is_null(r)){
                return r;
            }
        }
        child = tree_node_next_sibling(operate->overlay, child);
    }
    
     return (TreeNode){ .kind = TREE_NODE_NULL };
}

TreeNode operate_search_hierachy_keys(Operate *operate, TreeNode current, const char **keys, int nkeys){
    TreeNode child = (TreeNode){ .kind = TREE_NODE_NULL };
    TreeNode parent = current;
    while(!tree_node_is_null(parent)){
        TreeNode r = operate_search_subtree(operate, parent, keys, nkeys, child);
        if(!tree_node_is_null(r)){
            return r;
        }else{

        }

        child = parent;
        parent = tree_node_parent(operate->overlay, parent);
    }
    return (TreeNode){ .kind = TREE_NODE_NULL };
}