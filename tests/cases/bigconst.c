extern void print_int(int);
int main() {
    int a = 0x12345678, b = 0x7fffffff, c = 65535, d = 65536, e = 32767, f = 32768, g = -32768, h = -32769;
    print_int(a); print_int(b); print_int(c); print_int(d); print_int(e); print_int(f); print_int(g); print_int(h);
    print_int(a + c); print_int(a & 0xffff); print_int(a | 0xf0f0); print_int(a ^ 0x12345678); print_int(a - 40000); print_int(a + 70000);
    print_int(a & 0x7fff0000); print_int(a < 0x12345679); print_int(a == 0x12345678); print_int(b + 1 < 0);
    print_int(c * c); print_int(d * d); print_int(b * 2); print_int(a * 7 + 3);
    print_int(70000 - a); print_int(a == 305419896); print_int(a != 70000);
    return 0;
}
