extern void print_int(int);
int steps(int n) { int s = 0; while (n != 1) { if (n % 2 == 0) n /= 2; else n = 3 * n + 1; s++; } return s; }
int main() {
    int i, best = 0, besti = 0;
    for (i = 1; i < 300; i++) { int s = steps(i); if (s > best) { best = s; besti = i; } }
    print_int(besti); print_int(best); print_int(steps(27));
    return 0;
}
