#include "../src/chess.hpp"
#include "../src/evaluation.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <tuple> 
#include <string>
#include <vector>
#include <algorithm>

using namespace chess;

int main() {
    Board board = Board("1r1r2k1/1p6/8/8/8/3P4/3R4/1RR3K1 w - - 1 1");
    std::cout << "overall evaluation: " << evaluate(board) << std::endl;
    return 0;
}
