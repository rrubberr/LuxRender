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

// Advance one bounce with intersect, medium flight, Null collapse.

#ifndef LUX2_INTEGRATORS_LANE_ADVANCE_H
#define LUX2_INTEGRATORS_LANE_ADVANCE_H

#include "integrators/lane/geom.h"
#include "integrators/lane/lane_core.h"
#include "integrators/lane/tile_ctx.h"

#include "core/bsdf.h"
#include "core/medium.h"
#include "core/rng.h"

#include <enoki/array.h>

namespace lux2
{
    // Walk direction.
    enum class Dir
    {
        Eye,
        Light
    };

    // The outcome of one advance:
    //   surface where a non-Null surface was hit and shaded.
    //   scatter where the medium scattered before any surface.
    //   escape where the ray left the world.
    //   absorbed where the medium retired the lane.
    struct Event
    {
        MaskP surface, scatter, escape, absorbed;
        HitP hit;
        DifferentialGeometryP dg;
        BSDFPtr bsdf;
        MediumPtr med;
        FloatP tHit;
        FloatP k; // Schlick value at a medium scatter.
        SWCSpectrumP flightEmission;
    };

    // Select for a HitP.
    inline HitP SelectHit(const MaskP &m, const HitP &a, const HitP &b)
    {
        HitP r;
        r.geomID = enoki::select(m, a.geomID, b.geomID);
        r.primID = enoki::select(m, a.primID, b.primID);
        r.t = enoki::select(m, a.t, b.t);
        r.b1 = enoki::select(m, a.b1, b.b1);
        r.b2 = enoki::select(m, a.b2, b.b2);
        r.hit = enoki::select(m, a.hit, b.hit);
        r.p = enoki::select(m, a.p, b.p);
        r.ngeo = enoki::select(m, a.ngeo, b.ngeo);
        r.sh_n = enoki::select(m, a.sh_n, b.sh_n);
        r.uv = enoki::select(m, a.uv, b.uv);
        r.matID = enoki::select(m, a.matID, b.matID);
        r.lightID = enoki::select(m, a.lightID, b.lightID);
        r.mediumFrontID = enoki::select(m, a.mediumFrontID, b.mediumFrontID);
        r.mediumBackID = enoki::select(m, a.mediumBackID, b.mediumBackID);
        r.dp_du = enoki::select(m, a.dp_du, b.dp_du);
        r.dp_dv = enoki::select(m, a.dp_dv, b.dp_dv);
        r.dp_ds = enoki::select(m, a.dp_ds, b.dp_ds);
        r.dp_dt = enoki::select(m, a.dp_dt, b.dp_dt);
        return r;
    }

