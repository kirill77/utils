#pragma once

#include <random>
#include <cstdint>

/**
 * Centralized random number generator for simulation.
 * 
 * Provides thread-safe pseudo-random number generation with:
 * - Seedable global state for reproducible simulations
 * - Consistent API across the codebase
 * - Common distribution methods
 * 
 * Usage:
 *   RNG::seed(12345);           // Seed for reproducibility (call once at startup)
 *   float x = RNG::uniform01(); // Get random float in [0, 1)
 *   int n = RNG::uniformInt(0, 10); // Get random int in [0, 10)
 */
class RNG
{
public:
    /**
     * Seed the global random number generator.
     * Call once at simulation startup for reproducible results.
     * If not called, uses std::random_device for non-deterministic seeding.
     * 
     * @param seed The seed value
     */
    static void seed(uint64_t seed);
    
    /**
     * Generate uniform random float in [0, 1).
     */
    static float uniform01();
    
    /**
     * Generate uniform random double in [0, 1).
     */
    static double uniform01d();
    
    /**
     * Generate uniform random float in [min, max).
     */
    static float uniformFloat(float min, float max);
    
    /**
     * Generate uniform random double in [min, max).
     */
    static double uniformDouble(double min, double max);
    
    /**
     * Generate uniform random integer in [min, max).
     */
    static int uniformInt(int min, int max);
    
    /**
     * Generate random unit vector on sphere (uniform distribution).
     * Returns normalized (x, y, z) components.
     */
    static void uniformSphere(float& x, float& y, float& z);
    
    /**
     * Generate random angle in [0, 2π).
     */
    static float uniformAngle();
    
    /**
     * Generate random value from normal distribution.
     * 
     * @param mean Mean of the distribution
     * @param stddev Standard deviation
     */
    static double normal(double mean, double stddev);

private:
    static thread_local std::mt19937_64 s_generator;
    static thread_local bool s_seeded;
    static uint64_t s_globalSeed;
    static bool s_globalSeedSet;
    
    // Ensure generator is seeded before use
    static void ensureSeeded();
};

