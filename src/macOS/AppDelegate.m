#import "AppDelegate.h"
#import <Carbon/Carbon.h>

#import "CanvasView.h"


extern AppState* app_state;

static EventHotKeyRef gSelfHotKeyRef;
static EventHotKeyRef gFirefoxHotKeyRef;
static EventHotKeyRef gTerminalHotKeyRef;
static EventHotKeyRef gVsCodeHotKeyRef;
static NSString * const FirefoxBundleIdentifier = @"org.mozilla.firefox";
static NSString * const TerminalBundleIdentifier = @"com.apple.Terminal";
static NSString * const VsCodeBundleIdentifier = @"com.microsoft.VSCode";

typedef NS_ENUM(UInt32, DemoHotKeyID) {
    DemoHotKeyIDSelf,
    DemoHotKeyIDFirefox,
    DemoHotKeyIDTerminal,
    DemoHotKeyIDVsCode,
};


static void ActivateOrLaunchFirefox(void) {
    NSArray<NSRunningApplication *> *runningApps =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:FirefoxBundleIdentifier];

    NSRunningApplication *firefox = runningApps.firstObject;
    if (firefox) {
        [firefox activateWithOptions:NSApplicationActivateAllWindows];
        return;
    }

    NSURL *firefoxURL =
        [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:FirefoxBundleIdentifier];
    if (!firefoxURL) {
        NSLog(@"Firefox is not installed or could not be found.");
        return;
    }

    NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;

    [[NSWorkspace sharedWorkspace] openApplicationAtURL:firefoxURL
                                          configuration:configuration
                                      completionHandler:^(NSRunningApplication *app, NSError *error) {
        if (error) {
            NSLog(@"Failed to launch Firefox: %@", error);
        }
    }];
}
static void ActivateOrLaunchTerminal(void) {
    NSArray<NSRunningApplication *> *runningApps =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:TerminalBundleIdentifier];

    NSRunningApplication *terminal = runningApps.firstObject;
    if (terminal) {
        [terminal activateWithOptions:NSApplicationActivateAllWindows];
        return;
    }

    NSURL *terminalURL =
        [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:TerminalBundleIdentifier];
    if (!terminalURL) {
        NSLog(@"Terminal is not installed or could not be found.");
        return;
    }

    NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;

    [[NSWorkspace sharedWorkspace] openApplicationAtURL:terminalURL
                                          configuration:configuration
                                      completionHandler:^(NSRunningApplication *app, NSError *error) {
        if (error) {
            NSLog(@"Failed to launch Terminal: %@", error);
        }
    }];
}

static void ActivateOrLaunchVsCode(void) {
    NSArray<NSRunningApplication *> *runningApps =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:VsCodeBundleIdentifier];

    NSRunningApplication *vscode = runningApps.firstObject;
    if (vscode) {
        [vscode activateWithOptions:NSApplicationActivateAllWindows];
        return;
    }

    NSURL *vscodeURL =
        [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:VsCodeBundleIdentifier];
    if (!vscodeURL) {
        // Try with the Insiders version
        vscodeURL = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.microsoft.VSCodeInsiders"];
        if (!vscodeURL) {
            NSLog(@"VS Code is not installed or could not be found.");
            return;
        }
    }

    NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;

    [[NSWorkspace sharedWorkspace] openApplicationAtURL:vscodeURL
                                          configuration:configuration
                                      completionHandler:^(NSRunningApplication *app, NSError *error) {
        if (error) {
            NSLog(@"Failed to launch VS Code: %@", error);
        }
    }];
}

OSStatus HotKeyHandler(EventHandlerCallRef nextHandler,
                       EventRef event,
                       void *userData) {

    EventHotKeyID hotKeyID;
    GetEventParameter(event,
                      kEventParamDirectObject,
                      typeEventHotKeyID,
                      NULL,
                      sizeof(hotKeyID),
                      NULL,
                      &hotKeyID);

    switch (hotKeyID.id) {
        case DemoHotKeyIDFirefox:
            ActivateOrLaunchFirefox();
            break;
        case DemoHotKeyIDTerminal:
            ActivateOrLaunchTerminal();
            break;
        case DemoHotKeyIDVsCode:
            ActivateOrLaunchVsCode();
            break;
        case DemoHotKeyIDSelf:
            [NSApp activateIgnoringOtherApps:YES];
            break;
        default:
            break;
    }
    
    return noErr;
}

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
    app_state->ui_get_search_backward_query = canvas_view_get_search_backward_query;
    app_state->ui_get_viewport_topmost_sibling = canvas_view_get_viewport_topmost_sibling;
    app_state->ui_get_viewport_bottommost_sibling = canvas_view_get_viewport_bottommost_sibling;
    app_state->ui_view_prev_page = canvas_view_prev_page;
    app_state->ui_view_next_page = canvas_view_next_page;
    app_state->ui_view_prev_half_page = canvas_view_prev_half_page;
    app_state->ui_view_next_half_page = canvas_view_next_half_page;
    app_state->ui_place_current_left = canvas_view_current_left;
    app_state->ui_place_current_right = canvas_view_current_right;
    app_state->ui_view_half_screen_right = canvas_view_half_screen_right;
    app_state->ui_view_half_screen_left = canvas_view_half_screen_left;

    [self.window setFrameAutosaveName:@"MainWindow"];

    // Only center if no saved window frame (first launch)
    NSString *savedFrameKey = [NSString stringWithFormat:@"NSWindow Frame %@", self.window.frameAutosaveName];
    if ([[NSUserDefaults standardUserDefaults] objectForKey:savedFrameKey] == nil) {
        [self.window center];
    }

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self registerHotKey];
    [self setupMainMenu];
}

