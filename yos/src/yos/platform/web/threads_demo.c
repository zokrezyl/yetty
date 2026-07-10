/* Real-threads proof. Compiled with shared memory + atomics so every
 * instance (main + one per worker thread) addresses the SAME linear
 * memory. pthread_* are env imports: the host implements pthread_create
 * by spawning a real Worker that runs the thread function against the
 * shared memory, and the mutex via Atomics on the shared buffer.
 *
 * run_racy:   N threads each do counter++ M times with NO lock → real
 *             parallel races lose updates → final < N*M (proof the
 *             threads truly run concurrently, not sequentially).
 * run_locked: same, but under a mutex → final == N*M exactly. */

typedef unsigned long size_t;

__attribute__((import_module("env"), import_name("pthread_create")))
int pthread_create(int *thread, void *attr, void *(*start)(void *), void *arg);
__attribute__((import_module("env"), import_name("pthread_join")))
int pthread_join(int thread, void **retval);
__attribute__((import_module("env"), import_name("pthread_mutex_lock")))
int pthread_mutex_lock(int *mutex);
__attribute__((import_module("env"), import_name("pthread_mutex_unlock")))
int pthread_mutex_unlock(int *mutex);
__attribute__((import_module("env"), import_name("write")))
long write(int fd, const void *buf, size_t count);

enum { NTHREADS = 4, ITERS = 200000 };

static volatile int counter;
static int mutex;
static int use_lock;
static int barrier; /* threads spin here until all have arrived */

__attribute__((export_name("thread_fn"))) void *thread_fn(void *arg)
{
	/* Start barrier: every thread parks here until all NTHREADS have
	 * arrived, so the loop below runs with maximal real overlap — that
	 * is what turns the unsynchronized case into actual lost updates. */
	__atomic_add_fetch(&barrier, 1, __ATOMIC_SEQ_CST);
	while (__atomic_load_n(&barrier, __ATOMIC_SEQ_CST) < NTHREADS) { /* spin */ }

	for (int i = 0; i < ITERS; i++) {
		if (use_lock) {
			pthread_mutex_lock(&mutex);
			counter++;
			pthread_mutex_unlock(&mutex);
		} else {
			/* deliberately unsynchronized read-modify-write */
			int value = counter;
			counter = value + 1;
		}
	}
	return 0;
}

static void print_line(const char *label, unsigned value)
{
	char out[64];
	int n = 0;
	while (label[n]) { out[n] = label[n]; n++; }
	char digits[16];
	int d = 0;
	if (!value) digits[d++] = '0';
	while (value) { digits[d++] = (char)('0' + value % 10); value /= 10; }
	while (d) out[n++] = digits[--d];
	out[n++] = '\n';
	write(1, out, (size_t)n);
}

static int run(int with_lock)
{
	counter = 0;
	mutex = 0;
	barrier = 0;
	use_lock = with_lock;
	int threads[NTHREADS];
	for (int i = 0; i < NTHREADS; i++)
		pthread_create(&threads[i], 0, thread_fn, 0);
	for (int i = 0; i < NTHREADS; i++)
		pthread_join(threads[i], 0);
	return counter;
}

__attribute__((export_name("run_racy"))) int run_racy(void)
{
	int got = run(0);
	print_line("racy   counter = ", (unsigned)got);
	print_line("expected        = ", (unsigned)(NTHREADS * ITERS));
	return got;
}

__attribute__((export_name("run_locked"))) int run_locked(void)
{
	int got = run(1);
	print_line("locked counter = ", (unsigned)got);
	print_line("expected        = ", (unsigned)(NTHREADS * ITERS));
	return got;
}
