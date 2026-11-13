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

// Wavefront vertex storage.

#ifndef LUX2_INTEGRATORS_WF_STORAGE_H
#define LUX2_INTEGRATORS_WF_STORAGE_H

#include "core/math/geometry.h"
#include "core/math/vecp.h"
#include "core/memory/huge_alloc.h"
#include "core/math/simd_transpose.h"

#include <enoki/array.h>

#include <cstdint>
#include <new>
#include <vector>

namespace lux2
{
    namespace wf
    {
        // Layout constants.
        constexpr int MAX_DEPTH = 128;
        // MIS indexing uses this constant. TODO: widen
        // to 16 (alignas(64)) for VCM recurrences?
        constexpr int MIS_WORDS = 8;

        // Vertex records per path slot.
        constexpr int PATH_PAD = 8;
        constexpr int PATH_STRIDE = MAX_DEPTH + PATH_PAD;
        static_assert(PATH_STRIDE % 32 != 0,
                      "geom path stride must not alias L1 sets");

        constexpr uint32_t LUX_INVALID_PATH = 0xFFFFFFFFu;

        // Addressing.
        template <int WORDS>
        inline UInt32P RecWord(const UInt32P &rec, uint32_t k)
        {
            return rec * UInt32P(uint32_t(WORDS)) + UInt32P(k);
        }

