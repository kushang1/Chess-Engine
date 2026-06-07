#define _CRT_SECURE_NO_WARNINGS


#include "Engine.h"
#include "Profiler.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <new>
#include <random>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#endif


static const int MATE_SCORE = 2000000000;       // same as your INF
static const int MATE_THRESHOLD = MATE_SCORE - 10000;

static const int INF = 2000000000;
static const int SEARCH_ABORTED = -2000000001;
static const int TT_SCORE_LIMIT = 30000;
static const int TT_MATE_SCORE = 32000;
static const int TT_MATE_THRESHOLD = 31000;

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

int promotionKind(Piece piece)
{
	static constexpr int kinds[13] = {
		0, 1, 2, 0, 4, 0, 3,
		   1, 2, 0, 4, 0, 3
	};
	return kinds[static_cast<int>(piece)];
}

bool sameMoveIdentity(const Move& lhs, const Move& rhs)
{
	if (lhs.from != rhs.from || lhs.to != rhs.to) {
		return false;
	}
	if (lhs.wasPromotion != rhs.wasPromotion) {
		return false;
	}
	return !lhs.wasPromotion ||
		promotionKind(lhs.promotedTo) == promotionKind(rhs.promotedTo);
}

bool isRepeatedPosition(const std::vector<uint64_t>& history, uint64_t key)
{
	if (history.size() < 2) {
		return false;
	}

	for (std::size_t i = history.size() - 2;; i -= 2) {
		if (history[i] == key) {
			return true;
		}
		if (i < 2) {
			break;
		}
	}
	return false;
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
	long long nodes, std::chrono::steady_clock::time_point searchStart)
{
	auto now = std::chrono::steady_clock::now();
	long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - searchStart).count();
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

static_assert(sizeof(SharedTranspositionTable::Entry) == 16,
	"TT entry must stay 16 bytes");
static_assert(sizeof(SharedTranspositionTable::Cluster) == 64,
	"TT cluster must stay one cache line");
static_assert(alignof(SharedTranspositionTable::Cluster) == 64,
	"TT cluster must be cache-line aligned");

SharedTranspositionTable::SharedTranspositionTable()
{
	resize(DefaultHashMb);
}

SharedTranspositionTable::~SharedTranspositionTable()
{
	delete[] table;
}

void SharedTranspositionTable::resize(int megabytes)
{
	megabytes = std::clamp(megabytes, MinHashMb, MaxHashMb);

	const uint64_t bytes = static_cast<uint64_t>(megabytes) * 1024ULL * 1024ULL;
	uint64_t clusters = floorPowerOfTwo(bytes / sizeof(Cluster));
	if (clusters == 0) {
		clusters = 1;
	}

	Cluster* newTable = new (std::nothrow) Cluster[clusters]();
	if (newTable == nullptr) {
		return;
	}

	delete[] table;
	table = newTable;
	count = clusters;
	mask = clusters - 1;
	used.store(0, std::memory_order_relaxed);
	generation.store(0, std::memory_order_relaxed);
}

void SharedTranspositionTable::clear()
{
	if (table == nullptr || count == 0) {
		return;
	}

	for (uint64_t cluster = 0; cluster < count; ++cluster) {
		for (Entry& entry : table[cluster].entries) {
			entry.data.store(0, std::memory_order_relaxed);
			entry.keyXorData.store(0, std::memory_order_relaxed);
		}
	}
	used.store(0, std::memory_order_relaxed);
}

uint8_t SharedTranspositionTable::newSearch()
{
	const unsigned previous = generation.fetch_add(1, std::memory_order_acq_rel);
	return static_cast<uint8_t>((previous + 1U) & 0x3FU);
}

SharedTranspositionTable::Cluster* SharedTranspositionTable::clusters() const
{
	return table;
}

uint64_t SharedTranspositionTable::clusterCount() const
{
	return count;
}

uint64_t SharedTranspositionTable::clusterMask() const
{
	return mask;
}

uint64_t SharedTranspositionTable::usedEntries() const
{
	return used.load(std::memory_order_relaxed);
}

void SharedTranspositionTable::noteNewEntry()
{
	used.fetch_add(1, std::memory_order_relaxed);
}

void SharedSearchControl::start(int milliseconds, long long nodeLimitValue)
{
	timeLimitMs = std::max(1, milliseconds);
	nodeLimit = std::max(0LL, nodeLimitValue);
	nodes.store(0, std::memory_order_relaxed);
	stop.store(false, std::memory_order_release);
	started = std::chrono::steady_clock::now();
}

void SharedSearchControl::clearStop()
{
	stop.store(false, std::memory_order_release);
}

void SharedSearchControl::requestStop()
{
	stop.store(true, std::memory_order_release);
}

bool SharedSearchControl::isStopRequested() const
{
	return stop.load(std::memory_order_acquire);
}

bool SharedSearchControl::shouldStop()
{
	if (stop.load(std::memory_order_acquire)) {
		return true;
	}

	if (nodeLimit > 0 && nodes.load(std::memory_order_relaxed) >= nodeLimit) {
		stop.store(true, std::memory_order_release);
		return true;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - started).count();
	if (elapsed >= timeLimitMs) {
		stop.store(true, std::memory_order_release);
		return true;
	}

	return false;
}

long long SharedSearchControl::countNode()
{
	const long long value = nodes.fetch_add(1, std::memory_order_relaxed) + 1;
	if (nodeLimit > 0 && value >= nodeLimit) {
		stop.store(true, std::memory_order_release);
	}
	return value;
}

long long SharedSearchControl::nodesSearched() const
{
	return nodes.load(std::memory_order_relaxed);
}

std::chrono::steady_clock::time_point SharedSearchControl::startTime() const
{
	return started;
}

Engine::Engine() : tt(std::make_shared<SharedTranspositionTable>()), stopSearch(false) {
	for (int ply = 0; ply < MAX_DEPTH; ++ply)
		for (int slot = 0; slot < 2; ++slot)
			killerMoves[ply][slot] = Move();  // invalid move

	memset(historyHeuristic, 0, sizeof(historyHeuristic));

	initializeExternalData();

}


Engine::~Engine() {
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
	totalNodes = 0;
	leafNodes = 0;
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

void Engine::setSharedTranspositionTable(const std::shared_ptr<SharedTranspositionTable>& table)
{
	tt = table;
}

std::shared_ptr<SharedTranspositionTable> Engine::sharedTranspositionTable() const
{
	return tt;
}

void Engine::setSharedSearchControl(const std::shared_ptr<SharedSearchControl>& control)
{
	sharedControl = control;
}

void Engine::setWorkerId(int id)
{
	workerId = std::max(0, id);
}

void Engine::setEmitUciInfo(bool enabled)
{
	emitSearchInfo = enabled;
}

void Engine::resizeTranspositionTable(int megabytes)
{
	if (!tt) {
		tt = std::make_shared<SharedTranspositionTable>();
	}
	tt->resize(megabytes);
	currentGeneration = 0;
}

void Engine::clearStop()
{
	stopSearch.store(false, std::memory_order_relaxed);
	if (sharedControl) {
		sharedControl->clearStop();
	}
}

void Engine::requestStop()
{
	stopSearch.store(true, std::memory_order_relaxed);
	if (sharedControl) {
		sharedControl->requestStop();
	}
}

long long Engine::nodesSearched() const
{
	return totalNodes;
}

long long Engine::leafNodesSearched() const
{
	return leafNodes;
}

int Engine::lastSearchScore() const
{
	return lastScore;
}

int Engine::lastSearchDepth() const
{
	return lastDepth;
}

void Engine::clearTT()
{
	if (!tt) {
		return;
	}
	tt->clear();
}

void Engine::newSearch()
{
	currentGeneration = tt ? tt->newSearch() :
		static_cast<uint8_t>((currentGeneration + 1) & 0x3F);
}

uint8_t Engine::entryGeneration(const DecodedTTEntry& entry)
{
	return static_cast<uint8_t>(entry.generationBound >> 2);
}

Engine::TTBound Engine::entryBound(const DecodedTTEntry& entry)
{
	return static_cast<TTBound>(entry.generationBound & 0x03);
}

uint8_t Engine::makeGenerationBound(uint8_t generation, TTBound bound)
{
	return static_cast<uint8_t>(
		((generation & 0x3F) << 2) | (static_cast<uint8_t>(bound) & 0x03));
}

uint64_t Engine::packTTEntry(const DecodedTTEntry& entry)
{
	return static_cast<uint64_t>(entry.move16) |
		(static_cast<uint64_t>(static_cast<uint16_t>(entry.score)) << 16) |
		(static_cast<uint64_t>(static_cast<uint16_t>(entry.staticEval)) << 32) |
		(static_cast<uint64_t>(entry.depth) << 48) |
		(static_cast<uint64_t>(entry.generationBound) << 56);
}

Engine::DecodedTTEntry Engine::unpackTTEntry(uint64_t data)
{
	DecodedTTEntry entry;
	entry.move16 = static_cast<uint16_t>(data & 0xFFFFULL);
	entry.score = static_cast<int16_t>((data >> 16) & 0xFFFFULL);
	entry.staticEval = static_cast<int16_t>((data >> 32) & 0xFFFFULL);
	entry.depth = static_cast<uint8_t>((data >> 48) & 0xFFULL);
	entry.generationBound = static_cast<uint8_t>((data >> 56) & 0xFFULL);
	return entry;
}

uint8_t Engine::generationAge(uint8_t entryGen) const
{
	return static_cast<uint8_t>((currentGeneration - entryGen) & 0x3F);
}

int Engine::replacementScore(const DecodedTTEntry& entry) const
{
	const int exactBonus = entryBound(entry) == TTBound::Exact ? 8 : 0;
	const int agePenalty = static_cast<int>(generationAge(entryGeneration(entry))) * 4;
	return static_cast<int>(entry.depth) + exactBonus - agePenalty;
}

uint16_t Engine::packMove(const Move& move) const
{
	if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
		return 0;
	}

	uint16_t flags = 0;
	if (move.wasPromotion) {
		flags = static_cast<uint16_t>(promotionKind(move.promotedTo));
	}

	return static_cast<uint16_t>(
		(static_cast<uint16_t>(move.from) & 0x3F) |
		((static_cast<uint16_t>(move.to) & 0x3F) << 6) |
		((flags & 0x0F) << 12));
}

