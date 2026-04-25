#pragma once

#include "ui/ui.h"

typedef struct {
    int height, width;
    int x, y;
} NodeLayoutInfo;

// utils
void mind_node_layout_wh(UiContext *ctx, TreeOverlay *ov, TreeNode n,
                                int *out_x, int *out_y, int *out_w, int *out_h) ;

void mindmap_layout_and_render(UiContext *ctx); 
int mindmap_layout_update(UiContext *ui, TreeOverlay *overlay, Event *e);

// Layout calculation functions
void mind_node_layout_origin(TreeOverlay *ov, TreeNode n, double *out_x, int *out_y, double (*display_width_func)(TreeNode n),
    double link_width) ;
