#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/wal/wal.h"
#include "../src/operate/operate.h"
#include "../src/tree/tree_overlay.h"
#include "../src/app/app.h"
#include "../src/ui/ui.h"
#include "../src/utils/stack.h"

typedef struct {
    uint64_t blocked_id;
} FilterCtx;

static bool filter_all(TreeNode node, void *ctx) {
    (void)node;
    (void)ctx;
    return true;
}

static bool filter_exclude_id(TreeNode node, void *ctx) {
    FilterCtx *f = (FilterCtx *)ctx;
    return tree_node_id(node) != f->blocked_id;
}

static void test_search_hits_first_child(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_first_child.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode first = tree_add_first_child(ov, &root, "first-hit");
    TreeNode second = tree_add_sibling(ov, &first, "second");
    TreeNode nested = tree_add_first_child(ov, &second, "nested-hit");
    assert(!tree_node_is_null(first));
    assert(!tree_node_is_null(second));
    assert(!tree_node_is_null(nested));

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode r1 = operate_search_next_in_subtree(&operate, root, "first-hit", filter_all, NULL);
    assert(!tree_node_is_null(r1));
    assert(tree_node_id(r1) == tree_node_id(first));

    TreeNode r2 = operate_search_next_in_subtree(&operate, root, "nested-hit", filter_all, NULL);
    assert(!tree_node_is_null(r2));
    assert(tree_node_id(r2) == tree_node_id(nested));
}

static void test_search_in_empty_subtree_returns_null(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_empty.umt");
    assert(ov != NULL);

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode root = ov->root;
    TreeNode r = operate_search_next_in_subtree(&operate, root, "anything", filter_all, NULL);
    assert(tree_node_is_null(r));
}

static void test_search_respects_filter(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_filter.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode first = tree_add_first_child(ov, &root, "target");
    TreeNode second = tree_add_sibling(ov, &first, "target");
    assert(!tree_node_is_null(first));
    assert(!tree_node_is_null(second));

    Operate operate = {0};
    operate.overlay = ov;

    FilterCtx ctx = { .blocked_id = tree_node_id(first) };
    TreeNode r = operate_search_next_in_subtree(&operate, root, "target", filter_exclude_id, &ctx);
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(second));
}

static void test_search_next_exact_matches_full_text_only(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_next_exact.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode start = tree_add_first_child(ov, &root, "alpha");
    TreeNode partial = tree_add_sibling(ov, &start, "alpha beta");
    TreeNode exact = tree_add_sibling(ov, &partial, "alpha");

    assert(!tree_node_is_null(start));
    assert(!tree_node_is_null(partial));
    assert(!tree_node_is_null(exact));

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode r = operate_search_next_exact(&operate, start, "alpha");
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(exact));
}

static void test_search_prev_exact_matches_full_text_only(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_prev_exact.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode first_exact = tree_add_first_child(ov, &root, "topic");
    TreeNode middle = tree_add_sibling(ov, &first_exact, "topic details");
    TreeNode second_exact = tree_add_sibling(ov, &middle, "topic");

    assert(!tree_node_is_null(first_exact));
    assert(!tree_node_is_null(middle));
    assert(!tree_node_is_null(second_exact));

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode r = operate_search_prev_exact(&operate, second_exact, "topic");
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(first_exact));
}

static void test_search_next_exact_skips_dot_meta_subtree(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_next_exact_skip_meta.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode start = tree_add_first_child(ov, &root, "topic");
    TreeNode meta = tree_add_sibling(ov, &start, ".meta");
    TreeNode hidden_match = tree_add_first_child(ov, &meta, "topic");
    TreeNode visible_match = tree_add_sibling(ov, &meta, "topic");

    assert(!tree_node_is_null(start));
    assert(!tree_node_is_null(meta));
    assert(!tree_node_is_null(hidden_match));
    assert(!tree_node_is_null(visible_match));

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode r = operate_search_next_exact(&operate, start, "topic");
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(visible_match));
    assert(tree_node_id(r) != tree_node_id(hidden_match));
}