Move Engine::unpackMove(uint16_t packed) const
{
	if (packed == 0) {
		return Move();
	}

	Move move;
	move.from = packed & 0x3F;
	move.to = (packed >> 6) & 0x3F;

	const uint16_t flags = (packed >> 12) & 0x0F;
	if (flags != 0) {
		move.wasPromotion = true;
		switch (flags) {
		case 2: move.promotedTo = WR; break;
		case 3: move.promotedTo = WB; break;
		case 4: move.promotedTo = WN; break;
		case 1:
		default:
			move.promotedTo = WQ;
			break;
		}
	}

	return move;
}

int16_t Engine::packStaticEval(int staticEval) const
{
	if (staticEval == TT_NO_STATIC_EVAL) {
		return static_cast<int16_t>(TT_NO_STATIC_EVAL);
	}
	return static_cast<int16_t>(
		std::clamp(staticEval, -TT_SCORE_LIMIT, TT_SCORE_LIMIT));
}

int16_t Engine::scoreToTT(int score, int ply) const
{
	if (score >= MATE_THRESHOLD) {
		const int normalized = score + ply;
		const int distance = std::clamp(MATE_SCORE - normalized, 0, 1000);
		return static_cast<int16_t>(TT_MATE_SCORE - distance);
	}
	if (score <= -MATE_THRESHOLD) {
		const int normalized = score - ply;
		const int distance = std::clamp(MATE_SCORE + normalized, 0, 1000);
		return static_cast<int16_t>(-TT_MATE_SCORE + distance);
	}
	return static_cast<int16_t>(std::clamp(score, -TT_SCORE_LIMIT, TT_SCORE_LIMIT));
}

int Engine::scoreFromTT(int16_t score, int ply) const
{
	if (score >= TT_MATE_THRESHOLD) {
		const int distance = TT_MATE_SCORE - score;
		return MATE_SCORE - distance - ply;
	}
	if (score <= -TT_MATE_THRESHOLD) {
		const int distance = score + TT_MATE_SCORE;
		return -MATE_SCORE + distance + ply;
	}
	return static_cast<int>(score);
}

Engine::TTProbeResult Engine::probeTT(uint64_t key, int ply)
{
	TTProbeResult result;
	if (!tt || tt->clusters() == nullptr || tt->clusterCount() == 0) {
		return result;
	}

	PROFILE_INC(::Profiler::TTProbes);
	SharedTranspositionTable::Cluster& cluster =
		tt->clusters()[key & tt->clusterMask()];
	bool occupied = false;

	for (int slot = 0; slot < 4; ++slot) {
		SharedTranspositionTable::Entry& slotEntry = cluster.entries[slot];
		const uint64_t keyXorData =
			slotEntry.keyXorData.load(std::memory_order_acquire);
		const uint64_t data =
			slotEntry.data.load(std::memory_order_acquire);

		if (data == 0) {
			continue;
		}

		occupied = true;
		if ((keyXorData ^ data) != key) {
			continue;
		}

		const DecodedTTEntry entry = unpackTTEntry(data);
		const TTBound bound = entryBound(entry);
		if (bound == TTBound::Empty) {
			continue;
		}

		PROFILE_INC(::Profiler::TTHits);
#ifdef ENABLE_ENGINE_PROFILING
		PROFILE_INC(static_cast<::Profiler::CounterId>(
			static_cast<int>(::Profiler::TTSlotHit0) + slot));
#endif
		if (bound == TTBound::Exact) {
			PROFILE_INC(::Profiler::TTExactHits);
		}
		else if (bound == TTBound::Lower) {
			PROFILE_INC(::Profiler::TTLowerHits);
			PROFILE_INC(::Profiler::TTBetaHits);
		}
		else if (bound == TTBound::Upper) {
			PROFILE_INC(::Profiler::TTUpperHits);
			PROFILE_INC(::Profiler::TTAlphaHits);
		}

		result.hit = true;
		result.score = scoreFromTT(entry.score, ply);
		result.depth = static_cast<int>(entry.depth);
		result.bound = bound;
		result.move = unpackMove(entry.move16);
		result.hasMove = entry.move16 != 0 &&
			result.move.from >= 0 && result.move.to >= 0;
		result.hasStaticEval = entry.staticEval != TT_NO_STATIC_EVAL;
		if (result.hasStaticEval) {
			result.staticEval = static_cast<int>(entry.staticEval);
		}
		return result;
	}

	PROFILE_INC(::Profiler::TTMisses);
	if (occupied) {
		PROFILE_INC(::Profiler::TTCollisionMisses);
	}
	return result;
}

