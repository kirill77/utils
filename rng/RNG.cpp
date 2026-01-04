#include "RNG.h"
#include <cmath>

// Static member definitions
thread_local std::mt19937_64 RNG::s_generator;
thread_local bool RNG::s_seeded = false;
uint64_t RNG::s_globalSeed = 0;
bool RNG::s_globalSeedSet = false;

void RNG::seed(uint64_t seed)
{
    s_globalSeed = seed;
    s_globalSeedSet = true;
    // Reset thread-local seeded flag so next call will use new seed
    s_seeded = false;
}

void RNG::ensureSeeded()
{
    if (!s_seeded)
    {
        if (s_globalSeedSet)
        {
            // Use global seed (for reproducibility)
            s_generator.seed(s_globalSeed);
        }
        else
        {
            // Use random device for non-deterministic seeding
            std::random_device rd;
            s_generator.seed(rd());
        }
        s_seeded = true;
    }
}

float RNG::uniform01()
{
    ensureSeeded();
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(s_generator);
}

double RNG::uniform01d()
{
    ensureSeeded();
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(s_generator);
}

float RNG::uniformFloat(float min, float max)
{
    ensureSeeded();
    std::uniform_real_distribution<float> dist(min, max);
    return dist(s_generator);
}

double RNG::uniformDouble(double min, double max)
{
    ensureSeeded();
    std::uniform_real_distribution<double> dist(min, max);
    return dist(s_generator);
}

int RNG::uniformInt(int min, int max)
{
    ensureSeeded();
    std::uniform_int_distribution<int> dist(min, max - 1);  // [min, max)
    return dist(s_generator);
}

void RNG::uniformSphere(float& x, float& y, float& z)
{
    ensureSeeded();
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    // Use spherical coordinates with uniform distribution
    float u = dist(s_generator);
    float v = dist(s_generator);
    
    float theta = 2.0f * 3.14159265358979323846f * u;
    float phi = std::acos(2.0f * v - 1.0f);
    
    x = std::sin(phi) * std::cos(theta);
    y = std::sin(phi) * std::sin(theta);
    z = std::cos(phi);
}

float RNG::uniformAngle()
{
    ensureSeeded();
    std::uniform_real_distribution<float> dist(0.0f, 2.0f * 3.14159265358979323846f);
    return dist(s_generator);
}

double RNG::normal(double mean, double stddev)
{
    ensureSeeded();
    std::normal_distribution<double> dist(mean, stddev);
    return dist(s_generator);
}

