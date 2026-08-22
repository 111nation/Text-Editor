#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

#define VERSION "0.0.1"
#define LEFT_PADDING 3
#define TAB_STOP 4

#define _BSD_SOURCE
#define _GNU_SOURCE

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

typedef struct erow { // Single row data
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

struct editorConfig {
	unsigned int cx, cy; 			// Cursor position in file
	unsigned int rx; 				// Rendered cursor position in rendered file
	unsigned int ws_col, ws_row;	// Screen dimensions
	int numrows;					// Number of rows in file
	int rowoff; 					// Line number of file displayed first on screen 
	int coloff;						// Character of current line first displayed on screen
	erow* row;						// File row data
	struct termios orig_termios;	// Terminal Settings
};

struct editorConfig esettings;


/** String Handling **/
typedef struct {
	char *buf;
	unsigned int len; // Excludes null terminating character
} str;

static inline int clamp(int a, int min, int max) {
	if (a < min) return min;
	if (a > max) return max;
	return a;
}

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

/*** Keys ***/
#define CTRL_KEY(k) ((k) & 0x1f)


void edraw_rows(str* buf) {
	for (unsigned int i = 0; i < esettings.ws_row; i++) {
		int filerow = i + esettings.rowoff;
		if (filerow >= esettings.numrows) { 
			// Display empty space
			if (esettings.numrows == 0 && i == esettings.ws_row/2) {
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
		} else {
			// Display file 
			int len = esettings.row[filerow].rsize - esettings.coloff;
			if (len < 0) len = 0;
			if (len > esettings.ws_col-LEFT_PADDING) len = esettings.ws_col-LEFT_PADDING; // Truncate
			int padding = LEFT_PADDING;
			while (padding--) str_append(buf, " ", 1);
			str_append(buf, &esettings.row[filerow].render[esettings.coloff], len);
		}

		str_append(buf, "\x1b[K", 3);
		str_append(buf, "\r\n", 2);
	}
}

void clear_screen() {
	write(STDOUT_FILENO, "\x1b[?25l", 6);	
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	write(STDOUT_FILENO, "\x1b[?25h", 6);	
}

/** Row Operations **/
int row_cx_to_rx(erow *row, int cx) {
	int rx = 0;
	for (int i = 0; i < cx; i++) {
		if (row->chars[i] == '\t') {
			rx += (TAB_STOP-1) - (rx % TAB_STOP);
		}

		rx++;
	}
	return rx;
}

void update_row(erow *row) {
	int tabs = 0;
	for (int j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') tabs++;
	}

	free(row->render);
	row->render = malloc(row->size+ tabs*(TAB_STOP-1) +1); // Allocate for space substitutes for tabs

	int index = 0;
	for (int j = 0; j < row->size; j++) {

		if (row->chars[j] == '\t') {
			row->render[index++]  = ' ';
			while (index % TAB_STOP != 0) row->render[index++] = ' ';
			// Only use 8 spaces at a time for ONE tab
			continue;
		}

		row->render[index++] = row->chars[j];
	}
	row->render[index] = '\0';
	row->rsize = index;
}



void escroll() {
	esettings.rx = 0;
	if (esettings.cy < esettings.numrows) {
		esettings.rx = row_cx_to_rx(&esettings.row[esettings.cy], esettings.cx);
	}

	// Vertical Scrolling 
	// Scroll Up
	if (esettings.cy < esettings.rowoff) {
		esettings.rowoff = esettings.cy;
	}

	// Scroll Down
	if (esettings.cy >= esettings.rowoff + esettings.ws_row) {
		esettings.rowoff = esettings.cy - esettings.ws_row + 1;
	}

	// Horizontal Scrolling 
	// Scroll Left
	if (esettings.rx < esettings.coloff) {
		esettings.coloff = esettings.rx;
	}

	if (esettings.rx >= esettings.coloff + esettings.ws_col) {
		esettings.coloff = esettings.rx - esettings.ws_col + 1;
	}
}

void erefresh_screen() {
	escroll();

	str buf;
	str_init(&buf);

	// Clear screen
	str_append(&buf, "\x1b[?25l", 6);	 // Hide cursor
	str_append(&buf, "\x1b[H", 3);

	edraw_rows(&buf);
	// Move cursor back to original position
	char temp[32];
	snprintf(temp, sizeof(temp), "\x1b[%d;%dH", 
			(esettings.cy - esettings.rowoff)+1,
		 	(esettings.rx - esettings.coloff)+1 + LEFT_PADDING);
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

int static inline curr_row() {
	return clamp(esettings.cy, 0, esettings.numrows);
}

void move_cursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			// Go to previous line
			if (esettings.cx == 0 && esettings.cy > 0) {
				esettings.cy--;
				esettings.cx = esettings.row[curr_row()].size;
			} else {
				esettings.cx--;
			}
			break;
		case ARROW_UP:
			esettings.cy--;
			break;
		case ARROW_DOWN:
			esettings.cy++;
			break;
		case ARROW_RIGHT: 
			if (esettings.cx >= esettings.row[curr_row()].size && 
					esettings.cy < esettings.numrows) {
				esettings.cy++;
				esettings.cx = 0;
			} else {
				esettings.cx++;
			}

			break;
	}

	// Bounds
	esettings.cx = clamp(esettings.cx, 0, esettings.row[curr_row()].size);
	esettings.cy = clamp(esettings.cy, 0, esettings.numrows);
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
			esettings.cx = esettings.row[curr_row()].size;
			break;

		case PAGE_UP:
		case PAGE_DOWN: {

			int offset = esettings.cy - esettings.rowoff;
			int direction = (c == PAGE_UP) ? -1 : 1;

			esettings.rowoff += esettings.ws_row * direction;

			esettings.rowoff = clamp(esettings.rowoff, 0, esettings.numrows-esettings.ws_row+1);

			esettings.cy = esettings.rowoff;


			offset = clamp(offset, 0, esettings.numrows-esettings.rowoff-1);
			while (offset > 0 && offset--) move_cursor(ARROW_DOWN);


			break;
		}
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

/** Screen Output ***/

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

/** File I/O **/
void append_row(char* s, size_t len) {
	esettings.row = realloc(esettings.row, sizeof(erow) * (esettings.numrows+1));

	int row = esettings.numrows;
	esettings.row[row].size = len;
	esettings.row[row].chars = malloc(len+1);
	memcpy(esettings.row[row].chars, s, len);
	esettings.row[row].chars[len] = '\0';
	
	esettings.row[row].rsize = 0;
	esettings.row[row].render = NULL;
	update_row(&esettings.row[row]);

	++esettings.numrows;
}

void open(char* filename) {
	FILE *fp = fopen(filename, "r");
	if (!fp) panic("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t len;
	while((len = getline(&line, &linecap, fp)) != -1) {
		// Remove Trailing '\n' and '\r'
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
		append_row(line, len);
	}

	free(line);
	fclose(fp);
}

/** Initialization **/


void init() {
	esettings.cx = 0;
	esettings.cy = 0;
	esettings.rowoff = 0;
	esettings.coloff = 0;
	esettings.numrows = 0;
	esettings.row = NULL;
	esettings.rx = 0;

	enableRawMode();
	if (get_window_size(&esettings.ws_row, &esettings.ws_col) == -1) panic("get_window_size");
	esettings.ws_row -= 1;
	clear_screen();
}


int main(int argc, char* argv[]) {
	init();
	if (argc >= 2) {
		open(argv[1]);
	}

	while (TRUE) {
		erefresh_screen();
		int c = get_key();
		process_keypress(c);
	}

	return 0;
}
