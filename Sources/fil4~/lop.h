/* fil4.lv2 - highpass
 *
 * Copyright (C) 2015 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _FIL4_LOP_H
#define _FIL4_LOP_H

#include <math.h>
#include "iir.h"

#define RESLP(X) (3.0 * pow((X), 3.20772))

/* Define to use an additional high-shelf at SR/3
 * to further reduce gain near nyquist and achieve
 * a perfect -12dB slope /until the end/.
 *
 * (High-shelf because it has zero latency and
 * no phase-shift at nyquist).
 */
#define LP_EXTRA_SHELF

#ifndef SQUARE
#define SQUARE(X) ( (X) * (X) )
#endif

typedef struct {
	double z1, z2, z3, z4;
	double a, b, r, g;
	double alpha, beta, fb, tg;

	double freq, res;
	double rate;
	bool  en;
#ifdef LP_EXTRA_SHELF
	IIRProc iir_hs;
#endif
} LowPass;

static double calc_lop_alpha (double rate, double freq) {
	double fr = freq / rate;
	if (fr < 0.0002) fr = 0.0002;
	if (fr > 0.4998) fr = 0.4998;
	return 1.0 - exp (-2.0 * PI * fr);
}

static void lop_setup (LowPass *f, double rate, double freq, double res) {
	memset (f, 0, sizeof(LowPass));
	f->rate = rate;
	f->freq = freq;
	f->res  = res;
	f->en   = false;

	f->fb = RESLP(res);
	if (f->fb < 0) f->fb = 0;
	if (f->fb > 9) f->fb = 9;

	double fs = freq / sqrt(1 + f->fb);
	f->alpha = calc_lop_alpha (f->rate, fs);
	f->beta  = calc_lop_alpha (f->rate, .25 * f->rate + .5 * fs);

	const double w2 = 4 * f->freq / f->rate;
	const double w3 = f->freq / (.25 * f->rate + .5 + f->freq);
	f->tg = (1 + SQUARE(w3)) / (1 + SQUARE(w2));

	f->a = 1.0;
	f->b = 1.0;
	f->r = 0;
	f->g = 0;

#ifdef LP_EXTRA_SHELF
	iir_init (&f->iir_hs, rate);
	f->iir_hs.freq = rate / 3;
	f->iir_hs.q = .444;
	f->iir_hs.gain = 1.0;
	iir_calc_highshelf (&f->iir_hs);
#endif
}

static bool lop_interpolate (LowPass *f, bool en, double freq, double res) {
	bool changed = f->en != en;
	bool rchange = false;
	f->en = en;
	if (res != f->res) {
		f->res = res;
		f->fb = RESLP(res);
		if (f->fb < 0) f->fb = 0;
		if (f->fb > 9) f->fb = 9;
		//printf("RESONANCE: %f -> %f\n", res, f->fb);
		rchange = true;
	}

	if (freq != f->freq || rchange) {
		double fs = freq / sqrt(1 + f->fb);
		f->alpha = calc_lop_alpha (f->rate, fs);
		f->beta = calc_lop_alpha (f->rate, .25 * f->rate + .5 * fs);
		f->freq = freq;
		//printf("FREQ: %f a:%f b:%f\n", freq, f->alpha, f->beta);
		const double w2 = 4 * f->freq / f->rate;
		const double w3 = f->freq / (.25 * f->rate + .5 + f->freq);
		f->tg = (1 + SQUARE(w3)) / (1 + SQUARE(w2));
		changed = true;
	}

	const double ta= en ? f->alpha : 1.0;
	if (fabs(ta - f->a) < 1e-5) {
		f->a = ta;
	} else {
		f->a += .01 * (ta - f->a);
	}

	const double tb= en ? f->beta : 1.0;
	if (fabs(tb - f->b) < 1e-5) {
		f->b = tb;
	} else {
		f->b += .01 * (tb - f->b);
	}

	const double tr = en ? f->fb : 0.0;
	if (fabs(tr - f->r) < 1e-4) {
		f->r = tr;
	} else {
		f->r += .01 * (tr - f->r);
	}

	const double tg = en ? f->tg : 0.0;
	if (fabs(tg - f->g) < 1e-5) {
		f->g = tg;
	} else {
		f->g += .01 * (tg - f->g);
		changed = true;
	}

#ifdef LP_EXTRA_SHELF
	if (iir_interpolate (&f->iir_hs, en ? .5 : 1.0, f->rate / 3, .444)) {
		iir_calc_highshelf (&f->iir_hs);
		changed = true;
	}
#endif

/*
#ifndef NO_NAN_PROTECTION
	if (isnan(f->z1)) f->z1 = 0;
	if (isnan(f->z2)) f->z2 = 0;
	if (isnan(f->z3)) f->z3 = 0;
	if (isnan(f->z4)) f->z4 = 0;
#endif
*/

#ifndef NO_NAN_PROTECTION
	if (IS_NAN_SAMPLE(f->z1)) f->z1 = 0;
	if (IS_NAN_SAMPLE(f->z2)) f->z2 = 0;
	if (IS_NAN_SAMPLE(f->z3)) f->z3 = 0;
	if (IS_NAN_SAMPLE(f->z4)) f->z4 = 0;
#endif
	return changed;
}

static void lop_set (LowPass *f, double freq, double res) {
	lop_interpolate (f, true, freq, res);
	f->g = f->tg;
	f->r = f->fb;
	f->a = f->alpha;
	f->b = f->beta;
#ifdef LP_EXTRA_SHELF
	f->iir_hs.gain = .5;
	iir_calc_highshelf (&f->iir_hs);
#endif
}

static void lop_compute (LowPass *f, int n_samples, double *buf) {
	double z1 = f->z1;
	double z2 = f->z2;
	double z3 = f->z3;
	double z4 = f->z4;
	const double a = f->a;
	const double b = f->b;
	const double r = f->r * f->g;

	if (a == 1.0 && b == 1.0 && f->g == 0.0
#ifdef LP_EXTRA_SHELF
			&& f->iir_hs.gain == 0
#endif
		 )
	{
		// might as well save some computing power
		return;
	}

	for (int i = 0; i < n_samples; ++i) {
		const double in = (1 + r) * buf[i] - z2 * r;
		z1 += a * (in - z1);
		z2 += a * (z1 - z2);
		z3 += b * (z2 - z3);
		z4 += b * (z3 - z4);
		FIX_DENORM_DOUBLE(z4);
		buf[i] = z4;
	}
	//f->z1 = z1 + 1e-12;
	//f->z2 = z2 + 1e-12;
	//f->z3 = z3 + 1e-12;
	//f->z4 = z4 + 1e-12;
	FIX_DENORM_DOUBLE(z1);
	f->z1 = z1;
	FIX_DENORM_DOUBLE(z2);
	f->z2 = z2;
	FIX_DENORM_DOUBLE(z3);
	f->z3 = z3;
	FIX_DENORM_DOUBLE(z4);
	f->z4 = z4;

#ifdef LP_EXTRA_SHELF
	iir_compute (&f->iir_hs, n_samples, buf);
#endif
}
#endif
