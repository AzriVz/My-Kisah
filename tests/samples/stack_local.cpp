int use_local(int value) {
    volatile int local = value + 1;
    return local;
}

int main() {
    return use_local(4);
}
