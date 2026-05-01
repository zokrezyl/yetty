/* iOS main - Application entry point */

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <GameController/GameController.h>

#include <webgpu/webgpu.h>
#include <pthread.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/event.h>
#include <yetty/platform/platform-input-pipe.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/platform/extract-assets.h>
#include <yetty/ytrace/ytrace.h>

/* Forward declarations */
const char *yetty_yplatform_get_cache_dir(void);
const char *yetty_yplatform_get_data_dir(void);
const char *yetty_yplatform_get_runtime_dir(void);
WGPUSurface yetty_yplatform_create_surface_from_layer(WGPUInstance instance, CAMetalLayer *layer);

/* YettyMetalView - UIView backed by CAMetalLayer */
@interface YettyMetalView : UIView
@property (nonatomic, readonly) CAMetalLayer *metalLayer;
@end

@implementation YettyMetalView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (CAMetalLayer *)metalLayer {
    return (CAMetalLayer *)self.layer;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.metalLayer.device = MTLCreateSystemDefaultDevice();
        self.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        self.metalLayer.framebufferOnly = YES;
        self.metalLayer.contentsScale = [UIScreen mainScreen].scale;
        self.backgroundColor = [UIColor blackColor];
    }
    return self;
}

@end

/* Render thread args */
struct yetty_yplatform_render_thread_args {
    struct yetty_yetty_yetty *yetty;
    int *running;
};

static void *render_thread_func(void *arg)
{
    struct yetty_yplatform_render_thread_args *args = arg;
    yetty_run(args->yetty);
    *(args->running) = 0;
    return NULL;
}

/* YettyViewController - implements UIKeyInput for keyboard */
@interface YettyViewController : UIViewController <UIKeyInput> {
    YettyMetalView *_metalView;
    struct yetty_yetty_yetty *_yetty;
    struct yetty_ycore_input_pipe *_pipe;
    struct yetty_yconfig_config *_config;
    struct yetty_yplatform_pty_factory *_ptyFactory;
    WGPUInstance _instance;
    WGPUSurface _surface;
    pthread_t _renderThread;
    int _running;
}
@end

@implementation YettyViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    _metalView = [[YettyMetalView alloc] initWithFrame:self.view.bounds];
    _metalView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:_metalView];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self initializeYetty];
}

