/* tvOS / iOS UIKit harness for qemu. Copied verbatim into qemu's
 * system/tvos-main.m by build-tools/3rdparty/qemu/_build.sh; meson.build
 * is patched (patches/0005) to compile + link it next to system/main.c.
 * Owns main(), spins up a UITextView,
 * and calls yetty_qemu_main(argc, argv) on a worker thread with stdout/
 * stderr redirected through a socketpair so qemu's output renders on the
 * Apple TV / iOS device screen. The same socketpair fd is also passed to
 * qemu via -chardev socket,fd=N so virtio-console (kernel boot output)
 * arrives on the same stream.
 */
/* Restore the real availability prohibitions before importing UIKit. The
 * apple-prohibition-stubs.h that qemu's iOS/tvOS branch force-includes
 * neutralizes __TVOS_PROHIBITED so glib/qemu C code can reference fork/etc.
 * — but that also un-hides UIKit classes like UIDocumentInteractionController
 * that pull in iOS-only protocols (UIActionSheetDelegate). Re-arm the
 * prohibition just for this translation unit so UIKit's umbrella header
 * compiles cleanly. */
#undef  __TVOS_PROHIBITED
#define __TVOS_PROHIBITED __attribute__((unavailable("not available on tvOS")))
#undef  __IOS_PROHIBITED
#define __IOS_PROHIBITED __attribute__((unavailable("not available on iOS")))
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/qos.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <time.h>

extern int yetty_qemu_main(int argc, char **argv);
extern int (*qemu_main)(void); /* qemu's internal Darwin CFRunLoop pointer */

/* Tiny step-tracer; write to the app sandbox tmp/ so we can pull it back
 * via `xcrun devicectl ... copy from --domain-type appDataContainer
 * --source tmp/yq-status.txt`. The literal /tmp on iOS is the system tmp
 * (read-only for sandboxed apps); $HOME is the app container, so
 * $HOME/tmp/yq-status.txt works. */
static const char *yq_status_path(void) {
    static char buf[1024];
    if (!buf[0]) {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        snprintf(buf, sizeof(buf), "%s/tmp/yq-status.txt", home);
    }
    return buf;
}
static void yq_status(const char *s) {
    int fd = open(yq_status_path(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%ld %s\n", (long)time(NULL), s);
    write(fd, buf, n);
    close(fd);
}

@interface YQAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) UITextView *textView;
@end

static YQAppDelegate *g_delegate;

static void yq_append(NSString *s) {
    if (!s) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        UITextView *tv = g_delegate.textView;
        if (!tv) return;
        tv.text = [tv.text stringByAppendingString:s];
        NSRange end = NSMakeRange(tv.text.length, 0);
        [tv scrollRangeToVisible:end];
    });
}

static void *yq_reader(void *arg) {
    int fd = (int)(intptr_t)arg;
    /* Mirror everything the chardev/qemu sends to a logfile in the app
     * sandbox so devicectl can pull it back even if qemu hangs/panics. */
    char log_path[1024];
    const char *home = getenv("HOME"); if (!home) home = ".";
    snprintf(log_path, sizeof(log_path), "%s/tmp/yq-output.log", home);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char buf[2048];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        if (log_fd >= 0) write(log_fd, buf, n);
        buf[n] = '\0';
        NSString *s = [[NSString alloc] initWithBytes:buf length:n
                                             encoding:NSUTF8StringEncoding];
        if (!s) {
            s = [[NSString alloc] initWithBytes:buf length:n
                                       encoding:NSISOLatin1StringEncoding];
        }
        yq_append(s);
    }
    if (log_fd >= 0) close(log_fd);
    return NULL;
}

/* qemu's option handlers (-version / -machine help / etc.) call exit(0).
 * exit() from a worker thread tears the whole process down, including the
 * UI thread, before the user sees anything. Pin the worker here on atexit
 * so the qemu code path can't kill the app — the runloop keeps spinning. */
static void yq_block_atexit(void) {
    fflush(stdout);
    fflush(stderr);
    /* atexit runs on the calling thread (the qemu worker). Park it so the
     * subsequent _exit() never fires; main thread keeps the UI alive. */
    while (1) { pause(); }
}

