// oscillator.h
#ifndef OSCILLATOR

#include "globals.h"
#include "wave.h"

namespace soundmath
{
	// An Oscillator has an associated frequency and phase. The frequency is used to update the phase each sample.
	// This update requires a call to tick.
	// Phase and frequency can be modulated; such modulation is smoothed according to a "drag coefficient"
	template <typename T> class Oscillator
	{
	public:
		// k is relaxation time in seconds
		Oscillator(T f = 0, T phi = 0, T k = 2.0 / SR)
		{
			frequency = abs(f);
			target_freq = frequency;
			phase = fmax(0, phi);
			target_phase = phase;

			stiffness = relaxation(k);
			// stiffness = k;
		}

		// needs to be called once per sample
		void tick()
		{
			phase += frequency / SR;
			target_phase += frequency / SR;

			frequency = target_freq * (1 - stiffness) + frequency * stiffness;
			T weight = (1 - stiffness) * cycle(2 * abs(target_phase - phase) + 0.25); // deals with ambiguity of phasemod(0.5)
			phase = weight * target_phase + (1 - weight) * phase;

			phase -= int(phase);
			target_phase -= int(target_phase);
		}

		T lookup()
		{ return phase; }

		T operator()()
		{ return phase; }

		void freqmod(T target)
		{ target_freq = target; }

		void phasemod(T offset)
		{
			// ensure target_phase is in [0,1)
			target_phase += offset;
			target_phase -= int(target_phase);
			target_phase += 1;
			target_phase -= int(target_phase);
		}

		void reset(T f)
		{
			frequency = target_freq = f;
			phase = target_phase = 0;
		}

	protected:
		T phase;
		T frequency;
		T target_freq;
		T target_phase;
		T stiffness;
	};
}

#define OSCILLATOR
#endif