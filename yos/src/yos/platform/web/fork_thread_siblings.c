/* Regression for the memPool-reuse path (issue #23). TWO sequential fork
 * children from the same parent reuse the engine's per-depth child memory,
 * and EACH creates a thread. This guards that the second sibling's worker
 * pool never collides with the first sibling's (asynchronously terminated)
 * workers on the shared control region: each sibling's thread must observe
 * ITS OWN process marker and run exactly once. Built like threads_demo with
 * a `wasm-opt --asyncify` post-pass (fork is driven by asyncify); all state
 * is zero-init BSS (no data-segment init under -nostdlib shared memory).
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

static volatile int process_marker;
static volatile int thread_observed;
static volatile int thread_runs;

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

__attribute__((export_name("sib_thread")))
void *sib_thread(void *arg)
{
	(void)arg;
	thread_observed = process_marker;
	thread_runs = thread_runs + 1;   /* detect double-execution */
	return 0;
}

__attribute__((export_name("__main_argc_argv")))
int __main_argc_argv(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	process_marker = 100;
	for (int i = 0; i < 2; i++) {
		int pid = fork();
		if (pid == 0) {
			process_marker = 7 + i;   /* child0 -> 7, child1 -> 8 */
			thread_observed = 0;
			thread_runs = 0;
			int tid = 0;
			pthread_create(&tid, 0, sib_thread, 0);
			pthread_join(tid, 0);
			emit_tagged('t', (unsigned)thread_observed);  /* t7 then t8 */
			emit_tagged('r', (unsigned)thread_runs);      /* r1 (no double-exec) */
			return 0;
		}
		int status = 0;
		waitpid(pid, &status, 0);
	}
	process_marker = 42;
	emit_tagged('p', (unsigned)process_marker);
	return 0;
}
