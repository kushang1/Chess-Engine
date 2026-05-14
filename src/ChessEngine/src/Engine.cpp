#define _CRT_SECURE_NO_WARNINGS


#include "Engine.h"
#include "Profiler.h"

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

void Engine::setNodeLimit(long long nodes)
{
	nodeLimit = std::max(0LL, nodes);
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

int Engine::scoreToTT(int score, int ply) const
{
	if (score >= MATE_THRESHOLD) {
		return score + ply;
	}
	if (score <= -MATE_THRESHOLD) {
		return score - ply;
	}
	return score;
}

int Engine::scoreFromTT(int score, int ply) const
{
	if (score >= MATE_THRESHOLD) {
		return score - ply;
	}
	if (score <= -MATE_THRESHOLD) {
		return score + ply;
	}
	return score;
}

bool Engine::shouldStop()
{
	if (stopSearch.load(std::memory_order_relaxed)) {
		return true;
	}

	if (nodeLimit > 0 &&
		totalNodes.load(std::memory_order_relaxed) >= nodeLimit) {
		stopSearch.store(true, std::memory_order_relaxed);
		return true;
	}

	long long nodes = totalNodes.load(std::memory_order_relaxed);
	if ((nodes & 0x0FFF) == 0) {
		auto now = std::chrono::steady_clock::now();
		auto elapsedMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();
		if (elapsedMs >= timeLimitMs) {
			stopSearch.store(true, std::memory_order_relaxed);
			return true;
		}
	}

	return false;
}

static const int pieceValueSimple[13] = {
	0, 9, 5, 1, 3, 100, 3,
	   9, 5, 1, 3, 100, 3
};

static int pieceValueCp(Piece p)
{
	switch (p) {
	case WP: case BP: return 100;
	case WN: case BN: return 320;
	case WB: case BB: return 330;
	case WR: case BR: return 500;
	case WQ: case BQ: return 900;
	case WK: case BK: return 20000;
	default: return 0;
	}
}

static int promotionGainCp(const Move& m)
{
	return m.wasPromotion ? pieceValueCp(m.promotedTo) - pieceValueCp(m.moved) : 0;
}

static bool isWhitePieceFast(Piece p)
{
	return p >= WQ && p <= WB;
}

static Piece pawnPieceFor(bool white)
{
	return white ? WP : BP;
}

static Piece knightPieceFor(bool white)
{
	return white ? WN : BN;
}

static Piece bishopPieceFor(bool white)
{
	return white ? WB : BB;
}

static Piece rookPieceFor(bool white)
{
	return white ? WR : BR;
}

static Piece queenPieceFor(bool white)
{
	return white ? WQ : BQ;
}

static Piece kingPieceFor(bool white)
{
	return white ? WK : BK;
}

static Piece capturedPieceForMove(const Move& m)
{
	if (m.wasEnPassant && m.captured == EMPTY) {
		return isWhitePieceFast(m.moved) ? BP : WP;
	}
	return m.captured;
}

static bool isQuietMove(const Move& m)
{
	return m.captured == EMPTY && !m.wasEnPassant && !m.wasPromotion;
}

#ifdef ENABLE_ENGINE_PROFILING
static void profileApproximateSeeCallSite()
{
	switch (Profiler::currentSeeContext()) {
	case Profiler::SeeContext::MoveOrdering:
		PROFILE_INC(::Profiler::ApproximateSeeForMoveOrdering);
		break;
	case Profiler::SeeContext::MainSearchPruning:
		PROFILE_INC(::Profiler::ApproximateSeeForMainSearchPruning);
		break;
	case Profiler::SeeContext::QsearchPruning:
		PROFILE_INC(::Profiler::ApproximateSeeForQsearchPruning);
		break;
	case Profiler::SeeContext::QsearchMoveOrdering:
		PROFILE_INC(::Profiler::ApproximateSeeForQsearchMoveOrdering);
		break;
	case Profiler::SeeContext::PromotionHandling:
		PROFILE_INC(::Profiler::ApproximateSeeForPromotionHandling);
		break;
	case Profiler::SeeContext::Other:
	default:
		PROFILE_INC(::Profiler::ApproximateSeeOther);
		break;
	}
}

static void profileApproximateSeeMoveType(const Move& move)
{
	if (move.captured != EMPTY || move.wasEnPassant) {
		PROFILE_INC(::Profiler::ApproximateSeeOnCapture);
	}
	if (move.wasPromotion) {
		PROFILE_INC(::Profiler::ApproximateSeeOnPromotion);
	}
	if (move.captured == EMPTY && !move.wasEnPassant && !move.wasPromotion) {
		PROFILE_INC(::Profiler::ApproximateSeeOnQuiet);
	}
}

static void profileApproximateSeeResult(int gain)
{
	if (gain > 0) {
		PROFILE_INC(::Profiler::ApproximateSeePositive);
	}
	else if (gain < 0) {
		PROFILE_INC(::Profiler::ApproximateSeeNegative);
	}
	else {
		PROFILE_INC(::Profiler::ApproximateSeeEqual);
	}
}
#endif // ENABLE_ENGINE_PROFILING

static Bitboard attackersToSquare(const Bitboard pieces[13], int sq, bool byWhite, Bitboard occ)
{
	if (sq < 0 || sq >= 64) {
		return 0;
	}

	Bitboard attackers = 0;
	attackers |= Bitboards::PawnAttackers[byWhite ? Bitboards::WHITE : Bitboards::BLACK][sq]
		& pieces[pawnPieceFor(byWhite)];
	attackers |= Bitboards::KnightAttacks[sq] & pieces[knightPieceFor(byWhite)];
	attackers |= Bitboards::KingAttacks[sq] & pieces[kingPieceFor(byWhite)];
	attackers |= Bitboards::bishopAttacks(sq, occ) &
		(pieces[bishopPieceFor(byWhite)] | pieces[queenPieceFor(byWhite)]);
	attackers |= Bitboards::rookAttacks(sq, occ) &
		(pieces[rookPieceFor(byWhite)] | pieces[queenPieceFor(byWhite)]);
	return attackers;
}

static Bitboard attackMapForSide(const board& b, bool byWhite)
{
	Bitboard attacks = 0;
	const Bitboard occ = b.occupied;
	const int color = byWhite ? Bitboards::WHITE : Bitboards::BLACK;

	Bitboard pieces = b.pieces(pawnPieceFor(byWhite));
	while (pieces != 0) {
		int from = Bitboards::poplsb(pieces);
		attacks |= Bitboards::PawnAttacks[color][from];
	}

	pieces = b.pieces(knightPieceFor(byWhite));
	while (pieces != 0) {
		int from = Bitboards::poplsb(pieces);
		attacks |= Bitboards::KnightAttacks[from];
	}

	pieces = b.pieces(bishopPieceFor(byWhite));
	while (pieces != 0) {
		int from = Bitboards::poplsb(pieces);
		attacks |= Bitboards::bishopAttacks(from, occ);
	}

	pieces = b.pieces(rookPieceFor(byWhite));
	while (pieces != 0) {
		int from = Bitboards::poplsb(pieces);
		attacks |= Bitboards::rookAttacks(from, occ);
	}

	pieces = b.pieces(queenPieceFor(byWhite));
	while (pieces != 0) {
		int from = Bitboards::poplsb(pieces);
		attacks |= Bitboards::queenAttacks(from, occ);
	}

	pieces = b.pieces(kingPieceFor(byWhite));
	while (pieces != 0) {
		int from = Bitboards::poplsb(pieces);
		attacks |= Bitboards::KingAttacks[from];
	}

	return attacks;
}

static Bitboard kingSafetyZoneMask(int kingSq)
{
	if (kingSq < 0 || kingSq >= 64) {
		return 0;
	}

	static const int kingZone[12][2] = {
		{-1,-1},{-1,0},{-1,1},
		{0,-1},        {0,1},
		{1,-1},{1,0},{1,1},
		{-2,0}, {-1,-2}, {-1,2}, {2,0}
	};

	Bitboard zone = 0;
	int r = kingSq >> 3;
	int c = kingSq & 7;
	for (const auto& d : kingZone) {
		int nr = r + d[0];
		int nc = c + d[1];
		if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8) {
			zone |= Bitboards::bit((nr << 3) | nc);
		}
	}
	return zone;
}