static void *yq_qemu_thread(void *arg) {
    yq_status("qemu_thread enter");

    /* Pin this thread to SCHED_RR @ max priority so the scheduler hands
     * it more CPU even when YettyQemu is in background (audio mode keeps
     * us alive but the OS can still throttle non-audio threads). */
    {
        struct sched_param sp;
        memset(&sp, 0, sizeof(sp));
        sp.sched_priority = sched_get_priority_max(SCHED_RR);
        int rc = pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
        char msg[64]; snprintf(msg, sizeof(msg), "sched_setschedparam rc=%d prio=%d", rc, sp.sched_priority);
        yq_status(msg);
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    }

    int qemu_fd = (int)(intptr_t)arg;
    /* Redirect stdout/stderr through the same fd so qemu's own messages
     * (option parsing errors, OpenSBI banner if -serial chardev:char0) all
     * land in the UITextView. Line-buffer stdout so we don't sit on output
     * forever waiting for a full block. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    dup2(qemu_fd, STDOUT_FILENO);
    dup2(qemu_fd, STDERR_FILENO);
    yq_status("stdout/stderr dup2 done");

    /* Skip the Darwin CFRunLoopRun branch in system/main.c — we're already
     * on a worker thread, run qemu_default_main here directly. */
    qemu_main = NULL;

    /* Block process termination from qemu's exit() calls. */
    atexit(yq_block_atexit);

    fprintf(stdout, "[yetty] starting qemu_main\n");
    fflush(stdout);

    /* Locate OpenSBI/kernel left over in the app's data container by a
     * previous yetty install (same bundle ID). $HOME on iOS sandbox is
     * the per-app container root. */
    const char *home = getenv("HOME");
    if (!home) home = ".";
    static char bios_path[1024], kernel_path[1024], disk_path[1024];
    snprintf(bios_path, sizeof(bios_path),
             "%s/Library/Caches/yetty/yemu/opensbi-fw_dynamic.bin", home);
    snprintf(kernel_path, sizeof(kernel_path),
             "%s/Library/Caches/yetty/yemu/kernel-riscv64.bin", home);
    snprintf(disk_path, sizeof(disk_path),
             "%s/Library/Caches/yetty/yemu/alpine-rootfs.img", home);
    int has_bios = (access(bios_path, R_OK) == 0);
    int has_kernel = (access(kernel_path, R_OK) == 0);
    int has_disk = (access(disk_path, R_OK) == 0);
    fprintf(stdout, "[yetty] bios=%s (%s)\n", bios_path, has_bios ? "found" : "missing");
    fprintf(stdout, "[yetty] kernel=%s (%s)\n", kernel_path, has_kernel ? "found" : "missing");
    fprintf(stdout, "[yetty] disk=%s (%s)\n", disk_path, has_disk ? "found" : "missing");
    fflush(stdout);

    /* Wire qemu's UART (-serial) to the same socketpair fd; OpenSBI prints
     * its banner there, kernel earlycon does too. */
    static char chardev_arg[128];
    snprintf(chardev_arg, sizeof(chardev_arg),
             "socket,id=char0,fd=%d,server=off", qemu_fd);
    static char drive_arg[1280];
    snprintf(drive_arg, sizeof(drive_arg),
             "file=%s,if=none,format=raw,id=hd0", disk_path);

    char *argv_no_kernel[] = {
        (char *)"qemu-system-riscv64",
        (char *)"-machine", (char *)"virt",
        (char *)"-bios",    bios_path,
        (char *)"-display", (char *)"none",
        (char *)"-chardev", chardev_arg,
        (char *)"-serial",  (char *)"chardev:char0",
        (char *)"-no-reboot",
        (char *)"-no-shutdown",
        NULL,
    };
    char *argv_with_kernel[] = {
        (char *)"qemu-system-riscv64",
        (char *)"-machine", (char *)"virt",
        (char *)"-bios",    bios_path,
        (char *)"-kernel",  kernel_path,
        (char *)"-append",  (char *)"earlycon=sbi console=ttyS0",
        (char *)"-display", (char *)"none",
        (char *)"-chardev", chardev_arg,
        (char *)"-serial",  (char *)"chardev:char0",
        (char *)"-no-reboot",
        (char *)"-no-shutdown",
        NULL,
    };
    /* Match yetty's qemu config: kernel console = virtio-console (hvc0),
     * not the UART. The alpine-extended init expects /dev/hvc0 — booting
     * with console=ttyS0 makes init bail with "unable to open initial
     * console" → kernel panic. We lose the OpenSBI banner this way (no
     * UART chardev to render it), but kernel + init talk on hvc0. */
    /* Slirp user-net + hostfwd: forward host TCP 2323 → guest TCP 23 so the
     * companion yetty.app (--telnet 127.0.0.1:2323) reaches the in-guest
     * telnetd on port 23. virtio-console (hvc0) stays as the kernel-boot
     * console rendered into the local UITextView. */
    char *argv_with_disk[] = {
        (char *)"qemu-system-riscv64",
        (char *)"-machine", (char *)"virt",
        (char *)"-m",       (char *)"256",
        /* Bigger TB cache = fewer re-translations of hot loops under TCI. */
        (char *)"-accel",   (char *)"tcg,tb-size=128",
        (char *)"-bios",    bios_path,
        (char *)"-kernel",  kernel_path,
        (char *)"-append",  (char *)"earlycon=sbi console=hvc0 root=/dev/vda rw init=/init",
        (char *)"-drive",   drive_arg,
        (char *)"-device",  (char *)"virtio-blk-device,drive=hd0",
        (char *)"-device",  (char *)"virtio-serial-device",
        (char *)"-device",  (char *)"virtconsole,chardev=char0",
        (char *)"-chardev", chardev_arg,
        (char *)"-netdev",  (char *)"user,id=net0,hostfwd=tcp::2323-:23",
        (char *)"-device",  (char *)"virtio-net-device,netdev=net0",
        (char *)"-serial",  (char *)"none",
        (char *)"-display", (char *)"none",
        (char *)"-no-reboot",
        (char *)"-no-shutdown",
        NULL,
    };
    char **argv;
    if (has_bios && has_kernel && has_disk)      argv = argv_with_disk;
    else if (has_bios && has_kernel)             argv = argv_with_kernel;
    else if (has_bios)                            argv = argv_no_kernel;
    else {
        static char *argv_fallback[] = { (char *)"qemu-system-riscv64",
                                         (char *)"-version", NULL };
        argv = argv_fallback;
    }
    int argc = 0; while (argv[argc]) argc++;
    yq_status(has_disk ? "calling yetty_qemu_main (bios+kernel+disk)" :
              has_kernel ? "calling yetty_qemu_main (bios+kernel)" :
              has_bios ? "calling yetty_qemu_main (bios only)" :
              "calling yetty_qemu_main (-version fallback)");
    int rc = yetty_qemu_main(argc, argv);
    fprintf(stdout, "\n[yetty] qemu_main returned rc=%d\n", rc);
    fflush(stdout);
    /* If qemu_main returns instead of exit()ing, park ourselves too. */
    while (1) { pause(); }
    return NULL;
}

