extern void print_int(int);
int main() {
    int i, j, s = 0;
    for (i = 0; i < 10; i++) s += i;
    print_int(s);
    s = 0;
    for (i = 0; i < 10; i++) for (j = 0; j < i; j++) s += i * j;
    print_int(s);
    i = 0;
    while (i < 100) { i += 7; if (i % 2 == 0) continue; if (i > 60) break; }
    print_int(i);
    s = 0;
    for (i = 10; i > 0; i -= 3) s = s * 2 + i;
    print_int(s);
    for (i = 0; ; i++) { if (i * i > 200) break; }
    print_int(i);
    i = 0; j = 0;
    while (1) { i++; if (i > 50) break; if (i % 3) continue; j += i; }
    print_int(j);
    s = 0;
    for (i = 0; i < 5; i++) { int k = i * 2; for (j = k; j < 10; j++) { if (j == 7) break; s += j; } }
    print_int(s);
    i = 5;
    while (i) { i--; print_int(i); }
    for (i = 0; i < 0; i++) print_int(999);
    return 0;
}
