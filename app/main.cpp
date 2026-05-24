#include <iostream>
#include <string_view>
#include "basic.hpp"

void printUsage(std::string_view program) {
    std::cerr
        << "Usage: " << program << " <flag>\n"
        << "\n"
        << "Flags:\n"
        << "  --basic    Seed an orderbook and run the market maker demo.\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string_view flag = argv[1];
    if (flag == "--basic") {
        runBasic();
        return 0;
    }

    std::cerr << "unknown flag: " << flag << "\n\n";
    printUsage(argv[0]);
    return 1;
}