- (void)initializeYetty {
    ydebug("initializeYetty starting");

    const char *cache_dir = yetty_yplatform_get_cache_dir();
    const char *runtime_dir = yetty_yplatform_get_runtime_dir();
    /* extract_assets writes shaders/fonts under data_dir (Application
     * Support/yetty), so point yetty at that — not cache_dir which is never
     * populated. */
    const char *data_dir = yetty_yplatform_get_data_dir();
    ydebug("cache_dir=%s data_dir=%s runtime_dir=%s",
           cache_dir, data_dir, runtime_dir);

    static char shaders_dir[512];
    static char fonts_dir[512];
    snprintf(shaders_dir, sizeof(shaders_dir), "%s/shaders", data_dir);
    snprintf(fonts_dir, sizeof(fonts_dir), "%s/fonts", data_dir);

    struct yetty_yplatform_paths paths = {
        .shaders_dir = shaders_dir,
        .fonts_dir = fonts_dir,
        .runtime_dir = runtime_dir,
        .bin_dir = NULL
    };

    /* Config */
    ydebug("creating config");
    struct yetty_yconfig_result config_result = yetty_yconfig_create(0, NULL, &paths);
    if (!YETTY_IS_OK(config_result)) {
        yerror("failed to create config: %s", config_result.error);
        return;
    }
    _config = config_result.value;
    ydebug("config created");

    /* Extract embedded assets (fonts, shaders) to cache */
    ydebug("extracting assets");
    yetty_platform_extract_assets(_config);
    ydebug("assets extracted");

    /* Platform input pipe */
    ydebug("creating input pipe");
    struct yetty_yplatform_input_pipe_result pipe_result = yetty_platform_input_pipe_create();
    if (!YETTY_IS_OK(pipe_result)) {
        yerror("failed to create input pipe: %s", pipe_result.error);
        return;
    }
    _pipe = pipe_result.value;
    ydebug("input pipe created");

    /* PTY factory */
    ydebug("creating PTY factory");
    struct yetty_yplatform_pty_factory_result pty_result = yetty_yplatform_pty_factory_create(_config, NULL);
    if (!YETTY_IS_OK(pty_result)) {
        yerror("failed to create PTY factory: %s", pty_result.error);
        return;
    }
    _ptyFactory = pty_result.value;
    ydebug("PTY factory created");

    /* WebGPU instance */
    ydebug("creating WebGPU instance");
    _instance = wgpuCreateInstance(NULL);
    if (!_instance) {
        yerror("failed to create WebGPU instance");
        return;
    }
    ydebug("WebGPU instance created");

    /* Surface */
    ydebug("creating surface");
    _surface = yetty_yplatform_create_surface_from_layer(_instance, _metalView.metalLayer);
    if (!_surface) {
        yerror("failed to create surface");
        return;
    }
    ydebug("surface created");

    /* Get framebuffer size */
    CGSize size = _metalView.bounds.size;
    CGFloat scale = _metalView.metalLayer.contentsScale;
    uint32_t fb_width = (uint32_t)(size.width * scale);
    uint32_t fb_height = (uint32_t)(size.height * scale);
    _metalView.metalLayer.drawableSize = CGSizeMake(fb_width, fb_height);
    ydebug("framebuffer size: %ux%u", fb_width, fb_height);

    /* Create Yetty */
    ydebug("creating yetty");
    struct yetty_yetty_app_context ctx = {
        .app_gpu_context = {
            .instance = _instance,
            .surface = _surface,
            .surface_width = fb_width,
            .surface_height = fb_height
        },
        .config = _config,
        .platform_input_pipe = _pipe,
        .clipboard_manager = NULL,
        .pty_factory = _ptyFactory
    };

    struct yetty_yetty_yetty_result yetty_result = yetty_create(&ctx);
    if (!YETTY_IS_OK(yetty_result)) {
        yerror("failed to create yetty: %s", yetty_result.error);
        return;
    }
    _yetty = yetty_result.value;
    ydebug("yetty created");

    /* Start render thread */
    _running = 1;
    struct yetty_yplatform_render_thread_args *args = malloc(sizeof(struct yetty_yplatform_render_thread_args));
    args->yetty = _yetty;
    args->running = &_running;
    pthread_create(&_renderThread, NULL, render_thread_func, args);

    /* Send initial resize event */
    {
        CGSize size = _metalView.bounds.size;
        CGFloat scale = _metalView.metalLayer.contentsScale;
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_RESIZE;
        ev.resize.width = (float)(size.width * scale);
        ev.resize.height = (float)(size.height * scale);
        ydebug("initial resize: %.0fx%.0f", ev.resize.width, ev.resize.height);
        _pipe->ops->write(_pipe, &ev, sizeof(ev));
    }

    /* Become first responder to receive keyboard input */
    [self becomeFirstResponder];

    /* tvOS does NOT route hardware-keyboard UIPress events through the
     * responder chain to the view controller — they go via the focus
     * engine, which our full-screen render view is not part of. The
     * GameController framework's GCKeyboard gives us raw HW key events
     * regardless of focus, which is the only reliable path for a HW kb
     * on tvOS. (Works on iOS too, harmless to register there.) */
    [self setupGCKeyboard];
}

#pragma mark - GCKeyboard (tvOS hardware keyboard)

- (void)setupGCKeyboard
{
    if (GCKeyboard.coalescedKeyboard) {
        [self attachKeyboard:GCKeyboard.coalescedKeyboard];
    }
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(keyboardDidConnect:)
               name:GCKeyboardDidConnectNotification
             object:nil];
}

- (void)keyboardDidConnect:(NSNotification *)note
{
    GCKeyboard *kb = note.object;
    ydebug("GCKeyboard connected");
    [self attachKeyboard:kb];
}

- (void)attachKeyboard:(GCKeyboard *)kb
{
    /* File compiles under MRC (no ARC), can't use __weak. Use
     * __unsafe_unretained — view controller lives for app lifetime so
     * dangling-ref risk is nil. */
    __unsafe_unretained YettyViewController *self_ = self;
    kb.keyboardInput.keyChangedHandler =
        ^(GCKeyboardInput *input, GCControllerButtonInput *btn, GCKeyCode keyCode, BOOL pressed) {
            if (!pressed || !self_ || !self_->_pipe) return;
            [self_ handleGCKey:keyCode input:input];
        };
}

