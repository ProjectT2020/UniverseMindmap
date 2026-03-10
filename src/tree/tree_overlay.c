#include "tree_overlay.h"
#include "tree_storage.h"
#include "../utils/logging.h"
#include "../utils/radix_tree.h"
// #include "../utils/hashtable_u64.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "../event/event_types.h"
#include "../event/event.h"

static void tree_overlay_propagate_layout_change(TreeOverlay *ov, TreeNode *n, 
    int64_t delta_height // delta must be signed to allow negative changes
);
static TreeNode radixtree_value_to_tree_node(TreeOverlay *ov, void *offset_or_pointer);

// ===== MutableNode creation =====
static MutableNode *mut_node_new(NodeID id,
                                 TreeNode parent,
                                 TreeNode first_child,
                                 TreeNode next_sibling,
                                 uint64_t descendents,
                                 TreeNodeFlags flags,
                                 const char *text
                                ) {
    if(text == NULL){
        log_error("mut_node_new: text is NULL");
        return NULL;
    }
    MutableNode *n = calloc(1, sizeof(MutableNode));
    n->node_id = id;

    n->parent = parent;
    n->first_child = first_child;
    n->next_sibling = next_sibling;

    n->layout_height = 1;
    n->descendents = descendents;

    n->type = NODE_TYPE_DEFAULT;

    bool hidden_node = false;
    if(text && text[0] == '.' ){
        hidden_node = true;
    }
    if(hidden_node){
        n->flags |= TREE_FLAG_HIDDEN;
    }

    n->flags = flags;
    n->text = strdup(text ? text : "");
    n->resource = NULL;

    return n;
}

TreeOverlay* tree_overlay_open(TreeView *base, const char *path) {
    if (!base) {
        return NULL;  // Invalid input: caller should use tree_overlay_create_empty for empty overlays
    }

    NodeRef root_ref = tree_root(base);
    if (node_is_null(root_ref)) {
        return NULL;  // Invalid TreeView: no root
    }

    TreeOverlay *ov = calloc(1, sizeof(TreeOverlay));
    if (!ov) return NULL;

    ov->path = strdup(path);
    ov->tree_view = base;
    // ov->node_id_map = u64_hashtable_create(NULL, 1024);
    // if (!ov->node_id_map) {
    //     if (ov->node_id_map) u64_hashtable_destroy(ov->node_id_map);
    //     free(ov);
    //     return NULL;
    // }

    // static char id_map_path_buffer[1024];
    // snprintf(id_map_path_buffer, sizeof(id_map_path_buffer), "%s.id_map", path);
    // ov->id_map_path = strdup(id_map_path_buffer);
    // int fd = open(ov->id_map_path, O_RDONLY);
    // if(fd < 0 ){
    //     log_error("tree_overlay_open: Failed to open id_map file %s", ov->id_map_path);
    //     free(ov->path);
    //     free(ov);
    //     return NULL;
    // }
    // struct stat st;
    // if (fstat(fd, &st) < 0) {
    //     perror("fstat");
    //     close(fd);
    //     return NULL;
    // }

    // size_t size = st.st_size;
    // if (size == 0) {
    //     close(fd);
    //     return NULL;
    // }

    // void *base_id_map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    // if(base_id_map == MAP_FAILED){
    //     log_error("tree_overlay_open: Failed to mmap id_map file %s", ov->id_map_path);
    //     close(fd);
    //     free(ov->path);
    //     free(ov);
        // return NULL;
    // }
    // close(fd);
    
    ov->id_map = tree_view_id_map(base);
    //  radix_tree_deserialize(base_id_map, size);
    if(!ov->id_map){
        free(ov->path);
        free(ov);
        return NULL;
    }

    ov->root.kind = TREE_NODE_DISK;
    ov->root.disk = root_ref;
    ov->last_applied_lsn = base->hdr->checkpoint_lsn;
    
    // Initialize max_node_id from loaded tree
    ov->max_node_id = base->hdr->max_node_id;
    
    return ov;
}
TreeOverlay* tree_overlay_create_empty(const char *path) {
    TreeOverlay *ov = calloc(1, sizeof(TreeOverlay));
    if (!ov) return NULL;

    ov->path = strdup(path);
    ov->tree_view = NULL;  // No base tree
    // ov->node_id_map = u64_hashtable_create(NULL, 1024);
    // if (!ov->node_id_map) {
    //     if (ov->node_id_map) u64_hashtable_destroy(ov->node_id_map);
    //     free(ov);
    //     return NULL;
    // }
    ov->id_map = radix_tree_create(NULL);   
    if(!ov->id_map){
        free(ov->path);
        free(ov);
        return NULL;
    }

    ov->max_node_id = 1;  // No nodes yet
    MutableNode *new_mut = mut_node_new(ov->max_node_id,
        (TreeNode){ .kind = TREE_NODE_NULL }, // parent
        (TreeNode){ .kind = TREE_NODE_NULL }, // no first child
        (TreeNode){ .kind = TREE_NODE_NULL }, // next sibling
        0, // descendents
        TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN, // flags
        "universe-mindmap" // text
    );
    radix_tree_insert_mem_address(ov->id_map, new_mut->node_id, (void *)new_mut);
    ov->root = (TreeNode){
        .kind = TREE_NODE_MUTABLE,
        .mut = new_mut
    };
    ov->last_applied_lsn = 0;

    return ov;
}

static inline TreeNode overlay_promote_disk_node(TreeOverlay *ov, NodeRef ref) {
    if (node_is_null(ref))
        return (TreeNode){ .kind = TREE_NODE_NULL };

    assert(ov->id_map) ;
    void *offset_or_pointer = radix_tree_lookup(ov->id_map, node_id(ref));
    if (offset_or_pointer) {
        return radixtree_value_to_tree_node(ov, offset_or_pointer);
    }

    return (TreeNode){
        .kind = TREE_NODE_DISK,
        .disk = ref
    };
}

TreeNode tree_node_parent(TreeOverlay *ov, TreeNode n) {
    if (n.kind == TREE_NODE_MUTABLE) {
        if (!tree_node_is_null(n.mut->parent)) {
            if(n.mut->parent.kind == TREE_NODE_DISK){
                return overlay_promote_disk_node(ov, n.mut->parent.disk);
            }
            return n.mut->parent;
        }
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }

    if (n.kind == TREE_NODE_DISK) {
        NodeRef pref = node_parent(n.disk);
        return overlay_promote_disk_node(ov, pref);
    }

    return (TreeNode){ .kind = TREE_NODE_NULL };
}
void tree_node_set_parent(TreeOverlay *ov, TreeNode *n, TreeNode parent){
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_MUTABLE){
        n->mut->parent = parent;
        return;
    }
    log_warn("tree_node_set_parent: Cannot set parent for NULL node");
    return ;
}


