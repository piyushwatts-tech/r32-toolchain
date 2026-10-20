extern void print_int(int);
int table[10] = {5, 3, 9, 1, 7, 2, 8, 6, 4, 0};
int zeros[6];
int sum(int a[], int n) { int i, s = 0; for (i = 0; i < n; i++) s += a[i]; return s; }
void bubble(int a[], int n) {
    int i, j;
    for (i = 0; i < n - 1; i++)
        for (j = 0; j < n - 1 - i; j++)
            if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }
}
int find(int a[], int n, int key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] == key) return mid;
        if (a[mid] < key) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}
void fill(int a[], int n, int v) { int i; for (i = 0; i < n; i++) a[i] = v + i; }
int main() {
    int local[8];
    int grid[12];
    int i, j;
    print_int(sum(table, 10));
    bubble(table, 10);
    for (i = 0; i < 10; i++) print_int(table[i]);
    print_int(find(table, 10, 7)); print_int(find(table, 10, 11));
    fill(local, 8, 100);
    print_int(sum(local, 8));
    for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) grid[i * 4 + j] = i * j + 1;
    print_int(sum(grid, 12));
    for (i = 0; i < 6; i++) zeros[i] += i * i;
    print_int(sum(zeros, 6));
    local[3] += 5; local[3] *= 2; local[3]--; local[3] <<= 1;
    print_int(local[3]);
    return 0;
}
