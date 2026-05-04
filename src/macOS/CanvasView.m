#import <Cocoa/Cocoa.h>

#include "../app/app.h"
#include "../layout/mindmap_layout.h"
#include "../ui/ui.h"
#include "../ui/input_state.h"
#include "../tree/tree_overlay.h"
#include "../utils/logging.h"

#import "CanvasView.h"


static int default_text_points = 12;// 14
static int default_base_points = 15; // 17

extern bool dont_adjust_doc_view_by_current;
extern bool is_hand_on_mouse;
static double view_w = 0;
static double view_h = 0;

AppState* app_state = NULL;

#pragma mark - Canvas View callbacks
void canvas_view_render(void *view){
    CanvasView *v = (__bridge CanvasView *)view;
    [v performWithoutImplicitAnimation:^{
        [v render_mindmap];
    }];
}
void canvas_view_center_view_on_current(void *view){
    CanvasView *v = (__bridge CanvasView *)view;
    // move doc view to cover current node
    double origin_x = 0;
    int origin_y_int = 0;
    int default_link_points = default_text_points * 3 / 2;
    mind_node_layout_origin(app_state->tree_overlay, app_state->current_node, &origin_x, &origin_y_int, 
        text_field_display_width, default_link_points
    );
    int layout_height = mind_node_height(app_state->tree_overlay, app_state->current_node);
    double origin_y = (origin_y_int-0) * default_base_points;
    double y_end = origin_y + layout_height * default_base_points;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, (origin_y + y_end ) / 2.0 - view_h / 2.0);
    
    [v performWithoutImplicitAnimation:^{
        [v render_mindmap];
    }];
}

char* canvas_view_get_search_query(void *view){
    CanvasView *v = (__bridge CanvasView *)view;
    NSTextView *textView = v.bottomCommandTextView;
    textView.font = default_font();
    textView.drawsBackground = YES;
    textView.backgroundColor = [NSColor blackColor];
    textView.textColor = [NSColor whiteColor];
    textView.string = @"/";
    textView.hidden = NO;
    NSSize textSize = [textView.string sizeWithAttributes:@{NSFontAttributeName: default_font()}];
    textView.frame = CGRectMake(0, 0, textSize.width + default_text_points, default_base_points);
    [v layout];
    [v.window makeFirstResponder:textView];
    return NULL;
}

char* canvas_view_get_search_backward_query(void *view){
    CanvasView *v = (__bridge CanvasView *)view;
    NSTextView *textView = v.bottomCommandTextView;
    textView.font = default_font();
    textView.drawsBackground = YES;
    textView.backgroundColor = [NSColor blackColor];
    textView.textColor = [NSColor whiteColor];
    textView.string = @"?";
    textView.hidden = NO;
    NSSize textSize = [textView.string sizeWithAttributes:@{NSFontAttributeName: default_font()}];
    textView.frame = CGRectMake(0, 0, textSize.width + default_text_points, default_base_points);
    [v layout];
    [v.window makeFirstResponder:textView];
    return NULL;
}

void canvas_view_info_message(void *view){
    CanvasView *v = (__bridge CanvasView *)view;
    if(app_state->info_message && strlen(app_state->info_message) > 0){
        NSString *text = [NSString stringWithUTF8String:app_state->info_message];
        NSSize textSize = [text sizeWithAttributes:@{NSFontAttributeName: default_font()}];
        CGFloat pad = default_text_points * 2;
        CGFloat layerW = textSize.width;
        CGFloat layerX = v.bounds.size.width - layerW - pad;

        // Remove old layer, create fresh one — never reuse, avoids stale content flash
        [v.infoMessageLayer removeFromSuperlayer];
        CATextLayer *newLayer = [CATextLayer layer];
        newLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        newLayer.fontSize = default_text_points;
        newLayer.font = default_font();
        newLayer.alignmentMode = kCAAlignmentRight;
        newLayer.foregroundColor = [NSColor whiteColor].CGColor;
        newLayer.backgroundColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.85].CGColor;
        newLayer.frame = CGRectMake(layerX, 0, layerW, default_base_points);
        newLayer.string = text;
        newLayer.opacity = 1.0;
        [v.layer addSublayer:newLayer];
        v.infoMessageLayer = newLayer;
    }else{
        [v hideInfoMessage];
    }
}

TreeNode canvas_view_get_viewport_bottommost_sibling(void *ui_ctx, TreeOverlay *ov, TreeNode current) {
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    TreeNode parent = tree_node_parent(ov, current);
    uint64_t parent_id = tree_node_id(parent);
    uint64_t best_id = 0;
    double best_y = -INFINITY;
    for (NSDictionary *info in v.visibleNodeInfos) {
        if ([info[@"parent"] unsignedLongLongValue] == parent_id) {
            double y = [info[@"y"] doubleValue];
            if (y > best_y) {
                best_y = y;
                best_id = [info[@"id"] unsignedLongLongValue];
            }
        }
    }
    if (best_id != 0) {
        return tree_find_by_id(ov, best_id);
    }
    return (TreeNode){.kind = TREE_NODE_NULL};
}

TreeNode canvas_view_get_viewport_topmost_sibling(void *ui_ctx, TreeOverlay *ov, TreeNode current) {
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    TreeNode parent = tree_node_parent(ov, current);
    uint64_t parent_id = tree_node_id(parent);
    uint64_t best_id = 0;
    double best_y = INFINITY;
    for (NSDictionary *info in v.visibleNodeInfos) {
        if ([info[@"parent"] unsignedLongLongValue] == parent_id) {
            double y = [info[@"y"] doubleValue];
            if (y < best_y) {
                best_y = y;
                best_id = [info[@"id"] unsignedLongLongValue];
            }
        }
    }
    if (best_id != 0) {
        return tree_find_by_id(ov, best_id);
    }
    return (TreeNode){.kind = TREE_NODE_NULL};
}

void canvas_view_prev_page(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    double actual_view_height = view_h - 2 * default_base_points;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, v.viewOrigon.y - actual_view_height);
}
void canvas_view_next_page(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    double actual_view_height = view_h - 2 * default_base_points;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, v.viewOrigon.y + actual_view_height);
}
void canvas_view_next_half_page(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    double actual_view_height = view_h - 2 * default_base_points;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, v.viewOrigon.y + actual_view_height / 2.0);
}
void canvas_view_prev_half_page(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    double actual_view_height = view_h - 2 * default_base_points;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, v.viewOrigon.y - actual_view_height / 2.0);
}
void canvas_view_current_left(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    double origin_x = 0;
    int origin_y_int = 0;
    int default_link_points = default_text_points * 3 / 2;
    mind_node_layout_origin(app_state->tree_overlay, app_state->current_node, &origin_x, &origin_y_int, 
        text_field_display_width, default_link_points);
    v.viewOrigon = CGPointMake(origin_x - default_base_points, v.viewOrigon.y);
}
void canvas_view_current_right(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    double origin_x = 0;
    int origin_y_int = 0;
    int default_link_points = default_text_points * 3 / 2;
    mind_node_layout_origin(app_state->tree_overlay, app_state->current_node, &origin_x, &origin_y_int, 
        text_field_display_width, default_link_points);
    int node_width = text_field_display_width(app_state->current_node);
    v.viewOrigon = CGPointMake(origin_x + node_width - view_w + default_base_points, v.viewOrigon.y);
}
void canvas_view_half_screen_right(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    CGFloat halfW = screenRect.size.width / 2.0;
    v.viewOrigon = CGPointMake(v.viewOrigon.x + halfW, v.viewOrigon.y);
}
void canvas_view_half_screen_left(void *ui_ctx){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    CGFloat halfW = screenRect.size.width / 2.0;
    v.viewOrigon = CGPointMake(v.viewOrigon.x - halfW, v.viewOrigon.y);
}
void canvas_view_down(void *ui_ctx, int lines){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, v.viewOrigon.y + lines * default_base_points);
}
void canvas_view_up(void *ui_ctx, int lines){
    CanvasView *v = (__bridge CanvasView *)ui_ctx;
    dont_adjust_doc_view_by_current = true;
    v.viewOrigon = CGPointMake(v.viewOrigon.x, v.viewOrigon.y - lines * default_base_points);
}

CGColorRef randomVividColor() {
    // Pick a vivid (high-saturation) background color
    CGFloat hue = (arc4random_uniform(1000) / 1000.0);
    CGFloat saturation = 0.92 + ((arc4random_uniform(80)) / 1000.0); // ~0.92-1.0
    CGFloat brightness = 0.78 + ((arc4random_uniform(170)) / 1000.0); // ~0.78-0.95
    NSColor *bgColor = [NSColor colorWithCalibratedHue:hue saturation:saturation brightness:brightness alpha:1.0];
    return bgColor.CGColor;
}

