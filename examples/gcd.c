int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main() {
    printf("gcd(84, 36) = %d\n", gcd(84, 36));
    printf("gcd(17, 5) = %d\n", gcd(17, 5));
    printf("lcm(4, 6) = %d\n", 4 / gcd(4, 6) * 6);
    return 0;
}
