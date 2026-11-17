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

// A chain deposits energy at arbitrary pixels, so deposits cannot go
// straight into a shared film. Entry = (pixel -> X, Y, Z, proposals).

#ifndef LUX2_INTEGRATORS_ERPT_SPARSE_ACCUM_H
#define LUX2_INTEGRATORS_ERPT_SPARSE_ACCUM_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lux2
{
    namespace erpt
    {
        struct SparseEntry
        {
            uint64_t key = 0u;                 // 0 = empty; else ((y << 32) | x) + 1
            float v[4] = {0.f, 0.f, 0.f, 0.f}; // X, Y, Z, proposals
        };

        class SparseAccum
        {
        public:
            explicit SparseAccum(size_t initialCapacity = 1024)
            {
                Reserve(initialCapacity);
            }

            // x,y must be >= 0.
            void Add(int32_t x, int32_t y, float X, float Y, float Z, float props = 0.f)
            {
                SparseEntry &e = Slot(Key(x, y));
                e.v[0] += X;
                e.v[1] += Y;
                e.v[2] += Z;
                e.v[3] += props;
            }

            size_t Size() const { return m_size; }
            size_t Bytes() const { return m_tab.size() * sizeof(SparseEntry); }
            bool Empty() const { return m_size == 0u; }

            // fn(int32 x, int32 y, const float v[4]).
            template <class F>
            void ForEach(F &&fn) const
            {
                for (const SparseEntry &e : m_tab)
                    if (e.key)
                    {
                        const uint64_t k = e.key - 1u;
                        fn(int32_t(k & 0xFFFFFFFFu), int32_t(k >> 32), e.v);
                    }
            }

            void Clear()
            {
                for (SparseEntry &e : m_tab)
                    e = SparseEntry{};
                m_size = 0u;
            }

        private:
            static uint64_t Key(int32_t x, int32_t y)
            {
                return ((uint64_t(uint32_t(y)) << 32) | uint32_t(x)) + 1u;
            }
            static size_t Hash(uint64_t k)
            {
                k ^= k >> 33;
                k *= 0xff51afd7ed558ccdULL;
                k ^= k >> 33;
                k *= 0xc4ceb9fe1a85ec53ULL;
                k ^= k >> 33;
                return size_t(k);
            }
            void Reserve(size_t want)
            {
                size_t cap = 16;
                while (cap < want * 2u)
                    cap <<= 1u;
                std::vector<SparseEntry> old;
                old.swap(m_tab);
                m_tab.assign(cap, SparseEntry{});
                m_size = 0u;
                for (const SparseEntry &e : old)
                    if (e.key)
                    {
                        SparseEntry &d = Slot(e.key);
                        for (int i = 0; i < 4; ++i)
                            d.v[i] = e.v[i];
                    }
            }
            SparseEntry &Slot(uint64_t key)
            {
                if ((m_size + 1u) * 2u > m_tab.size())
                    Reserve(m_tab.size()); // doubles: cap >= want*2
                const size_t mask = m_tab.size() - 1u;
                size_t i = Hash(key) & mask;
                for (;;)
                {
                    SparseEntry &e = m_tab[i];
                    if (e.key == key)
                        return e;
                    if (e.key == 0u)
                    {
                        e.key = key;
                        ++m_size;
                        return e;
                    }
                    i = (i + 1u) & mask;
                }
            }

            std::vector<SparseEntry> m_tab;
            size_t m_size = 0u;
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_SPARSE_ACCUM_H
