#pragma once

#include "chess.hpp"

// Function Declarations

/**
 * Generates a prioritized list of moves based on their tactical value.
 * @param board The chess board for which to generate moves.
 * @return A vector of move-priority pairs sorted by priority.
 */
std::vector<std::pair<chess::Move, int>> generatePrioritizedMoves(chess::Board& board);

/**
 * Performs quiescence search to evaluate a position.
 * @param board The current chess board state.
 * @param depth The remaining depth for the quiescence search.
 * @param alpha The alpha bound for alpha-beta pruning.
 * @param beta The beta bound for alpha-beta pruning.
 * @param whiteTurn Indicates if it's white's turn.
 * @return The evaluation score of the position.
 */
int quiescence(chess::Board& board, int depth, int alpha, int beta, bool whiteTurn);

/**
 * Performs alpha-beta search to find the best move evaluation.
 * @param board The current chess board state.
 * @param depth The remaining depth for the search.
 * @param alpha The alpha bound for alpha-beta pruning.
 * @param beta The beta bound for alpha-beta pruning.
 * @param whiteTurn Indicates if it's white's turn.
 * @return The evaluation score of the position.
 */
int alphaBeta(chess::Board& board, int depth, int alpha, int beta, bool whiteTurn);

/**
 * Finds the best move for the current position using alpha-beta pruning.
 * @param board The current chess board state.
 * @param timeLimit The time limit for the search.
 * @return The best move for the current position.
 */
chess::Move findBestMove(chess::Board& board, int timeLimit);