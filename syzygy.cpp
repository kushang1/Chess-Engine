#include "syzygy.h"
#include "board.h"
#include "tbprobe.h"

static inline int flip_sq(int sq) {
    return sq ^ 56;   // maps your index to Syzygy index
}


// Simple global flag: did tb_init succeed?
static bool g_tbInitialized = false;

// ------------------------------------------------------------------
// INITIALIZE SYZYGY (call once from main or Engine ctor)
// ------------------------------------------------------------------
bool initSyzygy(const char* path)
{
    g_tbInitialized = tb_init(path);  // tb_init is from tbprobe.h
    return g_tbInitialized;
}

// ------------------------------------------------------------------
// Helper: build bitboards from your board representation
// ------------------------------------------------------------------
static void build_bitboards(board& b,
    uint64_t& white,
    uint64_t& black,
    uint64_t& kings,
    uint64_t& queens,
    uint64_t& rooks,
    uint64_t& bishops,
    uint64_t& knights,
    uint64_t& pawns,
    int& pieceCount)
{
    white = black = kings = queens = rooks = bishops = knights = pawns = 0;
    pieceCount = 0;

    for (int p = BQ; p <= WB; ++p) {
        Piece piece = static_cast<Piece>(p);
        for (int i = 0; i < b.pieceCount[piece]; ++i) {
            int sq = b.pieceList[piece][i];
            ++pieceCount;
            int tb_sq = flip_sq(sq);
            uint64_t bb = (1ULL << tb_sq);

            if (b.isWhitePiece(piece))
                white |= bb;
            else
                black |= bb;

            switch (piece) {
            case WK: case BK: kings |= bb; break;
            case WQ: case BQ: queens |= bb; break;
            case WR: case BR: rooks |= bb; break;
            case WB: case BB: bishops |= bb; break;
            case WN: case BN: knights |= bb; break;
            case WP: case BP: pawns |= bb; break;
            default: break;
            }
        }
    }
}

// ------------------------------------------------------------------
// MAIN PROBE FUNCTION
// ------------------------------------------------------------------
bool probeSyzygy(board& b, int& outScore, Move& outBestMove)
{
    outBestMove = Move();  // default invalid
    outScore = 0;

    if (!g_tbInitialized)
        return false;

    // If no TB files were found, TB_LARGEST is 0
    if (TB_LARGEST == 0)
        return false;

    uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    int pieceCount;
    build_bitboards(b, white, black, kings, queens, rooks, bishops, knights, pawns, pieceCount);

    // If more pieces than the largest TB we have, just skip
    if (pieceCount > (int)TB_LARGEST)
        return false;

    // 50-move and castling: for endgames we don't care about rule50 or castling
    // (Fathom ignores castling and requires rule50 == 0 in tb_probe_wdl).
    unsigned rule50 = 0;
    unsigned castling = 0;

    // EP square: 0 means "no EP"; EP can never be on a1 so this is safe.
    unsigned ep = 0;
    if (b.hasEnPassant)
        ep = flip_sq(b.enPassantSquare);


    bool turn = b.isWhiteTurn;

    // First try root probe (gives move + DTZ / WDL info)
    unsigned tbRes = tb_probe_root(
        white, black,
        kings, queens, rooks, bishops, knights, pawns,
        rule50, castling, ep, turn,
        nullptr // no per-move results array
    );

    if (tbRes == TB_RESULT_FAILED) {
        // Fall back to WDL-only probe
        unsigned wdl = tb_probe_wdl(
            white, black,
            kings, queens, rooks, bishops, knights, pawns,
            rule50, castling, ep, turn
        );

        if (wdl == TB_RESULT_FAILED)
            return false;

        int score;
        switch (wdl) {
        case TB_WIN:
        case TB_CURSED_WIN:
            score = 30000;  // big win
            break;
        case TB_LOSS:
        case TB_BLESSED_LOSS:
            score = -30000; // big loss
            break;
        case TB_DRAW:
        default:
            score = 0;
            break;
        }

        outScore = score;
        // No specific move from WDL probe alone
        outBestMove = Move();
        return true;
    }

    // Decode WDL from tbRes
    unsigned wdl = TB_GET_WDL(tbRes);
    int score;
    switch (wdl) {
    case TB_WIN:
    case TB_CURSED_WIN:
        score = 30000;
        break;
    case TB_LOSS:
    case TB_BLESSED_LOSS:
        score = -30000;
        break;
    case TB_DRAW:
    default:
        score = 0;
        break;
    }

    // Decode move from tbRes
    unsigned raw_from = TB_GET_FROM(tbRes);
    unsigned raw_to = TB_GET_TO(tbRes);

    int from = flip_sq(raw_from);   // convert to your indexing
    int to = flip_sq(raw_to);

    unsigned prom = TB_GET_PROMOTES(tbRes); // TB_PROMOTES_*

    Piece moved = EMPTY;
    Piece captured = EMPTY;

    if (from < 64)
        moved = b.pieceAt(from);
    if (to < 64)
        captured = b.pieceAt(to);

    // Map promotion flag to your Piece enum
    Piece promoPiece = EMPTY;
    if (prom != TB_PROMOTES_NONE) {
        bool whiteToMove = b.isWhiteTurn;
        switch (prom) {
        case TB_PROMOTES_QUEEN:
            promoPiece = whiteToMove ? WQ : BQ;
            break;
        case TB_PROMOTES_ROOK:
            promoPiece = whiteToMove ? WR : BR;
            break;
        case TB_PROMOTES_BISHOP:
            promoPiece = whiteToMove ? WB : BB;
            break;
        case TB_PROMOTES_KNIGHT:
            promoPiece = whiteToMove ? WN : BN;
            break;
        default:
            promoPiece = EMPTY;
            break;
        }
    }

    Move best(from, to, moved, captured, b.castleRights);
    if (promoPiece != EMPTY) {
        best.wasPromotion = true;
        best.promotedTo = promoPiece;
    }

    outBestMove = best;
    outScore = score;
    return true;
}
