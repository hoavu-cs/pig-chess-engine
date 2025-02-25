#pragma once

#include "chess.hpp"
#include "evaluation.hpp"

using namespace chess;

/*-------------------
    Helper Functions
-------------------*/

void bitBoardVisualize(const Bitboard& board);

int gamePhase(const Board& board);

int materialImbalance(const Board& board);

bool isPassedPawn(int sqIndex, Color color, const Bitboard& theirPawns);

int manhattanDistance(const Square& sq1, const Square& sq2);

int minDistance(const Square& sq, const Square& sq2);

int mopUpScore(const Board& board);

int moveScoreByTable(const Board& board, Move move);