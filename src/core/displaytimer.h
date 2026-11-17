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

#ifndef LUX2_DISPLAYTIMER_H
#define LUX2_DISPLAYTIMER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace lux2
{

    class Film;
    class RenderStatistics;

    // Periodic display refresh and output.
    class DisplayTimer
    {
    public:
        DisplayTimer(Film &film, RenderStatistics *stats);
        ~DisplayTimer();

        DisplayTimer(const DisplayTimer &) = delete;
        DisplayTimer &operator=(const DisplayTimer &) = delete;

        void Start();
        // Signals the thread and joins it.
        void Stop();

    private:
        void Run();

        Film &m_film;
        RenderStatistics *m_stats;

        std::thread m_thread;
        std::atomic<bool> m_stop{false};
        bool m_joined = false;
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };

} // namespace lux2

#endif // LUX2_DISPLAYTIMER_H
