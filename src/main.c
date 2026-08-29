#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

#define VERSION "0.0.1"
#define MIN_PADDING 1
#define TAB_STOP 4
#define STATUS_TIME 5
#define QUIT_TIMES 1

#define _BSD_SOURCE
#define _GNU_SOURCE

#define TRUE 1

// e is idiomatic for editor

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_UP,
	ARROW_DOWN,
	ARROW_RIGHT,
	DEL_KEY,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY, 
	END_KEY,
	NO_KEY_PRESS,
};

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** Data ***/
struct editorSyntax {
	char *filetype;
	char **filematch;
	char **keywords;
	char *singleline_comment_start;
	char *multiline_comment_start;
	char *multiline_comment_end;
	int flags;
};

typedef struct erow { // Single row data
	int index;
	int size;
	int indent_level;
	int rsize;
	char *chars;
	char *render;
	unsigned char *hl;
	int hl_open_comment;
} erow;

struct editorConfig {
	unsigned int cx, cy; 			// Cursor position in file
	unsigned int rx; 				// Rendered cursor position in rendered file
	unsigned int ws_col, ws_row;	// Screen dimensions
	int numrows;					// Number of rows in file
	int rowoff; 					// Line number of file displayed first on screen 
	int coloff;						// Character of current line first displayed on screen
	erow* row;						// File row data
	int dirty;						// Indicate if file has been modifed
	int padding;					// Gloabal padding variable	
	char* filename;					// File name
	char statusmsg[80];				// Status bar message
	time_t statusmsg_time;			// Time created
	struct editorSyntax *syntax;	// Syntax Highlighting: Current File Type
	struct termios orig_termios;	// Terminal Settings
};

struct editorConfig esettings;

/*** Filetypes ***/
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else", 
	"struct", "union", "typedef", "static", "enum", "class", "case",

	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|", "bool|", NULL,
};

char *Python_HL_extensions[] = { ".py", NULL };
char *Python_HL_keywords[] = {
	"as", "assert", "async", "await", "break", "case", "class", "continue", "def", 
	"del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import",
	"in", "is", "lambda", "match", "nonlocal", "and", "or", "not", "pass", "raise", "return",
	"try", "while", "with", "yield", "type", "print", "import",	  	
	
	"int|", "float|", "bool|", "str|", "False|", "True|", "None|", NULL,  		
};

char *TinyScript_HL_extensions[] = { ".tss", NULL };
char *TinyScript_HL_keywords[] = {				
	"IF", "WHILE", "THEN", "END", "DEFINE", "CALL", "DO", "ELSE",
			
	"JOYSTICK_X|", "JOYSTICK_Y|", "JOYSTICK_BUTTON|", "BUTTON_A|", 
	"TIME|", "SCREEN_WIDTH|", "SCREEN_HEIGHT|", "NOT|", "AND|", "OR|", NULL,
};	