static void test_search_prev_exact_skips_dot_meta_subtree(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_search_prev_exact_skip_meta.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode visible_match = tree_add_first_child(ov, &root, "topic");
    TreeNode meta = tree_add_sibling(ov, &visible_match, ".meta");
    TreeNode hidden_match = tree_add_first_child(ov, &meta, "topic");
    TreeNode tail = tree_add_sibling(ov, &meta, "tail");

    assert(!tree_node_is_null(visible_match));
    assert(!tree_node_is_null(meta));
    assert(!tree_node_is_null(hidden_match));
    assert(!tree_node_is_null(tail));

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode r = operate_search_prev_exact(&operate, tail, "topic");
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(visible_match));
    assert(tree_node_id(r) != tree_node_id(hidden_match));
}

static void test_bfs_search_prefers_shallower_match(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_bfs_level.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode left = tree_add_first_child(ov, &root, "left");
    TreeNode right = tree_add_sibling(ov, &left, "[target]");
    TreeNode deep = tree_add_first_child(ov, &left, "[target]");
    assert(!tree_node_is_null(left));
    assert(!tree_node_is_null(right));
    assert(!tree_node_is_null(deep));

    Operate operate = {0};
    operate.overlay = ov;

    TreeNode r = operate_bfs_search(&operate, root, "[target]", filter_all, NULL);
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(right));
}

static void test_bfs_search_filter_blocks_branch(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_operate_bfs_filter.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode blocked = tree_add_first_child(ov, &root, "blocked");
    TreeNode allowed = tree_add_sibling(ov, &blocked, "allowed");
    TreeNode target_under_blocked = tree_add_first_child(ov, &blocked, "[target]");
    TreeNode target_under_allowed = tree_add_first_child(ov, &allowed, "[target]");
    assert(!tree_node_is_null(blocked));
    assert(!tree_node_is_null(allowed));
    assert(!tree_node_is_null(target_under_blocked));
    assert(!tree_node_is_null(target_under_allowed));

    Operate operate = {0};
    operate.overlay = ov;

    FilterCtx ctx = { .blocked_id = tree_node_id(blocked) };
    TreeNode r = operate_bfs_search(&operate, root, "[target]", filter_exclude_id, &ctx);
    assert(!tree_node_is_null(r));
    assert(tree_node_id(r) == tree_node_id(target_under_allowed));
}

typedef struct {
    uint64_t app_metadata_node_id;
} JumpDefinitionFilterContext;

static void test_gd_hierarchy_filter_skips_dot_metadata(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_gd_skip_metadata.umt");
    assert(ov != NULL);

    TreeNode root = ov->root;
    TreeNode metadata = tree_add_first_child(ov, &root, ".metadata");
    TreeNode defs = tree_add_sibling(ov, &metadata, "defs");
    TreeNode current_parent = tree_add_sibling(ov, &defs, "current-parent");

    TreeNode bad = tree_add_first_child(ov, &metadata, "[foo]");
    TreeNode good = tree_add_first_child(ov, &defs, "[foo]");
    TreeNode current = tree_add_first_child(ov, &current_parent, "foo");

    assert(!tree_node_is_null(bad));
    assert(!tree_node_is_null(good));
    assert(!tree_node_is_null(current));

    Operate operate = {0};
    operate.overlay = ov;

    AppState app = {0};
    app.tree_overlay = ov;
    app.operate = &operate;
    app.current_node = current;
    app.jump_back_stack = stack_create(32);
    app.jump_forward_stack = stack_create(32);

    JumpDefinitionFilterContext filter_ctx = { .app_metadata_node_id = 0 };
    int r = app_test_handle_jump_hierachy_definition(
        &app,
        root,
        "foo",
        app_test_jump_definition_filter,
        &filter_ctx
    );

    assert(r == 0);
    assert(tree_node_id(app.current_node) == tree_node_id(good));
    assert(tree_node_id(app.current_node) != tree_node_id(bad));

    stack_destroy(app.jump_back_stack);
    stack_destroy(app.jump_forward_stack);
}

