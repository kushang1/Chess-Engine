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
#include <string>

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
static const int SEARCH_ABORTED = -2000000001;

static inline bool isSearchAborted(int score)
{
	return score == SEARCH_ABORTED;
}

namespace {

namespace fs = std::filesystem;

constexpr int DefaultHashMb = 128;
constexpr int MinHashMb = 1;
constexpr int MaxHashMb = 4096;
constexpr int MaxBookSelectionPool = 8;
constexpr const char* UciInfoEnvVar = "CHESS_ENGINE_UCI_INFO";

std::mt19937_64& bookRng()
{
	static std::mt19937_64 rng(std::random_device{}());
	return rng;
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

bool uciInfoOutputEnabled()
{
	const char* value = std::getenv(UciInfoEnvVar);
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}

void appendUciMove(std::string& out, const Move& move)
{
	if (move.from < 0 || move.to < 0) {
		out += "0000";
		return;
	}

	auto appendSquare = [&](int sq) {
		out.push_back(static_cast<char>('a' + (sq & 7)));
		out.push_back(static_cast<char>('8' - (sq >> 3)));
		};

	appendSquare(move.from);
	appendSquare(move.to);

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
}

void appendUciScore(std::string& out, int score)
{
	if (score > MATE_THRESHOLD) {
		int plies = MATE_SCORE - score;
		int moves = std::max(1, (plies + 1) / 2);
		out += " score mate " + std::to_string(moves);
		return;
	}

	if (score < -MATE_THRESHOLD) {
		int plies = MATE_SCORE + score;
		int moves = std::max(1, (plies + 1) / 2);
		out += " score mate -" + std::to_string(moves);
		return;
	}

	out += " score cp " + std::to_string(score);
}

void writeUciInfoLine(const std::string& line)
{
#ifdef _WIN32
	const std::string output = line + "\n";
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
		return;
	}

	DWORD written = 0;
	WriteFile(handle, output.data(), static_cast<DWORD>(output.size()), &written, nullptr);
#else
	std::fputs(line.c_str(), stdout);
	std::fputc('\n', stdout);
#endif
}

void emitCompletedDepthInfo(int depth, int score, const Move& bestMove,
	std::chrono::steady_clock::time_point searchStart)
{
	auto now = std::chrono::steady_clock::now();
	long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - searchStart).count();
	long long nodes = totalNodes.load(std::memory_order_relaxed);
	long long nps = elapsedMs > 0 ? (nodes * 1000LL) / elapsedMs : 0;

	std::string line;
	line.reserve(96);
	line += "info depth " + std::to_string(depth);
	line += " nodes " + std::to_string(nodes);
	line += " nps " + std::to_string(nps);
	line += " time " + std::to_string(elapsedMs);
	appendUciScore(line, score);
	line += " pv ";
	appendUciMove(line, bestMove);

	writeUciInfoLine(line);
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
	for (int ply = 0; ply < MAX_DEPTH; ++ply)
		for (int slot = 0; slot < 2; ++slot)
			killerMoves[ply][slot] = Move();  // invalid move

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

