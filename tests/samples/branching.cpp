int increment(int value) {
    return value + 1;
}

int decrement(int value) {
    return value - 1;
}

int choose(int value) {
    if(value > 0) {
        return increment(value);
    }
    return decrement(value);
}

int main() {
    return choose(4);
}
