#include "chess.hpp"
#include "evaluation.hpp"
#include "search.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <tuple> 
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <unordered_map>

using namespace chess;

void writePNGToFile(const std::vector<std::string>& pgnMoves, std::string filename) {
    std::ofstream pgnFile("game.pgn");
    if (pgnFile.is_open()) {
        pgnFile << "[Event \"AI vs AI\"]\n";
        pgnFile << "[Site \"Local\"]\n";
        pgnFile << "[Date \"2024.11.29\"]\n";
        pgnFile << "[Round \"1\"]\n";
        pgnFile << "[White \"AI\"]\n";
        pgnFile << "[Black \"AI\"]\n";
        pgnFile << "[Result \"" << (pgnMoves.back().find("1-0") != std::string::npos
                                      ? "1-0"
                                      : pgnMoves.back().find("0-1") != std::string::npos
                                            ? "0-1"
                                            : "1/2-1/2") << "\"]\n\n";

        for (const auto& move : pgnMoves) {
            pgnFile << move << " ";
        }
        pgnFile << "\n";
        pgnFile.close();
    }
}

int main() {
    // Board board1 = Board("r4rk1/pp3ppp/2pp1q2/2P1p3/N1PnP3/P4N1P/2P2PP1/R2Q1RK1 b - - 0 14");
    // std::cout << "Eval = " << evaluate(board1) << std::endl;

    Board board = Board("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    // Tactical fen 
    //Board board = Board("2rq1rk1/pp3ppp/2p2n2/3p3P/3P1n2/2N2N2/PPPQ1PP1/1K1R3R b - - 2 16");
    std::vector<std::string> pgnMoves; // Store moves in PGN format
    //board = Board("2rq1rk1/pp3ppp/2p2n2/3p3P/3P1n2/2N2N2/PPPQ1PP1/1K1R3R b - - 2 16");

    // board = Board("rnbq1rk1/1pN2ppp/p3p3/2bp4/4n3/3BPNB1/PPP2PPP/R2QK2R b KQ - 1 10");
    // board = Board("4r1k1/1pq2ppp/p7/2Pp4/P1b1rR2/2P1P1Q1/6PP/R1B3K1 b - - 3 24");
    // board = Board("rnbq1rk1/1pN2ppp/p3p3/2bp4/4n3/3BPNB1/PPP2PPP/R2QK2R b KQ - 1 10");
    // board = Board("3rr1k1/1ppbqppp/p1nbpn2/3pN3/3P1P2/P1NQP1B1/1PP1B1PP/R4RK1 b - - 0 12");
    // board = Board("r1br4/2kp2pp/ppnRP3/8/P1B5/2N5/1P4PP/2R3K1 w - - 1 26");
    // board = Board("r3kbnr/pp1n1ppp/4p3/2ppP3/8/2N2N2/PPPP1PPP/R1B1K2R b KQkq - 1 8");
    // board = Board("1r1q1rk1/1ppb1pp1/1bn1p1np/p3P3/P1Bp2QP/1N1P1N2/1PP2PP1/R1B1R1K1 b - - 0 14");
    //board = Board("3r1rk1/1pqnbppp/p3p1n1/2p1P3/3pQ2P/N2P1N2/PPP2PP1/R1B1R1K1 b - - 0 14");
    // board = Board("rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2");
    // board = Board("r3kb1r/ppp1qp2/2nnb2p/6p1/3N4/2NBB1Q1/PPP2PPP/R3K2R b KQkq - 1 12");

    // Default settings
    int depth = 20;
    int quiescenceDepth = 10;
    int numThreads = 6;
    int timeLimit = 30000;

    Move bestMove;

    int moveCount = 40;
    bool resetHistory = true;

    for (int i = 0; i < moveCount; i++) {
        // Start timer
        auto start = std::chrono::high_resolution_clock::now();

        Move bestMove = findBestMove(board, numThreads, depth, quiescenceDepth, timeLimit, true, resetHistory);
        resetHistory = false;

        // End timer
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = end - start;
        std::cout << "(Time taken: " << duration.count() << "s" << "; NPS: " << positionCount / duration.count() << ")"<< std::endl;

        if (bestMove == Move::NO_MOVE) {
            auto gameResult = board.isGameOver();
            if (gameResult.first == GameResultReason::CHECKMATE) {
                pgnMoves.push_back(board.sideToMove() == Color::WHITE ? "0-1" : "1-0");
            } else {
                pgnMoves.push_back("1/2-1/2");
            }
            break;
        }
        
        board.makeMove(bestMove);
        std::cout << "Move " << i + 1 << ": " << uci::moveToUci(bestMove) << std::endl;
        
        // std::string moveStr = uci::moveToUci(bestMove);
        
        // if (board.sideToMove() == Color::BLACK) {
        //     pgnMoves.push_back(std::to_string((i / 2) + 1) + ". " + moveStr);
        // } else {
        //     pgnMoves.back() += " " + moveStr;
        // }
    }

    // Write PGN to file
    // writePNGToFile(pgnMoves, "game.pgn");
    // std::cout << "Game saved to game.pgn" << std::endl;

    return 0;
}