static bool recaptureLeavesKingSafe(const Bitboard pieces[13], Bitboard occ,
	int target, int from, Piece attacker, bool recapturingWhite, Piece capturedOnTarget)
{
	Bitboard afterPieces[13];
	for (int p = 0; p < 13; ++p) {
		afterPieces[p] = pieces[p];
	}

	Bitboard fromMask = Bitboards::bit(from);
	Bitboard targetMask = Bitboards::bit(target);
	afterPieces[attacker] &= ~fromMask;
	afterPieces[attacker] |= targetMask;
	afterPieces[capturedOnTarget] &= ~targetMask;

	Bitboard occAfter = occ & ~fromMask;
	Piece recapturingKing = kingPieceFor(recapturingWhite);
	int kingSq = -1;
	if (attacker == recapturingKing) {
		kingSq = target;
	}
	else if (afterPieces[recapturingKing] != 0) {
		kingSq = Bitboards::lsb(afterPieces[recapturingKing]);
	}

	return kingSq != -1 &&
		attackersToSquare(afterPieces, kingSq, !recapturingWhite, occAfter) == 0;
}

static bool hasLegalRecaptureTo(const Bitboard pieces[13], Bitboard occ,
	int target, bool recapturingWhite, Piece capturedOnTarget)
{
	const Piece attackersByValue[6] = {
		pawnPieceFor(recapturingWhite),
		knightPieceFor(recapturingWhite),
		bishopPieceFor(recapturingWhite),
		rookPieceFor(recapturingWhite),
		queenPieceFor(recapturingWhite),
		kingPieceFor(recapturingWhite)
	};

	Bitboard attackers = attackersToSquare(pieces, target, recapturingWhite, occ);
	for (Piece attacker : attackersByValue) {
		Bitboard candidates = attackers & pieces[attacker];
		while (candidates != 0) {
			int from = Bitboards::poplsb(candidates);
			PROFILE_INC(::Profiler::ApproximateSeeRecaptureCandidates);
			if (recaptureLeavesKingSafe(pieces, occ, target, from, attacker,
				recapturingWhite, capturedOnTarget)) {
				return true;
			}
		}
	}

	return false;
}

static int approximateSee(const board& b, MoveGenerator* generator, const Move& move)
{
	(void)generator;
	PROFILE_INC(::Profiler::ApproximateSeeCalls);
	PROFILE_TIMER(::Profiler::ApproximateSeeTime);
#ifdef ENABLE_ENGINE_PROFILING
	profileApproximateSeeCallSite();
	profileApproximateSeeMoveType(move);
#endif

	if (move.captured == EMPTY && !move.wasEnPassant) {
		PROFILE_INC(::Profiler::ApproximateSeeEarlyReturns);
		int gain = promotionGainCp(move);
#ifdef ENABLE_ENGINE_PROFILING
		profileApproximateSeeResult(gain);
#endif
		return gain;
	}

	Piece captured = capturedPieceForMove(move);
	int gain = pieceValueCp(captured) + promotionGainCp(move);

	Bitboard pieces[13];
	for (int p = 0; p < 13; ++p) {
		pieces[p] = b.pieceBB[p];
	}

	Bitboard occ = b.occupied;
	Bitboard fromMask = Bitboards::bit(move.from);
	Bitboard toMask = Bitboards::bit(move.to);
	Piece movedAfterCapture = move.wasPromotion ? move.promotedTo : move.moved;

	pieces[move.moved] &= ~fromMask;
	occ &= ~fromMask;

	if (move.wasEnPassant) {
		int capturedSq = isWhitePieceFast(move.moved) ? (move.to + 8) : (move.to - 8);
		Bitboard capturedMask = Bitboards::bit(capturedSq);
		pieces[captured] &= ~capturedMask;
		occ &= ~capturedMask;
	}
	else if (captured != EMPTY) {
		pieces[captured] &= ~toMask;
		occ &= ~toMask;
	}

	pieces[movedAfterCapture] |= toMask;
	occ |= toMask;

	bool movedWhite = isWhitePieceFast(move.moved);
	if (hasLegalRecaptureTo(pieces, occ, move.to, !movedWhite, movedAfterCapture)) {
		gain -= pieceValueCp(movedAfterCapture);
	}

#ifdef ENABLE_ENGINE_PROFILING
	profileApproximateSeeResult(gain);
#endif

	return gain;
}

