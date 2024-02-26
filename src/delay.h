// delay.h
#ifndef DELAY

#include "globals.h"
#include "buffer.h"

namespace soundmath
{
	// template <typename T> class Delay
	// {

	// };

	// many delays; a sparse filterbank
	template <typename T> class Delay
	{
	public:
		Delay() { }
		~Delay()
		{
			delete [] forwards;
			delete [] backs;
		}

		// initialize N delays of given sparsity and maximum time
		Delay(uint sparsity, uint time) : input(time + 1), output(time + 1)
		{
			forwards = new std::pair<uint, T>[sparsity]; 
			backs = new std::pair<uint, T>[sparsity];

			for (size_t i = 0; i < sparsity; i++)
			{
				forwards[i] = {0, 0};
				backs[i] = {0, 0};
			}

			this->sparsity = sparsity;
			computed = false;
		}

		// initalize coefficients
		void coefficients(const std::vector<std::pair<uint, T>>& forward, const std::vector<std::pair<uint, T>>& back)
		{
			uint order = std::min(sparsity, (uint)forward.size());
			for (size_t i = 0; i < order; i++)
				forwards[i] = forward[i];
			for (size_t i = order; i < sparsity; i++)
				forwards[i] = {0, 0}; // zero out trailing coefficients

			order = std::min(sparsity, (uint)back.size());
			for (size_t i = 0; i < std::min(sparsity, (uint)back.size()); i++)
			{
				if (back[i].first != 0) // don't allow zero-time feedback
					backs[i] = back[i];
				else
					backs[i] = {0, 0};
			}
			for (size_t i = order; i < sparsity; i++)
				backs[i] = {0, 0}; // zero out trailing coefficients
		}

		// modulate the feedforward coeffs of nth delay
		void modulate_forward(uint n, const std::pair<uint, T>& forward)
		{ forwards[n] = forward; }

		// modulate the feedback coeffs of nth delay
		void modulate_back(uint n, const std::pair<uint, T>& back)
		{
			if (back.first == 0)
				backs[n] = {0, 0};
			else
				backs[n] = back;
		}

		// get the result of filter applied to a sample
		T operator()(T sample)
		{
			if (!computed)
			{
				input.write(sample);
				output.write(0);

				for (size_t i = 0; i < sparsity; i++) // i is delay time
				{
					std::pair<uint, T> forward = forwards[i];
					std::pair<uint, T> back = backs[i];
					output.accum(forward.second * input(forward.first) - back.second * output(back.first));
				}

				computed = true;
			}

			return output(0);
		}

		// timestep
		void tick()
		{
			input.tick();
			output.tick();
			computed = false;
		}

	private:
		Buffer<T> input; // circular buffers of inputs and outputs
		Buffer<T> output; 
		uint sparsity;

		std::pair<uint, T>* forwards; // feedforward times and coefficients
		std::pair<uint, T>* backs; // feedback times and coefficients

		bool computed; // flag in case of repeated calls to operator()
	};
}

#define DELAY
#endif