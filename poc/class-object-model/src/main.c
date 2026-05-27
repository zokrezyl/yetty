/* Multi-module driver. Two independent modules — yvehicle and yanimal —
 * built and linked side by side. Each ships its own RPC plumbing and
 * skel table behind a single -Iinclude. main.c uses each module strictly
 * through `#include "<module>/<class>.h"`. */

#include "yvehicle/sportscar.h"   /* pulls yvehicle/methods.gen.h transitively */
#include "yvehicle/rpc.gen.h"

#include "yanimal/cat.h"
#include "yanimal/dog.h"
#include "yanimal/rpc.gen.h"

#include "rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void exercise_sportscar(struct ctx *ctx, struct object *obj)
{
    yvehicle_vehicle_ctor(ctx, obj);
    yvehicle_vehicle_start(ctx, obj);
    int a = yvehicle_vehicle_accelerate(ctx, obj, 60.0f);
    fprintf(stderr, "[caller] accelerate -> %d\n", a);
    int b = yvehicle_vehicle_brake(ctx, obj, 0.5f);
    fprintf(stderr, "[caller] brake -> %d\n", b);
    struct str d = yvehicle_vehicle_describe(ctx, obj, 123.4f);
    fprintf(stderr, "[caller] describe: '%s'\n", d.buf);
    yvehicle_vehicle_dtor(ctx, obj);
}

static void exercise_animal(struct ctx *ctx, struct object *obj)
{
    yanimal_animal_ctor(ctx, obj);
    yanimal_animal_breathe(ctx, obj);
    struct str s = yanimal_animal_speak(ctx, obj, 7);
    fprintf(stderr, "[caller] speak: '%s'\n", s.buf);
    int e = yanimal_animal_eat(ctx, obj, 0.8f);
    fprintf(stderr, "[caller] eat -> %d\n", e);
    yanimal_animal_dtor(ctx, obj);
}

static void run_server(int fd)
{
    rpc_init();
    rpc_server_run(fd);
    fprintf(stderr, "[server] exit\n");
}

static void run_client(int fd)
{
    struct rpc_session *s = rpc_session_create(fd);
    struct ctx local = {0};
    struct ctx remote = { .session = s };

    fprintf(stderr, "\n=== local sportscar (yvehicle) ===\n");
    struct object *sc = yvehicle_sportscar_create(&local);
    exercise_sportscar(&local, sc);
    object_free(sc);

    fprintf(stderr, "\n=== local cat (yanimal) ===\n");
    struct object *c = yanimal_cat_create(&local);
    exercise_animal(&local, c);
    object_free(c);

    fprintf(stderr, "\n=== remote sportscar (yvehicle) ===\n");
    struct object *rsc = yvehicle_sportscar_create(&remote);
    exercise_sportscar(&remote, rsc);
    free(rsc);

    fprintf(stderr, "\n=== remote dog (yanimal) ===\n");
    struct object *rd = yanimal_dog_create(&remote);
    exercise_animal(&remote, rd);
    free(rd);

    rpc_session_destroy(s);
}

int main(void)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair");
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) { close(sv[0]); run_server(sv[1]); _exit(0); }
    close(sv[1]);
    run_client(sv[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}
