/* Multi-module driver. Three modules — yvehicle, yanimal, ytuning —
 * built and linked side by side. Each ships its own RPC plumbing and
 * skel table behind a single -Iinclude. main.c uses each module
 * strictly through `#include "<module>/<class>.h"`. Every public
 * stub now returns a Result, so each call site checks and either
 * propagates or absorbs at this boundary. */

#define _POSIX_C_SOURCE 200809L

#include "yvehicle/sportscar.h" /* pulls yvehicle/methods.gen.h transitively */
#include "yvehicle/rpc.gen.h"

#include "yanimal/cat.h"
#include "yanimal/dog.h"
#include "yanimal/rpc.gen.h"

#include "ytuning/tuned_sportscar.h" /* cross-domain subclass of yvehicle:sportscar */
#include "ytuning/rpc.gen.h"

#include "result.h"
#include "rpc.h"
#include "ytrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/* Absorb-at-boundary helper. main.c is the call-site for every
 * method dispatch; if any returns ERR, print the chain and keep
 * going so we still see the rest of the demo flow. */
#define ABSORB(label, res)                                                                         \
    do {                                                                                           \
        if (YETTY_IS_ERR(res)) {                                                                   \
            yetty_ycore_error_print(stderr, label, (res).error);                                   \
            yetty_ycore_error_destroy((res).error);                                                \
        }                                                                                          \
    } while (0)

static void exercise_vehicle(struct ctx *ctx, struct object *obj)
{
    struct yetty_ycore_void_result vr;
    struct yetty_ycore_int_result ir;
    struct str_result sr;

    vr = yvehicle_vehicle_ctor(ctx, obj);
    ABSORB("vehicle_ctor", vr);
    vr = yvehicle_vehicle_start(ctx, obj);
    ABSORB("vehicle_start", vr);

    ir = yvehicle_vehicle_accelerate(ctx, obj, 60.0f);
    ABSORB("accelerate", ir);
    ir = yvehicle_vehicle_accelerate(ctx, obj, 60.0f);
    ABSORB("accelerate", ir);
    ydebug("accelerate -> %d", ir.value);
    ir = yvehicle_vehicle_accelerate(ctx, obj, 60.0f);
    ABSORB("accelerate", ir);
    ydebug("accelerate -> %d", ir.value);
    ir = yvehicle_vehicle_accelerate(ctx, obj, 60.0f);
    ABSORB("accelerate", ir);
    ydebug("accelerate -> %d", ir.value);

    ir = yvehicle_vehicle_brake(ctx, obj, 0.5f);
    ABSORB("brake", ir);
    ydebug("brake -> %d", ir.value);

    sr = yvehicle_vehicle_describe(ctx, obj, 123.4f);
    ABSORB("describe", sr);
    ydebug("describe: '%s'", sr.value.buf);

    vr = yvehicle_vehicle_dtor(ctx, obj);
    ABSORB("vehicle_dtor", vr);
}

static void exercise_animal(struct ctx *ctx, struct object *obj)
{
    struct yetty_ycore_void_result vr;
    struct yetty_ycore_int_result ir;
    struct str_result sr;

    vr = yanimal_animal_ctor(ctx, obj);
    ABSORB("animal_ctor", vr);
    vr = yanimal_animal_breathe(ctx, obj);
    ABSORB("animal_breathe", vr);

    sr = yanimal_animal_speak(ctx, obj, 7);
    ABSORB("animal_speak", sr);
    ydebug("speak: '%s'", sr.value.buf);

    ir = yanimal_animal_eat(ctx, obj, 0.8f);
    ABSORB("animal_eat", ir);
    ydebug("eat -> %d", ir.value);

    vr = yanimal_animal_dtor(ctx, obj);
    ABSORB("animal_dtor", vr);
}

static void run_server(int fd)
{
    rpc_init();
    rpc_server_run(fd);
    ydebug("server exit");
}

static void run_client(int fd)
{
    struct rpc_session *s = rpc_session_create(fd);
    struct ctx local = {0};
    struct ctx remote = {.session = s};

    struct object_ptr_result obj_r;

    /* Plain base vehicle — its describe shows the PRIVATE fuel_level member
     * (no accessor is generated for it; only vehicle's own code can read
     * it). Subclasses below cannot reach fuel_level at all. */
    yinfo("=== local vehicle (yvehicle, base) ===");
    obj_r = yvehicle_vehicle_create(&local);
    if (YETTY_IS_OK(obj_r)) {
        exercise_vehicle(&local, obj_r.value);
        object_free(obj_r.value);
    } else {
        ABSORB("local vehicle create", obj_r);
    }

    yinfo("=== local sportscar (yvehicle) ===");
    obj_r = yvehicle_sportscar_create(&local);
    if (YETTY_IS_OK(obj_r)) {
        exercise_vehicle(&local, obj_r.value);
        object_free(obj_r.value);
    } else {
        ABSORB("local sportscar create", obj_r);
    }

    yinfo("=== local cat (yanimal) ===");
    obj_r = yanimal_cat_create(&local);
    if (YETTY_IS_OK(obj_r)) {
        exercise_animal(&local, obj_r.value);
        object_free(obj_r.value);
    } else {
        ABSORB("local cat create", obj_r);
    }

    yinfo("=== local tuned_sportscar (ytuning extends yvehicle) ===");
    obj_r = ytuning_tuned_sportscar_create(&local);
    if (YETTY_IS_OK(obj_r)) {
        exercise_vehicle(&local, obj_r.value);
        object_free(obj_r.value);
    } else {
        ABSORB("local tuned_sportscar create", obj_r);
    }

    yinfo("=== remote sportscar (yvehicle) ===");
    obj_r = yvehicle_sportscar_create(&remote);
    if (YETTY_IS_OK(obj_r)) {
        exercise_vehicle(&remote, obj_r.value);
        free(obj_r.value);
    } else {
        ABSORB("remote sportscar create", obj_r);
    }

    yinfo("=== remote dog (yanimal) ===");
    obj_r = yanimal_dog_create(&remote);
    if (YETTY_IS_OK(obj_r)) {
        exercise_animal(&remote, obj_r.value);
        free(obj_r.value);
    } else {
        ABSORB("remote dog create", obj_r);
    }

    yinfo("=== remote tuned_sportscar (ytuning extends yvehicle) ===");
    obj_r = ytuning_tuned_sportscar_create(&remote);
    if (YETTY_IS_OK(obj_r)) {
        exercise_vehicle(&remote, obj_r.value);
        free(obj_r.value);
    } else {
        ABSORB("remote tuned_sportscar create", obj_r);
    }

    rpc_session_destroy(s);
}

/* High-priority constructor: set YTRACE_DEFAULT_ON BEFORE the module
 * install-hook constructors (which register their first trace points
 * and snapshot the current default). Without this, every trace point
 * registered during constructor time would default to OFF and a bare
 * `./poc` would be silent. Priorities <= 100 are reserved by the
 * toolchain; 101 is the lowest user-available priority and runs
 * earliest among user constructors. */
__attribute__((constructor(101))) static void enable_ytrace_by_default(void)
{
    setenv("YTRACE_DEFAULT_ON", "yes", 0);
}

int main(void)
{
    /* Catch any points registered between this constructor and main()
     * that haven't been observed yet — set them on too. */
    ytrace_set_all_enabled(true);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair");
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        close(sv[0]);
        run_server(sv[1]);
        _exit(0);
    }
    close(sv[1]);
    run_client(sv[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}
