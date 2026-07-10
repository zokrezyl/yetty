/* launcher.m — tvOS app bundle entry for yos. Same shape as
 * ../yetty/build-tools/ios-tvos/tvos/qemu/tvos.m: the bundle's main()
 * creates a UITextView, redirects stdout/stderr through a socketpair
 * into a reader thread that mirrors to the view AND a logfile in the
 * app container, then kicks off yos_main on a worker thread with
 * YTRACE_DEFAULT_ON so the trace surfaces on screen.
 *
 * Why this exists:
 *   - tvOS bundle stdio is wired to /dev/null by default; any printf
 *     yos does would vanish without the redirect.
 *   - We can't enable a debugger remotely, so the on-screen UITextView
 *     IS the debugging surface — paired with the log file you can
 *     `xcrun devicectl device copy from` back to the Mac.
 *   - tvOS suspends background apps after ~30s; the silent-audio
 *     keep-alive (UIBackgroundMode=audio) holds the runtime + the
 *     wasm telnet listener open even when the user navigates away.
 *
 * Paths inside the bundle:
 *   <bundle>/yos                 this binary
 *   <bundle>/libexec/*.wasm      wasm tools (telnetd, zsh, runit, …)
 *   <bundle>/runit/example/…     runit service tree
 * Writeable storage:
 *   $HOME/Documents/yos-logs/    --log-dir
 *   $HOME/tmp/yos-status.txt     step-tracer (pull back via devicectl)
 *   $HOME/tmp/yos-output.log     full stderr mirror
 */

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>

extern int yos_main(int argc, char **argv);

/* ── step tracer ───────────────────────────────────────────────────── */

static const char *yos_status_path(void) {
    static char buf[1024];
    if (!buf[0]) {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        char dir[1024];
        snprintf(dir, sizeof dir, "%s/tmp", home);
        mkdir(dir, 0755);
        snprintf(buf, sizeof buf, "%s/yos-status.txt", dir);
    }
    return buf;
}
static void yos_status(const char *s) {
    int fd = open(yos_status_path(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    char buf[256];
    int n = snprintf(buf, sizeof buf, "%ld %s\n", (long)time(NULL), s);
    if (n > 0) (void)!write(fd, buf, (size_t)n);
    close(fd);
}

/* ── on-screen UI ──────────────────────────────────────────────────── */

/* tvOS UITextView is not focusable by default — the Siri Remote can't
 * touch it and the log stays pinned to the bottom. Subclass so the
 * focus engine offers it the focus, and consume swipe-up / swipe-down
 * / arrow presses to scroll the contentOffset. Menu (the back button)
 * is left to bubble so the user can exit / sleep the app normally.
 *
 * Auto-follow heuristic: we keep auto-scrolling to the tail every time
 * a new line arrives UNTIL the user explicitly scrolls; once they've
 * paused the tail, new lines append but the view stays where they put
 * it. Clicking the Siri-Remote click pad re-enables tail-follow. */
@interface YosLogView : UITextView
@property (nonatomic) BOOL followTail;
@end

@implementation YosLogView
- (instancetype)initWithFrame:(CGRect)frame textContainer:(NSTextContainer *)tc {
    self = [super initWithFrame:frame textContainer:tc];
    if (self) {
        self.followTail = YES;
    }
    return self;
}
- (BOOL)canBecomeFocused { return YES; }

- (void)pressesBegan:(NSSet<UIPress *> *)presses
           withEvent:(UIPressesEvent *)event
{
    BOOL consumed = NO;
    /* A full "page" of the visible area, minus a small overlap so the
     * user keeps context across the jump. */
    CGFloat page = self.bounds.size.height - 40.0;
    if (page < 60.0) page = 60.0;
    CGFloat line = self.font.lineHeight ?: 24.0;

    for (UIPress *p in presses) {
        switch (p.type) {
        case UIPressTypeUpArrow:
        case UIPressTypeLeftArrow:
            /* one line up — fine-grained scroll */
            [self scrollByDy:-line];
            self.followTail = NO;
            consumed = YES;
            break;
        case UIPressTypeDownArrow:
        case UIPressTypeRightArrow:
            [self scrollByDy:+line];
            /* If they scrolled to (or past) the bottom, re-arm tail-follow
             * so subsequent lines auto-scroll again. */
            if ([self atTail]) self.followTail = YES;
            consumed = YES;
            break;
        case UIPressTypePlayPause:
            /* Big jump — half a page each press of play/pause. */
            [self scrollByDy:+page * 0.5];
            if ([self atTail]) self.followTail = YES;
            consumed = YES;
            break;
        case UIPressTypeSelect:
            /* Click the touchpad → jump to tail and re-arm follow. */
            [self scrollToTailAnimated:YES];
            self.followTail = YES;
            consumed = YES;
            break;
        default:
            break;
        }
    }
    if (!consumed) [super pressesBegan:presses withEvent:event];
}

- (void)scrollByDy:(CGFloat)dy {
    CGPoint o = self.contentOffset;
    CGFloat maxY = MAX(0, self.contentSize.height - self.bounds.size.height);
    o.y = MAX(0, MIN(maxY, o.y + dy));
    [self setContentOffset:o animated:YES];
}
- (BOOL)atTail {
    CGFloat maxY = MAX(0, self.contentSize.height - self.bounds.size.height);
    return self.contentOffset.y >= maxY - 4.0;
}
- (void)scrollToTailAnimated:(BOOL)animated {
    NSRange end = NSMakeRange(self.text.length, 0);
    if (animated) [self scrollRangeToVisible:end];
    else {
        CGFloat maxY = MAX(0, self.contentSize.height - self.bounds.size.height);
        self.contentOffset = CGPointMake(0, maxY);
    }
}
@end

@interface YosAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow    *window;
@property (strong, nonatomic) YosLogView  *textView;
@end

static YosAppDelegate *g_delegate;

static void yos_append(NSString *s) {
    if (!s) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        YosLogView *tv = g_delegate.textView;
        if (!tv) return;
        /* Trim to last 64 KiB so very chatty ytrace doesn't OOM the view. */
        NSString *next = [tv.text stringByAppendingString:s];
        if (next.length > 65536) {
            next = [next substringFromIndex:next.length - 65536];
        }
        tv.text = next;
        /* Only auto-scroll if the user hasn't taken manual control. They
         * can re-arm tail-follow by clicking the touchpad (UIPressTypeSelect). */
        if (tv.followTail) {
            [tv scrollRangeToVisible:NSMakeRange(tv.text.length, 0)];
        }
    });
}

