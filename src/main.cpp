#include <iostream>

int main(int argc, char** argv) {
    for (int i = 0; i < argc; i++) {
        if (i != 0) {
            std::cout << ", ";
        }
        std::cout << argv[i];
    }

    // Flush output
    std::cout << std::endl;

    return 0;
}
