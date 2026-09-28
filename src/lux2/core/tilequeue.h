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

#ifndef LUX2_TILEQUEUE_H
#define LUX2_TILEQUEUE_H

#include <deque>
#include <mutex>

namespace lux2
{

    // A rectangular tile of pixels.
    struct Tile
    {
        int x0, y0, x1, y1;
    };

    // Thread safe tile queue.
    class TileQueue
    {
    public:
        bool Next(Tile *t);       // next tile; false when the queue is empty
        void Push(const Tile &t); // push a tile onto the queue
        bool Empty() const;       // true if no tiles remain
        int Remaining() const;    // tiles remaining

    private:
        mutable std::mutex m_mutex;
        std::deque<Tile> m_tiles;
    };

    // Fill the queue with tiles.
    inline void BuildTiles(int xStart, int yStart, int xCount, int yCount,
                           int tileSize, TileQueue *q)
    {
        if (xCount <= 0 || yCount <= 0)
            return;
        const int tw = tileSize > 0 ? tileSize : xCount;
        const int th = tileSize > 0 ? tileSize : yCount;
        for (int y = yStart; y < yStart + yCount; y += th)
        {
            const int y1 = y + th < yStart + yCount ? y + th : yStart + yCount;
            for (int x = xStart; x < xStart + xCount; x += tw)
            {
                const int x1 =
                    x + tw < xStart + xCount ? x + tw : xStart + xCount;
                q->Push(Tile{x, y, x1, y1});
            }
        }
    }

    // Inline definitions.
    inline bool TileQueue::Next(Tile *t)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tiles.empty())
            return false;
        *t = m_tiles.front();
        m_tiles.pop_front();
        return true;
    }

    inline void TileQueue::Push(const Tile &t)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tiles.push_back(t);
    }

    inline bool TileQueue::Empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tiles.empty();
    }

    inline int TileQueue::Remaining() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return (int)m_tiles.size();
    }

} // namespace lux2

#endif // LUX2_TILEQUEUE_H
