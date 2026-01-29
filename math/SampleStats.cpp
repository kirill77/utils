#include "SampleStats.h"
#include <algorithm>

void SampleStats::addSample(double fValue)
{
    m_nCount++;
    double fDelta = fValue - m_fMean;
    m_fMean += fDelta / static_cast<double>(m_nCount);
    double fDelta2 = fValue - m_fMean;
    m_fM2 += fDelta * fDelta2;
}

size_t SampleStats::getCount() const
{
    return m_nCount;
}

double SampleStats::getMean() const
{
    return m_fMean;
}

double SampleStats::getVariance() const
{
    if (m_nCount < 2)
    {
        return 0.0;
    }
    return m_fM2 / static_cast<double>(m_nCount - 1);
}

double SampleStats::getSigma() const
{
    return std::sqrt(getVariance());
}

double SampleStats::getStandardError() const
{
    if (m_nCount < 2)
    {
        return 0.0;
    }
    return getSigma() / std::sqrt(static_cast<double>(m_nCount));
}

bool SampleStats::isSameDistribution(const SampleStats& other, double fConfidenceLevel) const
{
    // Need at least 2 samples in each distribution for meaningful comparison
    if (m_nCount < 2 || other.m_nCount < 2)
    {
        return false;
    }
    
    double fVar1 = getVariance();
    double fVar2 = other.getVariance();
    double fN1 = static_cast<double>(m_nCount);
    double fN2 = static_cast<double>(other.m_nCount);
    
    // Standard error of the difference of means
    double fSE1 = fVar1 / fN1;
    double fSE2 = fVar2 / fN2;
    double fSEDiff = std::sqrt(fSE1 + fSE2);
    
    // Avoid division by zero if both variances are zero
    if (fSEDiff < 1e-15)
    {
        // Both distributions have zero variance; compare means directly
        return std::abs(m_fMean - other.m_fMean) < 1e-15;
    }
    
    // Welch's t-statistic
    double fT = std::abs(m_fMean - other.m_fMean) / fSEDiff;
    
    // Welch-Satterthwaite degrees of freedom
    double fNumerator = (fSE1 + fSE2) * (fSE1 + fSE2);
    double fDenom1 = (fSE1 * fSE1) / (fN1 - 1.0);
    double fDenom2 = (fSE2 * fSE2) / (fN2 - 1.0);
    double fDF = fNumerator / (fDenom1 + fDenom2);
    
    // Handle edge case where one variance is zero
    if (!std::isfinite(fDF))
    {
        fDF = std::min(fN1, fN2) - 1.0;
    }
    fDF = std::max(fDF, 1.0);
    
    // Get critical t-value for two-tailed test
    double fTCritical = getTCritical(fConfidenceLevel, fDF);
    
    // If t-statistic is less than critical value, means are not significantly different
    return fT < fTCritical;
}

void SampleStats::merge(const SampleStats& other)
{
    if (other.m_nCount == 0)
    {
        return;
    }
    
    if (m_nCount == 0)
    {
        *this = other;
        return;
    }
    
    // Parallel algorithm for combining running statistics
    size_t nCombinedCount = m_nCount + other.m_nCount;
    double fN1 = static_cast<double>(m_nCount);
    double fN2 = static_cast<double>(other.m_nCount);
    double fNCombined = static_cast<double>(nCombinedCount);
    
    double fDelta = other.m_fMean - m_fMean;
    double fCombinedMean = (fN1 * m_fMean + fN2 * other.m_fMean) / fNCombined;
    double fCombinedM2 = m_fM2 + other.m_fM2 + fDelta * fDelta * fN1 * fN2 / fNCombined;
    
    m_nCount = nCombinedCount;
    m_fMean = fCombinedMean;
    m_fM2 = fCombinedM2;
}

SampleStats SampleStats::merged(const SampleStats& a, const SampleStats& b)
{
    SampleStats result = a;
    result.merge(b);
    return result;
}

void SampleStats::clear()
{
    m_nCount = 0;
    m_fMean = 0.0;
    m_fM2 = 0.0;
}

bool SampleStats::isEmpty() const
{
    return m_nCount == 0;
}

double SampleStats::getTCritical(double fConfidenceLevel, double fDegreesOfFreedom)
{
    // Approximation of the inverse t-distribution for two-tailed test
    // Based on Abramowitz and Stegun approximation with refinements
    
    double fAlpha = 1.0 - fConfidenceLevel;
    double fP = 1.0 - fAlpha / 2.0;  // One-tailed probability
    
    // Initial approximation using inverse normal (Hastings approximation)
    double fU = fP;
    double fT0 = std::sqrt(-2.0 * std::log(1.0 - fU));
    
    // Coefficients for rational approximation
    constexpr double c0 = 2.515517;
    constexpr double c1 = 0.802853;
    constexpr double c2 = 0.010328;
    constexpr double d1 = 1.432788;
    constexpr double d2 = 0.189269;
    constexpr double d3 = 0.001308;
    
    double fZ = fT0 - (c0 + c1 * fT0 + c2 * fT0 * fT0) / 
                      (1.0 + d1 * fT0 + d2 * fT0 * fT0 + d3 * fT0 * fT0 * fT0);
    
    // Cornish-Fisher expansion to convert z to t
    double fDF = fDegreesOfFreedom;
    double fG1 = (fZ * fZ * fZ + fZ) / 4.0;
    double fG2 = (5.0 * fZ * fZ * fZ * fZ * fZ + 16.0 * fZ * fZ * fZ + 3.0 * fZ) / 96.0;
    double fG3 = (3.0 * fZ * fZ * fZ * fZ * fZ * fZ * fZ + 
                  19.0 * fZ * fZ * fZ * fZ * fZ + 
                  17.0 * fZ * fZ * fZ - 15.0 * fZ) / 384.0;
    
    double fTCrit = fZ + fG1 / fDF + fG2 / (fDF * fDF) + fG3 / (fDF * fDF * fDF);
    
    return fTCrit;
}
