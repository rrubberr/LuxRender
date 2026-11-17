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

#ifndef LUX2_CORE_MEMORY_HUGE_ALLOC_H
#define LUX2_CORE_MEMORY_HUGE_ALLOC_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace lux2
{
    // Allocation primitive for large buffers.
    void *HugeAlloc(std::size_t bytes);
    void HugeFree(void *ptr, std::size_t bytes) noexcept;

    // True if the hugepage path is compiled in.
    bool HugePagesEnabled();

    // STL allocator adapter over HugeAlloc/HugeFree.
    template <class T>
    struct HugeAllocator
    {
        using value_type = T;

        HugeAllocator() = default;
        template <class U>
        HugeAllocator(const HugeAllocator<U> &)
        {
        }

        T *allocate(std::size_t n)
        {
            return static_cast<T *>(HugeAlloc(n * sizeof(T)));
        }
        void deallocate(T *p, std::size_t n) noexcept
        {
            HugeFree(p, n * sizeof(T));
        }
        template <class U>
        bool operator==(const HugeAllocator<U> &) const
        {
            return true;
        }
        template <class U>
        bool operator!=(const HugeAllocator<U> &) const
        {
            return false;
        }
    };

    template <class T>
    using HugeVector = std::vector<T, HugeAllocator<T>>;

} // namespace lux2

#endif // LUX2_CORE_MEMORY_HUGE_ALLOC_H
