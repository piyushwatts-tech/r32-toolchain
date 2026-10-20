extern void print_int(int);
int f8(int a, int b, int c, int d, int e, int f, int g, int h) { return a - b * 2 + c * 3 - d * 4 + e * 5 - f * 6 + g * 7 - h * 8; }
int h_dummy_unused(int x);
int g3(int a, int b, int c) { return f8(c, b, a, a, b, c, h_dummy_unused(a), 1); }
int h_dummy_unused(int x) { return x + 100; }
int main() {
    print_int(f8(1, 2, 3, 4, 5, 6, 7, 8));
    print_int(f8(8, 7, 6, 5, 4, 3, 2, 1));
    print_int(g3(10, 20, 30));
    print_int(f8(f8(1,1,1,1,1,1,1,1), 2, 3, 4, 5, 6, 7, f8(2,2,2,2,2,2,2,2)));
    return 0;
}
