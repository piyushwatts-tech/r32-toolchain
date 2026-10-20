extern void print_int(int);
int a[16];
int main() {
    int i, t = 0;
    for (i = 0; i < 16; i++) a[i] = i * 3 - 7;
    for (i = 1; i < 15; i++) {
        t += a[i] * a[i] + a[i] - a[i - 1] + a[i + 1] * 2;
        t += (i * 4 + 1) * (i * 4 + 1);
    }
    print_int(t);
    {
        int x = 5, y = 7;
        int p = x * y + x * y;
        int q = (x + y) * (x + y) - (x + y);
        print_int(p); print_int(q);
        a[2] = 40; t = a[2] + 1; a[2] = 50; t += a[2] + 1;
        print_int(t);
    }
    return 0;
}
