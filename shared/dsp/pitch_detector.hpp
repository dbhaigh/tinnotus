/**
 * @file pitch_detector.h
 * @brief Pitch detection algorithms for tinnitus frequency identification
 * 
 * This header contains implementations for detecting the frequency of tinnitus
 * signals from audio input. Supports multiple detection methods:
 * 
 * - YIN algorithm (default): Efficient, accurate autocorrelation-based method
 * - Goertzel algorithm: Optimized for detecting specific frequency bins
 * - FFT-based analysis: Spectral peak detection
 * 
 * @version 0.1.0
 */

#ifndef PITCH_DETECTOR_H
#define PITCH_DETECTOR_H

#include "tinnotus.hpp"
#include <cmath>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>

namespace tinnotus {
namespace dsp {

// ============================================================================
// Pitch Detection Types and Parameters
// ============================================================================

/**
 * @brief Pitch detection algorithm type
 */
enum class PitchDetectionAlgorithm {
    YIN,           // YIN algorithm (default, recommended)
    Goertzel,      // Goertzel (optimzed for specific frequencies)
    FFT_SPECTRAL,  // FFT + spectral peak detection
    Auto_YIN,      // YIN with auto-parameter tuning
};

/**
 * @brief YIN algorithm parameters
 */
struct YINParams {
    int sampleRate;
    double minFrequency = 100.0;
    double maxFrequency = 8000.0;
    int blockSize = 2048;  // Block size for analysis
    int minPitchSamples = 10;  // Minimum samples for reliable detection
    double threshold = 0.03;  // Autocorrelation threshold
    std::vector<double> harmonicWeights;  // For detecting fundamental vs harmonics
};

/**
 * @brief Goertzel algorithm parameters
 */
struct GoertzelParams {
    int sampleRate;
    std::vector<double> frequencies;  // Frequencies to check (Hz)
    int blockSize = 2048;
    int numBins = 512;  // FFT size
    double threshold = 0.1;  // Amplitude threshold
    double snrThreshold = 6.0;  // Signal-to-noise ratio (dB)
};

/**
 * @brief FFT spectral analysis parameters
 */
struct FFTParams {
    int sampleRate;
    int fftSize = 1024;
    double windowType = 0;  // 0 = none, 1 = Hann, 2 = Blackman-Harris
    double frequencyThreshold = 0.05;  // Magnitude threshold
    double binWidth = 0;  // Filtered bin width for peak detection
    int peakSearchWindow = 2;  // Bin search window size
};

// ============================================================================
// Base Pitch Detector
// ============================================================================

/**
 * @brief Base class for pitch detection
 */
class PitchDetector {
public:
    virtual ~PitchDetector();
    
    /**
     * @brief Set sample rate
     */
    virtual void setSampleRate(int sampleRate);
    
    /**
     * @brief Detect pitch from audio buffer
     * @param audioBuffer Input audio samples (interleaved, numSamples = frames * channels)
     * @return Detected pitch in Hz, or 0.0 if no reliable detection
     */
    virtual double detectPitch(const float* audioBuffer, int numSamples);
    
    /**
     * @brief Get detection confidence (0.0 - 1.0)
     */
    virtual double getConfidence() const = 0;
    
    /**
     * @brief Get current minimum frequency for detection
     */
    virtual double getMinFrequency() const = 0;
    
    /**
     * @brief Set minimum frequency for detection
     */
    virtual void setMinFrequency(double freq) = 0;
    
    /**
     * @brief Get current maximum frequency for detection
     */
    virtual double getMaxFrequency() const = 0;
    
    /**
     * @brief Set maximum frequency for detection
     */
    virtual void setMaxFrequency(double freq) = 0;
    
    /**
     * @brief Get current confidence (cached detection)
     */
    double getDetectionConfidence() const { return detectionConfidence_; }
    
    /**
     * @brief Get last detected frequency
     */
    double getLastFrequency() const { return lastFrequency_; }
    
    /**
     * @brief Set detection callback
     */
    void onDetectionChanged(std::function<void(double, double)> callback) {
        detectionCallback_ = std::move(callback);
    }
    
protected:
    PitchDetector(int sampleRate) : sampleRate_(sampleRate), lastFrequency_(0.0), 
                                    detectionConfidence_(0.0) {}
    
