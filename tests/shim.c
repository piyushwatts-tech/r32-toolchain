/* Host-side implementation of the R32 runtime, used to produce reference output with gcc. */
#include <stdio.h>
void print_int(int x) { printf("%d\n", x); }
void print_char(int c) { putchar(c & 0xFF); }
int read_int(void) { int x = 0; if (scanf("%d", &x) != 1) x = 0; return x; }
