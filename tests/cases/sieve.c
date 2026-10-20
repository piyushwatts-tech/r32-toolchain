extern void print_int(int);
int composite[200];
int main() {
    int i, j, count = 0;
    composite[0] = 1; composite[1] = 1;
    for (i = 2; i < 200; i++) {
        if (!composite[i]) {
            count++;
            for (j = i * i; j < 200; j += i) composite[j] = 1;
        }
    }
    print_int(count);
    for (i = 190; i < 200; i++) if (!composite[i]) print_int(i);
    return 0;
}
