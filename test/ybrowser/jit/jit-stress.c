/*
 * Baseline-JIT differential + lifetime stress harness.
 *
 * 1. Differential: each program is evaluated in a fresh interpreter-only
 *    runtime and a fresh eager-JIT runtime; the results must stringify
 *    identically. Any divergence is a JIT miscompile.
 * 2. Stress: many create / evaluate / destroy cycles with eager JIT, to
 *    surface stale native entries, runtime-lifetime bugs, and (under
 *    ASan) leaks.
 *
 * Build (sanitized):
 *   cc -O1 -g -fsanitize=address,undefined -DQJS_ENABLE_JIT \
 *      -DSLJIT_CONFIG_AUTO=1 -D_GNU_SOURCE -Isrc/quickjs \
 *      -Isrc/quickjs/sljit/sljit_src -o tmp/jit-stress \
 *      test/ybrowser/jit/jit-stress.c src/quickjs/quickjs.c \
 *      src/quickjs/dtoa.c src/quickjs/libregexp.c src/quickjs/libunicode.c \
 *      src/quickjs/sljit/sljit_src/sljitLir.c -lm -ldl -lpthread
 */
#include <quickjs.h>
#include <quickjs-jit.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;

static const char *PROGRAMS[] = {
    "(function(){return 1+2*3-4;})()",
    "(function(n){var s=0;for(var i=0;i<n;i++)s=s+i;return s;})(1000)",
    "(function(n){var p=1;for(var i=1;i<=n;i++)p=(p*i)&0x7fffffff;return p;})(15)",
    "(function(){var a=[];for(var i=0;i<50;i++)a[i]=i*i;var s=0;"
    "for(var j=0;j<a.length;j++)s=s+a[j];return s;})()",
    "(function(){var o={x:0};for(var i=0;i<100;i++)o.x=o.x+i;return o.x;})()",
    "(function(n){var a=0,b=1;for(var i=0;i<n;i++){var t=a+b;a=b;b=t;}return a;})(30)",
    "(function(){function f(x){return x<2?x:f(x-1)+f(x-2);}return f(18);})()",
    "(function(){var s='';for(var i=0;i<20;i++)s=s+'ab';return s.length;})()",
    "(function(){var c=0;var g=function(){c=c+1;return c;};g();g();return g();})()",
    "(function(){var o={v:1,step:function(){this.v=this.v*2;return this.v;}};"
    "var r=0;for(var i=0;i<10;i++)r=o.step();return r;})()",
    "(function(){var x=0;var i=0;while(i<1000){if((i&1)===0)x=x+i;i=i+1;}return x;})()",
    "(function(){try{var g=function(){return z;let z=1;};return g();}catch(e){return -1;}})()",
    "(function(){var m=0;for(var i=0;i<30;i++)for(var j=0;j<30;j++)if(i>j)m=m+1;return m;})()",
    "(function(){function Pt(x,y){this.x=x;this.y=y;}var p=new Pt(3,4);"
    "return p.x*p.x+p.y*p.y;})()",
    "(function(){var s=0;for(var i=1;i<=100;i++){if(i%3===0||i%5===0)s=s+i;}return s;})()",
    "(function(){var a=[5,3,8,1,9,2];for(var i=0;i<a.length;i++)"
    "for(var j=i+1;j<a.length;j++)if(a[j]<a[i]){var t=a[i];a[i]=a[j];a[j]=t;}"
    "return a[0]*100+a[5];})()",
    "(function(){var v=1.5;for(var i=0;i<10;i++)v=v*1.1;return (v*1000)|0;})()",
    "(function(){var s=0,i=0;do{s=s+i;i=i+1;}while(i<50);return s;})()",
};
#define NUM_PROGRAMS ((int)(sizeof(PROGRAMS) / sizeof(PROGRAMS[0])))

static char *eval_to_string(int mode, const char *src)
{
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    char *out;
    JSValue v;
    const char *s;

    JS_JITSetMode(rt, mode);
    v = JS_Eval(ctx, src, strlen(src), "<s>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JSValue e = JS_GetException(ctx);
        s = JS_ToCString(ctx, e);
        out = strdup(s ? s : "<exc>");
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, e);
    } else {
        s = JS_ToCString(ctx, v);
        out = strdup(s ? s : "<?>");
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, v);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return out;
}

static void differential(void)
{
    int i;
    printf("== differential (interpreter vs eager JIT) ==\n");
    for (i = 0; i < NUM_PROGRAMS; i++) {
        char *interp = eval_to_string(JS_JIT_MODE_OFF, PROGRAMS[i]);
        char *jit = eval_to_string(JS_JIT_MODE_EAGER, PROGRAMS[i]);
        if (strcmp(interp, jit) != 0) {
            printf("FAIL prog %d: interp=%s jit=%s\n", i, interp, jit);
            failures++;
        } else {
            printf("ok   prog %2d -> %s\n", i, interp);
        }
        free(interp);
        free(jit);
    }
}

static void stress(int cycles)
{
    int i, c;
    printf("== stress: %d create/eval/destroy cycles (eager) ==\n", cycles);
    for (c = 0; c < cycles; c++) {
        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = JS_NewContext(rt);
        JS_JITSetMode(rt, JS_JIT_MODE_EAGER);
        for (i = 0; i < NUM_PROGRAMS; i++) {
            JSValue v = JS_Eval(ctx, PROGRAMS[i], strlen(PROGRAMS[i]), "<s>", JS_EVAL_TYPE_GLOBAL);
            JS_FreeValue(ctx, v);
        }
        /* Interleave pending jobs / GC. */
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
    printf("ok   %d cycles completed without crash/leak\n", cycles);
}

int main(int argc, char **argv)
{
    int cycles = argc > 1 ? atoi(argv[1]) : 200;
    differential();
    stress(cycles);
    if (failures) {
        printf("\nFAILED: %d divergence(s)\n", failures);
        return 1;
    }
    printf("\nPASS: interpreter and JIT agree; stress clean\n");
    return 0;
}
