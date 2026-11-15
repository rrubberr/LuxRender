
// Seed capture hook for the wavefront connectors.
//
// A connector that finishes a connection calls Emit() with the *MIS-weighted*
// contribution e = Lc * w and enough information to reconstruct the connected
// path geometrically. The pointer is null in bidirectional, so bidir
// behaviour is unchanged; ERPT installs a sink that turns each contribution
// into either a chain seed or a direct deposit.
//
// All five strategy families are reported, because ER sampling is only
// unbiased if the seeds' expected energy density is exactly f. If any
// strategy were skipped the seeded density would be f * W(x) with
// W = (sum of seeded MIS weights) < 1, and the chains (stationary for f) would
// redistribute a distribution they are not stationary for.
//
//   Gen       eye prefix [0,nE)  +  light prefix [0,nL)          (real vertices)
//   Nee       eye prefix [0,nE)  +  virtual emitter point
//   CamNee    camera vertex      +  virtual emitter point        (n == 2)
//   Cam       camera vertex      +  light prefix [0,nL)
//   HitLight  eye vertices [0,nE) (stored so far) + the emitter hit point
//
// `nE` always counts only vertices that exist in the eye PathStore; the
// virtual / hit emitter point is described separately.

#ifndef LUX2_INTEGRATORS_WF_SEED_SINK_H
#define LUX2_INTEGRATORS_WF_SEED_SINK_H

#include "integrators/wf/sample_table.h"
#include "integrators/wf/storage.h"
#include "integrators/wf/types.h"

#include "core/math/vecp.h"

namespace lux2
{
    namespace wf
    {
        enum class SeedKind : int
        {
            Gen = 0,
            Nee = 1,
            CamNee = 2,
            Cam = 3,
            HitLight = 4
        };

        struct SeedRec
        {
            SeedKind kind = SeedKind::Gen;
            MaskP cont;        // lanes that produced a contribution
            FloatP e;          // Lc * w, MIS weighted
            UInt32P sample;    // sample-table slot (identity: seed, sidx, w, x, y)

            const PathStore *eyePs = nullptr;
            UInt32P ePath;
            Int32P nE;         // eye vertices used, indices [0, nE)

            const PathStore *lightPs = nullptr;
            UInt32P lPath;
            Int32P nL;         // light vertices used, indices [0, nL)

            // Virtual emitter point (Nee / CamNee / HitLight).
            Point3fP vP;
            Normal3fP vN, vNg;
            UInt32P vLightIdx; // index into WalkCtx::lightPtrs  (Nee / CamNee)
            UInt32P vAreaId;   // area-light id                  (HitLight)

            // Raster position for strategies that do not live in the sample
            // table row (Cam / CamNee).
            FloatP px, py;

            SeedRec()
                : cont(false), e(0.f), sample(0u), ePath(0u), nE(0), lPath(0u),
                  nL(0), vP(0.f), vN(0.f), vNg(0.f), vLightIdx(0u),
                  vAreaId(0u), px(0.f), py(0.f)
            {
            }
        };

        struct SeedSink
        {
            virtual ~SeedSink() = default;
            virtual void Emit(const WalkCtx &c, const SampleTable &tbl,
                              const SeedRec &rec) = 0;
        };

    } // namespace wf
} // namespace lux2

#endif // LUX2_INTEGRATORS_WF_SEED_SINK_H
