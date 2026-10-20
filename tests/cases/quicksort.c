extern void print_int(int);
extern void print_char(int);
int data[40];
int seed = 12345;
int rnd() { seed = seed * 1103515245 + 12345; return (seed >> 16) & 32767; }
void swap(int a[], int i, int j) { int t = a[i]; a[i] = a[j]; a[j] = t; }
int partition(int a[], int lo, int hi) {
    int pivot = a[hi], i = lo - 1, j;
    for (j = lo; j < hi; j++) if (a[j] <= pivot) { i++; swap(a, i, j); }
    swap(a, i + 1, hi);
    return i + 1;
}
void qsort_(int a[], int lo, int hi) {
    if (lo < hi) {
        int p = partition(a, lo, hi);
        qsort_(a, lo, p - 1);
        qsort_(a, p + 1, hi);
    }
}
int main() {
    int i, ok = 1;
    for (i = 0; i < 40; i++) data[i] = rnd() % 1000;
    qsort_(data, 0, 39);
    for (i = 1; i < 40; i++) if (data[i - 1] > data[i]) ok = 0;
    print_int(ok);
    for (i = 0; i < 40; i += 8) print_int(data[i]);
    print_char('O'); print_char('K'); print_char(10);
    return 0;
}
