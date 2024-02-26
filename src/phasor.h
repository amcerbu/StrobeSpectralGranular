// phasor.h
#ifndef PHASOR

#include "globals.h"

namespace soundmath
{
	using namespace std::complex_literals;

	template <typename T> class Spinner
	{
	public:
		Spinner(T f = 0, T phi = 0)
		{
			step = {std::cos(2 * PI * f / SR), std::sin(2 * PI * f / SR)};

			// if you wanted some nonzero initial phase for some reason
			state = {std::cos(2 * PI * phi / SR), std::sin(2 * PI * f / SR)};

			// this is what's going to be hitting state every frame
			update = step;
		}

		// needs to be called once per sample
		void tick()
		{
			state *= update;
			state *= 2 / (1 + std::norm(state)); // cheap (stable) normalization
		}

		std::complex<T> operator()()
		{
			return state;
		}

		T real()
		{
			return std::real(state);
		}

		T imag()
		{
			return std::imag(state);
		}

		// pushes the update complex number along the line tangent to unit circle at 
		void freqmod(T deviation)
		{
			update = step + (2 * PI / SR) * std::complex<T>(0,1) * deviation * step;
		}

	protected:
		std::complex<T> step;
		std::complex<T> update;
		std::complex<T> state;
	};
}

#define PHASOR
#endif