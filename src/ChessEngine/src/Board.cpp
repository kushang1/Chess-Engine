#include "Board.h"

#include <algorithm>
#include <cctype>

#include "Profiler.h"
#include "ZobristHashing.h"

namespace {

constexpr const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

} // namespace

board::board() {
    Bitboards::init();
    ZobristData::init();
    resetBoard();
}

void board::clearBoard() {
    std::fill(std::begin(pieceBB), std::end(pieceBB), 0ULL);
    whiteOcc = 0;
    blackOcc = 0;
    occupied = 0;

    for (int p = 0; p < 13; ++p) {
        std::fill(std::begin(pieceList[p]), std::end(pieceList[p]), -1);
        pieceCount[p] = 0;
    }

    std::fill(std::begin(pieceIndex), std::end(pieceIndex), -1);
    std::fill(std::begin(squareBoard), std::end(squareBoard), EMPTY);

    isWhiteTurn = true;
    castleRights = 0;
    hasEnPassant = false;
    enPassantSquare = -1;
    halfmoveClock = 0;
    fullmoveNumber = 1;
    whiteKingSquare = -1;
    blackKingSquare = -1;
    hash = 0;
}

void board::addPiece(Piece p, int sq) {
    if (p == EMPTY || sq < 0 || sq >= 64) {
        return;
    }

    Bitboard sqMask = Bitboards::bit(sq);
    pieceBB[p] |= sqMask;
    occupied |= sqMask;

    if (isWhitePiece(p)) {
        whiteOcc |= sqMask;
        if (p == WK) {
            whiteKingSquare = sq;
        }
    }
    else {
        blackOcc |= sqMask;
        if (p == BK) {
            blackKingSquare = sq;
        }
    }

    int idx = pieceCount[p]++;
    pieceList[p][idx] = sq;
    pieceIndex[sq] = idx;
    squareBoard[sq] = p;

    hash ^= ZobristData::pieceSquare(p, sq);
}

void board::removePiece(Piece p, int sq) {
    if (p == EMPTY || sq < 0 || sq >= 64) {
        return;
    }

    Bitboard sqMask = Bitboards::bit(sq);
    pieceBB[p] &= ~sqMask;
    occupied &= ~sqMask;

    if (isWhitePiece(p)) {
        whiteOcc &= ~sqMask;
        if (p == WK) {
            whiteKingSquare = -1;
        }
    }
    else {
        blackOcc &= ~sqMask;
        if (p == BK) {
            blackKingSquare = -1;
        }
    }

    int idx = pieceIndex[sq];
    if (idx >= 0) {
        int lastIdx = --pieceCount[p];
        int lastSq = pieceList[p][lastIdx];
        pieceList[p][idx] = lastSq;
        pieceList[p][lastIdx] = -1;
        if (idx != lastIdx) {
            pieceIndex[lastSq] = idx;
        }
        pieceIndex[sq] = -1;
    }
    squareBoard[sq] = EMPTY;

    hash ^= ZobristData::pieceSquare(p, sq);
}

void board::movePiece(Piece p, int from, int to) {
    if (p == EMPTY || from == to) {
        return;
    }

    Bitboard fromMask = Bitboards::bit(from);
    Bitboard toMask = Bitboards::bit(to);
    Bitboard delta = fromMask | toMask;

    pieceBB[p] ^= delta;
    occupied ^= delta;

    if (isWhitePiece(p)) {
        whiteOcc ^= delta;
        if (p == WK) {
            whiteKingSquare = to;
        }
    }
    else {
        blackOcc ^= delta;
        if (p == BK) {
            blackKingSquare = to;
        }
    }

    int idx = pieceIndex[from];
    pieceList[p][idx] = to;
    pieceIndex[from] = -1;
    pieceIndex[to] = idx;
    squareBoard[from] = EMPTY;
    squareBoard[to] = p;

    hash ^= ZobristData::pieceSquare(p, from);
    hash ^= ZobristData::pieceSquare(p, to);
}

void board::resetBoard() {
    loadFEN(START_FEN);
}

