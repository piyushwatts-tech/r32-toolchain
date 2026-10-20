extern void print_int(int);
int is_odd(int n);
int is_even(int n) { if (n == 0) return 1; return is_odd(n - 1); }
int is_odd(int n) { if (n == 0) return 0; return is_even(n - 1); }
int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
int gcd(int a, int b) { if (b == 0) return a; return gcd(b, a % b); }
int power(int base, int e) {
    int r = 1;
    while (e > 0) {
        if (e & 1) r *= base;
        base *= base;
        e >>= 1;
    }
    return r;
}
int ack(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}
int main() {
    int i;
    for (i = 0; i < 15; i++) print_int(fib(i));
    print_int(fact(10)); print_int(gcd(1071, 462)); print_int(power(3, 13)); print_int(ack(2, 3));
    print_int(is_even(10)); print_int(is_odd(10)); print_int(is_even(7));
    return 0;
}
