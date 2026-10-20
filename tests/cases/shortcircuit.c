extern void print_int(int);
int calls;
int t(int tag) { calls++; print_int(tag); return 1; }
int f(int tag) { calls++; print_int(tag); return 0; }
int main() {
    int x;
    if (f(1) && t(2)) print_int(100);
    if (t(3) || f(4)) print_int(101);
    if (f(5) || f(6) || t(7)) print_int(102);
    if (t(8) && t(9) && f(10)) print_int(103); else print_int(104);
    x = t(11) && f(12); print_int(x);
    x = f(13) || t(14); print_int(x);
    x = !(t(15) && t(16)); print_int(x);
    print_int(calls);
    if (!f(17)) print_int(105);
    x = 5;
    if (x > 3 && x < 10 || x == 100) print_int(106);
    if (x < 3 || x > 4 && x != 6) print_int(107);
    return 0;
}
