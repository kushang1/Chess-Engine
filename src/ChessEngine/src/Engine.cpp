#define _CRT_SECURE_NO_WARNINGS


#include "Engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <new>
#include <random>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#endif


std::atomic<long long> leafNodes{ 0 };
std::atomic<long long> totalNodes{ 0 };


static const int MATE_SCORE = 2000000000;       // same as your INF
static const int MATE_THRESHOLD = MATE_SCORE - 10000;

static const int INF = 2000000000;

namespace {

namespace fs = std::filesystem;

constexpr int DefaultHashMb = 128;
constexpr int MinHashMb = 1;
constexpr int MaxHashMb = 4096;
constexpr int MaxBookSelectionPool = 8;

void engineLog(const std::string& text)
{
#ifdef _WIN32
	const std::string debugLine = "info string " + text + "\n";
	OutputDebugStringA(debugLine.c_str());
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

std::mt19937_64& bookRng()
{
	static std::mt19937_64 rng(std::random_device{}());
	return rng;
}

std::string moveToUciText(const Move& move)
{
	if (move.from < 0 || move.to < 0) {
		return "0000";
	}

	auto square = [](int sq) {
		std::string out;
		out.push_back(static_cast<char>('a' + (sq & 7)));
		out.push_back(static_cast<char>('8' - (sq >> 3)));
		return out;
	};

	std::string out = square(move.from) + square(move.to);
	if (move.wasPromotion) {
		char promotion = 'q';
		switch (move.promotedTo) {
		case WR: case BR: promotion = 'r'; break;
		case WB: case BB: promotion = 'b'; break;
		case WN: case BN: promotion = 'n'; break;
		default: break;
		}
		out.push_back(promotion);
	}
	return out;
}

bool sameMoveIdentity(const Move& lhs, const Move& rhs)
{
	if (lhs.from != rhs.from || lhs.to != rhs.to) {
		return false;
	}
	if (lhs.wasPromotion != rhs.wasPromotion) {
		return false;
	}
	return !lhs.wasPromotion || lhs.promotedTo == rhs.promotedTo;
}

bool findLegalEquivalent(const std::vector<Move>& legalMoves, const Move& candidate, Move& legalMove)
{
	for (const Move& legal : legalMoves) {
		if (sameMoveIdentity(legal, candidate)) {
			legalMove = legal;
			return true;
		}
	}
	return false;
}

void addUniquePath(std::vector<fs::path>& paths, const fs::path& path)
{
	if (path.empty()) {
		return;
	}

	fs::path normalized = path.lexically_normal();
	auto existing = std::find(paths.begin(), paths.end(), normalized);
	if (existing == paths.end()) {
		paths.push_back(normalized);
	}
}

void addRootAndParents(std::vector<fs::path>& roots, fs::path root)
{
	std::error_code ec;
	root = fs::absolute(root, ec);
	if (ec) {
		return;
	}

	for (int i = 0; i < 6 && !root.empty(); ++i) {
		addUniquePath(roots, root);
		fs::path parent = root.parent_path();
		if (parent == root) {
			break;
		}
		root = parent;
	}
}

fs::path executableDirectory()
{
#ifdef _WIN32
	char buffer[MAX_PATH] = {};
	DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
	if (length != 0 && length < MAX_PATH) {
		return fs::path(buffer).parent_path();
	}
#endif
	return {};
}

std::vector<fs::path> searchRoots()
{
	std::vector<fs::path> roots;
	std::error_code ec;
	addRootAndParents(roots, fs::current_path(ec));
	addRootAndParents(roots, executableDirectory());
	return roots;
}

bool isRegularFile(const fs::path& path)
{
	std::error_code ec;
	return fs::is_regular_file(path, ec);
}

bool isDirectory(const fs::path& path)
{
	std::error_code ec;
	return fs::is_directory(path, ec);
}

fs::path resolveOpeningBookPath()
{
	std::vector<fs::path> candidates;

	if (const char* envFile = std::getenv("CHESS_BOOK_FILE")) {
		if (std::strcmp(envFile, "<empty>") == 0) {
			return {};
		}
		addUniquePath(candidates, envFile);
	}
	if (const char* envBook = std::getenv("CHESS_BOOK")) {
		if (std::strcmp(envBook, "<empty>") == 0) {
			return {};
		}
		fs::path envPath(envBook);
		addUniquePath(candidates, envPath);
		addUniquePath(candidates, envPath / "book.bin");
		addUniquePath(candidates, envPath / "Perfect2023.bin");
		addUniquePath(candidates, envPath / "komodo.bin");
		addUniquePath(candidates, envPath / "Human.bin");
	}

	for (const fs::path& root : searchRoots()) {
		addUniquePath(candidates, root / "Book" / "Human.bin");
		addUniquePath(candidates, root / "Book" / "Perfect2023.bin");
		addUniquePath(candidates, root / "Book" / "komodo.bin");
		addUniquePath(candidates, root / "Book" / "Perfect2023.bin");
		addUniquePath(candidates, root / "Human.bin");
	}

	for (const fs::path& candidate : candidates) {
		if (isRegularFile(candidate)) {
			return candidate;
		}
	}

	return {};
}

std::string resolveSyzygyPath()
{
	if (const char* envPath = std::getenv("CHESS_SYZYGY_PATH")) {
		if (std::strcmp(envPath, "<empty>") == 0) {
			return {};
		}
		if (*envPath != '\0') {
			return envPath;
		}
	}

	std::vector<fs::path> candidates;
	for (const fs::path& root : searchRoots()) {
		addUniquePath(candidates, root / "syzygy");
		addUniquePath(candidates, root / "Syzygy");
	}

	for (const fs::path& candidate : candidates) {
		if (isDirectory(candidate)) {
			return candidate.string();
		}
	}

	return {};
}

uint64_t floorPowerOfTwo(uint64_t value)
{
	if (value == 0) {
		return 0;
	}

	uint64_t out = 1;
	while (out <= value / 2) {
		out <<= 1;
	}
	return out;
}

class RepetitionFrame {
public:
	RepetitionFrame(std::vector<uint64_t>& history, uint64_t key)
		: history(history)
	{
		history.push_back(key);
	}

	~RepetitionFrame()
	{
		history.pop_back();
	}

private:
	std::vector<uint64_t>& history;
};

} // namespace


Engine::Engine() : stopSearch(false) {
	for (int i = 0; i < 2; i++)
		for (int d = 0; d < MAX_DEPTH; d++)
			killerMoves[i][d] = Move();  // invalid move

	memset(historyHeuristic, 0, sizeof(historyHeuristic));

	resizeTranspositionTable(DefaultHashMb);
	initializeExternalData();

}


Engine::~Engine() {
	delete[] tt;
}

void Engine::initializeExternalData()
{
	if (externalDataInitialized) {
		return;
	}
	externalDataInitialized = true;

	std::filesystem::path bookPath = resolveOpeningBookPath();
	if (!bookPath.empty()) {
		loadOpeningBook(bookPath.string());
	}
	else {
		engineLog("book file not found; set CHESS_BOOK_FILE or place a Polyglot book under Book\\Human.bin");
	}

	std::string syzygyPath = resolveSyzygyPath();
	if (!syzygyPath.empty()) {
		initSyzygy(syzygyPath.c_str());
	}
	else {
		engineLog("syzygy path not found; set CHESS_SYZYGY_PATH or place tablebases under syzygy");
	}
}

void Engine::resetSearchStats()
{
	totalNodes.store(0, std::memory_order_relaxed);
	leafNodes.store(0, std::memory_order_relaxed);
}

void Engine::setTimeLimitMs(int milliseconds)
{
	timeLimitMs = std::max(1, milliseconds);
}

void Engine::setHashSizeMb(int megabytes)
{
	resizeTranspositionTable(megabytes);
}

void Engine::resizeTranspositionTable(int megabytes)
{
	megabytes = std::clamp(megabytes, MinHashMb, MaxHashMb);

	const uint64_t bytes = static_cast<uint64_t>(megabytes) * 1024ULL * 1024ULL;
	uint64_t entries = floorPowerOfTwo(bytes / sizeof(TTEntry));
	if (entries == 0) {
		entries = 1;
	}

	TTEntry* newTable = new (std::nothrow) TTEntry[entries]();
	if (newTable == nullptr) {
		return;
	}

	delete[] tt;
	tt = newTable;
	ttSize = entries;
	ttMask = entries - 1;
}

void Engine::clearStop()
{
	stopSearch.store(false, std::memory_order_relaxed);
}

void Engine::requestStop()
{
	stopSearch.store(true, std::memory_order_relaxed);
}

long long Engine::nodesSearched() const
{
	return totalNodes.load(std::memory_order_relaxed);
}

long long Engine::leafNodesSearched() const
{
	return leafNodes.load(std::memory_order_relaxed);
}

static const int pieceValueSimple[13] = {
	0, 9, 5, 1, 3, 100, 3,
	   9, 5, 1, 3, 100, 3
};



// ==========================================================
// PIECE-SQUARE TABLES
// ==========================================================
static const int pawnPST[64] = {
	 0,  0,  0,  0,  0,  0,  0,  0,
	 5, 10, 10, -20, -20, 10, 10,  5,
	 5, -5, -10,   0,   0, -10, -5,  5,
	 0,  0,   0,  20,  20,   0,  0,  0,
	 5,  5,  10,  25,  25,  10,  5,  5,
	10, 10,  20,  30,  30,  20, 10, 10,
	50, 50,  50,  50,  50,  50, 50, 50,
	 0,  0,   0,   0,   0,   0,  0,  0
};

static const int knightPST[64] = {
	-50, -40, -30, -30, -30, -30, -40, -50,
	-40, -20,   0,   0,   0,   0, -20, -40,
	-30,   0,  10,  15,  15,  10,   0, -30,
	-30,   5,  15,  20,  20,  15,   5, -30,
	-30,   0,  15,  20,  20,  15,   0, -30,
	-30,   5,  10,  15,  15,  10,   5, -30,
	-40, -20,   0,   5,   5,   0, -20, -40,
	-50, -40, -30, -30, -30, -30, -40, -50
};

static const int bishopPST[64] = {
	-20, -10, -10, -10, -10, -10, -10, -20,
	-10,   5,   0,   0,   0,   0,   5, -10,
	-10,  10,  10,  10,  10,  10,  10, -10,
	-10,   0,  10,  10,  10,  10,   0, -10,
	-10,   5,   5,  10,  10,   5,   5, -10,
	-10,   0,   5,  10,  10,   5,   0, -10,
	-10,   0,   0,   0,   0,   0,   0, -10,
	-20, -10, -10, -10, -10, -10, -10, -20
};

static const int knightMob[64] = {
		2,3,4,4,4,4,3,2,
		3,4,6,6,6,6,4,3,
		4,6,8,8,8,8,6,4,
		4,6,8,8,8,8,6,4,
		4,6,8,8,8,8,6,4,
		4,6,8,8,8,8,6,4,
		3,4,6,6,6,6,4,3,
		2,3,4,4,4,4,3,2
};

static const int bishopMob[64] = {
	3,4,4,5,5,4,4,3,
	4,6,6,7,7,6,6,4,
	4,6,8,8,8,8,6,4,
	5,7,8,9,9,8,7,5,
	5,7,8,9,9,8,7,5,
	4,6,8,8,8,8,6,4,
	4,6,6,7,7,6,6,4,
	3,4,4,5,5,4,4,3
};

static const int rookMob[64] = {
	5,6,7,8,8,7,6,5,
	6,7,8,9,9,8,7,6,
	7,8,9,9,9,9,8,7,
	8,9,9,10,10,9,9,8,
	8,9,9,10,10,9,9,8,
	7,8,9,9,9,9,8,7,
	6,7,8,9,9,8,7,6,
	5,6,7,8,8,7,6,5
};

// ----------------------------------------------------------
	// PIECE-SQUARE TABLES
	// (added rook, queen, king PST)
	// ----------------------------------------------------------
static const int rookPST[64] = {
	0,0, 5,10,10, 5,0,0,
	0,0, 5,10,10, 5,0,0,
	0,0, 5,10,10, 5,0,0,
	0,0, 5,15,15, 5,0,0,
	5,10,10,15,15,10,10,5,
	5,10,10,15,15,10,10,5,
   10,15,15,20,20,15,15,10,
	0,0, 0, 5, 5, 0,0,0
};

static const int queenPST[64] = {
   -20,-10,-10,-5,-5,-10,-10,-20,
   -10,  0,  5, 0, 0, 0, 0,-10,
   -10,  5,  5, 5, 5, 5, 0,-10,
	-5,  5, 10,10,10,10, 5, -5,
	 0,  0, 10,10,10,10, 0,  0,
   -10,  5,  5, 5, 5, 5, 0,-10,
   -10,  0,  0, 0, 0, 0, 0,-10,
   -20,-10,-10,-5,-5,-10,-10,-20
};

static const int kingPST[64] =
{
   20,30,10, 0, 0,10,30,20,
   20,20, 0, 0, 0, 0,20,20,
  -10,-20,-20,-20,-20,-20,-20,-10,
  -20,-30,-30,-40,-40,-30,-30,-20,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30,
  -30,-40,-40,-50,-50,-40,-40,-30
};

// ==========================================================
// MOVE ORDERING (CAPTURE + KILLER + HISTORY)
// ==========================================================
int Engine::scoreMove(const Move& m, const board& b, int depth)
{
	depth = std::clamp(depth, 0, MAX_DEPTH - 1);

	// 1. MVV-LVA captures
	if (m.captured != EMPTY) {
		int victim = pieceValueSimple[m.captured];
		int attacker = pieceValueSimple[m.moved];
		return 100000 + (victim * 10 - attacker);
	}

	// 2. Killer moves
	if (killerMoves[0][depth].from == m.from &&
		killerMoves[0][depth].to == m.to)
		return 90000;

	if (killerMoves[1][depth].from == m.from &&
		killerMoves[1][depth].to == m.to)
		return 80000;

	// 3. History heuristic for quiet moves
	return historyHeuristic[m.from][m.to];
}

// ==========================================================
// STATIC EVALUATION
// ==========================================================
int Engine::evaluate(board& b)
{
	int score = 0;

	// ----------------------------------------------------------
	// MATERIAL VALUES
	// ----------------------------------------------------------
	static const int matVal[13] = {
		  0,
	   -900, -500, -100, -300, -20000, -320,   // black pieces
		900,  500,  100,  300,  20000,  320    // white pieces
	};

	// ----------------------------------------------------------
	// PIECE-SQUARE TABLE HELPER
	// ----------------------------------------------------------
	auto pst = [&](Piece p, int sq)
		{
			switch (p)
			{
			case WP: return  pawnPST[sq ^ 56];
			case BP: return -pawnPST[sq];

			case WN: return  knightPST[sq ^ 56];
			case BN: return -knightPST[sq];

			case WB: return  bishopPST[sq ^ 56];
			case BB: return -bishopPST[sq];

			case WR: return  rookPST[sq ^ 56];
			case BR: return -rookPST[sq];

			case WQ: return  queenPST[sq ^ 56];
			case BQ: return -queenPST[sq];

			case WK: return  kingPST[sq ^ 56];
			case BK: return -kingPST[sq];
			}
			return 0;
		};

	// ----------------------------------------------------------
	// MATERIAL + PST
	// ----------------------------------------------------------
	for (int p = BQ; p <= WB; ++p)
	{
		int n = b.pieceCount[p];
		const int* list = b.pieceList[p];
		int mv = matVal[p];

		for (int i = 0; i < n; i++) {
			int sq = list[i];
			score += mv;
			score += pst((Piece)p, sq);
		}
	}

	// ----------------------------------------------------------
	// MOBILITY
	// ----------------------------------------------------------
	for (int i = 0; i < b.pieceCount[WN]; i++) score += knightMob[b.pieceList[WN][i]];
	for (int i = 0; i < b.pieceCount[BN]; i++) score -= knightMob[b.pieceList[BN][i]];

	for (int i = 0; i < b.pieceCount[WB]; i++) score += bishopMob[b.pieceList[WB][i]];
	for (int i = 0; i < b.pieceCount[BB]; i++) score -= bishopMob[b.pieceList[BB][i]];

	for (int i = 0; i < b.pieceCount[WR]; i++) score += rookMob[b.pieceList[WR][i]];
	for (int i = 0; i < b.pieceCount[BR]; i++) score -= rookMob[b.pieceList[BR][i]];

	// ----------------------------------------------------------
	// PAWN STRUCTURE ? Passed pawns
	// ----------------------------------------------------------
	auto isPassed = [&](int sq, bool white)
		{
			int file = sq & 7;
			int rank = sq >> 3;

			if (white) {
				for (int i = 0; i < b.pieceCount[BP]; i++) {
					int psq = b.pieceList[BP][i];
					if (abs((psq & 7) - file) <= 1 && (psq >> 3) < rank)
						return false;
				}
				return true;
			}
			else {
				for (int i = 0; i < b.pieceCount[WP]; i++) {
					int psq = b.pieceList[WP][i];
					if (abs((psq & 7) - file) <= 1 && (psq >> 3) > rank)
						return false;
				}
				return true;
			}
		};

	// White passed pawns
	for (int i = 0; i < b.pieceCount[WP]; i++) {
		int sq = b.pieceList[WP][i];
		if (isPassed(sq, true)) score += 40 + 10 * (sq >> 3);
	}

	// Black passed pawns
	for (int i = 0; i < b.pieceCount[BP]; i++) {
		int sq = b.pieceList[BP][i];
		if (isPassed(sq, false)) score -= 40 + 10 * (7 - (sq >> 3));
	}

	// ----------------------------------------------------------
	// DOUBLED PAWNS
	// ----------------------------------------------------------
	int wFileCnt[8] = { 0 }, bFileCnt[8] = { 0 };

	for (int i = 0; i < b.pieceCount[WP]; i++)
		wFileCnt[b.pieceList[WP][i] & 7]++;

	for (int i = 0; i < b.pieceCount[BP]; i++)
		bFileCnt[b.pieceList[BP][i] & 7]++;

	for (int f = 0; f < 8; f++) {
		if (wFileCnt[f] > 1) score -= 15 * (wFileCnt[f] - 1);
		if (bFileCnt[f] > 1) score += 15 * (bFileCnt[f] - 1);
	}

	// ----------------------------------------------------------
	// KING SAFETY (big Elo booster)
	// ----------------------------------------------------------
	int wKing = b.pieceList[WK][0];
	int bKing = b.pieceList[BK][0];

	// King zone relative offsets
	static const int kingZone[12][2] = {
		{-1,-1},{-1,0},{-1,1},
		{0,-1},        {0,1},
		{1,-1},{1,0},{1,1},
		{-2,0}, {-1,-2}, {-1,2}, {2,0}
	};

	// Attack counts
	int atkWhiteKing = 0;
	int atkBlackKing = 0;

	auto add_attacks = [&](int kingSq, bool kingIsWhite)
		{
			int r = kingSq >> 3, c = kingSq & 7;
			for (auto& d : kingZone)
			{
				int nr = r + d[0], nc = c + d[1];
				if (nr < 0 || nr > 7 || nc < 0 || nc > 7) continue;
				int sq = nr * 8 + nc;

				bool whiteKingInCheck = moveGenerator->isSquareAttacked(b, b.pieceList[WK][0], false);
				bool blackKingInCheck = moveGenerator->isSquareAttacked(b, b.pieceList[BK][0], true);
			}
		};

	add_attacks(wKing, true);
	add_attacks(bKing, false);

	score -= atkWhiteKing * 20;
	score += atkBlackKing * 20;

	// ----------------------------------------------------------
	// KING PAWN SHIELD
	// ----------------------------------------------------------
	auto pawnShield = [&](int ksq, bool isWhite) {
		int r = ksq >> 3, c = ksq & 7;
		int s = 0;

		if (isWhite && r < 6)
		{
			if (b.pieceAt((r - 1) * 8 + c) == WP) s += 15;
			if (c > 0 && b.pieceAt((r - 1) * 8 + c - 1) == WP) s += 10;
			if (c < 7 && b.pieceAt((r - 1) * 8 + c + 1) == WP) s += 10;
		}
		else if (!isWhite && r > 1)
		{
			if (b.pieceAt((r + 1) * 8 + c) == BP) s += 15;
			if (c > 0 && b.pieceAt((r + 1) * 8 + c - 1) == BP) s += 10;
			if (c < 7 && b.pieceAt((r + 1) * 8 + c + 1) == BP) s += 10;
		}
		return s;
		};

	score += pawnShield(wKing, true);
	score -= pawnShield(bKing, false);

	// ----------------------------------------------------------
	// OPEN FILES NEAR KING
	// ----------------------------------------------------------
	auto fileHasPawn = [&](int f, bool white) {
		int cnt = white ? b.pieceCount[WP] : b.pieceCount[BP];
		int p = white ? WP : BP;
		for (int i = 0; i < cnt; i++)
			if ((b.pieceList[p][i] & 7) == f)
				return true;
		return false;
		};

	auto kingOpenPenalty = [&](int ksq, bool white) {
		int f = ksq & 7;
		int s = 0;
		if (!fileHasPawn(f, white)) s -= 15;
		if (f > 0 && !fileHasPawn(f - 1, white)) s -= 10;
		if (f < 7 && !fileHasPawn(f + 1, white)) s -= 10;
		return s;
		};

	score += kingOpenPenalty(wKing, true);
	score -= kingOpenPenalty(bKing, false);

	// ----------------------------------------------------------
	// CASTLED BONUS
	// ----------------------------------------------------------
	if (wKing == 62 || wKing == 58) score += 40;
	if (bKing == 6 || bKing == 2) score -= 40;

	// ----------------------------------------------------------
	// KING IN CENTER AFTER MOVE 10
	// ----------------------------------------------------------
	auto isCenter = [&](int sq) {
		int r = sq >> 3, c = sq & 7;
		return (r >= 2 && r <= 5 && c >= 2 && c <= 5);
		};

	if (b.fullmoveNumber > 10)
	{
		if (isCenter(wKing)) score -= 40;
		if (isCenter(bKing)) score += 40;
	}

	// ----------------------------------------------------------
	// SIDE TO MOVE BONUS
	// ----------------------------------------------------------
	return b.isWhiteTurn ? score : -score;
}


struct RootSearchResult {
	Move move;
	int score;
};

static void selectBestScoredMove(Move* moves, int* scores, int index, int count)
{
	int best = index;
	for (int i = index + 1; i < count; ++i) {
		if (scores[i] > scores[best]) {
			best = i;
		}
	}

	if (best != index) {
		std::swap(scores[index], scores[best]);
		std::swap(moves[index], moves[best]);
	}
}



Move Engine::findBestMove(board& b, int maxDepth,
	const std::vector<uint64_t>& globalReps)
{
	resetSearchStats();
	

	maxDepth = std::clamp(maxDepth, 1, MAX_DEPTH - 1);
	this->maxDepth = maxDepth;

	Move bookMove = probeBook(b);
	if (bookMove.from != -1)
	{
		return bookMove;
	}

	int rootPieceCount = 0;
	for (int p = BQ; p <= WB; ++p) {
		rootPieceCount += b.pieceCount[p];
	}
	if (syzygyIsAvailable() &&
		rootPieceCount <= static_cast<int>(syzygyMaxPieces()) &&
		b.castleRights == 0) {
		int tbScore = 0;
		Move tbMove;
		if (probeSyzygyRoot(b, tbScore, tbMove) && tbMove.from != -1) {
			std::vector<Move> legalMoves = moveGenerator->generateLegalMoves(b);
			Move legalTbMove;
			if (findLegalEquivalent(legalMoves, tbMove, legalTbMove)) {
				engineLog("syzygy selected root move: " + moveToUciText(legalTbMove));
				return legalTbMove;
			}
			engineLog("syzygy root move rejected as illegal: " + moveToUciText(tbMove));
		}
	}

	// --------------------------------------------
	// 5 second search time
	// --------------------------------------------
	searchStart = std::chrono::steady_clock::now();

	// --------------------------------------------
	// Build repetition history
	// --------------------------------------------
	std::vector<uint64_t> repHistory = globalReps;
	const uint64_t currentKey = computeHash(b);
	if (repHistory.empty() || repHistory.back() != currentKey) {
		repHistory.push_back(currentKey);
	}

	// --------------------------------------------
	// Root move generation
	// --------------------------------------------
	auto rootMoves = moveGenerator->generateLegalMoves(b);

	if(rootMoves.size() == 1)
	{
		return rootMoves[0];
	}

	if (rootMoves.empty())
		return Move(-1, -1, EMPTY, EMPTY, 0);

	Move bestFullMove = rootMoves[0];
	int  bestFullScore = -INF;
	bool haveFull = false;

	// --------------------------------------------
	// Iterative deepening loop
	// --------------------------------------------
	for (int depth = 1; depth <= maxDepth; depth++)
	{

		auto now = std::chrono::steady_clock::now();
		auto elapsed =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();

		if (elapsed >= timeLimitMs)
		{
			break;
		}

		// Move ordering: try previous best first
		if (haveFull)
		{
			auto it = std::find_if(rootMoves.begin(), rootMoves.end(),
				[&](const Move& m) {
					return m.from == bestFullMove.from &&
						m.to == bestFullMove.to &&
						m.moved == bestFullMove.moved;
				});
			if (it != rootMoves.end())
				std::swap(rootMoves[0], *it);
		}

		struct RootSearchResult { Move move; int score; bool full; };
		std::vector<RootSearchResult> results(rootMoves.size());

		// --------------------------------------------
		// Search root moves serially for tournament stability.
		// --------------------------------------------
		for (size_t i = 0; i < rootMoves.size(); i++)
		{
			if (stopSearch.load(std::memory_order_relaxed)) {
				results[i] = { rootMoves[i], -INF, false };
				continue;
			}

			Move rm = rootMoves[i];
			board local = b;
			std::vector<uint64_t> localRepHistory = repHistory;

			Unmove u = local.makeMove(rm);
			(void)u;

			bool fullEval = true;
			int score = -search(local, depth - 1, -INF, INF, localRepHistory);

			if (stopSearch.load(std::memory_order_relaxed))
				fullEval = false;

			results[i] = { rm, score, fullEval };
		}

		now = std::chrono::steady_clock::now();
		elapsed =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();

		bool timedOut = elapsed >= timeLimitMs || stopSearch.load();

		int bestDepthScore = -INF;
		Move bestDepthMove = rootMoves[0];
		bool anyFullThisDepth = false;

		// --------------------------------------------
		// Collect FULL results (not cut early)
		// --------------------------------------------
		for (auto& r : results)
		{
			if (r.full)
			{
				if (!anyFullThisDepth || r.score > bestDepthScore)
				{
					anyFullThisDepth = true;
					bestDepthScore = r.score;
					bestDepthMove = r.move;
				}
			}
		}

		if (!timedOut)
		{
			if (anyFullThisDepth)
			{
				bestFullMove = bestDepthMove;
				bestFullScore = bestDepthScore;
				haveFull = true;

			}
			else {
			}
			continue;
		}

		// --------------------------------------------
		// Timeout happened
		// --------------------------------------------

		if (!anyFullThisDepth)
		{
			break;
		}

		if (!haveFull || bestDepthScore > bestFullScore)
		{
			bestFullMove = bestDepthMove;
			bestFullScore = bestDepthScore;
			haveFull = true;

		}
		else
		{
		}

		break;
	}


	return bestFullMove;
}


int Engine::search(board& b, int depth, int alpha, int beta,
	std::vector<uint64_t>& repHistory)
{
	depth = std::clamp(depth, 0, MAX_DEPTH - 1);
	totalNodes++;
	auto fix_mate_score = [&](int s, int ply) {
		if (s > MATE_THRESHOLD)     return s - ply;
		if (s < -MATE_THRESHOLD)    return s + ply;
		return s;
		};

	// --------------------------------------------------
	// Time control: periodically check if time is up
	// --------------------------------------------------
	if ((totalNodes & 0x0FFF) == 0) { // check every ~4K nodes
		if (!stopSearch.load(std::memory_order_relaxed)) {
			auto now = std::chrono::steady_clock::now();
			auto elapsedMs =
				std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();
			if (elapsedMs >= timeLimitMs) {
				stopSearch.store(true, std::memory_order_relaxed);
			}
		}
	}

	if (stopSearch.load(std::memory_order_relaxed)) {
		
		return -INF;
	}

	// --------------------------------------------------
	// 50-move rule: automatic draw
	// --------------------------------------------------
	if (b.halfmoveClock >= 100) {
		// 100 halfmoves = 50 full moves without pawn move/capture
		return 0;
	}

	// --------------------------------------------------
	// Zobrist key for TT + repetition detection
	// --------------------------------------------------
	uint64_t key = computeHash(b);

	// check only same-side-to-move positions
	for (auto it = repHistory.rbegin(); it != repHistory.rend(); ++it) {
		if (*it == key) {
			return 0;   // repetition ? draw score
		}
	}

	RepetitionFrame repetitionFrame(repHistory, key);
	

	// Leaf ? quiescence search
	if (depth == 0) {
		leafNodes++;
		return quiescence(b, alpha, beta);
	}

	int alphaOrig = alpha;

	// -------------------------------
	// TRANSPOSITION TABLE PROBE
	// -------------------------------
	TTEntry& entry = tt[key & ttMask];

	Move ttMove;
	bool haveTTMove = false;

	if (entry.flag != TT_EMPTY && entry.key == key && entry.depth >= depth)
	{
		int stored = entry.score;

		if (entry.flag == TT_EXACT)
			return stored;
		else if (entry.flag == TT_ALPHA && stored <= alpha)
			return stored;
		else if (entry.flag == TT_BETA && stored >= beta)
			return stored;

		// we can still use TT move for ordering
		ttMove = entry.bestMove;
		haveTTMove = true;
	}

	
	int tbScore;
	Move tbMove;

	// ------------------------------------
	// SYZYGY SAFETY CONDITIONS
	// ------------------------------------
	bool tbSafe = true;

	// Build piece count (fastest way)
	int totalPieces = 0;
	for (int p = BQ; p <= WB; ++p)
		totalPieces += b.pieceCount[p];

	// 1. Must be = TB_LARGEST (usually = 7)
	if (totalPieces > (int)TB_LARGEST)
		tbSafe = false;

	// 3. EP must be disabled (EP creates illegal tablebase states)
	if (b.hasEnPassant)
		tbSafe = false;

	
	// 5. Avoid tablebase inside null move / unstable positions
	// OPTIONAL but recommended
	if (depth < maxDepth - 2) // not at or near root
		tbSafe = false;


	// ------------------------------------
	// If safe ? probe TB
	// ------------------------------------
	if (tbSafe && probeSyzygy(b, tbScore, tbMove)) {

		TTEntry& tbEntry = tt[key & ttMask];
		tbEntry.key = key;
		tbEntry.score = tbScore;
		tbEntry.depth = 127;     // highest possible depth
		tbEntry.flag = TT_EXACT;
		tbEntry.bestMove = tbMove;

		return tbScore;
	}

	

	// -------------------------------
	// IN-CHECK DETECTION
	// -------------------------------
	int kingSq = moveGenerator->findKing(b, b.isWhiteTurn);
	bool inCheck = moveGenerator->isSquareAttacked(b, kingSq, !b.isWhiteTurn);



	// -------------------------------
	// NULL MOVE PRUNING
	// -------------------------------
	// Conditions:
	//  - depth high enough (depth >= 3)
	//  - not in check
	//  - side to move has some non-pawn material (avoid zugzwang-ish endings)
	if (depth >= 3 && !inCheck) {
		bool hasNonPawnMaterial = false;

		if (b.isWhiteTurn) {
			if (b.pieceCount[WQ] > 0 || b.pieceCount[WR] > 0 ||
				b.pieceCount[WB] > 0 || b.pieceCount[WN] > 0)
				hasNonPawnMaterial = true;
		}
		else {
			if (b.pieceCount[BQ] > 0 || b.pieceCount[BR] > 0 ||
				b.pieceCount[BB] > 0 || b.pieceCount[BN] > 0)
				hasNonPawnMaterial = true;
		}

		if (hasNonPawnMaterial) {
			// Save state we touch
			bool prevTurn = b.isWhiteTurn;
			bool prevHasEP = b.hasEnPassant;
			int  prevEPSq = b.enPassantSquare;
            uint64_t prevHash = b.hash;
            int prevEpFile = b.zobristEnPassantFile();

			// Make null move: just give opponent the turn, clear EP
            b.hash ^= ZobristData::sideToMove(b.isWhiteTurn);
            if (prevEpFile != -1) {
                b.hash ^= ZobristData::enPassantFile(prevEpFile);
            }
			b.isWhiteTurn = !b.isWhiteTurn;
			b.hasEnPassant = false;
			b.enPassantSquare = -1;
            b.hash ^= ZobristData::sideToMove(b.isWhiteTurn);

			const int R = 2; // depth reduction
			int score = -search(b, depth - 1 - R,
				-beta, -beta + 1,
				repHistory);

			// Undo null move
			b.isWhiteTurn = prevTurn;
			b.hasEnPassant = prevHasEP;
			b.enPassantSquare = prevEPSq;
            b.hash = prevHash;

			// Fail-high ? position is so good we can prune
			if (score >= beta) {
				return score;
			}
		}
	}
	// -------------------------------
	// NORMAL MOVE GENERATION
	// -------------------------------
	MoveList moves;
	moveGenerator->generateLegalMoves(b, moves);

	// No legal moves ? checkmate or stalemate
	if (moves.count == 0) {
		// we already have inCheck from above
		if (inCheck) {
			return -(MATE_SCORE - (maxDepth - depth));
		}
		else {
			return 0; // stalemate
		}
	}

	// Score and order moves
	int scores[MoveList::MAX_MOVES];
	Move* moveData = moves.data();

	for (int i = 0; i < moves.count; ++i)
	{
		Move& m = moveData[i];
		int s = scoreMove(m, b, depth);

		if (haveTTMove && m.from == ttMove.from && m.to == ttMove.to)
			s += 200000; // big bonus for TT move

		scores[i] = s;
	}

	int besteval = -INF;
	Move bestMoveLocal;

	// -------------------------------
	// SEARCH CHILD MOVES
	// -------------------------------
	int moveIndex = 0;

	for (int orderedIndex = 0; orderedIndex < moves.count; ++orderedIndex) {
		selectBestScoredMove(moveData, scores, orderedIndex, moves.count);
		Move& move = moveData[orderedIndex];

		Unmove u = b.makeMove(move);

		bool isCapture = (move.captured != EMPTY);

		// ---------------------------------------
		// CHECK EXTENSION: does this move give check?
		// After makeMove, b.isWhiteTurn is the opponent's turn.
		// So we find the opponent king and see if it's attacked
		int oppKingSq = moveGenerator->findKing(b, b.isWhiteTurn);
		bool givesCheck = moveGenerator->isSquareAttacked(b, oppKingSq, !b.isWhiteTurn);

		bool isTTMove = false;
		if (haveTTMove &&
			move.from == ttMove.from &&
			move.to == ttMove.to &&
			move.moved == ttMove.moved)
		{
			isTTMove = true;
		}

		bool isKiller =
			(killerMoves[0][depth].from == move.from && killerMoves[0][depth].to == move.to) ||
			(killerMoves[1][depth].from == move.from && killerMoves[1][depth].to == move.to);

		// Base depth for this move: one ply less
		int newDepth = depth - 1;
		if (givesCheck) {
			// Extend checks by 1 ply
			newDepth++;
		}

		// ---------------------------------------
		// LATE MOVE PRUNING (LMP)


		if (!isCapture &&
			!givesCheck &&
			!inCheck &&
			depth <= 3 &&
			moveIndex >= 6 &&
			!isTTMove &&
			!isKiller)
		{
			b.unmakeMove(move, u);
			moveIndex++;
			continue;
		}

		int eval;

		// ---------------------------------------
		// LMR CONDITIONS:
		//  - depth >= 3
		//  - moveIndex >= 3 (late move)
		//  - quiet move (no capture)
		//  - not in check already
		//  - move itself does NOT give check
		// ---------------------------------------
		if (newDepth > 0 &&
			depth >= 3 &&
			moveIndex >= 3 &&
			!isCapture &&
			!inCheck &&
			!givesCheck)
		{
			int R = 1;  // reduction (tuneable, try 1 or 2)

			// Reduced-depth search with null window
			eval = -search(b, newDepth - R,
				-alpha - 1, -alpha,
				repHistory);

			eval = fix_mate_score(eval, 1);

			// If it looks better than alpha, research at full depth
			if (eval > alpha) {
				eval = -search(b, newDepth,
					-beta, -alpha,
					repHistory);

				eval = fix_mate_score(eval, 1);
			}
		}
		else {
			// Normal full-depth search (with check extension applied)
			eval = -search(b, newDepth,
				-beta, -alpha,
				repHistory);

			eval = fix_mate_score(eval, 1);
		}
		
		b.unmakeMove(move, u);

		// ------------------------------
		// Normal alpha-beta bookkeeping
		// ------------------------------
		if (eval > besteval) {
			besteval = eval;
			bestMoveLocal = move;
		}

		if (eval > alpha) {
			alpha = eval;

			// If quiet move, store killers
			if (!isCapture) {
				killerMoves[1][depth] = killerMoves[0][depth];
				killerMoves[0][depth] = move;
			}
		}

		// Alpha–beta cutoff
		if (alpha >= beta) {
			if (!isCapture) {
				historyHeuristic[move.from][move.to] += depth * depth;
			}
			break;
		}

		moveIndex++;
	}


	// -----------------------------------
	// STORE TT ENTRY
	// -----------------------------------
	TTEntry& store = tt[key & ttMask];
	store.key = key;
	store.score = fix_mate_score(besteval, 0);
	store.depth = depth;

	if (besteval <= alphaOrig)
		store.flag = TT_ALPHA;  // fail-low
	else if (besteval >= beta)
		store.flag = TT_BETA;   // fail-high
	else
		store.flag = TT_EXACT;  // exact score

	store.bestMove = bestMoveLocal;

	return besteval;
}


int Engine::quiescence(board& b, int alpha, int beta)
{
	totalNodes++;  // still count these as nodes
	auto fix_mate_score = [&](int s, int ply) {
		if (s > MATE_THRESHOLD)     return s - ply;
		if (s < -MATE_THRESHOLD)    return s + ply;
		return s;
		};


	if ((totalNodes & 0x0FFF) == 0) {
		if (!stopSearch.load(std::memory_order_relaxed)) {
			auto now = std::chrono::steady_clock::now();
			auto elapsedMs =
				std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();
			if (elapsedMs >= timeLimitMs) {
				stopSearch.store(true, std::memory_order_relaxed);
			}
		}
	}

	if (stopSearch.load(std::memory_order_relaxed)) {
		return -INF;
	}

	// Stand-pat evaluation: assume we do nothing
	int standPat = evaluate(b);

	// Fail-high: too good for the opponent
	if (standPat >= beta)
		return standPat;

	if (standPat > alpha)
		alpha = standPat;

	// Generate all legal moves, then filter captures
	MoveList moves;
	moveGenerator->generateLegalMoves(b, moves);

	// Score only captures using MVV-LVA (depth 0 so killers/history are harmless)
	int scores[MoveList::MAX_MOVES];
	Move* moveData = moves.data();
	int captureCount = 0;

	for (auto& m : moves) {
		if (m.captured == EMPTY) continue; // only captures in quiescence
		moveData[captureCount] = m;
		scores[captureCount] = scoreMove(m, b, 0);
		++captureCount;
	}

	if (captureCount == 0) {
		// No captures ? position is quiet, return stand-pat eval
		return alpha;
	}

	for (int orderedIndex = 0; orderedIndex < captureCount; ++orderedIndex) {
		selectBestScoredMove(moveData, scores, orderedIndex, captureCount);
		const Move& m = moveData[orderedIndex];

		Unmove u = b.makeMove(m);
		int score = -quiescence(b, -beta, -alpha);
		score = fix_mate_score(score, 1);

		b.unmakeMove(m, u);

		if (score >= beta)
			return score;

		if (score > alpha)
			alpha = score;
	}

	return fix_mate_score(alpha, 0);

}


static uint64_t read_be_u64(const uint8_t* b) {
	return (uint64_t(b[0]) << 56) | (uint64_t(b[1]) << 48) |
		(uint64_t(b[2]) << 40) | (uint64_t(b[3]) << 32) |
		(uint64_t(b[4]) << 24) | (uint64_t(b[5]) << 16) |
		(uint64_t(b[6]) << 8) | (uint64_t(b[7]) << 0);
}
static uint32_t read_be_u32(const uint8_t* b) {
	return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
		(uint32_t(b[2]) << 8) | (uint32_t(b[3]) << 0);
}
static uint16_t read_be_u16(const uint8_t* b) {
	return (uint16_t(b[0]) << 8) | (uint16_t(b[1]) << 0);
}

bool Engine::loadOpeningBook(const std::string& filename)
{
	std::error_code ec;
	uintmax_t fileBytes = std::filesystem::file_size(filename, ec);
	if (!ec && (fileBytes % 16) != 0) {
		std::ostringstream log;
		log << "book file size is not a multiple of 16 bytes: " << filename
			<< " bytes=" << fileBytes;
		engineLog(log.str());
	}

	FILE* f = nullptr;
	fopen_s(&f, filename.c_str(), "rb");
	if (!f) {
		engineLog("book file open failed: " + filename);
		return false;
	}

	openingBook.clear();
	if (!ec) {
		openingBook.reserve(static_cast<size_t>(fileBytes / 16));
	}

	uint8_t buf[16];
	uint16_t maxWeight = 0;
	size_t weightedEntries = 0;
	while (fread(buf, 1, 16, f) == 16) {
		PolyglotEntry e;
		e.key = read_be_u64(buf + 0);
		e.move = read_be_u16(buf + 8);
		e.weight = read_be_u16(buf + 10);
		e.learn = read_be_u32(buf + 12);
		maxWeight = std::max(maxWeight, e.weight);
		if (e.weight > 1) {
			++weightedEntries;
		}
		openingBook.push_back(e);
	}

	fclose(f);

	if (openingBook.empty()) {
		engineLog("book file contained no complete entries: " + filename);
		return false;
	}

	std::ostringstream log;
	log << "book file loaded successfully: " << filename
		<< " entries=" << openingBook.size()
		<< " max_weight=" << maxWeight
		<< " weighted_entries=" << weightedEntries;
	engineLog(log.str());
	if (maxWeight <= 1) {
		engineLog("book weights appear uninformative; weighted variety will fall back to equal random choice");
	}
	return true;
}




Move Engine::probeBook(board& b)
{
	if (openingBook.empty()) {
		engineLog("book unavailable: no entries loaded");
		return Move();
	}

	uint64_t key = polyglotHash(b);

	std::vector<const PolyglotEntry*> matches;
	for (auto& e : openingBook)
		if (e.key == key)
			matches.push_back(&e);

	if (matches.empty()) {
		std::ostringstream log;
		log << "book no hit: key=0x" << std::hex << key;
		engineLog(log.str());
		return Move();
	}

	if (moveGenerator == nullptr) {
		engineLog("book hit ignored: move generator is not attached");
		return Move();
	}

	std::vector<Move> legalMoves = moveGenerator->generateLegalMoves(b);

	struct BookCandidate {
		Move move;
		uint16_t weight;
		uint16_t rawMove;
	};

	std::vector<BookCandidate> candidates;
	candidates.reserve(matches.size());
	int rejected = 0;
	for (const PolyglotEntry* entry : matches) {
		Move decoded = polyglotDecodeMove(entry->move, b);
		Move legal;
		if (findLegalEquivalent(legalMoves, decoded, legal)) {
			candidates.push_back(BookCandidate{ legal, entry->weight, entry->move });
		}
		else {
			++rejected;
		}
	}

	if (candidates.empty()) {
		std::ostringstream log;
		log << "book hit rejected: key=0x" << std::hex << key
			<< std::dec << " matching_entries=" << matches.size()
			<< " legal_candidates=0 rejected=" << rejected;
		engineLog(log.str());
		return Move();
	}

	// ---------------------------------------
	// Sort legal book moves by weight descending.
	// ---------------------------------------
	std::sort(candidates.begin(), candidates.end(),
		[](const BookCandidate& a, const BookCandidate& b) {
			return a.weight > b.weight;
		});

	int topWeight = candidates.front().weight;
	int threshold = topWeight > 1 ? std::max(1, (topWeight * 25) / 100) : topWeight;
	int poolSize = 0;
	while (poolSize < static_cast<int>(candidates.size()) &&
		poolSize < MaxBookSelectionPool &&
		candidates[poolSize].weight >= threshold) {
		++poolSize;
	}
	if (poolSize == 0) {
		poolSize = 1;
	}

	std::vector<double> selectionWeights;
	selectionWeights.reserve(poolSize);
	for (int i = 0; i < poolSize; ++i) {
		selectionWeights.push_back(candidates[i].weight > 0 ? candidates[i].weight : 1.0);
	}

	std::discrete_distribution<int> dist(selectionWeights.begin(), selectionWeights.end());
	const BookCandidate& chosen = candidates[dist(bookRng())];

	std::ostringstream log;
	log << "book hit: key=0x" << std::hex << key
		<< std::dec << " matching_entries=" << matches.size()
		<< " legal_candidates=" << candidates.size()
		<< " rejected=" << rejected
		<< " selection_pool=" << poolSize
		<< " selected=" << moveToUciText(chosen.move)
		<< " weight=" << chosen.weight
		<< " raw=0x" << std::hex << chosen.rawMove;
	engineLog(log.str());

	return chosen.move;
}
