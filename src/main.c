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

enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_UP,
	ARROW_DOWN,
	ARROW_RIGHT,
	DEL_KEY,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY, 
	END_KEY,
};

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

int get_key() {
	char c = '\0';
	ssize_t res = read(STDIN_FILENO, &c, 1);

	// Catch read errors (If standard input is not terminal input)
	if (res < 0 && errno != EAGAIN) {
		panic("read");
		return c;
	}

	if (c == '\x1b') {
		// Capture special keys -> Arrow keys
		char special[3];

		if (read(STDIN_FILENO, &special[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &special[1], 1) != 1) return '\x1b';

		if (special[0] == '[') {
			switch (special[1]) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}

			if (special[1] >= '0' && special[1] <= '9'){
				if (read(STDIN_FILENO, &special[2], 1) != 1) return '\x1b';
				if (special[2] != '~') return '\x1b';
				switch (special[1]) {
					case '1': case '7': return HOME_KEY;
					case '4': case '8': return END_KEY;
					case '3': return DEL_KEY;
					case '5': return PAGE_UP;
					case '6': return PAGE_DOWN;
				}
			} 
		} else if (special[0] == 'O') {
			switch (special[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}


		return '\x1b';
	}

	return c;
}

static inline int clamp(int a, int min, int max) {
	if (a < min) return min;
	if (a > max) return max;
	return a;
}

void move_cursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			esettings.cx--;
			break;
		case ARROW_UP:
			esettings.cy--;
			break;
		case ARROW_DOWN:
			esettings.cy++;
			break;
		case ARROW_RIGHT:
			esettings.cx++;
			break;
	}

	// Bounds
	esettings.cx = clamp(esettings.cx, 0, esettings.ws_col);
	esettings.cy = clamp(esettings.cy, 0, esettings.ws_row);
}

void process_keypress(int c) {
	switch (c) {
		case CTRL_KEY('c'):
		case 'q':
			clear_screen();
			exit(0);
			break;

		// NAVIAGATION
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			move_cursor(c);
			break;
		
		case HOME_KEY:
			esettings.cx = 0;
			break;
		case END_KEY:
			esettings.cx = esettings.ws_col;
			break;

		case PAGE_UP:
			esettings.cy = 0;
			break;
		case PAGE_DOWN:
			esettings.cy = esettings.ws_row;
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
		int c = get_key();
		debug_keypress(c);
		process_keypress(c);
	}

	return 0;
}
