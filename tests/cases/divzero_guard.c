extern void print_int(int);
int safe_div(int a, int b) { if (b == 0) return -1; return a / b; }
int main() {
    print_int(safe_div(10, 2)); print_int(safe_div(10, 0)); print_int(safe_div(-10, 3));
    print_int(-2147483647 - 1);
    return 0;
}
