#include <benchmark/benchmark.h>
#include "shmap.h"

//////////////////////////////////////////////////////////////
static void system_malloc_and_free_once(benchmark::State &state) {
    for (auto _ : state) {
        auto p = std::malloc(1);
        benchmark::DoNotOptimize(p);
        if (p) {
            std::free(p);
        }
    }
}

BENCHMARK(system_malloc_and_free_once);

BENCHMARK_MAIN();