- (void)handleGCKey:(GCKeyCode)keyCode input:(GCKeyboardInput *)input
{
    /* GCKeyCode constants are extern consts (not compile-time) so we
     * can't use switch. Use if/else. Map HID-style key codes to GLFW
     * key numbers for the navigation/control set vterm needs. */
    int glfw_key = 0;
    if      (keyCode == GCKeyCodeReturnOrEnter)     glfw_key = 257;
    else if (keyCode == GCKeyCodeTab)               glfw_key = 258;
    else if (keyCode == GCKeyCodeDeleteOrBackspace) glfw_key = 259;
    else if (keyCode == GCKeyCodeEscape)            glfw_key = 256;
    else if (keyCode == GCKeyCodeLeftArrow)         glfw_key = 263;
    else if (keyCode == GCKeyCodeRightArrow)        glfw_key = 262;
    else if (keyCode == GCKeyCodeUpArrow)           glfw_key = 265;
    else if (keyCode == GCKeyCodeDownArrow)         glfw_key = 264;
    else if (keyCode == GCKeyCodeHome)              glfw_key = 268;
    else if (keyCode == GCKeyCodeEnd)               glfw_key = 269;
    else if (keyCode == GCKeyCodePageUp)            glfw_key = 266;
    else if (keyCode == GCKeyCodePageDown)          glfw_key = 267;
    if (glfw_key) {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_KEY_DOWN;
        ev.key.key = glfw_key;
        ev.key.mods = 0;
        ev.key.scancode = 0;
        _pipe->ops->write(_pipe, &ev, sizeof(ev));
        return;
    }

    /* Printable: derive ASCII from keycode + shift. Shift detected via
     * the GCKeyboardInput dictionary lookup. */
    GCControllerButtonInput *lshift = [input buttonForKeyCode:GCKeyCodeLeftShift];
    GCControllerButtonInput *rshift = [input buttonForKeyCode:GCKeyCodeRightShift];
    BOOL shift = (lshift && lshift.pressed) || (rshift && rshift.pressed);
    int ascii = 0;
    if (keyCode >= GCKeyCodeKeyA && keyCode <= GCKeyCodeKeyZ) {
        ascii = (shift ? 'A' : 'a') + (int)(keyCode - GCKeyCodeKeyA);
    } else if (keyCode == GCKeyCodeSpacebar) {
        ascii = ' ';
    } else if (keyCode >= GCKeyCodeOne && keyCode <= GCKeyCodeNine) {
        ascii = '1' + (int)(keyCode - GCKeyCodeOne);
    } else if (keyCode == GCKeyCodeZero) {
        ascii = '0';
    }
    if (ascii) {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_CHAR;
        ev.chr.codepoint = (uint32_t)ascii;
        ev.chr.mods = 0;
        _pipe->ops->write(_pipe, &ev, sizeof(ev));
    }
}

#pragma mark - UIKeyInput

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (BOOL)hasText {
    return YES;
}

- (void)insertText:(NSString *)text {
    if (!_pipe) return;

    ydebug("insertText: %s", [text UTF8String]);

    /* Send each character as YETTY_EVENT_CHAR */
    NSUInteger len = [text length];
    for (NSUInteger i = 0; i < len; i++) {
        unichar ch = [text characterAtIndex:i];
        struct yetty_ycore_event ev = {0};
        ev.type = YETTY_EVENT_CHAR;
        ev.chr.codepoint = (uint32_t)ch;
        ev.chr.mods = 0;
        _pipe->ops->write(_pipe, &ev, sizeof(ev));
    }
}

- (void)deleteBackward {
    if (!_pipe) return;

    ydebug("deleteBackward");

    /* Send backspace key event (GLFW_KEY_BACKSPACE = 259) */
    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_KEY_DOWN;
    ev.key.key = 259;
    ev.key.mods = 0;
    ev.key.scancode = 0;
    _pipe->ops->write(_pipe, &ev, sizeof(ev));
}

#pragma mark - UIPress (hardware keyboard, required for tvOS)

