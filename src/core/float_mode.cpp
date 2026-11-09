#include "core/float_mode.h"

#include <cstdint>

#if defined(_M_IX86) || defined(__i386__)
#error "Wind does not support 32-bit x86: x87 and its SSE flavours break deterministic simulation (Principles.md)"
#endif

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#elif defined(_M_ARM64)
#include <intrin.h>
#endif

namespace engine {
namespace {

#if defined(_M_X64) || defined(__x86_64__)
// MXCSR: flush to zero (bit 15), denormals are zero (bit 6), rounding control (bits 13-14, 00 is nearest).
inline constexpr std::uint32_t kModeBits = 0x8000u | 0x0040u | 0x6000u;

[[nodiscard]] std::uint32_t wanted(std::uint32_t csr) noexcept {
    return csr & ~kModeBits;
}
#elif defined(__aarch64__) || defined(_M_ARM64)
// FPCR: flush to zero (bit 24), rounding mode (bits 22-23, 00 is nearest), half-precision flush (bit 19), and the
// FEAT_AFP controls (bits 0-1: flush inputs to zero, alternate handling), RES0 where FEAT_AFP is absent.
inline constexpr std::uint64_t kModeBits = (1ull << 24) | (3ull << 22) | (1ull << 19) | 0x3ull;

[[nodiscard]] std::uint64_t wanted(std::uint64_t fpcr) noexcept {
    return fpcr & ~kModeBits;
}

#if defined(_M_ARM64)
// ARM64_SYSREG(3, 3, 4, 4, 0), spelled out: not every intrin.h defines the macro.
inline constexpr int kFpcr = (1 << 14) | (3 << 11) | (4 << 7) | (4 << 3) | 0;

[[nodiscard]] std::uint64_t read_fpcr() noexcept {
    return static_cast<std::uint64_t>(_ReadStatusReg(kFpcr));
}

void write_fpcr(std::uint64_t value) noexcept {
    _WriteStatusReg(kFpcr, static_cast<__int64>(value));
}
#else
[[nodiscard]] std::uint64_t read_fpcr() noexcept {
    std::uint64_t value = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(value));
    return value;
}

void write_fpcr(std::uint64_t value) noexcept {
    __asm__ __volatile__("msr fpcr, %0" : : "r"(value));
}
#endif
#endif

}

void set_simulation_float_mode() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    const std::uint32_t csr = _mm_getcsr();
    if (csr != wanted(csr)) {
        _mm_setcsr(wanted(csr));
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    const std::uint64_t fpcr = read_fpcr();
    if (fpcr != wanted(fpcr)) {
        write_fpcr(wanted(fpcr));
    }
#elif defined(__EMSCRIPTEN__) || defined(__wasm__)
    // WebAssembly floats are IEEE with denormals and round to nearest; there is no mode to set.
#else
#error "set_simulation_float_mode: unsupported architecture (Principles.md, deterministic simulation)"
#endif
}

bool simulation_float_mode_active() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    const std::uint32_t csr = _mm_getcsr();
    return csr == wanted(csr);
#elif defined(__aarch64__) || defined(_M_ARM64)
    const std::uint64_t fpcr = read_fpcr();
    return fpcr == wanted(fpcr);
#else
    return true;
#endif
}

}
