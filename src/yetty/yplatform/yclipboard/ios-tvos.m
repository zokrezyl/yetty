/*
 * yplatform/yclipboard/ios-tvos.m — UIPasteboard companion for the iOS/tvOS
 * clipboard subclass (yplatform/yclipboard/ios-tvos.c).
 *
 * UIKit's UIPasteboard must be touched on the main thread, so both operations
 * dispatch onto the main queue. set_text is fire-and-forget. request_paste reads
 * the pasteboard and posts a YETTY_YCORE_PASTE event (payload = heap UTF-8 text,
 * which the event-loop receiver frees) onto the borrowed response pipe — the
 * same delivery contract the desktop glfw clipboard uses.
 *
 * tvOS has no UIPasteboard, so the operations degrade to no-ops there;
 * request_paste still posts an empty paste so the requester completes.
 */
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if !TARGET_OS_TV
#import <UIKit/UIKit.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>

/* Post a paste result onto the borrowed response pipe; the receiver owns and
 * frees `copy`. On a failed/short write the bytes never leave, so free here. */
static void ios_clipboard_post_paste(struct yetty_ycore_xthread_event_pipe *response_pipe,
                                     char *copy)
{
    struct yetty_yui_event response = {.type = YETTY_YCORE_PASTE, .payload = copy};
    struct yetty_ycore_size_result write_result =
        response_pipe->ops->write(response_pipe, &response, sizeof(response));
    if (YETTY_IS_ERR(write_result) || write_result.value != sizeof(response)) {
        free(copy);
    }
}

int yetty_yplatform_ios_clipboard_set_text(const char *text, size_t len)
{
#if TARGET_OS_TV
    (void)text;
    (void)len;
    return 0; /* tvOS has no system clipboard */
#else
    if (!text) {
        return -1;
    }
    NSString *string = [[NSString alloc] initWithBytes:text
                                                length:len
                                              encoding:NSUTF8StringEncoding];
    if (!string) {
        return -1;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        UIPasteboard.generalPasteboard.string = string;
    });
    return 0;
#endif
}

int yetty_yplatform_ios_clipboard_request_paste(
    struct yetty_ycore_xthread_event_pipe *response_pipe)
{
    if (!response_pipe) {
        return -1;
    }
#if TARGET_OS_TV
    ios_clipboard_post_paste(response_pipe, NULL); /* no clipboard on tvOS */
    return 0;
#else
    dispatch_async(dispatch_get_main_queue(), ^{
        char *copy = NULL;
        @autoreleasepool {
            const char *utf8 = UIPasteboard.generalPasteboard.string.UTF8String;
            if (utf8) {
                size_t copy_len = strlen(utf8);
                copy = malloc(copy_len + 1);
                if (copy) {
                    memcpy(copy, utf8, copy_len + 1);
                }
            }
        }
        ios_clipboard_post_paste(response_pipe, copy);
    });
    return 0;
#endif
}