uint64_t tree_node_id(TreeNode n) {
    if (n.kind == TREE_NODE_MUTABLE)
        return n.mut->node_id;
    
    if (n.kind == TREE_NODE_DISK)
        return node_id(n.disk);
    
    return 0;  // NULL node has ID 0
}

const char* tree_node_text(TreeNode n) {
    if (n.kind == TREE_NODE_MUTABLE)
        return n.mut->text;
    
    if (n.kind == TREE_NODE_DISK)
        return node_text(n.disk);
    
    log_warn("tree_node_text: NULL node has no text");
    return "";
}

TreeNodeFlags tree_node_flags(TreeNode n) {
    if (n.kind == TREE_NODE_MUTABLE)
        return n.mut->flags;
    
    if (n.kind == TREE_NODE_DISK)
        return node_flags(n.disk);
    
    return 0;
}

bool tree_node_hidden(TreeNode n) {
    uint64_t flags = tree_node_flags(n);
    return (flags & TREE_FLAG_HIDDEN) != 0;
}

bool tree_node_collapsed(TreeNode n){
    uint64_t flags = tree_node_flags(n);
    return (flags & TREE_FLAG_COLLAPSED) != 0;
}
bool tree_node_is_collapsed(TreeNode n){
    return tree_node_collapsed(n);
}

bool tree_node_show_hidden_children(TreeNode n){
    uint64_t flags = tree_node_flags(n);
    return (flags & TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN) != 0;
}

// ===== Copy-on-write =====

void overlay_materialize(TreeOverlay *ov, TreeNode *node) {
    if(node->kind == TREE_NODE_NULL){
        log_warn("overlay_materialize: Cannot materialize NULL node");
        return;
    }
    if(node->kind == TREE_NODE_MUTABLE){
        // Already mutable
        log_error("overlay_materialize: Node is already mutable (id=%" PRIu64 ", %s)", tree_node_id(*node), tree_node_text(*node));
        return;
    }
    void *offset_or_pointer = radix_tree_lookup(ov->id_map, tree_node_id(*node));
    if (offset_or_pointer) { 
        TreeNode indexed_node = radixtree_value_to_tree_node(ov, offset_or_pointer);
        if(indexed_node.kind == TREE_NODE_MUTABLE){
            log_warn("overlay_materialize: Node already materialized in id_map (id=%" PRIu64 ", %s)", tree_node_id(*node), tree_node_text(*node));
            node->kind = TREE_NODE_MUTABLE;
            node->mut = (MutableNode *)indexed_node.mut;
            return;
        }
    }

    // node.kind == TREE_NODE_DISK
    uint64_t node_id_val = tree_node_id(*node);
    assert(node_id_val != 0);
    TreeNodeFlags flags = tree_node_flags(*node);
    MutableNode *mn = mut_node_new( node_id_val, 
        tree_node_parent(ov, *node), tree_node_first_child(ov, *node), tree_node_next_sibling(ov, *node), 
        tree_node_descendents(*node),
        flags,
        tree_node_text(*node) 
    );
    mn->layout_height = node_layout_height(node->disk);
    radix_tree_insert_mem_address(ov->id_map, mn->node_id, (void *)mn);

    // ExtTLVs
    if(flags & TREE_FLAG_HAS_RESOURCE){
        mn->resource = node_resource(node->disk); 
    }

    node->kind = TREE_NODE_MUTABLE;
    node->mut = mn;


    // ID -> MutableNode* 
    // assert(ov->node_id_map);
    // u64_hashtable_insert(ov->node_id_map, node_id_val, (void*)mn);

    // cache id_map
    // tree_cache_id_mutable_node(ov, mn);

    if(node_id_val == tree_node_id(ov->root)){
        ov->root = *node;
    }
    return;
}

TreeNode tree_node_first_child(TreeOverlay *ov, TreeNode n){
    if(n.kind == TREE_NODE_DISK){
        n = overlay_promote_disk_node(ov, n.disk);
    }

    if (n.kind == TREE_NODE_MUTABLE) {
        if(n.mut->first_child.kind == TREE_NODE_DISK){
            return overlay_promote_disk_node(ov, n.mut->first_child.disk);
        }
        return n.mut->first_child;
    }

    if (n.kind == TREE_NODE_DISK) {
        NodeRef cref = node_first_child(n.disk);
        return overlay_promote_disk_node(ov, cref);
    }

    return (TreeNode){ .kind = TREE_NODE_NULL };
}

void tree_node_set_first_child(TreeOverlay *ov, TreeNode *n, TreeNode first_child){
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_MUTABLE){
        n->mut->first_child = first_child;
        return;
    }
    log_warn("tree_node_set_first_child: Cannot set first_child for NULL node");
    return ;
}

TreeNode tree_node_next_sibling(TreeOverlay *ov, TreeNode n){
    if(n.kind == TREE_NODE_DISK){
        n = overlay_promote_disk_node(ov, n.disk);
    }

    if( n.kind == TREE_NODE_MUTABLE) {
        if(n.mut->next_sibling.kind == TREE_NODE_DISK){
            return overlay_promote_disk_node(ov, n.mut->next_sibling.disk);
        }
        return n.mut->next_sibling;
    }
    if( n.kind == TREE_NODE_DISK) {
        NodeRef sref = node_next_sibling(n.disk);
        return overlay_promote_disk_node(ov, sref);
    }
    return (TreeNode){ .kind = TREE_NODE_NULL };
}

bool tree_node_is_first_child(TreeOverlay *ov, TreeNode n){
    TreeNode p = tree_node_parent(ov, n);
    if(tree_node_is_null(p)){
        return false;
    }
    TreeNode first_child = tree_node_first_child(ov, p);
    return (tree_node_id(first_child) == tree_node_id(n));
}

