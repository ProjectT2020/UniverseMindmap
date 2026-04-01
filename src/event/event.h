#ifndef EVENT_H
#define EVENT_H

#include "event_types.h"
#include <stddef.h> // size_t

Event* event_create_add_first_child(uint64_t parent_id, const char *text) ;
Event* event_create_add_last_child(uint64_t parent_id, const char *text) ;
Event* event_create_add_sibling(uint64_t node_id, const char *text) ;

Event* event_create_move_subtree(
    uint64_t node_id,
    uint64_t old_parent, uint64_t old_next_sibling_id, 
    uint64_t new_parent, uint64_t new_next_sibling_id);

Event *event_create_copy_subtree(uint64_t node_id,
    uint64_t new_parent, uint64_t new_next_sibling_id);// only need new position for undo (delete)

Event *event_create_move_to_children_tail(uint64_t node_id, uint64_t new_parent_id,
    uint64_t old_parent_id,
    uint64_t old_next_next_sibling_id);

Event* event_create_delete_node(uint64_t node_id, uint64_t parent_id, uint64_t next_sibling_id, const char *text);
Event* event_create_delete_subtree(uint64_t node_id);
Event* event_create_update_text(uint64_t node_id, const char *new_text);
Event* event_create_move_current(EventType type);
Event* event_create_collapse_node(uint64_t node_id);
Event* event_create_expand_node(uint64_t node_id);
Event* event_create_scroll_view(int delta_y, int delta_x);
Event* event_create(EventType type);


// field modify
Event *event_create_set_hidden(uint64_t node_id, bool hidden);
Event *event_create_set_show_hidden_children(uint64_t node_id, bool hidden);

// transaction control
Event* event_create_begin_transaction(void);
Event* event_create_commit_transaction(void);

void event_destroy(Event* e);

Event* event_invert(uint64_t lsn, const Event* e);

// for WAL
uint8_t *event_serialize(Event* e, size_t *out_size);
Event* event_deserialize(uint8_t *buf, size_t buf_size);

int event_validate(Event *e);

const char* event_type_to_string(EventType type);

#endif // EVENT_H
