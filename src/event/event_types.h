
#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "../tree/tree_overlay.h"
#include "../utils/list.h" // List

#define MAX_NODE_TEXT 1024

typedef uint64_t NodeID;   // unique Node ID
typedef uint64_t ParentID; // parent node ID (type safety)

// event types
typedef enum {
    EVENT_NONE = 0,
    EVENT_ADD_SINGLE_NODE,
    EVENT_ADD_FIRST_CHILD,
    EVENT_ADD_LAST_CHILD,
    EVENT_ADD_SIBLING,
    EVENT_DELETE_SINGLE_NODE,
    EVENT_UPDATE_TEXT,
    EVENT_MOVE_SUBTREE,
    EVENT_COPY_SUBTREE,
    EVENT_COLLAPSE_NODE,
    EVENT_EXPAND_NODE,
    EVENT_JOIN_SIBLING_AS_CHILD,
    EVENT_DELETE_SUBTREE,

    // field modify
    EVENT_SET_FLAG_HIDDEN, // set/unset hidden flag
    EVENT_SET_FLAG_SHOW_HIDDEN_CHILDREN, // set/unset show_hidden_children flag
    
    // transaction control
    EVENT_BEGIN_TRANSACTION,
    EVENT_COMMIT_TRANSACTION
} EventType;

// event struct
typedef struct Event {
    uint64_t lsn;
    EventType type;
    // node
    uint64_t node_id;
    bool collapsed;           // collapsed state
    bool flag;
    TreeNodeFlags flags;
    char *text;
    List *old_children;    // for DELETE_SINGLE_NODE undo
    char *old_text;           // for UPDATE_TEXT undo

    // new parent, position, node
    uint64_t parent_id;    // for Add/Move
    uint64_t next_sibling_id; // for insertion positioning

    uint64_t new_node_id; // output field, for copy subtree, add node, add sibling etc. to return new node ID

    uint64_t old_parent;   // MoveNode old parent
    uint64_t old_next_sibling_id;     // MoveNode old position

    // app state
    char keyboard_hit;    // for navigation events, record key press

    // scroll
    int delta_x, delta_y; // for ScrollView event
} Event;

#endif // EVENT_TYPES_H
