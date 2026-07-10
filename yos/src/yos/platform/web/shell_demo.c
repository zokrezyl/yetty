/* Interactive guest stand-in: a minimal line shell. NOT zsh — zsh needs
 * the full 169-import surface (fork/exec, termios, VFS, FILE*). This is
 * the skeleton that surface plugs into: a guest that takes keystrokes,
 * edits a line, and writes output, entirely through the wasm bridge.
 *
 * Input is push-driven (the terminal calls feed_byte per keystroke) so
 * this milestone needs no blocking read() — that arrives with the PTY +
 * asyncify step. Output goes through env.write, same as any guest. */

typedef unsigned long size_t;
typedef int           ssize_t;

__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t count);

static size_t length_of(const char *string)
{
	const char *cursor = string;
	while (*cursor)
		cursor++;
	return (size_t)(cursor - string);
}

static void put(const char *string)
{
	write(1, string, length_of(string));
}

static char line[256];
static unsigned line_len;

static int equals(const char *a, const char *b)
{
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int starts_with(const char *string, const char *prefix)
{
	while (*prefix) {
		if (*string != *prefix)
			return 0;
		string++;
		prefix++;
	}
	return 1;
}

static void prompt(void)
{
	put("\x1b[38;2;107;168;146myos-web$\x1b[0m ");
}

static void run_line(void)
{
	line[line_len] = '\0';
	put("\r\n");
	if (line_len == 0) {
		/* nothing */
	} else if (equals(line, "help")) {
		put("builtins: help, echo <text>, clear\r\n");
	} else if (equals(line, "clear")) {
		put("\x1b[2J\x1b[H");
	} else if (starts_with(line, "echo ")) {
		put(line + 5);
		put("\r\n");
	} else if (equals(line, "echo")) {
		put("\r\n");
	} else {
		put("yos-web: command not found: ");
		put(line);
		put("\r\n");
	}
	line_len = 0;
	prompt();
}

/* Called once after instantiation to paint the banner + first prompt. */
__attribute__((export_name("shell_start"))) void shell_start(void)
{
	put("\x1b[38;2;116;197;165myos\x1b[0m in the browser — guest wasm "
	    "through a wasm bridge\r\n");
	put("type 'help'. this is a stand-in shell; zsh is the full port.\r\n\r\n");
	prompt();
}

/* One keystroke from the terminal. */
__attribute__((export_name("feed_byte"))) void feed_byte(int byte)
{
	if (byte == '\r' || byte == '\n') {
		run_line();
	} else if (byte == 0x7f || byte == 0x08) {
		if (line_len > 0) {
			line_len--;
			put("\b \b");
		}
	} else if (byte >= 0x20 && byte < 0x7f) {
		if (line_len < sizeof(line) - 1) {
			line[line_len++] = (char)byte;
			char echo[2] = { (char)byte, 0 };
			put(echo);
		}
	}
}
