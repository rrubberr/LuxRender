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

#include <array>
#include <cstdint>
#include <new>
#include <type_traits>
#include <vector>

namespace lux2
{
    namespace wf
    {
        // Layout constants.
        constexpr int MAX_DEPTH = 128;

        // Record tiles.
        constexpr uint32_t GEOM_TILE_WORDS = 32;
        constexpr uint32_t MIS_TILE_WORDS = 16;

        // Vertex records per path slot. The stride is sized per render from the
        // configured bounce count.
        constexpr int PATH_PAD = 8;
        inline constexpr uint32_t PathStrideFor(int maxDepth)
        {
            uint32_t s = uint32_t(maxDepth < 1 ? 1 : maxDepth);
            if (s & 1u)
                ++s; // records are 64B each
            if (s % 32u == 0u)
                s += uint32_t(PATH_PAD); // L1 set antialiasing
            return s;
        }
        // Default / maximum stride.
        constexpr uint32_t PATH_STRIDE = PathStrideFor(MAX_DEPTH);
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

        // Field kinds and per-kind traits: value type and word width live here once.
        struct K_V3
        {
        };
        struct K_V2
        {
        };
        struct K_F
        {
        };
        struct K_U
        {
        };
        struct K_Pair
        {
        }; // two u16 halves packed into one word

        template <class K>
        struct FieldInfo;
        template <>
        struct FieldInfo<K_V3>
        {
            using Value = Point3fP;
            static constexpr uint32_t Width = 3u;
        };
        template <>
        struct FieldInfo<K_V2>
        {
            using Value = Point2fP;
            static constexpr uint32_t Width = 2u;
        };
        template <>
        struct FieldInfo<K_F>
        {
            using Value = FloatP;
            static constexpr uint32_t Width = 1u;
        };
        template <>
        struct FieldInfo<K_U>
        {
            using Value = UInt32P;
            static constexpr uint32_t Width = 1u;
        };
        template <>
        struct FieldInfo<K_Pair>
        {
            using Value = UInt32P;
            static constexpr uint32_t Width = 1u;
        };

        // A packed u16 pair, loaded/stored as one word. Both halves always
        // travel together: a single-half scatter would clobber the other half.
        struct PairU16
        {
            UInt32P lo, hi;
        };

        // Field tag: kind + word offset + record word count + member pointer(s).
        template <class K, uint32_t W, uint32_t REC_W, auto M>
        struct FieldTag
        {
            using Kind = K;
            static constexpr uint32_t Word = W;
            static constexpr uint32_t RecWords = REC_W;
            static constexpr auto Member = M;
        };
        template <uint32_t W, uint32_t REC_W, auto LoP, auto HiP>
        struct PairTag
        {
            using Kind = K_Pair;
            static constexpr uint32_t Word = W;
            static constexpr uint32_t RecWords = REC_W;
            static constexpr auto LoM = LoP;
            static constexpr auto HiM = HiP;
        };

        struct TagEnd
        {
        }; // list terminator, skipped by ForEachTag
        template <class... Ts>
        struct TagList
        {
        };
        template <class T, class F>
        ENOKI_INLINE constexpr void ApplyOne(F &f)
        {
            if constexpr (!std::is_same_v<T, TagEnd>)
                f(T{});
        }
        template <class... Ts, class F>
        ENOKI_INLINE constexpr void ForEachTag(TagList<Ts...>, F &&f)
        {
            (ApplyOne<Ts>(f), ...);
        }

