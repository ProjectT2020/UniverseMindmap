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

#import "AppDelegate.h"

extern AppState* app_state;

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

        AppDelegate *delegate = [AppDelegate new];
        app.delegate = delegate;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        [app run];
    }
    return 0;
}
