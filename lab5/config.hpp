#pragma once


namespace config{
    constexpr int ITERATIONS = 1000;
    constexpr int K = 8;
    constexpr int RING_CAPACITY = 16;
    constexpr int PASSES_PER_RESULT = 100;

    // Variant 2 runs ITERATIONS / PASSES_PER_RESULT results of PASSES_PER_RESULT passes each,
    // so every stage runs exactly ITERATIONS passes. That only works if this divides evenly
    // (e.g. for a single-run experiment, set both ITERATIONS and PASSES_PER_RESULT to 1).
    static_assert(ITERATIONS >= 1, "ITERATIONS must be at least 1");
    static_assert(PASSES_PER_RESULT >= 1, "PASSES_PER_RESULT must be at least 1");
    static_assert(ITERATIONS % PASSES_PER_RESULT == 0,
                  "ITERATIONS must be a multiple of PASSES_PER_RESULT (variant 2 would skip passes)");
}
