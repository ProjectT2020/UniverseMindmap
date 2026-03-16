#include <assert.h>
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "mindmap_layout.h"
#include "../utils/logging.h"
#include "event/event.h"
#include "event/event_types.h"

static const int link_width = 3; // link width in tty

typedef struct {
    int width;
    int height;
} MindmapTreeSize;

static const char* display_text(TreeNode node) {
    static char buf[1024];
    if(tree_node_is_collapsed(node)) {
        snprintf(buf, sizeof(buf), "%s [+](%lu)", tree_node_text(node), tree_node_descendents(node));
        return buf;
    }else{
        return tree_node_text(node);
    }
}
static int display_width(TreeNode node) {
    const char *str = display_text(node);

    if (!str) return 0;
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    const char *p = str;
    size_t len = strlen(str);
    int w = 0;
    while (len > 0) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, len, &st);
        if (n == (size_t)-1 || n == (size_t)-2) {
            w += 1;
            p += 1;
            len -= 1;
            memset(&st, 0, sizeof(st));
            continue;
        }
        if (n == 0) break;
        int cw = wcwidth(wc);
        if (cw < 0) cw = 1;
        w += cw;
        p += n;
        len -= n;
    }
    return w;
}

static MindmapTreeSize *mindmap_tree_size_create(int width, int height) {
    MindmapTreeSize *mts = malloc(sizeof(MindmapTreeSize));
    if (!mts) return NULL;
    mts->width = width;
    mts->height = height;
    return mts;
}

static void mind_node_width(UiContext *ctx, TreeOverlay *ov, TreeNode n, int *out_w) {
    if (tree_node_is_null(n)) {
        *out_w = 0;
        return;
    }
    *out_w = display_width(n) + 1; // plus link column
}

void mind_node_height(UiContext *ctx, TreeOverlay *ov, TreeNode n, int *out_h) {
    if (tree_node_is_null(n)) {
        *out_h = 0;
        return;
    }
    if (tree_node_collapsed(n)) {// collapsed n->layout_height stores the non-collapsed height, not 1;
                                // layout layer needs to handle collapsed nodes specially
        *out_h = 1;
        return;
    }

    *out_h = tree_node_layout_height(ov, n);
}

void mind_node_layout_origin(UiContext *ctx, TreeOverlay *ov, TreeNode n, int *out_x, int *out_y) {
    TreeNode parent = tree_node_parent(ov, n);
    if (tree_node_is_null(parent)) {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    int parent_x = 0, parent_y = 0;
    mind_node_layout_origin(ctx, ov, parent, &parent_x, &parent_y);

    int x = parent_x + display_width(parent) + link_width; // plus link column
    int y = parent_y;

    for (TreeNode child = tree_node_first_child(ov, parent); !tree_node_is_null(child); child = tree_node_next_sibling(ov, child)) {
        if (tree_node_id(child) == tree_node_id(n)) break;
        int ch = 0;
        mind_node_height(ctx, ov, child, &ch);
        y += ch;
    }

    *out_x = x;
    *out_y = y;
}

void mind_node_layout_wh(UiContext *ctx, TreeOverlay *ov, TreeNode n,
                                int *out_x, int *out_y, int *out_w, int *out_h) {
    mind_node_layout_origin(ctx, ov, n, out_x, out_y);
    mind_node_width(ctx, ov, n, out_w);
    mind_node_height(ctx, ov, n, out_h);
}

static void render_link_vertical(int x, int y_start, int y_end) {
    int y0 = y_start;
    int y1 = y_end;
    if (y0 > y1) {
        int tmp = y0; y0 = y1; y1 = tmp;
    }
    for (int y = y0; y <= y1; ++y) {
        printf("\033[%d;%dH|", y + 1, x + 1);
    }
}

const char *cut_utf8_text_to_width(const char *text, int width) {
    static char buf[4096];

    mbstate_t st;
    memset(&st, 0, sizeof(st));

    const char *p = text;
    const char *last = text;   // last character before the width limit
    int w = 0;

    while (*p) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &st);

        if (n == (size_t)-1) {
            // illegal byte, treat as single byte
            memset(&st, 0, sizeof(st));
            if (w + 1 > width) break;
            w += 1;
            p += 1;
            last = p;
            continue;
        }

        if (n == (size_t)-2) {
            // incomplete UTF-8, stop
            break;
        }

        if (n == 0) {
            // '\0'
            break;
        }

        int cw = wcwidth(wc);
        if (cw < 0) cw = 1;

        if (w + cw > width)
            break;

        w += cw;
        p += n;
        last = p;
    }

    size_t out_len = last - text;
    if (out_len >= sizeof(buf))
        out_len = sizeof(buf) - 1;

    memcpy(buf, text, out_len);
    buf[out_len] = '\0';

    return buf;
}

