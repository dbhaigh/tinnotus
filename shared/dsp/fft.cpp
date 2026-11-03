/**
 * @file fft.h
 * @brief Fast Fourier Transform implementation for pitch detection
 * 
 * Implements FFT/IFFT algorithms for spectral analysis:
 * - Decimation-in-time FFT (Cooley-Tukey)
 * - Decimation-in-frequency FFT
 * - Simple FFT (for small buffers)
 * - Window functions (Hann, Hamming, Blackman-Harris)
 * 
 * @version 0.1.0
 */

#ifndef FFT_H
#define FFT_H

#include "tinnotus.hpp"
#include <cmath>
#include <complex>
#include <vector>
#include <memory>

namespace tinnotus {
namespace dsp {

// ============================================================================
// Window Functions
// ============================================================================

/**
 * @brief Hann window
 */
inline double hann(double n, double N) {
    return 0.5 * (1.0 - std::cos(2.0 * M_PI * n / N));
}

/**
 * @brief Hamming window
 */
inline double hamming(double n, double N) {
    return 0.54 - 0.46 * std::cos(2.0 * M_PI * n / N);
}

/**
 * @brief Blackman-Harris window
 */
inline double blackmanHarris(double n, double N) {
    double term1 = 0.35875 * std::cos(2.0 * M_PI * n / N);
    double term2 = 0.48860 * std::cos(4.0 * M_PI * n / N);
    double term3 = 0.14125 * std::cos(6.0 * M_PI * n / N);
    return 1.0 - term1 + term2 - term3;
}

/**
 * @brief Blackman window
 */
inline double blackman(double n, double N) {
    double term1 = 0.42 * std::cos(2.0 * M_PI * n / N);
    double term2 = 0.5 * std::cos(4.0 * M_PI * n / N);
    return 1.0 - term1 + term2;
}

/**
 * @brief Flattop window (for accurate magnitude measurement)
 */
inline double flattop(double n, double N) {
    double term1 = 0.375 * std::cos(2.0 * M_PI * n / N);
    double term2 = 0.423 * std::cos(4.0 * M_PI * n / N);
    double term3 = 0.142 * std::cos(6.0 * M_PI * n / N);
    double term4 = 0.051 * std::cos(8.0 * M_PI * n / N);
    return 1.0 - term1 + term2 - term3 + term4;
}

/**
 * @brief Kaiser window
 */
inline std::vector<double> kaiser(double N, double beta, int n = -1) {
    std::vector<double> window(N);
    double alpha = 0.64 * std::min(beta, 0.5) + 0.47 * (beta - 0.5);
    for (int i = 0; i < N; i++) {
        if (n == -1 || n == 0 || n == i + 1) {
            double arg = beta * std::sqrt(1.0 - 4.0 * ((n == 0 ? 0 : i - N/2) / N) * ((n == N - 1 ? 0 : (n + 1) - N/2) / N));
            window[i] = I0(arg) / I0(beta);
        } else {
            double arg = beta * std::sqrt(1.0 - 4.0 * ((i - N/2) / N) * ((i + 1) - N/2) / N);
            window[i] = I0(arg) / I0(beta);
        }
    }
    return window;
}

// I0 - Modified Bessel function of first kind (order 0)
inline double I0(double x) {
    // Use continued fraction approximation for x > 0
    if (x < 0) x = -x;
    
    if (x < 0.8) {
        // Power series for small x
        double x2 = x * x;
        double term = 1.0;
        double sum = 1.0;
        for (int n = 0; n < 50; n++) {
            term *= x2 / ((2 * n) * (2 * n + 2));
            sum += term;
            if (std::abs(term) < 1e-15) break;
        }
        return sum;
    } else {
        // Continued fraction for large x
        double p = 0.0, d = 1.0, d1 = 0.0;
        int m = 50;
        for (int n = 0; n <= m; n++) {
            double b = 2.0 * (n + 1);
            double d2 = d1 * (b - x) + 2.0;
            double p1 = d2 * p + b;
            d = d2 / p1;
            d1 = 1.0 / d;
            if (n > 20 && std::abs(d - 1.0) < 1e-15) break;
        }
        return 2.0 / d;
    }
}

// ============================================================================
// Simple FFT (for small buffers)
// ============================================================================

/**
 * @brief Simple DFT-based FFT (O(N^2))
 * 
 * Only used for very small buffers where overhead of FFT is not worth it.
 */
class SimpleFFT {
public:
    /**
     * @brief Create simple FFT instance
     * @param size Power of 2 buffer size
     */
    SimpleFFT(int size = 16);
    
