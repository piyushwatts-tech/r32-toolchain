/* 32x32 integer matrix multiply, textbook i-j-k loop order.
 * The inner loop walks matrix B column-wise (stride 128 bytes), which is hostile to the data cache.
 * Run:  scripts/r32cc -o /tmp/m.exe examples/matmul_ijk.c && bin/r32sim --stats --dcache 4096:32:2 /tmp/m.exe */
extern void print_int(int);
int A[1024]; int B[1024]; int C[1024];
void matmul(int a[], int b[], int c[], int n) {
    int i, j, k;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            int s = 0;
            for (k = 0; k < n; k++) s += a[i * n + k] * b[k * n + j];
            c[i * n + j] = s;
        }
}
int main() {
    int i, check = 0;
    for (i = 0; i < 1024; i++) { A[i] = (i * 7 + 3) % 11 - 5; B[i] = (i * 5 + 1) % 13 - 6; }
    matmul(A, B, C, 32);
    for (i = 0; i < 1024; i++) check = check * 31 + C[i];
    print_int(check);
    return 0;
}