/* Keep YettyQemu alive while yetty.app is foreground.
 *
 * tvOS suspends background apps by default — qemu's slirp would die
 * within a few seconds, dropping yetty's --telnet 127.0.0.1:2323 socket.
 * The "audio" UIBackgroundMode (declared in Info.plist) lets a process
 * stay scheduled as long as it's actively playing audio. We start an
 * AVAudioEngine playing a silent buffer in a loop — the OS sees a
 * media app, qemu keeps running, the listener stays open. */
static AVAudioEngine *g_audio_engine;
static AVAudioPlayerNode *g_audio_player;
static void yq_keep_alive_with_silent_audio(void) {
    NSError *err = nil;
    AVAudioSession *session = [AVAudioSession sharedInstance];
    [session setCategory:AVAudioSessionCategoryPlayback
                   mode:AVAudioSessionModeDefault
                options:AVAudioSessionCategoryOptionMixWithOthers
                  error:&err];
    if (err) yq_status([[NSString stringWithFormat:@"audio setCategory err: %@", err] UTF8String]);
    [session setActive:YES error:&err];
    if (err) yq_status([[NSString stringWithFormat:@"audio setActive err: %@", err] UTF8String]);

    g_audio_engine = [[AVAudioEngine alloc] init];
    g_audio_player = [[AVAudioPlayerNode alloc] init];
    [g_audio_engine attachNode:g_audio_player];

    AVAudioFormat *fmt = [[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:44100 channels:1];
    [g_audio_engine connect:g_audio_player to:g_audio_engine.mainMixerNode format:fmt];

    AVAudioPCMBuffer *buf = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:fmt frameCapacity:44100];
    buf.frameLength = 44100;
    /* Already zeroed by AVAudioPCMBuffer; silence is what we want. */

    [g_audio_engine startAndReturnError:&err];
    if (err) yq_status([[NSString stringWithFormat:@"audio engine start err: %@", err] UTF8String]);
    [g_audio_player scheduleBuffer:buf
                            atTime:nil
                           options:AVAudioPlayerNodeBufferLoops
                 completionHandler:nil];
    [g_audio_player play];
    yq_status("silent-audio keep-alive started");
}