void tree_node_set_next_sibling(TreeOverlay *ov, TreeNode *n, TreeNode next_sibling){
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_MUTABLE){
        n->mut->next_sibling = next_sibling;
        return;
    }
    log_warn("tree_node_set_next_sibling: Cannot set next_sibling for NULL node");
    return ;
}
void tree_node_set_collapse(TreeOverlay *ov, TreeNode *n, bool collapsed){
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_NULL){
        log_warn("tree_node_set_collapse: Cannot set collapse for NULL node");
    }
    bool origin_collapsed = tree_node_collapsed(*n);
    bool changed = origin_collapsed != collapsed;
    if(changed && n->kind == TREE_NODE_MUTABLE){
        TreeNode parent = tree_node_parent(ov, *n);
        if(collapsed){
            n->mut->flags |= TREE_FLAG_COLLAPSED;
            tree_overlay_propagate_layout_change(ov, &parent, -(n->mut->layout_height - 1));
        } else {
            n->mut->flags &= ~TREE_FLAG_COLLAPSED;
            tree_overlay_propagate_layout_change(ov, &parent,   n->mut->layout_height - 1);
        }
        return;
    }
    return ;
}

void tree_node_set_hidden(TreeOverlay *ov, TreeNode *n, bool hidden) {
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_MUTABLE){
        if(hidden){
            n->mut->flags |= TREE_FLAG_HIDDEN;
        } else {
            n->mut->flags &= ~TREE_FLAG_HIDDEN;
        }
        return;
    }
    log_warn("tree_node_set_hidden: Cannot set hidden for NULL node");
    return ;
}

void tree_node_set_layout_height(TreeOverlay *ov, TreeNode *n, uint64_t height) {
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_MUTABLE){
        uint64_t old_layout_height = n->mut->layout_height;
        n->mut->layout_height = height;
        int64_t delta_height = (int64_t)height - (int64_t)old_layout_height;
        if(delta_height != 0){
            TreeNode parent = tree_node_parent(ov, *n);
            // tree_overlay_propagate_layout_change(ov, &parent, delta_height);
        }
        return;
    }
    log_warn("tree_node_set_layout_height: Cannot set layout_height for NULL node");
    return ;
}


void tree_layout_change_on_show_hidden_children(TreeOverlay *ov, TreeNode *n){
    if(tree_node_is_null(*n)){
        return;
    }
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind != TREE_NODE_MUTABLE){
        log_error("tree_layout_change_on_show_hidden_children: Node is not mutable after materialization");
        return;
    }
    bool show_hidden = tree_node_show_hidden_children(*n);
    // Recalculate layout height
    uint64_t old_layout_height = n->mut->layout_height;
    uint64_t new_layout_height = 0; // self
    TreeNode child = tree_node_first_child(ov, *n);
    while(!tree_node_is_null(child)){
        if(tree_node_hidden(child) && !show_hidden){
            child = tree_node_next_sibling(ov, child);
            continue;
        }
        if(tree_node_collapsed(child)){
            new_layout_height += 1;
        } else {
            new_layout_height += tree_node_layout_height(ov, child);
        }
        child = tree_node_next_sibling(ov, child);
    }
    if(new_layout_height == 0){
        new_layout_height = 1; // at least self
    }
    n->mut->layout_height = new_layout_height;
    int64_t delta_height = (int64_t)new_layout_height - (int64_t)old_layout_height;
    if(delta_height != 0){
        TreeNode parent = tree_node_parent(ov, *n);
        tree_overlay_propagate_layout_change(ov, &parent, delta_height);
    }
}

void tree_node_set_show_hidden_children(TreeOverlay *ov, TreeNode *n, bool show){
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    bool changed = tree_node_show_hidden_children(*n) != show;
    if(changed && n->kind == TREE_NODE_MUTABLE){
        if(show){
            n->mut->flags |= TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN;
        } else {
            n->mut->flags &= ~TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN;
        }
        tree_layout_change_on_show_hidden_children(ov, n);
        return;
    }
    log_warn("tree_node_set_show_hidden_children: Cannot set show_hidden_children for NULL node");
    return ;
}

void tree_node_set_text(TreeOverlay *ov, TreeNode *n, const char *new_text){
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind == TREE_NODE_MUTABLE){
        free(n->mut->text);
        n->mut->text = strdup(new_text ? new_text : "");
        if(new_text && new_text[0] == '.' ){
            n->mut->flags |= TREE_FLAG_HIDDEN;
        } else {
            n->mut->flags &= ~TREE_FLAG_HIDDEN;
        }
        return;
    }
    log_warn("tree_node_set_text: Cannot set text for NULL node");
    return ;
}

static void tree_overlay_propagate_descendents_change(TreeOverlay *ov, TreeNode *n, int64_t delta_descendents) {
    if(tree_node_is_null(*n)){
        return;
    }
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind != TREE_NODE_MUTABLE){
        log_error("tree_overlay_propagate_descendents_change: Node is not mutable after materialization");
        return;
    }
    n->mut->descendents += delta_descendents;
    TreeNode parent = tree_node_parent(ov, *n);
    tree_overlay_propagate_descendents_change(ov, &parent, delta_descendents);
}

static void tree_overlay_propagate_layout_change(TreeOverlay *ov, TreeNode *n, int64_t delta_height){
    if(tree_node_is_null(*n)){
        return;
    }
    if(n->kind == TREE_NODE_DISK){
        overlay_materialize(ov, n);
    }
    if(n->kind != TREE_NODE_MUTABLE){
        log_error("tree_overlay_propagate_layout_change: Node is not mutable after materialization");
        return;
    }
    n->mut->layout_height += delta_height;
    if(n->mut->layout_height < 0){
        log_error("tree_overlay_propagate_layout_change: layout_height became less than 1");
        n->mut->layout_height = 1;
    }else if(n->mut->layout_height == 0){
        n->mut->layout_height = 1;
    }
    if(tree_node_collapsed(*n)){// do not propagate layout change for collapsed nodes
        return;
    }
    TreeNode parent = tree_node_parent(ov, *n);
    tree_overlay_propagate_layout_change(ov, &parent, delta_height);
}

