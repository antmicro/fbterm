#include <stdio.h>
#include <stdlib.h>
#include <termio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
	bool help;
	bool plain;
} Args;

const char* ascii_lookup[] = {
	"NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",  "BS",  "HT",  "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
	"DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB", "CAN",  "EM", "SUB", "ESC",  "FS",  "GS",  "RS",  "US"
};

// convert character to a short, printable, string
static void ascii_string(char* buffer, unsigned char ascii) {

	if (ascii < ' ') {
		strcpy(buffer, ascii_lookup[ascii]);
		return;
	}

	if (ascii >= ' ' && ascii <= '~') {
		buffer[0] = '\'';
		buffer[1] = ascii;
		buffer[2] = '\'';
		return;
	}

	if (ascii == 127) {
		strcpy(buffer, "DEL");
		return;
	}
}

// print string given an escaped sequance
static void print_escape(const char* str) {
	int escape = 0;

	while (*str) {
		char c = *str;
		str ++;

		if (c == '\\') {
			escape = 1;
			continue;
		}

		if (escape) {
			if (c == 'e') printf("%c", 0x1b);
			if (c == '\\') printf("%c", c);
			if (c == 'n') printf("%c", '\n');
			if (c == 'r') printf("%c", '\r');
			if (c == 'a') printf("%c", 0x07);
			if (c == 't') printf("%c", '\t');
			if (c == 'f') printf("%c", '\f');
			if (c == '0') printf("%c", '\0');

			escape = 0;
			continue;
		}

		printf("%c", c);
	}
}

// print help and exit with the given code
static void usage(int ec) {
	printf("Usage: angbd [OPTIONS] <escape string>\n");
	printf("Example: angbd \"\\e[>c\"\n");
	printf("\n");
	printf("Options:\n");
	printf(" -b : Print as plain bytes\n");
	printf(" -h : Print usage and quit\n");
	exit(ec);
}

// parse option string
static void parse_args(Args* args, const char* arg) {

	if (arg[0] != '-') {
		printf("Expected flag, got '%s'!\n", arg);
		exit(1);
	}

	// skip '-'
	arg ++;

	while (*arg) {
		char c = *arg;

		switch (c) {
			case 'h': args->help = true; break;
			case 'b': args->plain = true; break;

			default:
				printf("Unknown flag '-%c'!\n", c);
				exit(1);
		}

		arg ++;
	}

}

int main(int argc, char* argv[]) {

	if (argc < 2) {
		usage(1);
	}

	int last = argc - 1;

	Args args;
	args.help = false;
	args.plain = false;

	for (int i = 1; i < last; i ++) {
		parse_args(&args, argv[i]);
	}

	if (args.help) {
		usage(0);
	}

	const char* escape = argv[last];
	struct termios term, initial_term;

	tcgetattr(STDIN_FILENO, &initial_term);
	term = initial_term;
	term.c_lflag &=~ICANON;
	term.c_lflag &=~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &term);

	print_escape(escape);
	fflush(stdout);

	int bytes = 0;

	while (1) {
		fd_set readset;
		struct timeval time;

		FD_ZERO(&readset);
		FD_SET(STDIN_FILENO, &readset);
		time.tv_sec = 0;
		time.tv_usec = 100000;

		if (select(STDIN_FILENO + 1, &readset, NULL, NULL, &time) == 1) {

			char c = 0;
			if (read(STDIN_FILENO, &c, 1) != 1) {
				printf("Unexpected end of input!\n");
				break;
			}

			bytes ++;
			char buffer[8];
			ascii_string(buffer, c);

			if (args.plain) {
				printf("%02X\n", c);
			} else {
				printf("Received: 0x%02X (dec %d) %s\n", c, c, buffer);
			}
			continue;
		}

		break;
	}

	tcsetattr(STDIN_FILENO, TCSADRAIN, &initial_term);

	if (!args.plain) {
		printf("\n");
		printf("Received %d %s as response to \"%s\"\n", bytes, (bytes == 1 ? "byte" : "bytes"), escape);
	}

	return 0;
}