struct utf8_skip_result {
    const char *ptr;
    int skipped_cols;
};

static const char *utf8_skip_cols(const char *s, int *skip_cols) {
    mbstate_t st;
    memset(&st, 0, sizeof(st));

    const char *p = s;
    int w = 0;

    while (*p && w < *skip_cols) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &st);

        if (n == (size_t)-1) {
            memset(&st, 0, sizeof(st));
            p++;
            w++;
            continue;
        }
        if (n == (size_t)-2 || n == 0)
            break;

        int cw = wcwidth(wc);
        if (cw < 0) cw = 1;

        if (w + cw > *skip_cols) {
            /* half-width clipping → full-width character discard */
            w += cw;
            p += n;
            break;
        }

        w += cw;
        p += n;
    }

    *skip_cols = w;
    return p;
}

static bool mindmap_node_should_hightlight(UiContext *ctx, TreeNode n) {
    uint64_t node_id = tree_node_id(n);
    TreeNode parent = ctx->current_node;
    while(!tree_node_is_null(parent)){
        if(tree_node_id(parent) == node_id){
            return true;
        }
        parent = tree_node_parent(ctx->overlay, parent);
    }
    return false;
}

/**
 * position: -1: don't show position; >0: show position 
 */
static void mindmap_render_node(UiContext *ctx, TreeNode n, int origin_x, int origin_y, int parent_text_render_y,
    int position
) {
    int view_x = ctx->view_x;
    int view_y = ctx->view_y;
    int view_w = ctx->width;
    int view_h = ctx->height;

    int node_x = 0, node_y = 0, node_w = 0, node_h = 0;
    mind_node_height(ctx, ctx->overlay, n, &node_h);
    mind_node_width(ctx, ctx->overlay, n, &node_w);
    node_x = origin_x;
    node_y = origin_y;
    // check intersection with view
    if( node_y + node_h <= view_y ||
        node_y >= view_y + view_h ){
        return;
    }

    int node_render_x = node_x - view_x;
    int node_render_y = node_y - view_y ;

    const char *text = tree_node_text(n);
    const char *display_str = display_text(n);
    int text_w = display_width(n);

    int text_render_x = node_render_x;
    int text_render_y = node_render_y + (node_h - 1) / 2;
    // adjust text_render_y to be within view
    if(text_render_y < 0){
        text_render_y = 0;
    }
    if(text_render_y >= view_h){
        text_render_y = view_h - 1;
    }
    // adjust text to be within view width
    const char *render_text = display_str;
    if(text_render_x + text_w >= view_w){
        // need to clip text
        int avail_w = view_w - text_render_x;
        render_text = cut_utf8_text_to_width(display_str, avail_w);
    }

    bool is_current = (tree_node_id(n) == tree_node_id(ctx->current_node));
    if(text_render_x >= 0){
        bool should_highlight = mindmap_node_should_hightlight(ctx, n);
        // do render text
        if(is_current){
            log_debug("Render current node #%lu at (%d, %d)", tree_node_id(n), text_render_x, text_render_y);
            ctx->current_text_x = text_render_x;
            ctx->current_text_y = text_render_y;
        }
        // show text
        printf("\033[%d;%dH", text_render_y + 1, text_render_x + 1);
        if (is_current) {
            printf("\033[7m");
        }else if(should_highlight){
            printf("\033[48;5;250;38;5;0m");
            // printf("\033[4m");
        }

        if(render_text == NULL || render_text[0] == '\0'){
            printf("\b  ");
        }
        printf("%s", render_text);
        if (is_current || should_highlight) {
            printf("\033[0m");
        }

        if(is_current && ctx->show_ancestors_in_one_line){
            // show parent text in the same line if possible
            int64_t child_text_render_x = text_render_x;
            TreeNode parent = tree_node_parent(ctx->overlay, n);
            while(!tree_node_is_null(parent)){
                const char *parent_text = tree_node_text(parent);
                int parent_text_w = display_width(parent);
                int64_t parent_render_x = child_text_render_x - link_width - parent_text_w;
                if(parent_render_x >= 0){
                    printf("\033[%d;%dH", text_render_y + 1, parent_render_x + 1);
                    printf("\033[3m"); // italic
                    printf("%s", parent_text);
                    printf("\033[0m"); // reset
                }
                child_text_render_x = parent_render_x;
                parent = tree_node_parent(ctx->overlay, parent);
            }
        }

    }else{
        // need to skip some columns
        int skip_cols = -text_render_x;
        const char *skip_ptr = utf8_skip_cols(display_str, &skip_cols);
        const char *final_text = skip_ptr;
        int final_text_w = text_w - skip_cols;
        if (final_text_w > view_w) {
            final_text = cut_utf8_text_to_width(final_text, view_w);
        }
        // do render text
        if(is_current){
            log_debug("Render current node '%s' at (%d, %d) skip=%d", tree_node_text(n), text_render_x, text_render_y, skip_cols);
            ctx->current_text_x = 0;
            ctx->current_text_y = text_render_y;
        }
        if(text_render_x + skip_cols >= 0){
            printf("\033[%d;%dH", text_render_y + 1, text_render_x + skip_cols + 1);
            if (is_current) {
                printf("\033[7m");
            }
            printf("%s", final_text);
            if (is_current) {
                printf("\033[0m");
            }
        }
    }

    bool marked_node = false;

    // render link
    uint64_t node_id = tree_node_id(n);
    TreeNode parent = tree_node_parent(ctx->overlay, n);
    TreeNode top_sibling = ui_first_visible_child(ctx, parent);
    TreeNode bottom_sibling = ui_last_visible_child(ctx, parent);
    bool is_first_child = node_id == tree_node_id(top_sibling);
    bool is_last_child = node_id == tree_node_id(bottom_sibling);
    int link_x = node_render_x - link_width + 1;

    // show jump mark
    if(!tree_node_is_null(parent) && link_x >=0 && link_x < view_w){
        const int mark_page_size = 26 * 26; // 676
        if(ctx->mark_and_show_visible_nodes && ctx->mark >= mark_page_size){
            ctx->mark_and_show_visible_nodes = false;
        }
        int link_y = text_render_y;
        if(ctx->mark_and_show_visible_nodes){
            if(0 <= ctx->mark && ctx->mark < mark_page_size){
                // do mark
                ctx->node_marks[ctx->mark] = node_id;
                // show position
                char mark1 = 'a' + (ctx->mark / 26);
                char mark2 = 'a' + (ctx->mark % 26);
                
                printf("\033[1m"); // bold
                printf("\033[4m"); // underline
                printf("\033[%d;%dH%c%c", link_y + 1, link_x + 1 - 1, mark1, mark2);
                printf("\033[24m"); // reset underline
                printf("\033[22m"); // reset bold
                marked_node = true;
            }
            ctx->mark++;
        }else if(position > 0){
            char pos = '0';
            if(position < 10){
                pos = '0' + position;
            }else if(position < 10 + 26){
                pos = 'a' + (position - 10);
            }else if(position < 10 + 26 + 26){
                pos = 'A' + (position - 10 - 26);
            }
            printf("\033[%d;%dH%c", link_y + 1, link_x + 1, pos);
        } else if(is_first_child){
            // first child, draw ┌
            int link_y = text_render_y;
            if(link_x >=0 && link_y >=0 && link_y < view_h){
                if(text_render_y == parent_text_render_y){
                    printf("\033[%d;%dH┬", link_y + 1, link_x + 1);
                }else{
                    printf("\033[%d;%dH╭", link_y + 1, link_x + 1);
                }
            }
        }else if(is_last_child){
            // last child, draw └
            int link_y = text_render_y;
            if(link_x >=0 && link_y >=0 && link_y < view_h){
                if(text_render_y == parent_text_render_y){
                    printf("\033[%d;%dH┴", link_y + 1, link_x + 1);
                }else{
                    printf("\033[%d;%dH╰", link_y + 1, link_x + 1);
                }
            }
        }else {
            // middle child, draw ├
            int link_y = text_render_y;
            if(link_x >=0 && link_y >=0 && link_y < view_h){
                if(text_render_y == parent_text_render_y){
                    printf("\033[%d;%dH┼", link_y + 1, link_x + 1);
                }else{
                    printf("\033[%d;%dH├", link_y + 1, link_x + 1);
                }
            }
        }
        // draw vertical link 
        if(!tree_node_collapsed(n)){
            if(!is_first_child){
                for(int link_y = text_render_y - 1; link_y >= node_render_y; link_y--){
                    if(link_x >=0 && link_y >=0 && link_y < view_h){
                        if(link_y == parent_text_render_y){
                            printf("\033[%d;%dH┤", link_y + 1, link_x + 1);
                        }else{
                            printf("\033[%d;%dH│", link_y + 1, link_x + 1);
                        }
                    }
                }
            }
            if(!is_last_child){
                for(int link_y = text_render_y + 1; link_y < node_render_y + node_h; link_y++){
                    if(link_x >=0 && link_y >=0 && link_y < view_h){
                        if(link_y == parent_text_render_y){
                            printf("\033[%d;%dH┤", link_y + 1, link_x + 1);
                        }else{
                            printf("\033[%d;%dH│", link_y + 1, link_x + 1);
                        }
                    }
                }
            }
        }

    }


    if (tree_node_is_collapsed(n)) {
        return;
    }

    int child_start_x = node_x + text_w + link_width;
    int child_start_y = node_y;

    int child_position = 0;
    for (TreeNode child = ui_first_visible_child(ctx, n);
        !tree_node_is_null(child);
        child = ui_next_visible_sibling(ctx, child)
    ) {
        int pos = -1;
        if(ctx->show_child_position && tree_node_id(n) == tree_node_id(ctx->current_node)){
            pos = child_position;
        }
        mindmap_render_node(ctx, child, child_start_x, child_start_y, text_render_y, pos);
        int ch = 0;
        mind_node_height(ctx, ctx->overlay, child, &ch);
        child_start_y += ch;
        child_position++;
    }
    
    // render '+'/'-' connector
    TreeNode first_visible_child = ui_first_visible_child(ctx, n);
    if (!tree_node_is_null(first_visible_child) && text_render_x + text_w + 1 >= 1  && text_render_x + text_w + 1 < view_w) {
        TreeNode last_visible_child = ui_last_visible_child(ctx, n);
        TreeNode next_sibling = ui_next_visible_sibling(ctx, first_visible_child);
        if(tree_node_id(first_visible_child) == tree_node_id(last_visible_child)){
            if(!marked_node){
                printf("\033[%d;%dH ─", text_render_y + 1, text_render_x + text_w + 1);
            }
        }else if(tree_node_id(next_sibling) == tree_node_id(last_visible_child)){
            // printf("\033[%d;%dH ┬", text_render_y + 1, text_render_x + text_w + 1);
        }else{
            // printf("\033[%d;%dH ┼", text_render_y + 1, text_render_x + text_w + 1);
        }
    }

}