void Engine::storeTT(uint64_t key, int depth, int score, TTBound bound,
	const Move& bestMove, int ply, int staticEval)
{
	if (!tt || tt->clusters() == nullptr || tt->clusterCount() == 0 ||
		bound == TTBound::Empty) {
		return;
	}

	SharedTranspositionTable::Cluster& cluster =
		tt->clusters()[key & tt->clusterMask()];
	const uint16_t packedMove = packMove(bestMove);

	SharedTranspositionTable::Entry* target = nullptr;
	SharedTranspositionTable::Entry* empty = nullptr;
	SharedTranspositionTable::Entry* replacement = &cluster.entries[0];
	DecodedTTEntry replacementEntry =
		unpackTTEntry(replacement->data.load(std::memory_order_relaxed));
	int replacementValue = replacementScore(replacementEntry);
	DecodedTTEntry targetEntry;
	bool targetHadEntry = false;
	bool targetSameKey = false;

	for (SharedTranspositionTable::Entry& slotEntry : cluster.entries) {
		const uint64_t keyXorData =
			slotEntry.keyXorData.load(std::memory_order_acquire);
		const uint64_t data =
			slotEntry.data.load(std::memory_order_acquire);
		DecodedTTEntry entry = unpackTTEntry(data);
		const TTBound entryType = data == 0 ? TTBound::Empty : entryBound(entry);
		const bool sameKey = data != 0 && ((keyXorData ^ data) == key);
		if (entryType != TTBound::Empty && sameKey) {
			target = &slotEntry;
			targetEntry = entry;
			targetHadEntry = true;
			targetSameKey = sameKey;
			break;
		}
		if (entryType == TTBound::Empty && empty == nullptr) {
			empty = &slotEntry;
		}
		const int value = replacementScore(entry);
		if (value < replacementValue) {
			replacementValue = value;
			replacement = &slotEntry;
			replacementEntry = entry;
		}
	}

	if (target == nullptr) {
		target = empty != nullptr ? empty : replacement;
		if (empty == nullptr) {
			targetEntry = replacementEntry;
			const uint64_t oldData = target->data.load(std::memory_order_acquire);
			const uint64_t oldKeyXorData =
				target->keyXorData.load(std::memory_order_acquire);
			targetHadEntry = oldData != 0;
			targetSameKey = oldData != 0 && ((oldKeyXorData ^ oldData) == key);
		}
		else {
			targetEntry = DecodedTTEntry{};
			targetHadEntry = false;
			targetSameKey = false;
		}
	}

	if (target != nullptr &&
		targetHadEntry &&
		targetSameKey &&
		entryBound(targetEntry) == TTBound::Exact &&
		bound != TTBound::Exact &&
		static_cast<int>(targetEntry.depth) > depth + 2) {
		if (packedMove != 0) {
			targetEntry.move16 = packedMove;
		}
		if (staticEval != TT_NO_STATIC_EVAL) {
			targetEntry.staticEval = packStaticEval(staticEval);
		}
		targetEntry.generationBound =
			makeGenerationBound(currentGeneration, entryBound(targetEntry));
		const uint64_t updatedData = packTTEntry(targetEntry);
		target->data.store(updatedData, std::memory_order_release);
		target->keyXorData.store(key ^ updatedData, std::memory_order_release);
		return;
	}

	const bool replacingOccupied = targetHadEntry &&
		entryBound(targetEntry) != TTBound::Empty;
	const bool replacingDifferent = replacingOccupied &&
		(!targetSameKey || targetEntry.move16 != packedMove);

	PROFILE_INC(::Profiler::TTStores);
	if (replacingOccupied) {
		PROFILE_INC(::Profiler::TTOverwrites);
		if (replacingDifferent) {
			if (generationAge(entryGeneration(targetEntry)) != 0) {
				PROFILE_INC(::Profiler::TTReplacedByAge);
			}
			else {
				PROFILE_INC(::Profiler::TTReplacedByDepth);
			}
		}
	}
	else {
		tt->noteNewEntry();
	}

	DecodedTTEntry newEntry;
	newEntry.move16 = (packedMove != 0 || !targetSameKey)
		? packedMove
		: targetEntry.move16;
	newEntry.score = scoreToTT(score, ply);
	newEntry.staticEval = packStaticEval(staticEval);
	newEntry.depth = static_cast<uint8_t>(std::clamp(depth, 0, 255));
	newEntry.generationBound = makeGenerationBound(currentGeneration, bound);

	const uint64_t data = packTTEntry(newEntry);
	target->data.store(data, std::memory_order_release);
	target->keyXorData.store(key ^ data, std::memory_order_release);
}

int Engine::ttHashfullPermille() const
{
	if (!tt || tt->clusterCount() == 0) {
		return 0;
	}

	const uint64_t capacity = tt->clusterCount() * 4ULL;
	return static_cast<int>(std::min<uint64_t>(
		1000ULL, (tt->usedEntries() * 1000ULL) / capacity));
}