void board::loadFEN(const std::string& fen) {
    clearBoard();

    size_t firstSpace = fen.find(' ');
    if (firstSpace == std::string::npos) {
        throw std::invalid_argument("Invalid FEN: missing active color.");
    }

    size_t secondSpace = fen.find(' ', firstSpace + 1);
    size_t thirdSpace = fen.find(' ', secondSpace + 1);
    size_t fourthSpace = fen.find(' ', thirdSpace + 1);
    size_t fifthSpace = fen.find(' ', fourthSpace + 1);

    if (secondSpace == std::string::npos ||
        thirdSpace == std::string::npos ||
        fourthSpace == std::string::npos ||
        fifthSpace == std::string::npos) {
        throw std::invalid_argument("Invalid FEN: incomplete fields.");
    }

    std::string piecePlacement = fen.substr(0, firstSpace);
    std::string activeColor = fen.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::string castleRightsStr = fen.substr(secondSpace + 1, thirdSpace - secondSpace - 1);
    std::string enPassant = fen.substr(thirdSpace + 1, fourthSpace - thirdSpace - 1);
    std::string halfmoveClockStr = fen.substr(fourthSpace + 1, fifthSpace - fourthSpace - 1);
    std::string fullmoveNumberStr = fen.substr(fifthSpace + 1);

    int sq = 0;
    for (char c : piecePlacement) {
        if (c == '/') {
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            sq += c - '0';
            continue;
        }

        Piece piece = EMPTY;
        switch (c) {
        case 'p': piece = BP; break;
        case 'n': piece = BN; break;
        case 'b': piece = BB; break;
        case 'r': piece = BR; break;
        case 'q': piece = BQ; break;
        case 'k': piece = BK; break;
        case 'P': piece = WP; break;
        case 'N': piece = WN; break;
        case 'B': piece = WB; break;
        case 'R': piece = WR; break;
        case 'Q': piece = WQ; break;
        case 'K': piece = WK; break;
        default:
            throw std::invalid_argument("Invalid character in FEN.");
        }

        addPiece(piece, sq++);
    }

    isWhiteTurn = (activeColor == "w");

    castleRights = 0;
    if (castleRightsStr.find('K') != std::string::npos) castleRights |= 0b0001;
    if (castleRightsStr.find('Q') != std::string::npos) castleRights |= 0b0010;
    if (castleRightsStr.find('k') != std::string::npos) castleRights |= 0b0100;
    if (castleRightsStr.find('q') != std::string::npos) castleRights |= 0b1000;

    if (enPassant != "-") {
        int file = enPassant[0] - 'a';
        int rank = enPassant[1] - '0';
        int row = 8 - rank;
        hasEnPassant = true;
        enPassantSquare = row * 8 + file;
    }
    else {
        hasEnPassant = false;
        enPassantSquare = -1;
    }

    halfmoveClock = std::stoi(halfmoveClockStr);
    fullmoveNumber = std::stoi(fullmoveNumberStr);

    hash ^= ZobristData::sideToMove(isWhiteTurn);
    hash ^= ZobristData::castlingRights(castleRights);
    int epFile = zobristEnPassantFile();
    if (epFile != -1) {
        hash ^= ZobristData::enPassantFile(epFile);
    }
}

int board::zobristEnPassantFile() const {
    if (!hasEnPassant || enPassantSquare < 0 || enPassantSquare >= 64) {
        return -1;
    }

    Bitboard attackers = isWhiteTurn
        ? (Bitboards::PawnAttackers[Bitboards::WHITE][enPassantSquare] & pieceBB[WP])
        : (Bitboards::PawnAttackers[Bitboards::BLACK][enPassantSquare] & pieceBB[BP]);

    return attackers ? (enPassantSquare & 7) : -1;
}

void board::rebuildPieceLists() {
    for (int p = 0; p < 13; ++p) {
        std::fill(std::begin(pieceList[p]), std::end(pieceList[p]), -1);
        pieceCount[p] = 0;
    }
    std::fill(std::begin(pieceIndex), std::end(pieceIndex), -1);
    std::fill(std::begin(squareBoard), std::end(squareBoard), EMPTY);

    for (int p = BQ; p <= WB; ++p) {
        Bitboard bb = pieceBB[p];
        while (bb) {
            int sq = Bitboards::poplsb(bb);
            int idx = pieceCount[p]++;
            pieceList[p][idx] = sq;
            pieceIndex[sq] = idx;
            squareBoard[sq] = static_cast<Piece>(p);
        }
    }
}