- (void)setupMainMenu {
    NSString *appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];

    NSMenu *mainMenu = [[NSMenu alloc] init];

    // ── App Menu ──
    NSMenuItem *appMenuItem = [mainMenu addItemWithTitle:@"" action:NULL keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:[NSString stringWithFormat:@"About %@", appName]
                       action:@selector(orderFrontStandardAboutPanel:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", appName]
                       action:@selector(hide:)
                keyEquivalent:@"h"];
    NSMenuItem *hideOthersItem = [appMenu addItemWithTitle:@"Hide Others"
                                                    action:@selector(hideOtherApplications:)
                                             keyEquivalent:@"h"];
    [hideOthersItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [appMenu addItemWithTitle:@"Show All"
                       action:@selector(unhideAllApplications:)
                keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[NSString stringWithFormat:@"Quit %@", appName]
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    appMenuItem.submenu = appMenu;

    // ── File Menu ──
    NSMenuItem *fileMenuItem = [mainMenu addItemWithTitle:@"File" action:NULL keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] init];
    [fileMenu addItemWithTitle:@"Close Window"
                        action:@selector(performClose:)
                 keyEquivalent:@"w"];
    fileMenuItem.submenu = fileMenu;

    // ── Edit Menu ──
    NSMenuItem *editMenuItem = [mainMenu addItemWithTitle:@"Edit" action:NULL keyEquivalent:@""];
    NSMenu *editMenu = [[NSMenu alloc] init];
    [editMenu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [editMenu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Delete" action:@selector(delete:) keyEquivalent:@""];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    editMenuItem.submenu = editMenu;

    // ── Window Menu ──
    NSMenuItem *windowMenuItem = [mainMenu addItemWithTitle:@"Window" action:NULL keyEquivalent:@""];
    NSMenu *windowMenu = [[NSMenu alloc] init];
    [windowMenu addItemWithTitle:@"Minimize"
                          action:@selector(performMiniaturize:)
                   keyEquivalent:@"m"];
    [windowMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *centerItem = [windowMenu addItemWithTitle:@"Center"
                                                   action:@selector(centerWindow:)
                                            keyEquivalent:@""];
    centerItem.target = self;

    [windowMenu addItemWithTitle:@"Bring All to Front"
                          action:@selector(arrangeInFront:)
                   keyEquivalent:@""];

    windowMenuItem.submenu = windowMenu;
    [NSApp setWindowsMenu:windowMenu];

    // ── Help Menu ──
    NSMenuItem *helpMenuItem = [mainMenu addItemWithTitle:@"Help" action:NULL keyEquivalent:@""];
    NSMenu *helpMenu = [[NSMenu alloc] init];
    [helpMenu addItemWithTitle:[NSString stringWithFormat:@"%@ Help", appName]
                        action:@selector(showHelp:)
                 keyEquivalent:@"?"];
    helpMenuItem.submenu = helpMenu;

    [NSApp setMainMenu:mainMenu];
}

- (IBAction)centerWindow:(id)sender {
    (void)sender;
    [self.window center];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    if (gFirefoxHotKeyRef) {
        UnregisterEventHotKey(gFirefoxHotKeyRef);
    }
    if (gTerminalHotKeyRef) {
        UnregisterEventHotKey(gTerminalHotKeyRef);
    }
    if (gVsCodeHotKeyRef) {
        UnregisterEventHotKey(gVsCodeHotKeyRef);
    }
    if (gSelfHotKeyRef) {
        UnregisterEventHotKey(gSelfHotKeyRef);
    }
}

- (void)registerHotKey {

    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'demo';
    hotKeyID.id = DemoHotKeyIDFirefox;

    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;

    InstallApplicationEventHandler(&HotKeyHandler,
                                   1,
                                   &eventType,
                                   NULL,
                                   NULL);

    RegisterEventHotKey(
        kVK_ANSI_F,                // F
        cmdKey | controlKey,        // ⌘ + ^
        hotKeyID,
        GetApplicationEventTarget(),
        0,
        &gFirefoxHotKeyRef
    );

    hotKeyID.id = DemoHotKeyIDSelf;
    RegisterEventHotKey(
        kVK_ANSI_U,                // U 
        cmdKey | controlKey,       // ⌘ + ^
        hotKeyID,
        GetApplicationEventTarget(),
        0,
        &gSelfHotKeyRef
    );

    hotKeyID.id = DemoHotKeyIDTerminal;
    RegisterEventHotKey(
        kVK_ANSI_T,                // T
        cmdKey | controlKey,       // ⌘ + ^
        hotKeyID,
        GetApplicationEventTarget(),
        0,
        &gTerminalHotKeyRef
    );

    hotKeyID.id = DemoHotKeyIDVsCode;
    RegisterEventHotKey(
        kVK_ANSI_V,                // V
        cmdKey | controlKey,       // ⌘ + ^
        hotKeyID,
        GetApplicationEventTarget(),
        0,
        &gVsCodeHotKeyRef
    );

}

@end