bool Engine::shouldStop()
{
	if (stopSearch.load(std::memory_order_relaxed)) {
		return true;
	}

	if (sharedControl) {
		if (sharedControl->isStopRequested()) {
			stopSearch.store(true, std::memory_order_relaxed);
			return true;
		}
		if ((totalNodes & 0x0FFF) != 0) {
			return false;
		}
		if (sharedControl->shouldStop()) {
			stopSearch.store(true, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	if (nodeLimit > 0 &&
		totalNodes >= nodeLimit) {
		stopSearch.store(true, std::memory_order_relaxed);
		return true;
	}

	if ((totalNodes & 0x0FFF) == 0) {
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

long long Engine::countNode()
{
	++totalNodes;
	if (sharedControl) {
		return sharedControl->countNode();
	}
	return totalNodes;
}

long long Engine::reportedNodeCount() const
{
	return sharedControl ? sharedControl->nodesSearched() : totalNodes;
}

static const int pieceValueSimple[13] = {
	0, 9, 5, 1, 3, 100, 3,
	   9, 5, 1, 3, 100, 3
};

static const int pieceValueCpTable[13] = {
	0, 900, 500, 100, 320, 20000, 330,
	   900, 500, 100, 320, 20000, 330
};

static int pieceValueCp(Piece p)
{
	return pieceValueCpTable[static_cast<int>(p)];
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

namespace {

constexpr int EvalMaxPhase = 24;

struct EvalScore {
	int mg = 0;
	int eg = 0;
};

inline EvalScore& operator+=(EvalScore& lhs, const EvalScore& rhs)
{
	lhs.mg += rhs.mg;
	lhs.eg += rhs.eg;
	return lhs;
}

inline EvalScore operator-(const EvalScore& score)
{
	return { -score.mg, -score.eg };
}

struct EvalBreakdown {
	EvalScore materialPst;
	EvalScore pawns;
	EvalScore mobility;
	EvalScore rooks;
	EvalScore bishopPair;
	EvalScore kingSafety;
	EvalScore kingActivity;
	EvalScore total;
	int phase = 0;
	int whiteAttackUnits = 0;
	int blackAttackUnits = 0;
	int whiteKingZoneHits = 0;
	int blackKingZoneHits = 0;
	int finalWhiteScore = 0;
	int finalSideScore = 0;
};

struct AttackEval {
	EvalScore mobility;
	EvalScore rooks;
	int whiteAttackUnits = 0;
	int blackAttackUnits = 0;
	int whiteKingZoneHits = 0;
	int blackKingZoneHits = 0;
};

struct EvalContext {
	const board& b;
	Bitboard occ = 0;
	Bitboard whiteOcc = 0;
	Bitboard blackOcc = 0;
	Bitboard whitePawns = 0;
	Bitboard blackPawns = 0;
	uint8_t whitePawnFiles = 0;
	uint8_t blackPawnFiles = 0;
	int whiteKing = -1;
	int blackKing = -1;
	Bitboard whiteKingZone = 0;
	Bitboard blackKingZone = 0;
	int phase = 0;
};

constexpr Bitboard bitConst(int sq)
{
	return 1ULL << sq;
}

constexpr Bitboard makeFileMaskConst(int file)
{
	Bitboard mask = 0;
	for (int row = 0; row < 8; ++row) {
		mask |= bitConst((row << 3) | file);
	}
	return mask;
}

constexpr std::array<Bitboard, 8> makeFileMasks()
{
	std::array<Bitboard, 8> masks{};
	for (int file = 0; file < 8; ++file) {
		masks[file] = makeFileMaskConst(file);
	}
	return masks;
}

constexpr std::array<Bitboard, 8> makeAdjacentFileMasks()
{
	std::array<Bitboard, 8> masks{};
	for (int file = 0; file < 8; ++file) {
		if (file > 0) {
			masks[file] |= makeFileMaskConst(file - 1);
		}
		if (file < 7) {
			masks[file] |= makeFileMaskConst(file + 1);
		}
	}
	return masks;
}

constexpr std::array<Bitboard, 8> makeRankMasks()
{
	std::array<Bitboard, 8> masks{};
	for (int row = 0; row < 8; ++row) {
		masks[row] = 0xFFULL << (row * 8);
	}
	return masks;
}

constexpr std::array<uint8_t, 8> makeKingFileBits()
{
	std::array<uint8_t, 8> bits{};
	for (int file = 0; file < 8; ++file) {
		uint8_t mask = static_cast<uint8_t>(1u << file);
		if (file > 0) {
			mask |= static_cast<uint8_t>(1u << (file - 1));
		}
		if (file < 7) {
			mask |= static_cast<uint8_t>(1u << (file + 1));
		}
		bits[file] = mask;
	}
	return bits;
}

constexpr std::array<std::array<Bitboard, 64>, 2> makeKingZoneMasks()
{
	std::array<std::array<Bitboard, 64>, 2> masks{};
	for (int color = 0; color < 2; ++color) {
		const int forward = color == Bitboards::WHITE ? -1 : 1;
		for (int sq = 0; sq < 64; ++sq) {
			const int row = sq >> 3;
			const int file = sq & 7;
			Bitboard zone = bitConst(sq);
			for (int dr = -1; dr <= 1; ++dr) {
				for (int df = -1; df <= 1; ++df) {
					const int nr = row + dr;
					const int nf = file + df;
					if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
						zone |= bitConst((nr << 3) | nf);
					}
				}
			}
			for (int df = -1; df <= 1; ++df) {
				const int nr = row + 2 * forward;
				const int nf = file + df;
				if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
					zone |= bitConst((nr << 3) | nf);
				}
			}
			masks[color][sq] = zone;
		}
	}
	return masks;
}

constexpr std::array<std::array<Bitboard, 64>, 2> makePawnShieldMasks()
{
	std::array<std::array<Bitboard, 64>, 2> masks{};
	for (int color = 0; color < 2; ++color) {
		const int forward = color == Bitboards::WHITE ? -1 : 1;
		for (int sq = 0; sq < 64; ++sq) {
			const int row = sq >> 3;
			const int file = sq & 7;
			const int nr = row + forward;
			Bitboard shield = 0;
			if (nr >= 0 && nr < 8) {
				for (int df = -1; df <= 1; ++df) {
					const int nf = file + df;
					if (nf >= 0 && nf < 8) {
						shield |= bitConst((nr << 3) | nf);
					}
				}
			}
			masks[color][sq] = shield;
		}
	}
	return masks;
}

constexpr std::array<std::array<Bitboard, 64>, 2> makePawnShieldCenterMasks()
{
	std::array<std::array<Bitboard, 64>, 2> masks{};
	for (int color = 0; color < 2; ++color) {
		const int forward = color == Bitboards::WHITE ? -1 : 1;
		for (int sq = 0; sq < 64; ++sq) {
			const int row = sq >> 3;
			const int file = sq & 7;
			const int nr = row + forward;
			masks[color][sq] = (nr >= 0 && nr < 8) ? bitConst((nr << 3) | file) : 0;
		}
	}
	return masks;
}

constexpr std::array<Bitboard, 8> FileMasks = makeFileMasks();
constexpr std::array<Bitboard, 8> AdjacentFileMasks = makeAdjacentFileMasks();
constexpr std::array<Bitboard, 8> RankMasks = makeRankMasks();
constexpr std::array<uint8_t, 8> KingFileBits = makeKingFileBits();
constexpr std::array<std::array<Bitboard, 64>, 2> KingZoneMasks = makeKingZoneMasks();
constexpr std::array<std::array<Bitboard, 64>, 2> PawnShieldMasks = makePawnShieldMasks();
constexpr std::array<std::array<Bitboard, 64>, 2> PawnShieldCenterMasks = makePawnShieldCenterMasks();

static const int kingEgPST[64] = {
	-50,-35,-25,-20,-20,-25,-35,-50,
	-35,-20,-10, -5, -5,-10,-20,-35,
	-25,-10,  5, 12, 12,  5,-10,-25,
	-20, -5, 12, 25, 25, 12, -5,-20,
	-20, -5, 12, 25, 25, 12, -5,-20,
	-25,-10,  5, 12, 12,  5,-10,-25,
	-35,-20,-10, -5, -5,-10,-20,-35,
	-50,-35,-25,-20,-20,-25,-35,-50
};

static const EvalScore passedPawnBonus[8] = {
	{ 0, 0 }, { 8, 18 }, { 14, 28 }, { 24, 48 },
	{ 40, 78 }, { 65, 125 }, { 105, 210 }, { 0, 0 }
};

static inline int blendScore(const EvalScore& score, int phase)
{
	return (score.mg * phase + score.eg * (EvalMaxPhase - phase)) / EvalMaxPhase;
}

static inline int relativeSquare(bool white, int sq)
{
	return white ? (sq ^ 56) : sq;
}

static inline int popcountFiles(uint8_t files)
{
	return Bitboards::popcount(static_cast<Bitboard>(files));
}

static inline uint8_t pawnFilesMask(Bitboard pawns)
{
	uint8_t files = 0;
	for (int file = 0; file < 8; ++file) {
		if ((pawns & FileMasks[file]) != 0) {
			files |= static_cast<uint8_t>(1u << file);
		}
	}
	return files;
}

static inline Bitboard eastOne(Bitboard bb)
{
	return (bb & ~FileMasks[7]) << 1;
}

static inline Bitboard westOne(Bitboard bb)
{
	return (bb & ~FileMasks[0]) >> 1;
}

static inline Bitboard northFillExclusive(Bitboard bb)
{
	bb |= bb >> 8;
	bb |= bb >> 16;
	bb |= bb >> 32;
	return bb >> 8;
}

static inline Bitboard southFillExclusive(Bitboard bb)
{
	bb |= bb << 8;
	bb |= bb << 16;
	bb |= bb << 32;
	return bb << 8;
}

static inline Bitboard whitePawnAttackMap(Bitboard pawns)
{
	return ((pawns & ~FileMasks[0]) >> 9) | ((pawns & ~FileMasks[7]) >> 7);
}

static inline Bitboard blackPawnAttackMap(Bitboard pawns)
{
	return ((pawns & ~FileMasks[0]) << 7) | ((pawns & ~FileMasks[7]) << 9);
}

static inline EvalScore materialForPiece(Piece p)
{
	switch (p) {
	case WP: case BP: return { 82, 94 };
	case WN: case BN: return { 337, 281 };
	case WB: case BB: return { 365, 297 };
	case WR: case BR: return { 477, 512 };
	case WQ: case BQ: return { 1025, 936 };
	default: return { 0, 0 };
	}
}

static inline EvalScore pstForPiece(Piece p, int sq)
{
	const bool white = isWhitePieceFast(p);
	const int psq = relativeSquare(white, sq);
	switch (p) {
	case WP: case BP: return { pawnPST[psq], pawnPST[psq] / 2 };
	case WN: case BN: return { knightPST[psq], knightPST[psq] / 2 };
	case WB: case BB: return { bishopPST[psq], bishopPST[psq] / 2 };
	case WR: case BR: return { rookPST[psq], rookPST[psq] / 3 };
	case WQ: case BQ: return { queenPST[psq], queenPST[psq] / 3 };
	case WK: case BK: return { kingPST[psq], 0 };
	default: return { 0, 0 };
	}
}

static std::array<std::array<EvalScore, 64>, 13> makePieceSquareScores()
{
	std::array<std::array<EvalScore, 64>, 13> scores{};
	constexpr Piece pieces[] = { WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK };
	for (Piece p : pieces) {
		const bool white = isWhitePieceFast(p);
		for (int sq = 0; sq < 64; ++sq) {
			EvalScore term = materialForPiece(p);
			term += pstForPiece(p, sq);
			scores[p][sq] = white ? term : -term;
		}
	}
	return scores;
}

static const std::array<std::array<EvalScore, 64>, 13> PieceSquareScores = makePieceSquareScores();

static inline int computePhase(const board& b)
{
	int phase = 0;
	phase += b.countPieces(WN) + b.countPieces(BN);
	phase += b.countPieces(WB) + b.countPieces(BB);
	phase += 2 * (b.countPieces(WR) + b.countPieces(BR));
	phase += 4 * (b.countPieces(WQ) + b.countPieces(BQ));
	return std::min(phase, EvalMaxPhase);
}

static EvalContext makeEvalContext(const board& b)
{
	EvalContext ctx{ b };
	ctx.occ = b.occupied;
	ctx.whiteOcc = b.whiteOcc;
	ctx.blackOcc = b.blackOcc;
	ctx.whitePawns = b.pieces(WP);
	ctx.blackPawns = b.pieces(BP);
	ctx.whitePawnFiles = pawnFilesMask(ctx.whitePawns);
	ctx.blackPawnFiles = pawnFilesMask(ctx.blackPawns);
	ctx.whiteKing = b.kingSquare(true);
	ctx.blackKing = b.kingSquare(false);
	ctx.whiteKingZone = ctx.whiteKing >= 0 ? KingZoneMasks[Bitboards::WHITE][ctx.whiteKing] : 0;
	ctx.blackKingZone = ctx.blackKing >= 0 ? KingZoneMasks[Bitboards::BLACK][ctx.blackKing] : 0;
	ctx.phase = computePhase(b);
	return ctx;
}

static EvalScore evaluateMaterialPst(const EvalContext& ctx)
{
	PROFILE_TIMER(::Profiler::EvalMaterialPstTime);
	constexpr Piece pieces[] = { WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK };
	EvalScore score;
	for (Piece p : pieces) {
		Bitboard bb = ctx.b.pieces(p);
		while (bb != 0) {
			const int sq = Bitboards::poplsb(bb);
			score += PieceSquareScores[p][sq];
		}
	}
	return score;
}

static EvalScore evaluatePawns(const EvalContext& ctx)
{
	PROFILE_TIMER(::Profiler::EvalPawnStructureTime);
	EvalScore score;
	Bitboard whitePassed = 0;
	Bitboard blackPassed = 0;
	Bitboard whiteConnectedPassed = 0;
	Bitboard blackConnectedPassed = 0;

	{
		PROFILE_TIMER(::Profiler::EvalPassedPawnTime);
		const Bitboard blackStops = southFillExclusive(ctx.blackPawns);
		const Bitboard whiteStops = northFillExclusive(ctx.whitePawns);
		const Bitboard whiteBlocked = blackStops | eastOne(blackStops) | westOne(blackStops);
		const Bitboard blackBlocked = whiteStops | eastOne(whiteStops) | westOne(whiteStops);
		whitePassed = ctx.whitePawns & ~whiteBlocked;
		blackPassed = ctx.blackPawns & ~blackBlocked;

		const Bitboard whiteConnected =
			whitePawnAttackMap(ctx.whitePawns) | eastOne(ctx.whitePawns) | westOne(ctx.whitePawns);
		const Bitboard blackConnected =
			blackPawnAttackMap(ctx.blackPawns) | eastOne(ctx.blackPawns) | westOne(ctx.blackPawns);
		whiteConnectedPassed = whitePassed & whiteConnected;
		blackConnectedPassed = blackPassed & blackConnected;

		Bitboard passed = whitePassed;
		while (passed != 0) {
			const int sq = Bitboards::poplsb(passed);
			const int advanced = 7 - (sq >> 3);
			score.mg += passedPawnBonus[advanced].mg;
			score.eg += passedPawnBonus[advanced].eg;
			if ((whiteConnectedPassed & Bitboards::bit(sq)) != 0) {
				score.mg += 10 + 2 * advanced;
				score.eg += 22 + 5 * advanced;
			}
		}

		passed = blackPassed;
		while (passed != 0) {
			const int sq = Bitboards::poplsb(passed);
			const int advanced = sq >> 3;
			score.mg -= passedPawnBonus[advanced].mg;
			score.eg -= passedPawnBonus[advanced].eg;
			if ((blackConnectedPassed & Bitboards::bit(sq)) != 0) {
				score.mg -= 10 + 2 * advanced;
				score.eg -= 22 + 5 * advanced;
			}
		}
	}

	for (int file = 0; file < 8; ++file) {
		const int whiteCount = Bitboards::popcount(ctx.whitePawns & FileMasks[file]);
		const int blackCount = Bitboards::popcount(ctx.blackPawns & FileMasks[file]);

		if (whiteCount > 1) {
			score.mg -= 8 * (whiteCount - 1);
			score.eg -= 12 * (whiteCount - 1);
		}
		if (blackCount > 1) {
			score.mg += 8 * (blackCount - 1);
			score.eg += 12 * (blackCount - 1);
		}

		if (whiteCount != 0 && (ctx.whitePawns & AdjacentFileMasks[file]) == 0) {
			score.mg -= 10 * whiteCount;
			score.eg -= 14 * whiteCount;
		}
		if (blackCount != 0 && (ctx.blackPawns & AdjacentFileMasks[file]) == 0) {
			score.mg += 10 * blackCount;
			score.eg += 14 * blackCount;
		}
	}

	return score;
}

static inline EvalScore knightMobilityScore(int count)
{
	return { -24 + 8 * count, -16 + 5 * count };
}

static inline EvalScore bishopMobilityScore(int count)
{
	return { -18 + 5 * count, -12 + 4 * count };
}

static inline EvalScore rookMobilityScore(int count)
{
	return { -10 + 3 * count, -8 + 4 * count };
}

static inline EvalScore queenMobilityScore(int count)
{
	return { -4 + count, -2 + count };
}

static inline EvalScore rookFeatureScore(const EvalContext& ctx, bool white, int sq)
{
	const Bitboard ownPawns = white ? ctx.whitePawns : ctx.blackPawns;
	const Bitboard enemyPawns = white ? ctx.blackPawns : ctx.whitePawns;
	const int file = sq & 7;
	const int row = sq >> 3;
	const Bitboard fileMask = FileMasks[file];
	EvalScore score;
	if ((ownPawns & fileMask) == 0) {
		if ((enemyPawns & fileMask) == 0) {
			score.mg += 22;
			score.eg += 14;
		}
		else {
			score.mg += 12;
			score.eg += 8;
		}
	}
	if ((white && row == 1) || (!white && row == 6)) {
		score.mg += 18;
		score.eg += 28;
	}
	return score;
}

static inline void addKingPressure(AttackEval& eval, bool attackerWhite,
	Bitboard attacks, Bitboard kingZone, int weight)
{
	const int hits = Bitboards::popcount(attacks & kingZone);
	if (attackerWhite) {
		eval.whiteAttackUnits += hits * weight;
		eval.blackKingZoneHits += hits;
	}
	else {
		eval.blackAttackUnits += hits * weight;
		eval.whiteKingZoneHits += hits;
	}
}

static AttackEval evaluateMobilityAndKingPressure(const EvalContext& ctx)
{
	PROFILE_TIMER(::Profiler::EvalMobilityActivityTime);
	PROFILE_TIMER(::Profiler::EvalAttackGenerationTime);
	AttackEval eval;

	auto addMobility = [](EvalScore& total, bool white, EvalScore term) {
		total += white ? term : -term;
		};

	Bitboard bb = ctx.b.pieces(WN);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::KnightAttacks[sq];
		addMobility(eval.mobility, true, knightMobilityScore(Bitboards::popcount(attacks & ~ctx.whiteOcc)));
		addKingPressure(eval, true, attacks, ctx.blackKingZone, 4);
	}

	bb = ctx.b.pieces(BN);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::KnightAttacks[sq];
		addMobility(eval.mobility, false, knightMobilityScore(Bitboards::popcount(attacks & ~ctx.blackOcc)));
		addKingPressure(eval, false, attacks, ctx.whiteKingZone, 4);
	}

	bb = ctx.b.pieces(WB);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::bishopAttacks(sq, ctx.occ);
		addMobility(eval.mobility, true, bishopMobilityScore(Bitboards::popcount(attacks & ~ctx.whiteOcc)));
		addKingPressure(eval, true, attacks, ctx.blackKingZone, 3);
	}

	bb = ctx.b.pieces(BB);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::bishopAttacks(sq, ctx.occ);
		addMobility(eval.mobility, false, bishopMobilityScore(Bitboards::popcount(attacks & ~ctx.blackOcc)));
		addKingPressure(eval, false, attacks, ctx.whiteKingZone, 3);
	}

	bb = ctx.b.pieces(WR);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::rookAttacks(sq, ctx.occ);
		addMobility(eval.mobility, true, rookMobilityScore(Bitboards::popcount(attacks & ~ctx.whiteOcc)));
		eval.rooks += rookFeatureScore(ctx, true, sq);
		addKingPressure(eval, true, attacks, ctx.blackKingZone, 5);
	}

	bb = ctx.b.pieces(BR);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::rookAttacks(sq, ctx.occ);
		addMobility(eval.mobility, false, rookMobilityScore(Bitboards::popcount(attacks & ~ctx.blackOcc)));
		eval.rooks += -rookFeatureScore(ctx, false, sq);
		addKingPressure(eval, false, attacks, ctx.whiteKingZone, 5);
	}

	bb = ctx.b.pieces(WQ);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::queenAttacks(sq, ctx.occ);
		addMobility(eval.mobility, true, queenMobilityScore(Bitboards::popcount(attacks & ~ctx.whiteOcc)));
		addKingPressure(eval, true, attacks, ctx.blackKingZone, 7);
	}

	bb = ctx.b.pieces(BQ);
	while (bb != 0) {
		const int sq = Bitboards::poplsb(bb);
		const Bitboard attacks = Bitboards::queenAttacks(sq, ctx.occ);
		addMobility(eval.mobility, false, queenMobilityScore(Bitboards::popcount(attacks & ~ctx.blackOcc)));
		addKingPressure(eval, false, attacks, ctx.whiteKingZone, 7);
	}

	return eval;
}

