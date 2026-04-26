#include <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#include <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <unistd.h>
#import <libgen.h>
#include <getopt.h>

    
#include "../app/app.h"
#include "../layout/mindmap_layout.h"
#include "../ui/ui.h"
#include "../ui/input_state.h"
#include "../tree/tree_overlay.h"
#include "../utils/logging.h"

void logd(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

CGColorRef randomVividColor() {
    // Pick a vivid (high-saturation) background color
    CGFloat hue = (arc4random_uniform(1000) / 1000.0);
    CGFloat saturation = 0.92 + ((arc4random_uniform(80)) / 1000.0); // ~0.92-1.0
    CGFloat brightness = 0.78 + ((arc4random_uniform(170)) / 1000.0); // ~0.78-0.95
    NSColor *bgColor = [NSColor colorWithCalibratedHue:hue saturation:saturation brightness:brightness alpha:1.0];
    return bgColor.CGColor;
}

// double default_text_points = 45;
int default_text_points = 12;// 14
int default_base_points = 15; // 17
AppState* app_state = NULL;

NSFont *default_font() {
    return [NSFont monospacedSystemFontOfSize:default_text_points weight:NSFontWeightRegular];
}

CGSize measure_text(NSString *text) {
    NSFont *font = default_font();
    NSDictionary *attr = @{ NSFontAttributeName: font };
    CGSize measured = [text sizeWithAttributes:attr];
    return measured;
}

extern bool dont_adjust_doc_view_by_current;
double view_w = 0;
double view_h = 0;

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
@interface TextInputView : NSTextView
@property(nonatomic, copy) void (^onCancel)(void);
@end
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
@end

#pragma mark - Canvas View (Layer-based, no drawRect)
@interface CanvasView : NSView <NSTextViewDelegate>
@property(nonatomic,strong) CADisplayLink *displayLink;
@property(nonatomic,strong) CALayer *worldLayer;
// @property(nonatomic,strong) CATextLayer *fpsLayer;
@property(nonatomic,strong) CATextLayer *infoLayer;
@property(nonatomic,strong) NSMutableArray<Node *> *nodes;
@property(nonatomic) BOOL hitTesting;
@property(nonatomic) TreeNode hitNode;
@property(nonatomic) BOOL hitCurrent;
@property(nonatomic,strong) NSTrackingArea *trackingArea;
@property(nonatomic) BOOL isDragging;
@property(nonatomic) CGPoint lastMouse;
@property(nonatomic) CGPoint lastWorldPosition;
@property(nonatomic) CGPoint latestMouseViewPoint;
@property(nonatomic) CGPoint p_in_doc_view;
@property(nonatomic) CGPoint viewOrigon;
@property(nonatomic) CGFloat zoomScale;
@property(nonatomic) CGRect currentNodeFrame;
@property(nonatomic) TextInputView *buttomCommandTextView;
// @property(nonatomic) CFTimeInterval lastFPSUpdateTime;
@end

@implementation CanvasView

- (double)mindmap_x2canvas_x:(double)mindmap_x {
    return mindmap_x  - self.viewOrigon.x;
}
- (double)mindmap_y2canvas_y:(double)mindmap_y {
    return self.worldLayer.bounds.size.height - (mindmap_y  - self.viewOrigon.y );
}

- (BOOL)textView:(NSTextView *)textView
shouldChangeTextInRange:(NSRange)range
replacementString:(NSString *)string
{
    if(range.location < 1 && app_state->input_state->type != INPUT_STATE_TYPE_GET_NAME
    && app_state->input_state->type != INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
    && app_state->input_state->type != INPUT_STATE_TYPE_GET_NAME_INSERT_END){
        return NO;
    }
    // editing node text
    if(app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME 
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_FRONT
    || app_state->input_state->type == INPUT_STATE_TYPE_GET_NAME_INSERT_END){
        NSInteger oldLength = textView.string.length;
        NSInteger newLength = oldLength - range.length + string.length;
        // NSInteger originTextLenth = [NSString stringWithUTF8String:tree_node_text(app_state->current_node)].length;
        BOOL lengthAdded = (newLength > oldLength);
        NSString *old = textView.string;
        NSString *newString = [old stringByReplacingCharactersInRange:range
                                 withString:string];
        if(lengthAdded){
            textView.frame = NSMakeRect(textView.frame.origin.x, textView.frame.origin.y,
                         measure_text(newString).width, textView.frame.size.height);
        }
    }
    if ([string isEqualToString:@"\n"]) {
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

        _nodes = [NSMutableArray array];
        _buttomCommandTextView = [[TextInputView alloc] initWithFrame:CGRectMake(0, default_base_points, 
                self.bounds.size.width, default_base_points)];
        _buttomCommandTextView.font = default_font();
        _buttomCommandTextView.backgroundColor = [NSColor grayColor];
        _buttomCommandTextView.textColor = [NSColor blackColor];
        _buttomCommandTextView.string = @"Command --";
        _buttomCommandTextView.delegate = (self);
        _buttomCommandTextView.hidden = YES;
        _buttomCommandTextView.onCancel = ^{
          logd("callback: Text input cancelled");
          app_state->input_state->type = INPUT_STATE_DEFAULT;
          [self layout];
          [self.window makeFirstResponder:self];
          [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
          }];
        };
        [self addSubview:_buttomCommandTextView positioned:NSWindowAbove relativeTo:nil];

        _worldLayer = [CALayer layer];
        _worldLayer.anchorPoint = CGPointZero;
        // _worldLayer.backgroundColor = [NSColor lightGrayColor].CGColor;
        _worldLayer.frame = self.bounds;
        [self.layer addSublayer:_worldLayer];

        _infoLayer = [CATextLayer layer];
        _infoLayer.contentsScale = NSScreen.mainScreen.backingScaleFactor;
        _infoLayer.fontSize = 14;
        _infoLayer.alignmentMode = kCAAlignmentLeft;
        _infoLayer.foregroundColor = [NSColor colorWithCalibratedWhite:0.08 alpha:0.5].CGColor;
        _infoLayer.backgroundColor = [NSColor colorWithCalibratedWhite:1.0 alpha:0.20].CGColor;
        _infoLayer.string = @"Info --";
        _infoLayer.frame = CGRectMake(0, 12, self.bounds.size.width, 7*_infoLayer.fontSize);
        _infoLayer.hidden = !debuging;
        [self.layer addSublayer:_infoLayer];

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

- (void)mindmap_render_node:(AppState *)state node:(TreeNode)node worldLayer:(CALayer *)worldLayer
    originX:(double)origin_x originY:(double)origin_y parentY:(double)parent_y
{
    int default_link_padding = default_text_points / 4;
    int default_link_points = default_text_points * 3 / 2;
    if(default_link_points < default_link_padding * 2) default_link_points = default_link_padding * 2;
    uint64_t id = tree_node_id(node);
    bool is_current = tree_node_id(node) == tree_node_id(state->current_node);
    int layout_height = mind_node_height(state->tree_overlay, node);
    if(tree_node_collapsed(node))   {
        layout_height = 1;
    };
    double layout_height_points = layout_height * default_base_points;
    if(!intersect1D(origin_y, origin_y + layout_height_points, self.viewOrigon.y, self.viewOrigon.y + view_h)){
        return;
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
    textLayer.alignmentMode = kCAAlignmentCenter;
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
    bool moved_to_buttom_when_current_ancestor_is_hidden = false;
    bool moved_to_top_when_current_ancestor_is_hidden = false;
    // if(is_tree_node_ancestor(state->tree_overlay, node, state->current_node)){
        if(frame_y < 0){
            moved_to_buttom_when_current_ancestor_is_hidden = true;
            frame_y = 0;
            if(origin_y + default_base_points > self.viewOrigon.y + view_h){
                frame_y = ( self.viewOrigon.y + view_h) - (origin_y + default_base_points );
            }
        }
        if(frame_y + default_base_points > self.worldLayer.bounds.size.height){
            moved_to_top_when_current_ancestor_is_hidden = true;
            frame_y = self.worldLayer.bounds.size.height - default_base_points;
            if(origin_y + layout_height_points - default_base_points < self.viewOrigon.y){
                frame_y = self.worldLayer.bounds.size.height - default_base_points + (
                    self.viewOrigon.y - (origin_y + layout_height_points - default_base_points)
                );
            }
        }
    // }
    textLayer.frame = CGRectMake(
        [self mindmap_x2canvas_x:origin_x],
        frame_y,
        textSize.width > 2 ? textSize.width : 2, default_base_points);
    if(is_current) {
        self.currentNodeFrame = textLayer.frame;
        // self.currentNodeFrame = CGRectMake(
        //     origin_x,
        //     origin_y,
        //     textSize.width, 
        //     layout_height_points
        // );
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
    [worldLayer addSublayer:textLayer];

    if(tree_node_collapsed(node)) {
        return;
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
        if(moved_to_buttom_when_current_ancestor_is_hidden && !only_child){
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
        [self mindmap_render_node:state node:child worldLayer:worldLayer 
            originX:origin_x + textSize.width + default_link_points 
            originY:origin_y + y parentY:origin_y+layout_height_points/2.0];
        y += child_layout_height_points;
        child = tree_node_next_sibling(state->tree_overlay, child);
    }
}

- (void)canvas_view_get_name{
    NSTextView *textView = self.buttomCommandTextView;
    textView.textContainerInset = NSMakeSize(0, 0);
    textView.textContainer.lineFragmentPadding = 0;
    textView.backgroundColor = [NSColor whiteColor];
    textView.textColor = [NSColor blackColor];
    textView.font = default_font();
    textView.string = app_state->node_text != NULL ? [NSString stringWithUTF8String:app_state->node_text] : @"";

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

- (void)render_mindmap {
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
    self.worldLayer.sublayers = nil;
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
    [self mindmap_render_node:(AppState *) app_state node:app_state->tree_overlay->root worldLayer:self.worldLayer originX:0 originY:0 parentY:0];
    // flush
    [CATransaction flush];

    [self updateInfoView];
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (void)layout {
    [super layout];
    if(app_state->input_state->type == INPUT_STATE_TYPE_SEARCH_QUERY){
        view_w = CGRectGetWidth(self.bounds);
        view_h = CGRectGetHeight(self.bounds) - default_base_points;
        [self performWithoutImplicitAnimation:^{
            self.worldLayer.bounds = NSMakeRect(0, 0, view_w, view_h);
            self.worldLayer.frame = NSMakeRect(0, default_base_points, view_w, view_h);
        }];
        logd("Layout (search): view size (%.2f, %.2f), viewOrigon (%.2f, %.2f)", view_w, view_h, self.viewOrigon.x, self.viewOrigon.y);
    }else{
        view_w = CGRectGetWidth(self.bounds);
        view_h = CGRectGetHeight(self.bounds);
        [self performWithoutImplicitAnimation:^{
            self.worldLayer.bounds = self.bounds;
            self.worldLayer.frame = NSMakeRect(0, 0, view_w, view_h);
        }];
        logd("Layout: view size (%.2f, %.2f), viewOrigon (%.2f, %.2f)", view_w, view_h, self.viewOrigon.x, self.viewOrigon.y);
    }
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];

    if (self.window != nil) {
        self.window.acceptsMouseMovedEvents = YES;
        [self updateLayerScales];
        [self.window makeFirstResponder:self];
    } else {
    }
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self updateLayerScales];
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];

    if (self.trackingArea != nil) {
        [self removeTrackingArea:self.trackingArea];
    }

    self.trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                     options:NSTrackingMouseEnteredAndExited |
                                                             NSTrackingMouseMoved |
                                                             NSTrackingActiveInKeyWindow |
                                                             NSTrackingInVisibleRect
                                                       owner:self
                                                    userInfo:nil];
    [self addTrackingArea:self.trackingArea];
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
    NSPoint locationInLayer = [self.layer convertPoint:locationInView toLayer:self.worldLayer];
    NSPoint locationInWorldLayer = [self.worldLayer convertPoint:locationInLayer toLayer:self.worldLayer];
    //
    NSPoint locationInDocView = [self view2docview:locationInWorldLayer];
    NSPoint locationInDocWorld = CGPointMake(
        locationInDocView.x + self.viewOrigon.x, 
    locationInDocView.y + self.viewOrigon.y);
    self.infoLayer.string = [NSString stringWithFormat:@"window(%.2f, %.2f)|view(%.2f, %.2f)|layer(%.2f, %.2f)|worldLayer(%.2f, %.2f)\n"
    "docview(%.2f, %.2f)|docworld(%.2f, %.2f)\n"
    "worldLayer pos(%.2f, %.2f) zoomScale: %.2f anchor(%.2f, %.2f)\n"
    "viewOrigon(%.2f, %.2f) viewH(%.2f) viewW(%.2f)\n"
    "#layers %d; canvas frame(%.2f, %.2f, %.2f, %.2f)\n"
    "currentNode: %s|Frame(%.2f, %.2f, %.2f, %.2f)\n",

    locationInWindow.x, locationInWindow.y,
     locationInView.x, locationInView.y, 
     locationInLayer.x, locationInLayer.y,
    locationInWorldLayer.x, locationInWorldLayer.y,
     locationInDocView.x, locationInDocView.y,
     locationInDocWorld.x, locationInDocWorld.y,
     self.worldLayer.position.x, self.worldLayer.position.y, self.zoomScale,
     self.worldLayer.anchorPoint.x, self.worldLayer.anchorPoint.y,
     self.viewOrigon.x, self.viewOrigon.y, view_h, view_w,
        (int)self.worldLayer.sublayers.count,
    self.worldLayer.frame.origin.x, self.worldLayer.frame.origin.y,
    self.worldLayer.frame.size.width, self.worldLayer.frame.size.height,
    tree_node_text(app_state->current_node),
    self.currentNodeFrame.origin.x, self.currentNodeFrame.origin.y,
    self.currentNodeFrame.size.width, self.currentNodeFrame.size.height
     ];
}

- (void)mouseMoved:(NSEvent *)event {
    [self updateInfoView];
}

- (void)mouseDown:(NSEvent *)event {
    log_debug("mouseDown: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
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
    self.lastMouse = [self convertPoint:event.locationInWindow fromView:nil];
    self.lastWorldPosition = self.viewOrigon;
    [self updateInfoView];
}

- (void)rightMouseDragged:(NSEvent *)event {
    log_debug("mouseDragged: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    dont_adjust_doc_view_by_current = true;
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

- (void)scrollWheel:(NSEvent *)event {
    dont_adjust_doc_view_by_current = true;

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
    log_debug("mouseUp: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    if(self.isDragging) {
        self.isDragging = NO;// drag end
    }else{
        // handle click

        self.hitTesting = YES;
        self.hitNode = (TreeNode){.kind = TREE_NODE_NULL};
        self.hitCurrent = NO;
        self.latestMouseViewPoint = [self convertPoint:event.locationInWindow fromView:nil];
        self.p_in_doc_view = [self.layer convertPoint:self.latestMouseViewPoint
                                        toLayer:self.worldLayer];

        [self performWithoutImplicitAnimation:^{
            [self render_mindmap];
        }];

        self.hitTesting = NO;

        if(!tree_node_is_null(self.hitNode)) {
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
-(void)snapWindowToFullScreen{
    NSWindow *window = self.window;
    NSRect screenRect = [NSScreen mainScreen].visibleFrame;
    [window setFrame:screenRect display:YES animate:NO];
    [self layout];
    [self performWithoutImplicitAnimation:^{
        [self render_mindmap];
    }];
}

- (void)keyDown:(NSEvent *)event {
    log_debug("keyDown: %p, x: %.2f, y: %.2f", event, event.locationInWindow.x, event.locationInWindow.y);
    NSString *characters = event.charactersIgnoringModifiers;
    unichar key = [characters characterAtIndex:0];
    BOOL isControlDown = (event.modifierFlags & NSEventModifierFlagControl) != 0;
    BOOL isFnDown = (event.modifierFlags & NSEventModifierFlagFunction) != 0;
    BOOL isCommandDown = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
    BOOL isOptionDown = (event.modifierFlags & NSEventModifierFlagOption) != 0;
    if(isFnDown && isControlDown ){
        if(event.keyCode == 119){
            [self snapWindowToRightHalf];
            [self layout];
            return;
        }
        if(event.keyCode == 115){
            [self snapWindowToLeftHalf];
            [self layout];
            return;
        }
        if(event.keyCode == 116){
            [self snapWindowToTopHalf];
            [self layout];
            return;
        }
        if(event.keyCode == 121){
            [self snapWindowToBottomHalf];
            return;
        }
        if(key == 'f' || event.keyCode == 3){
            [self snapWindowToFullScreen];
            return;
        }
    }
    // Command + ...
    if(!isFnDown && !isControlDown && !isOptionDown && isCommandDown){
        CGPoint focusPoint = self.latestMouseViewPoint;
        if (CGPointEqualToPoint(focusPoint, CGPointZero)) {
            focusPoint = CGPointMake(CGRectGetMidX(self.bounds), CGRectGetMidY(self.bounds));
        }

        switch (key) {
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

    dont_adjust_doc_view_by_current = false;

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
                || key == '/'
                || key == '[' || key == ']' 
                || key == '#' || key == '$' || key == '%' || key == '^' || key == '&' || key == '*' || key == '(' || key == ')'
                || key == '-' || key == '=' || key == ' '
                || key == '\t' || key == ';' || key == '\\'
                ){ 
                UserOperation uo = input_convert(app_state->input_state, key, 0, NULL, NO);
                app_apply_event(app_state, uo);
                if(app_state->input_state->type != INPUT_STATE_PREFIX
                    && uo.type != UO_VIEW_HALF_SCREEN_LEFT 
                    && uo.type != UO_VIEW_HALF_SCREEN_RIGHT){
                    dont_adjust_doc_view_by_current = false;
                }
                if(app_state->input_state->type == INPUT_STATE_PREFIX){
                    self.buttomCommandTextView.string = [NSString stringWithFormat:@"Prefix: %c%.*s", 
                        app_state->input_state->prefix,
                        app_state->input_state->prefix_count,
                        app_state->input_state->key_buffer];

                    self.buttomCommandTextView.backgroundColor = [NSColor blackColor];
                    self.buttomCommandTextView.textColor = [NSColor whiteColor];
                    self.buttomCommandTextView.hidden = NO;
                    NSSize textSize = [self.buttomCommandTextView.string sizeWithAttributes:@{NSFontAttributeName: default_font()}];
                    self.buttomCommandTextView.frame = CGRectMake(0, 0, textSize.width + default_text_points, default_base_points);
                    [self layout];
                }else if(app_state->input_state->type != INPUT_STATE_TYPE_SEARCH_QUERY){
                    self.buttomCommandTextView.hidden = YES;
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
    NSTextView *textView = v.buttomCommandTextView;
    textView.backgroundColor = [NSColor blackColor];
    textView.textColor = [NSColor whiteColor];
    textView.string = @"/";
    textView.hidden = NO;
    textView.frame = CGRectMake(0, 0, view_w, default_base_points);
    [v layout];
    [v.window makeFirstResponder:textView];
    return NULL;
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

#pragma mark - AppDelegate

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic,strong) NSWindow *window;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;

    NSRect frame = NSMakeRect(0, 0, 900, 600);

    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:(NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskResizable 
                                                         )
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.styleMask =
        NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
        NSWindowStyleMaskFullSizeContentView;

    self.window.titleVisibility = NSWindowTitleHidden;
    self.window.titlebarAppearsTransparent = YES;

    CanvasView *view = [[CanvasView alloc] initWithFrame:frame];
    self.window.contentView = view;

    // bind callbacks
    app_state->ui_ctx = view;
    app_state->ui_render = canvas_view_render;
    app_state->ui_center_view_on_current = canvas_view_center_view_on_current; 
    app_state->ui_get_search_query = canvas_view_get_search_query;
    app_state->ui_view_prev_page = canvas_view_prev_page;
    app_state->ui_view_next_page = canvas_view_next_page;
    app_state->ui_view_prev_half_page = canvas_view_prev_half_page;
    app_state->ui_view_next_half_page = canvas_view_next_half_page;
    app_state->ui_place_current_left = canvas_view_current_left;
    app_state->ui_place_current_right = canvas_view_current_right;
    app_state->ui_view_half_screen_right = canvas_view_half_screen_right;
    app_state->ui_view_half_screen_left = canvas_view_half_screen_left;

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

@end
NSMenu * buildMenu() {
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        NSMenuItem *minimizeItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [windowMenu addItem:minimizeItem];
    NSMenuItem *windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [mainMenu addItem:windowMenuItem];

    NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
        NSMenuItem *aboutItem = [[NSMenuItem alloc] initWithTitle:@"About" action:nil keyEquivalent:@""];
        [helpMenu addItem:aboutItem];

    NSMenuItem *helpMenuItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
    [helpMenuItem setSubmenu:helpMenu];
    [mainMenu addItem:helpMenuItem];

    return mainMenu;
}
int main(int argc, char *const*argv) {

    static struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "d", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                debuging = 1;
                break;
            default:
                break;
        }
    }


    init_logging();
    @autoreleasepool {
        // 切换 cwd 到可执行文件所在目录
        char path[1024];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            chdir(dirname(path));
        }
    }
    
    // 现在的 cwd 就是 Contents/MacOS 了
    char cwdbuf[1024];
    getcwd(cwdbuf, sizeof(cwdbuf));
    NSLog(@"New cwd = %s", cwdbuf);

    NSString *dbPath = [[NSBundle mainBundle] pathForResource:@"universe-mindmap" ofType:@"umt"];
    if (!dbPath) {
        dbPath = @"universe-mindmap.umt";
    }
    if(argc > 1 && argv[argc-1] != NULL && strstr(argv[argc-1], ".umt") != 0){
        NSLog(@"Using db path from command line: %s", argv[argc-1]);
        dbPath = [NSString stringWithUTF8String:argv[argc-1]];
    }
    app_state = app_init([dbPath UTF8String]);

    // const char *db_file = "universe-mindmap.umt";
    // app_state = app_init(db_file);
    if(app_state == NULL) {
        NSLog( @"Failed to initialize app state\n");
        return 1;
    }else{
        NSLog( @"App state initialized successfully\n");
        NSLog( @"Root node text: %s\n", tree_node_text(app_state->tree_overlay->root));
    }

    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];

        NSMenu *mainMenu = buildMenu();
        [NSApp setMainMenu:mainMenu];

        AppDelegate *delegate = [AppDelegate new];
        app.delegate = delegate;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        [app run];
    }
    return 0;
}
