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

// Worker seed path storage.

#ifndef LUX2_INTEGRATORS_ERPT_SEED_POOL_H
#define LUX2_INTEGRATORS_ERPT_SEED_POOL_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lux2
{
    namespace erpt
    {
        struct SeedHeader
        {
            float e = 0.f;            // MIS weighted seed energy, > 0
            float wavelength = 0.f;   // the sample's wavelength
            uint32_t seedKey = 0u;    // sampler stream key
            uint32_t sidx = 0u;       // sample index
            uint32_t n = 0u;          // vertex count, camera .. emitter
            uint32_t lightIdx = 0u;   // index into WalkCtx::lightPtrs
            float px = 0.f, py = 0.f; // raster position of the seed path
            uint32_t reserved = 0u;
        };

        class SeedPool
        {
        public:
            // wordsPerVertex = wf::GEOM_WORDS. Passed in so this header stays
            // free of renderer includes.
            SeedPool(uint32_t wordsPerVertex = 32u, size_t byteLimit = size_t(8) << 20)
                : m_wpv(wordsPerVertex), m_limit(byteLimit)
            {
            }

            void Configure(uint32_t wordsPerVertex, size_t byteLimit)
            {
                m_wpv = wordsPerVertex;
                m_limit = byteLimit;
            }

            // Reserve a record and return its writable geom block
            // (n * wordsPerVertex uint32 words).
            uint32_t *Begin(const SeedHeader &h)
            {
                m_hdr.push_back(h);
                m_off.push_back(m_words.size());
                m_words.resize(m_words.size() + size_t(h.n) * m_wpv);
                return m_words.data() + m_off.back();
            }

            size_t Count() const { return m_hdr.size(); }
            bool Empty() const { return m_hdr.empty(); }
            const SeedHeader &Header(size_t i) const { return m_hdr[i]; }
            const uint32_t *Verts(size_t i) const { return m_words.data() + m_off[i]; }
            uint32_t WordsPerVertex() const { return m_wpv; }

            size_t Bytes() const
            {
                return m_words.size() * sizeof(uint32_t) +
                       m_hdr.size() * (sizeof(SeedHeader) + sizeof(size_t));
            }
            size_t ByteLimit() const { return m_limit; }
            bool ShouldDrain() const { return Bytes() >= m_limit; }

            void Clear()
            {
                m_hdr.clear();
                m_off.clear();
                m_words.clear(); // keeps capacity
            }

        private:
            uint32_t m_wpv;
            size_t m_limit;
            std::vector<SeedHeader> m_hdr;
            std::vector<size_t> m_off;
            std::vector<uint32_t> m_words;
        };

    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_SEED_POOL_H
