#pragma once

namespace chess {

struct SearchLimits {
    int maxDepth = 128;
    int moveTimeMs = 1000;
};

} // namespace chess
