int sum_to_n(int number) {
    int sum = 0;

    for(int index = 0; index <= number; ++index) {
        sum += index;
    }

    return sum;
}

int main() {
    return sum_to_n(10);
}

