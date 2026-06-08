#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <fcntl.h>

#define ITERATIONS 20000000

typedef struct {
    int id;
    unsigned long result;
} thread_arg_t;

int console_fd = -1;

void print(const char *s)
{
    write(console_fd, s, strlen(s));
}

void print_num(long n)
{
    char buf[32];
    char out[32];
    int i = 0, j = 0, neg = 0;
    if (n < 0) {
        neg = 1;
        n = -n;
    }
    if (n == 0) {
        write(console_fd, "0", 1);
        return;
    }
    while (n) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    if (neg) {
        out[j++] = '-';
    }
    while (i--) {
        out[j++] = buf[i];
    }
    write(console_fd, out, j);
}

void *heavy_work(void *arg)
{
    thread_arg_t *t = (thread_arg_t *)arg;
    unsigned long sum = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        sum += (i * 17 + t->id) ^ (i >> 3);
        sum ^= (sum << 5) | (sum >> 27);
    }
    t->result = sum;
    return NULL;
}

int main(int argc, char **argv)
{
    console_fd = open("/dev/console", O_WRONLY);
    if (console_fd < 0) {
        console_fd = open("/dev/hvc0", O_WRONLY);
    }
    if (console_fd < 0) {
        console_fd = 1;
    }

    int nthreads = 4;
    if (argc > 1) {
        nthreads = atoi(argv[1]);
    }

    pthread_t *threads = malloc(nthreads * sizeof(pthread_t));
    thread_arg_t *args = malloc(nthreads * sizeof(thread_arg_t));

    struct timeval start, end;
    gettimeofday(&start, NULL);

    print("Starting ");
    print_num(nthreads);
    print(" threads\n");

    for (int i = 0; i < nthreads; i++) {
        args[i].id = i;
        pthread_create(&threads[i], NULL, heavy_work, &args[i]);
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, NULL);
    long elapsed = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
    print("Total time: ");
    print_num(elapsed);
    print(" ms\n");

    free(threads);
    free(args);
    return 0;
}
