// RMS.h
#ifndef RMSh

#include "globals.h"
#include "delay.h"

namespace soundmath
{
	template <typename T> class RMS
	{
	public:
		~RMS()
		{
			delete averager;
		}

		RMS(uint width = SR / 20) : width(width)
		{
			averager = new Delay<T>(2, width);
			averager->coefficients({{0, 1}, {width, -1}}, {{1,-1}});
		}

		T operator()(T sample)
		{
			return sqrt((*averager)(sample * sample) / width);
		}

		void tick()
		{
			averager->tick();
		}

	private:
		Delay<T>* averager;
		int width;
	};
}

#define RMSh
#endif