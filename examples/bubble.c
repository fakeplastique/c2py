int a[10];

void sort(int n) {
    int i, j, t, swapped;
    do {
        swapped = 0;
        for (i = 1; i < n; i++) {
            if (a[i - 1] > a[i]) {
                t = a[i];
                a[i] = a[i - 1];
                a[i - 1] = t;
                swapped = 1;
            }
        }
        n--;
    } while (swapped);
}

int main() {
    int i, seed = 7;
    for (i = 0; i < 10; i++) {
        seed = (seed * 31 + 11) % 97;
        a[i] = seed;
    }
    sort(10);
    for (i = 0; i < 10; i++)
        printf("%d ", a[i]);
    printf("\n");
    return 0;
}