static EvalScore evaluateBishopPair(const EvalContext& ctx)
{
	EvalScore score;
	if (ctx.b.countPieces(WB) >= 2) {
		score.mg += 30;
		score.eg += 45;
	}
	if (ctx.b.countPieces(BB) >= 2) {
		score.mg -= 30;
		score.eg -= 45;
	}
	return score;
}

static inline int kingDangerScore(int units)
{
	const int capped = std::min(units, 96);
	return std::min(320, (capped * capped) / 24);
}

static inline int kingShieldScore(Bitboard pawns, int color, int kingSq)
{
	if (kingSq < 0) {
		return 0;
	}
	const Bitboard shield = PawnShieldMasks[color][kingSq] & pawns;
	const Bitboard center = PawnShieldCenterMasks[color][kingSq] & pawns;
	return 8 * Bitboards::popcount(shield) + 6 * Bitboards::popcount(center);
}

static inline int kingFilePenalty(uint8_t ownPawnFiles, uint8_t enemyPawnFiles, int kingSq)
{
	if (kingSq < 0) {
		return 0;
	}
	const uint8_t nearFiles = KingFileBits[kingSq & 7];
	const uint8_t allPawnFiles = static_cast<uint8_t>(ownPawnFiles | enemyPawnFiles);
	const int semiOpen = popcountFiles(static_cast<uint8_t>(nearFiles & ~ownPawnFiles));
	const int open = popcountFiles(static_cast<uint8_t>(nearFiles & ~allPawnFiles));
	return 7 * semiOpen + 9 * open;
}