    int sampleRate_;
    double lastFrequency_;
    double detectionConfidence_;
    std::function<void(double, double)> detectionCallback_;
};

// ============================================================================
// YIN Pitch Detector
// ============================================================================

/**
 * @brief YIN (Yet Another Incremental) pitch detection algorithm
 * 
 * An optimized autocorrelation-based pitch detection method that:
 * - Uses difference function to cancel harmonic content
 * - Employs adaptive threshold for robustness
 * - Operates efficiently on streaming audio
 * 
 * Advantages:
 * - Fast (O(n) per block)
 * - Accurate for monophonic signals
 * - Handles amplitude variations well
 * - Resistant to noise with proper thresholding
 * 
 * Disadvantages:
 * - Struggles with polyphonic signals
 * - May fail on very noisy or low-quality audio
 */
class YINPitchDetector : public PitchDetector {
public:
    YINPitchDetector(int sampleRate = 44100, 
                     double minFreq = 100.0,
                     double maxFreq = 8000.0);
    
    ~YINPitchDetector() override;
    
    /**
     * @brief Set algorithm parameters
     */
    void setParameters(const YINParams& params);
    
    /**
     * @brief Detect pitch from audio buffer
     * @param audioBuffer Input audio samples
     * @param numSamples Number of samples
     * @return Detected pitch in Hz
     */
    double detectPitch(const float* audioBuffer, int numSamples) override;
    
    /**
     * @brief Get detection confidence (0.0 = unknown, 0.0-0.3 = unreliable, >0.7 = reliable)
     */
    double getConfidence() const override { return detectionConfidence_; }
    
    /**
     * @brief Get raw autocorrelation values for debug
     */
    const std::vector<double>& getAutoCorrelations() const { return autoCorrelations_; }
    
    /**
     * @brief Get pitch confidence as string
     */
    std::string getConfidenceString() const;
    
    /**
     * @brief Check if current frame has valid pitch
     */
    bool isPitchPresent() const { return pitchPresent_; }
    
protected:
    /**
     * @brief Preprocess audio (DC removal, normalization)
     */
    std::vector<double> preprocess(const float* audio, int numSamples);
    
    /**
     * @brief Calculate autocorrelation for given offset
     */
    double autocorrelation(const std::vector<double>& signal, int offset);
    
    /**
     * @brief Calculate difference function (to cancel harmonics)
     */
    double difference(const std::vector<double>& signal1, const std::vector<double>& signal2);
    
    /**
     * @brief Detect peak in difference function
     */
    int detectPeak(const std::vector<double>& diff, int minOffset, int maxOffset);
    
    /**
     * @brief Validate pitch detection (smoothness check)
     */
    bool validatePitch(double currentPitch, double prevPitch, 
                       double stabilityThreshold);
    
    /**
     * @brief Apply vocal detection check
     */
    bool isLikelyVocal(double freq, double threshold);
    
private:
    YINParams params_;
    std::vector<double> autoCorrelations_;
    std::vector<double> differenceFunc_;
    std::vector<double> previousCorrelations_;
    std::vector<double> previousDifferenceFunc_;
    
    double pitchPresentThreshold_ = 0.01;
    double pitchStabilityThreshold_ = 50.0;  // Hz
    double pitchSmoothness_ = 2.0;  // Hz per frame
    
    float lastEnergy_ = 0.0;
    int samplesSinceDetection_ = 0;
    bool pitchPresent_ = false;
    
    mutable std::mutex mutex_;
};

// ============================================================================
// Goertzel Pitch Detector
// ============================================================================

/**
 * @brief Goertzel algorithm pitch detection
 * 
 * Uses the Goertzel algorithm to detect energy at specific frequency bins.
 * Optimized when you know approximate frequency ranges to check.
 * 
 * Use case: When user provides approximate tinnitus frequency range.
 * 
 * Advantages:
 * - Very fast for small frequency ranges
 * - Good SNR performance
 * - Simple implementation
 * 
 * Disadvantages:
 * - Requires knowing frequency range in advance
 * - Less accurate than YIN for unknown pitches
 * - Sensitive to tone bursts (not sustained tones)
 */
class GoertzelPitchDetector : public PitchDetector {
public:
    GoertzelPitchDetector(int sampleRate = 44100,
                         const std::vector<double>& frequencies = std::vector<double>());
    
    ~GoertzelPitchDetector() override;
    
    /**
     * @brief Set frequencies to check
     */
    void setFrequencies(const std::vector<double>& frequencies);
    
    /**
     * @brief Detect pitch from audio buffer
     */
    double detectPitch(const float* audioBuffer, int numSamples) override;
    
