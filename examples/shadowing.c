int x = 100;

int f(int x) {
    {
        int x = 7;
        x++;
        printf("inner %d\n", x);
    }
    return x;
}

int bump() {
    x = x + 1;
    return x;
}

int main() {
    int print = 3, lambda = 4;
    printf("%d %d\n", f(1), bump());
    printf("%d %d %d\n", x, print, lambda);
    return 0;
}
