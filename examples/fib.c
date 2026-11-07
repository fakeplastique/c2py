int main() {
    int a = 0, b = 1, t, n = 0;
    while (n < 20) {
        printf("%d ", a);
        t = a + b;
        a = b;
        b = t;
        n++;
    }
    printf("\n");
    return 0;
}
