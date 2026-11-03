/**
 * @file dsp_filter.cpp
 * @brief Implementation of digital signal processing filters
 * 
 * Implements various digital filters for audio processing:
 * - Notch filters (IIR) for frequency-specific attenuation (tinnitus relief)
 * - Low-pass, high-pass, and band-pass filters
 * - All-pass filters for phase manipulation
 * - FIR notch filter
 * 
 * @version 0.1.0
 */

#include "dsp_filter.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tinnotus {
namespace dsp {

// ============================================================================
// Base Filter Implementation
// ============================================================================

BaseFilter::BaseFilter(int sampleRate, double frequency, double qFactor)
    : sampleRate_(sampleRate), frequency_(frequency), qFactor_(qFactor) {
    
    calculateCoefficients();
}

bool BaseFilter::setSampleRate(int newSampleRate) {
    if (newSampleRate == sampleRate_) return false;
    
    sampleRate_ = newSampleRate;
    calculateCoefficients();
    coefficientsChanged_ = true;
    return true;
}

float BaseFilter::processSample(float sample) {
    if (coefficientsChanged_) {
        coefficientsChanged_ = false;
        // Recompute coefficients if needed
    }
    
    // Direct form II transposed
    float y0 = history_[0];
    float y1 = history_[1];
    
    history_[0] = b0_ * sample + b1_ * y0 + b2_ * y1;
    history_[1] = a0_ * y0 + b1_ * y1 + b2_ * history_[1] - a1_ * history_[1] - a2_ * y1;
    
    return history_[0];
}

void BaseFilter::processBuffer(const float* input, float* output, int numFrames) {
    for (int i = 0; i < numFrames; i++) {
        output[i] = processSample(input[i]);
    }
}

double BaseFilter::getFrequency() const {
    return frequency_;
}

void BaseFilter::setFrequency(double newFrequency) {
    if (std::abs(newFrequency - frequency_) < 0.01) return;
    frequency_ = newFrequency;
    calculateCoefficients();
    coefficientsChanged_ = true;
}

double BaseFilter::getQFactor() const {
    return qFactor_;
}

void BaseFilter::setQFactor(double newQFactor) {
    if (std::abs(newQFactor - qFactor_) < 0.1) return;
    qFactor_ = newQFactor;
    calculateCoefficients();
    coefficientsChanged_ = true;
}

void BaseFilter::onCoefficientChanged() const {
    // Virtual function, derived classes override if needed
}

// ============================================================================
// Notch Filter Implementation (IIR SOS)
// ============================================================================

NotchFilter* NotchFilter::create(int sampleRate, double frequency, double bandwidth) {
    auto* filter = new NotchFilter(sampleRate, frequency, bandwidth);
    filter->calculateNotchCoefficients();
    return filter;
}

NotchFilter::NotchFilter(int sampleRate, double frequency, double bandwidth)
    : BaseFilter(sampleRate, frequency, bandwidth) {
    
    bandwidth_ = bandwidth;
    setBandwidth(bandwidth);
}

NotchFilter::~NotchFilter() {
    delete[] coefficients_[0];
    delete[] coefficients_[1];
    delete[] coefficients_[2];
    delete[] coefficients_[3];
    delete[] coefficients_[4];
    delete[] coefficients_[5];
}

void NotchFilter::setBandwidth(double newBandwidth) {
    if (std::abs(newBandwidth - bandwidth_) < 0.1) return;
    bandwidth_ = newBandwidth;
    calculateNotchCoefficients();
    coefficientsChanged_ = true;
}

void NotchFilter::setQFactor(double newQFactor) {
    // Q = bandwidth / (2 * log(2) * sampleRate / frequency)
    // bandwidth = Q * frequency / 44100 * PI * 2 / log(2)
    double tempBW = newQFactor * frequency_ / std::log(2.0) / sampleRate;
    double alpha = std::sin(hzToRadians(tempBW, sampleRate));
    double cos_omega = std::cos(hzToRadians(frequency_, sampleRate));
    double tempAlpha = alpha * alpha;
    
    // Adjust coefficients directly
    coefficients_[3] = -2.0 * tempAlpha * cos_omega;
    coefficients_[4] = tempAlpha;
    
    normalizeCoefficients();
    coefficientsChanged_ = true;
}

double NotchFilter::getBandwidth() const {
    return bandwidth_;
}

double NotchFilter::getAttenuation(double freq) const {
    // Approximate attenuation calculation
    // At notch center frequency, attenuation should be maximum
    if (freq < frequency_ - bandwidth_ || freq > frequency_ + bandwidth_) {
        return -0.1;  // Minimal attenuation outside bandwidth
    }
    
    // Simple approximation: attenuation ~ -Q dB at center frequency
    double Q = qFactor_;
    return -std::log(Q + 1) * 20.0;  // Convert to dB approximation
}

void NotchFilter::calculateNotchCoefficients() {
    double omega = hzToRadians(frequency_, sampleRate);
    double wDelta = bandwidth_ / sampleRate;
    double Q = qFactor_;
    
    // Calculate alpha for notch filter
    double alpha = 2.0 * wDelta * Q / (1.0 + 2.0 * wDelta * Q);
    
    double cos_omega = std::cos(omega);
    
    // Direct form II transposed coefficients
    // H(z) = (1 - 2*cos(ω)z^(-1) + z^(-2)) / (1 - 2*α*cos(ω)z^(-1) + α^2*z^(-2))
    
    b0_ = 1.0 - alpha * (1.0 - alpha);
    b1_ = -2.0 * (1.0 - alpha) * cos_omega;
    b2_ = 1.0 - alpha;
    
    a0_ = 1.0;
    a1_ = 2.0 * alpha * cos_omega;
    a2_ = alpha * alpha;
    
    // Initialize SOS format (5x2 for notch)
    int numSections = 5;
    coefficients_.clear();
    
    coefficients_[0] = new double[numSections * 2];
    coefficients_[1] = new double[numSections * 2];
    coefficients_[2] = new double[numSections * 2];
    coefficients_[3] = new double[numSections * 2];
    coefficients_[4] = new double[numSections * 2];
    coefficients_[5] = new double[numSections * 2];
    
    for (int i = 0; i < numSections; i++) {
        double alpha_i = alpha * std::pow(i + 1, 0.3) / std::pow(numSections, 0.3);
        double omega_i = std::cos(omega * std::pow(i + 1, 0.1));
        alpha_i = std::min(alpha_i, 0.99);
        
        coefficients_[i*2 + 0] = 1.0;
        coefficients_[i*2 + 1] = -2.0 * omega_i;
        coefficients_[i*2 + 2] = 1.0;
        coefficients_[i*2 + 3] = -2.0 * alpha_i * omega_i;
        coefficients_[i*2 + 4] = alpha_i * alpha_i;
        coefficients_[i*2 + 5] = 0.0;
    }
    
    normalizeCoefficients();
}

void NotchFilter::normalizeCoefficients() {
    // Normalize coefficients to prevent overflow
    double normA0 = 0.0;
    for (int i = 0; i < 6; i++) {
        normA0 += std::abs(coefficients_[i]);
    }
    
    if (normA0 > 0.001) {
        for (int i = 0; i < 6; i++) {
            coefficients_[i] /= normA0;
        }
    }
}

// ============================================================================
// Notch Filter Bank
// ============================================================================

NotchFilterBank::NotchFilterBank(int sampleRate, int maxNotches)
    : sampleRate_(sampleRate), maxNotches_(maxNotches) {
    
    notches_.clear();
    
    // Create default notch filters for common tinnitus frequencies
    int defaultFrequency = 4000;
    for (int i = 0; i < std::min(4, maxNotches_); i++) {
        notches_.push_back(NotchFilter::create(sampleRate, defaultFrequency, 500.0));
    }
}

NotchFilterBank::~NotchFilterBank() {
    for (auto* notch : notches_) {
        delete notch;
    }
    notches_.clear();
}

int NotchFilterBank::addNotch(double frequency, double qFactor) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate frequency range (typical tinnitus: 100-8000 Hz)
    if (frequency < 100.0 || frequency > 8000.0) {
        return -1;
    }
    