NSFont *default_font() {
    return [NSFont monospacedSystemFontOfSize:default_text_points weight:NSFontWeightRegular];
}

NSFont *default_font_bold() {
    return [NSFont monospacedSystemFontOfSize:default_text_points weight:NSFontWeightBold];
}

CGSize measure_text(NSString *text) {
    NSFont *font = default_font();
    NSDictionary *attr = @{ NSFontAttributeName: font };
    CGSize measured = [text sizeWithAttributes:attr];
    return measured;
}

static bool intersect1D(double a_min, double a_max, double b_min, double b_max) {
    return (a_min <= b_max) && (b_min <= a_max);
}

#pragma mark - Node
@interface Node : NSObject
@property(nonatomic,strong) CALayer *layer;
@end
@implementation Node
@end

double text_field_display_width(TreeNode node){
    const char*text = tree_node_text(node);
    NSSize textSize = measure_text([NSString stringWithUTF8String:text]);
    return textSize.width;
}

#pragma mark - Text Input View
@implementation TextInputView
- (void)cancelOperation:(id)sender {
    logd("Text input cancelled");
    self.hidden = YES;
    self.string = @"";

    app_state->input_state->type = INPUT_STATE_DEFAULT;

    if (self.onCancel) {
        logd("Text input onCancel callback triggered");
        self.onCancel();
    }
}
- (void)paste:(id)sender {
    // Strip formatting from pasted text — use plain text only
    [self pasteAsPlainText:sender];
}
@end

#pragma mark - Canvas View (Layer-based, no drawRect)

@implementation CanvasView

- (void)hideInfoMessage {
    [self.infoMessageLayer removeFromSuperlayer];
    self.infoMessageLayer = nil;
}

- (double)mindmap_x2canvas_x:(double)mindmap_x {
    return mindmap_x  - self.viewOrigon.x;
}
- (double)mindmap_y2canvas_y:(double)mindmap_y {
    return self.mindmapDocLayer.bounds.size.height - (mindmap_y  - self.viewOrigon.y );
}

- (BOOL)textView:(NSTextView *)textView
shouldChangeTextInRange:(NSRange)range
replacementString:(NSString *)string
{
    if(range.location < 1 && app_state->input_state->type != INPUT_STATE_TYPE_GET_NAME
    && app_state->input_state->type != INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
    && app_state->input_state->type != INPUT_STATE_TYPE_GET_NAME_INSERT_END){
        // Cmd+Backspace deletes from position 0: strip query text but keep the "/" or "?" prefix
        if (range.location == 0 && range.length > 1) {
            textView.string = [textView.string substringToIndex:1];
            [textView setSelectedRange:NSMakeRange(1, 0)];
        }
        return NO;
    }
    // editing node text
    if(app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME 
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_END){
        NSString *old = textView.string;
        CGFloat oldWidth = measure_text(old).width;
        NSString *newString = [old stringByReplacingCharactersInRange:range
                                 withString:string];
        CGFloat newWidth = measure_text(newString).width;
        BOOL widthAdded = (newWidth > oldWidth);
        if(widthAdded){
            textView.frame = NSMakeRect(textView.frame.origin.x, textView.frame.origin.y,
                        newWidth + default_text_points / 4.0, textView.frame.size.height);
        }
    }
    // Resize text view as user types in search / backward / command modes
    if(app_state->input_state->type == INPUT_STATE_TYPE_SEARCH_QUERY
    || app_state->input_state->type == INPUT_STATE_TYPE_SEARCH_BACKWARD_QUERY
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_COMMAND){
        NSString *newString = [textView.string stringByReplacingCharactersInRange:range withString:string];
        CGFloat newWidth = measure_text(newString).width;
        textView.frame = NSMakeRect(textView.frame.origin.x, textView.frame.origin.y,
            newWidth + default_text_points, textView.frame.size.height);
    }
    if ([string isEqualToString:@"\n"] || [string isEqualToString:@"\t"]) {
        [textView.window makeFirstResponder:self];
        textView.hidden = YES;

        // editing node text
        if(app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME 
        || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
        || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_END){
            app_state->input_state->type = INPUT_STATE_DEFAULT;

            char name_buf[256];
            strncpy(name_buf, [textView.string UTF8String], sizeof(name_buf)-1);
            name_buf[sizeof(name_buf)-1] = '\0';
            app_state->node_text = strdup(name_buf);
            UserOperation uo = (UserOperation){.type = UO_DO_EDIT_NODE};
            app_apply_event(app_state, uo);
            [self layout];
            [self performWithoutImplicitAnimation: ^{
                    [self render_mindmap];
                }
            ];
            if([string isEqualToString:@"\t"]){
                UserOperation uo = (UserOperation){.type = UO_ADD_CHILD_TO_TAIL};
                app_apply_event(app_state, uo);
                [self layout];
                [self performWithoutImplicitAnimation:^{
                    [self render_mindmap];
                }];
            }
            return NO;
        }

        // Command mode: parse and execute command
        if(app_state->input_state->type == INPUT_STATE_TYPE_GET_COMMAND){
            NSString *cmdText = [textView.string substringFromIndex:1]; // strip ":"
            if(cmdText.length > 0){
                if(app_state->command) free(app_state->command);
                app_state->command = strdup([cmdText UTF8String]);
                UserOperation cmdUo = input_convert(app_state->input_state, '\0', 0,
                    [cmdText UTF8String], NO);
                app_apply_event(app_state, cmdUo);
            }
            [self layout];
            [self performWithoutImplicitAnimation:^{
                [self render_mindmap];
            }];
            return NO;
        }

        UserOperation uo = input_convert(app_state->input_state, '\0', 0, 
        [[textView.string substringFromIndex:1] UTF8String], NO);
        app_apply_event(app_state, uo);
        [self layout];
        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];
        return NO; 
    }
    return YES;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
        self.wantsLayer = YES;
        self.layer.anchorPoint = CGPointZero;
        // self.layer = [CALayer layer];
        // self.layer.backgroundColor = [NSColor grayColor].CGColor;

        self.bottomCommandTextView = [[TextInputView alloc] initWithFrame:CGRectMake(0, default_base_points, 
                self.bounds.size.width, default_base_points)];

        // prevent confusing behavior of automatic substitutions while typing commands (e.g. "--" turning into "—")
        self.bottomCommandTextView.automaticDashSubstitutionEnabled = NO;
        self.bottomCommandTextView.automaticQuoteSubstitutionEnabled = NO;

        self.bottomCommandTextView.font = default_font();
        self.bottomCommandTextView.drawsBackground = YES;
        self.bottomCommandTextView.backgroundColor = [NSColor grayColor];
        self.bottomCommandTextView.textColor = [NSColor blackColor];
        self.bottomCommandTextView.string = @"Command --";
        self.bottomCommandTextView.delegate = (self);
        self.bottomCommandTextView.hidden = YES;
        self.bottomCommandTextView.onCancel = ^{
          logd("callback: Text input cancelled");
          app_state->input_state->type = INPUT_STATE_DEFAULT;
          [self layout];
          [self.window makeFirstResponder:self];
          [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
          }];
        };
        [self addSubview:self.bottomCommandTextView positioned:NSWindowAbove relativeTo:nil];

        self.mindmapDocLayer = [CALayer layer];
        self.mindmapDocLayer.anchorPoint = CGPointZero;
        // _mindmapDocLayer.backgroundColor = [NSColor lightGrayColor].CGColor;
        self.mindmapDocLayer.frame = self.bounds;
        [self.layer addSublayer:self.mindmapDocLayer];

        self.breadsCrumbLayer = [CATextLayer layer];
        self.breadsCrumbLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        self.breadsCrumbLayer.fontSize = default_text_points;
        self.breadsCrumbLayer.font = default_font();
        self.breadsCrumbLayer.alignmentMode = kCAAlignmentLeft;
        self.breadsCrumbLayer.foregroundColor = [NSColor blackColor].CGColor;
        self.breadsCrumbLayer.backgroundColor = [NSColor whiteColor].CGColor;
        self.breadsCrumbLayer.truncationMode = kCATruncationEnd;
        self.breadsCrumbLayer.wrapped = NO;
        self.breadsCrumbLayer.hidden = YES;
        [self.layer addSublayer:self.breadsCrumbLayer];

        self.infoLayer = [CATextLayer layer];
        self.infoLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        self.infoLayer.fontSize = 14;
        self.infoLayer.alignmentMode = kCAAlignmentLeft;
        self.infoLayer.foregroundColor = [NSColor colorWithCalibratedWhite:0.08 alpha:0.5].CGColor;
        self.infoLayer.backgroundColor = [NSColor colorWithCalibratedWhite:1.0 alpha:0.20].CGColor;
        self.infoLayer.string = @"Info --";
        self.infoLayer.frame = CGRectMake(0, 12, self.bounds.size.width, 1); // height set dynamically in updateInfoView
        self.infoLayer.hidden = !debuging;
        [self.layer addSublayer:self.infoLayer];

        // Info message layer — bottom, right-aligned, always visible (opacity toggled, no hidden/visible flash)
        self.infoMessageLayer = [CATextLayer layer];
        self.infoMessageLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        self.infoMessageLayer.fontSize = default_text_points;
        self.infoMessageLayer.font = default_font();
        self.infoMessageLayer.alignmentMode = kCAAlignmentRight;
        self.infoMessageLayer.foregroundColor = [NSColor whiteColor].CGColor;
        self.infoMessageLayer.backgroundColor = [NSColor clearColor].CGColor;
        self.infoMessageLayer.string = @"";
        self.infoMessageLayer.frame = CGRectMake(0, 0,
            self.bounds.size.width, default_base_points);
        [self hideInfoMessage];
        [self.layer addSublayer:self.infoMessageLayer];

        _latestMouseViewPoint = CGPointMake(CGRectGetMidX(frameRect), CGRectGetMidY(frameRect));
        _viewOrigon = CGPointZero;
        _zoomScale = 1.0;

        // initialize view size so early rendering (first frame) has correct bounds
        view_w = CGRectGetWidth(frameRect);
        view_h = CGRectGetHeight(frameRect);
        if(app_state != NULL) {
            [self render_mindmap];
        }else{
            logd("app_state is NULL, skipping mindmap rendering");
        }

        [self updateInfoView];
        

    }
    return self;
}