TreeNode tree_add_first_child(TreeOverlay *overlay, TreeNode *parent, const char *text) {
    if(parent->kind == TREE_NODE_DISK){
        overlay_materialize(overlay, parent);
    }
    TreeNode next_sibling = tree_node_first_child(overlay, *parent);// next sibling
    MutableNode *new_mut = mut_node_new( ++overlay->max_node_id,
        *parent, // parent
        (TreeNode){ .kind = TREE_NODE_NULL }, // no first child
        next_sibling,// next sibling
        0,// zero descendents
        TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN,// flags
        text// text
    );
    tree_node_set_first_child(overlay, parent, (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut });
    tree_overlay_propagate_descendents_change(overlay, parent, 1);
    if(!tree_node_is_null(next_sibling)){
        tree_overlay_propagate_layout_change(overlay, parent, new_mut->layout_height);
    }

    TreeNode new_node =  (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut };

    radix_tree_insert_mem_address(overlay->id_map, new_mut->node_id, (void *)new_mut);
    return new_node;
}

TreeNode tree_add_single_node(TreeOverlay *ov, TreeNode *parent, NodeID next_sibling_id, TreeNodeFlags flags, char *text, NodeID node_id) {
    TreeNode first_child = tree_node_first_child(ov, *parent);
    void *offset_or_pointer = radix_tree_lookup(ov->id_map, node_id);
    if(offset_or_pointer != NULL){
    TreeNode existing_node = radixtree_value_to_tree_node(ov, offset_or_pointer);
        if(!tree_node_is_null(existing_node)){
            log_error("tree_add_single_node: node_id %" PRIu64 " already exists", node_id);
            return (TreeNode){ .kind = TREE_NODE_NULL};
        }
    }
    // if(node_id != ov->max_node_id){
        // log_warn("tree_add_single_node: node_id %" PRIu64 " is not equal to max_node_id %" PRIu64, node_id, ov->max_node_id);
        // node_id = ++ov->max_node_id;
    // }
    if(parent->kind == TREE_NODE_DISK){
        overlay_materialize(ov, parent);
    }
    MutableNode *new_mut = mut_node_new( node_id,
        *parent,
        (TreeNode){ .kind = TREE_NODE_NULL },// no first child
        (TreeNode){ .kind = TREE_NODE_NULL },// next sibling to be set
        0, // zero descendents 
        flags,// flags
        text// text
    );
    TreeNode new_node = (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut };
    int64_t delta_height = new_mut->layout_height;
    if(tree_node_is_null(first_child) || next_sibling_id == tree_node_id(first_child)){// insert as first child
        tree_node_set_first_child(ov, parent, new_node);
        tree_node_set_next_sibling(ov, &new_node, first_child);
        if(tree_node_is_null(first_child)){
            // no layout change if parent has no children
            delta_height = 0;
        }
    }else {
        // find previous sibling
        TreeNode prev_sibling = (TreeNode){ .kind = TREE_NODE_NULL };
        TreeNode curr = first_child;
        while (!tree_node_is_null(curr)) {
            if (tree_node_id(curr) == next_sibling_id) 
                break;
            prev_sibling = curr;
            curr = tree_node_next_sibling(ov, curr);
        }
        tree_node_set_next_sibling(ov, &new_node, curr);
        tree_node_set_next_sibling(ov, &prev_sibling, new_node);
    }
    radix_tree_insert_mem_address(ov->id_map, new_mut->node_id, (void *)new_mut);
    // update parent metadata
    tree_overlay_propagate_descendents_change(ov, parent, 1);
    tree_overlay_propagate_layout_change(ov, parent, delta_height);

    return (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut };
}

int tree_delete_single_node(TreeOverlay *overlay, TreeNode *node) {
    if(tree_node_is_null(*node)){
        log_warn("tree_delete_single_node: Cannot delete NULL node");
        return -1;
    }
    NodeID node_id = tree_node_id(*node);
    TreeNode first_child = tree_node_first_child(overlay, *node);
    if(!tree_node_is_null(first_child)){
        log_warn("tree_delete_single_node: Cannot delete node with children (id=%" PRIu64 ", %s)", node_id, tree_node_text(*node));
        return -1;
    }
    TreeNode parent = tree_node_parent(overlay, *node);
    TreeNode prev_sibling = tree_node_prev_sibling(overlay, *node);
    TreeNode next_sibling = tree_node_next_sibling(overlay, *node);
    int effective_parent_height = 0;
    if(tree_node_is_null(prev_sibling) && tree_node_is_null(next_sibling)){
        // node is the only child
        effective_parent_height = 1;
    }
    if(tree_node_is_null(prev_sibling)){
        // node is first child
        tree_node_set_first_child(overlay, &parent, next_sibling);
    }else{
        // node is not first child
        tree_node_set_next_sibling(overlay, &prev_sibling, next_sibling);
    }
    // propagate changes
    tree_overlay_propagate_descendents_change(overlay, &parent, -1);
    tree_overlay_propagate_layout_change(overlay, &parent, -tree_node_layout_height(overlay, *node) + effective_parent_height);

    // remove from id_map and release resources
    void *offset_or_pointer = radix_tree_lookup(overlay->id_map, node_id);
    if (offset_or_pointer) {
        TreeNode n = radixtree_value_to_tree_node(overlay, offset_or_pointer);
        if(n.kind == TREE_NODE_MUTABLE){
            // free mutable node
            free(n.mut->text);
            free(n.mut);
        }
        radix_tree_delete(overlay->id_map, node_id);
    }

    // set node to NULL
    *node = (TreeNode){ .kind = TREE_NODE_NULL };
    return 0;
}