    ~SimpleFFT() = default;
    
    /**
     * @brief Compute FFT
     * @param input Input signal
     * @param real Output real part of spectrum
     * @param imag Output imaginary part of spectrum
     */
    void compute(const float* input, float* real, float* imag);
    
    /**
     * @brief Get output size
     */
    int getSize() const { return size_; }
    
private:
    int size_;
    static const int maxBatchSize = 32;  // Max FFT size before switching to radix-2 FFT
};

// ============================================================================
// Radix-2 Decimation-in-Time FFT
// ============================================================================

/**
 * @brief Radix-2 Cooley-Tukey FFT (Decimation-in-Time)
 * 
 * Standard FFT implementation for buffer sizes that are powers of 2.
 * Efficient O(N log N) algorithm using butterfly operations.
 */
class FFT {
public:
    /**
     * @brief Create FFT instance
     * @param size Power of 2 buffer size (e.g., 16, 32, 64, 128, 256, ...)
     */
    FFT(int size = 256);
    
    ~FFT() = default;
    
    /**
     * @brief Compute FFT of input signal
     * 
     * @param input Input real-valued signal
     * @param output Output complex-valued spectrum
     */
    void compute(const float* input, std::vector<std::complex<double>>& output) const;
    
    /**
     * @brief Compute FFT of complex input
     * 
     * @param input Complex input signal
     * @param output Output complex spectrum
     */
    void compute(const std::complex<float>&* input, std::vector<std::complex<double>>& output) const;
    
    /**
     * @brief Compute FFT and pack output as complex32
     */
    void compute(const float* input, std::vector<std::complex<float>>& output) const;
    
    /**
     * @brief Get twiddle factor at given index
     */
    static std::complex<double> twiddle(int index) {
        return std::exp(std::complex<double>(0, -2.0 * M_PI * index / size_));
    }
    
    /**
     * @brief Get FFT size
     */
    int getSize() const { return size_; }
    
    /**
     * @brief Get output size (N/2 + 1 for real input)
     */
    int getOutputSize() const { return size_ / 2 + 1; }
    
    /**
     * @brief Get frequency resolution (Hz)
     */
    double getFrequencyResolution() const {
        return 44100.0 / size_;  // Default sample rate, user should override
    }
    
    /**
     * @brief Set sample rate for frequency calculations
     */
    void setSampleRate(double sampleRate) {
        freqResolution_ = sampleRate / size_;
    }
    
private:
    int size_;
    double freqResolution_ = 44100.0 / 256.0;
    int logSize_ = 0;
    int* bitReversal_ = nullptr;
    
    void initBitReversal();
    void fftRecursive(const std::complex<double>* input, std::complex<double>* output, int lo, int hi);
};

// ============================================================================
// Split RADIX-2 FFT (Optimized for non-power-of-2 sizes)
// ============================================================================

/**
 * @brief Split Cooley-Tukey FFT
 * 
 * Optimized for buffer sizes that are products of small primes (e.g., 24 = 3 * 8).
 * Uses Cooley-Tukey algorithm with multiple stages.
 */
class SplitFFT {
public:
    /**
     * @brief Create SplitFFT instance
     * @param size Buffer size (should be product of small primes)
     */
    SplitFFT(int size = 128);
    
    ~SplitFFT() = default;
    
    /**
     * @brief Compute FFT
     */
    void compute(const float* input, std::vector<std::complex<double>>& output) const;
    
