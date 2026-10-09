/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the belowe code to the    *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// Cross-check the SoA SPD/CIE pipeline (lux2::color) against the scalar
// colorref reference for wavelength sampling, Smits RGB->SPD reconstruction
// (reflectant and illuminant), and CIE luminance.
//
// Monochromatic SWA: a lux2 lane carries ONE wavelength. The scalar reference
// carries 4 (SWCSpectrum_<4>). We drive both to the SAME single wavelength --
// the scalar's SampleSingle() slot -- and compare lux2's single value against
// the scalar's c[single_w] (and Y). Every packet lane carries the same
// broadcast input, so lane 0 is compared against the scalar result.

#include "core/color.h"
#include "core/colorsystem.h"
#include "core/texture.h"

#include "color_ref/color_ref.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using colorref::RGBColor;
using colorref::RGBReflSPD;
using colorref::RGBIllumSPD;
using colorref::SpectrumWavelengths;
using colorref::SWCSpectrum;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
	if (!cond) {
		std::cerr << "  FAIL: " << what << std::endl;
		++g_failures;
	} else {
		std::cout << "  ok:   " << what << std::endl;
	}
}

bool Close(float a, float b, float tol = 2e-4f) {
	return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
}

float RandUnit() { return float(std::rand()) / float(RAND_MAX); }

// Read lane 0 of a broadcast packet.
float First(const lux2::FloatP &p) {
	float buf[lux2::PACKET_WIDTH];
	enoki::store_unaligned(buf, p);
	return buf[0];
}

lux2::RGBColorP ToP(const RGBColor &rgb) {
	return lux2::RGBColorP(lux2::FloatP(rgb.c[0]), lux2::FloatP(rgb.c[1]),
		lux2::FloatP(rgb.c[2]));
}

// Scalar reference at a single wavelength: sample the scalar's 4 wavelengths,
// split to a single slot (SampleSingle), and return that wavelength together
// with the reflectant/illuminant SPD value c[single_w] and luminance Y there.
// The caller drives lux2 to the SAME wavelength via FromWavelength.
struct ScalarSingle {
	float wl;
	float value;
	float Y;
};
ScalarSingle ScalarRefSingle(float u1, const RGBColor &rgb, bool illum) {
	SpectrumWavelengths sw;
	sw.Sample(u1);
	const float wl = sw.SampleSingle(); // sets sw.single, returns w[single_w]
	SWCSpectrum s = illum ? SWCSpectrum(sw, RGBIllumSPD(rgb))
	                      : SWCSpectrum(sw, RGBReflSPD(rgb));
	return ScalarSingle{wl, s.c[sw.single_w], s.Y(sw)};
}

// lux2's uniform wavelength must land in [START, END) and its bins must match
// the scalar's bin computation at the same wavelength.
void CheckWavelengths() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();

		SpectrumWavelengths sw;
		sw.Sample(u1);
		const float wl = sw.SampleSingle();

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(wl));

		// The wavelength round-trips exactly and stays in range.
		ok = ok && Close(First(swp.w), wl, 1e-5f);
		ok = ok && First(swp.w) >= 380.f && First(swp.w) <= 720.f;
		// The CIE bin must match the scalar's at the single slot.
		const int j = int(sw.single_w);
		ok = ok && (swp.binXYZ[0] == sw.binsXYZ[j]);
	}
	Check(ok, "wavelength sampling");
}

// Smits RGB->SPD (reflectant) at a single wavelength must match c[single_w].
void CheckRefl() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		const ScalarSingle ref = ScalarRefSingle(u1, rgb, false);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(ref.wl));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);

		if (!Close(First(s), ref.value)) {
			std::cerr << "  [dbg t=" << t << " rgb=(" << rgb.c[0] << ","
				<< rgb.c[1] << "," << rgb.c[2] << ") w=" << ref.wl
				<< " got=" << First(s) << " ref=" << ref.value << "]\n";
		}
		ok = ok && Close(First(s), ref.value);
	}
	Check(ok, "Smits reflectant");
}

// Smits RGB->SPD (illuminant) at a single wavelength must match c[single_w].
void CheckIllum() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		const ScalarSingle ref = ScalarRefSingle(u1, rgb, true);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(ref.wl));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, true);

		ok = ok && Close(First(s), ref.value);
	}
	Check(ok, "Smits illuminant");
}