static EvalScore evaluateKingSafety(const EvalContext& ctx, const AttackEval& attacks)
{
	PROFILE_TIMER(::Profiler::EvalKingSafetyTime);
	EvalScore score;
	const int whiteDanger = kingDangerScore(attacks.blackAttackUnits);
	const int blackDanger = kingDangerScore(attacks.whiteAttackUnits);
	score.mg += blackDanger - whiteDanger;

	score.mg += kingShieldScore(ctx.whitePawns, Bitboards::WHITE, ctx.whiteKing);
	score.mg -= kingShieldScore(ctx.blackPawns, Bitboards::BLACK, ctx.blackKing);
	score.mg -= kingFilePenalty(ctx.whitePawnFiles, ctx.blackPawnFiles, ctx.whiteKing);
	score.mg += kingFilePenalty(ctx.blackPawnFiles, ctx.whitePawnFiles, ctx.blackKing);
	return score;
}

static EvalScore evaluateKingActivity(const EvalContext& ctx)
{
	EvalScore score;
	if (ctx.whiteKing >= 0) {
		score.eg += kingEgPST[relativeSquare(true, ctx.whiteKing)];
		score.eg += 10 * Bitboards::popcount(Bitboards::KingAttacks[ctx.whiteKing] & ctx.blackPawns);
	}
	if (ctx.blackKing >= 0) {
		score.eg -= kingEgPST[relativeSquare(false, ctx.blackKing)];
		score.eg -= 10 * Bitboards::popcount(Bitboards::KingAttacks[ctx.blackKing] & ctx.whitePawns);
	}
	return score;
}

static int evaluateClassical(board& b, EvalBreakdown* breakdown)
{
	const EvalContext ctx = makeEvalContext(b);
	EvalScore total;

	EvalScore term = evaluateMaterialPst(ctx);
	total += term;
	if (breakdown) breakdown->materialPst = term;

	term = evaluatePawns(ctx);
	total += term;
	if (breakdown) breakdown->pawns = term;

	const AttackEval attacks = evaluateMobilityAndKingPressure(ctx);
	total += attacks.mobility;
	if (breakdown) {
		breakdown->mobility = attacks.mobility;
		breakdown->whiteAttackUnits = attacks.whiteAttackUnits;
		breakdown->blackAttackUnits = attacks.blackAttackUnits;
		breakdown->whiteKingZoneHits = attacks.whiteKingZoneHits;
		breakdown->blackKingZoneHits = attacks.blackKingZoneHits;
	}

	term = attacks.rooks;
	total += term;
	if (breakdown) breakdown->rooks = term;

	term = evaluateBishopPair(ctx);
	total += term;
	if (breakdown) breakdown->bishopPair = term;

	term = evaluateKingSafety(ctx, attacks);
	total += term;
	if (breakdown) breakdown->kingSafety = term;

	term = evaluateKingActivity(ctx);
	total += term;
	if (breakdown) breakdown->kingActivity = term;

	const int whiteScore = blendScore(total, ctx.phase);
	const int sideScore = b.isWhiteTurn ? whiteScore : -whiteScore;
	if (breakdown) {
		breakdown->total = total;
		breakdown->phase = ctx.phase;
		breakdown->finalWhiteScore = whiteScore;
		breakdown->finalSideScore = sideScore;
	}
	return sideScore;
}

static int evaluateLegacyPosition(const board& b)
{
	int score = 0;

	static const int matVal[13] = {
		  0,
	   -900, -500, -100, -300, -20000, -320,
		900,  500,  100,  300,  20000,  320
	};

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
			default: return 0;
			}
		};

	constexpr Piece legacyPieces[] = { BQ, BR, BP, BN, BK, BB, WQ, WR, WP, WN, WK, WB };
	for (Piece p : legacyPieces)
	{
		int n = b.pieceCount[p];
		const int* list = b.pieceList[p];
		int mv = matVal[p];

		for (int i = 0; i < n; i++) {
			int sq = list[i];
			score += mv;
			score += pst(p, sq);
		}
	}

	for (int i = 0; i < b.pieceCount[WN]; i++) score += knightMob[b.pieceList[WN][i]];
	for (int i = 0; i < b.pieceCount[BN]; i++) score -= knightMob[b.pieceList[BN][i]];
	for (int i = 0; i < b.pieceCount[WB]; i++) score += bishopMob[b.pieceList[WB][i]];
	for (int i = 0; i < b.pieceCount[BB]; i++) score -= bishopMob[b.pieceList[BB][i]];
	for (int i = 0; i < b.pieceCount[WR]; i++) score += rookMob[b.pieceList[WR][i]];
	for (int i = 0; i < b.pieceCount[BR]; i++) score -= rookMob[b.pieceList[BR][i]];

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
			for (int i = 0; i < b.pieceCount[WP]; i++) {
				int psq = b.pieceList[WP][i];
				if (abs((psq & 7) - file) <= 1 && (psq >> 3) > rank)
					return false;
			}
			return true;
		};

	for (int i = 0; i < b.pieceCount[WP]; i++) {
		int sq = b.pieceList[WP][i];
		int advancedRanks = 7 - (sq >> 3);
		if (isPassed(sq, true)) score += 40 + 10 * advancedRanks;
	}
	for (int i = 0; i < b.pieceCount[BP]; i++) {
		int sq = b.pieceList[BP][i];
		int advancedRanks = sq >> 3;
		if (isPassed(sq, false)) score -= 40 + 10 * advancedRanks;
	}

	int wFileCnt[8] = { 0 }, bFileCnt[8] = { 0 };
	for (int i = 0; i < b.pieceCount[WP]; i++) wFileCnt[b.pieceList[WP][i] & 7]++;
	for (int i = 0; i < b.pieceCount[BP]; i++) bFileCnt[b.pieceList[BP][i] & 7]++;
	for (int f = 0; f < 8; f++) {
		if (wFileCnt[f] > 1) score -= 15 * (wFileCnt[f] - 1);
		if (bFileCnt[f] > 1) score += 15 * (bFileCnt[f] - 1);
	}

	int wKing = b.kingSquare(true);
	int bKing = b.kingSquare(false);
	Bitboard whiteAttacks = attackMapForSide(b, true);
	Bitboard blackAttacks = attackMapForSide(b, false);
	int atkWhiteKing = Bitboards::popcount(kingSafetyZoneMask(wKing) & blackAttacks);
	int atkBlackKing = Bitboards::popcount(kingSafetyZoneMask(bKing) & whiteAttacks);
	score -= atkWhiteKing * 20;
	score += atkBlackKing * 20;

	auto pawnShield = [&](int ksq, bool isWhite) {
		int r = ksq >> 3, c = ksq & 7;
		int s = 0;
		if (isWhite && r > 0) {
			int front = r - 1;
			if (b.pieceAt(front * 8 + c) == WP) s += 15;
			if (c > 0 && b.pieceAt(front * 8 + c - 1) == WP) s += 10;
			if (c < 7 && b.pieceAt(front * 8 + c + 1) == WP) s += 10;
		}
		else if (!isWhite && r < 7) {
			int front = r + 1;
			if (b.pieceAt(front * 8 + c) == BP) s += 15;
			if (c > 0 && b.pieceAt(front * 8 + c - 1) == BP) s += 10;
			if (c < 7 && b.pieceAt(front * 8 + c + 1) == BP) s += 10;
		}
		return s;
		};

	score += pawnShield(wKing, true);
	score -= pawnShield(bKing, false);

	bool wPawnFiles[8] = { false };
	bool bPawnFiles[8] = { false };
	for (int i = 0; i < b.pieceCount[WP]; i++) wPawnFiles[b.pieceList[WP][i] & 7] = true;
	for (int i = 0; i < b.pieceCount[BP]; i++) bPawnFiles[b.pieceList[BP][i] & 7] = true;

	auto kingOpenPenalty = [&](int ksq, bool white) {
		int f = ksq & 7;
		int s = 0;
		const bool* files = white ? wPawnFiles : bPawnFiles;
		if (!files[f]) s -= 15;
		if (f > 0 && !files[f - 1]) s -= 10;
		if (f < 7 && !files[f + 1]) s -= 10;
		return s;
		};

	score += kingOpenPenalty(wKing, true);
	score -= kingOpenPenalty(bKing, false);

	if (wKing == 62 || wKing == 58) score += 40;
	if (bKing == 6 || bKing == 2) score -= 40;

	auto isCenter = [&](int sq) {
		int r = sq >> 3, c = sq & 7;
		return (r >= 2 && r <= 5 && c >= 2 && c <= 5);
		};
	if (b.fullmoveNumber > 10) {
		if (isCenter(wKing)) score -= 40;
		if (isCenter(bKing)) score += 40;
	}

	return b.isWhiteTurn ? score : -score;
}

} // namespace

