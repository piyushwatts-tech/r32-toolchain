extern void print_int(int);
int main() {
    int a = 17, b = 5, c = -17, d = -5;
    print_int(a + b); print_int(a - b); print_int(a * b); print_int(a / b); print_int(a % b);
    print_int(c / b); print_int(c % b); print_int(a / d); print_int(a % d); print_int(c / d); print_int(c % d);
    print_int(a & b); print_int(a | b); print_int(a ^ b); print_int(~a); print_int(-a);
    print_int(a << 3); print_int(a >> 2); print_int(c >> 2); print_int(c << 4);
    print_int(a < b); print_int(a > b); print_int(a <= 17); print_int(a >= 18); print_int(a == 17); print_int(a != 17);
    print_int(!a); print_int(!0); print_int(!!b);
    print_int((a + b) * (a - b) / 3 % 7);
    print_int(1 + 2 * 3 - 4 / 2 + 7 % 4);
    print_int(0x7fffffff); print_int(0x7fffffff + 1 - 1); print_int(-2147483647 - 1);
    print_int(a * 1); print_int(a * 0); print_int(a * 8); print_int(a * 16 + 3); print_int(a + 0); print_int(a - 0);
    print_int(1 << 31); print_int((1 << 31) >> 31); print_int(100000 * 100000);
    return 0;
}
