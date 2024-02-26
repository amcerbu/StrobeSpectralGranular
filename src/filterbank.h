// Filterbank.h
#ifndef FILTERBANK

#include "globals.h"
#include <Eigen/Dense>

using Eigen::Matrix;
using Eigen::Dynamic;
using Eigen::Map;
using Eigen::seqN;
using Eigen::Array;

namespace soundmath
{
	// preallocated filterbank
	// uses N * (o + 1) + N * o + 2 * (o + 1) + (2 * o + 1) * N + 4 * N
	// = No + N + No + 2o + 2 + 2oN + N + 4N
	// = 4No + 6N + 2o + 2
	template <typename T, size_t N, size_t order> class Filterbank
	{
		typedef Matrix<T, N, order + 1> ForwardMatrix;
		typedef Matrix<T, N, order> BackMatrix;
		typedef Matrix<T, 2 * (order + 1), 1> InputVector;
		typedef Matrix<T, 2 * (order + 1), N> OutputMatrix;
		typedef Matrix<T, N, 1> MixerVector;

	public:
		// initialize N filters of a given order, reading and working with circular buffers
		Filterbank(double k_p = 0.1, double k_g = 1) : 
			smoothing_p(relaxation(k_p)), smoothing_g(relaxation(k_g))
		{
			forwards.setZero();
			backs.setZero();
			input.setZero();
			outputs.setZero();
			
			preamps_in.setZero();
			preamps_out.setZero();

			gains_in.setZero();
			gains_out.setZero();
		}

		~Filterbank() { }

		// initalize the nth filter's coefficients
		void coefficients(int n, const std::vector<T>& forward, const std::vector<T>& back)
		{
			int coeffs = std::min<int>(order + 1, forward.size());
			for (int i = 0; i < coeffs; i++)
				forwards(n, i) = forward[i];

			coeffs = std::min<int>(order, back.size());
			for (int i = 0; i < coeffs; i++)
				backs(n, i) = back[i];
		}

		// set nth filter's preamp coefficient
		void boost(int n, T value)
		{
			preamps_in(n) = value;
		}

		// set all preamps_out coefficients
		void boost(const std::vector<T>& values)
		{
			for (int i = 0; i < std::min<int>(N, values.size()); i++)
				preamps_in(i) = values[i];

		}

		// set nth filter's mixdown coefficient
		void mix(int n, T value)
		{
			gains_in(n) = value;
		}

		// set all mixdown coefficients
		void mix(const std::vector<T>& values)
		{
			for (int i = 0; i < std::min<int>(N, values.size()); i++)
				gains_in(i) = values[i];

		}

		void open()
		{
			for (int i = 0; i < N; i++)
				gains_in(i) = 1;
		}

		// get the result of filters applied to a sample
		T operator()(T sample)
		{
			if (!computed)
				compute(sample);

			return outputs.row(origin) * gains_out;
		}

		T operator()(T sample, T (*distortion)(T in))
		{
			if (!computed)
				compute(sample);

			return ((outputs.row(origin).transpose().array() * gains_out.array()).unaryExpr(distortion)).sum();
		}

		// timestep
		void tick()
		{
			origin--;
			if (origin < 0)
				origin += order + 1;
			computed = false;
		}

	private:
		T smoothing_p, smoothing_g;

		ForwardMatrix forwards;
		BackMatrix backs;
		InputVector input;
		OutputMatrix outputs;

		MixerVector preamps_in, preamps_out, gains_in, gains_out;

		int origin = 0;
		bool computed = false; // flag in case of repeated calls to operator()

		void compute(T sample)
		{
			preamps_out = (1 - smoothing_p) * preamps_in + smoothing_p * preamps_out;
			gains_out = (1 - smoothing_g) * gains_in + smoothing_g * gains_out;

			input(origin) = sample;
			input(origin + (order + 1)) = sample;

			MixerVector temp = ((forwards * input(seqN(origin, order + 1))).array() * preamps_out.array()).matrix()
						 - (backs * outputs.block(origin + 1, 0, order, N)).diagonal();

			outputs.row(origin) = temp;
			outputs.row(origin + (order + 1)) = temp;

			computed = true;
		}
	};
}

#define FILTERBANK
#endif