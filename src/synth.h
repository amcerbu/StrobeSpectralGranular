// synth.h
#ifndef SYNTH

#include "globals.h"
#include "oscillator.h"
#include "wave.h"

namespace soundmath
{
	template <typename T> class Synth : public Oscillator<T>
	{
	public:
		Synth(Wave<T>* form, T f, T phi = 0, T k = 2.0 / SR) : Oscillator<T>(f, phi, k)
		{ waveform = form; }

		T operator()()
		{ return (*waveform)(this->lookup()); }

	private:
		Wave<T>* waveform;
	};
}

#define SYNTH
#endif