void mindmap_layout_and_render(UiContext *ctx) {

    if(ctx->fix_view){
        ctx->fix_view = false;
    }else{
        log_debug("tty size: width=%d, height=%d", ctx->width, ctx->height);
        int cx = 0, cy = 0, cw = 0, ch = 0;
        mind_node_layout_wh(ctx, ctx->overlay, ctx->current_node, &cx, &cy, &cw, &ch);
        // int current_text_y = cy + ch / 2;
        int64_t delta_top = cy - ctx->view_y;
        int64_t delta_bottom = (cy + ch) - (ctx->view_y + ctx->height);
        int64_t delta = 0;// delta of the lowest absolute value
        if(delta_top < 0 && delta_bottom < 0){
            delta = delta_top > delta_bottom ? delta_top : delta_bottom;
        }
        if(delta_top > 0 && delta_bottom > 0){
            delta = delta_top < delta_bottom ? delta_top : delta_bottom;
        }
        ctx->view_y += delta;
        

        if(ctx->view_x < cx + cw - ctx->width + 2){ // text exceeds view right
            ctx->view_x = cx + cw - ctx->width + 2; 
        }
        if (ctx->view_x > cx - 2) // text is on the left side of the view
            ctx->view_x = cx - 2;
    }
    printf("\033[2J");
    mindmap_render_node(ctx, ctx->overlay->root, 0, 0, -1, -1/*don't show position*/);
}