// CIE luminance at a single wavelength must match SWCSpectrum::Y (single).
void CheckLuminance() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		const ScalarSingle ref = ScalarRefSingle(u1, rgb, false);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(ref.wl));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);
		ok = ok && Close(First(lux2::SWCY(s, swp)), ref.Y);
	}
	Check(ok, "CIE luminance");
}

// CIE XYZ at a single wavelength must match XYZColor(sw, s) (single).
void CheckXYZ() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		SpectrumWavelengths sw;
		sw.Sample(u1);
		const float wl = sw.SampleSingle(); // sets sw.single
		const SWCSpectrum s = SWCSpectrum(sw, RGBReflSPD(rgb));
		const colorref::XYZColor ref(sw, s);

		lux2::SpectrumWavelengthsP swp;
		swp.FromWavelength(lux2::FloatP(wl));
		lux2::SWCSpectrumP sp = lux2::RGBToSmitsSPD(ToP(rgb), swp, false);
		const lux2::XYZColorP xyz = lux2::SWCToXYZ(sp, swp);

		if (!Close(First(xyz[0]), ref.c[0]) ||
			!Close(First(xyz[1]), ref.c[1]) ||
			!Close(First(xyz[2]), ref.c[2])) {
			std::cerr << "  [dbg t=" << t << " w=" << wl
				<< " got=(" << First(xyz[0]) << "," << First(xyz[1]) << ","
				<< First(xyz[2]) << ") ref=(" << ref.c[0] << "," << ref.c[1]
				<< "," << ref.c[2] << ")]\n";
			ok = false;
		}
	}
	Check(ok, "CIE XYZ tristimulus");
}

// SWCY must equal the Y channel of SWCToXYZ.
void CheckYMatchesXYZ() {
	bool ok = true;
	for (int t = 0; t < 32 && ok; ++t) {
		const float u1 = RandUnit();
		const RGBColor rgb(RandUnit(), RandUnit(), RandUnit());

		lux2::SpectrumWavelengthsP swp;
		swp.Sample(lux2::FloatP(u1));
		lux2::SWCSpectrumP s = lux2::RGBToSmitsSPD(
			ToP(RGBColor(RandUnit(), RandUnit(), RandUnit())), swp, false);

		ok = ok && Close(First(lux2::SWCY(s, swp)), First(lux2::SWCToXYZ(s, swp)[1]));
	}
	Check(ok, "SWCY == SWCToXYZ().Y");
}

// ConstantColorTexture must evaluate to the same single-wavelength value.
void CheckConstantTexture() {
	const RGBColor rgb(0.7f, 0.3f, 0.1f);
	const float u1 = 0.42f;

	const ScalarSingle ref = ScalarRefSingle(u1, rgb, false);

	lux2::ConstantColorTexture tex(ToP(rgb));
	lux2::SpectrumWavelengthsP swp;
	swp.FromWavelength(lux2::FloatP(ref.wl));
	lux2::SWCSpectrumP s = tex.Evaluate(lux2::DifferentialGeometryP(), swp);

	bool ok = tex.IsConstant() && Close(First(s), ref.value);
	Check(ok, "ConstantColorTexture matches scalar");
}

// ---------------------------------------------------------------------------
// lux2::ColorSystem (legacy luxrays::ColorSystem port)
// ---------------------------------------------------------------------------

void CheckColorSystemRoundTrip() {
	const lux2::ColorSystem cs;

	// RGB -> XYZ -> RGB must round-trip.
	bool ok = true;
	for (int i = 0; i < 64; ++i) {
		const lux2::RGBColor rgb(RandUnit(), RandUnit(), RandUnit());
		const lux2::RGBColor back = cs.ToRGB(cs.ToXYZ(rgb));
		ok = ok && Close(back[0], rgb[0], 1e-4f) &&
		     Close(back[1], rgb[1], 1e-4f) &&
		     Close(back[2], rgb[2], 1e-4f);
	}
	Check(ok, "ColorSystem RGB->XYZ->RGB round-trips");

	// The white point converts to (1,1,1) at unit luminance.
	const lux2::XYZColor xw = cs.ToXYZ(lux2::RGBColor(1.f));
	const lux2::RGBColor rw = cs.ToRGB(xw);
	Check(Close(rw[0], 1.f, 1e-4f) && Close(rw[1], 1.f, 1e-4f) &&
	          Close(rw[2], 1.f, 1e-4f),
	      "ColorSystem white point maps to unit RGB");

	// XYZ(1,1,1) chromaticity matches the configured white point.
	const float sum = xw[0] + xw[1] + xw[2];
	Check(Close(xw[0] / sum, cs.xWhite, 1e-4f) &&
	          Close(xw[1] / sum, cs.yWhite, 1e-4f),
	      "ToXYZ(white RGB) has the configured white chromaticity");
}

