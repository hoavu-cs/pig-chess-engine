#include "search.hpp"
#include "chess.hpp"
#include "evaluation.hpp"
#include <iostream>
#include <map>
#include <tuple> 
#include <string>
#include <vector>
#include <algorithm>
#include <omp.h> // Include OpenMP header
#include <chrono>
#include <stdlib.h>
#include <cmath>

using namespace chess;

// Constants and global variables
std::map<std::uint64_t, std::pair<int, int>> lowerBoundTable; // Hash -> (eval, depth)
std::map<std::uint64_t, std::pair<int, int>> upperBoundTable; // Hash -> (eval, depth)

// History heuristic tables
std::vector<std::vector<int>> whiteHistory (64, std::vector<int>(64, 0)); 
std::vector<std::vector<int>> blackHistory (64, std::vector<int>(64, 0));

std::vector<std::vector<Move>> killerMoves(100); // Killer moves
uint64_t positionCount = 0; // Number of positions evaluated for benchmarking

const size_t transTableMaxSize = 1000000000; 
const int R = 2; 
//const int razorMargin = 350; 
//int razorPly = 6; 

int nullDepth = 4; 
int improvement = 0;
int globalMaxDepth = 0; // Maximum depth of current search
int globalQuiescenceDepth = 0; // Quiescence depth
bool globalDebug = false; // Debug flag

// Basic piece values for move ordering
const int pieceValues[] = {
    0,    // No piece
    100,  // Pawn
    320,  // Knight
    330,  // Bishop
    500,  // Rook
    900,  // Queen
    20000 // King
};

// Transposition table lookup
bool transTableLookUp(std::map<std::uint64_t, std::pair<int, int>>& table, 
                            std::uint64_t hash, 
                            int depth, 
                            int& eval) {
    auto it = table.find(hash);
    bool found = it != table.end() && it->second.second >= depth;

    if (found) {
        eval = it->second.first;
        return true;
    } else {
        return false;
    }
}

bool isPromotion(const Move& move) {
    return (move.typeOf() & Move::PROMOTION) != 0;
}

// Update the killer moves
void updateKillerMoves(const Move& move, int depth) {
    #pragma omp critical
    {
        if (killerMoves[depth].size() < 2) {
            killerMoves[depth].push_back(move);
        } else {
            killerMoves[depth][1] = killerMoves[depth][0];
            killerMoves[depth][0] = move;
        }
    }
}

// Late move reduction
int depthReduction(Board& board, Move move, int i, int depth) {
    // if (i <= 5) {
    //     return depth - 1;
    // }  else if (i <= 10) {
    //     return std::max(depth - 2, depth / 2);
    // } else {
    //     return depth / 2;
    // }

    // if (i <= 2 || depth <= 2) {
    //     return depth - 1;
    // } else {
    //     return std::min(depth - 1, depth / 3);
    // }

    if (i <= 2 || depth <= 2) {
        return depth - 1;
    } 

    int R = 1 + 0.75 * log(depth) / log(2.0) + 0.75 * log(i) / log(2.0);
    return depth - R;
}

