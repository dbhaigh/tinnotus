/**
 * @file dsp_filter.h
 * @brief Digital signal processing filter implementations
 * 
 * This header contains implementations of various digital filters for audio processing:
 * - Notch filters for frequency-specific attenuation (tinnitus relief)
 * - Low-pass, high-pass, and band-pass filters
 * - All-pass filters for phase manipulation
 * 
 * @version 0.1.0
 */

#ifndef DSP_FILTER_H
#define DSP_FILTER_H

#include "tinnotus.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace tinnotus {
namespace dsp {

// Forward declaration
class BaseFilter;

/**
 * @brief Abstract base class for all digital filters
 * 
 * Provides common interface and coefficient management for all filter types.
 */
class BaseFilter {
public:
    /**
     * @brief Create filter with given parameters
     * @param sampleRate Target sample rate
     * @param frequency Center frequency of filter (Hz)
     * @param qFactor Quality factor (higher = narrower bandwidth)
     */
    BaseFilter(int sampleRate, double frequency, double qFactor = 10.0);
    
    /**
     * @brief Get current sample rate
     */
    int getSampleRate() const { return sampleRate_; }
    
    /**
     * @brief Set sample rate (may require reset)
     * @param newSampleRate New sample rate
     * @return true if change was applied, false if already at that rate
     */
    bool setSampleRate(int newSampleRate);
    
    /**
     * @brief Process single audio sample through filter
     * @param sample Input sample (normalized, -1.0 to 1.0)
     * @return Processed sample
     */
    float processSample(float sample);
    
    /**
     * @brief Process block of audio samples
     * @param input Input samples
     * @param output Output buffer
     * @param numFrames Number of frames to process
     */
    void processBuffer(const float* input, float* output, int numFrames);
    
    /**
     * @brief Get current center frequency
     */
    double getFrequency() const { return frequency_; }
    
    /**
     * @brief Set center frequency (Hz)
     */
    void setFrequency(double frequency);
    
    /**
     * @brief Get quality factor
     */
    double getQFactor() const { return qFactor_; }
    
    /**
     * @brief Set quality factor
     */
    void setQFactor(double qFactor);
    
    /**
     * @brief Serialize filter coefficients to string
     */
    std::string serializeCoefficients() const;
    
protected:
    /**
     * @brief Calculate filter coefficients from parameters
     */
    virtual void calculateCoefficients() = 0;
    
    /**
     * @brief Called when coefficients are changed
     */
    void onCoefficientChanged() const;
    
private:
    int sampleRate_;
    double frequency_;
    double qFactor_;
    double alpha_;
    double cosOmega_;
    
    // Direct form II transposed coefficients (common to all filters)
    double b0_, b1_, b2_;  // Numerator
    double a0_, a1_, a2_;  // Denominator
    
    // Filter history (state)
    double history_[2];
    
    mutable bool coefficientsChanged_ = false;
};

/**
 * @brief Notch filter (IIR) - attenuates specific frequency band
 * 
 * Uses second-order IIR notch filter optimized for tinnitus relief.
 * Default bandwidth ~10% of center frequency for perceptual coverage.
 * 
 * Transfer function: H(z) = (1 - 2*cos(ω)z^(-1) + z^(-2)) / (1 - 2*α*cos(ω)z^(-1) + α^2*z^(-2))
 */
class NotchFilter : public BaseFilter {
public:
    /**
     * @brief Create notch filter
     * @param sampleRate Target sample rate
     * @param frequency Frequency to notch out (Hz)
     * @param bandwidth Bandwidth in Hz (controls Q factor)
     */
    static NotchFilter* create(int sampleRate, double frequency, double bandwidth = 500.0);
    
    /**
     * @brief Create notch filter by Q factor
     * @param sampleRate Target sample rate
     * @param frequency Frequency to notch out (Hz)
     * @param qFactor Quality factor (typically 10-20 for tinnitus)
     */
    NotchFilter(int sampleRate, double frequency, double qFactor);
    
    ~NotchFilter();
    
    /**
     * @brief Set bandwidth (Hz)
     */
    void setBandwidth(double bandwidth);
    
    /**
     * @brief Set Q factor
     */
    void setQFactor(double qFactor);
    
    /**
     * @brief Get bandwidth (Hz)
     */
    double getBandwidth() const;
    
    /**
     * @brief Get notch attenuation at given frequency
     */
    double getAttenuation(double freq) const;
    
protected:
    double bandwidth_ = 500.0;
    
private:
    void calculateNotchCoefficients();
    void calculateNotchOptimizedCoefficients();
};

/**
 * @brief Low-pass filter (IIR) - attenuates frequencies above cutoff
 * 
 * Butterworth-style low-pass filter for removing high frequencies.
 * 
 * Transfer function: H(z) = (1 - α) / 2 + (1 + α - 2*cos(ω))z^(-1) / (1 - 2*cos(ω)z^(-1) - α)
 */
class LowPassFilter : public BaseFilter {
public:
    /**
     * @brief Create low-pass filter
     * @param sampleRate Target sample rate
     * @param cutoffFrequency Cutoff frequency (Hz)
     * @param slope Slope in dB (default 12 dB/octave)
     */
    LowPassFilter(int sampleRate, double cutoffFrequency, double slope = 12.0);
    