- (CGFloat)breadsCrumbHeight {
    if (app_state == NULL) {
        return 0;
    }
    return app_state->show_breadcrumb_title ? default_base_points : 0;
}

- (void)applyDocAndBreadcrumbLayerFrames {
    CGFloat totalH = CGRectGetHeight(self.bounds);
    CGFloat totalW = CGRectGetWidth(self.bounds);
    CGFloat crumbH = [self breadsCrumbHeight];
    CGFloat docY = 0;
    CGFloat docH = totalH - crumbH;
    if (docH < 0) {
        docH = 0;
    }

    view_w = totalW;
    view_h = docH;

    [self performWithoutImplicitAnimation:^{
        self.mindmapDocLayer.bounds = NSMakeRect(0, 0, totalW, docH);
        self.mindmapDocLayer.frame = NSMakeRect(0, docY, totalW, docH);
        self.breadsCrumbLayer.frame = NSMakeRect(0, totalH - crumbH, totalW, crumbH);
    }];
}

- (BOOL)mindmap_render_node:(AppState *)state node:(TreeNode)node worldLayer:(CALayer *)worldLayer
    originX:(double)origin_x originY:(double)origin_y parentY:(double)parent_y
{
    int default_link_padding = default_text_points / 4;
    int default_link_points = default_text_points * 3 / 2;
    if(default_link_points < default_link_padding * 2) default_link_points = default_link_padding * 2;
    uint64_t id = tree_node_id(node);
    bool is_current = tree_node_id(node) == tree_node_id(state->current_node);
    BOOL containsCurrent = is_current;
    int layout_height = mind_node_height(state->tree_overlay, node);
    if(tree_node_collapsed(node))   {
        layout_height = 1;
    };
    double layout_height_points = layout_height * default_base_points;
    if(!intersect1D(origin_y, origin_y + layout_height_points, self.viewOrigon.y, self.viewOrigon.y + view_h)){
        return containsCurrent;
    }

    // draw link to parent
    TreeNode parent = tree_node_parent(state->tree_overlay, node);
    if(!tree_node_is_null(parent)){
        CAShapeLayer *linkLayerVertical = [CAShapeLayer layer];
        CAShapeLayer *linkLayerHorizontal = [CAShapeLayer layer];
        linkLayerVertical.strokeColor = [NSColor blackColor].CGColor;
        linkLayerHorizontal.strokeColor = [NSColor blackColor].CGColor;
        CGMutablePathRef pathVertical = CGPathCreateMutable();
        CGMutablePathRef pathHorizontal = CGPathCreateMutable();

        if(id == tree_node_id(tree_node_first_child(state->tree_overlay, parent))
         && id == tree_node_id(tree_node_last_child(state->tree_overlay, parent))
        ){
          // vertical do nothing
          // horizontal
          CGPathMoveToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
          CGPathAddLineToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x - default_link_padding],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);

        }else if(id == tree_node_id(tree_node_first_child(state->tree_overlay, parent))
        ){
          CGPathMoveToPoint( pathVertical, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y + layout_height_points]);
          CGPathAddLineToPoint( pathVertical, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);

          CGPathMoveToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
          CGPathAddLineToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x - default_link_padding],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
        }else if(id == tree_node_id(tree_node_last_child(state->tree_overlay, parent))
        ){
          CGPathMoveToPoint( pathVertical, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y]);
          CGPathAddLineToPoint( pathVertical, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
          
          CGPathMoveToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
          CGPathAddLineToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x - default_link_padding],
              [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
        }else{
          CGPathMoveToPoint( pathVertical, NULL,
           [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
            [self mindmap_y2canvas_y:origin_y ]);
          CGPathAddLineToPoint( pathVertical, NULL,
           [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
            [self mindmap_y2canvas_y:origin_y + layout_height_points]);

          CGPathMoveToPoint( pathHorizontal, NULL, 
          [self mindmap_x2canvas_x:origin_x - 0.5 * default_link_points],
           [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
          CGPathAddLineToPoint( pathHorizontal, NULL,
          [self mindmap_x2canvas_x:origin_x - default_link_padding],
           [self mindmap_y2canvas_y:origin_y + layout_height_points / 2.0]);
        }

        linkLayerVertical.path = pathVertical;
        CGPathRelease(pathVertical);
        linkLayerVertical.strokeColor = NSColor.blackColor.CGColor;
        linkLayerVertical.fillColor = nil;   
        linkLayerVertical.lineWidth = 0.8;
        linkLayerVertical.allowsEdgeAntialiasing = NO;
        [worldLayer addSublayer:linkLayerVertical];
        linkLayerHorizontal.path = pathHorizontal;
        CGPathRelease(pathHorizontal);
        linkLayerHorizontal.strokeColor = NSColor.blackColor.CGColor;
        linkLayerHorizontal.fillColor = nil;   
        linkLayerHorizontal.lineWidth = 0.8;
        [worldLayer addSublayer:linkLayerHorizontal];
    }

    CATextLayer *textLayer = [CATextLayer layer];
    textLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
    textLayer.fontSize = default_text_points;
    textLayer.font = CFBridgingRetain(default_font());
    textLayer.alignmentMode = kCAAlignmentLeft;
    textLayer.backgroundColor = [NSColor whiteColor].CGColor;
    textLayer.foregroundColor = [NSColor blackColor].CGColor;
    // textLayer.backgroundColor = randomVividColor();
    // textLayer.cornerRadius = 8;
    if(tree_node_collapsed(node)){
      const char *cstr = tree_node_text(node);
      NSString *text = [NSString stringWithUTF8String:cstr];
      textLayer.string = [NSString stringWithFormat:@"%@ (%llu)", text, tree_node_descendents(node)];
    }else{
        const char *cstr = tree_node_text(node);
        NSString *text = [NSString stringWithUTF8String:cstr];
        textLayer.string = text;
    }
    // logd("Rendering node '%s' at (%.2f, %.2f) with layout height %.2f points", tree_node_text(node), origin_x, origin_y, layout_height_points);
    CGSize textSize = measure_text(textLayer.string);
    double frame_y = [self mindmap_y2canvas_y:origin_y] - layout_height_points / 2.0 - default_base_points / 2.0;
    bool moved_to_bottom_when_current_ancestor_is_hidden = false;
    bool moved_to_top_when_current_ancestor_is_hidden = false;
    // if(is_tree_node_ancestor(state->tree_overlay, node, state->current_node)){
        if(frame_y < 0){
            moved_to_bottom_when_current_ancestor_is_hidden = true;
            frame_y = 0;
            if(origin_y + default_base_points > self.viewOrigon.y + view_h){
                frame_y = ( self.viewOrigon.y + view_h) - (origin_y + default_base_points );
            }
        }
        if(frame_y + default_base_points > self.mindmapDocLayer.bounds.size.height){
            moved_to_top_when_current_ancestor_is_hidden = true;
            frame_y = self.mindmapDocLayer.bounds.size.height - default_base_points;
            if(origin_y + layout_height_points - default_base_points < self.viewOrigon.y){
                frame_y = self.mindmapDocLayer.bounds.size.height - default_base_points + (
                    self.viewOrigon.y - (origin_y + layout_height_points - default_base_points)
                );
            }
        }
    // }
    textLayer.frame = CGRectMake(
        [self mindmap_x2canvas_x:origin_x],
        frame_y,
        textSize.width > 2 ? textSize.width : 2, default_base_points);
    // collect visible node for L-key viewport navigation
    // Convert frame_y (canvas/Cocoa coords, Y↑) to doc coords (Y↓):
    //   doc_center = view_h - (frame_y + base_points/2) + viewOrigon.y
    // Bottommost visible = max doc_center.
    static const double VIEWPORT_PADDING = 2.0; // exclude nodes too close to viewport edge
    if (frame_y >= VIEWPORT_PADDING && frame_y + default_base_points <= view_h - VIEWPORT_PADDING) {
        double doc_y = self.mindmapDocLayer.bounds.size.height - (frame_y + default_base_points / 2.0) + self.viewOrigon.y;
        TreeNode p = tree_node_parent(state->tree_overlay, node);
        [self.visibleNodeInfos addObject:@{
            @"id": @(tree_node_id(node)),
            @"parent": @(tree_node_id(p)),
            @"y": @(doc_y)
        }];
    }
    if(app_state->input_state->mark_and_show_visible_nodes 
      && textLayer.frame.origin.y >= 0 
      && textLayer.frame.origin.y + textLayer.frame.size.height <= self.mindmapDocLayer.bounds.size.height
      && textLayer.frame.origin.x - default_text_points>= 0
    ){
        app_state->mark_id++;
        CATextLayer *markLayer = [CATextLayer layer];
        markLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;// without this the text will be blurry
        char tag1 = '\0', tag2 = '\0';
        if(app_state->tag_mouse_mode){
            ui_tag_index_to_tag_left_hand(app_state->mark_id, &tag1, &tag2);
        }else{
            ui_tag_index_to_tag(app_state->mark_id, &tag1, &tag2);
        }
        markLayer.string = [NSString stringWithFormat:@"%c%c", tag1, tag2];
        NSSize markTextSize = measure_text(markLayer.string);
        markLayer.fontSize = default_text_points;
        markLayer.font = default_font_bold();
        
        markLayer.foregroundColor = [NSColor whiteColor].CGColor;
        markLayer.frame = CGRectMake(
            textLayer.frame.origin.x - markTextSize.width - 2,
            textLayer.frame.origin.y, markTextSize.width + 2, markTextSize.height);
        markLayer.alignmentMode = kCAAlignmentCenter;
        markLayer.foregroundColor = [NSColor colorWithCalibratedRed:0.1 green:0.5 blue:0.2 alpha:1].CGColor;
        markLayer.backgroundColor = [NSColor whiteColor].CGColor;
        markLayer.borderColor = [NSColor colorWithCalibratedRed:0.1 green:0.5 blue:0.2 alpha:1].CGColor;
        markLayer.borderWidth = 1.0;
        markLayer.cornerRadius = 2.0;
        [worldLayer addSublayer:markLayer];
        app_state->node_marks[app_state->mark_id] = tree_node_id(node);
    }
    if(is_current) {
        self.currentNodeFrame = textLayer.frame;
    }
    if(self.hitTesting) {
        if(CGRectContainsPoint(textLayer.frame, self.p_in_doc_view)) {
            logd("Hit node '%s'", tree_node_text(node));
            self.hitNode = node;
            if(is_current){
                self.hitCurrent = YES;
            }
        }
    }
    textLayer.truncationMode = kCATruncationNone;
    if(is_current){
        textLayer.filters = @[ ({
        CIFilter *f = [CIFilter filterWithName:@"CIColorInvert"];
        [f setDefaults];
        f;
        })];
    }
    // Remove old ancestor collection
    [worldLayer addSublayer:textLayer];

    if(tree_node_collapsed(node)) {
        return containsCurrent;
    }


    TreeNode child = tree_node_first_child(state->tree_overlay, node);
    if(!tree_node_is_null(child) ){
        CAShapeLayer *linkLayerHorizontal = [CAShapeLayer layer];
        linkLayerHorizontal.strokeColor = [NSColor blackColor].CGColor;
        CGMutablePathRef pathHorizontal = CGPathCreateMutable();
        CGPathMoveToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x + textSize.width + default_link_padding],
              textLayer.frame.origin.y + default_base_points / 2.0
              );
        double next_y = 0;
        bool only_child = tree_node_is_null(tree_node_next_sibling(state->tree_overlay, child));
        if(moved_to_bottom_when_current_ancestor_is_hidden && !only_child){
            next_y = textLayer.frame.origin.y ;
        }else if(moved_to_top_when_current_ancestor_is_hidden && !only_child){
            next_y = textLayer.frame.origin.y + default_base_points ;
        }else{
            next_y = textLayer.frame.origin.y + default_base_points / 2.0;
        }
        CGPathAddLineToPoint( pathHorizontal, NULL,
              [self mindmap_x2canvas_x:origin_x + textSize.width  + default_link_points / 2.0],
                next_y
              );
        linkLayerHorizontal.path = pathHorizontal;
        CGPathRelease(pathHorizontal);
        linkLayerHorizontal.strokeColor = NSColor.blackColor.CGColor;
        linkLayerHorizontal.fillColor = nil;   
        linkLayerHorizontal.lineWidth = 1.0;
        [worldLayer addSublayer:linkLayerHorizontal];
    }


    int y = 0;
    while (!tree_node_is_null(child)) {
        int child_layout_height = mind_node_height(state->tree_overlay, child);
        double child_layout_height_points = child_layout_height * default_base_points;
        if([self mindmap_render_node:state node:child worldLayer:worldLayer 
            originX:origin_x + textSize.width + default_link_points 
            originY:origin_y + y parentY:origin_y+layout_height_points/2.0]){
            containsCurrent = YES;
        }
        y += child_layout_height_points;
        child = tree_node_next_sibling(state->tree_overlay, child);
    }

    // Ancestor tracking: if current node is in our subtree but we are not it, we are an ancestor
    if(app_state->show_ancestors && containsCurrent && !is_current){
        [self.ancestorLayers addObject:textLayer];
    }

    return containsCurrent;
}

