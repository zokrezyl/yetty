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

#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* Forward declarations */
const char *yetty_yplatform_get_cache_dir(void);

/* Tiny on-disk trace so we can pull the input pipeline state from the
 * device via devicectl after the app runs. iOS sandbox routes stderr
 * to nowhere, so ytrace's fprintf(stderr) is invisible.
 * File: $HOME/tmp/yetty-input.log. */
static void yetty_kb_log(const char *fmt, ...)
{
    static char path[1024];
    if (!path[0]) {
        const char *home = getenv("HOME");
        snprintf(path, sizeof(path), "%s/tmp/yetty-input.log", home ? home : ".");
    }
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        char ts[32];
        time_t t = time(NULL);
        int tn = snprintf(ts, sizeof(ts), "%ld ", (long)t);
        write(fd, ts, tn);
        write(fd, buf, n);
        write(fd, "\n", 1);
    }
    close(fd);
}
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
    struct yetty_ycore_xthread_event_pipe *_pipe;
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

    /* Config — there's no shell command line on iOS / tvOS, so synthesize
     * one. Connect to a telnet server (qemu-on-host or any reachable
     * telnetd). Override at build via -DYETTY_TVOS_TELNET=\"host:port\".
     * (Mirror Android's pattern, which fakes argv with `--temu`.) */
#ifndef YETTY_TVOS_TELNET
#define YETTY_TVOS_TELNET "192.168.1.10:2423"
#endif
    ydebug("creating config: --telnet %s", YETTY_TVOS_TELNET);
    char *fake_argv[] = {(char *)"yetty", (char *)"--telnet",
                         (char *)YETTY_TVOS_TELNET, NULL};
    int fake_argc = 3;
    struct yetty_yconfig_result config_result =
        yetty_yconfig_create(fake_argc, fake_argv, &paths);
    if (!YETTY_IS_OK(config_result)) {
        yerror("failed to create config: %s", config_result.error);
        return;
    }
    _config = config_result.value;
    /* Force raster glyphs on tvOS — MSDF's per-fragment SDF re-evaluation
     * pushes ~200 MB of texel R/W per frame at 4K through the GPU and
     * appears to be a major bandwidth contributor; raster blits a
     * pre-rendered atlas (single read-write per glyph). */
    _config->ops->set_string(_config,
                             YETTY_YCONFIG_KEY_TERMINAL_FONT_RENDER_METHOD,
                             "raster");
    ydebug("config created (font render-method forced to 'raster' on tvOS)");

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
    yetty_kb_log("attachKeyboard: kb=%p", (void *)kb);
    kb.keyboardInput.keyChangedHandler =
        ^(GCKeyboardInput *input, GCControllerButtonInput *btn, GCKeyCode keyCode, BOOL pressed) {
            yetty_kb_log("GCK keyCode=%ld pressed=%d pipe=%p",
                         (long)keyCode, pressed, (void *)(self_ ? self_->_pipe : NULL));
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

    /* Printable characters are NOT handled here. GCKeyboard delivers raw
     * HID positions only — no system-layout translation — so we'd ignore
     * the user's tvOS keyboard-layout setting (Dvorak/QWERTY/etc.).
     * pressesBegan: handles them instead via UIKey.characters, which is
     * already layout-translated by the OS. (void) suppresses unused-arg
     * warning on `input`. */
    (void)input;
}

#pragma mark - UIKeyInput

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (BOOL)hasText {
    return YES;
}

- (void)insertText:(NSString *)text {
    yetty_kb_log("insertText: '%s' pipe=%p", [text UTF8String], (void *)_pipe);
    if (!_pipe) return;

    ydebug("insertText: %s", [text UTF8String]);

    /* Send each character as YETTY_EVENT_CHAR */
    NSUInteger len = [text length];
    for (NSUInteger i = 0; i < len; i++) {
        unichar ch = [text characterAtIndex:i];
        struct yetty_yui_event ev = {0};
        ev.type = YETTY_YCORE_CHAR;
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
    yetty_kb_log("pressesBegan: count=%lu pipe=%p", (unsigned long)presses.count, (void *)_pipe);
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

        /* Special keys (arrows, return, ...) are handled by GCKeyboard's
         * keyChangedHandler — no need to forward them again here, that
         * was the source of the doubled-character bug. We only forward
         * printable characters from this path because UIKey.characters
         * is the only API that gives us the OS-layout-translated string
         * (e.g. Dvorak / QWERTY chosen in tvOS Settings). */
        if (glfw_key) continue;

        /* Plain printable character(s) — feed each as YETTY_YCORE_CHAR. */
        for (NSUInteger i = 0; i < chars.length; i++) {
            unichar ch = [chars characterAtIndex:i];
            struct yetty_yui_event ev = {0};
            ev.type = YETTY_YCORE_CHAR;
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
    /* iOS sandbox swallows stderr → ytrace_output's fprintf(stderr) goes
     * nowhere. Redirect stderr to $HOME/tmp/yetty-trace.log so the trace
     * stream is fetchable via devicectl. Enable all ytrace points so
     * frame_render: / on_pty_pipe_read: / etc. all emit. */
    {
        const char *home = getenv("HOME");
        char path[1024];
        snprintf(path, sizeof(path), "%s/tmp/yetty-trace.log", home ? home : ".");
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDERR_FILENO);
            close(fd);
            setvbuf(stderr, NULL, _IOLBF, 0);
        }
        setenv("YTRACE_DEFAULT_ON", "yes", 1);
    }
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([YettyAppDelegate class]));
    }
}
