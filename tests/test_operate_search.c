#include <assert.h>
#include <stdio.h>
#include <stdint.h>

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

    UiContext ui = {0};
    ui.current_node = current;

    AppState app = {0};
    app.tree_overlay = ov;
    app.operate = &operate;
    app.ui = &ui;
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
    assert(tree_node_id(app.ui->current_node) == tree_node_id(good));
    assert(tree_node_id(app.ui->current_node) != tree_node_id(bad));

    stack_destroy(app.jump_back_stack);
    stack_destroy(app.jump_forward_stack);
}

int main(void) {
    test_search_hits_first_child();
    test_search_in_empty_subtree_returns_null();
    test_search_respects_filter();
    test_bfs_search_prefers_shallower_match();
    test_bfs_search_filter_blocks_branch();
    test_gd_hierarchy_filter_skips_dot_metadata();

    printf("[PASS] operate search tests\n");
    return 0;
}