namespace {

constexpr int HistoryLimit = 400000;

static int historyBonusForDepth(int depth)
{
	return std::min(32000, 256 * depth * depth);
}

static void updateHistoryEntry(int& entry, int bonus)
{
	bonus = std::clamp(bonus, -HistoryLimit, HistoryLimit);
	const int absBonus = bonus < 0 ? -bonus : bonus;
	const int64_t adjusted = static_cast<int64_t>(entry) + bonus -
		(static_cast<int64_t>(entry) * absBonus) / HistoryLimit;
	entry = static_cast<int>(std::clamp<int64_t>(adjusted, -HistoryLimit, HistoryLimit));
}

static int lmrReductionForMove(int depth, int searchedMoves, int historyScore,
	bool pvNode, bool isKiller)
{
	const int moveNumber = searchedMoves + 1;
	if (depth < 3 || moveNumber < 3) {
		return 0;
	}

	int reduction = 1;
	if (depth >= 6) {
		++reduction;
	}
	if (depth >= 10) {
		++reduction;
	}
	if (moveNumber >= 6) {
		++reduction;
	}
	if (moveNumber >= 12) {
		++reduction;
	}
	if (historyScore < -12000) {
		++reduction;
	}
	else if (historyScore > 60000) {
		--reduction;
	}
	if (pvNode) {
		--reduction;
	}
	if (isKiller) {
		--reduction;
	}

	return std::clamp(reduction, 0, depth - 2);
}

} // namespace

// ==========================================================
// MOVE ORDERING (CAPTURE + KILLER + HISTORY)
// ==========================================================
int Engine::scoreMove(const Move& m, const board& b, int ply,
	bool haveTTMove, const Move& ttMove)
{
	PROFILE_INC(::Profiler::ScoreMoveCalls);

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
	return std::clamp(historyHeuristic[side][m.from][m.to], -HistoryLimit, HistoryLimit);
}

// ==========================================================
// STATIC EVALUATION
// ==========================================================
int Engine::evaluate(board& b)
{
	PROFILE_INC(::Profiler::EvalCalls);
	PROFILE_TIMER(::Profiler::EvalTime);
	return evaluateClassical(b, nullptr);
}

int Engine::debugEvaluate(board& b)
{
	return evaluate(b);
}

int Engine::debugEvaluateLegacy(const board& b) const
{
	return evaluateLegacyPosition(b);
}

