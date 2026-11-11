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

// Verifies PixelSampler patterns are bijections over their tile.

#include "pixelsamplers/linear.h"
#include "pixelsamplers/tile.h"
#include "core/tilequeue.h"

#include <iostream>
#include <memory>
#include <vector>

using namespace lux2;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
    if (!cond) {
        std::cerr << "  FAIL: " << what << std::endl;
        ++g_failures;
    } else {
        std::cout << "  ok:   " << what << std::endl;
    }
}

// Every position maps inside the tile and every pixel is hit exactly once.
void CheckBijection(const PixelSampler &ps, const Tile &tile,
                    const char *what) {
    const uint32_t n = ps.TotalPixels(tile);
    const int w = tile.x1 - tile.x0, h = tile.y1 - tile.y0;
    std::vector<int> hits(size_t(w * h), 0);
    bool inside = true;
    for (uint32_t p = 0; p < n; ++p) {
        int x = 0, y = 0;
        ps.GetPixel(p, tile, &x, &y);
        if (x < tile.x0 || x >= tile.x1 || y < tile.y0 || y >= tile.y1) {
            inside = false;
            continue;
        }
        ++hits[size_t(y - tile.y0) * size_t(w) + size_t(x - tile.x0)];
    }
    bool once = true;
    for (int v : hits)
        if (v != 1)
            once = false;
    Check(inside && once && n == uint32_t(w * h), what);
}

} // namespace

int main() {
    std::cout << "lux2 pixelsampler_check" << std::endl;

    // ---- 1. linear ------------------------------------------------------
    {
        LinearPixelSampler ps;
        CheckBijection(ps, Tile{0, 0, 8, 8}, "linear 8x8 bijection");
        CheckBijection(ps, Tile{3, 5, 11, 9}, "linear offset tile bijection");
        CheckBijection(ps, Tile{0, 0, 1, 1}, "linear single pixel");

        // Scanline order: pos = (y - y0) * w + (x - x0).
        int x = -1, y = -1;
        ps.GetPixel(9, Tile{2, 4, 6, 8}, &x, &y); // row 2, col 1
        Check(x == 3 && y == 6, "linear scanline position");
    }

    // ---- 2. tile --------------------------------------------------------
    {
        TilePixelSampler ps(32);
        CheckBijection(ps, Tile{0, 0, 8, 8}, "tile 8x8 block32 bijection");
        CheckBijection(ps, Tile{0, 0, 64, 64}, "tile 64x64 block32 bijection");
        CheckBijection(ps, Tile{1, 2, 35, 36}, "tile ragged block bijection");
        CheckBijection(ps, Tile{0, 0, 100, 37}, "tile wide ragged bijection");

        // First position is the tile origin; the first block stays full.
        int x = -1, y = -1;
        ps.GetPixel(0, Tile{5, 5, 40, 40}, &x, &y);
        Check(x == 5 && y == 5, "tile starts at origin");
        // Position 33 in a full first block: row 1, col 1 of the block.
        ps.GetPixel(33, Tile{0, 0, 64, 64}, &x, &y);
        Check(x == 1 && y == 1, "tile raster within block");
    }

    // ---- 3. Clone independence -------------------------------------------
    {
        TilePixelSampler ps(16);
        auto c = ps.Clone();
        Tile t{0, 0, 40, 40};
        bool same = true;
        for (uint32_t p = 0; p < ps.TotalPixels(t); ++p) {
            int ax, ay, bx, by;
            ps.GetPixel(p, t, &ax, &ay);
            c->GetPixel(p, t, &bx, &by);
            if (ax != bx || ay != by)
                same = false;
        }
        Check(same, "tile clone matches");
    }

    if (g_failures == 0) {
        std::cout << "ALL PIXELSAMPLER TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " test(s) FAILED" << std::endl;
    return 1;
}