        // Prefix sum of record word widths; Off[i] is field i's word offset,
        // Off[N] the total. N is size_t to match std::array's bound type.
        template <size_t N>
        inline constexpr std::array<uint32_t, N + 1>
        PrefixSum(const std::array<uint32_t, N> &w)
        {
            std::array<uint32_t, N + 1> o{};
            for (size_t i = 0; i < N; ++i)
                o[i + 1] = o[i] + w[i];
            return o;
        }

// Field lists: (kind, name). One row generates the member, its word offset
// and its tag. Insert a field by adding one row.
#define LUX2_GEOM_FIELDS(X)                                    \
    X(V3, p)                                                   \
    X(V3, n)                                                   \
    X(V3, ng) X(V3, wb) X(V3, dp_du) X(V3, dp_dv)              \
        X(V3, dp_ds) X(V3, dp_dt) X(V2, uv) X(F, flux) X(F, k) \
            X(F, footprint) X(F, spare0)

// Packed pairs.
#define LUX2_GEOM_PAIRS(X) X(matID, medID) X(medFront, medBack)

// Empty pair list for records with no packed fields.
#define LUX2_NO_PAIRS(X)

// 8 words = 32 B.
#define LUX2_MIS_FIELDS(X)                   \
    X(F, dAW)                                \
    X(F, dARW)                               \
    X(F, rr) X(F, rrR) X(F, cosi) X(F, coso) \
        X(F, d2) X(U, bits)

// Code generation.
#define LUX2_CAT_(a, b) a##b
#define LUX2_CAT(a, b) LUX2_CAT_(a, b)

#define LUX2_KIND_V3 K_V3
#define LUX2_KIND_V2 K_V2
#define LUX2_KIND_F K_F
#define LUX2_KIND_U K_U

// Member declaration.
#define LUX2_DECL(kind, name) \
    typename FieldInfo<LUX2_CAT(LUX2_KIND_, kind)>::Value name;
#define LUX2_DECL_PAIR(lo, hi) \
    UInt32P lo;                \
    UInt32P hi;

// Index enum + width array (one slot per field; a pair takes one).
#define LUX2_IDX(kind, name) kIdx_##name,
#define LUX2_IDX_PAIR(lo, hi) kIdx_##lo,
#define LUX2_WIDTH(kind, name) FieldInfo<LUX2_CAT(LUX2_KIND_, kind)>::Width,
#define LUX2_WIDTH_PAIR(lo, hi) FieldInfo<K_Pair>::Width,

// Tags. Off/kIdx are earlier members of the same namespace.
#define LUX2_TAG(kind, name)                                             \
    struct name : FieldTag<LUX2_CAT(LUX2_KIND_, kind), Off[kIdx_##name], \
                           kTile, &Rec::name>                            \
    {                                                                    \
    };
#define LUX2_TAG_PAIR(lo, hi)                                      \
    struct lo : PairTag<Off[kIdx_##lo], kTile, &Rec::lo, &Rec::hi> \
    {                                                              \
    };
#define LUX2_TAGREF(kind, name) name,
#define LUX2_TAGREF_PAIR(lo, hi) lo,

// Whole record struct + tag namespace (offsets, tags, tag list).
#define LUX2_RECORD(Prefix, FIELDS, PAIRS, TILE)                              \
    static_assert(TILE % 16 == 0,                                             \
                  #Prefix " tile must be a multiple of 16 words");            \
    struct Vertex##Prefix##P                                                  \
    {                                                                         \
        FIELDS(LUX2_DECL)                                                     \
        PAIRS(LUX2_DECL_PAIR)                                                 \
    };                                                                        \
    namespace Prefix##Tag                                                     \
    {                                                                         \
        using Rec = Vertex##Prefix##P;                                        \
        inline constexpr uint32_t kTile = TILE;                               \
        enum Idx : uint32_t                                                   \
        {                                                                     \
            FIELDS(LUX2_IDX)                                                  \
            PAIRS(LUX2_IDX_PAIR) kCount                                       \
        };                                                                    \
        inline constexpr auto Off = PrefixSum(                                \
            std::array<uint32_t, kCount>{                                     \
                FIELDS(LUX2_WIDTH) PAIRS(LUX2_WIDTH_PAIR)});                  \
        static_assert(Off[kCount] <= TILE,                                    \
                      #Prefix " fields exceed the record tile: bump " #TILE); \
        FIELDS(LUX2_TAG)                                                      \
        PAIRS(LUX2_TAG_PAIR)                                                  \
        using All = TagList<FIELDS(LUX2_TAGREF) PAIRS(LUX2_TAGREF_PAIR)       \
                                TagEnd>;                                      \
    }

        LUX2_RECORD(Geom, LUX2_GEOM_FIELDS, LUX2_GEOM_PAIRS, GEOM_TILE_WORDS)
        LUX2_RECORD(Mis, LUX2_MIS_FIELDS, LUX2_NO_PAIRS, MIS_TILE_WORDS)

        constexpr uint32_t GEOM_WORDS = GEOM_TILE_WORDS;
        constexpr uint32_t MIS_WORDS = MIS_TILE_WORDS;

        // Gather one field by tag. Returns the field's natural SIMD type
        // (including packed pairs of uint16)!
        template <class T>
        inline auto LoadField(const uint32_t *base, const UInt32P &rec,
                              const MaskP &m)
        {
            using K = typename T::Kind;
            if constexpr (std::is_same_v<K, K_V3>)
                return Point3fP(
                    reinterpret_array<FloatP>(
                        LoadWord(base, RecWord<T::RecWords>(rec, T::Word), m)),
                    reinterpret_array<FloatP>(LoadWord(
                        base, RecWord<T::RecWords>(rec, T::Word + 1), m)),
                    reinterpret_array<FloatP>(LoadWord(
                        base, RecWord<T::RecWords>(rec, T::Word + 2), m)));
            else if constexpr (std::is_same_v<K, K_V2>)
                return Point2fP(
                    reinterpret_array<FloatP>(
                        LoadWord(base, RecWord<T::RecWords>(rec, T::Word), m)),
                    reinterpret_array<FloatP>(LoadWord(
                        base, RecWord<T::RecWords>(rec, T::Word + 1), m)));
            else if constexpr (std::is_same_v<K, K_F>)
                return reinterpret_array<FloatP>(
                    LoadWord(base, RecWord<T::RecWords>(rec, T::Word), m));
            else if constexpr (std::is_same_v<K, K_U>)
                return LoadWord(base, RecWord<T::RecWords>(rec, T::Word), m);
            else // K_Pair
            {
                const UInt32P w =
                    LoadWord(base, RecWord<T::RecWords>(rec, T::Word), m);
                return PairU16{w & UInt32P(0xFFFFu), w >> 16u};
            }
        }

        // Masked scatter of one field by tag. Pairs write both halves in a
        // single word.
        template <class T, class V>
        inline void StoreField(uint32_t *base, const V &v, const UInt32P &rec,
                               const MaskP &m)
        {
            using K = typename T::Kind;
            if constexpr (std::is_same_v<K, K_V3>)
            {
                StoreWord(base, reinterpret_array<UInt32P>(v.x()),
                          RecWord<T::RecWords>(rec, T::Word), m);
                StoreWord(base, reinterpret_array<UInt32P>(v.y()),
                          RecWord<T::RecWords>(rec, T::Word + 1), m);
                StoreWord(base, reinterpret_array<UInt32P>(v.z()),
                          RecWord<T::RecWords>(rec, T::Word + 2), m);
            }
            else if constexpr (std::is_same_v<K, K_V2>)
            {
                StoreWord(base, reinterpret_array<UInt32P>(v.x()),
                          RecWord<T::RecWords>(rec, T::Word), m);
                StoreWord(base, reinterpret_array<UInt32P>(v.y()),
                          RecWord<T::RecWords>(rec, T::Word + 1), m);
            }
            else if constexpr (std::is_same_v<K, K_F>)
                StoreWord(base, reinterpret_array<UInt32P>(v),
                          RecWord<T::RecWords>(rec, T::Word), m);
            else if constexpr (std::is_same_v<K, K_U>)
                StoreWord(base, v, RecWord<T::RecWords>(rec, T::Word), m);
            else // K_Pair value is the packed word
                StoreWord(base, v, RecWord<T::RecWords>(rec, T::Word), m);
        }

        // Transposed record access (shared by geom and mis).
        namespace recx
        {
            using VecT = SimdVec;

            inline VecT FromU(const UInt32P &x)
            {
                return RecFromBits(x.m);
            }


            template <uint32_t WORDS>
            inline void LoadFields(VecT f[WORDS], const uint32_t *base,
                                   const UInt32P &rec, const MaskP &m)
            {
                alignas(64) uint32_t recA[SimdLanes];
                enoki::store_unaligned(recA, rec);
                LoadRecords<int(WORDS)>(f, base, recA, LaneBits(m));
            }

            template <uint32_t WORDS>
            inline void StoreFields(uint32_t *base, const VecT f[WORDS],
                                    const UInt32P &rec, const MaskP &m)
            {
                alignas(64) uint32_t recA[SimdLanes];
                enoki::store_unaligned(recA, rec);
                StoreRecords<int(WORDS)>(base, f, recA, LaneBits(m));
            }

            // Field word index -> register slot (width-independent).
            inline FloatP F(const VecT f[], uint32_t w)
            {
                return FloatP(f[w]);
            }
            inline UInt32P U(const VecT f[], uint32_t w)
            {
                return UInt32P(RecBits(f[w]));
            }
            inline Point3fP V3(const VecT f[], uint32_t w)
            {
                return Point3fP(F(f, w), F(f, w + 1), F(f, w + 2));
            }
            inline Point2fP V2(const VecT f[], uint32_t w)
            {
                return Point2fP(F(f, w), F(f, w + 1));
            }
        } // namespace recx

        static_assert(PACKET_WIDTH == SimdLanes,
                      "transposed record access assumes a full-width packet");

        // Read one field out of the transposed register array into a record.
        template <class T, class Rec>
        ENOKI_INLINE void PutReg(Rec &r, const recx::VecT f[], T)
        {
            using K = typename T::Kind;
            if constexpr (std::is_same_v<K, K_V3>)
                r.*(T::Member) = recx::V3(f, T::Word);
            else if constexpr (std::is_same_v<K, K_V2>)
                r.*(T::Member) = recx::V2(f, T::Word);
            else if constexpr (std::is_same_v<K, K_F>)
                r.*(T::Member) = recx::F(f, T::Word);
            else if constexpr (std::is_same_v<K, K_U>)
                r.*(T::Member) = recx::U(f, T::Word);
            else // K_Pair: unpack both halves from one word.
            {
                const UInt32P w = recx::U(f, T::Word);
                r.*(T::LoM) = w & UInt32P(0xFFFFu);
                r.*(T::HiM) = w >> 16u;
            }
        }

        // Write one field of a record into the transposed register array.
        template <class Rec, class T>
        ENOKI_INLINE void GetReg(const Rec &v, recx::VecT f[], T)
        {
            using K = typename T::Kind;
            if constexpr (std::is_same_v<K, K_V3>)
            {
                f[T::Word] = (v.*(T::Member)).x().m;
                f[T::Word + 1] = (v.*(T::Member)).y().m;
                f[T::Word + 2] = (v.*(T::Member)).z().m;
            }
            else if constexpr (std::is_same_v<K, K_V2>)
            {
                f[T::Word] = (v.*(T::Member)).x().m;
                f[T::Word + 1] = (v.*(T::Member)).y().m;
            }
            else if constexpr (std::is_same_v<K, K_F>)
                f[T::Word] = (v.*(T::Member)).m;
            else if constexpr (std::is_same_v<K, K_U>)
                f[T::Word] = recx::FromU(v.*(T::Member));
            else // K_Pair pack both halves into one word.
                f[T::Word] = recx::FromU(((v.*(T::LoM)) & UInt32P(0xFFFFu)) |
                                         ((v.*(T::HiM)) << 16u));
        }

        // Shared record assembly.
        inline VertexGeomP LoadGeom(const uint32_t *base, const UInt32P &rec,
                                    const MaskP &m)
        {
            alignas(64) recx::VecT f[GEOM_WORDS];
            recx::LoadFields<GEOM_WORDS>(f, base, rec, m);
            VertexGeomP r;
            ForEachTag(GeomTag::All{}, [&](auto t)
                       { PutReg(r, f, t); });
            return r;
        }

        inline void StoreGeom(uint32_t *base, const VertexGeomP &v,
                              const UInt32P &rec, const MaskP &m)
        {
            alignas(64) recx::VecT f[GEOM_WORDS];
            ForEachTag(GeomTag::All{}, [&](auto t)
                       { GetReg(v, f, t); });
            recx::StoreFields<GEOM_WORDS>(base, f, rec, m);
        }

        // MIS whole record transposed fold.
        inline VertexMisP LoadMis(const uint32_t *base, const UInt32P &rec,
                                  const MaskP &m)
        {
            alignas(64) recx::VecT f[MIS_WORDS];
            recx::LoadFields<MIS_WORDS>(f, base, rec, m);
            VertexMisP r;
            ForEachTag(MisTag::All{}, [&](auto t)
                       { PutReg(r, f, t); });
            return r;
        }

        inline void StoreMis(uint32_t *base, const VertexMisP &v,
                             const UInt32P &rec, const MaskP &m)
        {
            alignas(64) recx::VecT f[MIS_WORDS] = {};
            ForEachTag(MisTag::All{}, [&](auto t)
                       { GetReg(v, f, t); });
            recx::StoreFields<MIS_WORDS>(base, f, rec, m);
        }

        static_assert(GEOM_WORDS * 4 % 64 == 0,
                      "geom record must be cache-line aligned");
        static_assert(MIS_WORDS * 4 == 64, "mis record must be 64 B");

        // A mid record insertion fires this.
        static_assert(GeomTag::p::Word == 0 && GeomTag::n::Word == 3 &&
                          GeomTag::ng::Word == 6 && GeomTag::wb::Word == 9 &&
                          GeomTag::dp_du::Word == 12 &&
                          GeomTag::dp_dv::Word == 15 &&
                          GeomTag::dp_ds::Word == 18 &&
                          GeomTag::dp_dt::Word == 21 &&
                          GeomTag::uv::Word == 24,
                      "geom tile-0 layout moved");
        static_assert(GeomTag::flux::Word == 26 && GeomTag::k::Word == 27 &&
                          GeomTag::footprint::Word == 28 &&
                          GeomTag::spare0::Word == 29 &&
                          GeomTag::matID::Word == 30 &&
                          GeomTag::medFront::Word == 31,
                      "geom tile-1 layout moved");
        // A mid record insertion fires this.
        static_assert(MisTag::dAW::Word == 0 && MisTag::dARW::Word == 1 &&
                          MisTag::rr::Word == 2 && MisTag::rrR::Word == 3 &&
                          MisTag::cosi::Word == 4 && MisTag::coso::Word == 5 &&
                          MisTag::d2::Word == 6 && MisTag::bits::Word == 7,
                      "mis layout moved");
        static_assert(GEOM_WORDS == 32 && MIS_WORDS == 16,
                      "record word count changed");

#ifdef LUX2_INTEGRATOR_DEBUG
        // Glass diagnostic words.
        constexpr uint32_t DIAG_WORDS = 8;
#endif

        // Vertex arena.
        struct BlockArena
        {
            HugeVector<uint32_t> geomWords; // [nPaths * stride * GEOM_WORDS]
            HugeVector<uint32_t> misWords;  // [nPaths * stride * MIS_WORDS]
#ifdef LUX2_INTEGRATOR_DEBUG
            HugeVector<uint32_t> diagWords; // [nPaths * stride * DIAG_WORDS]
#endif
            // Vertex records per path slot.
            uint32_t stride = PATH_STRIDE;

            // Size the arena once per capacity or depth change.
            void Init(uint32_t nPaths, int maxDepth = MAX_DEPTH)
            {
                const uint32_t s = PathStrideFor(maxDepth);
                const size_t recs = size_t(nPaths) * s;
                if (geomWords.size() != recs * GEOM_WORDS || stride != s)
                    geomWords.resize(recs * GEOM_WORDS);
                if (misWords.size() != recs * MIS_WORDS || stride != s)
                    misWords.resize(recs * MIS_WORDS);
#ifdef LUX2_INTEGRATOR_DEBUG
                if (diagWords.size() != recs * DIAG_WORDS || stride != s)
                    diagWords.resize(recs * DIAG_WORDS);
#endif
                stride = s;
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

            void Init(BlockArena *a, uint32_t nPaths, int maxDepth = MAX_DEPTH)
            {
                arena = a;
                a->Init(nPaths, maxDepth);
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
        inline UInt32P RecIdx(const PathStore &ps, const UInt32P &path,
                              const Int32P &v, const MaskP &)
        {
            return path * UInt32P(ps.arena->stride) + UInt32P(v);
        }

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_STORAGE_H
