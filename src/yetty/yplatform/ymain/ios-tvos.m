/*
 * yplatform/ymain/ios-tvos.m — iOS / tvOS entry.
 *
 * UIApplicationMain owns the run loop (this is the platform's "run"); the native
 * CAMetalLayer-backed view arrives asynchronously, so the platform is created
 * and platform_init runs from the view controller's viewDidLoad. The Obj-C glue
 * lives here; the platform/window/clipboard objects are plain-C yclass objects.
 *
 * NOT WIRED IN: forward-declared platform symbols, not in any CMake target; the
 * existing yinit/ios-tvos.m stays until this replaces it.
 *
 * SEAMS: pushing the CAMetalLayer + framebuffer metrics into the platform's
 * ios_window, and running the app body on top, are the platform→app seams (same
 * as the other platforms).
 */

#import <UIKit/UIKit.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/ytrace/ytrace.h>

/* Generated platform symbols. */
struct yetty_ycore_void_result yetty_yplatform_register(void);
struct yetty_yclass_object_ptr_result yetty_yplatform_ios_platform_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object *obj,
                                                            int argc, char **argv);

@interface YettyViewController : UIViewController
@property(nonatomic, assign) struct yetty_yclass_object *platform;
@end

@implementation YettyViewController
- (void)viewDidLoad {
    [super viewDidLoad];

    struct yetty_ycore_void_result reg = yetty_yplatform_register();
    if (YETTY_IS_ERR(reg)) {
        yerror("ios: platform register failed: %s", reg.error.msg);
        yetty_ycore_error_destroy(reg.error);
        return;
    }

    struct yetty_yclass_object_ptr_result platform_res = yetty_yplatform_ios_platform_create(NULL);
    if (YETTY_IS_ERR(platform_res)) {
        yerror("ios: platform create failed: %s", platform_res.error.msg);
        yetty_ycore_error_destroy(platform_res.error);
        return;
    }
    self.platform = platform_res.value;

    /* TODO(seam): build the CAMetalLayer-backed view and push it + the
     * framebuffer metrics into the platform's ios_window before init. */
    /* run() calls init() itself; iOS has no argv here. UIApplicationMain is the
     * actual run loop, so run() returns after init. */
    struct yetty_ycore_void_result run_res = yetty_yplatform_platform_run(self.platform, 0, NULL);
    if (YETTY_IS_ERR(run_res)) {
        yerror("ios: platform run failed: %s", run_res.error.msg);
        yetty_ycore_error_destroy(run_res.error);
        return;
    }
    ydebug("ios: platform run invoked (UIApplicationMain drives the loop)");
}
@end

@interface YettyAppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow *window;
@end

@implementation YettyAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[YettyViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[])
{
    @autoreleasepool {
        /* UIApplicationMain is the run loop; the platform is init'd from
         * viewDidLoad above. */
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([YettyAppDelegate class]));
    }
}