Unmove board::makeMove(const Move& m) {
    PROFILE_INC(::Profiler::MakeMoveCalls);
    PROFILE_TIMER(::Profiler::MakeMoveTime);

    Unmove u;
    u.fromPiece = m.moved;
    u.toPiece = m.wasEnPassant ? EMPTY : m.captured;
    u.prevCastleRights = castleRights;
    u.prevHasEnPassant = hasEnPassant;
    u.prevEnPassantSquare = enPassantSquare;
    u.prevTurn = isWhiteTurn;
    u.prevHalfmoveClock = halfmoveClock;
    u.prevFullmoveNumber = fullmoveNumber;
    u.prevHash = hash;
    u.epCapturedPiece = EMPTY;
    u.epCapturedSquare = -1;

    hash ^= ZobristData::sideToMove(isWhiteTurn);
    hash ^= ZobristData::castlingRights(castleRights);
    int prevEpFile = zobristEnPassantFile();
    if (prevEpFile != -1) {
        hash ^= ZobristData::enPassantFile(prevEpFile);
    }

    Piece moving = u.fromPiece;
    Piece captured = u.toPiece;

    if (m.wasEnPassant) {
        int capturedSq = (moving == WP) ? (m.to + 8) : (m.to - 8);
        Piece capturedPawn = (moving == WP) ? BP : WP;
        u.epCapturedPiece = capturedPawn;
        u.epCapturedSquare = capturedSq;
        removePiece(capturedPawn, capturedSq);
        captured = capturedPawn;
    }
    else if (captured != EMPTY) {
        removePiece(captured, m.to);
    }

    if (m.wasPromotion) {
        removePiece(moving, m.from);
        addPiece(m.promotedTo, m.to);
    }
    else {
        movePiece(moving, m.from, m.to);
    }

    if (m.wasCastling) {
        int rookFrom = -1;
        int rookTo = -1;
        Piece rookPiece = EMPTY;

        if (m.to == 62) {
            rookFrom = 63; rookTo = 61; rookPiece = WR;
        }
        else if (m.to == 58) {
            rookFrom = 56; rookTo = 59; rookPiece = WR;
        }
        else if (m.to == 6) {
            rookFrom = 7; rookTo = 5; rookPiece = BR;
        }
        else if (m.to == 2) {
            rookFrom = 0; rookTo = 3; rookPiece = BR;
        }

        if (rookPiece != EMPTY) {
            movePiece(rookPiece, rookFrom, rookTo);
        }
    }

    castleRights = m.castleRights;
    hasEnPassant = m.hasEnPassant;
    enPassantSquare = m.hasEnPassant ? m.enPassantSquare : -1;

    if (moving == WP || moving == BP || captured != EMPTY) {
        halfmoveClock = 0;
    }
    else {
        ++halfmoveClock;
    }

    if (!u.prevTurn) {
        ++fullmoveNumber;
    }

    isWhiteTurn = !isWhiteTurn;

    hash ^= ZobristData::sideToMove(isWhiteTurn);
    hash ^= ZobristData::castlingRights(castleRights);
    int newEpFile = zobristEnPassantFile();
    if (newEpFile != -1) {
        hash ^= ZobristData::enPassantFile(newEpFile);
    }

    return u;
}

void board::unmakeMove(const Move& m, const Unmove& u) {
    PROFILE_INC(::Profiler::UnmakeMoveCalls);
    PROFILE_TIMER(::Profiler::UnmakeMoveTime);

    Piece moving = u.fromPiece;

    isWhiteTurn = u.prevTurn;
    castleRights = u.prevCastleRights;
    hasEnPassant = u.prevHasEnPassant;
    enPassantSquare = u.prevEnPassantSquare;
    halfmoveClock = u.prevHalfmoveClock;
    fullmoveNumber = u.prevFullmoveNumber;

    if (m.wasCastling) {
        int rookFrom = -1;
        int rookTo = -1;
        Piece rookPiece = EMPTY;

        if (m.to == 62) {
            rookFrom = 61; rookTo = 63; rookPiece = WR;
        }
        else if (m.to == 58) {
            rookFrom = 59; rookTo = 56; rookPiece = WR;
        }
        else if (m.to == 6) {
            rookFrom = 5; rookTo = 7; rookPiece = BR;
        }
        else if (m.to == 2) {
            rookFrom = 3; rookTo = 0; rookPiece = BR;
        }

        if (rookPiece != EMPTY) {
            movePiece(rookPiece, rookFrom, rookTo);
        }
    }

    if (m.wasPromotion) {
        removePiece(m.promotedTo, m.to);
        addPiece(moving, m.from);
    }
    else {
        movePiece(moving, m.to, m.from);
    }

    if (m.wasEnPassant) {
        addPiece(u.epCapturedPiece, u.epCapturedSquare);
    }
    else if (u.toPiece != EMPTY) {
        addPiece(u.toPiece, m.to);
    }

    hash = u.prevHash;
}