// Store all possible file types to add syntax highlighting rules
struct editorSyntax HLDB[] = {
	{
		"c",
		C_HL_extensions,
		C_HL_keywords, 
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
	{
		"py",
		Python_HL_extensions,
		Python_HL_keywords,
		"#", NULL, NULL,
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS										
	},
	{
		"tinyscript",
		TinyScript_HL_extensions,
		TinyScript_HL_keywords,
		"#", NULL, NULL,
		HL_HIGHLIGHT_NUMBERS,									
	},				
};	

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/** String Handling **/
typedef struct {
	char *buf;
	unsigned int len; // Excludes null terminating character
} str;

static inline int clamp(int a, int min, int max) {
	/// Clamp between values (inclusive)
	/// Only the min argument determines if the value is out of bounds
	/// I.E: if max < min, max will not trigger clamp to bound a to max
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

/*** Prototypes ***/
char* prompt(char* prompt, void (*callback)(char *, int));
void panic(const char *s);

/** Terminal **/
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

/*** Syntax Highlighting ***/
int is_seperator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void eupdate_syntax(erow *row) {
	row->hl = realloc(row->hl, row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);
	
	if (esettings.syntax == NULL) return;
	
	char **keywords = esettings.syntax->keywords;

	char *scs = esettings.syntax->singleline_comment_start;
	char *mcs = esettings.syntax->multiline_comment_start;
	char *mce = esettings.syntax->multiline_comment_end;	

	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;
	
	int prev_sep = 1;	
	int in_string = 0;
		
	// Determine if row is part of an open multiline comment
	int in_comment = (row->index > 0 && esettings.row[row->index-1].hl_open_comment);

	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i-1] : HL_NORMAL;	

		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->rsize-i);
				break;
			}	
		}

		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				
				// End of comment
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				} else {
					++i;
					continue;
				}
			} else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				// Start multiline comment
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}
	
		if (HL_HIGHLIGHT_STRINGS & esettings.syntax->flags) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				
				// Account for \" and \' 
				// Recognize them as part of the string
				// Not the end of string
				if (c == '\\' && i+1 < row->rsize) {
					row->hl[i+1] = HL_STRING;
					i += 2;
					continue;
				}

				if (c == in_string) in_string = 0;
				++i;
				prev_sep = 1;	
				continue;
			} else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					++i;
					continue;
				}
			}
		}
	
		if (HL_HIGHLIGHT_NUMBERS & esettings.syntax->flags) {
			// Numero highlighting
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
				(c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				++i;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) {
				int klen = strlen(keywords[j]);
				
				// Determine if secondary keyword
				int kw2 = keywords[j][klen-1] == '|';
				if (kw2) klen--;

				// Determine if keyword and apply rules
				if (!strncmp(&row->render[i], keywords[j], klen) &&
					is_seperator(row->render[i+klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}	 
			}
	
			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}		
		}
	   	
		prev_sep = is_seperator(c);
		++i;
	}

	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->index+1 < esettings.numrows) {
		eupdate_syntax(&esettings.row[row->index+1]);
	}
}

int esyntax_to_color(int hl) {
	switch (hl) {
		case HL_NUMBER: return 93;
		case HL_COMMENT:
		case HL_MLCOMMENT: return 36;
		case HL_STRING: return 92;
		case HL_KEYWORD1: return 35;
		case HL_KEYWORD2: return 96;
		case HL_MATCH: return 30;	
		default: return 37;
	}
}

void eselect_syntax_highlight() {
	/// Based on type of file
	/// We try set the syntax highlighting rules
	///	Selected from Hightlight Data base

	esettings.syntax = NULL;
	if (esettings.filename == NULL) return;

	char *file_ext = strrchr(esettings.filename, '.');
	
	// Check every entry in database and match correct syntax highlighting rules
	for (unsigned int i = 0; i < HLDB_ENTRIES; i++) {
		struct editorSyntax *s = &HLDB[i];
		unsigned int j = 0;
		
		// Compare character for character if the extension 
		// of current fetched predefined file extensions match
		while (s->filematch[j]) {
			int is_ext = (s->filematch[j][0] == '.'); 
			// Not every 'special file' is identifiable by its extension
			
			// Match correct file extension in database
			// To get filetype of opened file
			if ((is_ext && file_ext && !strcmp(file_ext, s->filematch[j])) ||
				(!is_ext && strstr(esettings.filename, s->filematch[j]))) {
				esettings.syntax = s;
				
				for (int filerow = 0; filerow < esettings.numrows; filerow++) {
					eupdate_syntax(&esettings.row[filerow]);	
				}

				return;
			}
			
			++j; 
		}		
	}
}

/*** Keys ***/
#define CTRL_KEY(k) ((k) & 0x1f)

