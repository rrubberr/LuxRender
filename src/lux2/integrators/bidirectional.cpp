/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *   This file is part of LuxRender (GPLv3+, see LICENSE).                 *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

// Bidirectional path tracer: wavefront SoA port of LEGACY src/lux/integrators/
// bidirectional.cpp (NOT PBRT). SCAFFOLD: uncompiled, see "API ASSUMPTIONS".
//
// Legacy -> SoA mapping
//   Li() eye loop           -> TraceEye   (s==0 emission is evaluated IN the walk,
//                                          exactly where legacy calls WeightPath
//                                          with nLight==0)
//   Li() light loop         -> TraceLight (connections deferred until the walk ends)
//   GetDirectLight          -> ConnectDirect   (isLightDirect = true,  nLight = 1)
//   EvalPath, light0 / j>0  -> ConnectGeneral  (isLightDirect = false, nLight = s)
//   WeightPath              -> WeightPath (literal transcription, lane masks for
//                                          legacy `break`s)
//
// Legacy conventions kept verbatim (this is the point of the port):
//   * Specular vertices keep a real pdf/pdfR (lobe probability). They are excluded
//     from MIS only through the `spec` flags, never by zeroing pdfs.
//   * Field names and meanings are legacy's. Eye subpath: pdfR = forward pdf,
//     pdf = reverse pdf, dARWeight = forward area density (aF), dAWeight = reverse
//     area density (aR). Light subpath: pdf = forward, pdfR = reverse,
//     dAWeight = aF, dARWeight = aR. wb = direction back along the incoming
//     segment (legacy eye.wo / light.wi).
//   * MIS is the legacy power-2 sum with Russian-roulette ratios. EvalPath's
//     "modify then restore" of the last two vertices of each subpath is a
//     Transient (stored arrays are never mutated by a connection).
//   * Only eyedepth/lightdepth bound path length (no combined maxdepth).
//   * Connection vertices are connectible if the BSDF has a non-delta lobe
//     (f > 0), regardless of which lobe the walk sampled.
//
// Deliberate divergences (SoA / phase 1)
//   [D1] Connections run after both walks finish; legacy interleaves. Equivalent,
//        because every state a connection reads is final or is in Transient.
//   [D2] Null surfaces collapse inside the intersect loop (no vertex, no depth
//        cost, tPdf == tPdfR == 1). An emitter that is also Null still gets its
//        s==0 evaluation (hook inside IntersectCollapsed).
//   [D3] Geometry only: tPdf/tPdfR/scattered dropped. Uniform light selection,
//        shadowRayCount == lightRayCount == 1. Env lights: TODO.
//   [D4] t==1 (light tracing to camera) is OFF (needs camera inverse mapping and
//        camera pdf). Legacy weights assume it exists, so with it off the two
//        spots marked [T1-OFF] in WeightPath drop exactly the t'==1 terms.
//   [D5] Emitters are diffuse area lights: emitter F = |cos|, pdf = |cos|/pi,
//        emission area pdf = 1/TotalArea.
//   [D6] Legacy GetDirectLight sets vE.tPdf = ePdfDirect, which then leaks into
//        EvalPath's eye[nEye-2].dAWeight. That looks like a legacy state-corruption
//        bug; this port uses tPdf = 1. Verify against a trusted legacy build.

#include "integrators/bidirectional.h"

#include "core/bsdf_call.h"
#include "core/bsdfptr_table.h"
#include "core/camera.h"
#include "core/color.h"
#include "core/dynload.h"
#include "core/embree2.h"
#include "core/light.h"
#include "core/light_call.h"
#include "core/math.h"
#include "core/register.h"
#include "core/rng.h"
#include "core/sampling.h"
#include "core/scene.h"