    /**
     * @brief Compute IFFT
     */
    void inverseCompute(std::vector<std::complex<double>>& input, float* output) const;
    
    /**
     * @brief Get FFT size
     */
    int getSize() const { return size_; }
    
    /**
     * @brief Set sample rate
     */
    void setSampleRate(double sampleRate) {
        freqResolution_ = sampleRate;
    }
    
private:
    int size_;
    double freqResolution_;
    std::vector<std::complex<double>> twiddles_;
    int stage0_;
    int stage1_;
    
    // Twiddle factors for each stage
    std::complex<double> w0, w1, w2, w3;
    
    void initTwiddles();
};

// ============================================================================
// Real-Input FFT (Optimized for real-valued audio)
// ============================================================================

/**
 * @brief Optimized FFT for real-valued input
 * 
 * Computes FFT of real input by packing two real signals into one complex
 * FFT, then unpacking the result. Reduces computational cost by ~30%.
 */
class RealFFT {
public:
    /**
     * @brief Create RealFFT instance
     * @param size Power of 2 input size (must be even, max 16384 recommended)
     */
    RealFFT(int size = 4096);
    
    ~RealFFT() = default;
    
    /**
     * @brief Compute FFT for real input
     * 
     * @param input Real-valued input signal
     * @param output Output complex spectrum (only half spectrum needed)
     * @param conjugate If true, input is already conjugate-symmetric
     */
    void compute(const float* input, std::vector<std::complex<double>>& output, bool conjugate = false) const;
    
    /**
     * @brief Compute inverse FFT
     * 
     * @param input Complex input (half spectrum only)
     * @param output Real-valued output signal
     */
    void inverseCompute(const std::vector<std::complex<double>>& input, float* output) const;
    
    /**
     * @brief Get frequency resolution
     */
    double getFrequencyResolution() const { return freqResolution_; }
    
    /**
     * @brief Set sample rate
     */
    void setSampleRate(double sampleRate) {
        freqResolution_ = sampleRate;
    }
    
private:
    int size_;
    double freqResolution_;
    std::unique_ptr<FFT> fftComplex_;
    std::unique_ptr<FFT> fftReal_;
    
    void init();
};

// ============================================================================
// Peak Detection in Spectrum
// ============================================================================

/**
 * @brief Signal peak in frequency spectrum
 */
struct SignalPeak {
    double frequency;  // Frequency in Hz
    double magnitude;  // Magnitude (dB or linear)
    double confidence; // 0.0 - 1.0 (sharpness of peak)
    double frequencyResolution;  // Bin width in Hz
    
    SignalPeak() : frequency(0.0), magnitude(0.0), confidence(0.0), frequencyResolution(0.0) {}
    
    bool isValid() const {
        return frequency > 0.0 && magnitude > 0.0;
    }
};

/**
 * @brief Find peaks in frequency spectrum
 */
class SpectrumAnalyzer {
public:
    /**
     * @brief Analyze spectrum for peaks
     */
    static std::vector<SignalPeak> detectPeaks(
        const std::vector<double>& realPart,
        const std::vector<double>& imagPart,
        double sampleRate,
        double magnitudeThreshold = 0.1,
        int peakSearchWindow = 2,
        double minPeakDistance = 0.0);
    
    /**
     * @brief Find dominant frequency from spectrum
     */
    static double findDominantFrequency(
        const std::vector<double>& realPart,
        const std::vector<double>& imagPart,
        double sampleRate,
        double minFrequency = 100.0,
        double maxFrequency = 8000.0);
    
private:
    /**
     * @brief Calculate magnitude spectrum
     */
    static std::vector<double> calculateMagnitude(
        const std::vector<double>& realPart,
        const std::vector<double>& imagPart);
    
    /**
     * @brief Calculate magnitude squared
     */
    static std::vector<double> calculateMagnitudeSquared(
        const std::vector<double>& realPart,
        const std::vector<double>& imagPart);
    
