#ifndef TREE_OVERLAY_H
#define TREE_OVERLAY_H

#include "tree_view.h"
#include "../utils/hashtable_u64.h"
#include "../utils/radix_tree.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * tree_overlay.h
 * 
 * design philosophy:
 * - TreeNode: logical truth, unified view of a node (disk or mutable)
 * - NodeDisk: history truth, immutable snapshot from disk
 * - MutableNode: "current will" (modified nodes)
 * 
 * rules: mutable → disk fallback
 */

typedef struct MutableNode MutableNode;
typedef struct TreeOverlay TreeOverlay;
typedef struct Event Event;  // Forward declaration for tree_overlay_on_replay

// unified tree node view (the only view seen by the logical layer)
typedef enum {
    TREE_NODE_NULL = 0,      // None
    TREE_NODE_DISK,      // From mmap (NodeRef)
    TREE_NODE_MUTABLE,   // From mutable overlay
} TreeNodeKind;

typedef struct {
    TreeNodeKind kind;
    union {
        NodeRef disk;                // if kind == TREE_NODE_DISK
        MutableNode *mut;            // if kind == TREE_NODE_MUTABLE
    };
} TreeNode;

typedef uint64_t TreeNodeFlags;
enum {
    TREE_FLAG_NONE      =0ULL,
    TREE_FLAG_COLLAPSED =(1ULL << 0),
    TREE_FLAG_HIDDEN    =(1ULL << 1),

    TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN = (1ULL << 2),

    // TLV flags
    TREE_FLAG_HAS_RESOURCE = (1ULL << 16),

    // Overlay-only marker to indicate in-memory mutation; not persisted as-is.
    TREE_FLAG_MODIFIED  =(1ULL << 63),
};

typedef uint16_t NodeType;
enum {
    NODE_TYPE_DEFAULT = 0,
    NODE_TYPE_RESOURCE  = 1,
};

// modified node in the overlay, created on demand (COW)
struct MutableNode {
    uint64_t node_id;  // unique identifier, assigned from TreeOverlay.max_node_id

    TreeNode parent;
    TreeNode first_child;
    TreeNode next_sibling;

    uint64_t layout_height;
    uint64_t descendents;

    NodeType type;   // uint16_t
    TreeNodeFlags flags; // uint64_t
    char *text;

    NodeResource *resource;
};

// session object, represents an active overlay tree
struct TreeOverlay {
    char *path;
    TreeView *tree_view;      // readonly mmap view
    TreeNode root;       // current logical root (may be replaced)
    RadixTree *id_map; // node ID -> NodeRef.off/MutableNode* mapping
    char *id_map_path; 
    uint64_t last_applied_lsn; // last applied LSN
    uint64_t max_node_id; // used for allocating new node IDs, next ID = max_node_id + 1
    // U64Hashtable *node_id_map;    
};


// ===== Lifecycle API =====
TreeOverlay* tree_overlay_open(TreeView *base, const char *path);
TreeOverlay* tree_overlay_create_empty(const char *path);

// ===== TreeNode helpers =====
static inline bool tree_node_is_null(TreeNode n) {
    return n.kind == TREE_NODE_NULL;
}

static inline bool tree_node_is_disk(TreeNode n) {
    return n.kind == TREE_NODE_DISK;
}

static inline bool tree_node_is_mut(TreeNode n) {
    return n.kind == TREE_NODE_MUTABLE;
}

static inline bool tree_node_is_mutable(TreeNode n) {
    return n.kind == TREE_NODE_MUTABLE;
}

// Traversal core
TreeNode tree_node_parent(TreeOverlay *ov, TreeNode n);
TreeNode tree_node_first_child(TreeOverlay *ov, TreeNode n);
TreeNode tree_node_first_child_with_filter(TreeOverlay *ov, TreeNode n, bool (*filter)(TreeNode n, void *ctx), void *ctx);
TreeNode tree_node_next_sibling(TreeOverlay *ov, TreeNode n);
TreeNode tree_node_next_sibling_with_filter(TreeOverlay *ov, TreeNode n, bool (*filter)(TreeNode n, void *ctx), void *ctx);