    ~LowPassFilter();
    
    /**
     * @brief Set cutoff frequency
     */
    void setCutoffFrequency(double frequency);
    
    /**
     * @brief Get cutoff frequency
     */
    double getCutoffFrequency() const { return cutoffFrequency_; }
    
protected:
    double cutoffFrequency_;
    double slope_;  // in dB
    
private:
    void calculateLPCoefficients();
};

/**
 * @brief High-pass filter (IIR) - attenuates frequencies below cutoff
 * 
 * Butterworth-style high-pass filter for removing low frequencies.
 */
class HighPassFilter : public BaseFilter {
public:
    /**
     * @brief Create high-pass filter
     * @param sampleRate Target sample rate
     * @param cutoffFrequency Cutoff frequency (Hz)
     * @param slope Slope in dB (default 12 dB/octave)
     */
    HighPassFilter(int sampleRate, double cutoffFrequency, double slope = 12.0);
    
    ~HighPassFilter();
    
    /**
     * @brief Set cutoff frequency
     */
    void setCutoffFrequency(double frequency);
    
    /**
     * @brief Get cutoff frequency
     */
    double getCutoffFrequency() const { return cutoffFrequency_; }
    
protected:
    double cutoffFrequency_;
    double slope_;  // in dB
    
private:
    void calculateHPCoefficients();
};

/**
 * @brief Band-pass filter (IIR) - passes frequencies in range
 * 
 * Creates band-pass response by combining low and high pass characteristics.
 * Useful for isolating specific frequency ranges.
 */
class BandPassFilter : public BaseFilter {
public:
    /**
     * @brief Create band-pass filter
     * @param sampleRate Target sample rate
     * @param centerFrequency Center frequency (Hz)
     * @param bandwidth Bandwidth in Hz
     * @param slope Slope in dB (default 12 dB/octave)
     */
    BandPassFilter(int sampleRate, double centerFrequency, double bandwidth, 
                   double slope = 12.0);
    
    ~BandPassFilter();
    
    /**
     * @brief Set center frequency
     */
    void setCenterFrequency(double frequency);
    
    /**
     * @brief Get center frequency
     */
    double getCenterFrequency() const { return centerFrequency_; }
    
    /**
     * @brief Set bandwidth
     */
    void setBandwidth(double bandwidth);
    
    /**
     * @brief Get bandwidth
     */
    double getBandwidth() const { return bandwidth_; }
    
protected:
    double centerFrequency_;
    double bandwidth_;
    double slope_;
    
private:
    void calculateBPCoefficients();
};

/**
 * @brief All-pass filter (IIR) - preserves amplitude, modifies phase
 * 
 * Used for phase correction, delay lines, and special effects.
 * 
 * Transfer function: H(z) = (α + z^(-1)) / (α + 2*cos(ω)z^(-1) + z^(-2))
 */
class AllPassFilter : public BaseFilter {
public:
    /**
     * @brief Create all-pass filter
     * @param sampleRate Target sample rate
     * @param delayTime Delay in milliseconds
     * @param gain Output gain (0.0 to 1.0)
     */
    AllPassFilter(int sampleRate, double delayTime, double gain = 1.0);
    
    ~AllPassFilter();
    
    /**
     * @brief Set delay time (ms)
     */
    void setDelayTime(double delayTime);
    
    /**
     * @brief Get delay time (ms)
     */
    double getDelayTime() const { return delayTime_; }
    
    /**
     * @brief Set output gain
     */
    void setGain(double gain);
    
    /**
     * @brief Get output gain
     */
    double getGain() const { return gain_; }
    
protected:
    double delayTime_;
    double gain_;
    std::vector<float> buffer_;
    int readIndex_ = 0;
    int writeIndex_ = 0;
    
private:
    void calculateCoefficients();
};

// ============================================================================
// Filter Bank for Multiple Notches
// ============================================================================

/**
 * @brief Notch filter bank for managing multiple simultaneous notches
 * 
 * Handles multiple notch filters, adds/removes them, and manages transitions.
 */
class NotchFilterBank {
public:
    /**
     * @brief Create filter bank
     * @param sampleRate Target sample rate
     * @param maxNotches Maximum number of notches (default 8)
     */
    NotchFilterBank(int sampleRate, int maxNotches = 8);
    
    ~NotchFilterBank();
    