- (void)canvas_view_get_name{
    NSTextView *textView = self.bottomCommandTextView;
    textView.textContainerInset = NSMakeSize(0, 0);
    textView.textContainer.lineFragmentPadding = 0;
    textView.drawsBackground = YES;
    textView.backgroundColor = [NSColor whiteColor];
    textView.textColor = [NSColor blackColor];
    textView.font = default_font();
    NSString *nameStr = app_state->node_text ? [NSString stringWithUTF8String:app_state->node_text] : nil;
    textView.string = nameStr ?: @"";

    // place cursor 
    switch(app_state->input_state->type){
        case INPUT_STATE_TYPE_GET_NAME:{
            NSString *str = textView.string;
            [textView setSelectedRange:NSMakeRange(str.length, 0)];
            [textView scrollRangeToVisible:NSMakeRange(str.length, 0)];
            textView.frame = NSMakeRect(self.currentNodeFrame.origin.x, self.currentNodeFrame.origin.y, 
                default_text_points / 2.0, textView.frame.size.height);
            break;
        }
        case INPUT_STATE_TYPE_GET_NAME_INSERT_END:{
            NSString *str = textView.string;
            [textView setSelectedRange:NSMakeRange(str.length, 0)];
            [textView scrollRangeToVisible:NSMakeRange(str.length, 0)];
            textView.frame = self.currentNodeFrame;
            break;
        }
        case INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT:
            [textView setSelectedRange:NSMakeRange(0, 0)];
            [textView scrollRangeToVisible:NSMakeRange(0, 0)];
            textView.frame = self.currentNodeFrame;
            break;
        default:
            break;
    }


    textView.hidden = NO;
    [self layout];
    [self.window makeFirstResponder:textView];
}

