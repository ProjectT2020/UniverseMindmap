#include "event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "../utils/logging.h"

Event* event_create_add_first_child(uint64_t parent_id, const char *text) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_ADD_FIRST_CHILD;
    e->parent_id = parent_id;
    if (text) e->text = strdup(text);
    return e;
}

Event* event_create_add_last_child(uint64_t parent_id, const char *text) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_ADD_LAST_CHILD;
    e->parent_id = parent_id;
    if (text) e->text = strdup(text);
    return e;
}

Event* event_create_add_sibling(uint64_t node_id, const char *text) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_ADD_SIBLING;
    e->node_id = node_id;
    if (text) e->text = strdup(text);
    return e;
}

// move node to new parent as its last child
Event *event_create_move_to_children_tail(uint64_t node_id, uint64_t new_parent_id,
    uint64_t old_parent_id,
     uint64_t old_next_next_sibling_id){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_MOVE_SUBTREE;
    e->node_id = node_id;
    e->parent_id = new_parent_id;
    e->next_sibling_id = 0; // indicate move to tail
    e->old_parent = old_parent_id;
    e->old_next_sibling_id = old_next_next_sibling_id;
    return e;
}

Event *event_create_copy_subtree(uint64_t node_id,
    uint64_t new_parent, uint64_t new_next_sibling_id){// only need new position for undo (delete)
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_COPY_SUBTREE;
    e->node_id = node_id;
    e->parent_id = new_parent;
    e->next_sibling_id = new_next_sibling_id;
    return e;
}

Event* event_create_delete_node(uint64_t node_id, uint64_t parent_id, uint64_t next_sibling_id, const char *text) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_DELETE_SINGLE_NODE;
    e->node_id = node_id;
    e->parent_id = parent_id;
    e->next_sibling_id = next_sibling_id;
    if (text) e->text = strdup(text);
    return e;
}

Event *event_create_set_hidden(uint64_t node_id, bool hidden){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_SET_FLAG_HIDDEN; 
    e->node_id = node_id;
    e->flag = hidden; 
    return e;
}

Event *event_create_set_show_hidden_children(uint64_t node_id, bool hidden){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_SET_FLAG_SHOW_HIDDEN_CHILDREN; 
    e->node_id = node_id;
    if(hidden){
        e->flags |= TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN;
    }else{
        e->flags &= ~TREE_NODE_FLAG_SHOW_HIDDEN_CHILDREN;
    }
    return e;
}

Event* event_create_delete_subtree(NodeID node_id) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_DELETE_SUBTREE;
    e->node_id = node_id;
    return e;
}

Event* event_create_update_text(NodeID node_id, const char *new_text) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_UPDATE_TEXT;
    e->node_id = node_id;
    e->text = strdup(new_text);
    return e;
}

Event* event_create_collapse_node(NodeID node_id) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_COLLAPSE_NODE;
    e->node_id = node_id;
    e->collapsed = 1;
    return e;
}

Event* event_create_expand_node(NodeID node_id) {
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_EXPAND_NODE;
    e->node_id = node_id;
    e->collapsed = 0;
    return e;
}

Event *event_create_begin_transaction(void){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_BEGIN_TRANSACTION;
    return e;
}

Event *event_create_commit_transaction(void){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_COMMIT_TRANSACTION;
    return e;
}

void event_destroy(Event* e) {
    if (!e) return;
    if (e->text) {
        free(e->text);
        e->text = NULL;
    }
    if(e->old_text){
        free(e->old_text);
        e->old_text = NULL;
    }
    if (e->old_children) {
        list_destroy(&e->old_children);
    }
    free(e);
}

/**
 * for undo
 */
Event* event_invert(uint64_t lsn, const Event* e) {
    if (!e) return NULL;
    Event *inv = NULL;
    switch(e->type) {
        case EVENT_ADD_SINGLE_NODE:
            // For nodes without moved children, use regular delete
            // (Paste operations don't set old_children anymore)
            inv = event_create_delete_node(e->node_id, e->parent_id, e->next_sibling_id, e->text);
            inv->old_children = e->old_children;
            break;
        case EVENT_DELETE_SUBTREE:
            // No meaningful inverse without full snapshot; treat as no-op placeholder
            inv = event_create(EVENT_NONE);
            break;
        case EVENT_MOVE_SUBTREE:
            inv = event_create_move_subtree(e->node_id,
                e->parent_id, e->next_sibling_id,// old
                e->old_parent, e->old_next_sibling_id// new
            );
            break;
        case EVENT_UPDATE_TEXT:
            // old_text as text 
            inv = event_create_update_text(e->node_id, e->old_text ? e->old_text : e->text);
            break;
        case EVENT_COLLAPSE_NODE:
            inv = event_create_expand_node(e->node_id);
            break;
        case EVENT_EXPAND_NODE:
            inv = event_create_collapse_node(e->node_id);
            break;
        // not implemented events
        case EVENT_DELETE_SINGLE_NODE:
            log_error("event_invert: Not implemented inversion for DELETE_SINGLE_NODE");
        default:
            assert(0 && "Unsupported event type for inversion");
            break;
    }
    inv->lsn = lsn;
    return inv;
}