bool tree_node_is_first_child(TreeOverlay *ov, TreeNode n);

// derived helper: get previous sibling
TreeNode tree_node_last_child(TreeOverlay *ov, TreeNode n);
TreeNode tree_node_last_child_with_filter(TreeOverlay *ov, TreeNode n, bool (*filter)(TreeNode n, void *ctx), void *ctx);
TreeNode tree_node_prev_sibling(TreeOverlay *ov, TreeNode n);
TreeNode tree_node_prev_sibling_with_filter(TreeOverlay *ov, TreeNode n, bool (*filter)(TreeNode n, void *ctx), void *ctx);

// traverser
int tree_traverse(TreeOverlay *ov, TreeNode n, int (*visit)(TreeNode n, void *ctx), void *ctx) ;
int tree_traverse_with_depth(TreeOverlay *ov, TreeNode n, int64_t depth, 
        int (*visit)(TreeNode n, int64_t depth, void *ctx), void *ctx, bool exclude_hidden) ;
TreeNode tree_node_dfs_next(TreeOverlay *ov, TreeNode n);
TreeNode tree_node_dfs_prev(TreeOverlay *ov, TreeNode n);

// ===== Field accessors =====
uint64_t tree_node_id(TreeNode n);
const char* tree_node_text(TreeNode n);
uint64_t tree_node_flags(TreeNode n);
bool tree_node_hidden(TreeNode n);
bool tree_node_collapsed(TreeNode n);
bool tree_node_is_collapsed(TreeNode n);
bool tree_node_show_hidden_children(TreeNode n);
uint64_t tree_node_layout_height(TreeOverlay *ov, TreeNode n);
uint64_t tree_node_descendents(TreeNode n);

// Field modiifers
void tree_node_set_first_child(TreeOverlay *ov, TreeNode *n, TreeNode first_child);
void tree_node_set_next_sibling(TreeOverlay *ov, TreeNode *n, TreeNode next_sibling);
void tree_node_set_collapse(TreeOverlay *ov, TreeNode *n, bool collapsed);
void tree_node_set_hidden(TreeOverlay *ov, TreeNode *n, bool hidden);
void tree_node_set_layout_height(TreeOverlay *ov, TreeNode *n, uint64_t height);

// relationships
bool tree_node_has_parent(TreeOverlay *ov, TreeNode n);

// ===== Mutation API (COW + edit) =====
void overlay_materialize(TreeOverlay *ov, TreeNode *node);
TreeNode tree_add_first_child(TreeOverlay *ov, TreeNode *parent, const char *text);
TreeNode tree_add_sibling(TreeOverlay *overlay, TreeNode *node, const char *text);
int tree_update_text(TreeOverlay *ov, TreeNode node, const char *text);
int tree_remove_child(TreeOverlay *ov, TreeNode parent, TreeNode child);

// derived mutation


// ===== Event Replay & Application =====

int tree_overlay_apply_event(TreeOverlay *ov, Event *event);

int tree_overlay_on_replay(Event *event, void *ctx);

int tree_move_as_first_child(TreeOverlay *ov, TreeNode node, TreeNode new_parent);
int tree_move_as_next_sibling(TreeOverlay *ov, TreeNode node, TreeNode ref_sibling);

void cache_id_node(TreeOverlay *ov, TreeNode n);

// operation center
/**
 * return: 0 on success, -1 on failure
 */
int tree_overlay_apply_event(TreeOverlay *overlay, Event *event);
TreeNode tree_find_by_id(TreeOverlay *ov, uint64_t id) ;

// ===== Persistence =====
int tree_overlay_save(TreeOverlay *ov, const char *path);
uint8_t *tree_overlay_serialize(TreeOverlay *ov, uint64_t *out_size, RadixTree *prepare_radix_tree);

#endif // TREE_OVERLAY_H

