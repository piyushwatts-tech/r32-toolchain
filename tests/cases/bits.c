extern void print_int(int);
int popcount(int x) { int c = 0; while (x) { c += x & 1; x = (x >> 1) & 0x7fffffff; } return c; }
int reverse8(int x) { int r = 0, i; for (i = 0; i < 8; i++) { r = (r << 1) | (x & 1); x >>= 1; } return r; }
int clz(int x) { int n = 0; if (x == 0) return 32; while (x > 0) { x <<= 1; n++; } return n; }
int main() {
    print_int(popcount(255)); print_int(popcount(0x12345678)); print_int(popcount(-1));
    print_int(reverse8(1)); print_int(reverse8(0xA5)); print_int(reverse8(0x0F));
    print_int(clz(1)); print_int(clz(0)); print_int(clz(0x00ff0000));
    print_int(-1 >> 5); print_int(-256 >> 4); print_int(1 << 30);
    print_int(5 ^ 3 | 8 & 12);
    return 0;
}