void CheckConstrain() {
	const lux2::ColorSystem cs;

	// In-gamut: a mid gray is untouched and reports no modification.
	const lux2::XYZColor gray = cs.ToXYZ(lux2::RGBColor(0.5f));
	lux2::RGBColor rgb = cs.ToRGB(gray);
	bool modified = cs.Constrain(gray, rgb);
	Check(!modified && Close(rgb[0], 0.5f, 1e-4f) &&
	          Close(rgb[1], 0.5f, 1e-4f) && Close(rgb[2], 0.5f, 1e-4f),
	      "Constrain leaves in-gamut colors unmodified");

	// Out-of-gamut: a spectral-ish blue outside SMPTE primaries.
	const lux2::XYZColor outOfGamut(0.05f, 0.03f, 0.4f);
	lux2::RGBColor rgb2 = cs.ToRGB(outOfGamut);
	Check(rgb2[0] < 0.f || rgb2[1] < 0.f || rgb2[2] < 0.f,
	      "test color is genuinely out of gamut");
	bool modified2 = cs.Constrain(outOfGamut, rgb2);
	Check(modified2 && rgb2[0] >= -1e-5f && rgb2[1] >= -1e-5f &&
	          rgb2[2] >= -1e-5f,
	      "Constrain desaturates out-of-gamut colors to non-negative RGB");

	// Zero-luminance negative case collapses to black.
	lux2::RGBColor rgb3(-1.f, 0.5f, 0.5f);
	bool modified3 = cs.Constrain(lux2::XYZColor(0.f), rgb3);
	Check(modified3 && rgb3[0] == 0.f && rgb3[1] == 0.f && rgb3[2] == 0.f,
	      "Constrain with Y<=0 collapses to black");
}

void CheckLimit() {
	const lux2::ColorSystem cs;
	const lux2::RGBColor over(1.5f, 0.8f, 1.2f);

	// In-range colors pass through every method unchanged.
	const lux2::RGBColor inRange(0.4f, 0.9f, 0.1f);
	bool ok = true;
	for (int m = 0; m < 4; ++m)
		ok = ok && enoki::all(cs.Limit(inRange, m) == inRange);
	Check(ok, "Limit passes in-range colors through");

	// method 2 (cut): per-channel clamp to [0,1].
	const lux2::RGBColor cut = cs.Limit(over, 2);
	Check(cut[0] == 1.f && cut[1] == 0.8f && cut[2] == 1.f,
	      "Limit cut clamps channels to [0,1]");

	// method 3 (darken): scale so max == 1.
	const lux2::RGBColor dark = cs.Limit(over, 3);
	Check(Close(dark[0], 1.f, 1e-5f) && Close(dark[1], 0.8f / 1.5f, 1e-5f) &&
	          Close(dark[2], 1.2f / 1.5f, 1e-5f),
	      "Limit darken scales max channel to 1 preserving hue");

	// methods 0/1 (lum/hue): result within [0,1] and modified.
	ok = true;
	for (int m = 0; m < 2; ++m) {
		const lux2::RGBColor l = cs.Limit(over, m);
		ok = ok && l[0] >= 0.f && l[0] <= 1.f && l[1] >= 0.f && l[1] <= 1.f &&
		     l[2] >= 0.f && l[2] <= 1.f;
	}
	Check(ok, "Limit lum/hue produce in-range results");
}

// ---------------------------------------------------------------------------
// lux2::ColorAdaptator (legacy luxrays::ColorAdaptator port)
// ---------------------------------------------------------------------------