    /**
     * @brief Get best detected frequency from all checked frequencies
     */
    double getBestFrequency() const;
    
    /**
     * @brief Get confidence for specific frequency
     */
    double getFrequencyConfidence(double frequency, double threshold = 0.5);
    
private:
    GoertzelParams params_;
    std::vector<double> bestFrequencies_;
    std::vector<std::pair<double, double>> frequencyResults_;  // (freq, confidence)
    
    std::vector<std::complex<double>> buffer_;  // For Goertzel implementation
    
    double findPeakFrequency(std::vector<std::pair<double, double>>& results);
};

// ============================================================================
// FFT Spectral Pitch Detector
// ============================================================================

/**
 * @brief FFT-based spectral peak detection
 * 
 * Computes FFT of audio buffer and finds frequency peaks.
 * Most accurate method for sustained tones.
 * 
 * Advantages:
 * - Most accurate for pure tones
 * - Can detect multiple frequencies simultaneously
 * - Good for frequency sweep analysis
 * 
 * Disadvantages:
 * - Slower than YIN
 * - Requires full buffer (not streaming-friendly)
 * - Less robust to noise
 */
class FFTPitchDetector : public PitchDetector {
public:
    FFTPitchDetector(int sampleRate = 44100,
                    int fftSize = 1024,
                    double minFreq = 100.0,
                    double maxFreq = 8000.0);
    
    ~FFTPitchDetector() override;
    
    /**
     * @brief Compute FFT of audio buffer
     */
    std::vector<double> computeFFT(const float* audioBuffer, int numSamples);
    
    /**
     * @brief Detect pitch from audio buffer
     */
    double detectPitch(const float* audioBuffer, int numSamples) override;
    
    /**
     * @brief Get full spectrum
     */
    const std::vector<double>& getSpectrum() const { return spectrum_; }
    
    /**
     * @brief Get peak frequencies above threshold
     */
    std::vector<SignalPeak> getPeaks(double threshold = 0.1) const;
    
    /**
     * @brief Detect dominant frequency in time range
     */
    double detectDominantFrequency(const float* audioBuffer, 
                                  int startIndex, int endIndex);
    
private:
    FFTParams params_;
    std::vector<double> spectrum_;
    std::vector<SignalPeak> currentPeaks_;
    
    // FFT implementation (inlined for header)
    void computeFFTSimple(const float* input, float* real, float* imag, 
                         int size);
    
    SignalPeak findPeakInRange(std::vector<double>& spectrum, 
                             int startBin, int endBin, double threshold);
};

// ============================================================================
// Combined Pitch Detection (Multiple Methods)
// ============================================================================

/**
 * @brief Combined pitch detector using multiple algorithms
 * 
 * Runs multiple pitch detection methods and returns consensus result.
 * Most robust for difficult signals.
 */
class CombinedPitchDetector {
public:
    CombinedPitchDetector(int sampleRate = 44100,
                         const std::vector<PitchDetectionAlgorithm>& algorithms = {
                             PitchDetectionAlgorithm::YIN,
                             PitchDetectionAlgorithm::Goertzel
                         });
    
    ~CombinedPitchDetector();
    
    /**
     * @brief Set algorithms to use
     */
    void setAlgorithms(const std::vector<PitchDetectionAlgorithm>& algorithms);
    
    /**
     * @brief Detect pitch using all configured algorithms
     * @return Consensus pitch (mode of all detections)
     */
    double detectPitch(const float* audioBuffer, int numSamples);
    
    /**
     * @brief Get all individual detections
     */
    const std::vector<PitchResult>& getIndividualResults() const { 
        return individualResults_; 
    }
    
    /**
     * @brief Get combined detection confidence
     */
    double getCombinedConfidence() const;
    
    /**
     * @brief Get method with highest confidence
     */
    PitchDetectionAlgorithm getBestMethod() const;
    
private:
    struct DetResult {
        PitchDetectionAlgorithm method;
        double frequency;
        double confidence;
        double intensity;
    };
    
    std::vector<std::unique_ptr<PitchDetector>> detectors_;
    std::vector<DetResult> individualResults_;
    
    std::vector<PitchDetectionAlgorithm> configuredAlgorithms_;
    
    std::vector<PitchResult> finalResults_;
    
    std::vector<PitchResult> consensusResults_;
    