TreeNode tree_add_sibling(TreeOverlay *overlay, TreeNode *node, const char *text) {
    if(tree_node_id(*node) == tree_node_id(overlay->root)){
        log_warn("tree_add_sibling: Cannot add sibling to root node");
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    if(node->kind == TREE_NODE_DISK){
        overlay_materialize(overlay, node);
    }
    TreeNode parent =  tree_node_parent(overlay, *node);
    // create new mutable node
    MutableNode *new_mut = mut_node_new(
        ++overlay->max_node_id,
        parent,
        (TreeNode){ .kind = TREE_NODE_NULL },// no first child
        node->mut->next_sibling,
        0, // zero descendents 
        TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN,// flags
        text// text
    );
    tree_overlay_propagate_descendents_change(overlay, &parent, 1);
    tree_overlay_propagate_layout_change(overlay, &parent, new_mut->layout_height);

    tree_node_set_next_sibling(overlay, node, (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut });
    TreeNode new_node = (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut };
    // tree_cache_id_mutable_node(overlay, new_node.mut);
    radix_tree_insert_mem_address(overlay->id_map, new_mut->node_id, (void *)new_mut);
    return new_node;
}

TreeNode tree_node_last_child(TreeOverlay *ov, TreeNode n){
    TreeNode last_child = (TreeNode){ .kind = TREE_NODE_NULL };
    TreeNode child = tree_node_first_child(ov, n);
    while (!tree_node_is_null(child)) {
        last_child = child;
        child = tree_node_next_sibling(ov, child);
    }
    return last_child;
}

TreeNode tree_node_prev_sibling(TreeOverlay *ov, TreeNode n){
    TreeNode p = tree_node_parent(ov, n);
    if (tree_node_is_null(p)) {
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }

    TreeNode prev_sibling = (TreeNode){ .kind = TREE_NODE_NULL };
    TreeNode curr = tree_node_first_child(ov, p);
    while (!tree_node_is_null(curr)) {
        if (tree_node_id(curr) == tree_node_id(n)) 
            break;
        prev_sibling = curr;
        curr = tree_node_next_sibling(ov, curr);
    }

    return prev_sibling;
}
static NodeDisk *node_img(uint8_t *base, uint64_t offset){
    return (NodeDisk *)(base + offset);
}

/**
 * return: new end offset after serialization
 */
uint64_t tree_node_serialize_recursive(TreeOverlay *ov, uint8_t **base, RadixTree *prepare_radix_tree,
     uint64_t node_offset, 
    TreeNode n, uint64_t parent_offset) {

    // prepare index
    uint64_t node_id = tree_node_id(n);
    radix_tree_insert_offset(prepare_radix_tree, node_id, (void *)(uintptr_t)node_offset);

    const char *text = tree_node_text(n);
    uint64_t text_storage_len = strlen(text) + 1;
    uint64_t node_size = sizeof(struct NodeDisk) + text_storage_len;

    uint64_t child_offset = node_offset + node_size;
    *base = realloc(*base, child_offset);

    node_img(*base, node_offset)->layout_height = tree_node_layout_height(ov, n);
    node_img(*base, node_offset)->descendents = tree_node_descendents(n);

    node_img(*base, node_offset)->node_id = tree_node_id(n);
    node_img(*base, node_offset)->flags = tree_node_flags(n);
    memcpy(node_img(*base, node_offset)->text, text, text_storage_len);

    // serialize children
    TreeNode child = tree_node_first_child(ov, n);
    if(tree_node_is_null(child)){
        node_img(*base, node_offset)->first_child = OFF_NULL;
    } else {
        node_img(*base, node_offset)->first_child = child_offset;
    }
    while(!tree_node_is_null(child)){
        child_offset = tree_node_serialize_recursive(ov, base, prepare_radix_tree, child_offset, child, 
            node_offset// every child's parent is current node
        );
        child = tree_node_next_sibling(ov, child);
    }


    node_img(*base, node_offset)->parent = parent_offset;
    if(tree_node_is_null(tree_node_next_sibling(ov, n))){
        node_img(*base, node_offset)->next_sibling = OFF_NULL;
    } else {
        uint64_t sibling_offset = child_offset;
        node_img(*base, node_offset)->next_sibling = sibling_offset;
    }
    return child_offset;
}

/**
 * this procedure shouldn't call find_by_id or similar functions because they depend on id_map,
 *  and id_map is being inserted with offsets during tree serialization
 */
uint8_t *tree_overlay_serialize(TreeOverlay *ov, uint64_t *out_size, RadixTree *prepare_radix_tree) {
    uint8_t *base = (uint8_t *) calloc(1, sizeof(TreeFileHeader));
    TreeFileHeader hdr = {
        .magic = TREE_MAGIC,
        .version = TREE_VERSION,
        .root_off = sizeof(TreeFileHeader), 
        .checkpoint_lsn = ov->last_applied_lsn,
        .max_node_id = ov->max_node_id,
        .node_count = 0 // to be filled later
    };
    memcpy(base, &hdr, sizeof(TreeFileHeader));

    uint64_t end_offset = tree_node_serialize_recursive(ov, &base, prepare_radix_tree,
        hdr.root_off,
        ov->root, 
        OFF_NULL// no parent for root
    );

    *out_size = end_offset;
    return base;
}

/**
 * DFS
 * return: 0 on processed, 1 on found, -1 on error
 */
int tree_traverse(TreeOverlay *ov, TreeNode n, int (*visit)(TreeNode n, void *ctx), void *ctx) {
    if (tree_node_is_null(n)) {
        return 0;
    }

    int res = visit(n, ctx);
    if(res != 0 ){
        return res;
    }

    TreeNode child = tree_node_first_child(ov, n);
    while (!tree_node_is_null(child)) {
        res = tree_traverse(ov, child, visit, ctx);
        if(res != 0){
            return res;
        }
        child = tree_node_next_sibling(ov, child);
    }

    return 0;

}

int tree_traverse_with_depth(TreeOverlay *ov, TreeNode n, int64_t depth, 
        int (*visit)(TreeNode n, int64_t depth, void *ctx), void *ctx) {
    if (tree_node_is_null(n)) {
        return 0;
    }

    int res = visit(n, depth, ctx);
    if(res != 0 ){
        return res;
    }

    TreeNode child = tree_node_first_child(ov, n);
    while (!tree_node_is_null(child)) {
        res = tree_traverse_with_depth(ov, child, depth + 1, visit, ctx);
        if(res != 0){
            return res;
        }
        child = tree_node_next_sibling(ov, child);
    }

    return 0;
}

TreeNode tree_node_dfs_next(TreeOverlay *ov, TreeNode n){
    if(tree_node_is_null(n)){
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    TreeNode first_child = tree_node_first_child(ov, n);
    if(!tree_node_is_null(first_child)){
        return first_child;
    }
    TreeNode curr = n;
    while(!tree_node_is_null(curr)){
        TreeNode next_sibling = tree_node_next_sibling(ov, curr);
        if(!tree_node_is_null(next_sibling)){
            return next_sibling;
        }
        curr = tree_node_parent(ov, curr);
    }
    return (TreeNode){ .kind = TREE_NODE_NULL };
}

TreeNode tree_node_dfs_prev(TreeOverlay *ov, TreeNode n){
    if(tree_node_is_null(n)){
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    TreeNode parent = tree_node_parent(ov, n);
    if(tree_node_is_null(parent)){
        return (TreeNode){ .kind = TREE_NODE_NULL };
    }
    TreeNode prev_sibling = tree_node_prev_sibling(ov, n);
    if(!tree_node_is_null(prev_sibling)){
        // go to the last child of prev_sibling
        TreeNode curr = prev_sibling;
        while(true){
            TreeNode last_child = tree_node_last_child(ov, curr);
            if(tree_node_is_null(last_child)){
                return curr;
            }
            curr = last_child;
        }
    }
    return parent;
}

// indexing
RadixTree id_map = {.root = NULL};
void *pointer_to_radixtree_value(MutableNode *mn){
    return (void *)( (uint64_t)(uintptr_t)mn | (1ULL << 63) );
}
void *offset_to_radixtree_value(uint64_t off){
    return (void *)( (uint64_t)(uintptr_t)off & ~(1ULL << 63) );
}

static TreeNode radixtree_value_to_tree_node(TreeOverlay *ov, void *offset_or_pointer){
    uint64_t val = (uint64_t)(uintptr_t)offset_or_pointer;
    if(val & (1ULL << 63)){
        // MutableNode*
        return (TreeNode){
            .kind = TREE_NODE_MUTABLE,
            .mut = (MutableNode *)(uintptr_t)(val & ~(1ULL << 63))
        };
    } else {
        if(ov->tree_view == NULL || ov->tree_view->base == NULL){
            log_error("radixtree_value_to_tree_node: Cannot resolve disk node without base tree");
            return (TreeNode){ .kind = TREE_NODE_NULL };
        }
        // disk offset
        return (TreeNode){
            .kind = TREE_NODE_DISK,
            .disk = (NodeRef){
                .base = ov->tree_view->base,
                .off = val
            }
        };
    }
}

// void tree_cache_id_mutable_node(TreeOverlay *ov, MutableNode *mn){
//     if(mn == NULL){
//         return;
//     }
//     radix_tree_insert_mem_address(ov->id_map, mn->node_id, (void *)mn);
// }


TreeNode tree_find_by_id(TreeOverlay *ov, uint64_t id) {
    void *val = radix_tree_lookup(ov->id_map, id);
    if(val != NULL){
        return radixtree_value_to_tree_node(ov, val);
    }
    log_warn("tree_find_by_id: Node ID %" PRIu64 " not found in overlay", id);
    return (TreeNode){ .kind = TREE_NODE_NULL };
}

uint64_t tree_node_layout_height(TreeOverlay *ov, TreeNode n){
    if (n.kind == TREE_NODE_DISK) {
        n = overlay_promote_disk_node(ov, n.disk);
    }

    if (n.kind == TREE_NODE_MUTABLE)
        return n.mut->layout_height;
    
    if (n.kind == TREE_NODE_DISK)
        return node_layout_height(n.disk);
    
    return 0;
}

uint64_t tree_node_descendents(TreeNode n) {
    if (n.kind == TREE_NODE_MUTABLE)
        return n.mut->descendents;
    
    if (n.kind == TREE_NODE_DISK)
        return node_descendents(n.disk);
    
    return 0;
}

int tree_move_subtree(TreeOverlay *ov, TreeNode node, TreeNode new_parent, NodeID next_sibling_id) {
    TreeNode new_first_child = tree_node_first_child(ov, new_parent);
    int effective_parent_height = 0;
    if(tree_node_is_null(new_first_child)){
        effective_parent_height = 1;
    }

    // detach from old parent or previous sibling
    TreeNode old_parent = tree_node_parent(ov, node);
    TreeNode old_first_child = tree_node_first_child(ov, old_parent);
    TreeNode old_last_child = tree_node_last_child(ov, old_parent);
    int effective_old_parent_height = 0;
    if(tree_node_id(old_first_child) == tree_node_id(old_last_child)){
        // only one child
        effective_old_parent_height = 1;
    }
    if(tree_node_id(old_first_child) == tree_node_id(node)){
        // node is first child
        tree_node_set_first_child(ov, &old_parent, tree_node_next_sibling(ov, node));
        tree_node_set_next_sibling(ov, &node, (TreeNode){ .kind = TREE_NODE_NULL });
    } else {
        // find previous sibling
        TreeNode prev_sibling = tree_node_first_child(ov, old_parent);
        while(!tree_node_is_null(prev_sibling)){
            TreeNode next_sib = tree_node_next_sibling(ov, prev_sibling);
            if(tree_node_id(next_sib) == tree_node_id(node)){
                // found previous sibling
                tree_node_set_next_sibling(ov, &prev_sibling, tree_node_next_sibling(ov, node));
                break;
            }
            prev_sibling = next_sib;
        }
    }
    tree_overlay_propagate_descendents_change(ov, &old_parent, -(1 + tree_node_descendents(node)));
    if(tree_node_collapsed(node)){
        // if node is collapsed, its layout height is 1
        tree_overlay_propagate_layout_change(ov, &old_parent, -1 + effective_old_parent_height);
    }else{
        tree_overlay_propagate_layout_change(ov, &old_parent, -tree_node_layout_height(ov, node) + effective_old_parent_height);
    }

    // attach to new parent
    tree_node_set_parent(ov, &node, new_parent);
    // attach to new position
    TreeNode first_child = tree_node_first_child(ov, new_parent);
    if(tree_node_is_null(first_child)){
        // no children, set as first child
        tree_node_set_first_child(ov, &new_parent, node);
        tree_node_set_next_sibling(ov, &node, (TreeNode){ .kind = TREE_NODE_NULL });
    } else {
        // find position to insert
        TreeNode prev = { .kind = TREE_NODE_NULL };
        TreeNode curr = first_child;
        while(!tree_node_is_null(curr)){
            if(tree_node_id(curr) == next_sibling_id){
                break;
            }
            prev = curr;
            curr = tree_node_next_sibling(ov, curr);
        }
        if(tree_node_is_null(prev)){
            // insert at beginning
            tree_node_set_first_child(ov, &new_parent, node);
            tree_node_set_next_sibling(ov, &node, first_child);
            goto update_layout_descendents;
        }
        // insert after prev
        tree_node_set_next_sibling(ov, &prev, node);
        tree_node_set_next_sibling(ov, &node, curr);
    }
update_layout_descendents:
    tree_overlay_propagate_descendents_change(ov, &new_parent, 1 + tree_node_descendents(node));
    if(tree_node_collapsed(node)){
        // if node is collapsed, its layout height is 1
        tree_overlay_propagate_layout_change(ov, &new_parent, 1 - effective_parent_height);
    }else{
        tree_overlay_propagate_layout_change(ov, &new_parent, tree_node_layout_height(ov, node) - effective_parent_height);
    }
    return 0;
}

typedef struct {
    TreeOverlay *ov;
    TreeNode src_node;
    TreeNode new_parent;
    NodeID new_sibling_id;
} TreeCopySubtreeContext;
TreeNode tree_copy_recursive(void *ctx){
    TreeCopySubtreeContext *data = (TreeCopySubtreeContext *)ctx;
    TreeOverlay *ov = data->ov;
    TreeNode src_node = data->src_node;
    TreeNode new_parent = data->new_parent;
    NodeID new_sibling_id = data->new_sibling_id;

    MutableNode *new_mut = mut_node_new( ++ov->max_node_id,
        new_parent, // parent
        (TreeNode){ .kind = TREE_NODE_NULL }, // no first child
        (TreeNode){ .kind = TREE_NODE_NULL }, // next sibling to be set later
        tree_node_descendents(src_node),// just copy descendents count, we will not propagate while copying
        tree_node_flags(src_node),// flags
        tree_node_text(src_node)// text
    );
    new_mut->layout_height = tree_node_layout_height(ov, src_node);
    TreeNode new_node = (TreeNode){ .kind = TREE_NODE_MUTABLE, .mut = new_mut };
    // tree_cache_id_mutable_node(ov, new_node.mut);
    radix_tree_insert_mem_address(ov->id_map, new_mut->node_id, (void *)new_mut);

    // insert; attach to new position
    TreeNode first_child = tree_node_first_child(ov, new_parent);
    if(tree_node_is_null(first_child) || tree_node_id(first_child) == new_sibling_id){
        tree_node_set_first_child(ov, &new_parent, new_node);
        tree_node_set_next_sibling(ov, &new_node, first_child);
    } else {
        // find position to insert
        TreeNode prev = { .kind = TREE_NODE_NULL };
        TreeNode curr = first_child;
        while(!tree_node_is_null(curr)){
            if(tree_node_id(curr) == new_sibling_id){
                break;
            }
            prev = curr;
            curr = tree_node_next_sibling(ov, curr);
        }
        if(tree_node_is_null(prev)){
            log_error("tree_copy_recursive: Unexpected NULL previous sibling while copying subtree");
            return (TreeNode){ .kind = TREE_NODE_NULL };
        }
        // insert after prev
        tree_node_set_next_sibling(ov, &prev, new_node);
        tree_node_set_next_sibling(ov, &new_node, curr);
    }


    // find dfs next
    TreeNode child = tree_node_first_child(ov, src_node);
    NodeID new_sibling_id_for_children = 0;
    while(!tree_node_is_null(child)){
        // copy child
        TreeCopySubtreeContext child_ctx = {
            .ov = ov,
            .src_node = child,// source node
            .new_parent = new_node,
            .new_sibling_id = new_sibling_id_for_children
        };
        TreeNode r = tree_copy_recursive(&child_ctx);
        if(r.kind == TREE_NODE_NULL){
            log_error("tree_copy_recursive: Failed to copy child node ID %" PRIu64, tree_node_id(child));
            return (TreeNode){ .kind = TREE_NODE_NULL };
        }
        child = tree_node_next_sibling(ov, child);
    }

    return new_node;
}

/**
 * *out_new_node_id: ID of the new copied subtree root
 */
int tree_copy_subtree(TreeOverlay *ov, TreeNode node, TreeNode new_parent, NodeID next_sibling_id, NodeID *out_new_node_id) {
    // Deep copy the subtree rooted at 'node'
    if(node.kind == TREE_NODE_DISK){
        overlay_materialize(ov, &node);
    }
    if(node.kind != TREE_NODE_MUTABLE){
        log_error("tree_copy_subtree: Node is not mutable after materialization");
        return -1;
    }
    bool new_parent_has_children = !tree_node_is_null(tree_node_first_child(ov, new_parent));

    TreeNode new_node = tree_copy_recursive(&(TreeCopySubtreeContext){
        .ov = ov,
        .src_node = node,
        .new_parent = new_parent,
        .new_sibling_id = next_sibling_id
    });

    *out_new_node_id = tree_node_id(new_node);

    // update descendents and layout height
    tree_overlay_propagate_descendents_change(ov, &new_parent, 1 + tree_node_descendents(new_node));
    uint64_t new_node_layout_height = tree_node_layout_height(ov, new_node);
    if(tree_node_collapsed(new_node)){
        if(new_parent_has_children){
            tree_overlay_propagate_layout_change(ov, &new_parent, 1);
        }
    } else {
        if(new_parent_has_children){
            tree_overlay_propagate_layout_change(ov, &new_parent, new_node_layout_height);
        }else{
            if(new_node_layout_height > 1)
                tree_overlay_propagate_layout_change(ov, &new_parent, new_node_layout_height - 1);
        }
    }

    return 0;
}

int tree_overlay_apply_event(TreeOverlay *overlay, Event *event) {
    switch(event->type){
        case EVENT_ADD_FIRST_CHILD: {
            // find parent node
            TreeNode parent = tree_find_by_id(overlay, event->parent_id);
            if(tree_node_is_null(parent)){
                log_error("tree_overlay_apply_event: Parent node ID %" PRIu64 " not found for ADD_FIRST_CHILD", event->parent_id);
                return -1;
            }
            TreeNode new_node = tree_add_first_child(overlay, &parent, event->text);
            if(tree_node_is_null(new_node)){
                log_error("tree_overlay_apply_event: Failed to add first child to parent ID %" PRIu64, event->parent_id);
                return -1;
            }
            event->new_node_id = tree_node_id(new_node);
            break;
        }
        case EVENT_DELETE_SINGLE_NODE:{
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for DELETE_SINGLE_NODE", event->node_id);
                return -1;
            }
            int r = tree_delete_single_node(overlay, &node);
            if(r != 0){
                log_error("tree_overlay_apply_event: Failed to delete single node ID %" PRIu64, event->node_id);
                return -1;
            }
            break;
        }
        case EVENT_ADD_LAST_CHILD: {
            TreeNode parent = tree_find_by_id(overlay, event->parent_id);
            if(tree_node_is_null(parent)){
                log_error("tree_overlay_apply_event: Parent node ID %" PRIu64 " not found for ADD_LAST_CHILD", event->parent_id);
                return -1;  
            }
            TreeNode last_child = tree_node_last_child(overlay, parent);
            if(tree_node_is_null(last_child)){
                // no children, add first child
                TreeNode new_node = tree_add_first_child(overlay, &parent, event->text);
                if(tree_node_is_null(new_node)){
                    log_error("tree_overlay_apply_event: Failed to add last child (as first child) to parent ID %" PRIu64, event->parent_id);
                    return -1;
                }
                event->new_node_id = tree_node_id(new_node);
            } else {
                // add sibling to last child
                TreeNode new_node = tree_add_sibling(overlay, &last_child, event->text);
                if(tree_node_is_null(new_node)){
                    log_error("tree_overlay_apply_event: Failed to add last child (as sibling) to parent ID %" PRIu64, event->parent_id);
                    return -1;
                }
                event->new_node_id = tree_node_id(new_node);
            }
            break;
        }
        case EVENT_ADD_SINGLE_NODE:{
            TreeNode parent = tree_find_by_id(overlay, event->parent_id);
            char *text = event->text;
            NodeID node_id = event->node_id;
            if(node_id == 0){
                node_id = overlay->max_node_id + 1;
                overlay->max_node_id = node_id;
            }
            TreeNode new_node = tree_add_single_node(overlay, &parent,  event->next_sibling_id, event->flags, text, node_id);
            if(tree_node_is_null(new_node)){
                log_error("tree_overlay_apply_event: Failed to add single node to parent ID %" PRIu64, event->parent_id);
                return -1;
            }
            event->new_node_id = tree_node_id(new_node);
            break;
        }
        case EVENT_ADD_SIBLING: {
            // find reference sibling node
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Reference sibling node ID %" PRIu64 " not found for ADD_SIBLING", event->node_id);
                return -1;
            }
            TreeNode new_node = tree_add_sibling(overlay, &node, event->text);
            if(tree_node_is_null(new_node)){
                log_error("tree_overlay_apply_event: Failed to add sibling to reference sibling ID %" PRIu64, event->node_id);
                return -1;
            }
            event->new_node_id = tree_node_id(new_node);
            break;
        }
        case EVENT_MOVE_SUBTREE:{
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for MOVE_SUBTREE", event->node_id);
                return -1;
            }
            TreeNode new_parent = tree_find_by_id(overlay, event->parent_id);
            if(tree_node_is_null(new_parent)){
                log_error("tree_overlay_apply_event: New parent ID %" PRIu64 " not found for MOVE_SUBTREE", event->parent_id);
                return -1;
            }
            // remove from old parent
            int r = tree_move_subtree(overlay, node, new_parent, event->next_sibling_id);
            break;
        }
        case EVENT_COPY_SUBTREE:{
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for COPY_SUBTREE", event->node_id);
                return -1;
            }
            TreeNode new_parent = tree_find_by_id(overlay, event->parent_id);
            if(tree_node_is_null(new_parent)){
                log_error("tree_overlay_apply_event: New parent ID %" PRIu64 " not found for COPY_SUBTREE", event->parent_id);
                return -1;
            }
            int r = tree_copy_subtree(overlay, node, new_parent, event->next_sibling_id, &event->new_node_id);
            break;
        }
        case EVENT_UPDATE_TEXT: {
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for UPDATE_TEXT", event->node_id);
                return -1;
            }
            event->old_text = strdup(tree_node_text(node)); // store old text for undo
            tree_node_set_text(overlay, &node, event->text);
            event->node_id = tree_node_id(node);
            break;
        }
        case EVENT_COLLAPSE_NODE: {
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for COLLAPSE_NODE", event->node_id);
                return -1;
            }
            tree_node_set_collapse(overlay, &node, true);
            event->collapsed = true;     
            break;
        }
        case EVENT_EXPAND_NODE: {
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for EXPAND_NODE", event->node_id);
                return -1;
            }
            tree_node_set_collapse(overlay, &node, false);
            event->collapsed = false;     
            break;
        }
        case EVENT_SET_FLAG_HIDDEN: {
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for SET_FLAG_HIDDEN", event->node_id);
                return -1;
            }
            tree_node_set_hidden(overlay, &node, event->flag);
            break;
        }
        case EVENT_SET_FLAG_SHOW_HIDDEN_CHILDREN:{
            TreeNode node = tree_find_by_id(overlay, event->node_id);
            if(tree_node_is_null(node)){
                log_error("tree_overlay_apply_event: Node ID %" PRIu64 " not found for SET_FLAG_SHOW_HIDDEN_CHILDREN", event->node_id);
                return -1;
            }
            tree_node_set_show_hidden_children(overlay, &node, event->flags & TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN);
            break;
        }
        default:
            log_warn("tree_overlay_apply_event: Unsupported event type %s", event_type_to_string(event->type));
            return -1;
    }
    // on success
    assert(overlay->last_applied_lsn < event->lsn);
    overlay->last_applied_lsn = event->lsn;
    return 0;
}

