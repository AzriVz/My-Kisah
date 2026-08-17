#include <cstdint>

volatile std::int32_t global_bias = 7;

__attribute__((noinline)) static int recursive_sum(int value) {
    if(value <= 0) {
        return global_bias;
    }
    return value + recursive_sum(value - 1);
}

__attribute__((noinline)) static int calculate(int left, int right) {
    const auto mixed = (left * 3) ^ (right + global_bias);
    return mixed > 20 ? mixed - 4 : mixed + 9;
}

int main(int argc, char**) {
    return calculate(argc, recursive_sum(argc & 3));
}