/* ── stdio reader ──────────────────────────────────────────────────── */

static void *yos_reader(void *arg) {
    int fd = (int)(intptr_t)arg;
    const char *home = getenv("HOME"); if (!home) home = ".";
    char log_path[1024];
    snprintf(log_path, sizeof log_path, "%s/tmp/yos-output.log", home);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        if (log_fd >= 0) (void)!write(log_fd, buf, (size_t)n);
        buf[n] = 0;
        NSString *s = [[NSString alloc] initWithBytes:buf length:(NSUInteger)n
                                             encoding:NSUTF8StringEncoding];
        if (!s) s = [[NSString alloc] initWithBytes:buf length:(NSUInteger)n
                                           encoding:NSISOLatin1StringEncoding];
        yos_append(s);
        /* DO NOT NSLog from this thread. NSLog writes to fd 2, which
         * we've redirected back into the socketpair we're reading —
         * the result is an exponential feedback loop that fills both
         * the on-screen view and the on-disk log with interleaved
         * timestamp soup. The on-screen UITextView and the
         * $HOME/tmp/yos-output.log file ARE the surfaces; the
         * unified-log fan-out is not worth the recursion risk. */
    }
    if (log_fd >= 0) close(log_fd);
    return NULL;
}

/* ── audio keep-alive ──────────────────────────────────────────────── */

static AVAudioEngine    *g_audio_engine;
static AVAudioPlayerNode *g_audio_player;