int get_padding_size() {
	// Determine the padding size based on
	// The number of lines in the file
	// Used for displaying line numbers
	int numrows = esettings.numrows;
	int padding = 0;
	while ((numrows / 10) > 0) ++padding;
	
	return padding; 
}

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
			if (len > esettings.ws_col-esettings.padding) len = esettings.ws_col-esettings.padding;
			
			// Print line number
			char linenumber[10];
			int linenumber_len = snprintf(linenumber, sizeof(linenumber), "%d", filerow+1);
			
			if (filerow != esettings.cy) str_append(buf, "\x1b[2m", 4);
			
			int padding = esettings.padding-linenumber_len-1; // Don't print divider twice
			while (padding > 0) {
				str_append(buf, " ", 1);
				--padding;
			}
			str_append(buf, linenumber, linenumber_len);
			str_append(buf, " ", 1); 			// Divider
		
			if (filerow != esettings.cy) str_append(buf, "\x1b[0m", 4);
				
			if (filerow == esettings.cy) str_append(buf, "\x1b[48;5;236m", 11);
																																																																																																																																																																										
			// Print Line
			char *c = &esettings.row[filerow].render[esettings.coloff];
			unsigned char *hl = &esettings.row[filerow].hl[esettings.coloff];
			int current_color = -1;

			int i;
			for (i = 0; i < len; i++) {
				if (iscntrl(c[i])) {
					// Handle drawing non-printable characters
					char sym = (c[i] <= 26) ? '@' + c[i] : '?';
					str_append(buf, "\x1b[7m", 4);
					str_append(buf, &sym, 1);
					str_append(buf, "\x1b[m", 3);
						
					if (current_color != -1) {
						char tmp[16];
						int clen = snprintf(tmp, sizeof(tmp), "\x1b[%dm", current_color);
						str_append(buf, tmp, clen);
					}
				} else if (hl[i] == HL_NORMAL) {
					if (current_color != -1) {
						str_append(buf, "\x1b[39;49m", 8);
						current_color = -1;
					}

					str_append(buf, &c[i], 1);
				} else {
					int color = esyntax_to_color(hl[i]);	
					if (color != current_color) {
						current_color = color;
						char ansi_color[16];
						int clen = snprintf(ansi_color, sizeof(ansi_color), "%s\x1b[%dm",
									hl[i] == HL_MATCH ? "\x1b[103m" : "", color);
						str_append(buf, ansi_color, clen);
					}
					
					str_append(buf, &c[i], 1);
				}
			}	
	
			// Draw empty characters for the remainder of the line
			// For the current row highlight indicator
			if (filerow == esettings.cy) {
				for (i = i; i < esettings.ws_col-esettings.padding; i++) {
					str_append(buf, " ", 1);
				}
			} 
			
			str_append(buf, "\x1b[39;49m", 8); 
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
void update_padding() {
	int digits = 0;
	int numrows = esettings.numrows;
	while (numrows > 0) {
		++digits;
		numrows /= 10;
	}

	esettings.padding = digits; // Account for extra dividing space
	if (esettings.padding < MIN_PADDING) esettings.padding = MIN_PADDING;
	esettings.padding += 1; // Account for extra space
}

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

int row_rx_to_cx(erow *row, int rx) {
	int cur_rx = 0;
	int cx;
	for (int cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t') {
			cur_rx += (TAB_STOP-1) - (cur_rx % TAB_STOP);
		}

		++cur_rx;

		if (cur_rx > rx) return cx;
	}
	return cx;
}

void update_row(erow *row) {
	/// Recieves row to be updated and 
	/// updates a single row's 
	/// render to be displayed later
	int tabs = 0;
	for (int j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') tabs++;
	}

	row->indent_level = tabs;

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
	
	eupdate_syntax(row);
}

void insert_row(int i, char* s, size_t len) {
	if (i < 0 || i > esettings.numrows) return;

	esettings.row = realloc(esettings.row, sizeof(erow) * (esettings.numrows+1));
	memmove(&esettings.row[i+1], &esettings.row[i], sizeof(erow)*(esettings.numrows-i));
	
	// Shift the indices of subsequent rows
	for (int j = i+1; j <= esettings.numrows; j++) ++esettings.row[j].index;

	esettings.row[i].index = i;

	esettings.row[i].size = len;
	esettings.row[i].chars = malloc(len+1);
	memcpy(esettings.row[i].chars, s, len);
	esettings.row[i].chars[len] = '\0';

	// Count leading tabs
	esettings.row[i].indent_level = 0;
	
	esettings.row[i].rsize = 0;
	esettings.row[i].render = NULL;
	esettings.row[i].hl = NULL;
	
	esettings.row[i].hl_open_comment = 0;
	
	update_row(&esettings.row[i]);
	update_padding();

	++esettings.numrows;
	++esettings.dirty;
}