int tree_overlay_on_replay(Event *event, void *ctx) {
    TreeOverlay *ov = (TreeOverlay *)ctx;
    return tree_overlay_apply_event(ov, event);
}
int tree_overlay_save(TreeOverlay *ov, const char *path){
    // prepare serialized data
    RadixTree *prepare_radix_tree = radix_tree_create(NULL);
    uint64_t bytes = 0;

    uint8_t *serialized_mem_base = tree_overlay_serialize(ov, &bytes, prepare_radix_tree);
    TreeFileHeader *umt_hdr = (TreeFileHeader *)serialized_mem_base;
    umt_hdr->id_map_off = bytes;

    // write to tmp file mktemp
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    int fd = open(tmp_path,  O_RDWR | O_CREAT | O_TRUNC, 0644);
    ssize_t written = write(fd, serialized_mem_base, bytes);
    assert(written == (ssize_t)bytes);

    size_t id_map_size;
    uint8_t *serialized_id_map = radix_tree_serialize(prepare_radix_tree, &id_map_size);
    ssize_t id_map_written = write(fd, serialized_id_map, id_map_size);
    assert(id_map_written == (ssize_t)id_map_size);

    close(fd);

    rename(tmp_path, path);

    radix_tree_destroy(prepare_radix_tree);
    free(serialized_id_map);
    free(serialized_mem_base);
    return 0;
}