#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define TRUE 1

struct termios orig_termios;

void panic(const char *s) {
	perror(s);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH,&orig_termios) == -1) {
		panic("tcsetattr");
	}
}

void enableRawMode() {
	// Save default settings
	// Apply old terminal settings when program
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) panic("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = orig_termios;

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

	raw.c_cc[VMIN] = 0; // read() can terminate with a min of 0 bytes recieved
	raw.c_cc[VTIME] = 1; // read() maximum wait time is 100 milliseconds

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw) == -1) panic("tcgetattr");
}

int main() {

	enableRawMode();

	while (TRUE) {
		char c = '\0';
		int8_t res = read(STDIN_FILENO, &c, 1);

		if (res == -1 && errno != EAGAIN) {
			panic("read");
			break;
		}

		if (res != 0)

		if (iscntrl(c)) {
			printf("%d\r\n", c);
		} else {
			printf("'%c' pressed with code '%i'\r\n", c, c);
		}

		if (c == 'q') break;
	}

	return 0;
}
