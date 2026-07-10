/* Regression guest for per-process thread-memory ownership (issue #23).
 *
 * A thread created by a fork() CHILD must run against the child's own
 * linear memory, not the root/parent address space the engine's worker
 * pool was first bound to. This guest makes that observable:
 *
 *   root main sets  process_marker = 100   (root address space)
 *   after fork the child sets process_marker = 7   (child address space)
 *   the child spawns a thread that copies process_marker -> thread_observed
 *   the child prints thread_observed  -> must be 7
 *   the parent sets process_marker = 42 and prints it -> must be 42
 *
 * If the child's thread ran against the wrong memory it would never write
 * the child's thread_observed slot, so the child would print t0 (or the
 * root's 100) instead of t7. The engine drives fork via asyncify, so this
 * is post-processed with `wasm-opt --asyncify` (see build.sh); it is built
 * -nostdlib with a shared linear memory and does NOT run the passive
 * data-segment initialiser, so it keeps ALL state in zero-init BSS and
 * avoids initialised static data (string literals included).
 */

typedef unsigned long size_t;

__attribute__((import_module("env"), import_name("write")))
long write(int fd, const void *buf, size_t count);
__attribute__((import_module("env"), import_name("fork")))
int fork(void);
__attribute__((import_module("env"), import_name("waitpid")))
int waitpid(int pid, int *status, int options);
__attribute__((import_module("env"), import_name("pthread_create")))
int pthread_create(int *thread, void *attr, void *(*start)(void *), void *arg);
__attribute__((import_module("env"), import_name("pthread_join")))
int pthread_join(int thread, void **retval);

static volatile int process_marker;   /* which address space set this */
static volatile int thread_observed;  /* value the spawned thread saw */

/* Emit "<tag><decimal>\n" using only char immediates (no string data). */
static void emit_tagged(char tag, unsigned value)
{
	char line[24];
	int n = 0;
	line[n++] = tag;
	char digits[12];
	int d = 0;
	if (!value) digits[d++] = '0';
	while (value) { digits[d++] = (char)('0' + value % 10); value /= 10; }
	while (d) line[n++] = digits[--d];
	line[n++] = '\n';
	write(1, line, (size_t)n);
}

/* Runs in a pool worker. Reads the process-global the child set and
 * records it into shared linear memory for the child main to report. */
__attribute__((export_name("child_thread")))
void *child_thread(void *arg)
{
	(void)arg;
	thread_observed = process_marker;
	return 0;
}

__attribute__((export_name("__main_argc_argv")))
int __main_argc_argv(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	process_marker = 100;              /* root/parent address space */
	int pid = fork();
	if (pid == 0) {
		process_marker = 7;        /* child's own address space */
		int tid = 0;
		pthread_create(&tid, 0, child_thread, 0);
		pthread_join(tid, 0);
		emit_tagged('t', (unsigned)thread_observed);  /* expect t7 */
		emit_tagged('c', (unsigned)process_marker);   /* expect c7 */
		return 0;
	}
	process_marker = 42;               /* parent continues after fork */
	int status = 0;
	waitpid(pid, &status, 0);
	emit_tagged('p', (unsigned)process_marker);           /* expect p42 */
	return 0;
}