uint8_t *event_serialize(Event* e, size_t *out_size) {
    if (!e) 
        return NULL;

    int buf_size = sizeof(Event) + (e->text ? strlen(e->text) + 1 : 0);
    uint8_t *buf = (uint8_t *)malloc(buf_size);

    char *original_text = e->text;
    e->text = NULL; // don't include text content pointer in serialization
    memcpy(buf, e, sizeof(Event));
    // copy text
    if(original_text){
        memcpy(buf + sizeof(Event), original_text, strlen(original_text) + 1);
    }

    e->text = original_text; // restore text pointer
    if (out_size) *out_size = buf_size;
    return buf;
}

Event* event_deserialize(uint8_t *buf, size_t buf_size) {
    if (!buf || buf_size < sizeof(Event)) return NULL;
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    memcpy(e, buf, sizeof(Event));
    // copy text
    size_t text_len = buf_size - sizeof(Event);
    if (text_len > 0) {
        e->text = (char*)calloc(1, text_len);
        if (e->text) {
            memcpy(e->text, buf + sizeof(Event), text_len);
        }
    }
    return e;
}

Event* event_create(EventType type){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = type;
    return e;
}

Event *event_create_move_subtree(
    uint64_t node_id,
    uint64_t old_parent_id, uint64_t old_next_sibling_id,
    uint64_t new_parent_id, uint64_t new_next_sibling_id
){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = EVENT_MOVE_SUBTREE;
    e->node_id = node_id;

    e->old_parent = old_parent_id;
    e->old_next_sibling_id = old_next_sibling_id;

    e->parent_id = new_parent_id;
    e->next_sibling_id = new_next_sibling_id;

    return e;
}

Event* event_create_move_current(EventType type){
    Event *e = (Event*)calloc(1, sizeof(Event));
    if (!e) return NULL;
    e->type = type;
    return e;
}

int event_validate(Event *e){
    if(!e){
        log_error("event_validate: Event is NULL");
        return -1;
    }
    switch(e->type){
        case EVENT_ADD_SINGLE_NODE:
            if(e->parent_id == 0){
                log_error("event_validate: EVENT_ADD_SINGLE_NODE with invalid parent_id=0");
                return -1;
            }
            if(!e->text){
                log_error("event_validate: EVENT_ADD_SINGLE_NODE with NULL text");
                return -1;
            }
            break;
        case EVENT_DELETE_SINGLE_NODE:
            if(e->parent_id == 0){
                log_error("event_validate: EVENT_DELETE_SINGLE_NODE with invalid parent_id=0");
                return -1;
            }
            break;
        case EVENT_UPDATE_TEXT:
            if(!e->text){
                log_error("event_validate: EVENT_UPDATE_TEXT with NULL text");
                return -1;
            }
            break;
        default:
            // other event types have no specific validation
            break;
    }
    return 0;
}


const char* event_type_to_string(EventType type) {
    switch (type) {
        case EVENT_NONE: return "EVENT_NONE";
        case EVENT_ADD_SINGLE_NODE: return "EVENT_ADD_SINGLE_NODE";
        case EVENT_ADD_FIRST_CHILD: return "EVENT_ADD_FIRST_CHILD";
        case EVENT_ADD_LAST_CHILD: return "EVENT_ADD_LAST_CHILD";
        case EVENT_ADD_SIBLING: return "EVENT_ADD_SIBLING";
        case EVENT_DELETE_SINGLE_NODE: return "EVENT_DELETE_SINGLE_NODE";
        case EVENT_UPDATE_TEXT: return "EVENT_UPDATE_TEXT";
        case EVENT_MOVE_SUBTREE: return "EVENT_MOVE_SUBTREE";
        case EVENT_COLLAPSE_NODE: return "EVENT_COLLAPSE_NODE";
        case EVENT_EXPAND_NODE: return "EVENT_EXPAND_NODE";
        case EVENT_JOIN_SIBLING_AS_CHILD: return "EVENT_JOIN_SIBLING_AS_CHILD";
        case EVENT_DELETE_SUBTREE: return "EVENT_DELETE_SUBTREE";
        case EVENT_COPY_SUBTREE: return "EVENT_COPY_SUBTREE";
        default: return "UNKNOWN_EVENT_TYPE";
    }
}