- (void)updateWindowBreadcrumbTitle {
    if (app_state == NULL || tree_node_is_null(app_state->current_node)) {
        return;
    }
    if (!app_state->show_breadcrumb_title || [self breadsCrumbHeight] <= 0) {
        [self.breadsCrumbLayer removeFromSuperlayer];
        self.breadsCrumbLayer = nil;
        return;
    }

    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    uint64_t current_id = tree_node_id(app_state->current_node);
    TreeNode node = app_state->current_node;
    TreeOverlay *ov = app_state->tree_overlay;
    while (!tree_node_is_null(node)) {
        const char *cstr = tree_node_text(node);
        if (tree_node_id(node) == current_id || app_is_topic_node(node)) {
            NSString *name = [NSString stringWithUTF8String:cstr];
            [parts insertObject:name atIndex:0];
        }
        node = tree_node_parent(ov, node);
    }

    NSString *breadcrumb = [parts componentsJoinedByString:@" ▶ "];
    [self.breadsCrumbLayer removeFromSuperlayer];
    CATextLayer *newLayer = [CATextLayer layer];
    newLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
    newLayer.fontSize = default_text_points;
    newLayer.font = default_font();
    newLayer.alignmentMode = kCAAlignmentLeft;
    newLayer.foregroundColor = [NSColor blackColor].CGColor;
    newLayer.backgroundColor = [NSColor whiteColor].CGColor;
    newLayer.truncationMode = kCATruncationEnd;
    newLayer.wrapped = NO;
    newLayer.frame = NSMakeRect(0,
        NSHeight(self.bounds) - [self breadsCrumbHeight],
        NSWidth(self.bounds),
        [self breadsCrumbHeight]);
    newLayer.string = breadcrumb.length > 0 ? breadcrumb : @"";
    [self.layer addSublayer:newLayer];
    self.breadsCrumbLayer = newLayer;
}

- (void)render_mindmap {
    [self applyDocAndBreadcrumbLayerFrames];
    [self updateWindowBreadcrumbTitle];
    if(app_state->running == 0) {
        [NSApp terminate:nil];
    }
    if(app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_END){
        [self canvas_view_get_name];
        return;
    }
    TreeNode n = app_state->current_node;
    TreeOverlay *ov = app_state->tree_overlay;
    while(!tree_node_is_null(n)){
        TreeNode parent = tree_node_parent(ov, n);
        if(tree_node_is_null(parent)){
            break;
        }
        if(tree_node_is_collapsed(parent)){
            tree_node_set_collapse(ov, &parent, false);
        }
        n = parent;
    }
    // clear existing layers
    self.mindmapDocLayer.sublayers = nil;
    // compute current node bounds
    double origin_x = 0;
    int origin_y_int = 0;
    int default_link_points = default_text_points * 3 / 2;
    mind_node_layout_origin(app_state->tree_overlay, app_state->current_node, &origin_x, &origin_y_int, 
        text_field_display_width, default_link_points
    );
    int layout_height = mind_node_height(app_state->tree_overlay, app_state->current_node);
    double origin_y = (origin_y_int-0) * default_base_points;
    double layout_height_points = layout_height * default_base_points;
    double y_end = origin_y + layout_height_points;

    if(!dont_adjust_doc_view_by_current){
        // move doc view to cover current node (existing behavior)
        double actual_view_height = view_h - 2 * default_base_points;

        if (y_end < self.viewOrigon.y + default_base_points) {
            // fully above
            if(layout_height_points < actual_view_height){
                // fit in actual view
                self.viewOrigon = CGPointMake(self.viewOrigon.x, origin_y - default_base_points);
            }else{
                self.viewOrigon = CGPointMake(self.viewOrigon.x, y_end - actual_view_height);
            }
        } else if (origin_y > self.viewOrigon.y + view_h - default_base_points) {
            // fully bellow
            if(layout_height_points < actual_view_height){
                self.viewOrigon = CGPointMake(self.viewOrigon.x, y_end - view_h + default_base_points);
            }else{
                self.viewOrigon = CGPointMake(self.viewOrigon.x, origin_y - default_base_points);
            }
        }else if(origin_y < self.viewOrigon.y + default_base_points && y_end > self.viewOrigon.y + view_h - default_base_points) {
            // node taller than view: don't nudge
        } else if(origin_y < self.viewOrigon.y + default_base_points && y_end < self.viewOrigon.y + view_h - default_base_points) {
            // partial above
            if(layout_height_points < actual_view_height){
                self.viewOrigon = CGPointMake(self.viewOrigon.x, origin_y - default_base_points);
            }else{
                self.viewOrigon = CGPointMake(self.viewOrigon.x, y_end - actual_view_height);
            }
        } else if(y_end > self.viewOrigon.y + view_h - default_base_points && origin_y > self.viewOrigon.y + default_base_points) {
            // partial bellow
            if(layout_height_points < actual_view_height){
                self.viewOrigon = CGPointMake(self.viewOrigon.x, y_end - view_h + default_base_points);
            }else{
                self.viewOrigon = CGPointMake(self.viewOrigon.x, origin_y - default_base_points);
            }
        }

        // adjust viewOrigon x
        if(origin_x < self.viewOrigon.x + default_base_points){
            self.viewOrigon = CGPointMake(origin_x - default_base_points, self.viewOrigon.y);
        }
        double x_end = origin_x + text_field_display_width(app_state->current_node);
        if(x_end > self.viewOrigon.x + view_w - default_base_points){
            self.viewOrigon = CGPointMake(x_end - view_w + default_base_points, self.viewOrigon.y);
        }

    }


    // render
    self.visibleNodeInfos = [NSMutableArray array];
    if(app_state->show_ancestors){
        self.ancestorLayers = [NSMutableArray array];
    }
    if(app_state->input_state->mark_and_show_visible_nodes){
        app_state->mark_id = -1;
    }
    (void)[self mindmap_render_node:(AppState *) app_state node:app_state->tree_overlay->root worldLayer:self.mindmapDocLayer originX:0 originY:0 parentY:0];
    // flush
    [CATransaction flush];

    [self updateInfoView];
    
    // Build ancestor breadcrumb row when showAncestors is on
    if(app_state->show_ancestors && self.ancestorLayers.count > 0
       && !tree_node_is_null(app_state->current_node)){
        // Copy ancestor layers to current node's Y, preserving their original X positions;
        // connect copies with → arrows
        CALayer *prevCopy = nil;
        for(CALayer *aLayer in self.ancestorLayers){
            CATextLayer *aText = (CATextLayer *)aLayer;

            // Copy of ancestor text layer at original X but current node's Y
            CATextLayer *copy = [CATextLayer layer];
            copy.contentsScale = aText.contentsScale;
            copy.fontSize = aText.fontSize;
            copy.font = aText.font;
            copy.alignmentMode = aText.alignmentMode;
            copy.foregroundColor = aText.foregroundColor;
            copy.backgroundColor = aText.backgroundColor;
            // Preserve font/color for ancestor breadcrumb copy.
            NSDictionary *attrs = @{
                NSFontAttributeName: (__bridge NSFont *)aText.font,
                NSForegroundColorAttributeName: [NSColor colorWithCGColor:aText.foregroundColor],
            };
            copy.string = [[NSAttributedString alloc] initWithString:(NSString *)aText.string
                attributes:attrs];
            copy.frame = CGRectMake(aText.frame.origin.x, 
                self.currentNodeFrame.origin.y,
                aText.frame.size.width, default_base_points);
            copy.borderColor = [NSColor colorWithCalibratedWhite:0.2 alpha:0.9].CGColor;
            copy.borderWidth = 1.0;
            copy.cornerRadius = 2.0;
            copy.masksToBounds = YES;
            [self.mindmapDocLayer addSublayer:copy];

            prevCopy = copy;
        }
    }
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (void)layout {
    [super layout];
    [self applyDocAndBreadcrumbLayerFrames];
    [self updateWindowBreadcrumbTitle];
    logd("Layout: view size (%.2f, %.2f), crumbH(%.2f), viewOrigon (%.2f, %.2f)",
         view_w, view_h, [self breadsCrumbHeight], self.viewOrigon.x, self.viewOrigon.y);
    // Re-render after frame change (zoom, resize, etc.)
    // Only when NOT in text-editing states to avoid recursive layout→render_mindmap→canvas_view_get_name→layout
    if(app_state->input_state->type == INPUT_STATE_DEFAULT
    || app_state->input_state->type == INPUT_STATE_TYPE_COMPUTED_INPUT){
        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];
    }
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];

    if (self.window != nil) {
        self.window.acceptsMouseMovedEvents = YES;
        [self updateLayerScales];
        [self.window makeFirstResponder:self];

        // Get native traffic light buttons and hide until hover
        self.closeButton = [self.window standardWindowButton:NSWindowCloseButton];
        self.minimizeButton = [self.window standardWindowButton:NSWindowMiniaturizeButton];
        self.zoomButton = [self.window standardWindowButton:NSWindowZoomButton];
        // Delay initial hide to ensure buttons are created (lazy on macOS 11+)
        dispatch_async(dispatch_get_main_queue(), ^{
            self.closeButton.alphaValue = 0;
            self.minimizeButton.alphaValue = 0;
            self.zoomButton.alphaValue = 0;
        });
    } else {
    }
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self updateLayerScales];
}