// Generate a prioritized list of moves based on their tactical value
std::vector<std::pair<Move, int>> prioritizedMoves(Board& board, int depth) {

    Movelist moves;
    movegen::legalmoves(moves, board);
    std::vector<std::pair<Move, int>> candidates;
    std::vector<std::pair<Move, int>> quietCandidates;

    bool whiteTurn = board.sideToMove() == Color::WHITE;

    // Move ordering 1. promotion 2. captures 3. killer moves 4. check moves
    for (const auto& move : moves) {
        int priority = 0;
        bool quiet = false;

        if (isPromotion(move)) {
            priority = 5000; 
        } else if (board.isCapture(move)) { 
            // MVV-LVA priority for captures
            auto victim = board.at<Piece>(move.to());
            auto attacker = board.at<Piece>(move.from());

            priority = 4000 + pieceValues[static_cast<int>(victim.type())] 
                            - pieceValues[static_cast<int>(attacker.type())];
        } else if (std::find(killerMoves[depth].begin(), killerMoves[depth].end(), move) != killerMoves[depth].end()) {
            priority = 3000;
        } else {
            board.makeMove(move);
            bool isCheck = board.inCheck();
            board.unmakeMove(move);

            if (isCheck) {
                priority = 2000;
            } else {
                // quite move
                int from = move.from().index(), to = move.to().index();
                quiet = true;

                if (whiteTurn) {
                    priority = whiteHistory[move.from().index()][move.to().index()];
                } else {
                    priority = blackHistory[move.from().index()][move.to().index()];
                }
            }
        } 
        if (!quiet) {
            candidates.push_back({move, priority});
        } else {
            int from = move.from().index(), to = move.to().index();
            quietCandidates.push_back({move, priority});
        }
    }

    // Sort capture, promotion, checks by priority
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    // Sort the quiet moves by priority
    std::sort(quietCandidates.begin(), quietCandidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    for (const auto& move : quietCandidates) {
        candidates.push_back(move);
    }

    return candidates;
}

int quiescence(Board& board, int depth, int alpha, int beta, int numChecks) {

    const int maxChecks = 4;

    if (globalDebug) {
        #pragma  omp critical
        positionCount++;
    }
    
    if (depth == 0) {
        return evaluate(board);
    }

    bool inCheck = board.inCheck();
    bool whiteTurn = board.sideToMove() == Color::WHITE;

    // Stand-pat evaluation: Evaluate the static position
    if (!inCheck) {
        int standPat = evaluate(board);
            
        if (whiteTurn) {
            if (standPat >= beta) {
                return beta;
            }
            if (standPat > alpha) {
                alpha = standPat;
            }
        } else {
            if (standPat <= alpha) {
                return alpha;
            }
            if (standPat < beta) {
                beta = standPat;
            }
        }
    }

    Movelist moves;
    movegen::legalmoves(moves, board);
    std::vector<std::pair<Move, int>> candidateMoves;

    for (const auto& move : moves) {

        board.makeMove(move);
        bool isCheck = board.inCheck();
        board.unmakeMove(move);

        if (!board.isCapture(move) && !isPromotion(move) && !isCheck && !inCheck) {
            continue;
        }

        if (numChecks > maxChecks && isCheck) {
            continue; // Skip the check moves if we have reached the maximum number of checks
        }

        if (isPromotion(move)) {
            candidateMoves.push_back({move, 5000});
            continue;
        } else if (board.isCapture(move)) {
            auto victim = board.at<Piece>(move.to());
            auto attacker = board.at<Piece>(move.from());

            int priority = pieceValues[static_cast<int>(victim.type())] - pieceValues[static_cast<int>(attacker.type())];
            candidateMoves.push_back({move, priority});
        } 
    }

    std::sort(candidateMoves.begin(), candidateMoves.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    
    for (const auto& [move, priority] : candidateMoves) {

        board.makeMove(move);
        int score;

        if (board.inCheck()) {
            score = quiescence(board, depth - 1, alpha, beta, numChecks + 1);
        } else {
            score = quiescence(board, depth - 1, alpha, beta, numChecks);
        }

        board.unmakeMove(move);

        if (whiteTurn) {
            if (score >= beta) {
                return beta;
            }
            if (score > alpha) {
                alpha = score;
            }
        } else {
            if (score <= alpha) {
                return alpha;
            }
            if (score < beta) {
                beta = score;
            }
        }
    }

    return whiteTurn ? alpha : beta;
}

int alphaBeta(Board& board, 
            int depth, 
            int alpha, 
            int beta, 
            int quiescenceDepth,
            std::vector<Move>& PV) {

    if (globalDebug) {
        #pragma  omp critical
        positionCount++;
    }

    bool whiteTurn = board.sideToMove() == Color::WHITE;

    // Check if the game is over
    auto gameOverResult = board.isGameOver();

    if (gameOverResult.first != GameResultReason::NONE) {
        if (gameOverResult.first == GameResultReason::CHECKMATE) {
            if (whiteTurn) {
                return -INF/2 + (1000 - depth); // Get the fastest checkmate possible
            } else {
                return INF/2 - (1000 - depth); 
            }
        }
        return 0;
    }

    // Probe the transposition table
    std::uint64_t hash = board.hash();
    bool found = false;
    int storedEval;

    #pragma omp critical
    { 
        if ((whiteTurn && transTableLookUp(lowerBoundTable, hash, depth, storedEval) && storedEval >= beta) ||
            (!whiteTurn && transTableLookUp(upperBoundTable, hash, depth, storedEval) && storedEval <= alpha)) {
            found = true;
        }
    }

    if (found) {
        return storedEval;
    } 

    if (depth <= 0) {
        int quiescenceEval = quiescence(board, quiescenceDepth, alpha, beta, 0);
        
        if (whiteTurn) {
            #pragma omp critical
            lowerBoundTable[hash] = {quiescenceEval, depth};
        } else {
            #pragma omp critical
            upperBoundTable[hash] = {quiescenceEval, depth};
        }
        
        return quiescenceEval;
    }

    // Null move pruning
    if (depth >= nullDepth) {
        if (!board.inCheck()) {
            board.makeNullMove();
            std::vector<Move> nullPV;
            int nullEval = alphaBeta(board, 
                                    depth - R,  
                                    alpha, 
                                    beta, 
                                    quiescenceDepth,
                                    nullPV);
            board.unmakeNullMove();

            if (whiteTurn && nullEval >= beta) {
                return beta;
            } else if (!whiteTurn && nullEval <= alpha) {
                return alpha;
            }
        }
    }

    std::vector<std::pair<Move, int>> moves = prioritizedMoves(board, depth);
    int bestEval = whiteTurn ? -INF : INF;

    for (int i = 0; i < moves.size(); i++) {
        Move move = moves[i].first;

        // Apply Late Move Reduction (LMR)
        int nextDepth = depthReduction(board, move, i, depth);

        board.makeMove(move);
        std::vector<Move> pvChild;
        int eval = alphaBeta(board, nextDepth, alpha, beta, quiescenceDepth, pvChild);
        board.unmakeMove(move);

        if (whiteTurn) {
            if (eval > alpha && nextDepth < depth - 1) { // Re-search to full depth if a potential PV node is found
                board.makeMove(move);
                eval = alphaBeta(board, depth - 1, alpha, beta, quiescenceDepth, pvChild);
                board.unmakeMove(move);
            }

            if (eval > alpha) {
                PV.clear();
                PV.push_back(move);
                for (auto& move : pvChild) {
                    PV.push_back(move);
                }
            }

            bestEval = std::max(bestEval, eval);
            alpha = std::max(alpha, eval);
        } else {
            if (eval < beta && nextDepth < depth - 1) { // Re-search to full depth if a potential PV node is found
                board.makeMove(move);
                eval = alphaBeta(board, depth - 1, alpha, beta, quiescenceDepth, pvChild);
                board.unmakeMove(move);
            }

            if (eval < beta) {
                PV.clear();
                PV.push_back(move);
                for (auto& move : pvChild) {
                    PV.push_back(move);
                }
            }

            bestEval = std::min(bestEval, eval);
            beta = std::min(beta, eval);
        }

        if (beta <= alpha) {
            updateKillerMoves(move, depth);

            if (!board.isCapture(move)) {
                int fromSq = move.from().index(), toSq = move.to().index();
                if (whiteTurn) {
                    whiteHistory[fromSq][toSq] += depth * depth;
                } else {
                    blackHistory[fromSq][toSq] += depth * depth;
                }
            }

            break;
        }
    }

    if (whiteTurn) {
            #pragma omp critical
            lowerBoundTable[board.hash()] = {bestEval, depth}; 
    } else {
            #pragma omp critical
            upperBoundTable[board.hash()] = {bestEval, depth}; 
    }

    return bestEval;
}


Move findBestMove(Board& board, 
                  int numThreads = 4, 
                  int maxDepth = 6, 
                  int quiescenceDepth = 10, 
                  int timeLimit = 5000,
                  bool debug = false,
                  bool resetHistory = false) {
    // Initialize timer

    if (resetHistory) {
        whiteHistory = std::vector<std::vector<int>>(64, std::vector<int>(64, 0)); 
        blackHistory = std::vector<std::vector<int>>(64, std::vector<int>(64, 0));
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    Move bestMove = Move(); 
    int bestEval = (board.sideToMove() == Color::WHITE) ? -INF : INF;
    bool whiteTurn = board.sideToMove() == Color::WHITE;

    std::vector<std::pair<Move, int>> moves;
    std::vector<Move> globalPV (maxDepth);

    globalDebug = debug;
    globalQuiescenceDepth = quiescenceDepth;
    omp_set_num_threads(numThreads);

    // Clear transposition tables
    #pragma omp critical
    {
        if (lowerBoundTable.size() > transTableMaxSize) {
            lowerBoundTable.clear();
        }
        if (upperBoundTable.size() > transTableMaxSize) {
            upperBoundTable.clear();
        }  
    }

    bool timeLimitExceeded = false;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        globalMaxDepth = depth;
        
        // Track the best move for the current depth
        Move currentBestMove = Move();
        int currentBestEval = whiteTurn ? -INF : INF;

        std::vector<std::pair<Move, int>> newMoves;
        std::vector<Move> PV; // Principal variation

        if (depth == 1) {
            moves = prioritizedMoves(board, depth);
        }

        #pragma omp parallel
        {
            #pragma omp for
            for (int i = 0; i < moves.size(); i++) {

                if (timeLimitExceeded) {
                    continue; // Skip iteration if time limit is exceeded
                }

                Move move = moves[i].first;
                std::vector<Move> childPV; 
                
                Board localBoard = board;
            
                localBoard.makeMove(move);
                int nextDepth = depthReduction(localBoard, move, i, depth);
                int eval = alphaBeta(localBoard, nextDepth, -INF, INF, quiescenceDepth, childPV);
                localBoard.unmakeMove(move);

                bool newBestMoveFlag = false;
                #pragma omp critical
                {
                    if ((whiteTurn && eval > currentBestEval) || (!whiteTurn && eval < currentBestEval)) {
                        newBestMoveFlag = true;
                    }

                }

                if (newBestMoveFlag && nextDepth < depth - 1) { // Re-search if a new PV node is found
                    localBoard.makeMove(move);
                    eval = alphaBeta(localBoard, depth - 1, -INF, INF, quiescenceDepth, childPV);
                    localBoard.unmakeMove(move);
                }

                #pragma omp critical
                newMoves.push_back({move, eval});

                #pragma omp critical
                {
                    if ((whiteTurn && eval > currentBestEval) || 
                        (!whiteTurn && eval < currentBestEval)) {
                        currentBestEval = eval;
                        currentBestMove = move;

                        PV.clear();
                        PV.push_back(move);
                        for (auto& move : childPV) {
                            PV.push_back(move);
                        }
                    }
                }

                // Check time limit
                auto currentTime = std::chrono::high_resolution_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() >= timeLimit) {
                    #pragma omp critical
                    {
                        timeLimitExceeded = true;
                    }
                }
            }
        }

        // Update the global best move and evaluation after this depth
        bestMove = currentBestMove;
        bestEval = currentBestEval;

        if (whiteTurn) {
            std::sort(newMoves.begin(), newMoves.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });
        } else {
            std::sort(newMoves.begin(), newMoves.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
        }

        if (debug) {
            std::cout << "---------------------------------" << std::endl;
            for (int j = 0; j < std::min<int>(5, newMoves.size()); j++) {
                std::cout << "Depth: " << depth 
                        << " Move: " << uci::moveToUci(newMoves[j].first) 
                        << " Eval: " << newMoves[j].second << std::endl;
            }
            std::cout << "PV: ";
            
            for (const auto& move : PV) {
                std::cout << uci::moveToUci(move) << " ";
            }

            std::cout << std::endl;
        }

        moves = newMoves;

        // Check time limit again
        auto currentTime = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() >= timeLimit) {
            break; 
        }
    }

    return bestMove; 
}