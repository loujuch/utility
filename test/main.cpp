#include "lock_free/lf_pipe.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    bit::utility::lock_free::Pipe<int> pipe;
    for (auto i = 0; i < 1024; ++i) {
        pipe.write(i, true);
    }
    int value = -1;
    for (auto i = 0; i < 512; ++i) {
        pipe.unwrite(value);
        std::cout << value << std::endl;
    }
    pipe.write(-1, false);
    pipe.flush();
    while (pipe.read(value)) {
        std::cout << value << std::endl;
    }
    return 0;
}