static int scoreCaptureWithSee(const Move& m, int see)
{
	Piece captured = capturedPieceForMove(m);
	int victim = pieceValueSimple[captured];
	int attacker = pieceValueSimple[m.moved];
	if (see >= 0) {
		return 650000 + see + (victim * 100 - attacker);
	}
	return 150000 + see + (victim * 100 - attacker);
}



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
	PROFILE_INC(::Profiler::ScoreMoveCalls);

	ply = std::clamp(ply, 0, MAX_DEPTH - 1);

	// 1. TT move first, even when the stored entry is shallow.
	if (haveTTMove && sameMoveIdentity(m, ttMove)) {
		PROFILE_INC(::Profiler::TTMoveScoreHits);
		return 1000000;
	}

	if (m.wasPromotion) {
		PROFILE_INC(::Profiler::PromotionScored);
		return 850000 + pieceValueCp(m.promotedTo) + (m.captured != EMPTY ? pieceValueCp(m.captured) : 0);
	}

	// 2. Winning/equal captures are searched before quiet moves; losing
	// captures are delayed so quiet refutations are not buried behind MVV-LVA.
	if (m.captured != EMPTY) {
		PROFILE_INC(::Profiler::CaptureScored);
		int see = approximateSee(b, moveGenerator, m);
		return scoreCaptureWithSee(m, see);
	}

	PROFILE_INC(::Profiler::QuietScored);

	// 3. Killer moves are quiet beta-cutoff moves from the same ply.
	if (sameMoveIdentity(killerMoves[ply][0], m)) {
		PROFILE_INC(::Profiler::KillerScored);
		return 400000;
	}

	if (sameMoveIdentity(killerMoves[ply][1], m)) {
		PROFILE_INC(::Profiler::KillerScored);
		return 390000;
	}

	// 4. Quiet moves use side-aware history scores.
	int side = b.isWhiteTurn ? 0 : 1;
	PROFILE_INC(::Profiler::HistoryScored);
	return std::clamp(historyHeuristic[side][m.from][m.to], -200000, 350000);
}