void CheckColorAdaptator() {
	// Adapt(white, white) is the identity within float rounding.
	const lux2::XYZColor white(0.95f, 1.f, 1.08f);
	const lux2::ColorAdaptator id(white, white);
	bool ok = true;
	for (int i = 0; i < 32; ++i) {
		const lux2::XYZColor c(RandUnit() * 2.f, RandUnit() * 2.f,
		                       RandUnit() * 2.f);
		const lux2::XYZColor a = id.Adapt(c);
		ok = ok && Close(a[0], c[0], 1e-4f) && Close(a[1], c[1], 1e-4f) &&
		     Close(a[2], c[2], 1e-4f);
	}
	Check(ok, "ColorAdaptator(white,white) is identity");

	// Composition: (A*B).Adapt(c) == B.Adapt(A.Adapt(c)).
	const lux2::XYZColor from(0.95f, 1.f, 1.08f);
	const lux2::XYZColor mid(1.1f, 1.f, 0.85f);
	const lux2::XYZColor to(0.83f, 1.f, 0.97f);
	const lux2::ColorAdaptator A(from, mid), B(mid, to), AB(from, to);
	ok = true;
	for (int i = 0; i < 32; ++i) {
		const lux2::XYZColor c(RandUnit(), RandUnit(), RandUnit());
		const lux2::XYZColor lhs = (A * B).Adapt(c);
		const lux2::XYZColor rhs = AB.Adapt(c);
		ok = ok && Close(lhs[0], rhs[0], 1e-4f) && Close(lhs[1], rhs[1], 1e-4f) &&
		     Close(lhs[2], rhs[2], 1e-4f);
	}
	Check(ok, "ColorAdaptator composition matches direct adapt");

	// operator*=(s) scales the adapted result linearly.
	lux2::ColorAdaptator s(from, to);
	const lux2::XYZColor c(0.3f, 0.6f, 0.2f);
	const lux2::XYZColor base = s.Adapt(c);
	s *= 2.5f;
	const lux2::XYZColor scaled = s.Adapt(c);
	ok = Close(scaled[0], base[0] * 2.5f, 1e-4f) &&
	     Close(scaled[1], base[1] * 2.5f, 1e-4f) &&
	     Close(scaled[2], base[2] * 2.5f, 1e-4f);
	Check(ok, "ColorAdaptator operator*= scales the result");
}

// ---------------------------------------------------------------------------
// lux2::BlackbodyToXYZ (legacy BlackbodySPD port)
// ---------------------------------------------------------------------------

void CheckBlackbody() {
	// Cross-check against the scalar colorref BlackbodySPD at several temps.
	const float temps[] = {1900.f, 3200.f, 5600.f, 6500.f, 9300.f};
	bool ok = true;
	for (float t : temps) {
		const colorref::BlackbodySPD ref(t);
		float r[3];
		ref.ToXYZ(r);
		const lux2::XYZColor got = lux2::BlackbodyToXYZ(t);
		if (!Close(got[0], r[0], 1e-5f) || !Close(got[1], r[1], 1e-5f) ||
		    !Close(got[2], r[2], 1e-5f)) {
			std::cerr << "  [dbg bb t=" << t << " got=(" << got[0] << ","
				<< got[1] << "," << got[2] << ") ref=(" << r[0] << "," << r[1]
				<< "," << r[2] << ")]\n";
			ok = false;
		}
	}
	Check(ok, "BlackbodyToXYZ matches scalar reference (~1e-5)");

	// Y is positive and hotter bodies are bluer (Z/Y rises with temperature).
	const lux2::XYZColor warm = lux2::BlackbodyToXYZ(2500.f);
	const lux2::XYZColor cool = lux2::BlackbodyToXYZ(9000.f);
	Check(warm[1] > 0.f && cool[1] > 0.f, "Blackbody Y positive");
	Check((cool[2] / cool[1]) > (warm[2] / warm[1]),
	      "Blackbody gets bluer as temperature rises");
}

} // namespace

int main() {
	std::srand(20260921);
	CheckWavelengths();
	CheckRefl();
	CheckIllum();
	CheckLuminance();
	CheckXYZ();
	CheckYMatchesXYZ();
	CheckConstantTexture();
	CheckColorSystemRoundTrip();
	CheckConstrain();
	CheckLimit();
	CheckColorAdaptator();
	CheckBlackbody();

	if (g_failures == 0) {
		std::cout << "lux2colorcheck: ALL CHECKS PASSED" << std::endl;
		return 0;
	}
	std::cerr << "lux2colorcheck: " << g_failures << " CHECK(S) FAILED"
		<< std::endl;
	return 1;
}
