// metro.h
#include "globals.h"
#include "wave.h"
#include "oscillator.h"

#ifndef METRO

namespace soundmath
{
	// generates clicks at a specified frequency; supports phase- and frequency-modulation
	template <typename T> class Metro : public Oscillator<T>
	{
	public:
		using Oscillator<T>::phase;

		// k is relaxation time in seconds
		Metro(T f = 0, T phi = 0, T k = 2.0 / SR) : Oscillator<T>(f, phi, k)
		{
			clicked = false;
		}

		// needs to be called once per sample
		void tick()
		{
			T old = phase;
			Oscillator<T>::tick();
			clicked = phase < old;
		}

		T lookup()
		{ return phase; }

		bool operator()()
		{ return clicked; }

	private:
		bool clicked;
	};
}

#define METRO
#endif