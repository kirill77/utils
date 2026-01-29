#pragma once

#include <cstddef>
#include <cmath>

/**
 * Collects samples and computes running statistics for a normal distribution.
 * 
 * Uses Welford's online algorithm for numerically stable computation of
 * mean and variance without storing individual samples.
 * 
 * Usage:
 *   SampleStats stats;
 *   stats.addSample(16.5);
 *   stats.addSample(16.7);
 *   stats.addSample(16.4);
 *   
 *   double avg = stats.getMean();     // ~16.53
 *   double sigma = stats.getSigma();  // standard deviation
 *   
 *   // Compare two distributions
 *   if (stats1.isSameDistribution(stats2)) { ... }
 *   
 *   // Merge distributions
 *   SampleStats combined = SampleStats::merged(stats1, stats2);
 */
class SampleStats
{
public:
    /**
     * Add a new sample value to the distribution.
     */
    void addSample(double fValue);
    
    /**
     * Get the number of samples collected.
     */
    size_t getCount() const;
    
    /**
     * Get the mean (average) of all samples.
     * Returns 0 if no samples have been added.
     */
    double getMean() const;
    
    /**
     * Get the sample variance (unbiased estimator using n-1 denominator).
     * Returns 0 if fewer than 2 samples have been added.
     */
    double getVariance() const;
    
    /**
     * Get the standard deviation (sigma) of all samples.
     * Returns 0 if fewer than 2 samples have been added.
     */
    double getSigma() const;
    
    /**
     * Get the standard error of the mean (sigma / sqrt(n)).
     * Returns 0 if fewer than 2 samples have been added.
     */
    double getStandardError() const;
    
    /**
     * Test if this distribution and another likely come from the same population.
     * 
     * Uses Welch's t-test which handles unequal variances and sample sizes.
     * 
     * @param other The other distribution to compare against
     * @param fConfidenceLevel Confidence level for the test (default 0.95)
     * @return true if the means are statistically indistinguishable at the given confidence
     */
    bool isSameDistribution(const SampleStats& other, double fConfidenceLevel = 0.95) const;
    
    /**
     * Merge another distribution's statistics into this one.
     * After merging, this object represents the combined distribution.
     */
    void merge(const SampleStats& other);
    
    /**
     * Create a new SampleStats that represents the merged distribution of two inputs.
     */
    static SampleStats merged(const SampleStats& a, const SampleStats& b);
    
    /**
     * Reset to empty state.
     */
    void clear();
    
    /**
     * Check if any samples have been added.
     */
    bool isEmpty() const;

private:
    size_t m_nCount = 0;
    double m_fMean = 0.0;
    double m_fM2 = 0.0;  // Sum of squared deviations from the mean (for Welford's algorithm)
    
    // Helper: compute t-critical value for given confidence level and degrees of freedom
    // Uses approximation suitable for df >= 1
    static double getTCritical(double fConfidenceLevel, double fDegreesOfFreedom);
};
