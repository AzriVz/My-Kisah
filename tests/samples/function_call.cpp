int multiply(int first, int second) {
    return first * second;
}

int calculate(int value) {
    return multiply(value, 2) + 1;
}

int main() {
    return calculate(4);
}
