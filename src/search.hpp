#pragma once

#include "chess.hpp"

using namespace chess;

// Constants
const int INF = 100000;
const int maxTranspositionTableSize = 100000000;
extern std::uint64_t positionCount;

// Function Declarations

/**
 * Generates a prioritized list of moves based on their tactical value.
 * @param board The chess board for which to generate moves.
 * @return A vector of move-priority pairs sorted by priority.
 */
std::vector<std::pair<Move, int>> generatePrioritizedMoves(Board& board);

/**
 * Performs quiescence search to evaluate a position.
 * @param board The current chess board state.
 * @param depth The remaining depth for the quiescence search.
 * @param alpha The alpha bound for alpha-beta pruning.
 * @param beta The beta bound for alpha-beta pruning.
 * @return The evaluation score of the position.
 */
int quiescence(
    Board& board, 
    int depth, 
    int alpha, 
    int beta
);

/**
 * Performs alpha-beta search to find the best move evaluation.
 * @param board The current chess board state.
 * @param depth The remaining depth for the search.
 * @param alpha The alpha bound for alpha-beta pruning.
 * @param beta The beta bound for alpha-beta pruning.
 * @param quiescenceDepth The depth for quiescence search.
 * @return The evaluation score of the position.
 */
int alphaBeta(
    Board& board, 
    int depth, 
    int alpha, 
    int beta, 
    int quiescenceDepth
);

/**
 * Performs alpha-beta search with move ordering and pruning.
 * @param board The current chess board state.
 * @param depth The remaining depth for the search.
 * @param lookAheadDepth The depth for the shallow search.
 * @param k The number of moves to consider.
 * @param alpha The alpha bound for alpha-beta pruning.
 * @param beta The beta bound for alpha-beta pruning.
 * @param quiescenceDepth The depth for quiescence search.
 * @return The evaluation score of the position.
 */
int alphaBetaPrune(Board& board, 
                   int depth, 
                   int lookAheadDepth, 
                   int k, 
                   int alpha, 
                   int beta, 
                   int quiescenceDepth);
                   
/**
 * Finds the best move for the current position using alpha-beta pruning.
 * @param board The current chess board state.
 * @param numThreads The number of threads to use for the search.
 * @param depth The normal search depth.
 * @param lookAheadDepth The depth for the shallow search.
 * @param k The number of moves to consider for next level.
 * @param quiescenceDepth The depth for quiescence search.
 * @return The best move for the current position.
 */
Move findBestMove(
    Board& board, 
    int numThreads, 
    int depth, 
    int lookAheadDepth,
    int k,
    int quiescenceDepth
);