        // Gather/scatter at (rec, w0+off).
        inline UInt32P LoadWord(const uint32_t *base, const UInt32P &addr,
                                const MaskP &m)
        {
            return enoki::gather<UInt32P>(base, addr, m);
        }
        inline void StoreWord(uint32_t *base, const UInt32P &val,
                              const UInt32P &addr, const MaskP &m)
        {
            enoki::scatter(base, val, addr, m);
        }

// Field lists.
#define LUX2_GEOM_FIELDS(X)                                                   \
    X(V3, p, 0u)                                                              \
    X(V3, n, kNext_p)                                                         \
    X(V3, ng, kNext_n)                                                        \
    X(V3, wb, kNext_ng)                                                       \
    X(V3, dp_du, kNext_wb)                                                    \
    X(V3, dp_dv, kNext_dp_du)                                                 \
    X(V3, dp_ds, kNext_dp_dv)                                                 \
        X(V3, dp_dt, kNext_dp_ds) X(V2, uv, kNext_dp_dt) X(F, flux, kNext_uv) \
            X(F, k, kNext_flux) X(F, footprint, kNext_k)                      \
                X(F, spare0, kNext_footprint)

// Packed pairs.
#define LUX2_GEOM_PAIRS(X)        \
    X(matID, medID, kNext_spare0) \
    X(medFront, medBack, kNext_medID)

// Empty pair list for records with no packed fields.
#define LUX2_NO_PAIRS(X)

// 8 words = 32 B.
#define LUX2_MIS_FIELDS(X) \
    X(F, dAW, 0u)          \
    X(F, dARW, kNext_dAW)  \
    X(F, rr, kNext_dARW)   \
    X(F, rrR, kNext_rr)    \
    X(F, cosi, kNext_rrR)  \
    X(F, coso, kNext_cosi) \
    X(F, d2, kNext_coso)   \
        X(U, bits, kNext_d2)

// Code generation.
#define LUX2_CAT_(a, b) a##b
#define LUX2_CAT(a, b) LUX2_CAT_(a, b)

// Register form field declaration.
#define LUX2_DECL_V3(name) Point3fP name;
#define LUX2_DECL_V2(name) Point2fP name;
#define LUX2_DECL_F(name) FloatP name;
#define LUX2_DECL_U(name) UInt32P name;
#define LUX2_DECL(kind, name, prev) LUX2_CAT(LUX2_DECL_, kind)(name)

// Field width per kind.
#define LUX2_W_V3 3u
#define LUX2_W_V2 2u
#define LUX2_W_F 1u
#define LUX2_W_U 1u

// Word offset chain.
#define LUX2_WORD_V3(name, prev)              \
    constexpr uint32_t kWord_##name = (prev); \
    constexpr uint32_t LUX2_CAT(kNext_, name) = (prev) + 3u;
#define LUX2_WORD_V2(name, prev)              \
    constexpr uint32_t kWord_##name = (prev); \
    constexpr uint32_t LUX2_CAT(kNext_, name) = (prev) + 2u;
#define LUX2_WORD_F(name, prev)               \
    constexpr uint32_t kWord_##name = (prev); \
    constexpr uint32_t LUX2_CAT(kNext_, name) = (prev) + 1u;
#define LUX2_WORD_U(name, prev)               \
    constexpr uint32_t kWord_##name = (prev); \
    constexpr uint32_t LUX2_CAT(kNext_, name) = (prev) + 1u;
#define LUX2_WORD(kind, name, prev) LUX2_CAT(LUX2_WORD_, kind)(name, prev)

// Load one field with one gather per word.
#define LUX2_LOAD_V3(name)                                             \
    r.name = Point3fP(                                                 \
        reinterpret_array<FloatP>(                                     \
            LoadWord(base, RecWord<WORDS>(rec, kWord_##name), m)),     \
        reinterpret_array<FloatP>(                                     \
            LoadWord(base, RecWord<WORDS>(rec, kWord_##name + 1), m)), \
        reinterpret_array<FloatP>(                                     \
            LoadWord(base, RecWord<WORDS>(rec, kWord_##name + 2), m)));
#define LUX2_LOAD_V2(name)                                         \
    r.name = Point2fP(                                             \
        reinterpret_array<FloatP>(                                 \
            LoadWord(base, RecWord<WORDS>(rec, kWord_##name), m)), \
        reinterpret_array<FloatP>(                                 \
            LoadWord(base, RecWord<WORDS>(rec, kWord_##name + 1), m)));
#define LUX2_LOAD_F(name)               \
    r.name = reinterpret_array<FloatP>( \
        LoadWord(base, RecWord<WORDS>(rec, kWord_##name), m));
#define LUX2_LOAD_U(name) \
    r.name = LoadWord(base, RecWord<WORDS>(rec, kWord_##name), m);
#define LUX2_LOAD(kind, name, prev) LUX2_CAT(LUX2_LOAD_, kind)(name)

// Store one field.
#define LUX2_STORE_V3(name)                                  \
    {                                                        \
        const Point3fP _v = v.name;                          \
        StoreWord(base, reinterpret_array<UInt32P>(_v.x()),  \
                  RecWord<WORDS>(rec, kWord_##name), m);     \
        StoreWord(base, reinterpret_array<UInt32P>(_v.y()),  \
                  RecWord<WORDS>(rec, kWord_##name + 1), m); \
        StoreWord(base, reinterpret_array<UInt32P>(_v.z()),  \
                  RecWord<WORDS>(rec, kWord_##name + 2), m); \
    }
#define LUX2_STORE_V2(name)                                  \
    {                                                        \
        const Point2fP _v = v.name;                          \
        StoreWord(base, reinterpret_array<UInt32P>(_v.x()),  \
                  RecWord<WORDS>(rec, kWord_##name), m);     \
        StoreWord(base, reinterpret_array<UInt32P>(_v.y()),  \
                  RecWord<WORDS>(rec, kWord_##name + 1), m); \
    }
#define LUX2_STORE_F(name)                              \
    StoreWord(base, reinterpret_array<UInt32P>(v.name), \
              RecWord<WORDS>(rec, kWord_##name), m);
#define LUX2_STORE_U(name) \
    StoreWord(base, v.name, RecWord<WORDS>(rec, kWord_##name), m);
#define LUX2_STORE(kind, name, prev) LUX2_CAT(LUX2_STORE_, kind)(name)

// Packed pair field.
#define LUX2_DECL_PAIR(lo, hi, prev) \
    UInt32P lo;                      \
    UInt32P hi;
#define LUX2_WORD_PAIR(lo, hi, prev)        \
    constexpr uint32_t kWord_##lo = (prev); \
    constexpr uint32_t kWord_##hi = (prev); \
    constexpr uint32_t LUX2_CAT(kNext_, hi) = (prev) + 1u;
#define LUX2_LOAD_PAIR(lo, hi, prev)                                           \
    {                                                                          \
        const UInt32P _w = LoadWord(base, RecWord<WORDS>(rec, kWord_##lo), m); \
        r.lo = _w & UInt32P(0xFFFFu);                                          \
        r.hi = _w >> 16u;                                                      \
    }
#define LUX2_STORE_PAIR(lo, hi, prev)                                 \
    {                                                                 \
        const UInt32P _p = (v.lo & UInt32P(0xFFFFu)) | (v.hi << 16u); \
        StoreWord(base, _p, RecWord<WORDS>(rec, kWord_##lo), m);      \
    }

// Generate a record. FIELDS are ordinary per-word fields; PAIRS are packed
// two ID pairs.
#define LUX2_RECORD(Prefix, FIELDS, PAIRS)                                    \
    struct Vertex##Prefix##P                                                  \
    {                                                                         \
        FIELDS(LUX2_DECL)                                                     \
        PAIRS(LUX2_DECL_PAIR)                                                 \
    };                                                                        \
    FIELDS(LUX2_WORD)                                                         \
    PAIRS(LUX2_WORD_PAIR)                                                     \
    constexpr uint32_t Prefix##_WORDS = LUX2_CAT(kNext_, LUX2_LAST_##Prefix); \
    inline Vertex##Prefix##P Load##Prefix##_gather(                           \
        const uint32_t *base, const UInt32P &rec, const MaskP &m)             \
    {                                                                         \
        constexpr uint32_t WORDS = Prefix##_WORDS;                            \
        Vertex##Prefix##P r;                                                  \
        FIELDS(LUX2_LOAD)                                                     \
        PAIRS(LUX2_LOAD_PAIR)                                                 \
        return r;                                                             \
    }                                                                         \
    inline void Store##Prefix##_gather(uint32_t *base,                        \
                                       const Vertex##Prefix##P &v,            \
                                       const UInt32P &rec, const MaskP &m)    \
    {                                                                         \
        constexpr uint32_t WORDS = Prefix##_WORDS;                            \
        FIELDS(LUX2_STORE)                                                    \
        PAIRS(LUX2_STORE_PAIR)                                                \
    }

// The last field of each record.
#define LUX2_LAST_Geom medBack
#define LUX2_LAST_Mis bits

        LUX2_RECORD(Geom, LUX2_GEOM_FIELDS, LUX2_GEOM_PAIRS)
        LUX2_RECORD(Mis, LUX2_MIS_FIELDS, LUX2_NO_PAIRS)

        constexpr uint32_t GEOM_WORDS = Geom_WORDS;
        constexpr uint32_t MIS_WORDS_TOTAL = Mis_WORDS;

        // MIS always uses the gather form.
        inline VertexMisP LoadMis(const uint32_t *base, const UInt32P &rec,
                                  const MaskP &m)
        {
            return LoadMis_gather(base, rec, m);
        }
        inline void StoreMis(uint32_t *base, const VertexMisP &v,
                             const UInt32P &rec, const MaskP &m)
        {
            StoreMis_gather(base, v, rec, m);
        }

#if defined(__AVX512F__) || defined(__AVX2__)
        // Transposed geom record access.
        namespace geomx
        {
            using VecT = SimdVec;

            inline VecT FromU(const UInt32P &x)
            {
                return RecFromBits(x.m);
            }

            inline unsigned LaneBits(const MaskP &m)
            {
#if defined(__AVX512F__)
                return unsigned(m.k);
#else
                return unsigned(
                    _mm256_movemask_ps(_mm256_castsi256_ps(m.m)));
#endif
            }

            inline void LoadFields(VecT f[GEOM_WORDS], const uint32_t *base,
                                   const UInt32P &rec, const MaskP &m)
            {
                alignas(64) uint32_t recA[SimdLanes];
                enoki::store_unaligned(recA, rec);
                LoadRecords<int(GEOM_WORDS)>(f, base, recA, LaneBits(m));
            }

            inline void StoreFields(uint32_t *base, const VecT f[GEOM_WORDS],
                                    const UInt32P &rec, const MaskP &m)
            {
                alignas(64) uint32_t recA[SimdLanes];
                enoki::store_unaligned(recA, rec);
                StoreRecords<int(GEOM_WORDS)>(base, f, recA, LaneBits(m));
            }

            // Field word index -> register slot.
            inline FloatP F(const VecT f[GEOM_WORDS], uint32_t w)
            {
                return FloatP(f[w]);
            }
            inline UInt32P U(const VecT f[GEOM_WORDS], uint32_t w)
            {
                return UInt32P(RecBits(f[w]));
            }
            inline Point3fP V3(const VecT f[GEOM_WORDS], uint32_t w)
            {
                return Point3fP(F(f, w), F(f, w + 1), F(f, w + 2));
            }
            inline Point2fP V2(const VecT f[GEOM_WORDS], uint32_t w)
            {
                return Point2fP(F(f, w), F(f, w + 1));
            }
        } // namespace geomx

        static_assert(PACKET_WIDTH == SimdLanes,
                      "transposed geom access assumes a full-width packet");
#endif

#if defined(__AVX512F__) || defined(__AVX2__)
        // Shared record assembly.
        inline VertexGeomP LoadGeom(const uint32_t *base, const UInt32P &rec,
                                    const MaskP &m)
        {
            // One register per record word.
            alignas(64) geomx::VecT f[GEOM_WORDS];
            geomx::LoadFields(f, base, rec, m);
            VertexGeomP r;
            r.p = geomx::V3(f, kWord_p);
            r.n = geomx::V3(f, kWord_n);
            r.ng = geomx::V3(f, kWord_ng);
            r.wb = geomx::V3(f, kWord_wb);
            r.dp_du = geomx::V3(f, kWord_dp_du);
            r.dp_dv = geomx::V3(f, kWord_dp_dv);
            r.dp_ds = geomx::V3(f, kWord_dp_ds);
            r.dp_dt = geomx::V3(f, kWord_dp_dt);
            r.uv = geomx::V2(f, kWord_uv);
            r.flux = geomx::F(f, kWord_flux);
            r.k = geomx::F(f, kWord_k);
            r.footprint = geomx::F(f, kWord_footprint);
            r.spare0 = geomx::F(f, kWord_spare0);
            const UInt32P idA = geomx::U(f, kWord_matID);
            r.matID = idA & UInt32P(0xFFFFu);
            r.medID = idA >> 16u;
            const UInt32P idB = geomx::U(f, kWord_medFront);
            r.medFront = idB & UInt32P(0xFFFFu);
            r.medBack = idB >> 16u;
            return r;
        }

        inline void StoreGeom(uint32_t *base, const VertexGeomP &v,
                              const UInt32P &rec, const MaskP &m)
        {
            alignas(64) geomx::VecT f[GEOM_WORDS];
            auto put = [&](uint32_t w, const FloatP &x)
            { f[w] = x.m; };
            auto putU = [&](uint32_t w, const UInt32P &x)
            { f[w] = geomx::FromU(x); };
            put(kWord_p + 0, v.p.x());
            put(kWord_p + 1, v.p.y());
            put(kWord_p + 2, v.p.z());
            put(kWord_n + 0, v.n.x());
            put(kWord_n + 1, v.n.y());
            put(kWord_n + 2, v.n.z());
            put(kWord_ng + 0, v.ng.x());
            put(kWord_ng + 1, v.ng.y());
            put(kWord_ng + 2, v.ng.z());
            put(kWord_wb + 0, v.wb.x());
            put(kWord_wb + 1, v.wb.y());
            put(kWord_wb + 2, v.wb.z());
            put(kWord_dp_du + 0, v.dp_du.x());
            put(kWord_dp_du + 1, v.dp_du.y());
            put(kWord_dp_du + 2, v.dp_du.z());
            put(kWord_dp_dv + 0, v.dp_dv.x());
            put(kWord_dp_dv + 1, v.dp_dv.y());
            put(kWord_dp_dv + 2, v.dp_dv.z());
            put(kWord_dp_ds + 0, v.dp_ds.x());
            put(kWord_dp_ds + 1, v.dp_ds.y());
            put(kWord_dp_ds + 2, v.dp_ds.z());
            put(kWord_dp_dt + 0, v.dp_dt.x());
            put(kWord_dp_dt + 1, v.dp_dt.y());
            put(kWord_dp_dt + 2, v.dp_dt.z());
            put(kWord_uv + 0, v.uv.x());
            put(kWord_uv + 1, v.uv.y());
            put(kWord_flux, v.flux);
            put(kWord_k, v.k);
            put(kWord_footprint, v.footprint);
            put(kWord_spare0, v.spare0);
            putU(kWord_matID, (v.matID & UInt32P(0xFFFFu)) | (v.medID << 16u));
            putU(kWord_medFront,
                 (v.medFront & UInt32P(0xFFFFu)) | (v.medBack << 16u));
            geomx::StoreFields(base, f, rec, m);
        }
#else
        // Only gather/scatter here.
        inline VertexGeomP LoadGeom(const uint32_t *base, const UInt32P &rec,
                                    const MaskP &m)
        {
            return LoadGeom_gather(base, rec, m);
        }
        inline void StoreGeom(uint32_t *base, const VertexGeomP &v,
                              const UInt32P &rec, const MaskP &m)
        {
            StoreGeom_gather(base, v, rec, m);
        }
#endif

        static_assert(GEOM_WORDS * 4 % 64 == 0,
                      "geom record must be cache-line aligned");
        static_assert(MIS_WORDS_TOTAL * 4 == 32, "mis record must be 32 B");

        // Vertex arena.
        static_assert(PATH_STRIDE * GEOM_WORDS % 16 == 0);
        static_assert(PATH_STRIDE * MIS_WORDS % 16 == 0,
                      "mis record starts must stay 64B aligned");
        struct BlockArena
        {
            HugeVector<uint32_t> geomWords; // [nPaths * PATH_STRIDE * GEOM_WORDS]
            HugeVector<uint32_t> misWords;  // [nPaths * PATH_STRIDE * MIS_WORDS]

            // Size the arena once per capacity change.
            void Init(uint32_t nPaths)
            {
                const size_t recs = size_t(nPaths) * PATH_STRIDE;
                if (geomWords.size() != recs * GEOM_WORDS)
                    geomWords.resize(recs * GEOM_WORDS);
                if (misWords.size() != recs * MIS_WORDS)
                    misWords.resize(recs * MIS_WORDS);
            }
        };

        // Path store.
        struct PathStore
        {
            BlockArena *arena = nullptr;
            std::vector<uint32_t> n;        // vertices written per path
            std::vector<uint32_t> owner;    // sample id
            std::vector<uint32_t> lightIdx; // lightPtrs index
            std::vector<float> directPdf1, selPdf;
            std::vector<uint32_t> freePaths;
            // Light path -> its index j in the owning sample's light list.
            std::vector<uint32_t> lightSlot;

            void Init(BlockArena *a, uint32_t nPaths)
            {
                arena = a;
                a->Init(nPaths);
                n.assign(nPaths, 0u);
                owner.assign(nPaths, 0u);
                lightIdx.assign(nPaths, 0u);
                directPdf1.assign(nPaths, 0.f);
                selPdf.assign(nPaths, 0.f);
                lightSlot.assign(nPaths, LUX_INVALID_PATH);
                freePaths.clear();
                for (uint32_t p = nPaths; p-- > 0;)
                    freePaths.push_back(p);
            }
        };

        // Record index for (path, vertex).
        inline UInt32P RecIdx(const PathStore &, const UInt32P &path,
                              const Int32P &v, const MaskP &)
        {
            return path * UInt32P(uint32_t(PATH_STRIDE)) + UInt32P(v);
        }

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_STORAGE_H