    // Check if notch already exists
    for (int i = 0; i < (int)notches_.size(); i++) {
        if (std::abs(notches_[i]->getFrequency() - frequency) < qFactor * 5.0) {
            // Merge with existing notch
            notches_[i]->setBandwidth(notches_[i]->getBandwidth() + 100.0);
            return -1;
        }
    }
    
    // Add new notch
    if ((int)notches_.size() >= maxNotches_) {
        return -1;  // Bank full
    }
    
    notches_.push_back(NotchFilter::create(sampleRate, frequency, qFactor));
    return (int)notches_.size() - 1;
}

bool NotchFilterBank::removeNotch(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < 0 || index >= (int)notches_.size()) {
        return false;
    }
    
    delete notches_[index];
    notches_.erase(notches_.begin() + index);
    return true;
}

void NotchFilterBank::removeLowestNotch() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (notches_.empty()) return;
    
    auto* lowest = notches_[0];
    // Replace with next notch if it exists
    if (notches_.size() > 1) {
        delete notches_[0];
        notches_[0] = notches_[1];
        notches_.erase(notches_.begin() + 1);
    } else {
        delete notches_[0];
        notches_[0] = nullptr;
    }
}

void NotchFilterBank::removeHighestNotch() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (notches_.empty()) return;
    
    delete notches_.back();
    notches_.pop_back();
}