static void yos_keep_alive_with_silent_audio(void) {
    NSError *err = nil;
    AVAudioSession *session = [AVAudioSession sharedInstance];
    [session setCategory:AVAudioSessionCategoryPlayback
                    mode:AVAudioSessionModeDefault
                 options:AVAudioSessionCategoryOptionMixWithOthers
                   error:&err];
    if (err) yos_status([[NSString stringWithFormat:@"audio setCategory err: %@", err] UTF8String]);
    [session setActive:YES error:&err];
    if (err) yos_status([[NSString stringWithFormat:@"audio setActive err: %@", err] UTF8String]);

    g_audio_engine = [[AVAudioEngine alloc] init];
    g_audio_player = [[AVAudioPlayerNode alloc] init];
    [g_audio_engine attachNode:g_audio_player];

    AVAudioFormat *fmt = [[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:44100 channels:1];
    [g_audio_engine connect:g_audio_player to:g_audio_engine.mainMixerNode format:fmt];

    AVAudioPCMBuffer *buf = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:fmt frameCapacity:44100];
    buf.frameLength = 44100;
    /* All-zero buffer = silence. */

    [g_audio_engine startAndReturnError:&err];
    if (err) yos_status([[NSString stringWithFormat:@"audio engine start err: %@", err] UTF8String]);
    [g_audio_player scheduleBuffer:buf atTime:nil
                           options:AVAudioPlayerNodeBufferLoops
                 completionHandler:nil];
    [g_audio_player play];
    yos_status("silent-audio keep-alive started");
}

/* ── yos worker ────────────────────────────────────────────────────── */

static void *yos_worker(void *arg) {
    int io_fd = (int)(intptr_t)arg;

    /* Redirect stdio into the socketpair so EVERY printf/fprintf yos does
     * (and the ytrace machinery's stderr writes) flow into yos_reader →
     * UITextView + on-disk log. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    dup2(io_fd, STDOUT_FILENO);
    dup2(io_fd, STDERR_FILENO);
    yos_status("worker stdout/stderr dup2 done");

    NSBundle *bundle    = [NSBundle mainBundle];
    NSString *libexec   = [bundle.bundlePath stringByAppendingPathComponent:@"libexec"];
    NSString *runit_src = [bundle.bundlePath stringByAppendingPathComponent:@"runit"];
    NSString *home      = NSHomeDirectory();
    /* tvOS apps do NOT have write access to ~/Documents (the iOS rule
     * doesn't carry over). Caches/tmp ARE writable; use tmp/ which we
     * already proved writable above for the status + stdio mirror. */
    NSString *logdir    = [home stringByAppendingPathComponent:@"tmp/yos-logs"];
    /* runsv needs to mkdir `supervise/` inside each service dir for its
     * status sockets + lockfile. The bundle filesystem (.app/) is
     * read-only at runtime on tvOS, so mkdir there fails EPERM and
     * runsv exits 111 in a respawn loop. Stage runit/ into a writeable
     * location at launch (mirrored from the bundle) and hand THAT path
     * to runsvdir instead of the bundle's read-only one. */
    NSString *runit     = [home stringByAppendingPathComponent:@"tmp/runit"];

    NSFileManager *fm = [NSFileManager defaultManager];
    [fm createDirectoryAtPath:logdir withIntermediateDirectories:YES
                   attributes:nil error:nil];
    /* Idempotent stage: nuke any previous runit/ (including stale
     * supervise/ from earlier runs) and clone fresh from the bundle. */
    [fm removeItemAtPath:runit error:nil];
    NSError *err = nil;
    if (![fm copyItemAtPath:runit_src toPath:runit error:&err]) {
        fprintf(stderr, "[yos] failed to stage runit %s -> %s: %s\n",
                [runit_src UTF8String], [runit UTF8String],
                [err.localizedDescription UTF8String] ?: "?");
    }
    /* copyItemAtPath preserves permissions, but the bundle install
     * sometimes clears the +x bit on shell scripts. Re-arm it
     * explicitly so runsv can exec ./run and ./shim. */
    NSDirectoryEnumerator *en = [fm enumeratorAtPath:runit];
    NSString *rel;
    while ((rel = [en nextObject])) {
        NSString *base = rel.lastPathComponent;
        if ([base isEqualToString:@"run"] || [base isEqualToString:@"shim"] ||
            [base isEqualToString:@"finish"]) {
            NSString *full = [runit stringByAppendingPathComponent:rel];
            chmod([full UTF8String], 0755);
        }
    }

    setenv("YOS_PATH",          [libexec UTF8String], 1);
    setenv("YOS_LIBEXEC",       [libexec UTF8String], 1);
    setenv("PATH",              [libexec UTF8String], 1);
    setenv("LOG_DIR",           [logdir UTF8String], 1);
    setenv("YTRACE_DEFAULT_ON", "yes", 1);
    /* tvOS app sandbox has a much tighter per-process address-space
     * cap than a desktop. yos's default linear-memory size is 256 MiB
     * per wasm process; multiplied by 5–10 concurrent forks (one per
     * accepted telnet connection that hasn't been reaped yet), we'd
     * blow past the jetsam limit and ResizeMemory starts failing.
     * 64 MiB per process easily covers zsh + supervisor + tcpserver
     * while leaving room for 8+ live children. Read by main.c during
     * load_wasm_module. */
    setenv("YOS_WASM_PAGES", "1024", 1);

    NSString *runsvdir = [libexec stringByAppendingPathComponent:@"runsvdir"];
    const char *argv[] = {
        "yos",
        "--server",
        "--log-dir", [logdir UTF8String],
        [runsvdir UTF8String],
        "-P", [runit UTF8String],
        NULL,
    };
    int argc = (int)((sizeof argv / sizeof argv[0]) - 1);

    fprintf(stdout, "[yos] worker about to call yos_main: libexec=%s runit=%s\n",
            [libexec UTF8String], [runit UTF8String]);
    fflush(stdout);
    yos_status("calling yos_main");

    int rc = yos_main(argc, (char **)argv);

    fprintf(stdout, "[yos] yos_main returned rc=%d\n", rc);
    fflush(stdout);
    yos_status("yos_main returned");

    /* yos_main returning is unusual (the server loops forever). Park so
     * the UI keeps showing the last frames of the log. */
    for (;;) pause();
    return NULL;
}