	std::string syzygyPath = resolveSyzygyPath();
	if (!syzygyPath.empty()) {
		initSyzygy(syzygyPath.c_str());
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
int Engine::scoreMove(const Move& m, const board& b, int ply,
	bool haveTTMove, const Move& ttMove)
{
	ply = std::clamp(ply, 0, MAX_DEPTH - 1);

	// 1. TT move first, even when the stored entry is shallow.
	if (haveTTMove && sameMoveIdentity(m, ttMove)) {
		return 1000000;
	}

	// 2. MVV-LVA captures before quiet moves.
	if (m.captured != EMPTY) {
		int victim = pieceValueSimple[m.captured];
		int attacker = pieceValueSimple[m.moved];
		return 500000 + (victim * 100 - attacker);
	}

	// 3. Killer moves are quiet beta-cutoff moves from the same ply.
	if (sameMoveIdentity(killerMoves[ply][0], m))
		return 400000;

	if (sameMoveIdentity(killerMoves[ply][1], m))
		return 390000;

	// 4. Quiet moves use side-aware history scores.
	int side = b.isWhiteTurn ? 0 : 1;
	return historyHeuristic[side][m.from][m.to];
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
		// White advances toward smaller board rows, so invert the row index.
		int advancedRanks = 7 - (sq >> 3);
		if (isPassed(sq, true)) score += 40 + 10 * advancedRanks;
	}

	// Black passed pawns
	for (int i = 0; i < b.pieceCount[BP]; i++) {
		int sq = b.pieceList[BP][i];
		// Black advances toward larger board rows.
		int advancedRanks = sq >> 3;
		if (isPassed(sq, false)) score -= 40 + 10 * advancedRanks;
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

				// Count enemy attacks on the king zone; the old code computed checks but never used them.
				if (moveGenerator->isSquareAttacked(b, sq, !kingIsWhite)) {
					if (kingIsWhite) {
						++atkWhiteKing;
					}
					else {
						++atkBlackKing;
					}
				}
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

		// The shield is on the rank in front of the king, including back-rank castled kings.
		if (isWhite && r > 0)
		{
			int front = r - 1;
			if (b.pieceAt(front * 8 + c) == WP) s += 15;
			if (c > 0 && b.pieceAt(front * 8 + c - 1) == WP) s += 10;
			if (c < 7 && b.pieceAt(front * 8 + c + 1) == WP) s += 10;
		}
		else if (!isWhite && r < 7)
		{
			int front = r + 1;
			if (b.pieceAt(front * 8 + c) == BP) s += 15;
			if (c > 0 && b.pieceAt(front * 8 + c - 1) == BP) s += 10;
			if (c < 7 && b.pieceAt(front * 8 + c + 1) == BP) s += 10;
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
	const bool emitUciInfo = uciInfoOutputEnabled();

	Move bookMove = probeBook(b);
	if (bookMove.from != -1)
	{
		return bookMove;
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

	if (syzygyIsAvailable()) {
		int tbScore = 0;
		Move tbMove;
		if (probeSyzygyRoot(b, tbScore, tbMove)) {
			Move legalTbMove;
			if (findLegalEquivalent(rootMoves, tbMove, legalTbMove)) {
				return legalTbMove;
			}
		}
	}

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

		int bestDepthScore = -INF;
		Move bestDepthMove = rootMoves[0];
		bool anyFullThisDepth = false;

		auto collectRootResults = [&]() {
			bestDepthScore = -INF;
			bestDepthMove = rootMoves[0];
			anyFullThisDepth = false;

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
			};

		auto searchRootMoves = [&](int rootAlpha, int rootBeta) {
			for (auto& r : results) {
				r = { Move(), -INF, false };
			}

			// Search root moves serially for tournament stability.
			for (size_t i = 0; i < rootMoves.size(); i++)
			{
				if (stopSearch.load(std::memory_order_relaxed)) {
					results[i] = { rootMoves[i], -INF, false };
					return false;
				}

				Move rm = rootMoves[i];
				board local = b;
				std::vector<uint64_t> localRepHistory = repHistory;

				Unmove u = local.makeMove(rm);
				(void)u;

				bool fullEval = true;
				int childScore = search(local, depth - 1, -rootBeta, -rootAlpha, localRepHistory);
				int score = -INF;
				if (isSearchAborted(childScore)) {
					results[i] = { rm, -INF, false };
					return false;
				}
				else {
					score = -childScore;
				}

				if (stopSearch.load(std::memory_order_relaxed))
					fullEval = false;

				results[i] = { rm, score, fullEval };
			}

			return true;
			};

		constexpr int AspirationWindow = 50;
		int rootAlpha = -INF;
		int rootBeta = INF;
		bool usedAspiration = false;

		if (depth > 1 && haveFull) {
			rootAlpha = std::max(-INF, bestFullScore - AspirationWindow);
			rootBeta = std::min(INF, bestFullScore + AspirationWindow);
			usedAspiration = true;
		}

		searchRootMoves(rootAlpha, rootBeta);
		collectRootResults();

		if (usedAspiration &&
			!stopSearch.load(std::memory_order_relaxed) &&
			anyFullThisDepth &&
			(bestDepthScore <= rootAlpha || bestDepthScore >= rootBeta))
		{
			// One stable fallback to full window on aspiration fail-low/high.
			searchRootMoves(-INF, INF);
			collectRootResults();
		}

		now = std::chrono::steady_clock::now();
		elapsed =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();

		bool timedOut = elapsed >= timeLimitMs || stopSearch.load();

		if (!timedOut)
		{
			if (anyFullThisDepth)
			{
				bestFullMove = bestDepthMove;
				bestFullScore = bestDepthScore;
				haveFull = true;

				if (emitUciInfo) {
					// Emit only after a fully completed root depth; never from node loops.
					emitCompletedDepthInfo(depth, bestFullScore, bestFullMove, searchStart);
				}
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
		
		return SEARCH_ABORTED;
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

	if (entry.flag != TT_EMPTY && entry.key == key)
	{
		// Always keep the stored best move for ordering, even from shallow entries.
		ttMove = entry.bestMove;
		haveTTMove = ttMove.from != -1 && ttMove.to != -1;

		if (entry.depth >= depth) {
			int stored = entry.score;

			if (entry.flag == TT_EXACT)
				return stored;
			else if (entry.flag == TT_ALPHA && stored <= alpha)
				return stored;
			else if (entry.flag == TT_BETA && stored >= beta)
				return stored;
		}

	}

	// Avoid tablebase overhead in the normal hot path; count pieces only when
	// a near-root Syzygy probe is actually possible.
	if (syzygyIsAvailable() && !b.hasEnPassant && depth >= maxDepth - 2) {
		int totalPieces = 0;
		for (int p = BQ; p <= WB; ++p)
			totalPieces += b.pieceCount[p];

		int tbScore;
		Move tbMove;
		if (totalPieces <= (int)TB_LARGEST && probeSyzygy(b, tbScore, tbMove)) {

			TTEntry& tbEntry = tt[key & ttMask];
			tbEntry.key = key;
			tbEntry.score = tbScore;
			tbEntry.depth = 127;     // highest possible depth
			tbEntry.flag = TT_EXACT;
			tbEntry.bestMove = tbMove;

			return tbScore;
		}
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
			int childScore = search(b, depth - 1 - R,
				-beta, -beta + 1,
				repHistory);

			// Undo null move
			b.isWhiteTurn = prevTurn;
			b.hasEnPassant = prevHasEP;
			b.enPassantSquare = prevEPSq;
            b.hash = prevHash;

			if (isSearchAborted(childScore)) {
				return SEARCH_ABORTED;
			}

			int score = -childScore;

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
	int ply = std::clamp(maxDepth - depth, 0, MAX_DEPTH - 1);

	for (int i = 0; i < moves.count; ++i)
	{
		Move& m = moveData[i];
		scores[i] = scoreMove(m, b, ply, haveTTMove, ttMove);
	}

	int besteval = -INF;
	Move bestMoveLocal;

	// -------------------------------
	// SEARCH CHILD MOVES
	// -------------------------------
	int moveIndex = 0;
	bool firstMove = true;

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
			sameMoveIdentity(killerMoves[ply][0], move) ||
			sameMoveIdentity(killerMoves[ply][1], move);

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
		bool usePvs = !firstMove && !inCheck && depth > 1;

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
			int childScore = search(b, newDepth - R,
				-alpha - 1, -alpha,
				repHistory);
			if (isSearchAborted(childScore)) {
				b.unmakeMove(move, u);
				return SEARCH_ABORTED;
			}

			eval = -childScore;
			eval = fix_mate_score(eval, 1);

			// If it looks better than alpha, research at full depth
			if (eval > alpha) {
				childScore = search(b, newDepth,
					-beta, -alpha,
					repHistory);
				if (isSearchAborted(childScore)) {
					b.unmakeMove(move, u);
					return SEARCH_ABORTED;
				}

				eval = -childScore;
				eval = fix_mate_score(eval, 1);
			}
		}
		else {
			int childScore;
			if (usePvs) {
				// PVS: late moves get a null-window probe before a full re-search.
				childScore = search(b, newDepth,
					-alpha - 1, -alpha,
					repHistory);
				if (isSearchAborted(childScore)) {
					b.unmakeMove(move, u);
					return SEARCH_ABORTED;
				}

				eval = -childScore;
				eval = fix_mate_score(eval, 1);

				if (eval > alpha) {
					childScore = search(b, newDepth,
						-beta, -alpha,
						repHistory);
					if (isSearchAborted(childScore)) {
						b.unmakeMove(move, u);
						return SEARCH_ABORTED;
					}

					eval = -childScore;
					eval = fix_mate_score(eval, 1);
				}
			}
			else {
				// First move, check nodes, and shallow nodes keep the full window.
				childScore = search(b, newDepth,
					-beta, -alpha,
					repHistory);
				if (isSearchAborted(childScore)) {
					b.unmakeMove(move, u);
					return SEARCH_ABORTED;
				}

				eval = -childScore;
				eval = fix_mate_score(eval, 1);
			}
		}
		
		firstMove = false;
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
		}

		// Alpha–beta cutoff
		if (alpha >= beta) {
			if (!isCapture) {
				// Quiet beta cutoffs update killer and side-aware history tables.
				if (!sameMoveIdentity(killerMoves[ply][0], move)) {
					killerMoves[ply][1] = killerMoves[ply][0];
					killerMoves[ply][0] = move;
				}
				int side = b.isWhiteTurn ? 0 : 1;
				historyHeuristic[side][move.from][move.to] += depth * depth;
			}
			break;
		}

		moveIndex++;
	}


	if (stopSearch.load(std::memory_order_relaxed)) {
		return SEARCH_ABORTED;
	}

	// -----------------------------------
	// STORE TT ENTRY
	// Do not store partial results after a timeout/stop abort.
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
		return SEARCH_ABORTED;
	}

	int kingSq = moveGenerator->findKing(b, b.isWhiteTurn);
	bool inCheck = kingSq != -1 &&
		moveGenerator->isSquareAttacked(b, kingSq, !b.isWhiteTurn);

	MoveList moves;
	moveGenerator->generateLegalMoves(b, moves);

	int scores[MoveList::MAX_MOVES];
	Move* moveData = moves.data();
	int searchCount = 0;

	if (inCheck) {
		// Stand-pat is illegal while in check; search every legal evasion.
		if (moves.count == 0) {
			return -(MATE_SCORE - maxDepth);
		}

		searchCount = moves.count;
		for (int i = 0; i < searchCount; ++i) {
			scores[i] = scoreMove(moveData[i], b, MAX_DEPTH - 1, false, Move());
		}
	}
	else {
		// Stand-pat evaluation: assume the quiet position can be held.
		int standPat = evaluate(b);

		// Fail-high: too good for the opponent
		if (standPat >= beta)
			return standPat;

		if (standPat > alpha)
			alpha = standPat;

		// Score only captures using MVV-LVA (depth 0 so killers/history are harmless)
		for (auto& m : moves) {
			if (m.captured == EMPTY) continue; // only captures in normal quiescence
			moveData[searchCount] = m;
			scores[searchCount] = scoreMove(m, b, MAX_DEPTH - 1, false, Move());
			++searchCount;
		}

		if (searchCount == 0) {
			// No captures ? position is quiet, return stand-pat eval
			return alpha;
		}
	}

	for (int orderedIndex = 0; orderedIndex < searchCount; ++orderedIndex) {
		selectBestScoredMove(moveData, scores, orderedIndex, searchCount);
		const Move& m = moveData[orderedIndex];

		Unmove u = b.makeMove(m);
		int childScore = quiescence(b, -beta, -alpha);
		if (isSearchAborted(childScore)) {
			b.unmakeMove(m, u);
			return SEARCH_ABORTED;
		}

		int score = -childScore;
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

	FILE* f = nullptr;
	fopen_s(&f, filename.c_str(), "rb");
	if (!f) {
		return false;
	}

	openingBook.clear();
	if (!ec) {
		openingBook.reserve(static_cast<size_t>(fileBytes / 16));
	}

	uint8_t buf[16];
	while (fread(buf, 1, 16, f) == 16) {
		PolyglotEntry e;
		e.key = read_be_u64(buf + 0);
		e.move = read_be_u16(buf + 8);
		e.weight = read_be_u16(buf + 10);
		e.learn = read_be_u32(buf + 12);
		openingBook.push_back(e);
	}

	fclose(f);

	if (openingBook.empty()) {
		return false;
	}

	return true;
}




Move Engine::probeBook(board& b)
{
	if (openingBook.empty()) {
		return Move();
	}

	uint64_t key = polyglotHash(b);

	std::vector<const PolyglotEntry*> matches;
	for (auto& e : openingBook)
		if (e.key == key)
			matches.push_back(&e);

	if (matches.empty()) {
		return Move();
	}

	if (moveGenerator == nullptr) {
		return Move();
	}

	std::vector<Move> legalMoves = moveGenerator->generateLegalMoves(b);

	struct BookCandidate {
		Move move;
		uint16_t weight;
	};

	std::vector<BookCandidate> candidates;
	candidates.reserve(matches.size());
	for (const PolyglotEntry* entry : matches) {
		Move decoded = polyglotDecodeMove(entry->move, b);
		Move legal;
		if (findLegalEquivalent(legalMoves, decoded, legal)) {
			candidates.push_back(BookCandidate{ legal, entry->weight });
		}
	}

	if (candidates.empty()) {
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

	return chosen.move;
}
