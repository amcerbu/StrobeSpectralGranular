// noise.h
#include "globals.h"

#ifndef NOISE

namespace soundmath
{
	template <typename T> class Noise
	{
	public:
		Noise(T low = -1, T high = 1) : low(low), high(high)
		{
			tick();
		}

		~Noise() { }

		T operator()(bool old = false)
		{
			if (!old)
				tick();
			return value;
		}

		void tick()
		{
			value = low + ((T)rand() / RAND_MAX) * (high - low);
		}

	private:
		T low;
		T high;
		T value;
	};
}

#define NOISE
#endif