// ==========================================================
// STATIC EVALUATION
// ==========================================================
int Engine::evaluate(board& b)
{
	PROFILE_INC(::Profiler::EvalCalls);
	PROFILE_TIMER(::Profiler::EvalTime);

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
	{
		PROFILE_TIMER(::Profiler::EvalMaterialPstTime);
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
	}

	// ----------------------------------------------------------
	// MOBILITY
	// ----------------------------------------------------------
	{
		PROFILE_TIMER(::Profiler::EvalMobilityActivityTime);
		for (int i = 0; i < b.pieceCount[WN]; i++) score += knightMob[b.pieceList[WN][i]];
		for (int i = 0; i < b.pieceCount[BN]; i++) score -= knightMob[b.pieceList[BN][i]];

		for (int i = 0; i < b.pieceCount[WB]; i++) score += bishopMob[b.pieceList[WB][i]];
		for (int i = 0; i < b.pieceCount[BB]; i++) score -= bishopMob[b.pieceList[BB][i]];

		for (int i = 0; i < b.pieceCount[WR]; i++) score += rookMob[b.pieceList[WR][i]];
		for (int i = 0; i < b.pieceCount[BR]; i++) score -= rookMob[b.pieceList[BR][i]];
	}

	// ----------------------------------------------------------
	// PAWN STRUCTURE ? Passed pawns
	// ----------------------------------------------------------
	{
		PROFILE_TIMER(::Profiler::EvalPassedPawnTime);
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
	}

	// ----------------------------------------------------------
	// DOUBLED PAWNS
	// ----------------------------------------------------------
	{
		PROFILE_TIMER(::Profiler::EvalPawnStructureTime);
		int wFileCnt[8] = { 0 }, bFileCnt[8] = { 0 };

		for (int i = 0; i < b.pieceCount[WP]; i++)
			wFileCnt[b.pieceList[WP][i] & 7]++;

		for (int i = 0; i < b.pieceCount[BP]; i++)
			bFileCnt[b.pieceList[BP][i] & 7]++;

		for (int f = 0; f < 8; f++) {
			if (wFileCnt[f] > 1) score -= 15 * (wFileCnt[f] - 1);
			if (bFileCnt[f] > 1) score += 15 * (bFileCnt[f] - 1);
		}
	}

	// ----------------------------------------------------------
	// KING SAFETY (big Elo booster)
	// ----------------------------------------------------------
	{
		PROFILE_TIMER(::Profiler::EvalKingSafetyTime);
		int wKing = b.pieceList[WK][0];
		int bKing = b.pieceList[BK][0];

		Bitboard whiteAttacks = attackMapForSide(b, true);
		Bitboard blackAttacks = attackMapForSide(b, false);
		int atkWhiteKing = Bitboards::popcount(kingSafetyZoneMask(wKing) & blackAttacks);
		int atkBlackKing = Bitboards::popcount(kingSafetyZoneMask(bKing) & whiteAttacks);

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
		bool wPawnFiles[8] = { false };
		bool bPawnFiles[8] = { false };
		for (int i = 0; i < b.pieceCount[WP]; i++) {
			wPawnFiles[b.pieceList[WP][i] & 7] = true;
		}
		for (int i = 0; i < b.pieceCount[BP]; i++) {
			bPawnFiles[b.pieceList[BP][i] & 7] = true;
		}

		auto fileHasPawn = [&](int f, bool white) {
			return white ? wPawnFiles[f] : bPawnFiles[f];
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

#ifdef ENABLE_ENGINE_PROFILING
static Profiler::CounterId profileCutoffMoveIndexCounter(int moveIndex)
{
	if (moveIndex <= 0) return Profiler::CutoffMoveIndex0;
	if (moveIndex == 1) return Profiler::CutoffMoveIndex1;
	if (moveIndex == 2) return Profiler::CutoffMoveIndex2;
	if (moveIndex == 3) return Profiler::CutoffMoveIndex3;
	if (moveIndex == 4) return Profiler::CutoffMoveIndex4;
	if (moveIndex <= 7) return Profiler::CutoffMoveIndex5To7;
	if (moveIndex <= 15) return Profiler::CutoffMoveIndex8To15;
	return Profiler::CutoffMoveIndex16Plus;
}
#endif // ENABLE_ENGINE_PROFILING



Move Engine::findBestMove(board& b, int maxDepth,
	const std::vector<uint64_t>& globalReps,
	const std::vector<Move>& rootMoveFilter)
{
	resetSearchStats();
	

	maxDepth = std::clamp(maxDepth, 1, MAX_DEPTH - 1);
	this->maxDepth = maxDepth;
	const bool emitUciInfo = uciInfoOutputEnabled();

	// --------------------------------------------
	// Search start time and repetition root.
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

	std::vector<Move> rootMoves;
	{
		PROFILE_MOVEGEN_CONTEXT(Root);
		rootMoves = moveGenerator->generateLegalMoves(b);
	}
	if (!rootMoveFilter.empty()) {
		std::vector<Move> filtered;
		filtered.reserve(rootMoveFilter.size());
		for (const Move& legal : rootMoves) {
			for (const Move& requested : rootMoveFilter) {
				if (sameMoveIdentity(legal, requested)) {
					filtered.push_back(legal);
					break;
				}
			}
		}

		if (!filtered.empty()) {
			rootMoves = std::move(filtered);
		}
	}

	if (rootMoves.empty()) {
		return Move(-1, -1, EMPTY, EMPTY, 0);
	}

	if (rootMoveFilter.empty()) {
		/*Move bookMove = probeBook(b);
		if (bookMove.from != -1) {
			Move legalBookMove;
			if (findLegalEquivalent(rootMoves, bookMove, legalBookMove)) {
				return legalBookMove;
			}
		}*/
	}

	if(rootMoves.size() == 1)
	{
		return rootMoves[0];
	}

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
	Move bestSafeMove = rootMoves[0];
	int bestSafeScore = -INF;
	bool haveSafe = false;
	std::vector<int> rootMoveScores(rootMoves.size(), -INF);
	std::vector<char> rootMoveHasScore(rootMoves.size(), 0);

	auto sortRootMovesByPreviousScores = [&]() {
		if (rootMoves.size() < 2) {
			return;
		}

		std::vector<size_t> order(rootMoves.size());
		for (size_t i = 0; i < order.size(); ++i) {
			order[i] = i;
		}

		std::stable_sort(order.begin(), order.end(),
			[&](size_t lhs, size_t rhs) {
				if (rootMoveHasScore[lhs] != rootMoveHasScore[rhs]) {
					return rootMoveHasScore[lhs] > rootMoveHasScore[rhs];
				}
				if (rootMoveHasScore[lhs] &&
					rootMoveScores[lhs] != rootMoveScores[rhs]) {
					return rootMoveScores[lhs] > rootMoveScores[rhs];
				}
				return false;
			});

		std::vector<Move> orderedMoves;
		std::vector<int> orderedScores;
		std::vector<char> orderedHasScore;
		orderedMoves.reserve(rootMoves.size());
		orderedScores.reserve(rootMoveScores.size());
		orderedHasScore.reserve(rootMoveHasScore.size());

		for (size_t index : order) {
			orderedMoves.push_back(rootMoves[index]);
			orderedScores.push_back(rootMoveScores[index]);
			orderedHasScore.push_back(rootMoveHasScore[index]);
		}

		rootMoves = std::move(orderedMoves);
		rootMoveScores = std::move(orderedScores);
		rootMoveHasScore = std::move(orderedHasScore);
		};

	struct RootSearchResult {
		Move move;
		int score = -INF;
		int completedDepth = 0;
		bool fullySearched = false;
		bool aborted = false;
	};

	struct RootIterationResult {
		std::vector<RootSearchResult> results;
		Move bestMove;
		int bestScore = -INF;
		bool anyCompleted = false;
		bool completedAll = false;
		bool stopped = false;
		bool betaCutoff = false;
	};

	auto rememberSafeMove = [&](const RootIterationResult& result) {
		if (result.anyCompleted &&
			(!haveSafe || result.bestScore > bestSafeScore)) {
			bestSafeMove = result.bestMove;
			bestSafeScore = result.bestScore;
			haveSafe = true;
		}
		};

	auto rememberRootScores = [&](const RootIterationResult& result) {
		if (result.results.size() != rootMoveScores.size()) {
			return;
		}

		for (size_t i = 0; i < result.results.size(); ++i) {
			if (result.results[i].fullySearched) {
				rootMoveScores[i] = result.results[i].score;
				rootMoveHasScore[i] = 1;
			}
		}
		};

	auto searchRootMoves = [&](int depth, int rootAlpha, int rootBeta) {
		RootIterationResult result;
		result.results.resize(rootMoves.size());
		result.bestMove = rootMoves[0];

		int alpha = rootAlpha;
		bool firstMove = true;

		for (size_t i = 0; i < rootMoves.size(); ++i) {
			RootSearchResult& entry = result.results[i];
			entry.move = rootMoves[i];
			entry.completedDepth = depth;

			if (shouldStop()) {
				entry.aborted = true;
				result.stopped = true;
				break;
			}

			Move rm = rootMoves[i];
			board local = b;
			std::vector<uint64_t> localRepHistory = repHistory;
			local.makeMove(rm);

			const int childDepth = depth - 1;
			int childScore = 0;
			int score = -INF;

			if (firstMove) {
				childScore = search(local, childDepth, -rootBeta, -alpha,
					1, localRepHistory, true, 0);
				if (isSearchAborted(childScore)) {
					entry.aborted = true;
					result.stopped = true;
					break;
				}
				score = -childScore;
			}
			else {
				childScore = search(local, childDepth, -alpha - 1, -alpha,
					1, localRepHistory, false, 0);
				if (isSearchAborted(childScore)) {
					entry.aborted = true;
					result.stopped = true;
					break;
				}
				score = -childScore;

				if (score > alpha && score < rootBeta) {
					childScore = search(local, childDepth, -rootBeta, -alpha,
						1, localRepHistory, true, 0);
					if (isSearchAborted(childScore)) {
						entry.aborted = true;
						result.stopped = true;
						break;
					}
					score = -childScore;
				}
			}

			if (shouldStop()) {
				entry.aborted = true;
				result.stopped = true;
				break;
			}

			entry.score = score;
			entry.fullySearched = true;
			result.anyCompleted = true;
			firstMove = false;

			if (score > result.bestScore) {
				result.bestScore = score;
				result.bestMove = rm;
			}

			if (score > alpha) {
				alpha = score;
			}

			if (alpha >= rootBeta) {
				result.betaCutoff = true;
				break;
			}
		}

		result.completedAll =
			result.anyCompleted &&
			!result.stopped &&
			!result.betaCutoff;
		return result;
		};

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

		if (haveFull) {
			sortRootMovesByPreviousScores();
		}

		constexpr int InitialAspirationWindow = 50;
		constexpr int MaxAspirationAttempts = 6;

		int rootAlpha = -INF;
		int rootBeta = INF;
		int delta = InitialAspirationWindow;
		bool depthCompleted = false;
		RootIterationResult lastAttempt;

		if (depth > 1 && haveFull) {
			rootAlpha = std::max(-INF, bestFullScore - delta);
			rootBeta = std::min(INF, bestFullScore + delta);
		}

		for (int attempt = 0; attempt < MaxAspirationAttempts; ++attempt) {
			lastAttempt = searchRootMoves(depth, rootAlpha, rootBeta);
			rememberSafeMove(lastAttempt);

			if (lastAttempt.stopped) {
				break;
			}

			if (lastAttempt.completedAll &&
				lastAttempt.bestScore > rootAlpha &&
				lastAttempt.bestScore < rootBeta) {
				depthCompleted = true;
				break;
			}

			if (rootAlpha == -INF && rootBeta == INF) {
				depthCompleted = lastAttempt.completedAll;
				break;
			}

			if (lastAttempt.completedAll && lastAttempt.bestScore <= rootAlpha) {
				rootAlpha = std::max(-INF, rootAlpha - delta);
			}
			else {
				rootBeta = std::min(INF, rootBeta + delta);
			}

			delta *= 2;
			if (attempt == MaxAspirationAttempts - 2) {
				rootAlpha = -INF;
				rootBeta = INF;
			}
		}

		now = std::chrono::steady_clock::now();
		elapsed =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();

		bool timedOut = elapsed >= timeLimitMs || stopSearch.load();

		if (!timedOut && depthCompleted)
		{
			rememberRootScores(lastAttempt);
			bestFullMove = lastAttempt.bestMove;
			bestFullScore = lastAttempt.bestScore;
			haveFull = true;
			bestSafeMove = bestFullMove;
			bestSafeScore = bestFullScore;
			haveSafe = true;

			if (emitUciInfo) {
				// Emit only after a fully completed root depth; never from node loops.
				emitCompletedDepthInfo(depth, bestFullScore, bestFullMove, searchStart);
			}
			continue;
		}

		// --------------------------------------------
		// Timeout/stop or incomplete aspiration. Keep the last completed
		// iteration as the baseline, but allow a fully searched root move from
		// this partial depth to replace it when its score is safely better.
		// --------------------------------------------
		if (!haveFull && haveSafe) {
			bestFullMove = bestSafeMove;
			bestFullScore = bestSafeScore;
			haveFull = true;
		}
		else if (haveFull && haveSafe && bestSafeScore > bestFullScore) {
			bestFullMove = bestSafeMove;
			bestFullScore = bestSafeScore;
		}

		break;
	}


	if (haveFull) {
		return bestFullMove;
	}
	if (haveSafe) {
		return bestSafeMove;
	}
	return rootMoves[0];
}


int Engine::search(board& b, int depth, int alpha, int beta, int ply,
	std::vector<uint64_t>& repHistory, bool pvNode, int extensionCount)
{
	depth = std::clamp(depth, 0, MAX_DEPTH - 1);
	const int tablePly = std::clamp(ply, 0, MAX_DEPTH - 1);
	totalNodes++;
	PROFILE_INC(::Profiler::SearchNodes);
#ifdef ENABLE_ENGINE_PROFILING
	const int profilePly = std::clamp(ply, 0, Profiler::MAX_PROFILE_PLY - 1);
	PROFILE_MAX(::Profiler::MaxPly, profilePly);
	PROFILE_ADD(::Profiler::NodesByPly0 + profilePly, 1);
#endif

	if (shouldStop()) {
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

	// Leaf ? quiescence search
	if (depth == 0) {
		leafNodes++;
		PROFILE_INC(::Profiler::LeafNodes);
		return quiescence(b, alpha, beta, ply, 0, repHistory);
	}

	RepetitionFrame repetitionFrame(repHistory, key);
	int alphaOrig = alpha;

	// -------------------------------
	// TRANSPOSITION TABLE PROBE
	// -------------------------------
	TTEntry* entry = (tt != nullptr && ttSize != 0) ? &tt[key & ttMask] : nullptr;
	if (entry != nullptr) {
		PROFILE_INC(::Profiler::TTProbes);
	}

	Move ttMove;
	bool haveTTMove = false;

	if (entry != nullptr && entry->flag != TT_EMPTY && entry->key == key)
	{
		PROFILE_INC(::Profiler::TTHits);
		if (entry->flag == TT_EXACT) {
			PROFILE_INC(::Profiler::TTExactHits);
		}
		else if (entry->flag == TT_ALPHA) {
			PROFILE_INC(::Profiler::TTAlphaHits);
		}
		else if (entry->flag == TT_BETA) {
			PROFILE_INC(::Profiler::TTBetaHits);
		}

		// Always keep the stored best move for ordering, even from shallow entries.
		ttMove = entry->bestMove;
		haveTTMove = ttMove.from != -1 && ttMove.to != -1;

		if (entry->depth >= depth) {
			int stored = scoreFromTT(entry->score, ply);

			if (entry->flag == TT_EXACT) {
				PROFILE_INC(::Profiler::TTCutoffs);
				return stored;
			}
			else if (entry->flag == TT_ALPHA && stored <= alpha) {
				PROFILE_INC(::Profiler::TTCutoffs);
				return stored;
			}
			else if (entry->flag == TT_BETA && stored >= beta) {
				PROFILE_INC(::Profiler::TTCutoffs);
				return stored;
			}
		}

	}
	else if (entry != nullptr) {
		PROFILE_INC(::Profiler::TTMisses);
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

			if (tt != nullptr && ttSize != 0) {
				TTEntry& tbEntry = tt[key & ttMask];
				PROFILE_INC(::Profiler::TTStores);
				if (tbEntry.flag != TT_EMPTY) {
					PROFILE_INC(::Profiler::TTOverwrites);
				}
				tbEntry.key = key;
				tbEntry.score = scoreToTT(tbScore, ply);
				tbEntry.depth = 127;     // highest possible depth
				tbEntry.flag = TT_EXACT;
				tbEntry.bestMove = tbMove;
			}

			return tbScore;
		}
	}

	

	// -------------------------------
	// IN-CHECK DETECTION
	// -------------------------------
	int kingSq = -1;
	bool inCheck = false;
	{
		PROFILE_INC(::Profiler::InCheckCalls);
		PROFILE_TIMER(::Profiler::InCheckTime);
		kingSq = moveGenerator->findKing(b, b.isWhiteTurn);
		inCheck = moveGenerator->isSquareAttacked(b, kingSq, !b.isWhiteTurn);
	}



	// -------------------------------
	// NULL MOVE PRUNING
	// -------------------------------
	// Conditions:
	//  - depth high enough (depth >= 3)
	//  - not in check
	//  - side to move has some non-pawn material (avoid zugzwang-ish endings)
	if (!pvNode && depth >= 3 && !inCheck) {
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
			PROFILE_INC(::Profiler::NullMoveAttempts);
			int staticEval = evaluate(b);
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

			int R = 2 + depth / 6;
			if (staticEval >= beta + 200) {
				++R;
			}
			R = std::min(R, depth - 1);
			int childScore = search(b, depth - 1 - R,
				-beta, -beta + 1,
				ply + 1, repHistory, false, extensionCount);

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
				PROFILE_INC(::Profiler::NullMoveCutoffs);
				return score;
			}
		}
	}
	// -------------------------------
	// NORMAL MOVE GENERATION
	// -------------------------------
	MoveList moves;
	{
		PROFILE_MOVEGEN_CONTEXT(Search);
		moveGenerator->generateLegalMoves(b, moves);
	}

	// No legal moves ? checkmate or stalemate
	if (moves.count == 0) {
		// we already have inCheck from above
		if (inCheck) {
			return -MATE_SCORE + ply;
		}
		else {
			return 0; // stalemate
		}
	}

	// Score and order moves
	int scores[MoveList::MAX_MOVES];
	Move* moveData = moves.data();

	{
		PROFILE_SEE_CONTEXT(MoveOrdering);
		for (int i = 0; i < moves.count; ++i)
		{
			Move& m = moveData[i];
			scores[i] = scoreMove(m, b, tablePly, haveTTMove, ttMove);
		}
	}

	int besteval = -INF;
	Move bestMoveLocal;
	bool selectivelyPruned = false;

	// -------------------------------
	// SEARCH CHILD MOVES
	// -------------------------------
	int moveIndex = 0;
	int searchedMoves = 0;
	bool firstMove = true;

	for (int orderedIndex = 0; orderedIndex < moves.count; ++orderedIndex) {
		selectBestScoredMove(moveData, scores, orderedIndex, moves.count);
		Move& move = moveData[orderedIndex];

		Unmove u = b.makeMove(move);

		bool isCapture = (move.captured != EMPTY) || move.wasEnPassant;

		// ---------------------------------------
		// CHECK EXTENSION: does this move give check?
		// After makeMove, b.isWhiteTurn is the opponent's turn.
		// So we find the opponent king and see if it's attacked
		int oppKingSq = -1;
		bool givesCheck = false;
		{
			PROFILE_INC(::Profiler::InCheckCalls);
			PROFILE_TIMER(::Profiler::InCheckTime);
			oppKingSq = moveGenerator->findKing(b, b.isWhiteTurn);
			givesCheck = moveGenerator->isSquareAttacked(b, oppKingSq, !b.isWhiteTurn);
		}

		bool isTTMove = false;
		if (haveTTMove &&
			move.from == ttMove.from &&
			move.to == ttMove.to &&
			move.moved == ttMove.moved)
		{
			PROFILE_INC(::Profiler::TTMoveTried);
			isTTMove = true;
		}

		bool isKiller =
			sameMoveIdentity(killerMoves[tablePly][0], move) ||
			sameMoveIdentity(killerMoves[tablePly][1], move);

		// Base depth for this move: one ply less
		int newDepth = depth - 1;
		int extension = 0;
		if (extensionCount < 1 &&
			((inCheck && depth <= 6) ||
			(givesCheck && (isCapture || move.wasPromotion || depth <= 4)))) {
			extension = 1;
		}
		if (extension != 0) {
			PROFILE_INC(::Profiler::CheckExtensions);
			newDepth++;
		}

		// ---------------------------------------
		// LATE MOVE PRUNING (LMP)

		PROFILE_INC(::Profiler::LmpAttempts);

		if (!isCapture &&
			!move.wasPromotion &&
			!givesCheck &&
			!inCheck &&
			!pvNode &&
			depth <= 3 &&
			moveIndex >= 6 &&
			searchedMoves > 0 &&
			!isTTMove &&
			!isKiller &&
			historyHeuristic[b.isWhiteTurn ? 0 : 1][move.from][move.to] < 12000)
		{
			PROFILE_INC(::Profiler::LmpPrunes);
			// LMP is forward pruning. Once a legal move is skipped, this node
			// must not be stored as a fully searched TT bound.
			selectivelyPruned = true;
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
		PROFILE_INC(::Profiler::LmrAttempts);
		if (newDepth > 0 &&
			depth >= 3 &&
			moveIndex >= 3 &&
			isQuietMove(move) &&
			!pvNode &&
			!inCheck &&
			!givesCheck &&
			!isTTMove &&
			!isKiller)
		{
			PROFILE_INC(::Profiler::LmrApplied);
			int R = 1;
			if (depth >= 6 && moveIndex >= 6) {
				++R;
			}
			if (historyHeuristic[b.isWhiteTurn ? 0 : 1][move.from][move.to] < 0) {
				++R;
			}
			R = std::min(R, newDepth - 1);

			// Reduced-depth search with null window
			int childScore = search(b, newDepth - R,
				-alpha - 1, -alpha,
				ply + 1, repHistory, false, extensionCount + extension);
			if (isSearchAborted(childScore)) {
				b.unmakeMove(move, u);
				return SEARCH_ABORTED;
			}

			eval = -childScore;

			// If it looks better than alpha, research at full depth
			if (eval > alpha) {
				PROFILE_INC(::Profiler::LmrResearches);
				childScore = search(b, newDepth,
					-beta, -alpha,
					ply + 1, repHistory, pvNode, extensionCount + extension);
				if (isSearchAborted(childScore)) {
					b.unmakeMove(move, u);
					return SEARCH_ABORTED;
				}

				eval = -childScore;
			}
		}
		else {
			int childScore;
			if (usePvs) {
				PROFILE_INC(::Profiler::PvsAttempts);
				// PVS: late moves get a null-window probe before a full re-search.
				childScore = search(b, newDepth,
					-alpha - 1, -alpha,
					ply + 1, repHistory, false, extensionCount + extension);
				if (isSearchAborted(childScore)) {
					b.unmakeMove(move, u);
					return SEARCH_ABORTED;
				}

				eval = -childScore;

				if (eval > alpha && eval < beta) {
					PROFILE_INC(::Profiler::PvsResearches);
					childScore = search(b, newDepth,
						-beta, -alpha,
						ply + 1, repHistory, pvNode, extensionCount + extension);
					if (isSearchAborted(childScore)) {
						b.unmakeMove(move, u);
						return SEARCH_ABORTED;
					}

					eval = -childScore;
				}
			}
			else {
				// First move, check nodes, and shallow nodes keep the full window.
				childScore = search(b, newDepth,
					-beta, -alpha,
					ply + 1, repHistory, pvNode && firstMove, extensionCount + extension);
				if (isSearchAborted(childScore)) {
					b.unmakeMove(move, u);
					return SEARCH_ABORTED;
				}

				eval = -childScore;
			}
		}
		
		firstMove = false;
		++searchedMoves;
#ifdef ENABLE_ENGINE_PROFILING
		if (isCapture) {
			PROFILE_INC(::Profiler::CaptureMovesSearched);
		}
		else if (isQuietMove(move)) {
			PROFILE_INC(::Profiler::QuietMovesSearched);
		}
#endif
		b.unmakeMove(move, u);

		// ------------------------------
		// Normal alpha-beta bookkeeping
		// ------------------------------
		if (eval > besteval) {
			besteval = eval;
			bestMoveLocal = move;
		}

		if (eval > alpha) {
			PROFILE_INC(::Profiler::AlphaRaises);
			alpha = eval;
		}

		// Alpha–beta cutoff
		if (alpha >= beta) {
			PROFILE_INC(::Profiler::BetaCutoffs);
#ifdef ENABLE_ENGINE_PROFILING
			if (searchedMoves == 1) {
				PROFILE_INC(::Profiler::FirstMoveBetaCutoffs);
			}
			if (isTTMove) {
				PROFILE_INC(::Profiler::TTMoveCutoffs);
			}
			PROFILE_INC(profileCutoffMoveIndexCounter(moveIndex));
#endif
			if (isQuietMove(move)) {
				// Quiet beta cutoffs update killer and side-aware history tables.
				if (!sameMoveIdentity(killerMoves[tablePly][0], move)) {
					killerMoves[tablePly][1] = killerMoves[tablePly][0];
					killerMoves[tablePly][0] = move;
				}
				int side = b.isWhiteTurn ? 0 : 1;
				historyHeuristic[side][move.from][move.to] =
					std::min(1000000, historyHeuristic[side][move.from][move.to] + depth * depth);
			}
			break;
		}

		moveIndex++;
	}


	if (stopSearch.load(std::memory_order_relaxed)) {
		return SEARCH_ABORTED;
	}

	if (selectivelyPruned) {
		return besteval;
	}

	// -----------------------------------
	// STORE TT ENTRY
	// Do not store partial results after a timeout/stop abort.
	// -----------------------------------
	if (tt == nullptr || ttSize == 0) {
		return besteval;
	}

	TTEntry& store = tt[key & ttMask];
	if (store.flag != TT_EMPTY && store.key != key && store.depth > depth + 2) {
		PROFILE_INC(::Profiler::TTKeptDueToDepth);
		return besteval;
	}

	PROFILE_INC(::Profiler::TTStores);
	if (store.flag != TT_EMPTY) {
		PROFILE_INC(::Profiler::TTOverwrites);
	}

	store.key = key;
	store.score = scoreToTT(besteval, ply);
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


int Engine::quiescence(board& b, int alpha, int beta, int ply, int qply,
	std::vector<uint64_t>& repHistory)
{
	totalNodes++;  // still count these as nodes
	PROFILE_INC(::Profiler::QSearchNodes);
#ifdef ENABLE_ENGINE_PROFILING
	const int profileQply = std::clamp(qply, 0, Profiler::MAX_PROFILE_PLY - 1);
	PROFILE_MAX(::Profiler::QSearchMaxPly, profileQply);
	PROFILE_ADD(::Profiler::QNodesByPly0 + profileQply, 1);
	if (qply >= 8) {
		PROFILE_INC(::Profiler::QNodesAtOrBeyondPly8);
	}
	if (qply >= 12) {
		PROFILE_INC(::Profiler::QNodesAtOrBeyondPly12);
	}
	if (qply >= 16) {
		PROFILE_INC(::Profiler::QNodesAtOrBeyondPly16);
	}
#endif
	constexpr int MaxQSearchPly = 24;
	constexpr int MaxQuietCheckQply = 4;
	constexpr int DeltaMargin = 150;

	if (shouldStop()) {
		return SEARCH_ABORTED;
	}

	if (b.halfmoveClock >= 100) {
		return 0;
	}

	uint64_t key = computeHash(b);
	for (auto it = repHistory.rbegin(); it != repHistory.rend(); ++it) {
		if (*it == key) {
			return 0;
		}
	}
	RepetitionFrame repetitionFrame(repHistory, key);

	int kingSq = -1;
	bool inCheck = false;
	{
		PROFILE_INC(::Profiler::InCheckCalls);
		PROFILE_TIMER(::Profiler::InCheckTime);
		kingSq = moveGenerator->findKing(b, b.isWhiteTurn);
		inCheck = kingSq != -1 &&
			moveGenerator->isSquareAttacked(b, kingSq, !b.isWhiteTurn);
	}
#ifdef ENABLE_ENGINE_PROFILING
	if (inCheck) {
		PROFILE_INC(::Profiler::QSearchInCheckCalls);
		PROFILE_INC(::Profiler::QInCheckEvasionNodes);
	}
	else {
		PROFILE_INC(::Profiler::QNonCheckNodes);
	}
#endif

	int standPat = 0;
	if (!inCheck) {
		if (qply >= MaxQSearchPly) {
			PROFILE_INC(::Profiler::QMaxPlyHits);
			PROFILE_INC(::Profiler::QMaxPlyLimitReturns);
			return evaluate(b);
		}

		// Stand-pat evaluation: assume the quiet position can be held.
		PROFILE_INC(::Profiler::QStandPatEvaluations);
		standPat = evaluate(b);

		// Fail-high: too good for the opponent.
		if (standPat >= beta) {
			PROFILE_INC(::Profiler::StandPatBetaCutoffs);
			return standPat;
		}

		if (standPat > alpha) {
			PROFILE_INC(::Profiler::StandPatAlphaRaises);
			alpha = standPat;
		}
	}

	MoveList moves;
	{
		PROFILE_MOVEGEN_CONTEXT(Qsearch);
		moveGenerator->generateQuiescenceMoves(b, moves);
	}
#ifdef ENABLE_ENGINE_PROFILING
	PROFILE_ADD(::Profiler::QGeneratedMovesTotal, moves.count);
	PROFILE_ADD(::Profiler::QCandidateMoves, moves.count);
	for (const Move& generated : moves) {
		if (generated.captured != EMPTY || generated.wasEnPassant) {
			PROFILE_INC(::Profiler::QCapturesGenerated);
		}
		if (generated.wasPromotion) {
			PROFILE_INC(::Profiler::QPromotionsGenerated);
		}
		if (!inCheck &&
			generated.captured == EMPTY &&
			!generated.wasEnPassant &&
			!generated.wasPromotion) {
			PROFILE_INC(::Profiler::QQuietChecksGenerated);
		}
	}
#endif

	if (inCheck && moves.count == 0) {
		return -MATE_SCORE + ply;
	}

	if (inCheck && qply >= MaxQSearchPly) {
		PROFILE_INC(::Profiler::QMaxPlyHits);
		PROFILE_INC(::Profiler::QMaxPlyLimitReturns);
		return evaluate(b);
	}

	int scores[MoveList::MAX_MOVES];
	Move qMoves[MoveList::MAX_MOVES];
	int searchCount = 0;

	if (inCheck) {
		// Stand-pat is illegal while in check; search every legal evasion.
		searchCount = moves.count;
		PROFILE_ADD(::Profiler::QMovesAfterPruning, searchCount);
		PROFILE_SEE_CONTEXT(QsearchMoveOrdering);
		for (int i = 0; i < searchCount; ++i) {
			qMoves[i] = moves.data()[i];
			scores[i] = scoreMove(qMoves[i], b, std::clamp(ply, 0, MAX_DEPTH - 1), false, Move());
		}
	}
	else {
		const bool alphaIsNormal = alpha > -MATE_THRESHOLD && alpha < MATE_THRESHOLD;
		for (auto& m : moves) {
			bool isCapture = (m.captured != EMPTY) || m.wasEnPassant;
			bool tactical = isCapture || m.wasPromotion;
			if (!tactical) {
				if (qply >= MaxQuietCheckQply) {
					PROFILE_INC(::Profiler::QMovesSkippedNotCaptureOrPromotionOrCheck);
					continue;
				}

				qMoves[searchCount] = m;
				{
					PROFILE_SEE_CONTEXT(QsearchMoveOrdering);
					scores[searchCount] = 250000 +
						std::clamp(scoreMove(m, b, std::clamp(ply, 0, MAX_DEPTH - 1), false, Move()),
							-50000, 50000);
				}
				++searchCount;
				PROFILE_INC(::Profiler::QMovesAfterPruning);
				continue;
			}

			int gain = pieceValueCp(m.captured) + promotionGainCp(m);
			if (!m.wasPromotion && alphaIsNormal) {
				PROFILE_INC(::Profiler::QDeltaPruneAttempts);
				if (standPat + gain + DeltaMargin <= alpha) {
					PROFILE_INC(::Profiler::QDeltaPrunes);
					continue;
				}
			}

			PROFILE_INC(::Profiler::QSeePruneAttempts);
			int see;
			{
				PROFILE_SEE_CONTEXT(QsearchPruning);
				see = approximateSee(b, moveGenerator, m);
			}
			if (!m.wasPromotion && see < -120) {
				PROFILE_INC(::Profiler::QSeePrunes);
				continue;
			}

			qMoves[searchCount] = m;
			{
				if (m.wasPromotion) {
					scores[searchCount] = 850000 + pieceValueCp(m.promotedTo) +
						(capturedPieceForMove(m) != EMPTY ? pieceValueCp(capturedPieceForMove(m)) : 0);
				}
				else {
					scores[searchCount] = scoreCaptureWithSee(m, see);
				}
			}
			++searchCount;
			PROFILE_INC(::Profiler::QMovesAfterPruning);
		}

		if (searchCount == 0) {
			// No captures ? position is quiet, return stand-pat eval
			return alpha;
		}
	}

	for (int orderedIndex = 0; orderedIndex < searchCount; ++orderedIndex) {
		int best = orderedIndex;
		for (int i = orderedIndex + 1; i < searchCount; ++i) {
			if (scores[i] > scores[best]) {
				best = i;
			}
		}
		if (best != orderedIndex) {
			std::swap(scores[orderedIndex], scores[best]);
			std::swap(qMoves[orderedIndex], qMoves[best]);
		}
		const Move& m = qMoves[orderedIndex];

		PROFILE_INC(::Profiler::QMovesSearched);
		PROFILE_INC(::Profiler::QMovesActuallySearched);
#ifdef ENABLE_ENGINE_PROFILING
		if (m.captured != EMPTY || m.wasEnPassant) {
			PROFILE_INC(::Profiler::QCapturesSearched);
		}
		if (m.wasPromotion) {
			PROFILE_INC(::Profiler::QPromotionsSearched);
		}
		if (!inCheck &&
			m.captured == EMPTY &&
			!m.wasEnPassant &&
			!m.wasPromotion) {
			PROFILE_INC(::Profiler::QQuietChecksSearched);
		}
#endif
		Unmove u = b.makeMove(m);
		int childScore = quiescence(b, -beta, -alpha, ply + 1, qply + 1, repHistory);
		if (isSearchAborted(childScore)) {
			b.unmakeMove(m, u);
			return SEARCH_ABORTED;
		}

		int score = -childScore;

		b.unmakeMove(m, u);

		if (score >= beta) {
			PROFILE_INC(::Profiler::QBetaCutoffs);
			return score;
		}

		if (score > alpha) {
			PROFILE_INC(::Profiler::QAlphaRaises);
			alpha = score;
		}
	}

	return alpha;

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