void efree_row(erow* row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void edel_row(int i) {
	if (i < 0 || i >= esettings.numrows) return;
	
	int removed_chars = esettings.row[i].size;

	efree_row(&esettings.row[i]);
	memmove(&esettings.row[i], &esettings.row[i+1], sizeof(erow) * (esettings.numrows-i-1));

	// Shift indices of previous rows
	for (int j = i; j < esettings.numrows-1; j++) --esettings.row[j].index;

	--esettings.numrows;

	esettings.dirty+=removed_chars;
	update_padding();
}

void erow_insert_char(erow *row, int i, int c) {
	if (i < 0 || i > row->size) i = row->size;
	row->chars = realloc(row->chars, row->size+2);
	memmove(&row->chars[i+1], &row->chars[i], row->size-i+1);
	row->size++;
	row->chars[i] = c;
	update_row(row);
	esettings.dirty++;
}

void erow_append_string(erow* row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	update_row(row);
	esettings.dirty+=len;
}

void erow_del_char(erow *row, int i) {
	if (i < 0 || i >= row->size) return;
	memmove(&row->chars[i], &row->chars[i+1], row->size-i);
	row->size--;
	update_row(row);
	esettings.dirty++;
}

/*** Editor Operations ***/
void einsert_char (int c) {
	if (esettings.cy == esettings.numrows) {
		insert_row(esettings.numrows, "", 0);
	}

	erow_insert_char(&esettings.row[esettings.cy], esettings.cx, c);
	esettings.cx++;
}

void einsert_new_line() {
	if (esettings.cx == 0) {
		insert_row(esettings.cy, "", 0);
		++esettings.cy;
		esettings.cx = 0;
	} else {
		erow *row = &esettings.row[esettings.cy];
		str buf;
		str_init(&buf);
		
		// Retain Indentation level by inserting indentation
		//int tabs = row->indent_level;
		int tabs = 0;
		for (int i = 0; i < esettings.cx; i++) {
			if (row->chars[i] != '\t') break;
			++tabs;
		}

		int tabs_save = tabs;
		while (tabs--) str_append(&buf, "\t", 1);

		str_append(&buf, &row->chars[esettings.cx], row->size-esettings.cx);
		insert_row(esettings.cy+1, buf.buf, buf.len);

		//insert_row(esettings.cy+1, &row->chars[esettings.cx], row->size-esettings.cx);

		row = &esettings.row[esettings.cy]; // Reset pointer
		row->size = esettings.cx;
		row->chars[row->size] = '\0';

		update_row(row);

		esettings.cx = tabs_save;
		++esettings.cy;
	}

}

void edel_char() {
	if (esettings.cy == esettings.numrows) return;
	if (esettings.cx == 0 && esettings.cy == 0) return;

	erow *row = &esettings.row[esettings.cy];
	if (esettings.cx > 0) {
		erow_del_char(row, esettings.cx-1);
		--esettings.cx;
	} else {
		esettings.cx = esettings.row[esettings.cy-1].size;
		erow_append_string(&esettings.row[esettings.cy-1], row->chars, row->size);
		edel_row(esettings.cy);
		--esettings.cy;
	}
}

void escroll() {
	esettings.rx = 0;
	if (esettings.cy < esettings.numrows && esettings.cy >= 0) {
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

	// Scroll Right
	if (esettings.rx >= esettings.coloff + (esettings.ws_col-esettings.padding)) {
		esettings.coloff = esettings.rx - (esettings.ws_col-esettings.padding) + 1;
	}
}

void eset_message(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(esettings.statusmsg, sizeof(esettings.statusmsg), fmt, ap);
	va_end(ap);

	esettings.statusmsg_time = time(NULL); 
}

void edraw_message(str* buf) {
	str_append(buf, "\x1b[K", 3);
	int len = clamp(strlen(esettings.statusmsg), 0, esettings.ws_col);
	time_t curr_time = time(NULL);
	if (len && (curr_time - esettings.statusmsg_time < STATUS_TIME)) {
		str_append(buf, esettings.statusmsg, len);
	}
}

void edraw_status(str* buf) {
	str_append(buf, "\x1b[7m", 4);

	char status[120], rstatus[80], modified[40];

	int mlen = snprintf(modified, sizeof(modified), "(%d bytes modified)", esettings.dirty);

	int len = snprintf(status, sizeof(status), "%s %.20s - %d of %d lines",
				esettings.dirty ? modified : "",
				esettings.filename ? esettings.filename : "[Empty File]", 
				esettings.cy+1, esettings.numrows);

	int rlen = snprintf(rstatus, sizeof(rstatus), "%s ",
				esettings.syntax ? esettings.syntax->filetype : "plain");

	if (len > esettings.ws_col) len = esettings.ws_col;
	str_append(buf, status, len);

	while (len < esettings.ws_col) {
		if (esettings.ws_col - len == rlen) {
			str_append(buf, rstatus, rlen);
			break;
		}

		str_append(buf, " ", 1);
		++len;
	}

	str_append(buf, "\x1b[m", 3);
	str_append(buf, "\r\n", 3);
}

void erefresh_screen() {
	escroll();

	str buf;
	str_init(&buf);

	// Clear screen
	str_append(&buf, "\x1b[?25l", 6);	 // Hide cursor
	str_append(&buf, "\x1b[H", 3);

	edraw_rows(&buf);
	edraw_status(&buf);
	edraw_message(&buf);
	// Move cursor back to original position
	char temp[32];
	snprintf(temp, sizeof(temp), "\x1b[%d;%dH", 
			(esettings.cy - esettings.rowoff)+1,
		 	(esettings.rx - esettings.coloff)+1+esettings.padding);
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

int get_key() {
	char c = '\0';
	ssize_t res = read(STDIN_FILENO, &c, 1);

	if (res == 0) {
		return NO_KEY_PRESS;
	}

	// Catch read errors (If standard input is not terminal input)
	if (res < 0 && errno != EAGAIN) {
		panic("read");
		return NO_KEY_PRESS;
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

char* prompt(char* prompt, void (*callback)(char *, int)) {
	/// Prompt user for input
	/// Store input and process it for later

	size_t bufsize = 128;
	char *buf = malloc(bufsize);

	size_t len = 0;
	buf[0] = '\0';
	
	while (TRUE) {
		eset_message(prompt, buf);
		erefresh_screen();
		
		int c = get_key();
		
		// BACKSPACE
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (len != 0) buf[--len] = '\0';	
			continue;
		}		

		// ESCAPE - Cancel Prompt
		if (c == '\x1b' || c == CTRL_KEY('q')) {
			eset_message("");
			if (callback) callback(buf, c);
			free(buf);
			buf = NULL;
			break;
		}

		// ENTER - Submit
		if (c == '\r') {
			if (len <= 0) continue;
			eset_message("");
			if (callback) callback(buf, c);
			break;					
		} 

		// KEY INPUT
		if (!iscntrl(c) && c < 128) {
			// User key input for prompt
			if (len == bufsize-1) {
				// Resize buffer if too small
				bufsize *= 2;
				buf = realloc(buf, bufsize);	
			}
			buf[len++] = c;
			buf[len] = '\0';  // Null terminate: Buffer used every iteration
		}

		if (callback) callback(buf, c);
	}
	
	// Move cursor back to original position
	erefresh_screen();
	return buf;
}

void move_cursor(int key) {
	if (esettings.numrows <= 0) return;

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
	esettings.cy = clamp(esettings.cy, 0, esettings.numrows);

	if (esettings.cy < esettings.numrows) {
		esettings.cx = clamp(esettings.cx, 0, esettings.row[curr_row()].size);
	} else {
		esettings.cx = 0;
	}
}

/** File I/O **/
char* erows_to_string(int *buflen) {
	// Count characters to store
	int totlen = 0;
	for (int i = 0; i < esettings.numrows; i++) {
		totlen += esettings.row[i].size + 1;
	}
	*buflen = totlen;


	// Store characters
	char *buf = malloc(totlen);
	char *p = buf;
	for (int i = 0; i < esettings.numrows; i++) {
		memcpy(p, esettings.row[i].chars, esettings.row[i].size);
		p += esettings.row[i].size;
		*p = '\n';
		++p;
	}

	return buf;
}

void eopen(char* filename) {
	free(esettings.filename);
	esettings.filename = strdup(filename);
	
	eselect_syntax_highlight();

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		esettings.dirty = 0;
		
		return;		
		//panic("fopen");
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t len;
	while((len = getline(&line, &linecap, fp)) != -1) {
		// Remove Trailing '\n' and '\r'
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
		insert_row(esettings.numrows, line, len);
	}

	free(line);
	fclose(fp);

	esettings.dirty = 0;
	
	update_padding();
}

void esave() {
	if (esettings.filename == NULL) {
		esettings.filename = prompt("Save file as:\t %s", NULL);
		if (esettings.filename == NULL) {
			eset_message("Save aborted");
			return;	
		}
		eselect_syntax_highlight();
	}

	int len;
	char *buf = erows_to_string(&len);

	int fd = open(esettings.filename, O_RDWR | O_CREAT, 0644);

	if (fd == -1) {
		goto cleanup_buf;
	}

	if (ftruncate(fd, len) == -1) {
		goto cleanup_fd;
	}

	if (write(fd, buf, len) != len) {
		goto cleanup_fd;
	}

	eset_message("%d bytes written to disk", len);

	close(fd);
	free(buf);
	esettings.dirty = 0;
	return;

	cleanup_fd:
		close(fd);
	cleanup_buf:
		free(buf);
	
	eset_message("Error Saving! I/O error: %s", strerror(errno));
}

/*** Find ***/
void find_callback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;
	
	static int saved_hl_line;
	static char* saved_hl = NULL;

	if (saved_hl) {
		memcpy(esettings.row[saved_hl_line].hl, saved_hl, esettings.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (strlen(query) == 0) {
		last_match = -1;
		return;
	}

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	} 

	int current;

	if (last_match <= -1) {
		direction = 1;
		current = esettings.cy;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
		current = last_match+1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
		current = last_match-1;
	} else {
		direction = 1;
		current = last_match;
	}
	
	for (int i = 0; i < esettings.numrows; i++) { 
		// Iterate by # of lines in file
		// Current doesn't necissarily start at the first line
		if (current <= -1) {
			current = esettings.numrows-1;
		} else if (current >= esettings.numrows) {
			current = 0;
		}

		erow* row = &esettings.row[current];
		char *match = strcasestr(row->render, query);
		if (match) {
			last_match = current;
			esettings.cy = current;
			esettings.cx = row_rx_to_cx(row, match-row->render);
			esettings.rowoff = esettings.numrows;
		
			saved_hl_line = current;
			saved_hl = malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);
					
			memset(&row->hl[match-row->render], HL_MATCH, strlen(query));
			break;
		}
		
		current += direction;
	}
}

void find() {
	int saved_cx = esettings.cx;
	int saved_cy = esettings.cy;
	int saved_coloff = esettings.coloff;
	int saved_rowoff = esettings.rowoff;

	char *query = prompt("Search: %s", find_callback);

	if (query) {
		free(query);
		return;
	} 

	// Search CANCELLED - Return user to original position
	esettings.cx = saved_cx;
	esettings.cy = saved_cy;
	esettings.coloff = saved_coloff;
	esettings.rowoff = saved_rowoff;
}


void process_keypress(int c) {
	static int quit_times = QUIT_TIMES;

	switch (c) {
		case NO_KEY_PRESS:
			return;

		case '\r':
			einsert_new_line();
			break;

		case CTRL_KEY('q'):
			if (esettings.dirty && quit_times > 0) {
				eset_message("WARNING!!! About to discard changes." 
						" Press Ctrl-Q again to force quit...", quit_times);
				--quit_times;
				return;
			}

			clear_screen();
			exit(0);
			break;

		case CTRL_KEY('s'):
			esave();
			break;

		case CTRL_KEY('f'):
			find();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (esettings.cx >= esettings.row[curr_row()].size && c == DEL_KEY) break;

			if (c == DEL_KEY) move_cursor(ARROW_RIGHT);
			edel_char();
			break;

		// Navigation
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

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			move_cursor(c);
			break;

		// Ignore Ctrl-l and Escape keys
		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			einsert_char(c);
			break;
	}

	quit_times = QUIT_TIMES; // Reset quit times if any other key pressed
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

/** Initialization **/
void init() {
	esettings.cx = 0;
	esettings.cy = 0;
	esettings.rowoff = 0;
	esettings.coloff = 0;
	esettings.numrows = 0;
	esettings.row = NULL;
	esettings.rx = 0;
	esettings.filename = NULL;
	esettings.statusmsg[0] = '\0';
	esettings.statusmsg_time = 0;
	esettings.dirty = 0;
	esettings.syntax = NULL;
	esettings.padding = 0;

	enableRawMode();
	if (get_window_size(&esettings.ws_row, &esettings.ws_col) == -1) panic("get_window_size");
	esettings.ws_row -= 2;
	clear_screen();
}


int main(int argc, char* argv[]) {
	init();
	if (argc >= 2) {
		eopen(argv[1]);
	}

	eset_message("HELP: Ctrl-S to save | Ctrl-Q to quit | Ctrl-F to search");

	while (TRUE) {
		erefresh_screen();
		int c = get_key();
		process_keypress(c);
	}

	return 0;
}
