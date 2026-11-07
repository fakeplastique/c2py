int calls;

int touch(int v) {
    calls++;
    return v;
}

int main() {
    int x = 5, y, a[3];
    printf("%d %d %d\n", 2 + 3 * 4, (2 + 3) * 4, 10 - 4 - 3);
    printf("%d %d\n", -x * 2, !x + !0);
    printf("%d %d\n", 1 < 2 == 1, 3 > 2 && 2 > 1 || 0);
    y = x++;
    y += ++x;
    printf("x=%d y=%d\n", x, y);
    x = y = 3;
    x += y *= 2;
    printf("x=%d y=%d\n", x, y);
    if (0 && touch(1)) printf("never\n");
    if (1 || touch(1)) printf("short-circuit, calls=%d\n", calls);
    a[0] = 1;
    a[a[0]] = 5;
    a[2] = a[1]++ + --a[0];
    a[1] %= 4;
    printf("%d %d %d\n", a[0], a[1], a[2]);
    printf("100%%\n");
    return 0;
}
