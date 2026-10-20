/* Same computation as matmul_ijk.c with the loops interchanged to i-k-j.
 * All three matrices are now walked row-wise, so far more accesses hit in the data cache.
 * Output is identical; compare `bin/r32sim --stats --dcache 4096:32:2` for both programs. */
extern void print_int(int);
int A[1024]; int B[1024]; int C[1024];
void matmul(int a[], int b[], int c[], int n) {
    int i, j, k;
    for (i = 0; i < n * n; i++) c[i] = 0;
    for (i = 0; i < n; i++)
        for (k = 0; k < n; k++) {
            int aik = a[i * n + k];
            for (j = 0; j < n; j++) c[i * n + j] += aik * b[k * n + j];
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
