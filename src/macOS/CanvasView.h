#import <Cocoa/Cocoa.h>
// CATextLayer
#import <QuartzCore/QuartzCore.h>

#include "../app/app.h"

static inline void logd(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

#pragma mark - Canvas View callbacks
void canvas_view_render(void *view);
void canvas_view_center_view_on_current(void *view);
char* canvas_view_get_search_query(void *view);
char* canvas_view_get_search_backward_query(void *view);
TreeNode canvas_view_get_viewport_topmost_sibling(void *ui_ctx, TreeOverlay *ov, TreeNode current);
TreeNode canvas_view_get_viewport_bottommost_sibling(void *ui_ctx, TreeOverlay *ov, TreeNode current);
void canvas_view_prev_page(void *ui_ctx);
void canvas_view_next_page(void *ui_ctx);
void canvas_view_next_half_page(void *ui_ctx);
void canvas_view_prev_half_page(void *ui_ctx);
void canvas_view_current_left(void *ui_ctx);
void canvas_view_current_right(void *ui_ctx);
void canvas_view_half_screen_right(void *ui_ctx);
void canvas_view_half_screen_left(void *ui_ctx);

CGColorRef randomVividColor() ;
NSFont *default_font() ;
NSFont *default_font_bold() ;

CGSize measure_text(NSString *text) ;

double text_field_display_width(TreeNode node);

#pragma mark - Text Input View
@interface TextInputView : NSTextView
@property(nonatomic, copy) void (^onCancel)(void);
@end

#pragma mark - Canvas View (Layer-based, no drawRect)
@interface CanvasView : NSView <NSTextViewDelegate>
@property(nonatomic,strong) CADisplayLink *displayLink;
@property(nonatomic,strong) CALayer *worldLayer;
// @property(nonatomic,strong) CATextLayer *fpsLayer;
@property(nonatomic,strong) CATextLayer *infoLayer;
@property(nonatomic) BOOL hitTesting;
@property(nonatomic) TreeNode hitNode;
@property(nonatomic) BOOL hitCurrent;
@property(nonatomic,strong) NSTrackingArea *trackingArea;
@property(nonatomic,strong) NSTrackingArea *titlebarTrackingArea;
@property(nonatomic,strong) NSButton *closeButton;
@property(nonatomic,strong) NSButton *minimizeButton;
@property(nonatomic,strong) NSButton *zoomButton;
@property(nonatomic) BOOL isDragging;
@property(nonatomic) CGPoint lastMouse;
@property(nonatomic) CGPoint lastWorldPosition;
@property(nonatomic) CGPoint latestMouseViewPoint;
@property(nonatomic) CGPoint p_in_doc_view;
@property(nonatomic) CGPoint viewOrigon;
@property(nonatomic) CGFloat zoomScale;
@property(nonatomic) CGRect currentNodeFrame;
@property(nonatomic, strong) TextInputView *bottomCommandTextView;
@property(nonatomic, strong) NSMutableArray<NSDictionary *> *visibleNodeInfos;
// @property(nonatomic) CFTimeInterval lastFPSUpdateTime;
@end
