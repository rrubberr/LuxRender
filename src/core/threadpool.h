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

#ifndef LUX2_THREADPOOL_H
#define LUX2_THREADPOOL_H

#include <tbb/global_control.h>
#include <tbb/task_arena.h>

#include <memory>

namespace lux2
{

    // TBB scheduler configuration.
    class RenderThreadPool
    {
    public:
        static RenderThreadPool &Get();

        // Process wide global_control.
        void Init(unsigned int nThreads);

        // Effective worker count.
        unsigned int Count() const { return m_count; }

        // Run fn inside a task_arena bounded to Count().
        template <class F>
        void RunInArena(F &&fn)
        {
            tbb::task_arena arena(static_cast<int>(m_count));
            arena.execute(std::forward<F>(fn));
        }

    private:
        RenderThreadPool() = default;
        ~RenderThreadPool() = default;
        RenderThreadPool(const RenderThreadPool &) = delete;
        RenderThreadPool &operator=(const RenderThreadPool &) = delete;

        std::unique_ptr<tbb::global_control> m_control;
        unsigned int m_count = 0;
    };

} // namespace lux2

#endif // LUX2_THREADPOOL_H