    /**
     * @brief Find local maxima
     */
    static std::vector<SignalPeak> findLocalMaxima(
        const std::vector<double>& magnitude,
        double sampleRate,
        double magnitudeThreshold,
        int searchWindow,
        double minPeakDistance);
};

// ============================================================================
// FFT Factory
// ============================================================================

/**
 * @brief Factory for creating FFT instances
 */
class FFTFactory {
public:
    /**
     * @brief Create FFT instance
     * @param size Buffer size (power of 2, max 262144)
     */
    static std::unique_ptr<FFT> create(int size = 1024);
    
    /**
     * @brief Create SplitFFT instance
     * @param size Buffer size (product of small primes, max 262144)
     */
    static std::unique_ptr<SplitFFT> createSplit(int size = 128);
    
    /**
     * @brief Create RealFFT instance
     * @param size Input size (power of 2, even, max 16384)
     */
    static std::unique_ptr<RealFFT> createReal(int size = 4096);
    
    /**
     * @brief Select best FFT type for given buffer
     */
    static std::unique_ptr<FFTBase> select(int bufferSize);
    
private:
    struct FFTBase {
        std::unique_ptr<FFT> fftComplex;
        std::unique_ptr<SplitFFT> fftSplit;
        std::unique_ptr<RealFFT> fftReal;
    };
};

// ============================================================================
// Inlined Helper Functions
// ============================================================================

namespace math {

/**
 * @brief Compute complex exponential
 */
inline std::complex<double> complexExp(double angle) {
    return std::polar(1.0, angle);
}

/**
 * @brief Compute real part of complex exponential
 */
inline double realExp(double angle) {
    return std::cos(angle);
}

/**
 * @brief Compute imaginary part of complex exponential
 */
inline double imagExp(double angle) {
    return std::sin(angle);
}

} // namespace math

// ============================================================================
// FFT Implementation Classes
// ============================================================================

// SimpleFFT implementation
inline SimpleFFT::SimpleFFT(int size) : size_(size) {
    if (size_ < 1) size_ = 16;
    if (size_ > maxBatchSize) size_ = maxBatchSize;
}

inline void SimpleFFT::compute(const float* input, float* real, float* imag) {
    int N = size_;
    
    // Direct DFT
    for (int k = 0; k < N; k++) {
        real[k] = 0.0;
        imag[k] = 0.0;
        for (int n = 0; n < N; n++) {
            double angle = -2.0 * M_PI * n * k / N;
            real[k] += input[n] * std::cos(angle);
            imag[k] -= input[n] * std::sin(angle);
        }
    }
}

// SplitFFT implementation
inline SplitFFT::SplitFFT(int size) : size_(size), freqResolution_(44100.0 / size) {
    if (size_ < 16 || size_ % 2 != 0) {
        throw std::invalid_argument("SplitFFT: size must be >= 16 and even");
    }
    
    // Factorize into small primes (2, 3, 5, 7, ...)
    std::vector<int> factors;
    int tempSize = size_;
    while (tempSize > 1) {
        if (tempSize % 2 == 0) {
            factors.push_back(2);
            tempSize /= 2;
        } else if (tempSize % 3 == 0) {
            factors.push_back(3);
            tempSize /= 3;
        } else if (tempSize % 5 == 0) {
            factors.push_back(5);
            tempSize /= 5;
        } else if (tempSize % 7 == 0) {
            factors.push_back(7);
            tempSize /= 7;
        } else {
            break;  // Large prime factor - not optimal
        }
    }
    
    if (tempSize > 1) {
        factors.push_back(tempSize);  // Prime factor
    }
    
    // Stage 0: reverse order
    stage0_ = factors.size() - 1;
    stage1_ = factors.back();
    
    // Precompute twiddles for each stage
    initTwiddles();
}

inline void SplitFFT::initTwiddles() {
    twiddles_.clear();
    int maxTwiddles = stage0_ * (size_ / stage0_) + stage1_ * (size_ / stage1_);
    
    for (int n = 0; n < maxTwiddles; n++) {
        if (n < stage0_) {
            twiddles_.push_back(std::exp(std::complex<double>(0, -2.0 * M_PI * n)));
        } else {
            int n1 = n % stage0_;
            int n2 = (n - n1) / stage0_;
            double angle = -2.0 * M_PI * n / size_;
            twiddles_.push_back(std::polar(1.0, angle));
        }
    }
}

inline void SplitFFT::compute(const float* input, std::vector<std::complex<double>>& output) const {
    int N = size_;
    std::vector<std::complex<double>> buffer(N);
    
    // Stage 1: DFT over stage1_ samples, decimated by stage0_
    for (int k = 0; k < stage0_; k++) {
        for (int j = 0; j < stage1_; j++) {
            int idx = k * stage1_ + j;
            buffer[idx] = input[idx];
        }
    }
    
    // Recursive FFT over each group
    fftRecursive(buffer.data(), output.data(), 0, N);
}

inline void SplitFFT::fftRecursive(const std::complex<double>* input, 
                                   std::complex<double>* output, 
                                   int lo, int hi) {
    int n = hi - lo;
    
    if (n <= stage1_) {
        // Simple DFT
        for (int k = 0; k < n; k++) {
            output[lo + k] = input[lo + k];
        }
    } else {
        // Divide
        int half = n / stage1_;
        std::vector<std::complex<double>> upper(half * stage0_);
        
        for (int k = 0; k < n; k++) {
            int upperIdx = k / stage1_;
            int lowerIdx = k % stage1_;
            if (upperIdx < half) {
                upper[upperIdx * stage1_ + lowerIdx] = input[lo + k];
            }
        }
        
        // Conquer
        fftRecursive(upper.data(), output, lo, lo + half * stage0_);
        fftRecursive(upper.data(), &output[lo + half * stage0_], lo + half * stage0_, lo + n);
        
        // Combine using twiddles
        double W = -2.0 * M_PI / n;
        for (int k = 0; k < half; k++) {
            std::complex<double> w = std::exp(std::complex<double>(0, W * k));
            for (int j = 0; j < stage1_; j++) {
                int idx1 = lo + k * stage1_ + j;
                int idx2 = lo + half * stage0_ + j;
                
                // Combine with twiddle
                output[idx1] += w * output[idx2];
                output[idx2] = output[idx2];
            }
        }
    }
}

inline void SplitFFT::inverseCompute(std::vector<std::complex<double>>& input, float* output) const {
    // IFFT with conjugation and normalization
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = std::conj(input[i]) / (double)input.size();
    }
    