/* ── app delegate ──────────────────────────────────────────────────── */

@implementation YosAppDelegate
- (BOOL)application:(UIApplication *)application
      didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    yos_status("didFinishLaunching enter");
    yos_keep_alive_with_silent_audio();
    g_delegate = self;

    UIScreen *screen = UIScreen.mainScreen;
    self.window = [[UIWindow alloc] initWithFrame:screen.bounds];

    UIViewController *vc = [[UIViewController alloc] init];
    UIView *root = vc.view;
    root.backgroundColor = UIColor.blackColor;

    self.textView = [[YosLogView alloc] initWithFrame:root.bounds];
    self.textView.selectable = NO;   /* `editable` is iOS-only on UITextView */
    self.textView.font = [UIFont monospacedSystemFontOfSize:24
                                                     weight:UIFontWeightRegular];
    self.textView.backgroundColor = UIColor.blackColor;
    self.textView.textColor = [UIColor colorWithRed:0.85 green:1.0
                                               blue:0.85 alpha:1.0];
    self.textView.text = @"yos — bringing up wasm runtime…\n"
                         @"(Siri Remote: swipe / arrows = scroll, "
                         @"click = jump to tail, play/pause = page)\n\n";
    self.textView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                     UIViewAutoresizingFlexibleHeight;
    [root addSubview:self.textView];

    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    /* The log view is the only focusable thing on screen, so the focus
     * engine will land on it automatically once the window is key. No
     * preferredFocusEnvironments override needed. */
    yos_status("window keyAndVisible");

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        yos_status("socketpair FAILED");
        yos_append(@"socketpair failed\n");
        return YES;
    }
    pthread_t rt;
    int rc_r = pthread_create(&rt, NULL, yos_reader, (void *)(intptr_t)fds[0]);
    if (rc_r != 0) {
        yos_status("reader pthread_create FAILED");
        yos_append([NSString stringWithFormat:
                    @"reader pthread_create failed rc=%d\n", rc_r]);
        close(fds[0]); close(fds[1]);
        return YES;
    }
    pthread_detach(rt);
    yos_status("reader spawned");

    /* Slight delay so the UITextView is up before yos's startup logs
     * begin pouring in — pure cosmetics. */
    int wfd = fds[1];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        pthread_t wt;
        int rc_w = pthread_create(&wt, NULL, yos_worker, (void *)(intptr_t)wfd);
        if (rc_w != 0) {
            yos_status("worker pthread_create FAILED");
            yos_append([NSString stringWithFormat:
                        @"worker pthread_create failed rc=%d\n", rc_w]);
            close(wfd);
            return;
        }
        pthread_detach(wt);
        yos_status("yos worker spawned");
    });

    yos_status("didFinishLaunching return YES");
    return YES;
}
@end

int main(int argc, char *argv[]) {
    unlink(yos_status_path());     /* fresh status file per launch */
    yos_status("main entered");
    Class delegateCls = [YosAppDelegate class];
    NSString *clsName = NSStringFromClass(delegateCls);
    yos_status(clsName ? [clsName UTF8String] : "delegate class NIL");
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, clsName);
    }
}