std::string Engine::debugEvaluateBreakdown(board& b)
{
	PROFILE_INC(::Profiler::EvalCalls);
	PROFILE_TIMER(::Profiler::EvalTime);
	EvalBreakdown breakdown;
	const int score = evaluateClassical(b, &breakdown);

	std::ostringstream out;
	out << "phase " << breakdown.phase << "/" << EvalMaxPhase
		<< " finalWhite " << breakdown.finalWhiteScore
		<< " finalSide " << score
		<< " attackUnitsW " << breakdown.whiteAttackUnits
		<< " attackUnitsB " << breakdown.blackAttackUnits
		<< " kingZoneHitsW " << breakdown.whiteKingZoneHits
		<< " kingZoneHitsB " << breakdown.blackKingZoneHits << '\n';

	auto append = [&](const char* name, const EvalScore& term) {
		out << name << " mg " << term.mg
			<< " eg " << term.eg
			<< " blended " << blendScore(term, breakdown.phase) << '\n';
		};

	append("materialPst", breakdown.materialPst);
	append("pawns", breakdown.pawns);
	append("mobility", breakdown.mobility);
	append("rooks", breakdown.rooks);
	append("bishopPair", breakdown.bishopPair);
	append("kingSafety", breakdown.kingSafety);
	append("kingActivity", breakdown.kingActivity);
	append("total", breakdown.total);
	return out.str();
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
	newSearch();
	lastScore = 0;
	lastDepth = 0;
	

	maxDepth = std::clamp(maxDepth, 1, MAX_DEPTH - 1);
	this->maxDepth = maxDepth;
	const bool emitUciInfo = emitSearchInfo && uciInfoOutputEnabled();
	auto recordTTHashfull = [&]() {
		PROFILE_MAX(::Profiler::TTHashfullPermille, ttHashfullPermille());
		};

	// --------------------------------------------
	// Search start time and repetition root.
	// --------------------------------------------
	searchStart = sharedControl
		? sharedControl->startTime()
		: std::chrono::steady_clock::now();

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
		recordTTHashfull();
		lastScore = 0;
		lastDepth = 0;
		return Move(-1, -1, EMPTY, EMPTY, 0);
	}

	if (workerId > 0 && rootMoves.size() > 2) {
		const std::size_t offset =
			static_cast<std::size_t>(workerId) % rootMoves.size();
		if (offset != 0) {
			std::rotate(rootMoves.begin(), rootMoves.begin() + offset, rootMoves.end());
		}
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
		recordTTHashfull();
		lastScore = 0;
		lastDepth = 0;
		return rootMoves[0];
	}

	if (workerId == 0 && syzygyIsAvailable()) {
		int tbScore = 0;
		Move tbMove;
		if (probeSyzygyRoot(b, tbScore, tbMove)) {
			Move legalTbMove;
			if (findLegalEquivalent(rootMoves, tbMove, legalTbMove)) {
				recordTTHashfull();
				lastScore = tbScore;
				lastDepth = maxDepth;
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
			Unmove undo = b.makeMove(rm);

			const int childDepth = depth - 1;
			int childScore = 0;
			int score = -INF;
			bool aborted = false;

			if (firstMove) {
				childScore = search(b, childDepth, -rootBeta, -alpha,
					1, repHistory, true, 0);
				aborted = isSearchAborted(childScore);
				if (!aborted) {
					score = -childScore;
				}
			}
			else {
				childScore = search(b, childDepth, -alpha - 1, -alpha,
					1, repHistory, false, 0);
				aborted = isSearchAborted(childScore);
				if (!aborted) {
					score = -childScore;
				}

				if (!aborted && score > alpha && score < rootBeta) {
					childScore = search(b, childDepth, -rootBeta, -alpha,
						1, repHistory, true, 0);
					aborted = isSearchAborted(childScore);
					if (!aborted) {
						score = -childScore;
					}
				}
			}

			b.unmakeMove(rm, undo);

			if (aborted) {
				entry.aborted = true;
				result.stopped = true;
				break;
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

		bool timedOut = elapsed >= timeLimitMs ||
			stopSearch.load(std::memory_order_relaxed) ||
			(sharedControl && sharedControl->shouldStop());

		if (!timedOut && depthCompleted)
		{
			rememberRootScores(lastAttempt);
			bestFullMove = lastAttempt.bestMove;
			bestFullScore = lastAttempt.bestScore;
			haveFull = true;
			bestSafeMove = bestFullMove;
			bestSafeScore = bestFullScore;
			haveSafe = true;
			lastScore = bestFullScore;
			lastDepth = depth;

			if (emitUciInfo) {
				// Emit only after a fully completed root depth; never from node loops.
				emitCompletedDepthInfo(depth, bestFullScore, bestFullMove,
					reportedNodeCount(), searchStart);
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
		recordTTHashfull();
		lastScore = bestFullScore;
		return bestFullMove;
	}
	if (haveSafe) {
		recordTTHashfull();
		lastScore = bestSafeScore;
		return bestSafeMove;
	}
	recordTTHashfull();
	lastScore = 0;
	lastDepth = 0;
	return rootMoves[0];
}


int Engine::search(board& b, int depth, int alpha, int beta, int ply,
	std::vector<uint64_t>& repHistory, bool pvNode, int extensionCount)
{
	depth = std::clamp(depth, 0, MAX_DEPTH - 1);
	const int tablePly = std::clamp(ply, 0, MAX_DEPTH - 1);
	countNode();
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
	if (isRepeatedPosition(repHistory, key)) {
		return 0;   // repetition draw score
	}

	// Leaf ? quiescence search
	if (depth == 0) {
		leafNodes++;
		PROFILE_INC(::Profiler::LeafNodes);
		return quiescence(b, alpha, beta, ply, 0, repHistory);
	}

	RepetitionFrame repetitionFrame(repHistory, key);
	int alphaOrig = alpha;
	int nodeStaticEval = TT_NO_STATIC_EVAL;

	// -------------------------------
	// TRANSPOSITION TABLE PROBE
	// -------------------------------
	Move ttMove;
	bool haveTTMove = false;
	TTProbeResult ttResult = probeTT(key, ply);

	if (ttResult.hit) {
		// Always keep the stored best move for ordering, even from shallow entries.
		ttMove = ttResult.move;
		haveTTMove = ttResult.hasMove;
		if (ttResult.hasStaticEval) {
			nodeStaticEval = ttResult.staticEval;
		}

		if (ttResult.depth >= depth) {
			const int stored = ttResult.score;

			if (ttResult.bound == TTBound::Exact) {
				PROFILE_INC(::Profiler::TTCutoffs);
				return stored;
			}
			if (ttResult.bound == TTBound::Upper && stored <= alpha) {
				PROFILE_INC(::Profiler::TTCutoffs);
				return stored;
			}
			if (ttResult.bound == TTBound::Lower && stored >= beta) {
				PROFILE_INC(::Profiler::TTCutoffs);
				return stored;
			}
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
			storeTT(key, 127, tbScore, TTBound::Exact, tbMove, ply, nodeStaticEval);

			return tbScore;
		}
	}

	

	// -------------------------------
	// IN-CHECK DETECTION
	// -------------------------------
	int kingSq = -1;
	bool inCheck = false;
	Bitboard checkers = 0;
	{
		PROFILE_INC(::Profiler::InCheckCalls);
		PROFILE_TIMER(::Profiler::InCheckTime);
		kingSq = b.kingSquare(b.isWhiteTurn);
		checkers = moveGenerator->attackersToSquare(b, kingSq, !b.isWhiteTurn, b.occupied);
		inCheck = checkers != 0;
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
			nodeStaticEval = staticEval;
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
		moveGenerator->generateLegalMoves(b, moves, kingSq, checkers);
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
	Move searchedQuietMoves[MoveList::MAX_MOVES];
	int searchedQuietCount = 0;

	for (int orderedIndex = 0; orderedIndex < moves.count; ++orderedIndex) {
		selectBestScoredMove(moveData, scores, orderedIndex, moves.count);
		Move& move = moveData[orderedIndex];

		const bool quietMove = isQuietMove(move);
		const int movingSide = b.isWhiteTurn ? 0 : 1;
		const int historyScore = quietMove
			? historyHeuristic[movingSide][move.from][move.to]
			: 0;

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
			oppKingSq = b.kingSquare(b.isWhiteTurn);
			givesCheck = moveGenerator->attackersToSquare(b, oppKingSq, !b.isWhiteTurn, b.occupied) != 0;
		}

		bool isTTMove = false;
		if (haveTTMove && sameMoveIdentity(move, ttMove))
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
			historyScore < 12000)
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
		// LMR: reduce late, quiet, tactically unforcing moves. PV/killer/high-history
		// moves are either unreduced or reduced less, and any alpha raise is
		// re-searched at full depth.
		PROFILE_INC(::Profiler::LmrAttempts);
		int reduction = 0;
		if (newDepth > 1 &&
			quietMove &&
			!inCheck &&
			!givesCheck &&
			!isTTMove)
		{
			reduction = std::min(
				lmrReductionForMove(depth, searchedMoves, historyScore, pvNode, isKiller),
				newDepth - 1);
		}

		if (reduction > 0)
		{
			PROFILE_INC(::Profiler::LmrApplied);
			// Reduced-depth search with null window
			int childScore = search(b, newDepth - reduction,
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
			if (quietMove) {
				// Quiet beta cutoffs update killer and side-aware history tables.
				if (!sameMoveIdentity(killerMoves[tablePly][0], move)) {
					killerMoves[tablePly][1] = killerMoves[tablePly][0];
					killerMoves[tablePly][0] = move;
				}
				const int bonus = historyBonusForDepth(depth);
				updateHistoryEntry(historyHeuristic[movingSide][move.from][move.to], bonus);
				for (int i = 0; i < searchedQuietCount; ++i) {
					const Move& failedQuiet = searchedQuietMoves[i];
					updateHistoryEntry(
						historyHeuristic[movingSide][failedQuiet.from][failedQuiet.to],
						-bonus);
				}
			}
			break;
		}

		if (quietMove && searchedQuietCount < MoveList::MAX_MOVES) {
			searchedQuietMoves[searchedQuietCount++] = move;
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
	TTBound bound = TTBound::Exact;
	if (besteval <= alphaOrig) {
		bound = TTBound::Upper;  // fail-low
	}
	else if (besteval >= beta) {
		bound = TTBound::Lower;  // fail-high
	}

	storeTT(key, depth, besteval, bound, bestMoveLocal, ply, nodeStaticEval);

	return besteval;
}


int Engine::quiescence(board& b, int alpha, int beta, int ply, int qply,
	std::vector<uint64_t>& repHistory)
{
	countNode();  // still count these as nodes
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
	constexpr int QuietCheckAlphaMargin = 180;

	if (shouldStop()) {
		return SEARCH_ABORTED;
	}

	if (b.halfmoveClock >= 100) {
		return 0;
	}

	uint64_t key = computeHash(b);
	if (isRepeatedPosition(repHistory, key)) {
		return 0;
	}
	RepetitionFrame repetitionFrame(repHistory, key);

	int kingSq = -1;
	bool inCheck = false;
	Bitboard checkers = 0;
	{
		PROFILE_INC(::Profiler::InCheckCalls);
		PROFILE_TIMER(::Profiler::InCheckTime);
		kingSq = b.kingSquare(b.isWhiteTurn);
		checkers = moveGenerator->attackersToSquare(b, kingSq, !b.isWhiteTurn, b.occupied);
		inCheck = checkers != 0;
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
	const bool includeQuietChecks =
		inCheck || (qply < MaxQuietCheckQply && standPat + QuietCheckAlphaMargin >= alpha);
	{
		PROFILE_MOVEGEN_CONTEXT(Qsearch);
		moveGenerator->generateQuiescenceMoves(b, moves, kingSq, checkers, includeQuietChecks);
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
	const int tablePly = std::clamp(ply, 0, MAX_DEPTH - 1);

	if (inCheck) {
		// Stand-pat is illegal while in check; search every legal evasion.
		searchCount = moves.count;
		PROFILE_ADD(::Profiler::QMovesAfterPruning, searchCount);
		PROFILE_SEE_CONTEXT(QsearchMoveOrdering);
		for (int i = 0; i < searchCount; ++i) {
			qMoves[i] = moves.data()[i];
			scores[i] = scoreMove(qMoves[i], b, tablePly, false, Move());
		}
	}
	else {
		const bool alphaIsNormal = alpha > -MATE_THRESHOLD && alpha < MATE_THRESHOLD;
		for (auto& m : moves) {
			bool isCapture = (m.captured != EMPTY) || m.wasEnPassant;
			bool tactical = isCapture || m.wasPromotion;
			if (!tactical) {
				if (!includeQuietChecks) {
					PROFILE_INC(::Profiler::QMovesSkippedNotCaptureOrPromotionOrCheck);
					continue;
				}

				qMoves[searchCount] = m;
				{
					PROFILE_SEE_CONTEXT(QsearchMoveOrdering);
					scores[searchCount] = 250000 +
						std::clamp(scoreMove(m, b, tablePly, false, Move()),
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