/// Compute titlebar zone height from the actual traffic light button position.
/// More accurate than hardcoded constants; adapts to different macOS versions and window styles.
- (CGFloat)titlebarZoneHeight {
    logd("titlebarZoneHeight begin: viewBounds=(x=%.1f,y=%.1f,w=%.1f,h=%.1f) window=%p closeButton=%p",
         self.bounds.origin.x, self.bounds.origin.y,
         self.bounds.size.width, self.bounds.size.height,
         self.window, self.closeButton);

    if (self.window == nil) {
        logd("titlebarZoneHeight path=contentLayout only: window is NULL, return 0");
        return 0;
    }

    NSRect contentLayoutInWindow = self.window.contentLayoutRect;
    NSRect contentLayoutInView = [self convertRect:self.window.contentLayoutRect fromView:nil];
    CGFloat topInset = NSHeight(self.bounds) - NSMaxY(contentLayoutInView);
    CGFloat result = ceil(MAX(0.0, topInset));
    logd("titlebarZoneHeight path=contentLayout rawWindowRect=(x=%.1f,y=%.1f,w=%.1f,h=%.1f) convertedViewRect=(x=%.1f,y=%.1f,w=%.1f,h=%.1f) calc: topInset=viewH(%.1f)-maxY(%.1f)=%.3f result=ceil(max(0,%.3f))=%.3f",
         contentLayoutInWindow.origin.x, contentLayoutInWindow.origin.y,
         contentLayoutInWindow.size.width, contentLayoutInWindow.size.height,
         contentLayoutInView.origin.x, contentLayoutInView.origin.y,
         contentLayoutInView.size.width, contentLayoutInView.size.height,
         NSHeight(self.bounds), NSMaxY(contentLayoutInView), topInset, topInset, result);
    return result;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];

    if (self.titlebarTrackingArea != nil) {
        [self removeTrackingArea:self.titlebarTrackingArea];
    }
    CGFloat titlebarHeight = [self titlebarZoneHeight];
    CGFloat zoneY0 = NSHeight(self.bounds) - titlebarHeight;
    self.titlebarTrackingArea = [[NSTrackingArea alloc] 
        initWithRect:NSMakeRect(0, zoneY0, NSWidth(self.bounds), titlebarHeight)
        options:NSTrackingMouseEnteredAndExited |
                NSTrackingActiveAlways
        owner:self
        userInfo:nil];
    [self addTrackingArea:self.titlebarTrackingArea];
    logd("titlebar tracking area set to (0, %.2f, %.2f, %.2f)", self.titlebarTrackingArea.rect.origin.y, self.titlebarTrackingArea.rect.size.width, self.titlebarTrackingArea.rect.size.height);
}

- (void)mouseEntered:(NSEvent *)event {
    logd("mouseEntered: trackingArea=%p titlebar=%p", event.trackingArea, self.titlebarTrackingArea);
    if (event.trackingArea == self.titlebarTrackingArea && self.closeButton) {
        logd("  -> show titlebar buttons (viewY=%.1f zone=[%.0f-%.0f])",
             [self convertPoint:event.locationInWindow fromView:nil].y,
             self.titlebarTrackingArea.rect.origin.y,
             self.titlebarTrackingArea.rect.origin.y + self.titlebarTrackingArea.rect.size.height);
        self.closeButton.alphaValue = 1;
        self.minimizeButton.alphaValue = 1;
        self.zoomButton.alphaValue = 1;
    }
}

- (void)mouseExited:(NSEvent *)event {
    logd("mouseExited: trackingArea=%p titlebar=%p", event.trackingArea, self.titlebarTrackingArea);
    if (event.trackingArea == self.titlebarTrackingArea && self.closeButton) {
        logd("  -> hide titlebar buttons");
        self.closeButton.alphaValue = 0;
        self.minimizeButton.alphaValue = 0;
        self.zoomButton.alphaValue = 0;
    }
}

- (void)updateLayerScales {
    CGFloat scale = self.window.screen.backingScaleFactor;
    if (scale <= 0) {
        scale = NSScreen.mainScreen.backingScaleFactor;
    }

    // self.fpsLayer.contentsScale = scale;
}

- (void)performWithoutImplicitAnimation:(void (^)(void))updates {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    updates();
    [CATransaction commit];
}


#pragma mark - Mouse

- (CGPoint)view2docview:(CGPoint)viewPoint {
    return CGPointMake(
        viewPoint.x, 
        CGRectGetHeight(self.bounds) - viewPoint.y
    );
}
- (void)updateInfoView {
    if(debuging == 0) {
        self.infoLayer.string = @"";
        return;
    }
    NSPoint screenPoint = [NSEvent mouseLocation];
    NSPoint locationInWindow = [self.window convertPointFromScreen:screenPoint];
    NSPoint locationInView = [self convertPoint:locationInWindow fromView:nil];
    NSPoint locationInLayer = [self.layer convertPoint:locationInView toLayer:self.mindmapDocLayer];
    NSPoint locationInWorldLayer = [self.mindmapDocLayer convertPoint:locationInLayer toLayer:self.mindmapDocLayer];
    //
    NSPoint locationInDocView = [self view2docview:locationInWorldLayer];
    NSPoint locationInDocWorld = CGPointMake(
        locationInDocView.x + self.viewOrigon.x, 
    locationInDocView.y + self.viewOrigon.y);
    CGFloat th = [self titlebarZoneHeight];
    self.infoLayer.string = [NSString stringWithFormat:@"window(%.2f, %.2f)|view(%.2f, %.2f)|layer(%.2f, %.2f)|worldLayer(%.2f, %.2f)\n"
    "docview(%.2f, %.2f)|docworld(%.2f, %.2f)\n"
    "worldLayer pos(%.2f, %.2f) zoomScale: %.2f anchor(%.2f, %.2f)\n"
    "viewOrigon(%.2f, %.2f) viewH(%.2f) viewW(%.2f)\n"
    "#layers %d; canvas frame(%.2f, %.2f, %.2f, %.2f)\n"
    "currentNode: %s|Frame(%.2f, %.2f, %.2f, %.2f)\n"
    "titlebar: zoneY=[%.0f-%.0f] mouseViewY=%.2f %s a=%.2f\n",

    locationInWindow.x, locationInWindow.y,
     locationInView.x, locationInView.y, 
     locationInLayer.x, locationInLayer.y,
    locationInWorldLayer.x, locationInWorldLayer.y,
     locationInDocView.x, locationInDocView.y,
     locationInDocWorld.x, locationInDocWorld.y,
     self.mindmapDocLayer.position.x, self.mindmapDocLayer.position.y, self.zoomScale,
     self.mindmapDocLayer.anchorPoint.x, self.mindmapDocLayer.anchorPoint.y,
     self.viewOrigon.x, self.viewOrigon.y, view_h, view_w,
        (int)self.mindmapDocLayer.sublayers.count,
    self.mindmapDocLayer.frame.origin.x, self.mindmapDocLayer.frame.origin.y,
    self.mindmapDocLayer.frame.size.width, self.mindmapDocLayer.frame.size.height,
    tree_node_text(app_state->current_node),
    self.currentNodeFrame.origin.x, self.currentNodeFrame.origin.y,
    self.currentNodeFrame.size.width, self.currentNodeFrame.size.height,
    NSHeight(self.bounds) - th, NSHeight(self.bounds),
    locationInView.y,
    (locationInView.y >= self.bounds.size.height - th) ? "IN" : "OUT",
    self.closeButton ? self.closeButton.alphaValue : -1
     ];
    // Auto-fit infoLayer height to content
    NSInteger lineCount = [self.infoLayer.string length] - [[self.infoLayer.string stringByReplacingOccurrencesOfString:@"\n" withString:@""] length];
    self.infoLayer.frame = CGRectMake(0, 12, self.bounds.size.width, (lineCount + 3) * self.infoLayer.fontSize);
}