/* tvOS has no on-screen keyboard, so UIKeyInput's insertText: never fires
 * for hardware-keyboard input. Both iOS and tvOS deliver hardware keys via
 * pressesBegan:withEvent: with the modern UIKey API (iOS 13.4+ / tvOS 13.4+).
 * Forward characters as YETTY_EVENT_CHAR; map a few control keys (BS, return,
 * arrows, escape) to GLFW-numbered YETTY_EVENT_KEY_DOWN. */
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    if (!_pipe) {
        [super pressesBegan:presses withEvent:event];
        return;
    }

    BOOL anyHandled = NO;
    for (UIPress *press in presses) {
        UIKey *key = press.key;
        if (!key) continue;
        anyHandled = YES;

        NSString *chars = key.characters;
        ydebug("pressesBegan: keyCode=%ld chars='%s' mods=0x%lx",
               (long)key.keyCode,
               chars.length ? [chars UTF8String] : "",
               (unsigned long)key.modifierFlags);

        /* Map a small set of control keys to GLFW-style key codes via
         * YETTY_EVENT_KEY_DOWN — vterm needs these for navigation/editing. */
        int glfw_key = 0;
        switch (key.keyCode) {
            case UIKeyboardHIDUsageKeyboardReturnOrEnter: glfw_key = 257; break; /* GLFW_KEY_ENTER */
            case UIKeyboardHIDUsageKeyboardTab:           glfw_key = 258; break; /* GLFW_KEY_TAB */
            case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: glfw_key = 259; break; /* BACKSPACE */
            case UIKeyboardHIDUsageKeyboardEscape:        glfw_key = 256; break; /* ESC */
            case UIKeyboardHIDUsageKeyboardLeftArrow:     glfw_key = 263; break;
            case UIKeyboardHIDUsageKeyboardRightArrow:    glfw_key = 262; break;
            case UIKeyboardHIDUsageKeyboardUpArrow:       glfw_key = 265; break;
            case UIKeyboardHIDUsageKeyboardDownArrow:     glfw_key = 264; break;
            case UIKeyboardHIDUsageKeyboardHome:          glfw_key = 268; break;
            case UIKeyboardHIDUsageKeyboardEnd:           glfw_key = 269; break;
            case UIKeyboardHIDUsageKeyboardPageUp:        glfw_key = 266; break;
            case UIKeyboardHIDUsageKeyboardPageDown:      glfw_key = 267; break;
            default: break;
        }

        if (glfw_key) {
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_KEY_DOWN;
            ev.key.key = glfw_key;
            ev.key.mods = (uint32_t)key.modifierFlags;
            ev.key.scancode = (int)key.keyCode;
            _pipe->ops->write(_pipe, &ev, sizeof(ev));
            continue;
        }

        /* Plain printable character(s) — feed each as YETTY_EVENT_CHAR. */
        for (NSUInteger i = 0; i < chars.length; i++) {
            unichar ch = [chars characterAtIndex:i];
            struct yetty_ycore_event ev = {0};
            ev.type = YETTY_EVENT_CHAR;
            ev.chr.codepoint = (uint32_t)ch;
            ev.chr.mods = (uint32_t)key.modifierFlags;
            _pipe->ops->write(_pipe, &ev, sizeof(ev));
        }
    }

    if (!anyHandled)
        [super pressesBegan:presses withEvent:event];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];

    CGSize size = _metalView.bounds.size;
    CGFloat scale = _metalView.metalLayer.contentsScale;
    size.width *= scale;
    size.height *= scale;
    _metalView.metalLayer.drawableSize = size;

    if (_pipe) {
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_RESIZE;
        ev.resize.width = (float)size.width;
        ev.resize.height = (float)size.height;
        _pipe->ops->write(_pipe, &ev, sizeof(ev));
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesBegan:touches withEvent:event];
    if (!_pipe) return;

    UITouch *touch = [touches anyObject];
    CGPoint loc = [touch locationInView:_metalView];
    CGFloat scale = _metalView.metalLayer.contentsScale;

    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_MOUSE_DOWN;
    ev.mouse.x = (float)(loc.x * scale);
    ev.mouse.y = (float)(loc.y * scale);
    ev.mouse.button = 0;
    ev.mouse.mods = 0;
    _pipe->ops->write(_pipe, &ev, sizeof(ev));
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesMoved:touches withEvent:event];
    if (!_pipe) return;

    UITouch *touch = [touches anyObject];
    CGPoint loc = [touch locationInView:_metalView];
    CGFloat scale = _metalView.metalLayer.contentsScale;

    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_MOUSE_MOVE;
    ev.mouse.x = (float)(loc.x * scale);
    ev.mouse.y = (float)(loc.y * scale);
    _pipe->ops->write(_pipe, &ev, sizeof(ev));
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesEnded:touches withEvent:event];
    if (!_pipe) return;

    UITouch *touch = [touches anyObject];
    CGPoint loc = [touch locationInView:_metalView];
    CGFloat scale = _metalView.metalLayer.contentsScale;

    struct yetty_yui_event ev = {0};
    ev.type = YETTY_YCORE_MOUSE_UP;
    ev.mouse.x = (float)(loc.x * scale);
    ev.mouse.y = (float)(loc.y * scale);
    ev.mouse.button = 0;
    ev.mouse.mods = 0;
    _pipe->ops->write(_pipe, &ev, sizeof(ev));
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }

- (void)dealloc {
    _running = 0;
    if (_renderThread) {
        pthread_join(_renderThread, NULL);
    }
    if (_yetty) yetty_destroy(_yetty);
    if (_surface) wgpuSurfaceRelease(_surface);
    if (_instance) wgpuInstanceRelease(_instance);
    if (_ptyFactory) _ptyFactory->ops->destroy(_ptyFactory);
    if (_pipe) _pipe->ops->destroy(_pipe);
    if (_config) _config->ops->destroy(_config);
}

@end

/* YettyAppDelegate */
@interface YettyAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@property (nonatomic, strong) YettyViewController *viewController;
@end

@implementation YettyAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.viewController = [[YettyViewController alloc] init];
    self.window.rootViewController = self.viewController;
    [self.window makeKeyAndVisible];

    return YES;
}

@end

/* main */
int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([YettyAppDelegate class]));
    }
}
