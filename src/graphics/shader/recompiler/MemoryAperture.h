#pragma once

#include <cstdint>

namespace Libs::Graphics::ShaderRecompiler {

// A shader-visible private aperture outside Kyty's guest virtual address space.
// Its low dword is a per-invocation scratch offset, not a host address.
inline constexpr uint32_t PrivateApertureHigh = 0x1000u;
inline constexpr uint64_t PrivateApertureBase = uint64_t {PrivateApertureHigh} << 32u;
inline constexpr uint64_t PrivateApertureLimit = PrivateApertureBase | UINT32_MAX;
inline constexpr uint32_t SharedApertureHigh = 0x2000u;
inline constexpr uint64_t SharedApertureBase = uint64_t {SharedApertureHigh} << 32u;
inline constexpr uint64_t SharedApertureLimit = SharedApertureBase | UINT32_MAX;

} // namespace Libs::Graphics::ShaderRecompiler
