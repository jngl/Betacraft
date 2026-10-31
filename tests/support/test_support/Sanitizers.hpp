#pragma once

// Whether this test binary is instrumented by AddressSanitizer or ThreadSanitizer (the CI jobs
// asan-ubsan and tsan). Instrumentation slows code several-fold, so a wall-clock guard that is
// not a benchmark scales its budget by kSanitizerSlowdown instead of tripping on it.

namespace betacraft::test {

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) // GCC
inline constexpr bool kSanitizedBuild = true;
#elif defined(__has_feature)                                      // Clang
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
inline constexpr bool kSanitizedBuild = true;
#else
inline constexpr bool kSanitizedBuild = false;
#endif
#else
inline constexpr bool kSanitizedBuild = false;
#endif

inline constexpr double kSanitizerSlowdown = kSanitizedBuild ? 5.0 : 1.0;

} // namespace betacraft::test
