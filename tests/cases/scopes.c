extern void print_int(int);
int x = 7;
int main() {
    int a = 1;
    print_int(x);
    {
        int a = 2;
        print_int(a);
        {
            int a = 3, x = 9;
            print_int(a + x);
        }
        print_int(a);
    }
    print_int(a);
    {
        int x = 100;
        print_int(x);
    }
    print_int(x);
    for (a = 0; a < 2; a++) { int t = a * 10; print_int(t); }
    return 0;
}
