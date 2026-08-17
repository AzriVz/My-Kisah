int absolute_value(int value) {
    if(value < 0) {
        return -value;
    } else {
        return value;
    }
}

int main() {
    return absolute_value(-7);
}
