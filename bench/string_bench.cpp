// Copy vs. move of std::string, for short strings (fit in the small-string
// buffer, no heap) and long strings (heap allocated). Run with:
//   ./build/release/bench
#include <string>
#include <utility>

#include <benchmark/benchmark.h>

namespace {

void BM_StringCopy(benchmark::State& state) {
  const std::string src(static_cast<std::size_t>(state.range(0)), 'x');
  for (auto _ : state) {
    std::string dst = src;
    benchmark::DoNotOptimize(dst);
  }
}
BENCHMARK(BM_StringCopy)->Arg(8)->Arg(64)->Arg(4096);

void BM_StringMove(benchmark::State& state) {
  std::string src(static_cast<std::size_t>(state.range(0)), 'x');
  for (auto _ : state) {
    std::string dst = std::move(src);
    benchmark::DoNotOptimize(dst);
    src = std::move(dst);  // give the buffer back so every iteration does the same work
  }
}
BENCHMARK(BM_StringMove)->Arg(8)->Arg(64)->Arg(4096);

}  // namespace