    // Advance every `alive` lane by one transport step.
    template <Dir D, class Consumer>
    inline Event Advance(const TileCtx &c, LaneCore &lane, Consumer &cons,
                         const MaskP &alive)
    {
        Event ev;
        // Enoki arrays are uninitialized on default construction!
        ev.surface = ev.scatter = ev.escape = ev.absorbed = MaskP(false);
        ev.bsdf = BSDFPtr(nullptr);
        ev.med = MediumPtr(nullptr);
        ev.tHit = FloatP(0.f);
        ev.k = FloatP(0.f);
        ev.flightEmission = SWCSpectrumP(0.f);
        HitP h0;
        h0.hit = MaskP(false);
        h0.p = Point3fP(0.f);
        h0.ngeo = Normal3fP(0.f);
        h0.sh_n = Normal3fP(0.f);
        ev.hit = h0;

        const Int32P bounce = cons.Bounce(lane);
        const Int32P depthBase = cons.DepthBase(lane);
        const SpectrumWavelengthsP &sw = lane.sw;

        // Rebuild the Embree ray from the lane core.
        RayP ray = MakeRay(lane);
        ray.alive = alive;

        MaskP tracing = alive;
        // Lanes that terminated on a non-Null surface.
        MaskP landed(false);
        for (int k = 0; k <= MAX_NULL_CROSSINGS && any(tracing); ++k)
        {
            ray.alive = tracing;
            HitP h;
            c.embree->Intersect(ray, h, Coherent::No);
            const MaskP didHit = tracing && h.hit;

            // Medium flight over [ray.mint, nearest surface] in ray.medium.
            MaskP stop(false);
            if (c.hasMedia)
            {
                const MaskP inMed =
                    tracing && c.mediumTable->GatherNonVacuum(ray.medium, tracing);
                if (any(inMed))
                {
                    const FloatP tEnd = select(didHit, h.t, FloatP(INF_F));
                    MediumSampleP ms;
                    ClearMediumSample(&ms, tEnd);
                    const MediumPtr med =
                        c.mediumTable->Gather(ray.medium, inMed);
                    RNGP rng = KeyedRNG(lane.seed, lane.sampleIdx, bounce,
                                        depthBase + Int32P(k), UInt32P(0u),
                                        FLIGHT_SALT);
                    const FloatP uFlight = rng.NextFloat(inMed);
                    med->SampleInteraction(sw, ray, ray.mint, tEnd, uFlight,
                                           rng, &ms, inMed);

                    // Gather emission at preflight beta, then scale throughput.
                    ev.flightEmission =
                        ev.flightEmission +
                        select(inMed, lane.beta * ms.emission, SWCSpectrumP(0.f));
                    lane.beta =
                        select(inMed, lane.beta * ms.weight, lane.beta);

                    ev.absorbed = ev.absorbed || (inMed && ms.absorbed);
                    const MaskP scat = inMed && ms.scattered;
                    if (any(scat))
                    {
                        ev.scatter = ev.scatter || scat;
                        // Keep the medium pointer a lane already carried.
                        ev.med = select(scat, med, ev.med);
                        ev.hit.p = select(scat, ms.p, ev.hit.p);
                        ev.tHit = select(scat, ms.d, ev.tHit);
                        ev.k = select(scat, ms.k, ev.k);
                    }
                    stop = inMed && (ms.scattered || ms.absorbed);
                }
            }
            // A lane stopped inside the medium never reaches the surface.
            tracing = tracing && !stop;
            const MaskP seen = didHit && !stop;

            // An emissive Null is accumulated but is not a vertex.
            MaskP isNull(false);
            if (k < MAX_NULL_CROSSINGS && any(seen))
            {
                const BSDFPtr b = c.table->Gather(h.matID, seen);
                isNull = seen && has_flag(b->flags(seen), BSDFType::Null);
                if (any(isNull))
                    cons.OnPassThrough(lane, h, isNull);
            }

            // A lane that is not Null this iteration terminates.
            const MaskP done = tracing && !isNull;
            landed = landed || done;
            ev.hit = SelectHit(done, h, ev.hit);
            ev.tHit = select(done, h.t, ev.tHit);

            // Offset through a Null boundary and swap to its exit medium.
            ray.o = select(isNull,
                           OffsetRay(h.p, h.ngeo, ray.d, FloatP(EPS_RAY)),
                           ray.o);
            // Intersect narrows ray.maxt to the hit distance.
            ray.maxt = select(isNull, FloatP(INF_F), ray.maxt);
            if (c.hasMedia && any(isNull))
                ray.medium =
                    select(isNull, SwapMedium(h, ray.d), ray.medium);
            tracing = isNull;
        }

        // Persist medium swap back to the lane.
        if (c.hasMedia)
            lane.medium = ray.medium;

        // Classify the surviving lanes into four states.
        ev.absorbed = ev.absorbed && alive;
        ev.scatter = ev.scatter && alive && !ev.absorbed;
        // A non-Null hit that terminated the collapse loop and
        // was not resolved in the medium.
        ev.surface = landed && ev.hit.hit && alive && !ev.scatter && !ev.absorbed;
        // Escape: still tracing!
        ev.escape = alive && !ev.surface && !ev.scatter && !ev.absorbed;

        if (any(ev.surface))
        {
            ev.dg = MakeDG(ev.hit, ray.d, ev.surface);
            ev.bsdf = c.table->Gather(ev.hit.matID, ev.surface);
        }
        return ev;
    }

} // namespace lux2

#endif // LUX2_INTEGRATORS_LANE_ADVANCE_H