static void test_expand_all_descendants_skips_meta_subtree(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_expand_all_descendants.umt");
    assert(ov != NULL);

    Wal *wal = wal_open("/tmp/um_expand_all_descendants.wal");
    assert(wal != NULL);

    Operate *operate = operate_create(wal, ov);
    assert(operate != NULL);

    TreeNode root = ov->root;
    TreeNode current = tree_add_first_child(ov, &root, "current");
    TreeNode meta = tree_add_first_child(ov, &current, ".meta");
    TreeNode child = tree_add_sibling(ov, &meta, "child");
    TreeNode meta_nested = tree_add_first_child(ov, &meta, "meta-nested");
    TreeNode grandchild = tree_add_first_child(ov, &child, "grandchild");

    assert(!tree_node_is_null(current));
    assert(!tree_node_is_null(meta));
    assert(!tree_node_is_null(child));
    assert(!tree_node_is_null(meta_nested));
    assert(!tree_node_is_null(grandchild));

    assert(operate_fold_node(operate, &meta) == 0);
    assert(operate_fold_node(operate, &child) == 0);
    assert(tree_node_is_collapsed(meta));
    assert(tree_node_is_collapsed(child));

    assert(operate_expand_all_descendants(operate, current) == 0);

    assert(tree_node_is_collapsed(meta));
    assert(!tree_node_is_collapsed(child));

    operate_destroy(operate);
    wal_close(wal);
}

static void test_lowercase_a_appends_text_in_headless_flow(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_append_headless.umt");
    assert(ov != NULL);

    Wal *wal = wal_open("/tmp/um_append_headless.wal");
    assert(wal != NULL);

    Operate *operate = operate_create(wal, ov);
    assert(operate != NULL);

    AppState app = {0};
    app.tree_overlay = ov;
    app.wal = wal;
    app.operate = operate;
    app.input_state = input_state_create();
    assert(app.input_state != NULL);

    TreeNode root = ov->root;
    TreeNode current = tree_add_first_child(ov, &root, "base");
    assert(!tree_node_is_null(current));
    app.current_node = current;

    UserOperation uo = input_convert(app.input_state, 'a', 0, NULL, false);
    assert(uo.type == UO_EDIT_NODE_END);

    app_apply_event(&app, uo);
    assert(app.input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_END);
    assert(app.node_text != NULL);
    assert(strcmp(app.node_text, "base") == 0);

    free(app.node_text);
    app.node_text = strdup("base-suffix");
    assert(app.node_text != NULL);

    app_apply_event(&app, (UserOperation){.type = UO_DO_EDIT_NODE});

    assert(strcmp(tree_node_text(app.current_node), "base-suffix") == 0);

    free(app.node_text);
    app.node_text = NULL;
    operate_destroy(operate);
    wal_close(wal);
}

static void test_edit_history_record_keeps_history_node_collapsed(void) {
    TreeOverlay *ov = tree_overlay_create_empty("/tmp/um_edit_history_collapsed.umt");
    assert(ov != NULL);

    Wal *wal = wal_open("/tmp/um_edit_history_collapsed.wal");
    assert(wal != NULL);

    Operate *operate = operate_create(wal, ov);
    assert(operate != NULL);

    TreeNode root = ov->root;
    TreeNode current = tree_add_first_child(ov, &root, "current");
    assert(!tree_node_is_null(current));

    TreeNode edit_history_node = app_metadata_key_node(operate, APP_META_EDIT_HISTORY);
    assert(!tree_node_is_null(edit_history_node));
    tree_node_set_collapse(ov, &edit_history_node, false);
    assert(!tree_node_is_collapsed(edit_history_node));

    Event event = {
        .type = EVENT_UPDATE_TEXT,
        .node_id = tree_node_id(current)
    };
    assert(operate_edit_history_record(operate, &event) == 0);

    edit_history_node = tree_find_by_id(ov, tree_node_id(edit_history_node));
    assert(!tree_node_is_null(edit_history_node));
    assert(tree_node_is_collapsed(edit_history_node));

    operate_destroy(operate);
    wal_close(wal);
}

int main(void) {
    test_search_hits_first_child();
    test_search_in_empty_subtree_returns_null();
    test_search_respects_filter();
    test_search_next_exact_matches_full_text_only();
    test_search_prev_exact_matches_full_text_only();
    test_search_next_exact_skips_dot_meta_subtree();
    test_search_prev_exact_skips_dot_meta_subtree();
    test_bfs_search_prefers_shallower_match();
    test_bfs_search_filter_blocks_branch();
    test_gd_hierarchy_filter_skips_dot_metadata();
    test_expand_all_descendants_skips_meta_subtree();
    test_lowercase_a_appends_text_in_headless_flow();
    test_edit_history_record_keeps_history_node_collapsed();

    printf("[PASS] operate search tests\n");
    return 0;
}
