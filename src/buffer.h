// buffer.h
#include "globals.h"

#ifndef BUFFER

namespace soundmath
{
	// circular buffer object
	template <typename T> class Buffer
	{
	public:
		Buffer() { }

		~Buffer()
		{
			delete [] data;
		}

		void initialize(uint size = 0)
		{
			size += (size == 0) ? 1 : 0; // disallow size zero
			this->size = size;
			origin = 0;
			data = new T[size];
			memset(data, 0, size * sizeof(T));
		}

		Buffer(uint size)
		{
			initialize(size);
		}

		inline void tick()
		{
			origin++;
			origin %= size;
		}

		// linear-interpolated lookup (into past)
		inline T operator()(T position = 0)
		{
			int center = (int)position;
			int before = center + 1;
			T disp = position - center;

			return data[(origin - center + size) % size] * (1 - disp) + data[(origin - before + size) % size] * disp;
		}

		// linear-interpolated read (for static buffers)
		inline T operator[](T position)
		{
			int center = (int)position;
			int after = (center + 1) % size;
			T disp = position - center;

			return data[(center + size) % size] * (1 - disp) + data[(after + size) % size] * disp;	
		}

		inline void write(T value)
		{
			data[origin] = value;
		}

		inline void accum(T value)
		{
			data[origin] += value;
		}

		inline uint get_size()
		{
			return size;
		}

		inline uint get_origin()
		{
			return origin;
		}

	protected:
		T* data;
		uint size;
		uint origin;
	};

	// circular buffer object: "fixed" buffer
	template <typename T> class FBuffer : public Buffer<T>
	{
	public:
		~FBuffer() { }

		FBuffer(T* data, size_t size = 0)
		{
			this->size = size;

			this->data = data;
			memset(this->data, 0, size * sizeof(T));

			this->origin = 0;
		}
	};
}

#define BUFFER
#endif