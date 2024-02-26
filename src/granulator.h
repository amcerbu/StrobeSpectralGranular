// granulator.h
#include "globals.h"
#include "wave.h"
#include "buffer.h"
#include "noise.h"
#include "metro.h"

#ifndef GRANULATOR

namespace soundmath
{
	template <typename T, size_t polyphony = 64> class Granulator
	{
	public:
		Granulator() { }
		~Granulator() { }

		Granulator(Wave<T>* window, Buffer<T>* source) :
			window(window), source(source), size(source->get_size())
		{
			memset(ticks, 0, polyphony * sizeof(size_t));
			memset(offsets, 0, polyphony * sizeof(T));
			memset(sizes, 0, polyphony * sizeof(T));
			memset(speeds, 0, polyphony * sizeof(T));
			memset(gains, 0, polyphony * sizeof(T));
			// memset(pans, 0, polyphony * sizeof(T));
			memset(active, false, polyphony * sizeof(bool));
		}

		// request a grain; return voice number
		int request(T offset, T size, T speed, T gain, T pan)
		{
			if (size == 0)
				return -1;
			
			offset = std::max(offset, size * (speed - 1)); // keep things causal
			int voice = -1;
			for (size_t i = 0; i < polyphony; i++) // inefficient: looks for first vacant voice
				if (!active[i])
				{
					voice = i;
					break;
				}

			if (voice >= 0)
			{		
				offsets[voice] = SR * offset; // delay in samples
				sizes[voice] = SR * size; // size in samples
				speeds[voice] = speed; // playback speed (negative numbers permitted)
				gains[voice] = gain;
				// pans[voice] = pan; // not in use
				active[voice] = true;
				activity++;

				ticks[voice] = 0;
			}

			return voice;
		}

		void tick()
		{
			for (size_t i = 0; i < polyphony; i++)
				if (active[i])
					ticks[i]++;
		}

		T operator()()
		{
			T out = 0;
			for (size_t i = 0; i < polyphony; i++) // somewhat inefficient
				if (active[i])
				{
					T phase = (T)ticks[i] / sizes[i];
					out += gains[i] * (*source)(offsets[i] + (1 - speeds[i]) * ticks[i]) * (*window)(phase);
					if (ticks[i] >= sizes[i])
					{
						active[i] = false;
						activity--;
					}
				}

			return out;
		}

		bool idle()
		{
			return activity == 0;
		}

	public:
		size_t activity = 0;

	private:
		Wave<T>* window;
		Buffer<T>* source;
		size_t size;

		size_t ticks[polyphony];
		T offsets[polyphony];
		T sizes[polyphony];
		T speeds[polyphony];
		T gains[polyphony];
		// T pans[polyphony];

		bool active[polyphony];
	};

	template <typename T> class Granary
	{
	public:
		static const int shape = 0;
		static const int jitter = 1;
		static const int speed = 2;
		static const int warble = 3;
		static const int size = 4;
		static const int texture = 5;
		static const int density = 6;
		static const int spray = 7;
		static const int pan = 8;
		static const int scatter = 9;
		static const int gain = 10;
		static const int wobble = 11;
		static const int n_params = 12;

	public:
		Granary()
		{
			randomizer = new Noise<T>();
			timekeeper = new Metro<T>();
		}

		~Granary()
		{
			delete randomizer;
			delete timekeeper;
		}

		void tick()
		{
			timekeeper->tick();
		}

		bool parameters(T* the_offset, T* the_size, T* the_speed, T* the_gain, T* the_pan)
		{
			if ((*timekeeper)() && (1 + (*randomizer)() > 2 * params[spray]))
			{
				*the_offset = 0.5 * (1.0 + (*randomizer)()) * params[jitter];
				*the_size = params[size] * pow(2, params[texture] * (*randomizer)());
				*the_speed = params[speed] * pow(2, params[warble] * (*randomizer)());
				*the_gain = params[gain] * pow(2, params[wobble] * (*randomizer)());
				*the_pan = params[pan] + (*randomizer)();

				return true;
			}

			return false;
		}

		void instruct(T param, int index)
		{
			params[index] = param;
			if (index == spray || index == density)
				timekeeper->freqmod(params[density] / (1 - params[spray] * 0.999));
		}

	private:
		T params[n_params];

		Noise<T>* randomizer;
		Metro<T>* timekeeper;
	};

}

#define GRANULATOR
#endif