void NotchFilterBank::replaceAll(const std::vector<double>& frequencies, double qFactor) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    notches_.clear();
    for (double freq : frequencies) {
        if (freq >= 100.0 && freq <= 8000.0) {
            notches_.push_back(NotchFilter::create(sampleRate_, freq, qFactor));
        }
    }
}

bool NotchFilterBank::hasNotch(double frequency, double tolerance) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto* notch : notches_) {
        if (std::abs(notch->getFrequency() - frequency) < tolerance) {
            return true;
        }
    }
    return false;
}

NotchFilter* NotchFilterBank::getNotch(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < 0 || index >= (int)notches_.size()) {
        return nullptr;
    }
    
    return notches_[index];
}

float NotchFilterBank::process(float sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    float output = sample;
    for (auto* notch : notches_) {
        output = notch->processSample(output);
    }
    return output;
}

void NotchFilterBank::processBuffer(const float* input, float* output, int frames) {
    for (int i = 0; i < frames; i++) {
        output[i] = process(input[i]);
    }
}

void NotchFilterBank::fadeNotchIn(int index, double durationSec, double targetQ) {
    // Placeholder - would need ring buffer for smooth transitions
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= 0 && index < (int)notches_.size()) {
        notches_[index]->setQFactor(std::min(targetQ, 10.0));
    }
}

void NotchFilterBank::fadeNotchOut(int index, double durationSec) {
    // Placeholder - would need ring buffer for smooth transitions
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= 0 && index < (int)notches_.size()) {
        notches_[index]->setQFactor(5.0);  // Lower Q for gradual fade
    }
}

void NotchFilterBank::updateNotches(double targetFrequencies, double qFactor, double sweepTimeSec) {
    // Placeholder - would need interpolation logic
}

std::string NotchFilterBank::serialize() const {
    std::ostringstream oss;
    oss << "{";
    for (int i = 0; i < (int)notches_.size(); i++) {
        oss << "{\"index\": " << i << ", \"freq\": " << notches_[i]->getFrequency()
            << ", \"bw\": " << notches_[i]->getBandwidth() << ", \"q\": " 
            << notches_[i]->getQFactor() << "}";
        if (i < (int)notches_.size() - 1) oss << ",";
    }
    oss << "}";
    return oss.str();
}

