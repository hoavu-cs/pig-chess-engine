#include "search.hpp"
#include "chess.hpp"
#include "evaluation.hpp"
#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <omp.h> // Include OpenMP header
#include <chrono>
#include <stdlib.h>
#include <cmath>
#include <unordered_set>

using namespace chess;

typedef std::uint64_t U64;


/*-------------------------------------------------------------------------------------------- 
    Constants and global variables.
--------------------------------------------------------------------------------------------*/
std::unordered_map<U64, std::pair<int, int>> transpositionTable; // Hash -> (eval, depth)
std::unordered_map<U64, Move> hashMoveTable; // Hash -> move

const int maxTableSize = 10000000; // Maximum size of the transposition table
U64 nodeCount; // Node count for each thread
std::vector<Move> previousPV; // Principal variation from the previous iteration
std::vector<std::vector<Move>> killerMoves(1000); // Killer moves

int globalMaxDepth = 0; // Maximum depth of current search
int globalQuiescenceDepth = 0; // Quiescence depth of current search
bool mopUp = false; // Mop up flag

const int ENGINE_DEPTH = 30; // Maximum search depth for the current engine version

// Basic piece values for move ordering, detection of sacrafices, etc.
const int pieceValues[] = {
    0,    // No piece
    100,  // Pawn
    320,  // Knight
    330,  // Bishop
    500,  // Rook
    900,  // Queen
    20000 // King
};

const int checkExtension = 1; // Number of plies to extend for checks
const int mateThreat = 1; // Number of plies to extend for mate threats
const int promotionExtension = 1; // Number of plies to extend for promotion threats.
const int oneReplyExtension = 1; // Number of plies to extend if there is only one legal move.

/*-------------------------------------------------------------------------------------------- 
    Transposition table lookup.
--------------------------------------------------------------------------------------------*/
bool transTableLookUp(U64 hash, int depth, int& eval) {    
    auto it = transpositionTable.find(hash);
    bool found = it != transpositionTable.end() && it->second.second >= depth;

    if (found) {
        eval = it->second.first;
        return true;
    } else {
        return false;
    }
}

