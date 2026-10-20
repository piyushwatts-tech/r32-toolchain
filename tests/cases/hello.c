extern void print_char(int);
extern void print_int(int);
void puts_(int s[]) { int i = 0; while (s[i] != 0) { print_char(s[i]); i++; } print_char('\n'); }
int main() {
    int msg[16];
    msg[0] = 'R'; msg[1] = '3'; msg[2] = '2'; msg[3] = ' '; msg[4] = 'o'; msg[5] = 'k'; msg[6] = '!'; msg[7] = 0;
    puts_(msg);
    print_int('A' + 1);
    return 0;
}
