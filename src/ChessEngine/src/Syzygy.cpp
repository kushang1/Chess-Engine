#include "Syzygy.h"

#include <cstdio>
#include <sstream>

#include "Board.h"
#include "tbprobe.h"

#ifdef _WIN32
#include <io.h>
#endif

namespace {

bool g_tbInitialized = false;

inline int flip_sq(int sq)
{
    return sq ^ 56;
}

void syzygyLog(const std::string& text)
{
#ifdef _WIN32
    if (stderr == nullptr || _fileno(stderr) < 0) {
        return;
    }
#else
    if (stderr == nullptr) {
        return;
    }
#endif
    std::fprintf(stderr, "info string %s\n", text.c_str());
    std::fflush(stderr);
}

void syzygyLimitedProbeLog(const std::string& text)
{
    static int emitted = 0;
    if (emitted >= 32) {
        return;
    }

    ++emitted;
    syzygyLog(text);
}

const char* wdlName(unsigned wdl)
{
    switch (wdl) {
    case TB_LOSS: return "loss";
    case TB_BLESSED_LOSS: return "blessed_loss";
    case TB_DRAW: return "draw";
    case TB_CURSED_WIN: return "cursed_win";
    case TB_WIN: return "win";
    default: return "unknown";
    }
}

int scoreFromWdl(unsigned wdl)
{
    switch (wdl) {
    case TB_WIN:
    case TB_CURSED_WIN:
        return 30000;
    case TB_LOSS:
    case TB_BLESSED_LOSS:
        return -30000;
    case TB_DRAW:
    default:
        return 0;
    }
}

unsigned syzygyEpSquare(const board& b)
{
    if (!b.hasEnPassant || b.enPassantSquare < 0) {
        return 0;
    }

    return static_cast<unsigned>(flip_sq(b.enPassantSquare));
}

void build_bitboards(const board& b,
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

            int tbSq = flip_sq(sq);
            uint64_t bb = 1ULL << tbSq;

            if (b.isWhitePiece(piece)) {
                white |= bb;
            }
            else {
                black |= bb;
            }

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

bool prepareProbe(const board& b,
    const char* mode,
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
    if (!syzygyIsAvailable()) {
        std::ostringstream log;
        log << "syzygy " << mode << " probe skipped: tablebases are not available";
        syzygyLimitedProbeLog(log.str());
        return false;
    }

    build_bitboards(b, white, black, kings, queens, rooks, bishops, knights, pawns, pieceCount);

    if (pieceCount > static_cast<int>(TB_LARGEST)) {
        std::ostringstream log;
        log << "syzygy " << mode << " probe skipped: " << pieceCount
            << " pieces exceeds supported max " << TB_LARGEST;
        syzygyLimitedProbeLog(log.str());
        return false;
    }

    if (b.castleRights != 0) {
        std::ostringstream log;
        log << "syzygy " << mode << " probe skipped: castling rights are not supported";
        syzygyLimitedProbeLog(log.str());
        return false;
    }

    return true;
}

} // namespace

bool initSyzygy(const char* path)
{
    const char* safePath = path != nullptr ? path : "";
    g_tbInitialized = tb_init(safePath);

    std::ostringstream log;
    if (!g_tbInitialized) {
        log << "syzygy init failed: " << safePath;
        syzygyLog(log.str());
        return false;
    }

    if (TB_LARGEST == 0) {
        log << "syzygy path initialized but no tablebase files were found: " << safePath;
        syzygyLog(log.str());
        return false;
    }

    log << "syzygy path loaded: " << safePath
        << " max pieces supported: " << TB_LARGEST;
    syzygyLog(log.str());
    return true;
}

bool syzygyIsAvailable()
{
    return g_tbInitialized && TB_LARGEST != 0;
}

unsigned syzygyMaxPieces()
{
    return syzygyIsAvailable() ? TB_LARGEST : 0;
}

bool probeSyzygy(board& b, int& outScore, Move& outBestMove)
{
    outBestMove = Move();
    outScore = 0;

    uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    int pieceCount;
    if (!prepareProbe(b, "WDL", white, black, kings, queens, rooks, bishops, knights, pawns, pieceCount)) {
        return false;
    }

    unsigned wdl = tb_probe_wdl(
        white, black,
        kings, queens, rooks, bishops, knights, pawns,
        0, 0, syzygyEpSquare(b), b.isWhiteTurn
    );

    if (wdl == TB_RESULT_FAILED) {
        std::ostringstream log;
        log << "syzygy WDL probe failed/missing for " << pieceCount << "-piece position";
        syzygyLimitedProbeLog(log.str());
        return false;
    }

    outScore = scoreFromWdl(wdl);

    std::ostringstream log;
    log << "syzygy WDL probe hit: " << pieceCount
        << " pieces wdl=" << wdlName(wdl)
        << " score=" << outScore;
    syzygyLimitedProbeLog(log.str());
    return true;
}

bool probeSyzygyRoot(board& b, int& outScore, Move& outBestMove)
{
    outBestMove = Move();
    outScore = 0;

    uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    int pieceCount;
    if (!prepareProbe(b, "root DTZ", white, black, kings, queens, rooks, bishops, knights, pawns, pieceCount)) {
        return false;
    }

    unsigned tbRes = tb_probe_root(
        white, black,
        kings, queens, rooks, bishops, knights, pawns,
        static_cast<unsigned>(b.halfmoveClock),
        0,
        syzygyEpSquare(b),
        b.isWhiteTurn,
        nullptr
    );

    if (tbRes == TB_RESULT_FAILED) {
        std::ostringstream log;
        log << "syzygy root DTZ probe failed/missing for " << pieceCount << "-piece position";
        syzygyLog(log.str());
        return false;
    }

    if (tbRes == TB_RESULT_CHECKMATE || tbRes == TB_RESULT_STALEMATE) {
        unsigned terminalWdl = TB_GET_WDL(tbRes);
        outScore = scoreFromWdl(terminalWdl);

        std::ostringstream log;
        log << "syzygy root DTZ probe hit terminal position: wdl="
            << wdlName(terminalWdl) << " score=" << outScore;
        syzygyLog(log.str());
        return true;
    }

    unsigned wdl = TB_GET_WDL(tbRes);
    outScore = scoreFromWdl(wdl);

    int from = flip_sq(static_cast<int>(TB_GET_FROM(tbRes)));
    int to = flip_sq(static_cast<int>(TB_GET_TO(tbRes)));

    Piece moved = from < 64 ? b.pieceAt(from) : EMPTY;
    Piece captured = to < 64 ? b.pieceAt(to) : EMPTY;

    bool wasEnPassant = TB_GET_EP(tbRes) != 0;
    if (wasEnPassant) {
        captured = b.isWhiteTurn ? BP : WP;
    }

    Piece promoPiece = EMPTY;
    switch (TB_GET_PROMOTES(tbRes)) {
    case TB_PROMOTES_QUEEN:
        promoPiece = b.isWhiteTurn ? WQ : BQ;
        break;
    case TB_PROMOTES_ROOK:
        promoPiece = b.isWhiteTurn ? WR : BR;
        break;
    case TB_PROMOTES_BISHOP:
        promoPiece = b.isWhiteTurn ? WB : BB;
        break;
    case TB_PROMOTES_KNIGHT:
        promoPiece = b.isWhiteTurn ? WN : BN;
        break;
    default:
        break;
    }

    Move best(from, to, moved, captured, b.castleRights);
    best.wasEnPassant = wasEnPassant;
    if (promoPiece != EMPTY) {
        best.wasPromotion = true;
        best.promotedTo = promoPiece;
    }

    outBestMove = best;

    std::ostringstream log;
    log << "syzygy root DTZ probe hit: " << pieceCount
        << " pieces wdl=" << wdlName(wdl)
        << " dtz=" << TB_GET_DTZ(tbRes)
        << " score=" << outScore;
    syzygyLog(log.str());
    return true;
}