@implementation YQAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    yq_status("didFinishLaunching enter");
    yq_keep_alive_with_silent_audio();
    g_delegate = self;
    UIScreen *screen = UIScreen.mainScreen;
    yq_status(screen ? "got mainScreen" : "mainScreen is nil");
    self.window = [[UIWindow alloc] initWithFrame:screen.bounds];
    yq_status("window created");
    UIViewController *vc = [[UIViewController alloc] init];
    UIView *root = vc.view;
    root.backgroundColor = UIColor.blackColor;
    self.textView = [[UITextView alloc] initWithFrame:root.bounds];
    yq_status("textView created");
    /* `editable` is iOS-only — UITextView on tvOS is read-only by design. */
    self.textView.selectable = NO;
    self.textView.font = [UIFont monospacedSystemFontOfSize:24
                                                    weight:UIFontWeightRegular];
    self.textView.backgroundColor = UIColor.blackColor;
    self.textView.textColor = [UIColor colorWithRed:0.85 green:1.0
                                               blue:0.85 alpha:1.0];
    self.textView.text = @"yetty-qemu smoke\n";
    self.textView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                     UIViewAutoresizingFlexibleHeight;
    [root addSubview:self.textView];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    yq_status("window keyAndVisible");

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        yq_status("socketpair FAILED");
        yq_append(@"socketpair failed\n");
        return YES;
    }
    yq_status("socketpair ok");
    pthread_t rt;
    pthread_create(&rt, NULL, yq_reader, (void *)(intptr_t)fds[0]);
    pthread_detach(rt);
    yq_status("reader spawned");
    /* Delay the qemu thread start by 500ms so the runloop has spun up and
     * the UITextView is on screen before any qemu output / fatal arrives. */
    int qfd = fds[1];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        yq_status("dispatch_after fired");
        pthread_t qt;
        pthread_create(&qt, NULL, yq_qemu_thread, (void *)(intptr_t)qfd);
        pthread_detach(qt);
        yq_status("qemu thread spawned");
    });
    yq_status("didFinishLaunching return YES");
    return YES;
}
@end

int main(int argc, char *argv[])
{
    /* unlink at start so we get a fresh file per launch */
    unlink(yq_status_path());
    yq_status("main entered");
    Class delegateCls = [YQAppDelegate class];
    yq_status(delegateCls ? "got delegate class" : "delegate class is Nil");
    NSString *clsName = NSStringFromClass(delegateCls);
    yq_status([clsName UTF8String] ?: "NSStringFromClass nil");
    @autoreleasepool {
        yq_status("calling UIApplicationMain");
        int rc = UIApplicationMain(argc, argv, nil, clsName);
        char msg[64]; snprintf(msg, sizeof(msg), "UIApplicationMain returned %d", rc);
        yq_status(msg);
        return rc;
    }
}