    /**
     * @brief Add a notch filter
     * @param frequency Frequency to notch out (Hz)
     * @param qFactor Quality factor for this notch
     * @return Index of added filter, or -1 if bank is full/invalid frequency
     */
    int addNotch(double frequency, double qFactor = 10.0);
    
    /**
     * @brief Remove notch at given index
     * @param index Index of notch to remove
     * @return true if removal was successful
     */
    bool removeNotch(int index);
    
    /**
     * @brief Remove lowest frequency notch
     */
    void removeLowestNotch();
    
    /**
     * @brief Remove highest frequency notch
     */
    void removeHighestNotch();
    
    /**
     * @brief Replace all notches with new set
     */
    void replaceAll(const std::vector<double>& frequencies, double qFactor = 10.0);
    
    /**
     * @brief Check if notch at given frequency exists
     */
    bool hasNotch(double frequency, double tolerance = 5.0) const;
    
    /**
     * @brief Get notch at given index
     */
    NotchFilter* getNotch(int index) const;
    
    /**
     * @brief Get number of active notches
     */
    int getCount() const { return notches_.size(); }
    
    /**
     * @brief Process audio through all active notches
     */
    float process(float sample);
    void processBuffer(const float* input, float* output, int frames);
    
    /**
     * @brief Fade notch in (smoothly increase Q)
     */
    void fadeNotchIn(int index, double durationSec, double targetQ = 15.0);
    
    /**
     * @brief Fade notch out (smoothly decrease Q)
     */
    void fadeNotchOut(int index, double durationSec);
    
    /**
     * @brief Get frequency sweep schedule
     */
    void updateNotches(double targetFrequencies, double qFactor, double sweepTimeSec);
    
    /**
     * @brief Serialize filter bank state
     */
    std::string serialize() const;
    
    /**
     * @brief Deserialize from string
     */
    bool deserialize(const std::string& data);
    
private:
    std::vector<NotchFilter*> notches_;
    int sampleRate_;
    int maxNotches_ = 8;
};

// ============================================================================
// FIR Filter Implementations
// ============================================================================

/**
 * @brief FIR notch filter
 * 
 * Uses windowed-sinc approach to create notch response.
 * Zero-phase implementation (linear filtering).
 */
class FIRNotchFilter {
public:
    FIRNotchFilter(int sampleRate, double frequency, double bandwidth);
    ~FIRNotchFilter();
    
    float processSample(float sample);
    void processBuffer(const float* input, float* output, int frames);
    
    double getCenterFrequency() const { return centerFreq_; }
    double getBandwidth() const { return bandwidth_; }
    std::vector<double> getCoefficients() const { return coefficients_; }
    
private:
    int sampleRate_;
    double centerFreq_;
    double bandwidth_;
    std::vector<double> coefficients_;
    int tapLength_ = 64;
};

// ============================================================================
// Filter Factory
// ============================================================================

/**
 * @brief Factory class for creating filters
 */
class FilterFactory {
public:
    static std::unique_ptr<BaseFilter> create(
        const std::string& type,
        int sampleRate,
        double frequency,
        double bandwidth = 500.0,
        double qFactor = 10.0);
    
    static std::unique_ptr<NotchFilter> createNotch(
        int sampleRate, double frequency, double qFactor = 10.0);
    
    static std::unique_ptr<LowPassFilter> createLowPass(
        int sampleRate, double frequency, double slope = 12.0);
    
    static std::unique_ptr<HighPassFilter> createHighPass(
        int sampleRate, double frequency, double slope = 12.0);
    
    static std::unique_ptr<BandPassFilter> createBandPass(
        int sampleRate, double centerFrequency, double bandwidth, 
        double slope = 12.0);
    
    static std::unique_ptr<AllPassFilter> createAllPass(
        int sampleRate, double delayTime, double gain = 1.0);
    
    static std::unique_ptr<FIRNotchFilter> createFIRNotch(
        int sampleRate, double frequency, double bandwidth);
    
    static std::unique_ptr<NotchFilterBank> createNotchBank(
        int sampleRate, int maxNotches = 8);
    
private:
    FilterFactory() = delete;
    FilterFactory(const FilterFactory&) = delete;
    FilterFactory& operator=(const FilterFactory&) = delete;
};

// ============================================================================
// Pitch Detection Helper Functions
// ============================================================================

/**
 * @brief Calculate YIN algorithm parameters
 */
PitchDetectorParams calculateYinParameters(int sampleRate, double minFreq, double maxFreq);

/**
 * @brief Get frequency resolution for pitch detection
 */
double getFrequencyResolution(int sampleRate, int bufferSize);

/**
 * @brief Convert pitch (MIDI number) to frequency (Hz)
 */
double pitchToFrequency(int midiNumber);

/**
 * @brief Convert frequency (Hz) to pitch (MIDI number)
 */
int frequencyToPitch(double frequency);

} // namespace dsp
} // namespace tinnotus

#endif // DSP_FILTER_H