- (void)mouseMoved:(NSEvent *)event {
    [self updateInfoView];
}

- (void)mouseDown:(NSEvent *)event {
    log_debug("mouseDown: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    [self hideInfoMessage];
    is_hand_on_mouse = true;
    self.lastMouse = [self convertPoint:event.locationInWindow fromView:nil];
    self.lastWorldPosition = self.viewOrigon;
    [self updateInfoView];
    self.isDragging = NO;
}
- (void)mouseDraged:(NSEvent *)event {
    self.isDragging = YES;
}
// - (void)mouseUp:(NSEvent *)event {
// }
- (void)rightMouseDown:(NSEvent *)event {
    log_debug("rightMouseDown: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    [self hideInfoMessage];
    is_hand_on_mouse = true;
    self.lastMouse = [self convertPoint:event.locationInWindow fromView:nil];
    self.lastWorldPosition = self.viewOrigon;
    [self updateInfoView];
}

- (void)rightMouseDragged:(NSEvent *)event {
    log_debug("mouseDragged: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    dont_adjust_doc_view_by_current = true;
    is_hand_on_mouse = true;
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];

    CGFloat dx = p.x - self.lastMouse.x;
    CGFloat dy = p.y - self.lastMouse.y;

    CGPoint worldPosition = self.viewOrigon;
    worldPosition.x = self.lastWorldPosition.x - dx;
    worldPosition.y = self.lastWorldPosition.y + dy;


    self.viewOrigon = worldPosition;
    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
    // [CATransaction flush];

    self.latestMouseViewPoint = p;
}

// Mouse navigation buttons: back (button 3) = ^O, forward (button 4) = ^I
- (void)otherMouseDown:(NSEvent *)event {
    [self hideInfoMessage];
    is_hand_on_mouse = true;
    if (event.buttonNumber == 3 || event.buttonNumber == 4) {
        app_state->input_state->type = INPUT_STATE_DEFAULT;
        char key = (event.buttonNumber == 3) ? 'o' : 'i';
        UserOperation uo = input_convert(app_state->input_state, key, 0, NULL, YES);
        if (uo.type != UO_NOP) {
            app_apply_event(app_state, uo);
        }
        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];
    }
}

- (void)scrollWheel:(NSEvent *)event {
    [self hideInfoMessage];
    dont_adjust_doc_view_by_current = true;
    is_hand_on_mouse = true;

    CGFloat dx = event.scrollingDeltaX;
    CGFloat dy = event.scrollingDeltaY;

    if (event.hasPreciseScrollingDeltas) {
        // for trackpad, the delta is already in points, we can use it directly
        // dx / dy 
    } else {
        // for mouse
        dx *= 10;
        dy *= 10;
    }

    CGPoint world = self.viewOrigon;
    world.x -= dx ;
    world.y -= dy ;

    self.viewOrigon = world;

    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
}

- (void)mouseUp:(NSEvent *)event {
    log_debug("mouseUp: %p, x: %.2f, y: %.2f, clickCount: %ld", event, event.locationInWindow.x, event.locationInWindow.y, (long)event.clickCount);
    [self hideInfoMessage];
    is_hand_on_mouse = true;

    // Double-click on titlebar zone → toggle zoom (maximize / restore)
    // fullSizeContentView 下系统不自动处理，需手动路由
    if (event.clickCount == 2) {
        NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
        CGFloat th = [self titlebarZoneHeight];
        if (loc.y >= NSHeight(self.bounds) - th) {
            [self.window zoom:nil];
            // After zoom/restore, reset traffic light buttons to hidden
            // (they were shown by mouseEntered before the double-click)
            // and update tracking areas to match the new frame.
            self.closeButton.alphaValue = 0;
            self.minimizeButton.alphaValue = 0;
            self.zoomButton.alphaValue = 0;
            [self updateTrackingAreas];
            [self layout];
            [self performWithoutImplicitAnimation:^{
                [self render_mindmap];
            }];
            return;
        }
    }

    if(self.isDragging) {
        self.isDragging = NO;// drag end
    }else{
        // handle click

        self.hitTesting = YES;
        self.hitNode = (TreeNode){.kind = TREE_NODE_NULL};
        self.hitCurrent = NO;
        self.latestMouseViewPoint = [self convertPoint:event.locationInWindow fromView:nil];
        self.p_in_doc_view = [self.layer convertPoint:self.latestMouseViewPoint
                                        toLayer:self.mindmapDocLayer];

        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];

        self.hitTesting = NO;

        if(!tree_node_is_null(self.hitNode)) {
            // Cmd+click: jump to definition, fallback to open resource link
            if (event.modifierFlags & NSEventModifierFlagCommand) {
                app_state->input_state->type = INPUT_STATE_DEFAULT;
                dont_adjust_doc_view_by_current = false;
                update_current_with_history(app_state, self.hitNode);

                // Try gd first (jump to keyword definition)
                TreeNode before = app_state->current_node;
                app_apply_event(app_state, (UserOperation){.type = UO_JUMP_KEYWORD_DEFINITION});
                TreeNode after = app_state->current_node;

                // If gd didn't move, fallback to gD (open resource link)
                if (tree_node_id(before) == tree_node_id(after)) {
                    app_apply_event(app_state, (UserOperation){.type = UO_OPEN_RESOURCE_LINK});
                }

                [self performWithoutImplicitAnimation:^{
                    [self render_mindmap];
                }];
                return;
            }

            if(self.hitCurrent) {
                if(tree_node_collapsed(self.hitNode)){
                    app_state->input_state->type = INPUT_STATE_DEFAULT;
                    UserOperation uo = { .type = UO_UNFOLD_NODE, };
                    app_apply_event(app_state, uo);
                }else{
                    app_state->input_state->type = INPUT_STATE_DEFAULT;
                    UserOperation uo = { .type = UO_FOLD_NODE, };
                    app_apply_event(app_state, uo);
                }
            }

            dont_adjust_doc_view_by_current = false;
            update_current_with_history(app_state, self.hitNode);
            [self performWithoutImplicitAnimation:^{
                [self render_mindmap];
            }];
        }
    }
}

- (void)snapWindowToRightHalf{
    NSWindow *window = self.window;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    CGFloat halfW = screenRect.size.width / 2.0;
    NSRect rightHalfRect = NSMakeRect(screenRect.origin.x + halfW, 
    screenRect.origin.y, halfW, screenRect.size.height);
    [window setFrame:rightHalfRect display:YES animate:NO];
    [self layout];
    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
}
- (void)snapWindowToLeftHalf{
    NSWindow *window = self.window;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    CGFloat halfW = screenRect.size.width / 2.0;
    NSRect leftHalfRect = NSMakeRect(screenRect.origin.x , 
    screenRect.origin.y, halfW, screenRect.size.height);
    [window setFrame:leftHalfRect display:YES animate:NO];
    [self layout];
    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
}
- (void)snapWindowToBottomHalf{
    NSWindow *window = self.window;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    CGFloat halfH = screenRect.size.height / 2.0;
    NSRect bottomHalfRect = NSMakeRect(screenRect.origin.x , 
    screenRect.origin.y, screenRect.size.width, halfH);
    [window setFrame:bottomHalfRect display:YES animate:NO];
    [self layout];
    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
}
-(void)snapWindowToTopHalf{
    NSWindow *window = self.window;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    CGFloat halfH = screenRect.size.height / 2.0;
    NSRect topHalfRect = NSMakeRect(screenRect.origin.x , 
    screenRect.origin.y + halfH, screenRect.size.width, halfH);
    [window setFrame:topHalfRect display:YES animate:NO];
    [self layout];
    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
}

