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

#include "core/memory/huge_alloc.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

namespace lux2
{
    namespace
    {
        // Returned pointers are aligned to this.
        constexpr std::size_t kAlign = 64;
        // Hugepage size we align/round toward.
        constexpr std::size_t kHuge = 2u * 1024u * 1024u;

        inline std::size_t RoundUp(std::size_t v, std::size_t a)
        {
            return (v + (a - 1)) & ~(a - 1);
        }

        // HugeFree can release either backing without a global lookup.
        struct Header
        {
            void *base;      // mmap base, or operator pointer
            std::size_t len; // mmap length, or operator total
            bool isMmap;
        };
    } // namespace

    bool HugePagesEnabled()
    {
#ifdef LUX_USE_HUGEPAGES
        return true;
#else
        return false;
#endif
    }

    void *HugeAlloc(std::size_t bytes)
    {
        if (bytes == 0)
            bytes = kAlign;

#ifdef LUX_USE_HUGEPAGES
#ifdef __linux__
        // Round the mapping up to a whole number of huge pages so khugepaged
        // has aligned 2 MiB windows to collapse.
        const std::size_t need = bytes + sizeof(Header) + kAlign;
        const std::size_t mapLen = RoundUp(need, kHuge);
        void *base = mmap(nullptr, mapLen, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base != MAP_FAILED)
        {
            // Best effort, ignore failure, the region is still usable.
            madvise(base, mapLen, MADV_HUGEPAGE);
            const std::uintptr_t raw =
                reinterpret_cast<std::uintptr_t>(base) + sizeof(Header);
            const std::uintptr_t aligned = RoundUp(raw, kAlign);
            Header *h = reinterpret_cast<Header *>(aligned - sizeof(Header));
            h->base = base;
            h->len = mapLen;
            h->isMmap = true;
            return reinterpret_cast<void *>(aligned);
        }
        // mmap failed (e.g. no memory).
#endif
#endif

        const std::size_t total = RoundUp(bytes + sizeof(Header) + kAlign, kAlign);
        void *p = ::operator new(total, std::align_val_t(kAlign));
        const std::uintptr_t raw =
            reinterpret_cast<std::uintptr_t>(p) + sizeof(Header);
        const std::uintptr_t aligned = RoundUp(raw, kAlign);
        Header *h = reinterpret_cast<Header *>(aligned - sizeof(Header));
        h->base = p;
        h->len = total;
        h->isMmap = false;
        return reinterpret_cast<void *>(aligned);
    }

    void HugeFree(void *ptr, std::size_t) noexcept
    {
        if (!ptr)
            return;
        Header *h =
            reinterpret_cast<Header *>(reinterpret_cast<char *>(ptr) -
                                       sizeof(Header));
#ifdef __linux__
        if (h->isMmap)
        {
            munmap(h->base, h->len);
            return;
        }
#endif
        ::operator delete(h->base, h->len, std::align_val_t(kAlign));
    }

} // namespace lux2
