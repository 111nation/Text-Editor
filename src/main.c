#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#define VERSION "0.0.1"

#define TRUE 1

// e is idiomatic for editor

struct editorConfig {
	struct termios orig_termios;
	unsigned int ws_col, ws_row;
	unsigned int cx, cy;
};

struct editorConfig esettings;

typedef struct {
	char *buf;
	unsigned int len; // Excludes null terminating character
} str;

int str_init(str* self) {
	if (self == NULL) return -1;
	self->buf = NULL;
	self->len = 0;
	return 0;
}

int str_append(str* self, const char *s, unsigned int len) {
	/// Appends string s 
	/// Uses given length (Excluding null terminating character)
	if (self == NULL) return -1;
	if (s == NULL || len <= 0) return 0;

	char* newBuf = realloc(self->buf, self->len+len+1);
	if (newBuf == NULL) return -1;

	memcpy(&newBuf[self->len], s, len);
	self->buf = newBuf;
	self->len += len;
	self->buf[self->len] = '\0';

	return 0;
}

void str_free(str* self) {
	free(self->buf);
}

/* KEYS */
#define CTRL_KEY(k) ((k) & 0x1f)


void edraw_rows(str* buf) {
	for (unsigned int i = 0; i < esettings.ws_row; i++) {

		if (i == esettings.ws_row/2) {
			char welcome[80];
			int len = snprintf(welcome, sizeof(welcome), "Editor 1975 -- version %s", VERSION);

			if (len > esettings.ws_col) len = esettings.ws_col;

			int padding = (esettings.ws_col-len)/2;
			if (padding) {
				str_append(buf, "~", 1);
				padding--;
			}

			while (padding--) str_append(buf, " ", 1);
			str_append(buf, welcome, len);
		} else {
			str_append(buf, "~", 1);
		}

		str_append(buf, "\x1b[K", 3);
		if (i < esettings.ws_row-1) {
			str_append(buf, "\r\n", 2);
		}
	}
}

void clear_screen() {
	write(STDOUT_FILENO, "\x1b[?25l", 6);	
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	write(STDOUT_FILENO, "\x1b[?25h", 6);	
}

void erefresh_screen() {
	str buf;
	str_init(&buf);

	// Clear screen
	str_append(&buf, "\x1b[?25l", 6);	 // Hide cursor
	str_append(&buf, "\x1b[H", 3);

	edraw_rows(&buf);

	// Move cursor back to original position
	char temp[32];
	snprintf(temp, sizeof(temp), "\x1b[%d;%dH", esettings.cy+1, esettings.cx+1);
	str_append(&buf, temp, strlen(temp));

	str_append(&buf, "\x1b[?25h", 6);	 // Unhide cursor

	write(STDOUT_FILENO, buf.buf, buf.len);

	str_free(&buf);
}

void panic(const char *s) {
	clear_screen();
	perror(s);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH,&esettings.orig_termios) == -1) {
		panic("tcsetattr");
	}
}

void enableRawMode() {
	// Save default settings
	// Apply old terminal settings when program
	if (tcgetattr(STDIN_FILENO, &esettings.orig_termios) == -1) panic("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = esettings.orig_termios;

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

	raw.c_cc[VMIN] = 0; // read() can terminate with a min of 0 bytes recieved
	raw.c_cc[VTIME] = 1; // read() maximum wait time is 100 milliseconds

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw) == -1) panic("tcgetattr");
}

char get_key() {
	char c = '\0';
	int8_t res = read(STDIN_FILENO, &c, 1);

	// Catch read errors (If standard input is not terminal input)
	if (res < 0 && errno != EAGAIN) {
		panic("read");
	}

	return c;
}

void process_keypress(char c) {
	switch (c) {
		case CTRL_KEY('c'):
		case 'q':
			clear_screen();
			exit(0);
			break;
		case 'h':
			esettings.cx--;
			break;
		case 'j':
			esettings.cy--;
			break;
		case 'k':
			esettings.cy++;
			break;
		case 'l':
			esettings.cx++;
			break;
	}
}

void debug_keypress(char c) {
	if (c == '\0') return;

	if (iscntrl(c)) {
		printf("%d\r\n", c);
	} else {
		printf("'%c' pressed with code '%i'\r\n", c, c);
	}
}

int get_cursor_position(unsigned int *rows, unsigned int *cols) {
	// Query for cursor position
	// ANSI \x1b[6n

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	printf("\r\n");

	// Get read the terminal input (cursor position)
	// Read character for character until no input left
	char buf[32];
	unsigned int i = 0;
	while (i < sizeof(buf)-1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R' || buf[i] == 'h') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;

	// Parse the cursor positions out 
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int get_window_size(unsigned int *rows, unsigned int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// Fall back 
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return get_cursor_position(rows, cols);
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

void init() {
	esettings.cx = 1;
	esettings.cy = 0;

	enableRawMode();
	if (get_window_size(&esettings.ws_row, &esettings.ws_col) == -1) panic("get_window_size");
	clear_screen();
}


int main() {
	init();

	while (TRUE) {
		erefresh_screen();
		char c = get_key();
		debug_keypress(c);
		process_keypress(c);
	}

	return 0;
}
