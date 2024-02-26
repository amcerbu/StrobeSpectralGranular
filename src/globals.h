#pragma once
#ifndef GLOBALS

#define PI 3.1415926535897932384626433832795
#define SR 48000
// #define SR 96000

#include <cmath>
#include <complex>
#include "shy_fft.h"

#define A4 440.0

namespace soundmath
{
	typedef float S; // sample type

	template<typename T> T relaxation(T k)
	{
		const static T order = log2(0.000000001);
		if (k == 0)
			return 0; 
		return pow(2.0, order / (fmax(0, k) * SR));
	}

	template<typename T> T ftom(T frequency)
	{
		return 69 + log2(frequency / A4) * 12;
	}

	template<typename T> T sign(T x)
	{
		return (x > 0) - (x < 0);
	}
}

#define GLOBALS
#endif