#include <enoki/array.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace lux2
{
    // TODO: Stub so unique_ptr<DebugStore> links.
    struct DebugStore
    {
    };

    // One (nE,nL) ghost connection cell.
    struct GhostCell
    {
        uint64_t delta = 0;
        uint64_t mixed = 0;
        double energy = 0.0;
    };

    // Diagnostic store.
    struct DiagStore
    {
        std::vector<std::vector<GhostCell>> ghost; // [nE][nL]
        // Total RecordFirefly calls and the global max |Lc*w|.
        uint64_t recordCalls = 0;
        float maxLcw = 0.f;
        struct Firefly
        {
            float lcw = 0.f, lc = 0.f, w = 0.f, d2 = 0.f;
            int s = 0, t = 0, px = 0, py = 0;
            // WeightPath denominator split.
            float base = 0.f, directOrigin = 0.f, hitLightAlt = 0.f;
            float lightExt = 0.f, eyeExt = 0.f, directPath = 0.f;
            bool okLight = false, okEye = false;
        };
        // Min-heap on lcw (std::greater comparator).
        std::vector<Firefly> top;
        static constexpr size_t kTopN = 64;
        std::mutex mtx;

        void Init(int eyeDepth, int lightDepth)
        {
            ghost.assign(eyeDepth + 2,
                         std::vector<GhostCell>(lightDepth + 2));
        }
        // Caller holds mutex.
        void InsertFirefly(const Firefly &f)
        {
            if (top.size() < kTopN)
            {
                top.push_back(f);
                std::push_heap(top.begin(), top.end(),
                               [](const Firefly &a, const Firefly &b)
                               { return a.lcw > b.lcw; });
            }
            else if (f.lcw > top.front().lcw)
            {
                std::pop_heap(top.begin(), top.end(),
                              [](const Firefly &a, const Firefly &b)
                              { return a.lcw > b.lcw; });
                top.back() = f;
                std::push_heap(top.begin(), top.end(),
                               [](const Firefly &a, const Firefly &b)
                               { return a.lcw > b.lcw; });
            }
        }
    };

    namespace
    {
        constexpr float INF_F = std::numeric_limits<float>::infinity();
        constexpr int MAX_NULL_CROSSINGS = 16;
        constexpr int RR_START = 3;              // legacy rrStart
        constexpr float PDF_EPS = 1.1920929e-7f; // MachineEpsilon::E(1.f)

        // a/b, 0 where b <= 0.
        inline FloatP divp(const FloatP &a, const FloatP &b)
        {
            return select(b > FloatP(0.f), a / b, FloatP(0.f));
        }

        inline Point3fP OffsetRay(const Point3fP &p, const Normal3fP &ng,
                                  const Vector3fP &d, FloatP k)
        {
            const Normal3fP s = select(dot(d, ng) > FloatP(0.f), ng, -ng);
            return p + Vector3fP(s.x(), s.y(), s.z()) * k;
        }

        // Solid angle density at p -> area density at pNext (normal ngNext).
        inline FloatP ConvertDensity(const Point3fP &p, FloatP pdf,
                                     const Point3fP &pNext,
                                     const Normal3fP &ngNext)
        {
            const Vector3fP d = pNext - p;
            const FloatP d2 = dot(d, d);
            const FloatP invD2 = divp(FloatP(1.f), d2);
            const Vector3fP w = d * enoki::sqrt(invD2);
            return pdf * abs(dot(ngNext, w)) * invD2;
        }

        // For a uniformly sampled area light, converting the solid angle Pdf_L
        // at the receiver to the emitter's area measure mustvrecover the selection pdf.
        inline void CheckAreaDensity(const char *what, const FloatP &areaPdf,
                                     const FloatP &area, const FloatP &selPdf)
        {
            const MaskP chk = (area > FloatP(0.f)) && (areaPdf > FloatP(0.f));
            if (!any(chk))
                return;
            const FloatP got = areaPdf * area;
            const FloatP tol =
                FloatP(1e-3f) * enoki::max(selPdf, FloatP(1e-6f));
            const MaskP bad = chk && (enoki::abs(got - selPdf) > tol);
            if (!any(bad))
                return;
            // Report the offending range only.
            LOG(LUX_WARNING, LUX_SYSTEM)
                << "BDPT(legacy): " << what << " area-density != selPdf ("
                << hmin(select(bad, got, INF_F)) << ".."
                << hmax(select(bad, got, FloatP(0.f)))
                << ", want " << selPdf << ")";
        }

        inline BSDFEvalP ZeroEval()
        {
            BSDFEvalP e;
            e.f = SWCSpectrumP(0.f);
            e.pdf = FloatP(0.f);
            e.pdfRev = FloatP(0.f);
            return e;
        }

        // Power heuristic term p^k.
        inline FloatP pw(const FloatP &p, float k)
        {
            if (k == 2.f)
                return p * p;
            if (k == 1.f)
                return p;
            if (k == 0.f)
                return FloatP(1.f);
            return enoki::pow(p, FloatP(k));
        }

        // Optional WeightPath out-parameter.
        struct Breakdown
        {
            FloatP base, directOrigin, hitLightAlt, lightExt, eyeExt, directPath;
            MaskP okLight, okEye;
            Breakdown()
                : base(0.f), directOrigin(0.f), hitLightAlt(0.f),
                  lightExt(0.f), eyeExt(0.f), directPath(0.f) {}
        };

        // Diagnostic accumulator.
        struct DiagLocal
        {
            std::vector<std::vector<GhostCell>> ghost; // [nE][nL]
            std::vector<DiagStore::Firefly> fireflies; // unsorted; heapified on merge
            // Counts RecordFirefly invocations and the largest |Lc*w| seen.
            uint64_t recordCalls = 0;
            float maxLcw = 0.f;
            void Init(int eyeDepth, int lightDepth)
            {
                ghost.assign(eyeDepth + 2,
                             std::vector<GhostCell>(lightDepth + 2));
            }
            // Keep only the kTopN largest by lcw.
            void TrimFireflies()
            {
                const size_t n = DiagStore::kTopN;
                if (fireflies.size() <= n)
                    return;
                std::nth_element(fireflies.begin(), fireflies.begin() + n,
                                 fireflies.end(),
                                 [](const DiagStore::Firefly &a,
                                    const DiagStore::Firefly &b)
                                 { return a.lcw > b.lcw; });
                fireflies.resize(n);
            }
        };

        // ------------------------------------------------------------------
        // Pixel jobs
        // ------------------------------------------------------------------
        struct Job
        {
            int px, py;
            uint32_t s;
        };

        struct JobCursor
        {
            int x0, y0, x1, y1;
            uint32_t spp;
            int px, py;
            uint32_t s = 0;

            JobCursor(const Tile &t, uint32_t n)
                : x0(t.x0), y0(t.y0), x1(t.x1), y1(t.y1), spp(n),
                  px(t.x0), py(t.y0) {}

            bool Next(Job &j)
            {
                if (s >= spp || x1 <= x0 || y1 <= y0)
                    return false;
                j = Job{px, py, s};
                if (++px == x1)
                {
                    px = x0;
                    if (++py == y1)
                    {
                        py = y0;
                        ++s;
                    }
                }
                return true;
            }
        };

        // ------------------------------------------------------------------
        // Subpath rows
        // ------------------------------------------------------------------
        struct Subpath
        {
            std::vector<Point3fP> p;
            std::vector<DifferentialGeometryP> dg;
            std::vector<BSDFPtr> bsdf;
            std::vector<Vector3fP> wb;      // back along incoming segment
            std::vector<SWCSpectrumP> flux; // RR-adjusted flux
            std::vector<FloatP> pdf, pdfR;
            std::vector<FloatP> cosi, coso; // |dot(., ng)|
            std::vector<FloatP> d2;         // squared distance to next vertex
            std::vector<FloatP> dAWeight, dARWeight;
            std::vector<FloatP> rr, rrR;
            std::vector<MaskP> spec; // sampled lobe was specular
            UInt32P selIdx;          // light chosen for the origin
            FloatP directPdf1;       // light->Pdf(light[1].p, light[0]) * sel,
                                     // area measure at light[0] (legacy lightDirectPdf)

            void Init(int n)
            {
                p.assign(n, Point3fP(0.f));
                dg.resize(n);
                bsdf.assign(n, BSDFPtr(nullptr));
                wb.assign(n, Vector3fP(0.f));
                flux.assign(n, SWCSpectrumP(0.f));
                pdf.assign(n, FloatP(0.f));
                pdfR.assign(n, FloatP(0.f));
                cosi.assign(n, FloatP(0.f));
                coso.assign(n, FloatP(0.f));
                d2.assign(n, FloatP(0.f));
                dAWeight.assign(n, FloatP(0.f));
                dARWeight.assign(n, FloatP(0.f));
                rr.assign(n, FloatP(1.f)); // legacy constructor default
                rrR.assign(n, FloatP(1.f));
                spec.assign(n, MaskP(false));
                selIdx = UInt32P(0u);
                directPdf1 = FloatP(0.f);
            }
        };

        struct WalkCtx
        {
            const Scene *scene;
            const EmbreeScene *embree;
            const BsdfPtrTable *table;
            const Camera *camera = nullptr;
            int nLights;
            FloatP invNLights;
            std::vector<const Light *> lightPtrs;
            std::vector<const Light *> areaPtrs;
            std::vector<std::int32_t> areaGroup;
            std::vector<float> areaById;  // by hit.lightID
            std::vector<float> areaByIdx; // by lightPtrs index
            int activeGroup = 0;
            bool multiGroup = false;
            int eyeDepth, lightDepth;
            FloatP eyeRR, lightRR; // eyeThreshold, lightThreshold
            bool useT1 = false;
            // Verify that uniform area conversions of a solid angle
            // pdf recover the selection pdf.
            bool checkDensity = false;
            // Diagnostic accumulator.
            DiagLocal *diag = nullptr;
            DiagConfig cfg;
        };

        inline MaskP ShadowTr(const WalkCtx &ctx, const Point3fP &origin,
                              const Vector3fP &dir, const FloatP &tMax,
                              const SpectrumWavelengthsP &sw, MaskP active)
        {
            // TODO: Occluded treats Null surfaces as transparent?
            RayP shadow(origin, dir, FloatP(EPS_RAY), tMax, FloatP(0.f));
            shadow.wavelengths = sw.w;
            shadow.mask = UInt32P(0xFFFFFFFFu);
            const MaskP blocked =
                ctx.embree->Occluded(shadow, active, Coherent::No);
            return active && !blocked;
        }

        struct LaneState
        {
            SpectrumWavelengthsP sw;
            FloatP x, y, camWeight, alpha;
            UInt32P seed, sampleIdx;
            SWCSpectrumP L;
            MaskP active;
        };

        // Strategy filter.
        inline bool want(const DiagConfig &c, Strat kind, int nE, int nL)
        {
            if (c.onlyT >= 0 && nE != c.onlyT)
                return false;
            if (c.onlyS >= 0)
            {
                const int sEff = (kind == Strat::HitLight)
                                     ? 0
                                 : (kind == Strat::NEE) ? 1
                                                        : nL;
                if (sEff != c.onlyS)
                    return false;
            }
            return true;
        }

        // A lobe reports reflectance (f > 0) but has no finite solid angle pdf.
        inline void TallyGhost(DiagLocal *d, const BSDFEvalP &ef, MaskP evaluated,
                               MaskP cont, const SWCSpectrumP &Lc,
                               const FloatP &w, const UInt32P &flags, int nE,
                               int nL)
        {
            if (!d)
                return;
            const MaskP hasF = ef.f > SWCSpectrumP(0.f);
            const MaskP hasPdf = ef.pdf > FloatP(0.f);
            const MaskP ghost = evaluated && hasF && !hasPdf;
            if (!any(ghost))
                return;
            const MaskP deltaOnly = has_flag(flags, BSDFType::Specular) &&
                                    !has_flag(flags, BSDFType::Smooth);
            GhostCell &c = d->ghost[nE][nL];
            c.delta += uint64_t(count(ghost && deltaOnly));
            c.mixed += uint64_t(count(ghost && !deltaOnly));
            c.energy += double(enoki::hsum(select(cont && ghost, Lc * w,
                                                  SWCSpectrumP(0.f))));
        }

        // Record contributions above the firefly threshold into the list.
        inline void RecordFirefly(DiagLocal *dl, const WalkCtx &ctx,
                                  const FloatP &x, const FloatP &y, MaskP cont,
                                  const SWCSpectrumP &Lc, const FloatP &w,
                                  const FloatP &d2, int s, int t,
                                  const Breakdown *bd)
        {
            if (!dl)
                return;
            // Run before the threshold test.
            dl->recordCalls += 1;
            const float tileMax = enoki::hmax(select(cont, Lc * w, SWCSpectrumP(0.f)));
            if (tileMax > dl->maxLcw)
                dl->maxLcw = tileMax;
            if (ctx.cfg.threshold <= 0.f)
                return;
            const SWCSpectrumP lcw = Lc * w;
            const MaskP hit = cont && (lcw > FloatP(ctx.cfg.threshold));
            if (!any(hit))
                return;
            alignas(64) float mags[PACKET_WIDTH], lcs[PACKET_WIDTH];
            alignas(64) float ws[PACKET_WIDTH], d2s[PACKET_WIDTH];
            alignas(64) float xs[PACKET_WIDTH], ys[PACKET_WIDTH];
            // compress() binds its destination as Mem&.
            auto cmp = [&](float *dst, const FloatP &v)
            { enoki::compress(dst, v, hit); };
            float *pm = mags;
            const size_t c = enoki::compress(pm, lcw, hit);
            cmp(lcs, Lc);
            cmp(ws, w);
            cmp(d2s, d2);
            cmp(xs, x);
            cmp(ys, y);
            // Breakdown fields are packets too.
            alignas(64) float bBase[PACKET_WIDTH], bDOrg[PACKET_WIDTH];
            alignas(64) float bHLit[PACKET_WIDTH], bLExt[PACKET_WIDTH];
            alignas(64) float bEExt[PACKET_WIDTH], bDPath[PACKET_WIDTH];
            alignas(64) float bOkL[PACKET_WIDTH], bOkE[PACKET_WIDTH];
            if (bd)
            {
                cmp(bBase, bd->base);
                cmp(bDOrg, bd->directOrigin);
                cmp(bHLit, bd->hitLightAlt);
                cmp(bLExt, bd->lightExt);
                cmp(bEExt, bd->eyeExt);
                cmp(bDPath, bd->directPath);
                cmp(bOkL, select(bd->okLight, FloatP(1.f), FloatP(0.f)));
                cmp(bOkE, select(bd->okEye, FloatP(1.f), FloatP(0.f)));
            }
            for (size_t i = 0; i < c; ++i)
            {
                DiagStore::Firefly f;
                f.lcw = mags[i];
                f.lc = lcs[i];
                f.w = ws[i];
                f.d2 = d2s[i];
                f.s = s;
                f.t = t;
                f.px = int(xs[i]);
                f.py = int(ys[i]);
                if (bd)
                {
                    f.base = bBase[i];
                    f.directOrigin = bDOrg[i];
                    f.hitLightAlt = bHLit[i];
                    f.lightExt = bLExt[i];
                    f.eyeExt = bEExt[i];
                    f.directPath = bDPath[i];
                    f.okLight = bOkL[i] > 0.5f;
                    f.okEye = bOkE[i] > 0.5f;
                }
                dl->fireflies.push_back(f);
            }
        }

        void Refill(const Sampler &sampler, JobCursor &jobs,
                    uint64_t sampleBasis, LaneState &st, int k = 2)
        {
            const int nDead = enoki::count(!st.active);
            if (nDead < k)
                return;

            alignas(64) float aliveF[PACKET_WIDTH];
            enoki::store_unaligned(aliveF,
                                   select(st.active, FloatP(1.f), FloatP(0.f)));
            alignas(64) float fillF[PACKET_WIDTH] = {};
            alignas(64) float pxs[PACKET_WIDTH] = {}, pys[PACKET_WIDTH] = {};
            alignas(64) uint32_t seeds[PACKET_WIDTH] = {},
                                 sidxs[PACKET_WIDTH] = {};

            const uint64_t baseSeed = sampler.BaseSeed();
            bool anyFill = false;
            for (size_t i = 0; i < PACKET_WIDTH; ++i)
            {
                if (aliveF[i] > 0.f)
                    continue;
                Job j;
                if (!jobs.Next(j))
                    break;
                fillF[i] = 1.f;
                pxs[i] = float(j.px);
                pys[i] = float(j.py);
                sidxs[i] = j.s;
                const uint64_t h =
                    ((sampleBasis ^ baseSeed) * 0x9E3779B97F4A7C15ull) ^
                    (uint64_t(j.px) * 0xBF58476D1CE4E5B9ull) ^
                    (uint64_t(j.py) * 0x94D049BB133111EBull);
                seeds[i] = uint32_t(h >> 32);
                anyFill = true;
            }
            if (!anyFill)
                return;

            const MaskP fillMask =
                enoki::load_unaligned<FloatP>(fillF) > FloatP(0.5f);
            const UInt32P vseed = enoki::load_unaligned<UInt32P>(seeds);
            const UInt32P vidx = enoki::load_unaligned<UInt32P>(sidxs);
            const FloatP pxf = enoki::load_unaligned<FloatP>(pxs);
            const FloatP pyf = enoki::load_unaligned<FloatP>(pys);

            const Point2fP jit = sampler.Get2D(vseed, vidx, UInt32P(0u));
            SpectrumWavelengthsP newSw;
            newSw.Sample(sampler.Get1D(vseed, vidx, UInt32P(1u)));

            st.x = select(fillMask, pxf + jit.x(), st.x);
            st.y = select(fillMask, pyf + jit.y(), st.y);
            st.camWeight = select(fillMask, FloatP(1.f), st.camWeight);
            st.alpha = select(fillMask, FloatP(0.f), st.alpha);
            st.seed = select(fillMask, vseed, st.seed);
            st.sampleIdx = select(fillMask, vidx, st.sampleIdx);
            st.L = select(fillMask, SWCSpectrumP(0.f), st.L);
            st.sw.w = select(fillMask, newSw.w, st.sw.w);
            st.sw.binRGB = select(fillMask, newSw.binRGB, st.sw.binRGB);
            st.sw.offsetRGB = select(fillMask, newSw.offsetRGB, st.sw.offsetRGB);
            st.sw.binXYZ = select(fillMask, newSw.binXYZ, st.sw.binXYZ);
            st.sw.offsetXYZ = select(fillMask, newSw.offsetXYZ, st.sw.offsetXYZ);
            st.active = st.active || fillMask;
        }

        struct RetireBuffer
        {
            static constexpr size_t CAP = 3 * PACKET_WIDTH;
            float bx[CAP], by[CAP], bL[CAP], bwl[CAP], ba[CAP], bwt[CAP];
            size_t n = 0;

            void Push(const LaneState &st, const MaskP &finished)
            {
                float *px = bx + n, *py = by + n, *pL = bL + n;
                float *pwl = bwl + n, *pa = ba + n, *pwt = bwt + n;
                const size_t c = enoki::compress(px, st.x, finished);
                enoki::compress(py, st.y, finished);
                enoki::compress(pL, st.L, finished);
                enoki::compress(pwl, st.sw.w, finished);
                enoki::compress(pa, st.alpha, finished);
                enoki::compress(pwt, st.camWeight, finished);
                n += c;
            }

            void Flush(Film &dest, bool all)
            {
                while (n >= PACKET_WIDTH || (all && n > 0))
                {
                    const size_t take =
                        n < PACKET_WIDTH ? n : size_t(PACKET_WIDTH);
                    const MaskP real =
                        enoki::arange<FloatP>() < FloatP(float(take));
                    auto loadMasked = [&](const float *b)
                    { return select(real, enoki::load_unaligned<FloatP>(b),
                                    FloatP(0.f)); };
                    SpectrumWavelengthsP sw;
                    sw.FromWavelength(loadMasked(bwl));
                    dest.Splat(loadMasked(bx), loadMasked(by),
                               SWCSpectrumP(loadMasked(bL)), sw,
                               loadMasked(ba), loadMasked(bwt), 0);
                    const size_t rem = n - take;
                    std::memmove(bx, bx + take, rem * sizeof(float));
                    std::memmove(by, by + take, rem * sizeof(float));
                    std::memmove(bL, bL + take, rem * sizeof(float));
                    std::memmove(bwl, bwl + take, rem * sizeof(float));
                    std::memmove(ba, ba + take, rem * sizeof(float));
                    std::memmove(bwt, bwt + take, rem * sizeof(float));
                    n = rem;
                }
            }
        };

        // RNG dimension layout (unchanged from the previous port).
        inline UInt32P EyeDim(int v) { return UInt32P(uint32_t(2 + (v - 1) * 4)); }
        inline int LightOriginBaseI(int eyeDepth)
        {
            return 2 + std::max(0, eyeDepth - 1) * 4;
        }
        inline UInt32P LightBounce(int loBase, int s)
        {
            return UInt32P(uint32_t(loBase + 6 + (s - 1) * 4));
        }
        inline int NeeBaseI(int loBase, int lightDepth)
        {
            return loBase + 6 + std::max(0, lightDepth - 1) * 4;
        }
        inline UInt32P NeeDim(int neeBase, int ei)
        {
            return UInt32P(uint32_t(neeBase + (ei - 1) * 4));
        }
        // Camera NEE block.
        inline int CamNeeBaseI(int neeBase, int eyeDepth)
        {
            return neeBase + 4 * std::max(0, eyeDepth - 1);
        }

        inline DifferentialGeometryP MakeDG(const HitP &hit, const Vector3fP &rayD,
                                            const MaskP &m)
        {
            DifferentialGeometryP dg;
            dg.p = hit.p;
            dg.n = select(m, hit.sh_n, Normal3fP(0.f));
            dg.ng = select(m, hit.ngeo, Normal3fP(0.f));
            dg.entering = dot(rayD, hit.ngeo) < FloatP(0.f);
            dg.uv_u = hit.uv.x();
            dg.uv_v = hit.uv.y();
            dg.dp_du = hit.dp_du;
            dg.dp_dv = hit.dp_dv;
            dg.dp_ds = hit.dp_ds;
            dg.dp_dt = hit.dp_dt;
            return dg;
        }

        // This is where the eye evaluates s==0 so an emissive Null surface is not lost.
        template <class OnHit>
        void IntersectCollapsed(const WalkCtx &ctx, RayP &ray, const MaskP &alive,
                                HitP &hit, OnHit &&onHit)
        {
            MaskP tracing = alive;
            for (int c = 0; c <= MAX_NULL_CROSSINGS && any(tracing); ++c)
            {
                ray.alive = tracing;
                HitP h;
                ctx.embree->Intersect(ray, h, Coherent::No);
                ctx.embree->ShadeHit(ray, h);
                const MaskP didHit = tracing && h.hit;
                onHit(h, ray.d, didHit);

                MaskP isNull(false);
                if (c < MAX_NULL_CROSSINGS && any(didHit))
                {
                    const BSDFPtr b = ctx.table->Gather(h.matID, didHit);
                    isNull = didHit && has_flag(b->flags(), BSDFType::Null);
                }
                if (c == 0)
                    hit = h; // initialize every field/lane
                const MaskP done = tracing && !isNull;
                hit.hit = select(done, h.hit, hit.hit);
                hit.p = select(done, h.p, hit.p);
                hit.ngeo = select(done, h.ngeo, hit.ngeo);
                hit.sh_n = select(done, h.sh_n, hit.sh_n);
                hit.uv = select(done, h.uv, hit.uv);
                hit.matID = select(done, h.matID, hit.matID);
                hit.lightID = select(done, h.lightID, hit.lightID);
                hit.dp_du = select(done, h.dp_du, hit.dp_du);
                hit.dp_dv = select(done, h.dp_dv, hit.dp_dv);
                hit.dp_ds = select(done, h.dp_ds, hit.dp_ds);
                hit.dp_dt = select(done, h.dp_dt, hit.dp_dt);
                ray.o = select(isNull,
                               OffsetRay(h.p, h.ngeo, ray.d, FloatP(EPS_RAY)),
                               ray.o);
                tracing = isNull;
            }
        }

        // ------------------------------------------------------------------
        // Transient
        // ------------------------------------------------------------------
        struct Transient
        {
            FloatP eAF_last;            // eye.dARWeight[nE-1] (not stored yet for s==0 in-walk)
            FloatP eAR_last, eAR_prev;  // eye.dAWeight[nE-1], [nE-2]
            FloatP eRR_last, eRRR_last; // eye.rr/rrR[nE-1]
            FloatP lAR_last, lAR_prev;  // light.dARWeight[nL-1], [nL-2]
            FloatP lRR_last, lRRR_last; // light.rr/rrR[nL-1]
            FloatP pdfLightDirect;      // legacy pdfLightDirect (area measure, incl. selection)
            bool isLightDirect = false;
            // Connection vertices (eye[nE-1], light[nL-1]) are non-specular.

            Transient()
                : eAF_last(0.f), eAR_last(0.f), eAR_prev(0.f), eRR_last(1.f),
                  eRRR_last(1.f), lAR_last(0.f), lAR_prev(0.f), lRR_last(1.f),
                  lRRR_last(1.f), pdfLightDirect(0.f) {}
        };

        // ------------------------------------------------------------------
        // WeightPath
        // ------------------------------------------------------------------
        FloatP WeightPath(const WalkCtx &ctx, const Subpath &eye, int nE,
                          const Subpath &light, int nL, const Transient &tr,
                          Breakdown *bd = nullptr)
        {
            if (ctx.cfg.noMis)
                return FloatP(1.f); // w = 1 isolates each strategy's raw Lc

            const float k = ctx.cfg.misPower;
            auto eAF = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eAF_last : eye.dARWeight[idx]; };
            auto eAR = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eAR_last
                                   : (idx == nE - 2 ? tr.eAR_prev : eye.dAWeight[idx]); };
            auto eRR = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eRR_last : eye.rr[idx]; };
            auto eRRR = [&](int idx) -> FloatP
            { return idx == nE - 1 ? tr.eRRR_last : eye.rrR[idx]; };
            auto eSpec = [&](int idx) -> MaskP
            { return idx == nE - 1 ? MaskP(false) : eye.spec[idx]; };

            auto lAF = [&](int idx) -> FloatP
            { return light.dAWeight[idx]; };
            auto lAR = [&](int idx) -> FloatP
            { return idx == nL - 1 ? tr.lAR_last
                                   : (idx == nL - 2 ? tr.lAR_prev : light.dARWeight[idx]); };
            auto lRR = [&](int idx) -> FloatP
            { return idx == nL - 1 ? tr.lRR_last : light.rr[idx]; };
            auto lRRR = [&](int idx) -> FloatP
            { return idx == nL - 1 ? tr.lRRR_last : light.rrR[idx]; };
            auto lSpec = [&](int idx) -> MaskP
            { return idx == nL - 1 ? MaskP(false) : light.spec[idx]; };

            const FloatP aF0 = nL >= 1 ? lAF(0) : FloatP(0.f);
            const FloatP pBase = (nL == 1 && tr.isLightDirect)
                                     ? divp(aF0, tr.pdfLightDirect)
                                     : FloatP(1.f);
            FloatP weight(1.f);
            FloatP p = pBase;
            if (bd)
                bd->base = pBase;

            if (nL == 1)
            {
                if (tr.isLightDirect)
                {
                    // light[0] flags are forced non-specular by EvalPath.
                    if (ctx.lightDepth > 0)
                    {
                        const MaskP m = aF0 > FloatP(0.f);
                        const FloatP add = pw(pBase, k);
                        weight = select(m, weight + add, weight);
                        if (bd)
                            bd->directOrigin = select(m, add, FloatP(0.f));
                    }
                }
                else
                {
                    const FloatP pDirect = divp(tr.pdfLightDirect, aF0);
                    const MaskP m = aF0 > FloatP(0.f);
                    const FloatP add = pw(pDirect, k);
                    weight = select(m, weight + add, weight);
                    if (bd)
                        bd->directOrigin = select(m, add, FloatP(0.f));
                }
            }

            // Eye path hit a light.
            if (nL == 0 && (nE - 2 >= 1 || ctx.useT1))
            {
                FloatP pDirect = divp(tr.pdfLightDirect, eAF(nE - 1));
                if (nE > RR_START + 1)
                    pDirect = divp(pDirect, eye.rrR[nE - 2]);
                const MaskP m = !eSpec(nE - 2);
                const FloatP add = pw(pDirect, k);
                weight = select(m, weight + add, weight);
                if (bd)
                    bd->hitLightAlt = select(m, add, FloatP(0.f));
            }

            // Extend the light path toward the eye path.
            // TODO: T1-OFF stops before the strategy whose eye path is only the camera.
            const int minEye = ctx.useT1 ? 1 : 2;
            const int nLightExt =
                std::min(nE - minEye,
                         ctx.lightDepth - std::min(ctx.lightDepth, nL));
            MaskP ok(true);
            for (int i = 1; i <= nLightExt; ++i)
            {
                const int idx = nE - i;
                ok = ok && (eAR(idx) > FloatP(0.f)) && (eAF(idx) > FloatP(0.f));
                p = p * divp(eAR(idx), eAF(idx));
                if (idx > RR_START)
                    p = divp(p, eye.rrR[idx - 1]);
                if (nL + i > RR_START + 1)
                    p = p * (i == 1 ? lRR(nL - 1) : eRR(idx + 1));
                const MaskP specOk = !eSpec(idx) && (i == nE ? MaskP(true) : !eSpec(idx - 1));
                const MaskP m = ok && specOk;
                const FloatP add = pw(p, k);
                weight = select(m, weight + add, weight);
                if (bd)
                    bd->lightExt += select(m, add, FloatP(0.f));
            }
            if (bd)
                bd->okLight = ok;

            // Extend the eye path toward the light path.
            p = pBase;
            const int nEyeExt =
                std::min(nL, ctx.eyeDepth - std::min(ctx.eyeDepth, nE));
            ok = MaskP(true);
            for (int i = 1; i <= nEyeExt; ++i)
            {
                const int idx = nL - i;
                ok = ok && (lAR(idx) > FloatP(0.f)) && (lAF(idx) > FloatP(0.f));
                p = p * divp(lAR(idx), lAF(idx));
                if (idx > RR_START)
                    p = divp(p, lRR(idx - 1));
                if (nE + i > RR_START + 1)
                    p = p * (i == 1 ? eRRR(nE - 1) : lRRR(idx + 1));
                const MaskP specOk = !lSpec(idx) && (i == nL ? MaskP(true) : !lSpec(idx - 1));
                const MaskP m = ok && specOk;
                const FloatP add = pw(p, k);
                weight = select(m, weight + add, weight);
                if (bd)
                    bd->eyeExt += select(m, add, FloatP(0.f));
                // Direct path: the second vertex must be non-specular.
                if (i == nL - 1)
                {
                    const MaskP dm = ok && !lSpec(1) && (aF0 > FloatP(0.f));
                    const FloatP pDirect = p * divp(tr.pdfLightDirect, aF0);
                    const FloatP add2 = pw(pDirect, k);
                    weight = select(dm, weight + add2, weight);
                    if (bd)
                        bd->directPath = select(dm, add2, FloatP(0.f));
                }
            }
            if (bd)
                bd->okEye = ok;
            return weight;
        }

        // ------------------------------------------------------------------
        // Eye path landed on an emitter
        // ------------------------------------------------------------------
        void AccumulateS0(const WalkCtx &ctx, LaneState &st, const Subpath &eye,
                          int t, const SWCSpectrumP &beta, const HitP &h,
                          const Vector3fP &rayD, const MaskP &emitIn)
        {
            if (!want(ctx.cfg, Strat::HitLight, t + 1, 0))
                return;
            MaskP emit = emitIn;
            if (!any(emit))
                return;
            if (ctx.multiGroup)
            {
                const Int32P g = enoki::gather<Int32P>(ctx.areaGroup.data(),
                                                       h.lightID, emit);
                emit = emit && eq(g, Int32P(ctx.activeGroup));
            }
            if (!any(emit))
                return;

            const DifferentialGeometryP dg = MakeDG(h, rayD, emit);
            const Vector3fP wo = -rayD;
            const LightPtr al =
                enoki::gather<LightPtr>(ctx.areaPtrs.data(), h.lightID, emit);
            const SWCSpectrumP Le = al->Le(dg, st.sw, wo, emit);
            const SWCSpectrumP Lc = beta * Le; // legacy: Ll(v.flux) * Le
            const MaskP cont = emit && (Lc > SWCSpectrumP(0.f));
            if (!any(cont))
                return;

            const int nE = t + 1;
            const Vector3fP dv = h.p - eye.p[t - 1];
            const FloatP d2 = dot(dv, dv); // legacy vp.d2
            const FloatP invD2 = divp(FloatP(1.f), d2);
            const Vector3fP wDir = dv * enoki::sqrt(invD2);
            const FloatP cB = abs(dot(wDir, h.ngeo)); // v.coso

            const FloatP area =
                enoki::gather<FloatP>(ctx.areaById.data(), h.lightID, cont);
            const FloatP emitPdf = abs(dot(wo, dg.n)) * INVPI; // v.pdf [D5]

            Transient tr;
            // v.dARWeight = vp.pdfR * vp.tPdfR / vp.d2 * v.coso
            tr.eAF_last = (t >= 2 || ctx.useT1)
                              ? eye.pdfR[t - 1] * cB * invD2
                              : FloatP(0.f);
            // v.dAWeight = pdfArea * lightPathStrategy->Pdf   [D5]
            tr.eAR_last = ctx.invNLights * divp(FloatP(1.f), area);
            // vp.dAWeight = v.pdf * v.tPdf / vp.d2 * vp.cosi
            tr.eAR_prev = emitPdf * eye.cosi[t - 1] * invD2;
            tr.eRR_last = FloatP(1.f);
            tr.eRRR_last = FloatP(1.f);
            // ePdfDirect = direct-sampling area density of THIS point from vp,
            // times light-selection pdf.
            const FloatP pdfSolid =
                al->Pdf_L(eye.p[t - 1], eye.dg[t - 1].n, h.p, dg.n, cont,
                          MaskP(t > 1)) *
                ctx.invNLights;
            tr.pdfLightDirect = ConvertDensity(eye.p[t - 1], pdfSolid, h.p, h.ngeo);
            tr.isLightDirect = false;

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, nE, eye, 0, tr,
                                             ctx.diag ? &bd : nullptr));
            st.L = select(cont, st.L + Lc * w, st.L);
            RecordFirefly(ctx.diag, ctx, st.x, st.y, cont, Lc, w, d2, /*s*/ 0,
                          nE, &bd);
        }

        // ------------------------------------------------------------------
        // Eye subpath
        // ------------------------------------------------------------------
        void TraceEye(const WalkCtx &ctx, const Camera &camera,
                      const Sampler &sampler, LaneState &st, Subpath &eye,
                      Int32P *nEyeOut)
        {
            const int maxV = ctx.eyeDepth;
            const MaskP active = st.active;

            RayP primary;
            FloatP camW;
            camera.GenerateRay(st.x, st.y, FloatP(0.f), &primary, &camW, active);
            primary.wavelengths = st.sw.w;
            primary.mask = UInt32P(0xFFFFFFFFu);
            st.camWeight = select(active, camW, st.camWeight);

            eye.p[0] = primary.o;
            eye.dg[0].p = primary.o;
            eye.dg[0].n = Normal3fP(0.f);
            eye.dg[0].ng = Normal3fP(0.f);
            eye.bsdf[0] = BSDFPtr(nullptr);
            eye.wb[0] = Vector3fP(0.f);
            eye.flux[0] = SWCSpectrumP(1.f);
            eye.spec[0] = MaskP(false);
            if (ctx.useT1)
            {
                eye.pdfR[0] = camera.Pdf(primary, active);
                eye.pdf[0] = eye.pdfR[0]; // symmetric for pinhole
            }
            else
            {
                eye.pdf[0] = eye.pdfR[0] = FloatP(0.f);
            }
            eye.rr[0] = FloatP(1.f);
            eye.rrR[0] = FloatP(1.f);
            eye.cosi[0] = eye.coso[0] = FloatP(0.f);
            eye.dAWeight[0] = eye.dARWeight[0] = FloatP(0.f);

            Int32P nEye = Int32P(1);
            MaskP alive = active;
            RayP ray = primary;
            SWCSpectrumP beta = eye.flux[0];

            for (int t = 1; t < maxV; ++t)
            {
                if (!any(alive))
                    break;
                const UInt32P d0 = EyeDim(t);
                const Point2fP u2bsdf = sampler.Get2D(st.seed, st.sampleIdx, d0);
                const FloatP ucomp =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(2u));
                const FloatP urr =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(3u));

                HitP hit;
                IntersectCollapsed(
                    ctx, ray, alive, hit,
                    [&](const HitP &h, const Vector3fP &rd, const MaskP &didHitC)
                    {
                        const MaskP emitHere = didHitC && (h.lightID >= Int32P(0));
                        AccumulateS0(ctx, st, eye, t, beta, h, rd, emitHere);
                    });

                const MaskP didHit = alive && hit.hit;
                st.alpha = select(didHit && (t == 1), FloatP(1.f), st.alpha);
                alive &= hit.hit; // miss ends the subpath (env lights: TODO)
                if (!any(alive))
                    break;

                const DifferentialGeometryP dg = MakeDG(hit, ray.d, didHit);
                const BSDFPtr bsdf = ctx.table->Gather(hit.matID, didHit);
                const Vector3fP wo = -ray.d;
                nEye = select(didHit, nEye + Int32P(1), nEye);

                // legacy: vp.d2, v.coso, v.dARWeight
                const Vector3fP dv = hit.p - eye.p[t - 1];
                const FloatP d2 = dot(dv, dv);
                const FloatP cB = abs(dot(wo, dg.ng));
                eye.d2[t - 1] = select(didHit, d2, eye.d2[t - 1]);
                eye.p[t] = hit.p;
                eye.dg[t] = dg;
                eye.bsdf[t] = bsdf;
                eye.wb[t] = wo;
                eye.flux[t] = beta;
                eye.coso[t] = cB;
                eye.dARWeight[t] =
                    select(didHit && (t >= 2 || ctx.useT1),
                           eye.pdfR[t - 1] * cB * divp(FloatP(1.f), d2),
                           FloatP(0.f));

                // No continuation sample at the last allowed vertex.
                if (t + 1 == maxV)
                    break;

                BSDFSampleP sample;
                bsdf->SampleF(st.sw, wo, dg, u2bsdf.x(), u2bsdf.y(), ucomp,
                              &sample, TransportMode::Radiance, didHit);
                const MaskP specular =
                    didHit && has_flag(sample.sampledType, BSDFType::Specular);
                const MaskP sampOk = didHit && (sample.pdf > FloatP(0.f));

                BSDFEvalP ev = ZeroEval();
                bsdf->Eval(st.sw, sample.wi, wo, dg, TransportMode::Radiance, &ev,
                           didHit && !specular);

                // Lux SampleF outputs pdfR = p(wi|wo) (forward),
                // pdf = p(wo|wi) (reverse). Specular keeps lobe probability.
                eye.pdfR[t] = sample.pdf;
                eye.pdf[t] = select(specular, sample.pdf, ev.pdfRev);
                eye.spec[t] = specular;
                const FloatP cN = abs(dot(sample.wi, dg.ng));
                eye.cosi[t] = cN;
                // Lux vp.dAWeight = v.pdf * tPdf / vp.d2 * vp.cosi
                if (t >= 2)
                    eye.dAWeight[t - 1] =
                        select(sampOk,
                               eye.pdf[t] * eye.cosi[t - 1] *
                                   divp(FloatP(1.f), eye.d2[t - 1]),
                               eye.dAWeight[t - 1]);

                const FloatP f = sample.f;
                eye.rr[t] = min(FloatP(1.f),
                                max(ctx.lightRR, f * cB * divp(FloatP(1.f), cN)));
                eye.rrR[t] = min(FloatP(1.f), max(ctx.eyeRR, f));

                const SWCSpectrumP newBeta = beta * f;
                const MaskP rrActive = alive && ((t + 1) > RR_START);
                const MaskP kill = rrActive && (eye.rrR[t] < urr);
                beta = select(rrActive && !kill,
                              newBeta * divp(FloatP(1.f), eye.rrR[t]), newBeta);
                alive &= !(didHit && !(sample.pdf > FloatP(0.f)));
                alive &= !kill;

                const Point3fP spawn =
                    OffsetRay(hit.p, hit.ngeo, sample.wi, FloatP(EPS_RAY));
                ray.o = select(alive, spawn, ray.o);
                ray.d = select(alive, sample.wi, ray.d);
                ray.mint = FloatP(EPS_RAY);
                ray.maxt = FloatP(INF_F);
                ray.time = FloatP(0.f);
                ray.wavelengths = st.sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                ray.medium = UInt32P(LUX2_MEDIUM_CLEAR);
                ray.UpdateReciprocalDirection();
            }
            *nEyeOut = nEye;
        }

        // ------------------------------------------------------------------
        // Light subpath
        // ------------------------------------------------------------------
        void TraceLight(const WalkCtx &ctx, const Sampler &sampler, LaneState &st,
                        Subpath &light, Int32P *nLightOut)
        {
            const int maxV = ctx.lightDepth;
            const MaskP active = st.active;
            const int loBase = LightOriginBaseI(ctx.eyeDepth);

            const UInt32P lo = UInt32P(uint32_t(loBase));
            const FloatP uSel = sampler.Get1D(st.seed, st.sampleIdx, lo);
            const Point2fP uPos =
                sampler.Get2D(st.seed, st.sampleIdx, lo + UInt32P(1u));
            const FloatP uComp0 =
                sampler.Get1D(st.seed, st.sampleIdx, lo + UInt32P(3u));
            const Point2fP uDir =
                sampler.Get2D(st.seed, st.sampleIdx, lo + UInt32P(4u));

            const MaskP la = active && (ctx.nLights > 0) && (maxV > 0);
            const UInt32P lightIdx =
                min(UInt32P(uint32_t(std::max(0, ctx.nLights - 1))),
                    floor2int<UInt32P>(uSel * FloatP(float(ctx.nLights))));
            const LightPtr selLight =
                enoki::gather<LightPtr>(ctx.lightPtrs.data(), lightIdx, la);
            light.selIdx = lightIdx;

            BSDFPtr ebsdf(nullptr);
            DifferentialGeometryP edg;
            FloatP pdfArea(0.f);
            SWCSpectrumP Le(0.f);
            const MaskP okOrigin = selLight->Sample_Le(
                st.sw, uPos.x(), uPos.y(), uComp0, &ebsdf, &edg, &pdfArea, &Le, la);

            const Vector3fP nVec(edg.n.x(), edg.n.y(), edg.n.z());
            BSDFSampleP s0;
            ebsdf->SampleF(st.sw, nVec, edg, uDir.x(), uDir.y(), FloatP(0.f), &s0,
                           TransportMode::Importance, okOrigin);
            const Vector3fP wi0 = s0.wi;
            const FloatP pdfDir = s0.pdf;
            const MaskP ok = okOrigin && (pdfDir > FloatP(0.f)) &&
                             (pdfArea > FloatP(0.f));

            // Lux light0.dAWeight = pdfArea * lPdf; Le /= lPdf (Le already
            // /pdfArea). flux[0] folds Le/(selPdf*pdfArea); f0 = cos/pdf
            // is the origin's scattering factor applied to flux[1].
            const FloatP selPdf = ctx.invNLights;
            const FloatP aF0 = selPdf * pdfArea;
            const FloatP cos0 = abs(dot(wi0, edg.n));
            const FloatP f0 = cos0 * divp(FloatP(1.f), pdfDir);

            light.p[0] = edg.p;
            light.dg[0] = edg;
            light.bsdf[0] = ebsdf;
            light.wb[0] = nVec; // Lux light0.wi = normal
            light.flux[0] = select(ok, Le * divp(FloatP(1.f), aF0), SWCSpectrumP(0.f));
            light.dAWeight[0] = select(ok, aF0, FloatP(0.f));
            light.dARWeight[0] = light.dAWeight[0]; // overwritten when vertex 1 samples
            light.cosi[0] = abs(dot(nVec, edg.ng));
            light.coso[0] = abs(dot(wi0, edg.ng));
            light.pdf[0] = pdfDir;
            light.pdfR[0] = FloatP(0.f);
            light.spec[0] = MaskP(false);
            light.rrR[0] = min(FloatP(1.f),
                               max(ctx.eyeRR, f0 * light.cosi[0] *
                                                  divp(FloatP(1.f), light.coso[0])));
            light.rr[0] = min(FloatP(1.f), max(ctx.lightRR, f0));

            Int32P nLight = select(ok, Int32P(1), Int32P(0));
            MaskP alive = ok;
            RayP ray(edg.p, wi0, FloatP(EPS_RAY), FloatP(INF_F), FloatP(0.f));
            ray.wavelengths = st.sw.w;
            ray.mask = UInt32P(0xFFFFFFFFu);
            ray.medium = UInt32P(LUX2_MEDIUM_CLEAR);
            SWCSpectrumP beta = light.flux[0] * f0;

            for (int s = 1; s < maxV; ++s)
            {
                if (!any(alive))
                    break;
                const UInt32P d0 = LightBounce(loBase, s);
                const Point2fP u2bsdf = sampler.Get2D(st.seed, st.sampleIdx, d0);
                const FloatP ucomp =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(2u));
                const FloatP urr =
                    sampler.Get1D(st.seed, st.sampleIdx, d0 + UInt32P(3u));

                HitP hit;
                IntersectCollapsed(ctx, ray, alive, hit,
                                   [](const HitP &, const Vector3fP &, const MaskP &) {});
                const MaskP didHit = alive && hit.hit;
                alive &= hit.hit;
                if (!any(alive))
                    break;

                const DifferentialGeometryP dg = MakeDG(hit, ray.d, didHit);
                const BSDFPtr bsdf = ctx.table->Gather(hit.matID, didHit);
                const Vector3fP wi = -ray.d; // Lux v.wi (back toward light)
                nLight = select(didHit, nLight + Int32P(1), nLight);

                const Vector3fP dv = hit.p - light.p[s - 1];
                const FloatP d2 = dot(dv, dv);
                const FloatP cB = abs(dot(wi, dg.ng)); // v.cosi
                light.d2[s - 1] = select(didHit, d2, light.d2[s - 1]);
                light.p[s] = hit.p;
                light.dg[s] = dg;
                light.bsdf[s] = bsdf;
                light.wb[s] = wi;
                light.flux[s] = beta;
                light.cosi[s] = cB;
                // Lux v.dAWeight = vp.pdf * vp.tPdf / vp.d2 * v.cosi
                light.dAWeight[s] =
                    select(didHit, light.pdf[s - 1] * cB * divp(FloatP(1.f), d2),
                           FloatP(0.f));

                if (s == 1)
                {
                    // legacy lightDirectPdf (nLight == 2): direct sampling of
                    // light[0] from vertex 1, area measure at light[0].
                    const FloatP pdfSolid =
                        selLight->Pdf_L(light.p[1], light.dg[1].n, light.p[0],
                                        light.dg[0].n, didHit, MaskP(true)) *
                        ctx.invNLights;
                    light.directPdf1 =
                        ConvertDensity(light.p[1], pdfSolid, light.p[0], light.dg[0].ng);
                    if (ctx.checkDensity)
                    {
                        const FloatP area = enoki::gather<FloatP>(
                            ctx.areaByIdx.data(), lightIdx, didHit);
                        CheckAreaDensity("directPdf1", light.directPdf1, area,
                                         ctx.invNLights);
                    }
                }

                if (s + 1 == maxV)
                    break; // legacy: no sample at the last allowed vertex

                BSDFSampleP sample;
                bsdf->SampleF(st.sw, wi, dg, u2bsdf.x(), u2bsdf.y(), ucomp, &sample,
                              TransportMode::Importance, didHit);
                const MaskP specular =
                    didHit && has_flag(sample.sampledType, BSDFType::Specular);
                const MaskP sampOk = didHit && (sample.pdf > FloatP(0.f));

                BSDFEvalP ev = ZeroEval();
                bsdf->Eval(st.sw, sample.wi, wi, dg, TransportMode::Importance, &ev,
                           didHit && !specular);

                // Lux light SampleF: pdf = forward, pdfR = reverse.
                light.pdf[s] = sample.pdf;
                light.pdfR[s] = select(specular, sample.pdf, ev.pdfRev);
                light.spec[s] = specular;
                const FloatP cN = abs(dot(sample.wi, dg.ng)); // v.coso
                light.coso[s] = cN;
                // Lux vp.dARWeight = v.pdfR * v.tPdfR / vp.d2 * vp.coso
                light.dARWeight[s - 1] =
                    select(sampOk,
                           light.pdfR[s] * light.coso[s - 1] *
                               divp(FloatP(1.f), light.d2[s - 1]),
                           light.dARWeight[s - 1]);

                const FloatP f = sample.f;
                light.rrR[s] = min(FloatP(1.f),
                                   max(ctx.eyeRR, f * cB * divp(FloatP(1.f), cN)));
                light.rr[s] = min(FloatP(1.f), max(ctx.lightRR, f));

                const SWCSpectrumP newBeta = beta * f;
                const MaskP rrActive = alive && ((s + 1) > RR_START);
                const MaskP kill = rrActive && (light.rr[s] < urr);
                beta = select(rrActive && !kill,
                              newBeta * divp(FloatP(1.f), light.rr[s]), newBeta);
                alive &= !(didHit && !(sample.pdf > FloatP(0.f)));
                alive &= !kill;

                const Point3fP spawn =
                    OffsetRay(hit.p, hit.ngeo, sample.wi, FloatP(EPS_RAY));
                ray.o = select(alive, spawn, ray.o);
                ray.d = select(alive, sample.wi, ray.d);
                ray.mint = FloatP(EPS_RAY);
                ray.maxt = FloatP(INF_F);
                ray.wavelengths = st.sw.w;
                ray.mask = UInt32P(0xFFFFFFFFu);
                ray.UpdateReciprocalDirection();
            }
            *nLightOut = nLight;
        }

        // ------------------------------------------------------------------
        // Eye part of EvalPath
        // ------------------------------------------------------------------
        inline void FillEyeTransient(Transient &tr, const Subpath &eye, int ei,
                                     const BSDFEvalP &ef, const FloatP &ecosi,
                                     const FloatP &lpdf, const FloatP &d2,
                                     const WalkCtx &ctx)
        {
            const FloatP invD2 = divp(FloatP(1.f), d2);
            tr.eAF_last = eye.dARWeight[ei];
            // eyeV.dAWeight = lpdf * ltPdf / d2 * ecosi
            tr.eAR_last = lpdf * ecosi * invD2;
            // eye[nE-2].dAWeight = epdf * tPdf / eye[nE-2].d2 * eye[nE-2].cosi
            tr.eAR_prev = ef.pdfRev * eye.cosi[ei - 1] *
                          divp(FloatP(1.f), eye.d2[ei - 1]);
            // eyeV.rr (nEye > 1), eyeV.rrR
            const FloatP den = ecosi * ef.pdfRev;
            tr.eRR_last = select(den > FloatP(PDF_EPS),
                                 min(FloatP(1.f),
                                     max(ctx.lightRR,
                                         ef.f * eye.coso[ei] * divp(FloatP(1.f), den))),
                                 FloatP(0.f));
            tr.eRRR_last = select(ef.pdf > FloatP(PDF_EPS),
                                  min(FloatP(1.f),
                                      max(ctx.eyeRR, ef.f * divp(FloatP(1.f), ef.pdf))),
                                  FloatP(0.f));
        }

        // ------------------------------------------------------------------
        // GetDirectLight at eye vertex nE-1
        // ------------------------------------------------------------------
        void ConnectDirect(const WalkCtx &ctx, const Sampler &sampler,
                           LaneState &st, const Subpath &eye, Subpath &nee,
                           int nE, const Int32P &nEye)
        {
            if (!want(ctx.cfg, Strat::NEE, nE, /*nL*/ 1))
                return;
            const int ei = nE - 1;
            const MaskP base = st.active && (nEye >= Int32P(nE)) && (ctx.nLights > 0);
            if (!any(base))
                return;
            const int neeBase =
                NeeBaseI(LightOriginBaseI(ctx.eyeDepth), ctx.lightDepth);
            const UInt32P nd = NeeDim(neeBase, ei);
            const FloatP uSel = sampler.Get1D(st.seed, st.sampleIdx, nd);
            const Point2fP uPos =
                sampler.Get2D(st.seed, st.sampleIdx, nd + UInt32P(1u));
            const FloatP uComp =
                sampler.Get1D(st.seed, st.sampleIdx, nd + UInt32P(3u));

            const UInt32P lightIdx =
                min(UInt32P(uint32_t(std::max(0, ctx.nLights - 1))),
                    floor2int<UInt32P>(uSel * FloatP(float(ctx.nLights))));
            const LightPtr selLight =
                enoki::gather<LightPtr>(ctx.lightPtrs.data(), lightIdx, base);

            Point3fP lightP(0.f);
            Vector3fP wiL(0.f);
            Normal3fP lightN(0.f);
            FloatP pdfPos(0.f);
            SWCSpectrumP Le(0.f);
            const MaskP sampleOk = selLight->Sample_Li(
                st.sw, eye.p[ei], eye.dg[ei].n, uPos.x(), uPos.y(), uComp, &lightP,
                &wiL, &lightN, &pdfPos, &Le, base);
            const FloatP dist = enoki::norm(lightP - eye.p[ei]);
            const FloatP d2 = dist * dist;
            const FloatP pdfL = pdfPos * ctx.invNLights;
            const FloatP tmax = dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);

            BSDFEvalP ef = ZeroEval();
            eye.bsdf[ei]->Eval(st.sw, wiL, eye.wb[ei], eye.dg[ei],
                               TransportMode::Radiance, &ef, sampleOk);
            const MaskP tryVis = sampleOk && (pdfL > FloatP(0.f)) &&
                                 (ef.f > SWCSpectrumP(0.f));
            const Point3fP shadowO =
                OffsetRay(eye.p[ei], eye.dg[ei].ng, wiL, FloatP(EPS_RAY));
            const MaskP vis = ShadowTr(ctx, shadowO, wiL, tmax, st.sw, tryVis);
            const MaskP cont = tryVis && vis;
            if (!any(cont))
                return;

            // Lux Ld * lf * ef * flux / d2 for a diffuse emitter.
            const SWCSpectrumP Lc =
                eye.flux[ei] * ef.f * Le * divp(FloatP(1.f), pdfL);

            const FloatP area =
                enoki::gather<FloatP>(ctx.areaByIdx.data(), lightIdx, cont);
            // One sided emitter.
            const FloatP cosLraw = dot(-wiL, lightN);
            const FloatP cosL =
                select(cosLraw > FloatP(0.f), cosLraw, FloatP(0.f));
            const FloatP ecosi = abs(dot(wiL, eye.dg[ei].ng));

            // Virtual light vertex dAWeight = emission area pdf * sel
            nee.dAWeight[0] = ctx.invNLights * divp(FloatP(1.f), area);

            Transient tr;
            tr.isLightDirect = true;
            tr.pdfLightDirect = ConvertDensity(eye.p[ei], pdfL, lightP, lightN);
            const FloatP lpdf = cosL * INVPI; // emitter pdf [D5]
            FillEyeTransient(tr, eye, ei, ef, ecosi, lpdf, d2, ctx);
            // lightV.dARWeight = epdfR * etPdfR / d2 * lcoso
            tr.lAR_last = ef.pdf * cosL * divp(FloatP(1.f), d2);
            tr.lRR_last = select(lpdf > FloatP(PDF_EPS),
                                 min(FloatP(1.f),
                                     max(ctx.lightRR, cosL * divp(FloatP(1.f), lpdf))),
                                 FloatP(0.f));
            tr.lRRR_last = FloatP(1.f); // nLight == 1

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, nE, nee, 1, tr,
                                             ctx.diag ? &bd : nullptr));
            st.L = select(cont, st.L + Lc * w, st.L);
            TallyGhost(ctx.diag, ef, sampleOk, cont, Lc, w,
                       eye.bsdf[ei]->flags(), nE, /*nL*/ 1);
            RecordFirefly(ctx.diag, ctx, st.x, st.y, cont, Lc, w, d2, /*s*/ 1,
                          nE, &bd);
        }

        // ------------------------------------------------------------------
        // EvalPath for light subpath prefix nL >= 1
        // ------------------------------------------------------------------
        void ConnectGeneral(const WalkCtx &ctx, LaneState &st, const Subpath &eye,
                            const Subpath &light, int nE, int nL,
                            const Int32P &nEye, const Int32P &nLight)
        {
            if (!want(ctx.cfg, nL == 1 ? Strat::Origin : Strat::General, nE, nL))
                return;
            const int ei = nE - 1;
            const int li = nL - 1;
            const MaskP base = st.active && (nEye >= Int32P(nE)) &&
                               (nLight >= Int32P(nL));
            if (!any(base))
                return;

            const Vector3fP dRaw = light.p[li] - eye.p[ei];
            const FloatP d2 = dot(dRaw, dRaw);
            const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
            const Vector3fP lwi = -ewi;

            BSDFEvalP ef = ZeroEval();
            eye.bsdf[ei]->Eval(st.sw, ewi, eye.wb[ei], eye.dg[ei],
                               TransportMode::Radiance, &ef, base);

            // Origin (nL==1) uses the analytic emitter and deeper vertices
            // use the BSDF. lf.pdf = lpdf, lf.pdfRev = lpdfR.

            // TODO: lf is hoisted so a glass vertex on the light subpath can be
            // ghost-tallied alongside the eye-side ef.
            FloatP lfv, lpdf, lpdfR(0.f);
            BSDFEvalP lf = ZeroEval();
            if (nL == 1)
            {
                // Emission is one sided.
                const FloatP front = dot(lwi, light.dg[0].ng);
                lfv = select(front > FloatP(0.f),
                             abs(dot(lwi, light.dg[0].n)), FloatP(0.f));
                lpdf = lfv * INVPI;
            }
            else
            {
                // Lux EvalPath where F(sw, lightV.wi /*back*/, lwo /*toward-eye*/,
                // reverse=false). SingleBSDF::F folds cos of its first arg and
                // the |sideTest| ng-Jacobian keyed to (wiW/woW).
                light.bsdf[li]->Eval(st.sw, light.wb[li], lwi, light.dg[li],
                                     TransportMode::Importance, &lf, base);
                lfv = lf.f;
                lpdf = lf.pdfRev;
                lpdfR = lf.pdf;
            }

            const SWCSpectrumP Lc0 = light.flux[li] * lfv * ef.f * eye.flux[ei];
            const MaskP preCont = base && (d2 > FloatP(EPS_DENOM)) &&
                                  (Lc0 > SWCSpectrumP(0.f));
            if (!any(preCont))
                return;

            const Point3fP shadowO =
                OffsetRay(eye.p[ei], eye.dg[ei].ng, ewi, FloatP(EPS_RAY));
            const FloatP dist = enoki::sqrt(d2);
            const FloatP tmax = dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
            const MaskP vis = ShadowTr(ctx, shadowO, ewi, tmax, st.sw, preCont);
            const MaskP cont = preCont && vis;
            if (!any(cont))
                return;
            const SWCSpectrumP Lc = Lc0 * divp(FloatP(1.f), d2);

            const FloatP ecosi = abs(dot(ewi, eye.dg[ei].ng));
            const FloatP lcoso = abs(dot(lwi, light.dg[li].ng));

            Transient tr;
            tr.isLightDirect = false;
            FillEyeTransient(tr, eye, ei, ef, ecosi, lpdf, d2, ctx);
            // lightV.dARWeight = epdfR * etPdfR / d2 * lcoso
            tr.lAR_last = ef.pdf * lcoso * divp(FloatP(1.f), d2);
            // light[nL-2].dARWeight = lpdfR * tPdfR / light[nL-2].d2 * light[nL-2].coso
            tr.lAR_prev = nL > 1 ? lpdfR * light.coso[li - 1] *
                                       divp(FloatP(1.f), light.d2[li - 1])
                                 : FloatP(0.f);
            tr.lRR_last = select(lpdf > FloatP(PDF_EPS),
                                 min(FloatP(1.f),
                                     max(ctx.lightRR, lfv * divp(FloatP(1.f), lpdf))),
                                 FloatP(0.f));
            if (nL == 1)
                tr.lRRR_last = FloatP(1.f);
            else
            {
                const FloatP den = lcoso * lpdfR;
                tr.lRRR_last =
                    select(den > FloatP(PDF_EPS),
                           min(FloatP(1.f),
                               max(ctx.eyeRR,
                                   lfv * light.cosi[li] * divp(FloatP(1.f), den))),
                           FloatP(0.f));
            }

            // pdfLightDirect: nL==1 -> direct sampling of light0 from the eye
            // vertex (Lux light->Pdf(vE.p, light0) * directWeight);
            // nL>=2 -> Lux lightDirectPdf cached by TraceLight.
            if (nL == 1)
            {
                const LightPtr selLight = enoki::gather<LightPtr>(
                    ctx.lightPtrs.data(), light.selIdx, cont);
                const FloatP pdfSolid =
                    selLight->Pdf_L(eye.p[ei], eye.dg[ei].n, light.p[0],
                                    light.dg[0].n, cont, MaskP(true)) *
                    ctx.invNLights;
                tr.pdfLightDirect =
                    ConvertDensity(eye.p[ei], pdfSolid, light.p[0], light.dg[0].ng);
                if (ctx.checkDensity)
                {
                    const FloatP area = enoki::gather<FloatP>(
                        ctx.areaByIdx.data(), light.selIdx, cont);
                    CheckAreaDensity("pdfLightDirect", tr.pdfLightDirect, area,
                                     ctx.invNLights);
                }
            }
            else
                tr.pdfLightDirect = light.directPdf1;

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, nE, light, nL, tr,
                                             ctx.diag ? &bd : nullptr));
            st.L = select(cont, st.L + Lc * w, st.L);
            TallyGhost(ctx.diag, ef, base, cont, Lc, w,
                       eye.bsdf[ei]->flags(), nE, nL);
            if (nL >= 2) // origin has no BSDF Eval, only deeper light vertices do
                TallyGhost(ctx.diag, lf, base, cont, Lc, w,
                           light.bsdf[li]->flags(), nE, nL);
            RecordFirefly(ctx.diag, ctx, st.x, st.y, cont, Lc, w, d2, nL, nE,
                          &bd);
        }

        // ------------------------------------------------------------------
        // Connect light subpath vertex to the camera
        // ------------------------------------------------------------------
        void ConnectCamera(const WalkCtx &ctx, LaneState &st,
                           const Subpath &eye, const Subpath &light,
                           int nL, const Int32P &nLight, Film &film)
        {
            if (!want(ctx.cfg, nL == 1 ? Strat::Origin : Strat::General,
                      /*nE*/ 1, nL))
                return;
            const int li = nL - 1;
            const MaskP base = st.active && (nLight >= Int32P(nL));
            if (!any(base))
                return;

            const Vector3fP dRaw = light.p[li] - eye.p[0];
            const FloatP d2 = dot(dRaw, dRaw);
            const Vector3fP ewi = dRaw * enoki::sqrt(divp(FloatP(1.f), d2));
            const Vector3fP lwi = -ewi;

            FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
            const MaskP inView =
                ctx.camera->Project(ewi, &px, &py, &We, &cosCam, base);
            if (!any(inView))
                return;

            FloatP lfv, lpdf, lpdfR(0.f);
            BSDFEvalP lf = ZeroEval();
            if (nL == 1)
            {
                const FloatP front = dot(lwi, light.dg[0].ng);
                lfv = select(front > FloatP(0.f),
                             abs(dot(lwi, light.dg[0].n)), FloatP(0.f));
                lpdf = lfv * INVPI;
            }
            else
            {
                // Lux F(sw, lightV.wi /*back*/, lwo /*toward-eye*/, false).
                light.bsdf[li]->Eval(st.sw, light.wb[li], lwi, light.dg[li],
                                     TransportMode::Importance, &lf, inView);
                lfv = lf.f;
                lpdf = lf.pdfRev;
                lpdfR = lf.pdf;
            }

            // Camera BSDF F = We (Lux PerspectiveBSDF::F).
            const SWCSpectrumP Lc0 = light.flux[li] * lfv * We;
            const MaskP preCont = inView && (d2 > FloatP(EPS_DENOM)) &&
                                  (Lc0 > SWCSpectrumP(0.f));
            if (!any(preCont))
                return;

            const FloatP dist = enoki::sqrt(d2);
            const FloatP tmax =
                dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);
            const MaskP vis =
                ShadowTr(ctx, eye.p[0], ewi, tmax, st.sw, preCont);
            const MaskP cont = preCont && vis;
            if (!any(cont))
                return;
            const SWCSpectrumP Lc = Lc0 * divp(FloatP(1.f), d2);

            const FloatP lcoso = abs(dot(lwi, light.dg[li].ng));

            Transient tr;
            tr.eAF_last = eye.pdfR[0] * cosCam * divp(FloatP(1.f), d2);
            tr.eAR_last = lpdf * cosCam * divp(FloatP(1.f), d2);
            tr.eAR_prev = FloatP(0.f);
            tr.eRR_last = FloatP(1.f);
            tr.eRRR_last = FloatP(1.f);
            tr.lAR_last = We * lcoso * divp(FloatP(1.f), d2);
            tr.lAR_prev = nL > 1
                              ? lpdfR * light.coso[li - 1] *
                                    divp(FloatP(1.f), light.d2[li - 1])
                              : FloatP(0.f);
            tr.lRR_last =
                select(lpdf > FloatP(PDF_EPS),
                       min(FloatP(1.f),
                           max(ctx.lightRR, lfv * divp(FloatP(1.f), lpdf))),
                       FloatP(0.f));
            if (nL == 1)
                tr.lRRR_last = FloatP(1.f);
            else
            {
                const FloatP den = lcoso * lpdfR;
                tr.lRRR_last =
                    select(den > FloatP(PDF_EPS),
                           min(FloatP(1.f),
                               max(ctx.eyeRR,
                                   lfv * light.cosi[li] *
                                       divp(FloatP(1.f), den))),
                           FloatP(0.f));
            }
            if (nL == 1)
            {
                const LightPtr selLight = enoki::gather<LightPtr>(
                    ctx.lightPtrs.data(), light.selIdx, cont);
                const FloatP pdfSolid =
                    selLight->Pdf_L(eye.p[0], Normal3fP(0.f), light.p[0],
                                    light.dg[0].n, cont, MaskP(false)) *
                    ctx.invNLights;
                tr.pdfLightDirect = ConvertDensity(eye.p[0], pdfSolid,
                                                   light.p[0], light.dg[0].ng);
            }
            else
            {
                tr.pdfLightDirect = light.directPdf1;
            }
            tr.isLightDirect = false;

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, 1, light, nL, tr,
                                             ctx.diag ? &bd : nullptr));
            film.SplatLight(px, py, Lc * w, st.sw, cont);
            if (nL >= 2)
                TallyGhost(ctx.diag, lf, inView, cont, Lc, w,
                           light.bsdf[li]->flags(), /*nE*/ 1, nL);
            RecordFirefly(ctx.diag, ctx, px, py, cont, Lc, w, d2, nL, /*t*/ 1,
                          &bd);
        }

        // ------------------------------------------------------------------
        // NEE at camera vertex
        // ------------------------------------------------------------------
        void ConnectCameraDirect(const WalkCtx &ctx, const Sampler &sampler,
                                 LaneState &st, const Subpath &eye,
                                 Subpath &nee, Film &film)
        {
            if (!want(ctx.cfg, Strat::NEE, /*nE*/ 1, /*nL*/ 1))
                return;
            const MaskP base = st.active && (ctx.nLights > 0);
            if (!any(base))
                return;

            const int camNeeBase = CamNeeBaseI(
                NeeBaseI(LightOriginBaseI(ctx.eyeDepth), ctx.lightDepth),
                ctx.eyeDepth);
            const UInt32P nd = UInt32P(uint32_t(camNeeBase));
            const FloatP uSel = sampler.Get1D(st.seed, st.sampleIdx, nd);
            const Point2fP uPos =
                sampler.Get2D(st.seed, st.sampleIdx, nd + UInt32P(1u));
            const FloatP uComp =
                sampler.Get1D(st.seed, st.sampleIdx, nd + UInt32P(3u));

            const UInt32P lightIdx =
                min(UInt32P(uint32_t(std::max(0, ctx.nLights - 1))),
                    floor2int<UInt32P>(uSel * FloatP(float(ctx.nLights))));
            const LightPtr selLight =
                enoki::gather<LightPtr>(ctx.lightPtrs.data(), lightIdx, base);

            Point3fP lightP(0.f);
            Vector3fP wiL(0.f);
            Normal3fP lightN(0.f);
            FloatP pdfPos(0.f);
            SWCSpectrumP Le(0.f);
            const MaskP sampleOk = selLight->Sample_Li(
                st.sw, eye.p[0], Normal3fP(0.f), uPos.x(), uPos.y(), uComp,
                &lightP, &wiL, &lightN, &pdfPos, &Le, base, MaskP(false));
            const FloatP dist = enoki::norm(lightP - eye.p[0]);
            const FloatP d2 = dist * dist;
            const FloatP pdfL = pdfPos * ctx.invNLights;
            const FloatP tmax =
                dist - enoki::max(FloatP(1e-3f), FloatP(1e-3f) * dist);

            const Vector3fP ewi =
                (lightP - eye.p[0]) * enoki::sqrt(divp(FloatP(1.f), d2));
            FloatP px(0.f), py(0.f), We(0.f), cosCam(0.f);
            const MaskP inView =
                ctx.camera->Project(ewi, &px, &py, &We, &cosCam, sampleOk);

            const MaskP tryVis =
                inView && (pdfL > FloatP(0.f)) && (We > FloatP(0.f));
            const MaskP vis =
                ShadowTr(ctx, eye.p[0], ewi, tmax, st.sw, tryVis);
            const MaskP cont = tryVis && vis;
            if (!any(cont))
                return;

            const SWCSpectrumP Lc = We * Le * divp(FloatP(1.f), pdfL);

            const FloatP cosLraw = dot(-wiL, lightN);
            const FloatP cosL =
                select(cosLraw > FloatP(0.f), cosLraw, FloatP(0.f));
            const FloatP area =
                enoki::gather<FloatP>(ctx.areaByIdx.data(), lightIdx, cont);

            nee.dAWeight[0] = ctx.invNLights * divp(FloatP(1.f), area);

            Transient tr;
            tr.isLightDirect = true;
            tr.pdfLightDirect =
                ConvertDensity(eye.p[0], pdfL, lightP, lightN);
            const FloatP lpdf = cosL * INVPI;
            tr.eAF_last = eye.pdfR[0] * cosCam * divp(FloatP(1.f), d2);
            tr.eAR_last = lpdf * cosCam * divp(FloatP(1.f), d2);
            tr.eAR_prev = FloatP(0.f);
            tr.eRR_last = FloatP(1.f);
            tr.eRRR_last = FloatP(1.f);
            tr.lAR_last = We * cosL * divp(FloatP(1.f), d2);
            tr.lRR_last =
                select(lpdf > FloatP(PDF_EPS),
                       min(FloatP(1.f),
                           max(ctx.lightRR, cosL * divp(FloatP(1.f), lpdf))),
                       FloatP(0.f));
            tr.lRRR_last = FloatP(1.f);

            Breakdown bd;
            const FloatP w = divp(FloatP(1.f),
                                  WeightPath(ctx, eye, 1, nee, 1, tr,
                                             ctx.diag ? &bd : nullptr));
            film.SplatLight(px, py, Lc * w, st.sw, cont);
            RecordFirefly(ctx.diag, ctx, px, py, cont, Lc, w, d2, /*s*/ 1,
                          /*t*/ 1, &bd);
        }
    } // namespace

    BidirIntegrator::~BidirIntegrator() = default;

    void BidirIntegrator::Start(const Scene &scene)
    {
        m_scene = &scene;
        if (m_diagCfg.diag && !m_diagStore)
        {
            m_diagStore = std::make_unique<DiagStore>();
            m_diagStore->Init(m_eyeDepth, m_lightDepth);
        }

        // TODO: handles finite area lights only.
        if (!m_warnedLights.exchange(true))
        {
            const int nGroups = scene.LightGroupCount();
            for (int g = 0; g < nGroups; ++g)
            {
                for (const Light *l : scene.GroupLights(g))
                {
                    if (!l)
                        continue;
                    if (l->IsInfinite())
                        LOG(LUX_WARNING, LUX_SYSTEM)
                            << "Bidirectional: environment/infinite lights are "
                               "unsupported (env eye hits and light subpaths from "
                               "env are not implemented); results will be biased.";
                    else if (l->TotalArea() <= 0.f)
                        LOG(LUX_WARNING, LUX_SYSTEM)
                            << "Bidirectional: non-area light (TotalArea()==0); "
                               "light subpaths through it are dropped and its NEE "
                               "weight is not MIS corrected.";
                }
            }
        }
    }

    void BidirIntegrator::End(const Scene &)
    {
        if (!m_diagStore)
            return;
        std::lock_guard<std::mutex> lk(m_diagStore->mtx);

        // Firefly probe.
        LOG(LUX_WARNING, LUX_SYSTEM)
            << "BDPT firefly probe: calls=" << m_diagStore->recordCalls
            << " maxLcw=" << m_diagStore->maxLcw
            << " threshold=" << m_diagCfg.threshold
            << " recorded=" << m_diagStore->top.size();

        uint64_t totDelta = 0, totMixed = 0;
        for (size_t t = 0; t < m_diagStore->ghost.size(); ++t)
            for (size_t s = 0; s < m_diagStore->ghost[t].size(); ++s)
            {
                const GhostCell &c = m_diagStore->ghost[t][s];
                if (c.delta == 0 && c.mixed == 0)
                    continue;
                totDelta += c.delta;
                totMixed += c.mixed;
                LOG(LUX_WARNING, LUX_SYSTEM)
                    << "BDPT diag: ghost (t=" << t << ",s=" << s
                    << ") delta=" << c.delta << " mixed=" << c.mixed
                    << " energy=" << c.energy;
            }
        LOG(LUX_WARNING, LUX_SYSTEM)
            << "BDPT diag: TOTAL ghost delta=" << totDelta
            << " mixed=" << totMixed
            << (totDelta > 0
                    ? " -- DELTA-ONLY ghosts present: connectibility bug"
                    : "");

        // Firefly top-N descending by |Lc*w|.
        std::vector<DiagStore::Firefly> sorted = m_diagStore->top;
        std::sort(sorted.begin(), sorted.end(),
                  [](const DiagStore::Firefly &a, const DiagStore::Firefly &b)
                  { return a.lcw > b.lcw; });
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            const auto &f = sorted[i];
            LOG(LUX_WARNING, LUX_SYSTEM)
                << "BDPT firefly #" << i << " (t=" << f.t << ",s=" << f.s
                << ") px=(" << f.px << "," << f.py << ") lcw=" << f.lcw
                << " lc=" << f.lc << " w=" << f.w << " d2=" << f.d2
                << " | base=" << f.base << " dirOrigin=" << f.directOrigin
                << " hitLightAlt=" << f.hitLightAlt
                << " lightExt=" << f.lightExt << " eyeExt=" << f.eyeExt
                << " directPath=" << f.directPath
                << " okL=" << int(f.okLight) << " okE=" << int(f.okEye);
        }
    }

    void BidirIntegrator::RenderTile(const Scene &scene, const Tile &tile,
                                     Film &dest, Sampler &sampler,
                                     uint64_t sampleBasis, uint32_t spp)
    {
        const Camera &camera = scene.GetCamera();

        WalkCtx ctx;
        ctx.scene = &scene;
        ctx.embree = scene.GetEmbree();
        ctx.table = &scene.GetBsdfTable();
        ctx.activeGroup = scene.GetActiveGroup();
        ctx.multiGroup = scene.LightGroupCount() > 1;
        ctx.lightPtrs = scene.GroupLights(ctx.activeGroup);
        ctx.nLights = static_cast<int>(ctx.lightPtrs.size());
        ctx.invNLights =
            ctx.nLights > 0 ? FloatP(1.f / float(ctx.nLights)) : FloatP(0.f);
        const int nArea = scene.AreaLightCount();
        ctx.areaPtrs.reserve(nArea);
        ctx.areaById.resize(size_t(std::max(0, nArea)), 0.f);
        for (int a = 0; a < nArea; ++a)
        {
            const Light *al = scene.GetAreaLight(a);
            ctx.areaPtrs.push_back(al);
            ctx.areaById[size_t(a)] = al ? al->TotalArea() : 0.f;
        }
        if (ctx.multiGroup)
        {
            ctx.areaGroup.resize(nArea);
            for (int a = 0; a < nArea; ++a)
                ctx.areaGroup[a] = scene.AreaLightGroup(a);
        }
        ctx.areaByIdx.resize(size_t(std::max(0, ctx.nLights)), 0.f);
        for (int i = 0; i < ctx.nLights; ++i)
        {
            const Light *l = ctx.lightPtrs[size_t(i)];
            ctx.areaByIdx[size_t(i)] = (l && !l->IsInfinite()) ? l->TotalArea() : 0.f;
        }
        ctx.camera = &camera;
        ctx.eyeDepth = m_eyeDepth;
        ctx.lightDepth = m_lightDepth;
        ctx.eyeRR = FloatP(m_eyeRRThreshold);
        ctx.lightRR = FloatP(m_lightRRThreshold);
        ctx.useT1 = m_useT1 && camera.SupportsProject();
        // Only the first tile of a debug render runs the density check.
        ctx.checkDensity = m_debug && !m_densityChecked.exchange(true);
        // Accumulators merged into the store at tile end.
        ctx.cfg = m_diagCfg;
        DiagLocal dl;
        if (m_diagStore)
        {
            dl.Init(m_eyeDepth, m_lightDepth);
            ctx.diag = &dl;
        }

        LaneState st;
        st.active = MaskP(false);
        JobCursor jobs(tile, spp);
        RetireBuffer retire;

        Subpath eye, light, nee;
        eye.Init(std::max(1, m_eyeDepth));
        light.Init(std::max(1, m_lightDepth));
        nee.Init(1);

        Refill(sampler, jobs, sampleBasis, st, 1);
        while (any(st.active))
        {
            Int32P nEye, nLight;
            TraceEye(ctx, camera, sampler, st, eye, &nEye); // also does s==0
            TraceLight(ctx, sampler, st, light, &nLight);

            const int maxT = std::min(m_eyeDepth, int(enoki::hmax(nEye)));
            const int maxS = std::min(m_lightDepth, int(enoki::hmax(nLight)));
            if (ctx.useT1)
            {
                ConnectCameraDirect(ctx, sampler, st, eye, nee,
                                    scene.GetFilm());
                for (int s = 1; s <= maxS; ++s)
                    ConnectCamera(ctx, st, eye, light, s, nLight,
                                  scene.GetFilm());
            }
            for (int t = 2; t <= maxT; ++t)
            {
                ConnectDirect(ctx, sampler, st, eye, nee, t, nEye);
                for (int s = 1; s <= maxS; ++s)
                    ConnectGeneral(ctx, st, eye, light, t, s, nEye, nLight);
            }

            st.L = select(enoki::isfinite(st.L), st.L, SWCSpectrumP(0.f));
            retire.Push(st, st.active);
            retire.Flush(dest, false);
            st.active = MaskP(false);
            Refill(sampler, jobs, sampleBasis, st);
        }
        retire.Flush(dest, true);

        // Merge this tile's diagnostics into the shared store under its mutex.
        if (m_diagStore)
        {
            dl.TrimFireflies();
            std::lock_guard<std::mutex> lk(m_diagStore->mtx);
            for (size_t t = 0; t < dl.ghost.size(); ++t)
                for (size_t s = 0; s < dl.ghost[t].size(); ++s)
                {
                    GhostCell &g = m_diagStore->ghost[t][s];
                    g.delta += dl.ghost[t][s].delta;
                    g.mixed += dl.ghost[t][s].mixed;
                    g.energy += dl.ghost[t][s].energy;
                }
            m_diagStore->recordCalls += dl.recordCalls;
            m_diagStore->maxLcw = std::max(m_diagStore->maxLcw, dl.maxLcw);
            for (const auto &f : dl.fireflies)
                m_diagStore->InsertFirefly(f);
        }
    }

    std::shared_ptr<SurfaceIntegrator> BidirIntegrator::CreateSurfaceIntegrator(
        const PluginContext &ctx)
    {
        ParamSet *p = ctx.params;
        const int eyeDepth = p ? p->FindOneInt("eyedepth", 8) : 8;
        const int lightDepth = p ? p->FindOneInt("lightdepth", 8) : 8;
        const int maxDepth = p ? p->FindOneInt("maxdepth", -1) : -1; // unused (legacy has none)
        const float eyeRR = p ? p->FindOneFloat("eyerrthreshold", 0.f) : 0.f;
        const float lightRR = p ? p->FindOneFloat("lightrrthreshold", 0.f) : 0.f;
        const bool useT1 = p ? p->FindOneBool("uset1", false) : false;
        const bool debug = p ? p->FindOneBool("debug", false) : false;
        DiagConfig cfg;
        cfg.onlyS = p ? p->FindOneInt("onlys", -1) : -1;
        cfg.onlyT = p ? p->FindOneInt("onlyt", -1) : -1;
        cfg.noMis = p ? p->FindOneBool("nomis", false) : false;
        cfg.misPower = p ? p->FindOneFloat("mispower", 2.f) : 2.f;
        cfg.diag = p ? p->FindOneBool("diag", false) : false;
        cfg.threshold = p ? p->FindOneFloat("diagthreshold", 0.f) : 0.f;
        if (p)
        {
            p->EraseString("lightstrategy");
            p->EraseString("lightpathstrategy");
            p->EraseString("mismode");
        }
        return std::make_shared<BidirIntegrator>(
            eyeDepth, lightDepth, maxDepth, eyeRR, lightRR, useT1, debug,
            MisMode::PBRT, -1, -1, cfg);
    }

    LUX2_REGISTER_SURFACE_INTEGRATOR(BidirIntegrator, "bidirectional");

} // namespace lux2