/*-------------------------------------------------------------------------------------------- 
    Check if the move is a queen promotion.
--------------------------------------------------------------------------------------------*/
bool isQueenPromotion(const Move& move) {

    if (move.typeOf() & Move::PROMOTION) {
        if (move.promotionType() == PieceType::QUEEN) {
            return true;
        } 
    } 

    return false;
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


/*-------------------------------------------------------------------------------------------- 
    Check for tactical threats beside the obvious checks, captures, and promotions.
    To be expanded. 
--------------------------------------------------------------------------------------------*/
bool mateThreatMove(Board& board, Move move) {
    Color color = board.sideToMove();
    PieceType type = board.at<Piece>(move.from()).type();

    Bitboard theirKing = board.pieces(PieceType::KING, !color);

    int destinationIndex = move.to().index();   
    int destinationFile = destinationIndex % 8;
    int destinationRank = destinationIndex / 8;

    int theirKingFile = theirKing.lsb() % 8;
    int theirKingRank = theirKing.lsb() / 8;

    if (manhattanDistance(move.to(), Square(theirKing.lsb())) <= 3) {
        return true;
    }

    if (type == PieceType::ROOK || type == PieceType::QUEEN) {
        if (abs(destinationFile - theirKingFile) <= 1 && 
            abs(destinationRank - theirKingRank) <= 1) {
            return true;
        }
    }

    return false;
}

bool promotionThreatMove(Board& board, Move move) {
    Color color = board.sideToMove();
    PieceType type = board.at<Piece>(move.from()).type();

    if (type == PieceType::PAWN) {
        int destinationIndex = move.to().index();
        int rank = destinationIndex / 8;
        Bitboard theirPawns = board.pieces(PieceType::PAWN, !color);

        bool isPassedPawnFlag = isPassedPawn(destinationIndex, color, theirPawns);

        if (isPassedPawnFlag) {
            if ((color == Color::WHITE && rank > 3) || 
                (color == Color::BLACK && rank < 4)) {
                return true;
            }
        }
    }

    return false;
}

/*--------------------------------------------------------------------------------------------
    Late move reduction. No reduction for the first few moves, checks, or when in check.
    Reduce less on captures, checks, killer moves, etc.
    isPV is true if the node is a principal variation node. However, right now it's not used 
    since our move ordering is not that good.
--------------------------------------------------------------------------------------------*/
int lateMoveReduction(Board& board, Move move, int i, int depth, bool isPV) {

    Color color = board.sideToMove();
    board.makeMove(move);
    bool isCheck = board.inCheck(); 
    board.unmakeMove(move);

    bool isCapture = board.isCapture(move);
    bool inCheck = board.inCheck();
    bool isPromoting = isQueenPromotion(move);
    bool isMateThreat = mateThreatMove(board, move);
    bool isPromotionThreat = promotionThreatMove(board, move);

    int k = isPV ? 5 : 2;
    bool noReduceCondition = mopUp || isPromoting || isMateThreat || isPromotionThreat;
    bool reduceLessCondition = isCheck || inCheck || isCapture;
    int reduction = 0;

    int nonPVReduction = isPV ? 0 : 1;
    int k1 = isPV ? 2 : 1;
    int k2 = isPV ? 5 : 3;

    if (i <= k1 || depth <= 3  || noReduceCondition) { 
        return depth - 1;
    } else if (i <= k2 || reduceLessCondition) {
        return depth - 2;
    } else {
        return depth - 3;
    }

}


/*-------------------------------------------------------------------------------------------- 
    Returns a list of candidate moves ordered by priority.
--------------------------------------------------------------------------------------------*/
std::vector<std::pair<Move, int>> orderedMoves(
    Board& board, 
    int depth, std::vector<Move>& previousPV, 
    bool leftMost) {

    Movelist moves;
    movegen::legalmoves(moves, board);

    std::vector<std::pair<Move, int>> candidates;
    std::vector<std::pair<Move, int>> quietCandidates;

    candidates.reserve(moves.size());
    quietCandidates.reserve(moves.size());

    bool whiteTurn = board.sideToMove() == Color::WHITE;
    Color color = board.sideToMove();
    U64 hash = board.hash();

    // Move ordering 1. promotion 2. captures 3. killer moves 4. hash 5. checks 6. quiet moves
    for (const auto& move : moves) {
        int priority = 0;
        bool quiet = false;
        int moveIndex = move.from().index() * 64 + move.to().index();
        int ply = globalMaxDepth - depth;
        bool hashMove = false;

        // Previous PV move > hash moves > captures/killer moves > checks > quiet moves
        if (hashMoveTable.count(hash) && hashMoveTable[hash] == move) {
            priority = 9000;
            candidates.push_back({move, priority});
            hashMove = true;
        }
      
        if (hashMove) continue;
        
        if (previousPV.size() > ply && leftMost) {
            if (previousPV[ply] == move) {
                priority = 10000; // PV move
            }
        } else if (std::find(killerMoves[depth].begin(), killerMoves[depth].end(), move) != killerMoves[depth].end()) {
            priority = 2000; // Killer moves
        } else if (isQueenPromotion(move)) {
            priority = 6000; 
        } else if (board.isCapture(move)) { 
            auto victim = board.at<Piece>(move.to());
            auto attacker = board.at<Piece>(move.from());
            int victimValue = pieceValues[static_cast<int>(victim.type())];
            int attackerValue = pieceValues[static_cast<int>(attacker.type())];
            priority = 4000 + victimValue - attackerValue;
        } else {
            board.makeMove(move);
            bool isCheck = board.inCheck();
            board.unmakeMove(move);

            if (isCheck) {
                priority = 3000;
            } else {
                quiet = true;
                priority = 0;// quietPriority(board, move);
            }
        } 

        if (!quiet) {
            candidates.push_back({move, priority});
        } else {
            quietCandidates.push_back({move, priority});
        }
    }

    // Sort capture, promotion, checks by priority
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    for (const auto& move : quietCandidates) {
        candidates.push_back(move);
    }

    return candidates;
}

/*-------------------------------------------------------------------------------------------- 
    Quiescence search for captures only.
--------------------------------------------------------------------------------------------*/
int quiescence(Board& board, int depth, int alpha, int beta) {
    
    #pragma omp critical
    nodeCount++;

    int color = board.sideToMove() == Color::WHITE ? 1 : -1;

    int standPat = color * evaluate(board);
    alpha = std::max(alpha, standPat);
    
    if (depth <= 0) {
        return standPat;
    }
    
    int bestScore = standPat;

    if (standPat >= beta) {
        return beta;
    }

    alpha = std::max(alpha, standPat);

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
    std::vector<std::pair<Move, int>> candidateMoves;
    candidateMoves.reserve(moves.size());
    int biggestMaterialGain = 0;
    

    for (const auto& move : moves) {
        auto victim = board.at<Piece>(move.to());
        auto attacker = board.at<Piece>(move.from());
        int victimValue = pieceValues[static_cast<int>(victim.type())];
        int attackerValue = pieceValues[static_cast<int>(attacker.type())];

        int priority = victimValue - attackerValue;
        biggestMaterialGain = std::max(biggestMaterialGain, priority);
        candidateMoves.push_back({move, priority});
        
    }

    std::sort(candidateMoves.begin(), candidateMoves.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    for (const auto& [move, priority] : candidateMoves) {
        board.makeMove(move);
        int score = 0;
        score = -quiescence(board, depth - 1, -beta, -alpha);
        board.unmakeMove(move);

        bestScore = std::max(bestScore, score);
        alpha = std::max(alpha, score);

        if (alpha >= beta) { 
            return beta;
        }
    }

    return bestScore;
}

/*-------------------------------------------------------------------------------------------- 
    Alpha-beta.
--------------------------------------------------------------------------------------------*/
int alphaBeta(Board& board, 
            int depth, 
            int alpha, 
            int beta, 
            int quiescenceDepth,
            std::vector<Move>& PV,
            bool leftMost,
            int extension) {

    #pragma omp critical
    nodeCount++;

    bool whiteTurn = board.sideToMove() == Color::WHITE;
    bool endGameFlag = gamePhase(board) <= 12;
    int color = whiteTurn ? 1 : -1;

    // Check if the game is over
    auto gameOverResult = board.isGameOver();
    if (gameOverResult.first != GameResultReason::NONE) {
        if (gameOverResult.first == GameResultReason::CHECKMATE) {
            return color * (INF/2 - (1000 - depth)); 
        }
        return 0;
    }

    // Probe the transposition table
    U64 hash = board.hash();
    bool found = false;
    int storedEval;
    
    #pragma omp critical
    {
        if (transTableLookUp(hash, depth, storedEval) && storedEval >= beta) {
            found = true;
        }
    }

    if (found) {
        return storedEval;
    } 

    if (depth <= 0) {
        int quiescenceEval = quiescence(board, quiescenceDepth, alpha, beta);
        
        #pragma omp critical
        {
            transpositionTable[hash] = {quiescenceEval, 0};
        }
            
        return quiescenceEval;
    }

    bool isPV = (alpha < beta - 1); // Principal variation node flag
    int standPat = color * evaluate(board);

    // Only pruning if the position is not in check, mop up flag is not set, and it's not the endgame phase
    // Disable pruning for when alpha is very high to avoid missing checkmates
    bool pruningCondition = !board.inCheck() && !mopUp && !endGameFlag && !alpha > INF/4;

    //  Futility pruning
    if (depth < 3 && pruningCondition) {
        int margin = depth * 130;
        if (standPat - margin > beta) {
            // If the static evaluation - margin > beta, 
            // then it is considered to be too good and most likely a cutoff
            return standPat - margin;
        } 
    }

    // Razoring: Skip deep search if the position is too weak. Only applied to non-PV nodes.
    if (depth <= 3 && pruningCondition && !isPV) {
        int razorMargin = 300 + (depth - 1) * 60; // Threshold increases slightly with depth

        if (standPat + razorMargin < alpha) {
            // If the position is too weak and unlikely to raise alpha, skip deep search
            return quiescence(board, quiescenceDepth, alpha, beta);
        } 
    }

    // Null move pruning. Avoid null move pruning in the endgame phase.
    const int nullDepth = 4; // Only apply null move pruning at depths >= 4

    if (depth >= nullDepth && !endGameFlag && !leftMost && !board.inCheck()) {
        std::vector<Move> nullPV;
        int nullEval;
        int reduction = 3 + depth / 4;

        board.makeNullMove();
        nullEval = -alphaBeta(board, depth - reduction, -beta, -beta + 1, quiescenceDepth, nullPV, false, extension);
        board.unmakeNullMove();

        if (nullEval >= beta) { 
            // Even if we skip our move and the evaluation is >= beta, this is a cutoff since it is
            // a fail high (too good for us)
            return beta;
        } 
    }

    std::vector<std::pair<Move, int>> moves = orderedMoves(board, depth, previousPV, leftMost);
    int bestEval = -INF;

    for (int i = 0; i < moves.size(); i++) {

        Move move = moves[i].first;
        std::vector<Move> childPV;

        int eval = 0;
        int nextDepth = lateMoveReduction(board, move, i, depth, isPV); 
        
        if (i > 0) {
            leftMost = false;
        }

        board.makeMove(move);
        
        // Check for extensions
        bool isCheck = board.inCheck();
        bool isMateThreat = mateThreatMove(board, move);
        bool isPromotionThreat = promotionThreatMove(board, move);
        bool isOneReply = (moves.size() == 1);
        bool extensionFlag = (isCheck || isMateThreat || isPromotionThreat) && extension > 0; // if the move is a check, extend the search
        
        if (extensionFlag) {
            extension--; // Decrement the extension counter
            int numPlies = 0;

            if (isCheck) {
                numPlies = std::max(checkExtension, numPlies);
            } 
            
            if (isMateThreat) {
                numPlies = std::max(mateThreat, numPlies);
            } 

            if (isPromotionThreat) {
                numPlies = std::max(promotionExtension, numPlies);
            }

            if (isOneReply && !isCheck) { // Apply one-reply extension (not applied twice if the)
                numPlies = std::max(oneReplyExtension, numPlies);
            }

            nextDepth += numPlies;
        }

        if (isPV || leftMost) {
            // full window search for PV nodes and leftmost line (I think nodes in leftmost line should be PV nodes, but this is just in case)
            eval = -alphaBeta(board, depth - 1, -beta, -alpha, quiescenceDepth, childPV, leftMost, extension);
        } else {
            eval = -alphaBeta(board, nextDepth, -(alpha + 1), -alpha, quiescenceDepth, childPV, leftMost, extension);
        }
        
        board.unmakeMove(move);

        // re-search if we raise alpha with reduced depth
        bool alphaRaised = eval > alpha;

        if (leftMost || (alphaRaised && nextDepth < depth - 1)) {
            board.makeMove(move);
            eval = -alphaBeta(board, depth - 1, -beta, -alpha, quiescenceDepth, childPV, leftMost, extension);
            board.unmakeMove(move);
        }

        if (eval > alpha) {
            PV.clear();
            PV.push_back(move);
            for (auto& move : childPV) {
                PV.push_back(move);
            }
        }

        bestEval = std::max(bestEval, eval);
        alpha = std::max(alpha, eval);

        if (beta <= alpha) {
            updateKillerMoves(move, depth);
            break;
        }
    }

    #pragma omp critical
    {
        // Update hash tables
        if (PV.size() > 0) {
            transpositionTable[board.hash()] = {bestEval, depth}; 
            hashMoveTable[board.hash()] = PV[0];
        }
    }

    return bestEval;
}

/*-------------------------------------------------------------------------------------------- 
    Main search function to communicate with UCI interface.
--------------------------------------------------------------------------------------------*/
Move findBestMove(Board& board, 
                int numThreads = 4, 
                int maxDepth = 8, 
                int quiescenceDepth = 10, 
                int timeLimit = 15000,
                bool quiet = false) {

    auto startTime = std::chrono::high_resolution_clock::now();
    bool timeLimitExceeded = false;

    Move bestMove = Move(); 
    int bestEval = -INF;
    int color = board.sideToMove() == Color::WHITE ? 1 : -1;

    std::vector<std::pair<Move, int>> moves;
    std::vector<Move> globalPV (maxDepth);

    if (board.us(Color::WHITE).count() == 1 || board.us(Color::BLACK).count() == 1) {
        mopUp = true;
    }


    globalQuiescenceDepth = quiescenceDepth;
    omp_set_num_threads(numThreads);

    // Clear transposition tables
    #pragma omp critical
    {
        if (transpositionTable.size() > maxTableSize) {
            transpositionTable = {};
            hashMoveTable = {};
            clearPawnHashTable();
        }
    }
    
    const int baseDepth = 1;
    int apsiration = color * evaluate(board);
    int depth = baseDepth;
    int evals[ENGINE_DEPTH + 1];
    Move candidateMove[ENGINE_DEPTH + 1];

    while (depth <= maxDepth) {
        nodeCount = 0;
        globalMaxDepth = depth;
        
        // Track the best move for the current depth
        Move currentBestMove = Move();
        int currentBestEval = -INF;
        std::vector<std::pair<Move, int>> newMoves;
        std::vector<Move> PV; // Principal variation

        if (depth == baseDepth) {
            moves = orderedMoves(board, depth, previousPV, false);
        }


        auto iterationStartTime = std::chrono::high_resolution_clock::now();
        bool unfinished = false;


        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < moves.size(); i++) {

            bool leftMost = (i == 0);

            Move move = moves[i].first;
            std::vector<Move> childPV; 
            int extension = 4;
        
            Board localBoard = board;
            bool newBestFlag = false;  
            int nextDepth = lateMoveReduction(localBoard, move, i, depth, true);
            int eval = -INF;
            int aspiration;

            if (depth == 1) {
                aspiration = color * evaluate(localBoard); // if at depth = 1, aspiration = static evaluation
            } else {
                aspiration = evals[depth - 1]; // otherwise, aspiration = previous depth evaluation
            }

            // aspiration window search
            int windowLeft = 50;
            int windowRight = 50;

            while (true) {

                localBoard.makeMove(move);

                // Check for extensions
                bool isCheck = board.inCheck();
                bool isMateThreat = mateThreatMove(board, move);
                bool isPromotionThreat = promotionThreatMove(board, move);
                bool isOneReply = (moves.size() == 1);
                bool extensionFlag = (isCheck || isMateThreat || isPromotionThreat) && extension > 0; // if the move is a check, extend the search
                
                if (extensionFlag) {
                    extension--; // Decrement the extension counter
                    int numPlies = 0;

                    if (isCheck) {
                        numPlies = std::max(checkExtension, numPlies);
                    } 
                    
                    if (isMateThreat) {
                        numPlies = std::max(mateThreat, numPlies);
                    } 

                    if (isPromotionThreat) {
                        numPlies = std::max(promotionExtension, numPlies);
                    }

                    if (isOneReply && !isCheck) { // Apply one-reply extension (not applied twice if the)
                        numPlies = std::max(oneReplyExtension, numPlies);
                    }

                    nextDepth += numPlies;
                }

                int alpha = aspiration - windowLeft;
                int beta = aspiration + windowRight;
                eval = -alphaBeta(localBoard, nextDepth, -beta, -alpha, quiescenceDepth, childPV, leftMost, extension);
                localBoard.unmakeMove(move);

                if (eval <= aspiration - windowLeft) {
                    windowLeft *= 2;
                } else if (eval >= aspiration + windowRight) {
                    windowRight *= 2;
                } else {
                    break;
                }
            }

            #pragma omp critical
            {
                if (eval > currentBestEval) {
                    newBestFlag = true;
                }
            }

            if (newBestFlag && nextDepth < depth - 1) {
                localBoard.makeMove(move);
                eval = -alphaBeta(localBoard, depth - 1, -INF, INF, quiescenceDepth, childPV, leftMost, extension);
                localBoard.unmakeMove(move);
            }

            // std::cout << "Eval: " << eval << " after move " << uci::moveToUci(move) << std::endl;

            #pragma omp critical
            newMoves.push_back({move, eval});

            #pragma omp critical
            {
                if (eval > currentBestEval) {
                    currentBestEval = eval;
                    currentBestMove = move;

                    PV.clear();
                    PV.push_back(move);
                    for (auto& move : childPV) {
                        PV.push_back(move);
                    }
                }
            }
        }
        
        // Update the global best move and evaluation after this depth if the time limit is not exceeded
        bestMove = currentBestMove;
        bestEval = currentBestEval;

        // Sort the moves by evaluation for the next iteration
        std::sort(newMoves.begin(), newMoves.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        #pragma omp critical
        {
            transpositionTable[board.hash()] = {bestEval, depth};
        }


        moves = newMoves;
        previousPV = PV;

        std::string depthStr = "depth " +  std::to_string(PV.size());
        std::string scoreStr = "score cp " + std::to_string(bestEval);
        std::string nodeStr = "nodes " + std::to_string(nodeCount);

        auto iterationEndTime = std::chrono::high_resolution_clock::now();
        std::string timeStr = "time " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(iterationEndTime - iterationStartTime).count());

        std::string pvStr = "pv ";
        for (const auto& move : PV) {
            pvStr += uci::moveToUci(move) + " ";
        }

        std::string analysis = "info " + depthStr + " " + scoreStr + " " +  nodeStr + " " + timeStr + " " + " " + pvStr;
        std::cout << analysis << std::endl;

        if (moves.size() == 1) {
            return moves[0].first;
        }


        auto currentTime = std::chrono::high_resolution_clock::now();
        bool spendTooMuchTime = false;
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

        timeLimitExceeded = duration > timeLimit;
        spendTooMuchTime = duration > 2 * timeLimit;

        evals[depth] = bestEval;
        candidateMove[depth] = bestMove; 

        // Check for stable evaluation
        bool stableEval = true;
        if (depth >= 4 && depth <= ENGINE_DEPTH) {  
            for (int i = 0; i < 4; i++) {
                if (std::abs(evals[depth - i] - evals[depth - i - 1]) > 25) {
                    stableEval = false;
                    break;
                }
            }
        }

        // Break out of the loop if the time limit is exceeded and the evaluation is stable.
        if (!timeLimitExceeded) {
            depth++;
        } else if (stableEval) {
            break;
        } else {
            if (depth > ENGINE_DEPTH || spendTooMuchTime) {
                break;
            } 
            
            depth++;
        }
    }
    
    #pragma omp critical
    {
        if (transpositionTable.size() > maxTableSize) {
            transpositionTable = {};
            hashMoveTable = {};
            clearPawnHashTable();
        }
    }

    return bestMove; 
}