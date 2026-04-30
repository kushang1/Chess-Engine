#ifndef SYZYGY_H
#define SYZYGY_H

#include "Board.h"

#include "tbprobe.h"


// Call this once at program start (or in Engine ctor) with the folder path
// that contains your .rtbw / .rtbz Syzygy files.
bool initSyzygy(const char* path);

// Probes Syzygy tablebases for the given position.
// Returns true if TB hit, false if not in tablebase / failed.
//
// outScore: evaluation from *side-to-move* POV, like your evaluate():
//   big positive -> winning, big negative -> losing, 0 -> draw.
//
// outBestMove: best TB move if available (may be default/invalid in some cases).
bool probeSyzygy(board& b, int& outScore, Move& outBestMove);

#endif // SYZYGY_H