    compute(input.data(), std::vector<std::complex<double>>{});
}

// RealFFT implementation
inline RealFFT::RealFFT(int size) : size_(size), freqResolution_(44100.0 / size) {
    fftComplex_ = std::make_unique<FFT>(2 * size);
    fftReal_ = std::make_unique<FFT>(size);
}

inline void RealFFT::init() {
    // Initialize FFT objects
    fftComplex_->compute(nullptr, std::vector<std::complex<double>>{});
    fftReal_->compute(nullptr, std::vector<std::complex<double>>{});
}

inline void RealFFT::compute(const float* input, std::vector<std::complex<double>>& output, bool conjugate) const {
    int N = size_;
    std::vector<std::complex<double>> complexInput(2 * N);
    
    // Pack real input into complex array
    for (int n = 0; n < N; n++) {
        complexInput[2 * n] = std::complex<double>(input[n], 0.0);
        complexInput[2 * N - 1 - n] = std::complex<double>(0.0, conjugate ? input[n] : -input[n]);
    }
    
    // Complex FFT
    fftComplex_->compute(complexInput.data(), output.size() > 2 * N ? std::vector<std::complex<double>>(2 * N, 0.0) : output);
    
    // Unpack: take only first N+1 points and make conjugate symmetric
    output.clear();
    for (int k = 0; k < N / 2 + 1; k++) {
        output.push_back(complexInput[k]);
    }
    
    // Set imaginary part of bin 0 and N/2 to 0
    output[0].imag() = 0.0;
    if (N % 2 == 0) {
        output[N / 2].imag() = 0.0;
    }
}

inline void RealFFT::inverseCompute(const std::vector<std::complex<double>>& input, float* output) const {
    int N = size_;
    std::vector<std::complex<double>> complexOutput(2 * N);
    
    // Pack spectrum into complex array
    for (int k = 0; k < N / 2 + 1; k++) {
        complexOutput[2 * k] = input[k];
        complexOutput[2 * N - 1 - k] = std::conj(input[k]);
    }
    
    // Add zeros for rest
    for (int k = N / 2 + 1; k < N; k++) {
        complexOutput[2 * k] = std::complex<double>(0.0, 0.0);
        complexOutput[2 * N - 1 - k] = std::complex<double>(0.0, 0.0);
    }
    
    // Complex IFFT
    fftComplex_->inverseCompute(complexOutput);
    
    // Unpack: take real part
    for (int n = 0; n < N; n++) {
        output[n] = std::real(complexOutput[n]) * 2.0;
    }
}

// SpectrumAnalyzer implementation
inline std::vector<double> SpectrumAnalyzer::calculateMagnitude(
    const std::vector<double>& realPart,
    const std::vector<double>& imagPart) {
    std::vector<double> mag(realPart.size());
    
    for (size_t i = 0; i < realPart.size(); i++) {
        mag[i] = std::sqrt(realPart[i] * realPart[i] + imagPart[i] * imagPart[i]);
    }
    
    return mag;
}

inline std::vector<double> SpectrumAnalyzer::calculateMagnitudeSquared(
    const std::vector<double>& realPart,
    const std::vector<double>& imagPart) {
    std::vector<double> magSq(realPart.size());
    
    for (size_t i = 0; i < realPart.size(); i++) {
        magSq[i] = realPart[i] * realPart[i] + imagPart[i] * imagPart[i];
    }
    
    return magSq;
}

inline std::vector<SignalPeak> SpectrumAnalyzer::findLocalMaxima(
    const std::vector<double>& magnitude,
    double sampleRate,
    double magnitudeThreshold,
    int searchWindow,
    double minPeakDistance) {
    std::vector<SignalPeak> peaks;
    
    int N = magnitude.size();
    int maxBins = N / 2 + 1;
    
    // Skip DC and Nyquist (unless we need the whole spectrum)
    int startBin = (sampleRate > 8000.0) ? 3 : 0;  // Start from a few bins to avoid DC
    
    for (int bin = startBin; bin < maxBins; bin++) {
        // Check magnitude threshold
        if (magnitude[bin] < magnitudeThreshold) continue;
        
        // Check if this is a local maximum
        int start = std::max(0, bin - searchWindow);
        int end = std::min(maxBins - 1, bin + searchWindow);
        
        double maxMag = magnitude[bin];
        bool isPeak = true;
        
        for (int k = start; k < end; k++) {
            if (magnitude[k] >= maxMag) {
                isPeak = false;
            }
            maxMag = std::max(maxMag, magnitude[k]);
        }
        
        if (isPeak) {
            SignalPeak peak;
            peak.frequency = bin * sampleRate / maxBins;
            peak.magnitude = 20.0 * std::log10(maxMag + 1e-10);
            peak.frequencyResolution = sampleRate / maxBins;
            
            // Calculate confidence (peak sharpness)
            double totalMag = 0.0;
            for (int k = start; k <= end; k++) {
                totalMag += magnitude[k];
            }
            peak.confidence = (maxMag / totalMag) * 0.5 + 0.5;
            
            // Filter by minimum distance
            bool tooClose = false;
            for (const auto& existing : peaks) {
                if (std::abs(peak.frequency - existing.frequency) < minPeakDistance) {
                    tooClose = true;
                    break;
                }
            }
            
            if (!tooClose) {
                peaks.push_back(peak);
            }
        }
    }
    
    // Sort by magnitude (descending)
    std::sort(peaks.begin(), peaks.end(), [](const SignalPeak& a, const SignalPeak& b) {
        return a.magnitude > b.magnitude;
    });
    
    return peaks;
}

} // namespace tinnotus

#endif // FFT_H