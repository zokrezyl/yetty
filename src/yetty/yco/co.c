/*
 * yco/co.c - libco wrappers with ydebug instrumentation.
 *
 * Each wrapper does the libco call, sandwiched by ydebug. Use these instead of
 * libco directly until the coroutine plumbing is stable; then the wrappers can
 * be inlined or compiled out.
 */

#include <yetty/yco/co.h>
#include <yetty/ytrace/ytrace.h>

#include <libco.h>

yetty_yco_thread yetty_yco_active(void)
{
    yetty_yco_thread t = co_active();
    ydebug("yco_active -> %p", t);
    return t;
}

yetty_yco_thread yetty_yco_create(unsigned int stack_size, void (*entry)(void))
{
    ydebug("yco_create stack=%u entry=%p", stack_size, (void *)entry);
    yetty_yco_thread t = co_create(stack_size, entry);
    ydebug("yco_create -> %p", t);
    return t;
}

void yetty_yco_delete(yetty_yco_thread thread)
{
    ydebug("yco_delete %p", thread);
    co_delete(thread);
}

void yetty_yco_switch(yetty_yco_thread thread)
{
    yetty_yco_thread from = co_active();
    ydebug("yco_switch %p -> %p", from, thread);
    co_switch(thread);
    ydebug("yco_switch resumed (now on %p, came back from %p)", co_active(), thread);
}
