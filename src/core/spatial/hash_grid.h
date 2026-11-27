/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

#ifndef LUX2_CORE_SPATIAL_HASH_GRID_H
#define LUX2_CORE_SPATIAL_HASH_GRID_H

#include "core/math/geometry.h"
#include "core/math/math.h"
#include "core/math/simd_transpose.h"
#include "core/math/vecp.h"
#include "core/memory/huge_alloc.h"

#include <enoki/array.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

#include <immintrin.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace lux2
{
    namespace hashgrid_detail
    {
        inline uint32_t NextPow2(uint32_t v)
        {
            constexpr uint32_t kFloor = 1024u; // keeps tiny grids from degenerating
            constexpr uint32_t kMax = 1u << 30;
            if (v <= kFloor)
                return kFloor;
            if (v > kMax)
                return kMax; // avoid p <<= 1 overflow above 2^30
            return 1u << (32u - static_cast<uint32_t>(__builtin_clz(v - 1u)));
        }

        // Teschner et al. cell hash multipliers; pow2 slot count.
        constexpr uint32_t HASH_X = 73856093u;
        constexpr uint32_t HASH_Y = 19349663u;
        constexpr uint32_t HASH_Z = 83492791u;

        // Cell coordinate implementation shared by the scalar build
        // (float/int) and the packet query (FloatP/Int32P).
        template <class FloatX, class IntX>
        inline void CellCoord(const FloatX &x, const FloatX &y, const FloatX &z,
                              const FloatX &minX, const FloatX &minY,
                              const FloatX &minZ, float invCell,
                              IntX *ix, IntX *iy, IntX *iz,
                              FloatX *fx = nullptr, FloatX *fy = nullptr,
                              FloatX *fz = nullptr)
        {
            const FloatX cx = (x - minX) * FloatX(invCell);
            const FloatX cy = (y - minY) * FloatX(invCell);
            const FloatX cz = (z - minZ) * FloatX(invCell);
            if constexpr (std::is_same_v<IntX, int>)
            {
                *ix = int(std::floor(cx));
                *iy = int(std::floor(cy));
                *iz = int(std::floor(cz));
            }
            else
            {
                *ix = enoki::floor2int<IntX>(cx);
                *iy = enoki::floor2int<IntX>(cy);
                *iz = enoki::floor2int<IntX>(cz);
                if (fx)
                    *fx = cx - enoki::floor(cx);
                if (fy)
                    *fy = cy - enoki::floor(cy);
                if (fz)
                    *fz = cz - enoki::floor(cz);
            }
        }

        // Slot hash.
        template <class IntX>
        inline auto SlotHash(const IntX &ix, const IntX &iy, const IntX &iz)
        {
            if constexpr (std::is_same_v<IntX, int>)
            {
                return (uint32_t(ix) * HASH_X) ^ (uint32_t(iy) * HASH_Y) ^
                       (uint32_t(iz) * HASH_Z);
            }
            else
            {
                const UInt32P ux = enoki::reinterpret_array<UInt32P>(ix);
                const UInt32P uy = enoki::reinterpret_array<UInt32P>(iy);
                const UInt32P uz = enoki::reinterpret_array<UInt32P>(iz);
                return (ux * HASH_X) ^ (uy * HASH_Y) ^ (uz * HASH_Z);
            }
        }

        // Slots of every cell the record's sphere overlaps. Returns 0 for a
        // non-finite record.
        template <uint32_t WORDS>
        inline int SplatSlots(const uint32_t *rec, const Point3f &min,
                              float invCell, uint32_t radiusWord,
                              std::vector<uint32_t> &scratch,
                              uint32_t slotMask)
        {
            const float px = std::bit_cast<float>(rec[0]);
            const float py = std::bit_cast<float>(rec[1]);
            const float pz = std::bit_cast<float>(rec[2]);
            const float R = std::bit_cast<float>(rec[radiusWord]);
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
                !std::isfinite(R))
                return 0;

            // Pad the splat AABB.
            const float Rp = R * (1.f + 4.f *
                                            std::numeric_limits<float>::epsilon()) +
                             EPS_DENOM;

            int x0, y0, z0, x1, y1, z1;
            CellCoord<float, int>(px - Rp, py - Rp, pz - Rp, min.x(), min.y(),
                                  min.z(), invCell, &x0, &y0, &z0);
            CellCoord<float, int>(px + Rp, py + Rp, pz + Rp, min.x(), min.y(),
                                  min.z(), invCell, &x1, &y1, &z1);

            const uint32_t span = uint32_t(x1 - x0 + 1) *
                                  uint32_t(y1 - y0 + 1) *
                                  uint32_t(z1 - z0 + 1);
            if (scratch.size() < span)
                scratch.resize(span);

            int n = 0;
            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                    {
                        const uint32_t h =
                            SlotHash(x, y, z) & slotMask;
                        bool dup = false;
                        for (int i = 0; i < n; ++i)
                            if (scratch[i] == h)
                            {
                                dup = true;
                                break;
                            }
                        if (!dup)
                            scratch[n++] = h;
                    }
            return n;
        }

        // Upper bound on the number of (record, slot) pairs a sphere
        // contributes. Returns 0 for a non-finite record.
        template <uint32_t WORDS>
        inline uint32_t SplatSpan(const uint32_t *rec, const Point3f &min,
                                  float invCell, uint32_t radiusWord)
        {
            const float px = std::bit_cast<float>(rec[0]);
            const float py = std::bit_cast<float>(rec[1]);
            const float pz = std::bit_cast<float>(rec[2]);
            const float R = std::bit_cast<float>(rec[radiusWord]);
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
                !std::isfinite(R))
                return 0;

            const float Rp = R * (1.f + 4.f *
                                            std::numeric_limits<float>::epsilon()) +
                             EPS_DENOM;
            int x0, y0, z0, x1, y1, z1;
            CellCoord<float, int>(px - Rp, py - Rp, pz - Rp, min.x(), min.y(),
                                  min.z(), invCell, &x0, &y0, &z0);
            CellCoord<float, int>(px + Rp, py + Rp, pz + Rp, min.x(), min.y(),
                                  min.z(), invCell, &x1, &y1, &z1);
            return uint32_t(x1 - x0 + 1) * uint32_t(y1 - y0 + 1) *
                   uint32_t(z1 - z0 + 1);
        }
    } // namespace hashgrid_detail

    // Field major view.
    struct HashGridFields
    {
        const SimdVec *f;

        FloatP F(uint32_t w) const { return FloatP(f[w]); }
        UInt32P U(uint32_t w) const { return UInt32P(RecBits(f[w])); }
        template <class T>
        T Gather(const T *table, uint32_t idWord, const MaskP &m) const
        {
            return enoki::gather<T>(table, U(idWord), m);
        }
    };

    template <uint32_t WORDS>
    class HashGrid
    {
    public:
        static_assert(WORDS % uint32_t(SimdLanes) == 0u && WORDS >= 4u,
                      "record must tile the vector width and hold xyz");

        HashGrid() = default;
        HashGrid(const HashGrid &) = delete;
        HashGrid &operator=(const HashGrid &) = delete;

        static constexpr uint32_t Words() { return WORDS; }
        bool Empty() const { return m_count == 0; }
        uint32_t RecordCount() const { return m_count; }
        float BuildRadius() const { return m_radius; }
        const uint32_t *Payload() const { return m_payload.data(); }

#ifdef LUX2_HASHGRID_TESTING
        // Serial reference build.
        void BuildPointsSerialForTesting(const uint32_t *records,
                                         uint32_t count, float buildRadius)
        {
            assert(buildRadius > 0.f);
            m_mode = Mode::Points;
            m_count = count;
            m_radius = buildRadius;
            m_bucket.clear();
            if (count == 0)
                return;
            ComputeBBox(records, count, buildRadius + EPS_RAY);
            m_invCell = 1.f / (2.f * buildRadius);
            m_slots = hashgrid_detail::NextPow2(count + count / 2u);
            m_slotMask = m_slots - 1u;
            BuildPointsSerial(records, count);
        }
        const uint32_t *CellStartData() const { return m_cellStart.data(); }
        uint32_t Slots() const { return m_slots; }
        const uint32_t *BucketData() const { return m_bucket.data(); }
        uint32_t SlotOfRecordForTesting(const uint32_t *rec) const
        {
            return ScalarSlot(rec) & m_slotMask;
        }
#endif

        // Point mode.
        void BuildPoints(const uint32_t *records, uint32_t count,
                         float buildRadius)
        {
            assert(buildRadius > 0.f);
            m_mode = Mode::Points;
            m_count = count;
            m_radius = buildRadius;
            m_bucket.clear();
            m_bucket.shrink_to_fit();
            if (count == 0)
            {
                m_slots = m_slotMask = 0;
                m_payload.clear();
                m_cellStart.clear();
                return;
            }
            // AVX2 gathers use signed 32 bit indices.
            assert(std::uint64_t(count) * WORDS < (1ull << 31));

            ComputeBBox(records, count, buildRadius + EPS_RAY);
            m_invCell = 1.f / (2.f * buildRadius);
            m_slots = hashgrid_detail::NextPow2(count + count / 2u);
            m_slotMask = m_slots - 1u;

            if (count < kParallelBuildThreshold)
                BuildPointsSerial(records, count);
            else
                BuildPointsParallel(records, count);
        }

        // Sphere mode where each sphere is splatted into every cell it
        // overlaps; QueryAt walks a single cell.
        void BuildSpheres(const uint32_t *records, uint32_t count,
                          float cellSize, uint32_t radiusWord)
        {
            assert(cellSize > 0.f && radiusWord < WORDS);
            m_mode = Mode::Spheres;
            m_count = count;
            m_radiusWord = radiusWord;
            if (count == 0)
            {
                m_slots = m_slotMask = 0;
                m_payload.clear();
                m_cellStart.clear();
                m_bucket.clear();
                return;
            }
            assert(std::uint64_t(count) * WORDS < (1ull << 31));

            // One parallel reduce for maxR and the finite-record bbox.
            struct Bounds
            {
                float lo[3], hi[3], maxR;
            };
            const Bounds b0{{1e30f, 1e30f, 1e30f},
                            {-1e30f, -1e30f, -1e30f},
                            0.f};
            Bounds bs = tbb::parallel_reduce(
                tbb::blocked_range<uint32_t>(0u, count, 1u << 14), b0,
                [&](const tbb::blocked_range<uint32_t> &r, Bounds acc)
                {
                    for (uint32_t i = r.begin(); i != r.end(); ++i)
                    {
                        const uint32_t *rec =
                            records + std::size_t(i) * WORDS;
                        const float R =
                            std::bit_cast<float>(rec[radiusWord]);
                        const float px = std::bit_cast<float>(rec[0]);
                        const float py = std::bit_cast<float>(rec[1]);
                        const float pz = std::bit_cast<float>(rec[2]);
                        if (!std::isfinite(px) || !std::isfinite(py) ||
                            !std::isfinite(pz) || !std::isfinite(R))
                            continue;
                        acc.maxR = std::max(acc.maxR, R);
                        acc.lo[0] = std::min(acc.lo[0], px);
                        acc.lo[1] = std::min(acc.lo[1], py);
                        acc.lo[2] = std::min(acc.lo[2], pz);
                        acc.hi[0] = std::max(acc.hi[0], px);
                        acc.hi[1] = std::max(acc.hi[1], py);
                        acc.hi[2] = std::max(acc.hi[2], pz);
                    }
                    return acc;
                },
                [](Bounds a, const Bounds &o)
                {
                    for (int k = 0; k < 3; ++k)
                    {
                        a.lo[k] = std::min(a.lo[k], o.lo[k]);
                        a.hi[k] = std::max(a.hi[k], o.hi[k]);
                    }
                    a.maxR = std::max(a.maxR, o.maxR);
                    return a;
                });
            m_radius = bs.maxR;

            // Pad the bbox by the same factor as the splat AABBs.
            const float pad = bs.maxR *
                                      (1.f + 4.f *
                                                    std::numeric_limits<float>::epsilon()) +
                              EPS_DENOM;
            for (int k = 0; k < 3; ++k)
            {
                bs.lo[k] -= pad;
                bs.hi[k] += pad;
            }
            m_min = Point3f(bs.lo[0], bs.lo[1], bs.lo[2]);
            m_minP = Point3fP(m_min.x(), m_min.y(), m_min.z());
            m_maxP = Point3fP(bs.hi[0], bs.hi[1], bs.hi[2]);
            cellSize = std::max(cellSize, bs.maxR * 0.25f);
            m_invCell = 1.f / cellSize;

            m_payload.assign(records, records + std::size_t(count) * WORDS);

            // Size the slot table from the (record, slot) pair upper bound.
            const uint32_t numChunks =
                (count + kRadixChunk - 1u) / kRadixChunk;
            std::vector<uint32_t> ub(numChunks + 1u, 0u);
            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, numChunks),
                [&](const tbb::blocked_range<uint32_t> &chunks)
                {
                    for (uint32_t c = chunks.begin(); c != chunks.end(); ++c)
                    {
                        const uint32_t r0 = c * kRadixChunk;
                        const uint32_t r1 =
                            std::min(r0 + kRadixChunk, count);
                        uint32_t s = 0;
                        for (uint32_t r = r0; r < r1; ++r)
                            s += hashgrid_detail::SplatSpan<WORDS>(
                                records + std::size_t(r) * WORDS, m_min,
                                m_invCell, radiusWord);
                        ub[c] = s;
                    }
                });
            uint32_t run = 0;
            for (uint32_t c = 0; c < numChunks; ++c)
            {
                const uint32_t n = ub[c];
                ub[c] = run;
                run += n;
            }
            ub[numChunks] = run; // upper bound on total pairs
            m_slots = hashgrid_detail::NextPow2(
                std::max(run, count + count / 2u));
            m_slotMask = m_slots - 1u;

            // Emit (slot<<32 | record) pairs at the chunk upper bound offsets.
            HugeVector<uint64_t> pairsA, pairsB;
            pairsA.resize(std::size_t(run ? run : 1));
            pairsB.resize(std::size_t(run ? run : 1));
            std::vector<uint32_t> nn(numChunks + 1u, 0u);
            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, numChunks),
                [&](const tbb::blocked_range<uint32_t> &chunks)
                {
                    std::vector<uint32_t> scratch;
                    for (uint32_t c = chunks.begin(); c != chunks.end(); ++c)
                    {
                        const uint32_t r0 = c * kRadixChunk;
                        const uint32_t r1 =
                            std::min(r0 + kRadixChunk, count);
                        uint64_t *o = &pairsA[ub[c]];
                        for (uint32_t r = r0; r < r1; ++r)
                        {
                            const int n = hashgrid_detail::SplatSlots<WORDS>(
                                records + std::size_t(r) * WORDS, m_min,
                                m_invCell, radiusWord, scratch, m_slotMask);
                            for (int i = 0; i < n; ++i)
                                *o++ = (uint64_t(scratch[i]) << 32) |
                                       uint64_t(r);
                        }
                        nn[c] = uint32_t(o - &pairsA[ub[c]]);
                    }
                });
            // Compact.
            run = 0;
            for (uint32_t c = 0; c < numChunks; ++c)
            {
                const uint32_t n = nn[c];
                nn[c] = run;
                run += n;
            }
            nn[numChunks] = run;
            const uint32_t total = run;

            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, numChunks),
                [&](const tbb::blocked_range<uint32_t> &chunks)
                {
                    for (uint32_t c = chunks.begin(); c != chunks.end(); ++c)
                        std::memcpy(&pairsB[nn[c]], &pairsA[ub[c]],
                                    std::size_t(nn[c + 1] - nn[c]) *
                                        sizeof(uint64_t));
                });

            const uint64_t *sorted = RadixSortBySlot(
                pairsB.data(), pairsA.data(), total, SlotBits());

            // cellStart over the sorted pair keys.
            m_cellStart.assign(m_slots + 1u, 0u);
            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, m_slots),
                [&](const tbb::blocked_range<uint32_t> &slotRange)
                {
                    for (uint32_t s = slotRange.begin(); s != slotRange.end();
                         ++s)
                        m_cellStart[s] = uint32_t(std::lower_bound(
                                                      sorted, sorted + total,
                                                      uint64_t(s) << 32) -
                                                  sorted);
                });
            m_cellStart[m_slots] = total;

            // Bucket = record index half of each sorted pair.
            m_bucket.assign(std::size_t(total), 0u);
            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, total, 1u << 14),
                [&](const tbb::blocked_range<uint32_t> &r)
                {
                    for (uint32_t i = r.begin(); i != r.end(); ++i)
                        m_bucket[i] = uint32_t(sorted[i]);
                });
        }

        // Packet proximity query (point mode).
        template <class VisitFn>
        void QueryNear(const Point3fP &p, const FloatP &radius2,
                       const MaskP &active, const VisitFn &visit) const
        {
            assert(m_mode == Mode::Points);
            if (m_count == 0 || !enoki::any(active))
                return;
            {
                const FloatP rmax = enoki::hmax(
                    enoki::select(active, radius2, FloatP(0.f)));
                assert(enoki::all(rmax <=
                                  m_radius * m_radius * (1.f + 1e-5f)));
            }

            const MaskP inside =
                active && enoki::all(p >= m_minP) && enoki::all(p <= m_maxP);
            if (!enoki::any(inside))
                return;

            FloatP fx, fy, fz;
            Int32P bx, by, bz;
            hashgrid_detail::CellCoord<FloatP, Int32P>(
                p.x(), p.y(), p.z(), m_minP.x(), m_minP.y(), m_minP.z(),
                m_invCell, &bx, &by, &bz, &fx, &fy, &fz);

            // The neighbour cell per axis is the one on the nearer half.
            const Int32P ox =
                enoki::select(fx < FloatP(0.5f), Int32P(-1), Int32P(1));
            const Int32P oy =
                enoki::select(fy < FloatP(0.5f), Int32P(-1), Int32P(1));
            const Int32P oz =
                enoki::select(fz < FloatP(0.5f), Int32P(-1), Int32P(1));

            // Two of the eight cells can hash to the same slot; skip
            // so a record is never visited twice.
            UInt32P seen[7];
            for (int s = 0; s < 8; ++s)
            {
                const Int32P cx = (s & 1) ? bx + ox : bx;
                const Int32P cy = (s & 2) ? by + oy : by;
                const Int32P cz = (s & 4) ? bz + oz : bz;
                const UInt32P h =
                    hashgrid_detail::SlotHash(cx, cy, cz) & UInt32P(m_slotMask);

                MaskP fresh = inside;
                for (int j = 0; j < s; ++j)
                    fresh = fresh && (h != seen[j]);
                if (s < 7)
                    seen[s] = h;
                if (!enoki::any(fresh))
                    continue;

                WalkBucket</*Bucketed=*/false, /*PerRecordR=*/false>(
                    h, fresh, p, radius2, 0u, visit);
            }
        }

        // Point query against sphere records.
        template <class VisitFn>
        void QueryAt(const Point3fP &p, const MaskP &active,
                     const VisitFn &visit) const
        {
            assert(m_mode == Mode::Spheres);
            if (m_count == 0 || !enoki::any(active))
                return;

            const MaskP inside =
                active && enoki::all(p >= m_minP) && enoki::all(p <= m_maxP);
            if (!enoki::any(inside))
                return;

            Int32P bx, by, bz;
            hashgrid_detail::CellCoord<FloatP, Int32P>(
                p.x(), p.y(), p.z(), m_minP.x(), m_minP.y(), m_minP.z(),
                m_invCell, &bx, &by, &bz);
            const UInt32P h =
                hashgrid_detail::SlotHash(bx, by, bz) & UInt32P(m_slotMask);

            WalkBucket</*Bucketed=*/true, /*PerRecordR=*/true>(
                h, inside, p, FloatP(0.f), m_radiusWord, visit);
        }

    private:
        enum class Mode : uint32_t
        {
            None,
            Points,
            Spheres
        };

        // Point mode builds switch.
        static constexpr uint32_t kParallelBuildThreshold = 1u << 18;
        static constexpr uint32_t kRadixChunk = 1u << 16;

        const Float *PosF() const
        {
            return reinterpret_cast<const Float *>(m_payload.data());
        }

        void ComputeBBox(const uint32_t *records, uint32_t count, float expand)
        {
            float lo[3] = {1e30f, 1e30f, 1e30f};
            float hi[3] = {-1e30f, -1e30f, -1e30f};
            for (uint32_t r = 0; r < count; ++r)
                for (int k = 0; k < 3; ++k)
                {
                    const float v = std::bit_cast<float>(
                        records[std::size_t(r) * WORDS + k]);
                    lo[k] = std::min(lo[k], v);
                    hi[k] = std::max(hi[k], v);
                }
            for (int k = 0; k < 3; ++k)
            {
                lo[k] -= expand;
                hi[k] += expand;
            }
            m_min = Point3f(lo[0], lo[1], lo[2]);
            m_minP = Point3fP(m_min.x(), m_min.y(), m_min.z());
            m_maxP = Point3fP(hi[0], hi[1], hi[2]);
        }

        uint32_t ScalarSlot(const uint32_t *rec) const
        {
            int ix, iy, iz;
            hashgrid_detail::CellCoord<float, int>(
                std::bit_cast<float>(rec[0]), std::bit_cast<float>(rec[1]),
                std::bit_cast<float>(rec[2]), m_min.x(), m_min.y(), m_min.z(),
                m_invCell, &ix, &iy, &iz);
            return hashgrid_detail::SlotHash(ix, iy, iz);
        }

        void BuildCellStart(const uint32_t *counts, uint32_t total)
        {
            m_cellStart.assign(m_slots + 1u, 0u);
            uint32_t sum = 0;
            for (uint32_t s = 0; s < m_slots; ++s)
            {
                m_cellStart[s] = sum;
                sum += counts[s];
            }
            m_cellStart[m_slots] = sum;
            assert(sum == total);
        }

        // Serial reference build.
        void BuildPointsSerial(const uint32_t *records, uint32_t count)
        {
            std::vector<uint32_t> slotOf(count);
            std::vector<uint32_t> counts(m_slots, 0u);
            for (uint32_t r = 0; r < count; ++r)
            {
                const uint32_t h =
                    ScalarSlot(records + std::size_t(r) * WORDS) & m_slotMask;
                slotOf[r] = h;
                counts[h]++;
            }
            BuildCellStart(counts.data(), count);

            m_payload.assign(std::size_t(count) * WORDS, 0u);
            std::vector<uint32_t> cursor(m_cellStart.begin(),
                                         m_cellStart.end() - 1);
            for (uint32_t r = 0; r < count; ++r)
            {
                const uint32_t pos = cursor[slotOf[r]]++;
                std::memcpy(&m_payload[std::size_t(pos) * WORDS],
                            records + std::size_t(r) * WORDS,
                            std::size_t(WORDS) * sizeof(uint32_t));
            }
        }

        // Number of bits needed to index m_slots slots (log2, m_slots is a
        // power of two).
        uint32_t SlotBits() const
        {
            uint32_t b = 1u;
            while ((1u << b) < m_slots)
                ++b;
            return b;
        }

        // LSD radix sort of `n` (slot<<32 | index) pairs keyed by the
        // high 32 bits.
        uint64_t *RadixSortBySlot(uint64_t *a, uint64_t *b, uint32_t n,
                                  uint32_t slotBits) const
        {
            const uint32_t numChunks =
                (n + kRadixChunk - 1u) / kRadixChunk;
            assert(slotBits <= 24u); // 12-bit digits, at most two passes
            const uint32_t w = 12u;
            const uint32_t numPasses = (slotBits + w - 1u) / w;

            std::vector<uint32_t> hist(std::size_t(numChunks) * (1u << w), 0u);

            uint64_t *src = a;
            uint64_t *dst = b;
            for (uint32_t pass = 0; pass < numPasses; ++pass)
            {
                const uint32_t shift = 32u + pass * w;
                const uint32_t pw =
                    std::min(w, slotBits - pass * w); // last pass may be narrow
                const uint64_t pmask = (1ull << pw) - 1ull;

                tbb::parallel_for(
                    tbb::blocked_range<uint32_t>(0u, numChunks),
                    [&](const tbb::blocked_range<uint32_t> &chunks)
                    {
                        for (uint32_t c = chunks.begin(); c != chunks.end(); ++c)
                        {
                            uint32_t *row = &hist[std::size_t(c) << w];
                            const uint32_t r0 = c * kRadixChunk;
                            const uint32_t r1 =
                                std::min(r0 + kRadixChunk, n);
                            for (uint32_t i = r0; i < r1; ++i)
                                row[(src[i] >> shift) & pmask]++;
                        }
                    });

                // Exclusive scan.
                uint32_t run = 0;
                const uint32_t digits = 1u << pw;
                for (uint32_t d = 0; d < digits; ++d)
                    for (uint32_t c = 0; c < numChunks; ++c)
                    {
                        uint32_t *cell = &hist[std::size_t(c) << w] + d;
                        const uint32_t cnt = *cell;
                        *cell = run;
                        run += cnt;
                    }
                assert(run == n);

                tbb::parallel_for(
                    tbb::blocked_range<uint32_t>(0u, numChunks),
                    [&](const tbb::blocked_range<uint32_t> &chunks)
                    {
                        for (uint32_t c = chunks.begin(); c != chunks.end(); ++c)
                        {
                            uint32_t *row = &hist[std::size_t(c) << w];
                            const uint32_t r0 = c * kRadixChunk;
                            const uint32_t r1 =
                                std::min(r0 + kRadixChunk, n);
                            for (uint32_t i = r0; i < r1; ++i)
                                dst[row[(src[i] >> shift) & pmask]++] = src[i];
                        }
                    });

                if (pass + 1u < numPasses)
                    std::fill(hist.begin(), hist.end(), 0u);

                std::swap(src, dst);
            }
            return src;
        }

        // Parallel point build.
        void BuildPointsParallel(const uint32_t *records, uint32_t count)
        {
            const uint32_t numChunks =
                (count + kRadixChunk - 1u) / kRadixChunk;

            HugeVector<uint64_t> pairsA;
            HugeVector<uint64_t> pairsB;
            pairsA.resize(std::size_t(count));
            pairsB.resize(std::size_t(count));

            // Parallel keys over fixed chunks.
            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, numChunks),
                [&](const tbb::blocked_range<uint32_t> &chunks)
                {
                    for (uint32_t c = chunks.begin(); c != chunks.end(); ++c)
                    {
                        const uint32_t r0 = c * kRadixChunk;
                        const uint32_t r1 = std::min(r0 + kRadixChunk, count);
                        for (uint32_t r = r0; r < r1; ++r)
                        {
                            const uint32_t h = ScalarSlot(
                                                   records + std::size_t(r) * WORDS) &
                                               m_slotMask;
                            pairsA[r] = (uint64_t(h) << 32) | uint64_t(r);
                        }
                    }
                });

            const uint64_t *sorted = RadixSortBySlot(
                pairsA.data(), pairsB.data(), count, SlotBits());

            // cellStart.
            m_cellStart.assign(m_slots + 1u, 0u);
            tbb::parallel_for(
                tbb::blocked_range<uint32_t>(0u, m_slots),
                [&](const tbb::blocked_range<uint32_t> &slotRange)
                {
                    for (uint32_t s = slotRange.begin(); s != slotRange.end();
                         ++s)
                        m_cellStart[s] = uint32_t(std::lower_bound(
                                                      sorted, sorted + count,
                                                      uint64_t(s) << 32) -
                                                  sorted);
                });
            m_cellStart[m_slots] = count;

            // Payload permutation.
            m_payload.assign(std::size_t(count) * WORDS, 0u);
            tbb::parallel_for(
                tbb::blocked_range<std::size_t>(0u, std::size_t(count)),
                [&](const tbb::blocked_range<std::size_t> &posRange)
                {
                    std::size_t pos = posRange.begin();
                    if (pos > 0)
                        _mm_prefetch(
                            reinterpret_cast<const char *>(&sorted[pos - 1]),
                            _MM_HINT_T0);
                    for (; pos != posRange.end(); ++pos)
                    {
                        if (pos + 8 < posRange.end())
                            _mm_prefetch(
                                reinterpret_cast<const char *>(
                                    &sorted[pos + 8]),
                                _MM_HINT_T0);
                        const uint32_t idx = uint32_t(sorted[pos]);
                        std::memcpy(&m_payload[pos * WORDS],
                                    records + std::size_t(idx) * WORDS,
                                    std::size_t(WORDS) * sizeof(uint32_t));
                    }
                });
        }

        // Bucket walk shared by both query modes.
        template <bool Bucketed, bool PerRecordR, class VisitFn>
        void WalkBucket(const UInt32P &h, const MaskP &work,
                        const Point3fP &p, const FloatP &radius2,
                        uint32_t radiusWord, const VisitFn &visit) const
        {
            const UInt32P i0 =
                enoki::gather<UInt32P>(m_cellStart.data(), h, work);
            const UInt32P i1 = enoki::gather<UInt32P>(
                m_cellStart.data(), h + UInt32P(1u), work);
            UInt32P cur = i0;

            while (enoki::any(work && cur < i1))
            {
                const MaskP lane = work && (cur < i1);
                const UInt32P rec = Bucketed
                                        ? enoki::gather<UInt32P>(m_bucket.data(), cur, lane)
                                        : cur;

                // Packet typed gathers.
                const UInt32P off = rec * uint32_t(WORDS);
                const FloatP dx =
                    enoki::gather<FloatP>(PosF(), off, lane) - p.x();
                const FloatP dy =
                    enoki::gather<FloatP>(PosF(), off + 1u, lane) - p.y();
                const FloatP dz =
                    enoki::gather<FloatP>(PosF(), off + 2u, lane) - p.z();
                const FloatP d2 = dx * dx + dy * dy + dz * dz;

                FloatP lim2 = radius2;
                if (PerRecordR)
                {
                    const FloatP r = enoki::gather<FloatP>(
                        PosF(), off + radiusWord, lane);
                    lim2 = r * r;
                }
                const MaskP hit = lane && (d2 <= lim2);

                if (enoki::any(hit))
                {
                    alignas(64) uint32_t recA[SimdLanes];
                    enoki::store_unaligned(
                        recA, enoki::select(hit, rec, UInt32P(0u)));
                    alignas(64) SimdVec f[WORDS];
                    LoadRecords<int(WORDS)>(f, m_payload.data(), recA,
                                            LaneBits(hit));
                    visit(HashGridFields{f}, hit);
                }
                cur = cur + UInt32P(1u);
            }
        }

        Mode m_mode = Mode::None;
        uint32_t m_count = 0;
        uint32_t m_slots = 0;       // power of two
        uint32_t m_slotMask = 0;    // m_slots - 1
        uint32_t m_radiusWord = 0;
        float m_radius = 0.f;       // build radius (points) / max record R (spheres)
        float m_invCell = 0.f;
        Point3f m_min;              // scalar bbox min for the build path
        Point3fP m_minP, m_maxP;    // packet copy for the inside test

        HugeVector<uint32_t> m_payload;   // count * WORDS
        HugeVector<uint32_t> m_cellStart; // slots + 1
        HugeVector<uint32_t> m_bucket;    // sphere mode only
    };
} // namespace lux2

#endif // LUX2_CORE_SPATIAL_HASH_GRID_H