bool NotchFilterBank::deserialize(const std::string& data) {
    try {
        // Simple JSON-like parsing (would use proper JSON library in production)
        notches_.clear();
        
        // Parse data - simplified for now
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// FIR Notch Filter Implementation
// ============================================================================

FIRNotchFilter::FIRNotchFilter(int sampleRate, double frequency, double bandwidth)
    : sampleRate_(sampleRate) {
    
    centerFreq_ = frequency;
    bandwidth_ = bandwidth;
    
    // Calculate FIR coefficients (simplified sinc-based approach)
    // Design notch filter using windowed sinc
    coefficients_.clear();
    
    int numTaps = std::min(64, sampleRate_ / 100);  // Adaptive tap count
    double omega = hzToRadians(frequency, sampleRate_);
    
    // Create windowed-sinc notch filter
    for (int n = 0; n < numTaps; n++) {
        double omega_n = 2.0 * M_PI * n / numTaps;
        
        // Zero response at notch frequency
        double h = 0.0;
        double omega_diff = std::abs(omega_n - omega);
        
        if (omega_diff < bandwidth / 100.0) {
            h = 0.0;  // Null in notch region
        } else {
            h = 1.0;  // Pass in passband
        }
        
        // Apply Hamming window and shift
        h = h * 0.54 + 0.46 * std::cos(M_PI * 2.0 * n / numTaps);
        coefficients_.push_back(h);
    }
    
    // Make coefficients antisymmetric for linear phase
    for (size_t n = 0; n < coefficients_.size(); n++) {
        if (n != numTaps / 2) {
            coefficients_[n] = -coefficients_[n];
        }
    }
    // Center tap
    if (numTaps % 2 == 0) {
        coefficients_[numTaps / 2] = 0.0;
    }
}

FIRNotchFilter::~FIRNotchFilter() {
    coefficients_.clear();
}

float FIRNotchFilter::processSample(float sample) {
    // Direct convolution (O(N) per sample)
    float output = 0.0;
    for (double coef : coefficients_) {
        output += sample * (float)coef;
    }
    return output;
}

void FIRNotchFilter::processBuffer(const float* input, float* output, int frames) {
    // Transposed FIR for efficiency
    for (int i = 0; i < frames; i++) {
        float acc = 0.0;
        for (int n = 0; n < (int)coefficients_.size(); n++) {
            int idx = i + n;
            if (idx < (int)input.size()) {
                acc += input[idx] * (float)coefficients_[n];
            }
        }
        output[i] = acc;
    }
}

// ============================================================================
// Low Pass Filter Implementation
// ============================================================================

LowPassFilter::LowPassFilter(int sampleRate, double cutoffFrequency, double slope)
    : BaseFilter(sampleRate, cutoffFrequency, slope) {
    
    cutoffFrequency_ = cutoffFrequency;
    slope_ = slope;
    
    calculateLPCoefficients();
}

LowPassFilter::~LowPassFilter() {
    for (auto* lp : lowPassSections_) {
        delete lp;
    }
    lowPassSections_.clear();
}

void LowPassFilter::setCutoffFrequency(double newFrequency) {
    if (std::abs(newFrequency - cutoffFrequency_) < 0.1) return;
    cutoffFrequency_ = newFrequency;
    calculateLPCoefficients();
    coefficientsChanged_ = true;
}

void LowPassFilter::calculateLPCoefficients() {
    double omega = hzToRadians(cutoffFrequency_, sampleRate_);
    
    // Bilinear transform: s = (1-z^-1)/(1+z^-1)
    double alpha = (1.0 - std::cos(omega)) / (1.0 + std::cos(omega));
    
    // Create multiple sections based on slope
    double baseSlope = 12.0;  // dB/octave default
    int numSections = (int)std::ceil(slope_ / baseSlope);
    double sectionSlope = slope_ / numSections;
    
    lowPassSections_.clear();
    
    // Create sections with Butterworth approximation
    double omega_c = omega;
    
    for (int i = 0; i < numSections; i++) {
        double sectionAlpha = (1.0 - std::cos(omega_c)) / (1.0 + std::cos(omega_c));
        double sectionSlopeAlpha = std::pow(sectionSlope / 20.0, 1.0 / std::log(2.0));
        sectionAlpha = std::min(sectionAlpha * (1.0 + 0.1 * (i + 1)), 0.99);
        
        double cos_omega_c = std::cos(omega_c);
        
        double* lpCoeffs = new double[6];
        lpCoeffs[0] = 1.0;
        lpCoeffs[1] = -(1.0 + sectionAlpha) * cos_omega_c;
        lpCoeffs[2] = 1.0;
        lpCoeffs[3] = -(1.0 - sectionAlpha) * cos_omega_c;
        lpCoeffs[4] = (1.0 - sectionAlpha) * (1.0 - sectionAlpha);
        lpCoeffs[5] = 0.0;
        
        lowPassSections_.push_back(lpCoeffs);
    }
}

// ============================================================================
// High Pass Filter Implementation
// ============================================================================

HighPassFilter::HighPassFilter(int sampleRate, double cutoffFrequency, double slope)
    : BaseFilter(sampleRate, cutoffFrequency, slope) {
    
    cutoffFrequency_ = cutoffFrequency;
    slope_ = slope;
    
    calculateHPCoefficients();
}

HighPassFilter::~HighPassFilter() {
    for (auto* lp : highPassSections_) {
        delete lp;
    }
    for (auto* lp : combinedLowPassSections_) {
        delete lp;
    }
    highPassSections_.clear();
    combinedLowPassSections_.clear();
}

void HighPassFilter::setCutoffFrequency(double newFrequency) {
    if (std::abs(newFrequency - cutoffFrequency_) < 0.1) return;
    cutoffFrequency_ = newFrequency;
    calculateHPCoefficients();
    coefficientsChanged_ = true;
}

void HighPassFilter::calculateHPCoefficients() {
    double omega = hzToRadians(cutoffFrequency_, sampleRate_);
    
    // All-pass filter cascade approach for high-pass
    double alpha = std::cos(omega);
    
    int numSections = (int)std::ceil(slope_ / 12.0);
    
    highPassSections_.clear();
    
    // Cascade of all-pass filters to create high-pass
    for (int i = 0; i < numSections; i++) {
        double* hpCoeffs = new double[6];
        
        // All-pass section
        hpCoeffs[0] = alpha;
        hpCoeffs[1] = 1.0 + alpha;
        hpCoeffs[2] = 1.0;
        hpCoeffs[3] = -2.0 * alpha;
        hpCoeffs[4] = -1.0;
        hpCoeffs[5] = 0.0;
        
        highPassSections_.push_back(hpCoeffs);
    }
}

void HighPassFilter::applyLowPassFilter() {
    for (int i = 0; i < (int)highPassSections_.size(); i++) {
        double* lpCoeffs = new double[6];
        for (int j = 0; j < 6; j++) {
            lpCoeffs[j] = highPassSections_[i][j];
        }
        combinedLowPassSections_.push_back(lpCoeffs);
    }
}

// ============================================================================
// Band Pass Filter Implementation
// ============================================================================

BandPassFilter::BandPassFilter(int sampleRate, double centerFrequency, double bandwidth, double slope)
    : BaseFilter(sampleRate, centerFrequency, slope) {
    
    centerFrequency_ = centerFrequency;
    bandwidth_ = bandwidth;
    slope_ = slope;
    
    calculateBPCoefficients();
}

BandPassFilter::~BandPassFilter() {
    for (auto* hp : bandPassLowPass_) {
        delete hp;
    }
    for (auto* hp : bandPassHighPass_) {
        delete hp;
    }
    bandPassLowPass_.clear();
    bandPassHighPass_.clear();
}

void BandPassFilter::setCenterFrequency(double newFrequency) {
    if (std::abs(newFrequency - centerFrequency_) < 0.1) return;
    centerFrequency_ = newFrequency;
    calculateBPCoefficients();
    coefficientsChanged_ = true;
}

void BandPassFilter::setBandwidth(double newBandwidth) {
    if (std::abs(newBandwidth - bandwidth_) < 0.1) return;
    bandwidth_ = newBandwidth;
    calculateBPCoefficients();
    coefficientsChanged_ = true;
}

void BandPassFilter::calculateBPCoefficients() {
    double lowFreq = centerFrequency_ - bandwidth_ / 2.0;
    double highFreq = centerFrequency_ + bandwidth_ / 2.0;
    
    // Create low-pass at high frequency
    bandPassLowPass_.push_back(new tinnotus::dsp::LowPassFilter(
        sampleRate_, highFreq, slope_));
    
    // Create high-pass at low frequency
    bandPassHighPass_.push_back(new tinnotus::dsp::HighPassFilter(
        sampleRate_, lowFreq, slope_));
    
    // Apply low-pass after high-pass cascade
    applyLowPassFilters();
}

void BandPassFilter::applyLowPassFilters() {
    // Apply low-pass filter to high-pass output
    tinnotus::dsp::LowPassFilter lp(sampleRate_, 16000.0, 12.0);  // Anti-aliasing
    combinedLowPassSections_.clear();
}

// ============================================================================
// All Pass Filter Implementation
// ============================================================================

AllPassFilter::AllPassFilter(int sampleRate, double delayTime, double gain)
    : BaseFilter(sampleRate, 0.0, 0.0), gain_(gain) {
    
    delayTime_ = delayTime;
    
    // Calculate delay in samples
    int delaySamples = (int)(delayTime / 1000.0 * sampleRate_);
    delaySamples = std::max(delaySamples, 1);
    
    int bufferSize = delaySamples + 10;
    buffer_.resize(bufferSize, 0.0f);
    writeIndex_ = bufferSize;  // Pre-fill buffer
}

AllPassFilter::~AllPassFilter() {
    buffer_.clear();
}

void AllPassFilter::setDelayTime(double newDelayTime) {
    double oldDelay = delayTime_;
    delayTime_ = newDelayTime;
    
    // Recalculate buffer size if needed
    int delaySamples = (int)(delayTime_ / 1000.0 * sampleRate_);
    delaySamples = std::max(delaySamples, 1);
    
    if ((int)buffer_.size() < delaySamples + 5) {
        int newSize = delaySamples + 10;
        buffer_.resize(newSize, 0.0f);
    }
    
    coefficientsChanged_ = true;
    onCoefficientChanged();
}

void AllPassFilter::setGain(double newGain) {
    gain_ = newGain;
    coefficientsChanged_ = true;
    onCoefficientChanged();
}

float AllPassFilter::processSample(float sample) {
    if (coefficientsChanged_) {
        // Recalculate buffer indices
        int delaySamples = (int)(delayTime_ / 1000.0 * sampleRate_);
        delaySamples = std::max(delaySamples, 1);
        buffer_.resize(delaySamples + 10, 0.0f);
        writeIndex_ = buffer_.size();
        coefficientsChanged_ = false;
    }
    
    // Read delayed value
    int readIdx = (writeIndex_ - 1) % buffer_.size();
    float delayedValue = buffer_[readIdx];
    
    // Update buffer
    buffer_[writeIndex_ % buffer_.size()] = delayedValue;
    
    // Calculate new delayed value
    float newDelayed = gain_ * sample;
    buffer_[writeIndex_ % buffer_.size()] = sample;
    
    return delayedValue;
}

// ============================================================================
// Helper Functions (Inlined)
// ============================================================================

namespace math {

double hzToRadians(double frequency, int sampleRate) {
    return 2.0 * M_PI * frequency / sampleRate;
}

double radiansToHz(double radians, int sampleRate) {
    return radians * sampleRate / (2.0 * M_PI);
}

double frequencyToOctaves(double frequency) {
    if (frequency <= 0.0) return -1.0;
    return std::log2(frequency);
}

double calculateNotchCoefficients(double centerFreq, double bandwidth, int sampleRate) {
    double omega = hzToRadians(centerFreq, sampleRate);
    double Q = bandwidth / (2.0 * std::log(2.0) / bandwidth * sampleRate);
    
    double alpha = std::sin(omega * bandwidth / (2.0 * M_PI * sampleRate));
    double cos_omega = std::cos(omega);
    
    // Return SOS format coefficients
    std::vector<double> sos[6];
    sos[0] = {1.0};
    sos[1] = {-2.0 * cos_omega};
    sos[2] = {1.0};
    sos[3] = {-2.0 * alpha * cos_omega};
    sos[4] = {alpha * alpha};
    sos[5] = {0.0};
    
    return 0.0;  // Simplified for return value
}

} // namespace math

} // namespace dsp
} // namespace tinnotus