- (void)keyDown:(NSEvent *)event {
    log_debug("keyDown: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    // Hide info message on any keyboard input; a new message may be shown during processing
    [self hideInfoMessage];
    NSString *characters = event.charactersIgnoringModifiers;
    unichar key = [characters characterAtIndex:0];
    BOOL isControlDown = (event.modifierFlags & NSEventModifierFlagControl) != 0;
    BOOL isFnDown = (event.modifierFlags & NSEventModifierFlagFunction) != 0;
    BOOL isCommandDown = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
    BOOL isOptionDown = (event.modifierFlags & NSEventModifierFlagOption) != 0;
    if(isFnDown && isControlDown ){
    }
    // Control + Command + arrows: window snapping (alias for Fn+Ctrl+arrows)
    if(isFnDown && isControlDown && isCommandDown && !isOptionDown){
        switch(event.keyCode){
            case 124:
                [self snapWindowToRightHalf];
                [self layout];
                return;
            case 123:
                [self snapWindowToLeftHalf];
                [self layout];
                return;
            case 126:
                [self snapWindowToTopHalf];
                [self layout];
                return;
            case 125:
                [self snapWindowToBottomHalf];
                [self layout];
                return;
            default:
                break;
        }
    }
    // Command + ...
    if(!isFnDown && !isControlDown && !isOptionDown && isCommandDown){
        CGPoint focusPoint = self.latestMouseViewPoint;
        if (CGPointEqualToPoint(focusPoint, CGPointZero)) {
            focusPoint = CGPointMake(CGRectGetMidX(self.bounds), CGRectGetMidY(self.bounds));
        }

        switch (key) {
            case 'c':
            {
                UserOperation uo = (UserOperation){.type = UO_COPY_SUBTREE_TO_SYSTEM_CLIPBOARD};
                app_apply_event(app_state, uo);
                return;
            }
            case 'w':
            {
                UserOperation uo = (UserOperation){.type = UO_EXIT_SAVE};
                app_apply_event(app_state, uo);
                [NSApp terminate:nil];
                return;
            }
            case '-':{
                int origin_text_points = default_text_points;
                default_text_points -= 1;
                if(default_text_points < 1) default_text_points = 1;
                double scale_x = default_text_points / (double)origin_text_points;
                int origin_base_points = default_base_points;
                default_base_points = (int)(default_text_points / 45.0 * 53.0);
                double scale_y = default_base_points / (double)origin_base_points;
                self.viewOrigon = CGPointMake(
                    self.viewOrigon.x * scale_x,
                    self.viewOrigon.y * scale_y
                );
                [self performWithoutImplicitAnimation:^{
                    [self render_mindmap];
                }];
                break;
            }
            case '=':{
                int origion_text_points = default_text_points;
                default_text_points += 1;
                double scale_x = default_text_points / (double)origion_text_points;
                int origin_base_points = default_base_points;
                default_base_points = (int)(default_text_points / 45.0 * 53.0);
                double scale_y = default_base_points / (double)origin_base_points;
                self.viewOrigon = CGPointMake(
                    self.viewOrigon.x * scale_x,
                    self.viewOrigon.y * scale_y
                );
                [self performWithoutImplicitAnimation:^{
                    [self render_mindmap];
                }];
                break;
            }
            default:
                break;
        }
        return;
    }
    // Control + ...
    if(!isFnDown && isControlDown && !isOptionDown && !isCommandDown){
        switch(key){
            case 'd':
            case 'u':
            case 'f':
            case 'b':
            case 'e':
            case 'y':
            case 'g':
            case 'o':
            case 'i':
            {
                UserOperation uo = input_convert(app_state->input_state, key, event.keyCode, NULL, isControlDown);
                app_apply_event(app_state, uo);
                [self performWithoutImplicitAnimation:^{
                    [self render_mindmap];
                }];

                return;
            }
        }
    }
    // if Page Down / Page Up / Home / End 
    if(event.keyCode == 121 || event.keyCode == 116 || event.keyCode == 115 || event.keyCode == 119){
        UserOperation uo = input_convert(app_state->input_state, '\0', event.keyCode, NULL, NO);
        app_apply_event(app_state, uo);
        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];
        return;
    }
    if(event.keyCode == 117){
        UserOperation uo = input_convert(app_state->input_state, '\0', event.keyCode, NULL, NO);
        app_apply_event(app_state, uo);
        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];
        return;
    }

    if(app_state->input_state->type != INPUT_STATE_TYPE_JUMP_TO_VISIBLE_TAG) {
        dont_adjust_doc_view_by_current = false;
    }

    // F1: toggle between system cursor and custom-drawn cursor
    if (event.keyCode == 103 || [characters isEqualToString:[NSString stringWithCharacters:(const unichar[]){ NSF11FunctionKey } length:1]]) {
        [self.window toggleFullScreen:nil];
        [self layout];
        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];
        return;
    }
    // F2
    if(event.keyCode == 120){
        [super keyDown:event];
        return;
    }
    // F12: toggle debug info
    if (event.keyCode == 111 ){
        debuging = !debuging;
        self.infoLayer.hidden = !debuging;
        [self updateInfoView];
        return;
    }

    if (characters.length == 0) {
        [super keyDown:event];
        return;
    }
    switch (event.keyCode) {
    case 123: // ← // left
    case 124: // → // right
    case 125: // ↓ // down
    case 126: // ↑ // up
        {
                UserOperation uo = input_convert(app_state->input_state, '\0', event.keyCode, NULL, NO);
                app_apply_event(app_state, uo);
                [self performWithoutImplicitAnimation:^{
                    [self render_mindmap];
                }];
            return;
        }
    }

    switch (key) {
        default:
            
            if(('a' <= key && key <= 'z') || ('A' <= key && key <= 'Z') || ('0' <= key && key <= '9') || key == '.' || key == '\''
                || key == '/' || key == ':'
                || key == '?'
                || key == '[' || key == ']' 
                || key == '#' || key == '$' || key == '%' || key == '^' || key == '&' || key == '*' || key == '(' || key == ')'
                || key == '-' || key == '=' || key == ' '
                || key == '\t' || key == ';' || key == '\\'
                || key == '\n' || key == '\r'
                || key == 0x1b   // Escape — cancel prefix/search
                ){ 
                UserOperation uo = input_convert(app_state->input_state, key, 0, NULL, isControlDown);
                app_apply_event(app_state, uo);
                if(app_state->input_state->type != INPUT_STATE_PREFIX
                    && app_state->input_state->type != INPUT_STATE_TYPE_JUMP_TO_VISIBLE_TAG
                    && uo.type != UO_CANCEL_JUMP_TO_VISIBLE_TAG
                    && uo.type != UO_VIEW_HALF_SCREEN_LEFT 
                    && uo.type != UO_VIEW_HALF_SCREEN_RIGHT){
                    dont_adjust_doc_view_by_current = false;
                }
                if(app_state->input_state->type == INPUT_STATE_PREFIX){
                    self.bottomCommandTextView.string = [NSString stringWithFormat:@"Prefix: %c%.*s", 
                        app_state->input_state->prefix,
                        app_state->input_state->prefix_count,
                        app_state->input_state->key_buffer];

                    self.bottomCommandTextView.font = default_font();
                    self.bottomCommandTextView.drawsBackground = YES;
                    self.bottomCommandTextView.backgroundColor = [NSColor blackColor];
                    self.bottomCommandTextView.textColor = [NSColor whiteColor];
                    self.bottomCommandTextView.hidden = NO;
                    NSSize textSize = [self.bottomCommandTextView.string sizeWithAttributes:@{NSFontAttributeName: default_font()}];
                    self.bottomCommandTextView.frame = CGRectMake(0, 0, textSize.width + default_text_points, default_base_points);
                    [self layout];
                }else if(app_state->input_state->type == INPUT_STATE_TYPE_GET_COMMAND){
                    // Command mode: show text input with ":" prefix
                    self.bottomCommandTextView.font = default_font();
                    self.bottomCommandTextView.drawsBackground = YES;
                    self.bottomCommandTextView.backgroundColor = [NSColor blackColor];
                    self.bottomCommandTextView.textColor = [NSColor whiteColor];
                    self.bottomCommandTextView.string = @":";
                    self.bottomCommandTextView.hidden = NO;
                    NSSize cmdSize = [self.bottomCommandTextView.string sizeWithAttributes:@{NSFontAttributeName: default_font()}];
                    self.bottomCommandTextView.frame = CGRectMake(0, 0, cmdSize.width + default_text_points, default_base_points);
                    [self.window makeFirstResponder:self.bottomCommandTextView];
                }else if(app_state->input_state->type != INPUT_STATE_TYPE_SEARCH_QUERY
                    && app_state->input_state->type != INPUT_STATE_TYPE_SEARCH_BACKWARD_QUERY){
                    self.bottomCommandTextView.hidden = YES;
                }

                if(app_state->input_state->type != INPUT_STATE_PREFIX){// don't re-render on prefix input, wait for next key
                    [self performWithoutImplicitAnimation:^{
                        [self render_mindmap];
                    }];
                }
                return;
            }
    }
    [super keyDown:event];
    return;

    // self.lastFPSUpdateTime = 0;
}

@end