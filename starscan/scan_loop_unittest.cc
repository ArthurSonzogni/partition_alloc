// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/partition_allocator/starscan/scan_loop.h"

#include "base/allocator/partition_allocator/partition_alloc_config.h"
#include "base/cpu.h"
#include "build/build_config.h"

#include "testing/gtest/include/gtest/gtest.h"

#if defined(PA_HAS_64_BITS_POINTERS)

namespace base {
namespace internal {

namespace {

enum class Cage { kOn, kOff };

class TestScanLoop final : public ScanLoop<TestScanLoop> {
  friend class ScanLoop<TestScanLoop>;

 public:
  TestScanLoop(SimdSupport ss, Cage cage)
      : ScanLoop(ss), with_cage_(cage == Cage::kOn) {}

  size_t visited() const { return visited_; }

  void Reset() { visited_ = 0; }

 private:
  static constexpr uintptr_t kCageMask = 0xffffff0000000000;
  static constexpr uintptr_t kBasePtr = 0x1234560000000000;

  bool WithCage() const { return with_cage_; }
  uintptr_t CageBase() const { return kBasePtr; }
  static constexpr uintptr_t CageMask() { return kCageMask; }

  void CheckPointer(uintptr_t maybe_ptr) { ++visited_; }
  void CheckPointerNoGigaCage(uintptr_t maybe_ptr) { ++visited_; }

  size_t visited_ = 0;
  bool with_cage_ = false;
};

static constexpr uintptr_t kValidPtr = 0x123456789abcdef0;
static constexpr uintptr_t kInvalidPtr = 0xaaaaaaaaaaaaaaaa;
static constexpr uintptr_t kZeroPtr = 0x0;

// Tests all possible compbinations of incoming args.
template <size_t Alignment, typename... Args>
void TestOnRangeWithAlignment(TestScanLoop& sl,
                              size_t expected_visited,
                              Args... args) {
  alignas(Alignment) uintptr_t range[] = {args...};
  std::sort(std::begin(range), std::end(range));
  do {
    sl.Run(std::begin(range), std::end(range));
    EXPECT_EQ(expected_visited, sl.visited());
    sl.Reset();
  } while (std::next_permutation(std::begin(range), std::end(range)));
}

}  // namespace

TEST(ScanLoopTest, UnvectorizedWithCage) {
  {
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOn);
    TestOnRangeWithAlignment<8>(sl, 0u, kInvalidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOn);
    TestOnRangeWithAlignment<8>(sl, 1u, kValidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOn);
    TestOnRangeWithAlignment<8>(sl, 2u, kValidPtr, kValidPtr, kInvalidPtr);
  }
  {
    // Make sure zeros are skipped.
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOn);
    TestOnRangeWithAlignment<8>(sl, 1u, kValidPtr, kInvalidPtr, kZeroPtr);
  }
}

TEST(ScanLoopTest, UnvectorizedNoCage) {
  // Without the cage all non-zero pointers are visited.
  {
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOff);
    TestOnRangeWithAlignment<8>(sl, 3u, kInvalidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOff);
    TestOnRangeWithAlignment<8>(sl, 3u, kValidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    // Make sure zeros are skipped.
    TestScanLoop sl(SimdSupport::kUnvectorized, Cage::kOff);
    TestOnRangeWithAlignment<8>(sl, 2u, kValidPtr, kInvalidPtr, kZeroPtr);
  }
}

// The vectorized tests try to test the part that can be scanned with vectorized
// instructions (aligned by the vector size) and also the residual part, which
// is scanned wwithout vectorization.

#if defined(ARCH_CPU_X86_64)
TEST(ScanLoopTest, VectorizedSSE4) {
  base::CPU cpu;
  if (!cpu.has_sse41())
    return;
  {
    TestScanLoop sl(SimdSupport::kSSE41, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 0u, kInvalidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kSSE41, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 1u, kValidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kSSE41, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 2u, kValidPtr, kValidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kSSE41, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 3u, kValidPtr, kValidPtr, kValidPtr);
  }
}

TEST(ScanLoopTest, VectorizedAVX2) {
  base::CPU cpu;
  if (!cpu.has_avx2())
    return;
  {
    TestScanLoop sl(SimdSupport::kAVX2, Cage::kOn);
    TestOnRangeWithAlignment<32>(sl, 0u, kInvalidPtr, kInvalidPtr, kInvalidPtr,
                                 kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kAVX2, Cage::kOn);
    TestOnRangeWithAlignment<32>(sl, 1u, kValidPtr, kInvalidPtr, kInvalidPtr,
                                 kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kAVX2, Cage::kOn);
    TestOnRangeWithAlignment<32>(sl, 2u, kValidPtr, kValidPtr, kInvalidPtr,
                                 kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kAVX2, Cage::kOn);
    TestOnRangeWithAlignment<32>(sl, 3u, kValidPtr, kValidPtr, kValidPtr,
                                 kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kAVX2, Cage::kOn);
    TestOnRangeWithAlignment<32>(sl, 4u, kValidPtr, kValidPtr, kValidPtr,
                                 kValidPtr, kInvalidPtr);
  }
  {
    // Check that the residual pointer is also visited.
    TestScanLoop sl(SimdSupport::kAVX2, Cage::kOn);
    TestOnRangeWithAlignment<32>(sl, 5u, kValidPtr, kValidPtr, kValidPtr,
                                 kValidPtr, kValidPtr);
  }
}
#endif  // defined(ARCH_CPU_X86_64)

#if defined(PA_STARSCAN_NEON_SUPPORTED)
TEST(ScanLoopTest, VectorizedNEON) {
  {
    TestScanLoop sl(SimdSupport::kNEON, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 0u, kInvalidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kNEON, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 1u, kValidPtr, kInvalidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kNEON, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 2u, kValidPtr, kValidPtr, kInvalidPtr);
  }
  {
    TestScanLoop sl(SimdSupport::kNEON, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 3u, kValidPtr, kValidPtr, kValidPtr);
  }
  {
    // Don't visit zeroes.
    TestScanLoop sl(SimdSupport::kNEON, Cage::kOn);
    TestOnRangeWithAlignment<16>(sl, 1u, kInvalidPtr, kValidPtr, kZeroPtr);
  }
}
#endif  // defined(PA_STARSCAN_NEON_SUPPORTED)

}  // namespace internal
}  // namespace base

#endif  // defined(PA_HAS_64_BITS_POINTERS)
