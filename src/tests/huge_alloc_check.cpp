/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the below code to the     *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/

// Exercises the unified allocation primitive: HugeAlloc/HugeFree and the
// HugeVector adapter. Verifies non-null returns, 64-byte alignment on every
// path, write/read round-trips across a range of sizes, and that HugeVector
// behaves like std::vector (grow, index, copy).

#include "core/memory/huge_alloc.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
    if (!cond) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

void CheckAlign(void *p, const char *what) {
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(p);
    Check(p != nullptr, what);
    Check((a % 64u) == 0u, "pointer is 64-byte aligned");
}

void CheckRoundTrip(std::size_t bytes) {
    char *p = static_cast<char *>(lux2::HugeAlloc(bytes));
    CheckAlign(p, "HugeAlloc non-null/aligned");
    // Touch first and last byte to confirm the whole region is mapped.
    if (p) {
        p[0] = 0x11;
        if (bytes > 1) {
            p[bytes - 1] = 0x22;
            Check(p[0] == 0x11 && p[bytes - 1] == 0x22,
                  "read-back after write");
        } else {
            Check(p[0] == 0x11, "read-back after write");
        }
    }
    lux2::HugeFree(p, bytes);
}

void CheckVector() {
    lux2::HugeVector<uint32_t> v;
    for (uint32_t i = 0; i < 100000u; ++i)
        v.push_back(i * 3u);
    Check(v.size() == 100000u, "HugeVector grew to expected size");
    bool ok = true;
    for (uint32_t i = 0; i < v.size(); ++i)
        if (v[i] != i * 3u) { ok = false; break; }
    Check(ok, "HugeVector contents correct after growth");

    // assign() must reuse capacity when capacity is sufficient (stable VA),
    // which is what BlockArena::Reset relies on.
    const uint32_t *before = v.data();
    v.assign(50u, 7u);
    Check(v.size() == 50u && v[49] == 7u, "assign shrinks and fills");
    Check(v.data() == before, "assign keeps capacity (stable data pointer)");

    // Copy construction from a plain vector.
    std::vector<float> src(1234u, 1.5f);
    lux2::HugeVector<float> dst(src.begin(), src.end());
    Check(dst.size() == src.size() && dst[1000] == 1.5f,
          "HugeVector range-construct from std::vector");
}

} // namespace

int main() {
    std::cout << "huge_alloc: HugePagesEnabled="
              << (lux2::HugePagesEnabled() ? "yes" : "no") << "\n";

    CheckRoundTrip(1);
    CheckRoundTrip(64);
    CheckRoundTrip(4096);
    CheckRoundTrip(1u << 20);          // 1 MiB
    CheckRoundTrip((1u << 20) + 7u);   // unaligned request
    CheckRoundTrip(33u * (1u << 20));  // ~33 MiB, film-scale

    CheckVector();

    if (g_failures == 0) {
        std::cout << "lux2hugeallocheck: ALL CHECKS PASSED\n";
        return EXIT_SUCCESS;
    }
    std::cerr << g_failures << " check(s) failed\n";
    return EXIT_FAILURE;
}
