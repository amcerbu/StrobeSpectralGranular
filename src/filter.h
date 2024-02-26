// filter.h
#ifndef FILTER

#include "globals.h"
#include <complex>

namespace soundmath
{
	template <typename T> class Filter
	{
	public:
		Filter() { }
		~Filter() { }


		// initialize a filter with its feedforward and feedback coefficients
		Filter(const std::vector<T>& feedforward, const std::vector<T>& feedback)
		{
			forward = feedforward;
			back = feedback;
			back[0] = 0;
			order = std::max(forward.size(), back.size()) - 1;

			forward.resize(order + 1, 0);
			back.resize(order + 1, 0);

			reset();
		}

		// initialize a filter with its gain, and the poles and zeros of its transfer function
		Filter(T gain, const std::vector<T>& zeros, const std::vector<T>& poles)
		{
			order = std::max(zeros.size(), poles.size());

			forward = coefficients(zeros);
			reverse(forward.begin(), forward.end());
			for (int i = 0; i < order + 1; i ++)
				forward[i] *= gain;

			back = coefficients(poles);
			reverse(back.begin(), back.end());
			back[0] = 0;

			reset();
		}

		void initialize(uint order)
		{
			forward = std::vector<T>(order + 1, 0);
			back = std::vector<T>(order + 1, 0);

			this->order = order;

			reset();
		}

		// adds some padding to the ring buffer
		void reset()
		{
			buffsize = order + 2;
			output = std::vector<T>(buffsize, 0);
			history = std::vector<T>(buffsize, 0);
			origin = 0;
			computed = false;
		}

		void forget()
		{
			output = std::vector<T>(buffsize, 0);
			computed = false;
		}

		// get coefficients of a monic polynomial given its roots
		static std::vector<T> coefficients(const std::vector<T>& zeros, int start = 0)
		{
			int degree = zeros.size() - start;
			if (degree == 0)
				return {0};
			if (degree == 1)
				return {zeros[start], 1};

			std::vector<T> total(degree + 1, 0);
			T first = zeros[start];

			std::vector<T> shifted(coefficients(zeros, start + 1));

			for (int i = 0; i < degree; i++)
			{
				total[i] += first * shifted[i];
				total[i + 1] += shifted[i];
			}

			return total;
		}

		void tick()
		{
			origin++;
			origin %= buffsize;
			computed = false;
		}

		T operator()(T sample)
		{
			if (!computed)
			{
				history[origin] = sample;

				T out = 0;
				int index;
				for (int i = 0; i < order + 1; i++)
				{
					index = (origin - i + buffsize) % buffsize;
					out += forward[i] * history[index] - back[i] * output[index];
				}

				output[origin] = out;
				computed = true;
			}

			return output[origin];
		}

		void resonant(T frequency, T Q)
		{
			using namespace std::complex_literals;

			T cosine = cos(2 * PI * frequency / SR);

			std::complex<T> cosine2(cos(4 * PI * frequency / SR), 0);
			std::complex<T> sine2(sin(4 * PI * frequency / SR), 0);
			
			std::complex<T> maximum = 1.0 / (Q - 1) - 1.0 / (Q - cosine2 - 1.0i * sine2);
			double amplitude = 1 / sqrt(abs(maximum));

			forward = std::vector<T>({amplitude, 0, -amplitude});
			back = std::vector<T>({0, -2 * Q * cosine, Q * Q});
			order = 2;
		}

		void bandpass(T frequency, T Q)
		{
			T theta = 2 * PI * frequency / SR;
			T beta = 0.5 * (1 - tan(theta / (2 * Q))) / (1 + tan(theta / (2 * Q)));
			T gamma = cos(theta) * (0.5 + beta);
			T alpha = 0.5 * (0.5 - beta);

			forward = std::vector<T>({2 * alpha, 0, -2 * alpha});
			back = std::vector<T>({0, -gamma, beta});
			order = 2;
		}

		

	private:
		int order;
		int origin;
		int buffsize;
		bool computed;

		std::vector<T> forward;
		std::vector<T> back;
		std::vector<T> output;
		std::vector<T> history;
	};
}

#define FILTER
#endif