    double calculateConsensus(const std::vector<DetResult>& results);
};

// ============================================================================
// Tinnitus Detection (Full Analysis)
// ============================================================================

/**
 * @brief Comprehensive tinnitus detection and analysis
 * 
 * Combines pitch detection with spectral analysis to identify tinnitus
 * characteristics. Can auto-detect or use manual user input.
 */
class TinnitusDetector {
public:
    enum class DetectionMethod {
        AUTO,        // Auto-detect from audio
        MANUAL,      // User identifies frequency manually
        GUIDED_TEST  // Play test tones, user reports
    };
    
    TinnitusDetector(int sampleRate = 44100,
                    DetectionMethod method = DetectionMethod::GUIDED_TEST,
                    double minFreq = 100.0,
                    double maxFreq = 8000.0);
    
    /**
     * @brief Set detection method
     */
    void setMethod(DetectionMethod method);
    
    /**
     * @brief Analyze audio for tinnitus
     * @param audioBuffer Input audio
     * @return Detected tinnitus frequency (or 0 if not detected)
     */
    double analyze(const float* audioBuffer, int numSamples);
    
    /**
     * @brief Manual frequency detection
     * User is asked to report which frequencies they hear
     */
    double analyzeManual(const std::vector<double>& testFrequencies,
                        const std::function<bool(double)>& userInput);
    
    /**
     * @brief Guided test - play test tones and get user feedback
     */
    double analyzeGuided(int numTones = 25,
                        const std::function<bool(double)>& userInput,
                        double minFreq = 100.0,
                        double maxFreq = 8000.0);
    
    /**
     * @brief Get detection result
     */
    TinnitusResult getResult() const { return tinnitusResult_; }
    
    /**
     * @brief Set tinnitus frequency manually
     */
    void setFrequency(double frequency, double confidence = 0.8);
    
    /**
     * @brief Validate detection (is this likely tinnitus?)
     */
    bool validateDetection();
    
private:
    struct TinnitusResult {
        bool detected;
        double frequency;
        double confidence;
        double estimatedIntensity;
        double durationMs;
        std::vector<double> peakFrequencies;  // If bimodal
        std::string characteristics;  // "modulatory", "continuous", etc.
    };
    
    std::function<bool(double)> userInput_;
    int sampleRate_;
    DetectionMethod method_;
    YINPitchDetector* yinDetector_;
    GoertzelPitchDetector* goertzelDetector_;
    FFTPitchDetector* fftDetector_;
    
    TinnitusResult tinnitusResult_;
    bool analysisComplete_ = false;
    
    std::vector<double> generateTestTones(double minFreq, double maxFreq, 
                                         int numTones);
    
    double analyzeSpectralPeaks(const float* audioBuffer, int numSamples);
};

// ============================================================================
// FFT Implementation (Inline)
// ============================================================================

/**
 * @brief Simple FFT implementation (Cooley-Tukey, recursive)
 * 
 * For production, consider using FFTW or KISS FFT for performance.
 */
namespace fft {

class FFT {
public:
    FFT(int size = 1024, WindowType window = WindowType::Hann);
    
    void execute(float* input, float* realOutput, float* imagOutput);
    void executeInPlace(float* input, float* output);
    
    void setWindow(WindowType window);
    
private:
    int size_;
    std::vector<float> window_;
    void compute();
};

enum class WindowType {
    NONE,
    HANN,
    BLACKMAN_HARRIS,
    HAMMING,
    NONE,
    FLAT_TOP
};

} // namespace fft

// ============================================================================
// Helper Functions and Utilities
// ============================================================================

/**
 * @brief Convert frequency to approximate pitch (MIDI number)
 */
double frequencyToPitch(double frequency, double referenceFreq = 440.0);

/**
 * @brief Convert pitch to frequency (A4 = 440Hz by default)
 */
double pitchToFrequency(int pitch, double referenceFreq = 440.0);

/**
 * @brief Calculate frequency resolution for given buffer size
 */
double getFrequencyResolution(int bufferSize, int sampleRate);

/**
 * @brief Find dominant frequency in audio buffer
 */
double findDominantFrequency(const float* audioBuffer, 
                            int bufferSize,
                            double minFreq = 100.0,
                            double maxFreq = 8000.0,
                            double threshold = 0.1);

/**
 * @brief Apply spectrogram smoothing
 */
std::vector<std::vector<double>> computeSpectrogram(
    const float* audioBuffer,
    int bufferSize,
    int sampleRate,
    int fftSize = 1024,
    int numOctaves = 3);

} // namespace dsp
} // namespace tinnotus

#endif // PITCH_DETECTOR_H