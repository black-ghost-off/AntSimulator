#pragma once
#include <atomic>
#include <cstdint>
#include <random>


class NumberGenerator
{
protected:
	std::random_device rd;
	std::mt19937 gen;

	NumberGenerator()
		: gen(0)
	{}

public:
	void seed(uint64_t seed_value)
	{
		gen.seed(static_cast<std::mt19937::result_type>(seed_value));
	}
};


template<typename T>
class RealNumberGenerator : public NumberGenerator
{
private:
	std::uniform_real_distribution<T> dis;

public:
	RealNumberGenerator()
		: NumberGenerator()
		, dis(0.0f, 1.0f)		
	{}
	
	// random_device is not copyable
	RealNumberGenerator(const RealNumberGenerator<T>& right)
		: NumberGenerator()
		, dis(right.dis)
	{}

	float get()
	{
		return dis(gen);
	}

	float getUnder(T max)
	{
		return get() * max;
	}

	float getRange(T min, T max)
	{
		return min + get() * (max - min);
	}

	float getRange(T width)
	{
		return getRange(-width * 0.5f, width * 0.5f);
	}
};


template<typename T>
class RNG
{
private:
	static uint64_t base_seed;
	static std::atomic<uint32_t> instance_count;

	// Each thread owns its generator so random numbers can be produced
	// concurrently without locking. Every generator gets a distinct
	// sequence derived from the base seed.
	static RealNumberGenerator<T>& instance()
	{
		static thread_local RealNumberGenerator<T> gen = createGenerator();
		return gen;
	}

	static RealNumberGenerator<T> createGenerator()
	{
		RealNumberGenerator<T> g;
		g.seed(base_seed + 0x9E3779B97F4A7C15ull * instance_count.fetch_add(1));
		return g;
	}

public:
    static void initialize()
    {
        // Kept for compatibility, generators are created lazily per thread
    }

	static void setSeed(uint64_t seed_value)
	{
		base_seed = seed_value;
		instance().seed(seed_value);
	}

	static T get()
	{
		return instance().get();
	}

	static float getUnder(T max)
	{
		return instance().getUnder(max);
	}

	static uint64_t getUintUnder(uint64_t max)
	{
		return static_cast<uint64_t>(instance().getUnder(static_cast<float>(max) + 1.0f));
	}

	static float getRange(T min, T max)
	{
		return instance().getRange(min, max);
	}

	static float getRange(T width)
	{
		return instance().getRange(width);
	}

	static float getFullRange(T width)
	{
		return instance().getRange(static_cast<T>(2.0f) * width);
	}

	static bool proba(float threshold)
	{
		return get() < threshold;
	}
};

using RNGf = RNG<float>;

template<typename T>
uint64_t RNG<T>::base_seed = 0;

template<typename T>
std::atomic<uint32_t> RNG<T>::instance_count{0};


template<typename T>
class IntegerNumberGenerator : public NumberGenerator
{
public:
	IntegerNumberGenerator()
		: NumberGenerator()
	{}

	// random_device is not copyable
	IntegerNumberGenerator(const IntegerNumberGenerator<T>&)
		: NumberGenerator()
	{}

	T getUnder(T max)
	{
		std::uniform_int_distribution<std::mt19937::result_type> dist(0, max);
		return dist(gen);
	}

	T getRange(T min, T max)
	{
		std::uniform_int_distribution<std::mt19937::result_type> dist(min, max);
		return dist(gen);
	}
};


template<typename T>
class RNGi
{
private:
	static IntegerNumberGenerator<T> gen;

public:
	static T getUnder(T max)
	{
		return gen.getUnder(max);
	}

	static T getRange(T min, T max)
	{
		return gen.getRange(min, max);
	}
};

template<typename T>
IntegerNumberGenerator<T> RNGi<T>::gen;

using RNGi32 = RNGi<int32_t>;
using RNGi64 = RNGi<int64_t>;
using RNGu32 = RNGi<uint32_t>;
using RNGu64 = RNGi<uint64_t>;
