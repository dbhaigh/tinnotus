/**
 * @file pitch_detector.cpp
 * @brief Pitch detection algorithms for tinnitus frequency identification
 * 
 * Implements multiple pitch detection methods:
 * - YIN: Efficient autocorrelation-based detection (default)
 * - Goertzel: Optimized for specific frequency bins
 * - FFT Spectral: Peak detection from frequency spectrum
 * 
 * @version 0.1.0
 */

#include "pitch_detector.hpp"
#include "fft.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <mutex>
#include <vector>

namespace tinnotus {
namespace dsp {

// ============================================================================
// YIN Pitch Detector Implementation
// ============================================================================

YINPitchDetector::YINPitchDetector(int sampleRate, double minFreq, double maxFreq)
    : PitchDetector(sampleRate),
      pitchPresentThreshold_(0.01),
      pitchStabilityThreshold_(50.0),
      pitchSmoothness_(2.0),
      samplesSinceDetection_(0),
      pitchPresent_(false) {
    
    params_.sampleRate = sampleRate;
    params_.minFrequency = minFreq;
    params_.maxFrequency = maxFreq;
    params_.blockSize = 2048;
    params_.minPitchSamples = 10;
    params_.threshold = 0.03;
    
    // Initialize buffers
    int bufferLen = params_.blockSize;
    autoCorrelations_.resize(bufferLen, 0.0);
    differenceFunc_.resize(bufferLen, 0.0);
    previousCorrelations_.resize(bufferLen, 0.0);
    previousDifferenceFunc_.resize(bufferLen, 0.0);
    
    // Auto-tune parameters
    autoTuneParameters();
}

YINPitchDetector::~YINPitchDetector() = default;

void YINPitchDetector::autoTuneParameters() {
    // Optimize parameters based on frequency range
    double freqRange = params_.maxFrequency - params_.minFrequency;
    params_.threshold = std::max(0.01, std::min(0.1, 0.01 + freqRange / 1000.0));
    
    // Shorter block for higher frequencies
    double optimalBlockSize = 44100.0 / params_.minFrequency;
    params_.blockSize = (int)std::max(512, std::min(optimalBlockSize, 4096.0));
}

void YINPitchDetector::setParameters(const YINParams& params) {
    params_ = params;
    autoTuneParameters();
}

double YINPitchDetector::preprocess(const float* audio, int numSamples) {
    std::vector<double> signal(numSamples);
    
    // Convert to double and remove DC offset
    double mean = 0.0;
    for (int i = 0; i < numSamples; i++) {
        signal[i] = static_cast<double>(audio[i]);
        mean += signal[i];
    }
    
    double dcOffset = mean / numSamples;
    for (int i = 0; i < numSamples; i++) {
        signal[i] -= dcOffset;
    }
    
    // Normalize
    double maxVal = 0.0;
    for (double val : signal) {
        maxVal = std::max(maxVal, std::abs(val));
    }
    
    double scale = maxVal > 0.0 ? 1.0 / maxVal : 1.0;
    for (int i = 0; i < numSamples; i++) {
        signal[i] *= scale;
    }
    
    return scale;
}

double YINPitchDetector::autocorrelation(const std::vector<double>& signal, int offset) {
    int len = signal.size();
    int end = len - offset;
    
    double sum = 0.0;
    for (int i = 0; i < end; i++) {
        sum += signal[i] * signal[i + offset];
    }
    
    double energy = 0.0;
    for (int i = 0; i < len; i++) {
        energy += signal[i] * signal[i];
    }
    
    return offset > 0 ? sum / energy : 1.0;
}

double YINPitchDetector::difference(const std::vector<double>& signal1, 
                                    const std::vector<double>& signal2) {
    double sum = 0.0;
    for (size_t i = 0; i < signal1.size(); i++) {
        sum += (signal1[i] - signal2[i]) * (signal1[i] - signal2[i]);
    }
    return sum;
}

int YINPitchDetector::detectPeak(const std::vector<double>& diff, 
                                 int minOffset, int maxOffset) {
    double minDiff = diff[minOffset];
    int bestOffset = minOffset;
    
    for (int offset = minOffset; offset <= maxOffset; offset++) {
        if (diff[offset] < minDiff) {
            minDiff = diff[offset];
            bestOffset = offset;
        }
    }
    
    return bestOffset;
}

double YINPitchDetector::detectPitch(const float* audioBuffer, int numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Need minimum samples for detection
    if (numSamples < params_.minPitchSamples * 2) {
        lastFrequency_ = 0.0;
        detectionConfidence_ = 0.0;
        return 0.0;
    }
    
    // Extract block for analysis
    int blockSize = params_.blockSize;
    std::vector<double> signal(numSamples);
    
    // Preprocess
    double scale = preprocess(audioBuffer, numSamples);
    
    // Copy audio to signal and ensure valid range
    int signalSize = std::min(numSamples, blockSize);
    for (int i = 0; i < signalSize; i++) {
        signal[i] = audioBuffer[i] * scale;
        if (audioBuffer[i] > 0.95) {  // Only process when signal is present
            pitchPresent_ = true;
        }
    }
    
    // YIN algorithm
    std::vector<double> acorr(blockSize + 1, 0.0);
    std::vector<double> diff(blockSize + 1, 0.0);
    
    // Calculate autocorrelation
    double minOffsetSamples = params_.minFrequency / params_.sampleRate_ * blockSize;
    double maxOffsetSamples = params_.maxFrequency / params_.sampleRate_ * blockSize;
    int minOffset = (int)std::max(2, minOffsetSamples);
    int maxOffset = (int)std::min(blockSize / 2, maxOffsetSamples);
    
    double minAc = 1.0;
    for (int offset = minOffset; offset <= maxOffset; offset++) {
        acorr[offset] = autocorrelation(signal, offset);
        if (acorr[offset] < minAc) {
            minAc = acorr[offset];
        }
    }
    
    // Calculate difference function (harmonic cancellation)
    for (int offset = minOffset; offset <= maxOffset; offset++) {
        double diffVal = std::abs(acorr[offset] - minAc);
        diff[offset] = diffVal;
        
        // Combine with previous frame
        if (offset < (int)previousCorrelations_.size()) {
            double weight = std::exp(-offset / pitchStabilityThreshold_);
            previousCorrelations_[offset] *= weight;
            diff[offset] = diff[offset] * (1.0 - weight) + previousCorrelations_[offset] * weight;
            previousCorrelations_[offset] = diff[offset];
        }
    }
    
    // Find peak in difference function
    int bestOffset = detectPeak(diff, minOffset, maxOffset);
    
    // Check threshold
    if (diff[bestOffset] < params_.threshold) {
        pitchPresent_ = false;
        detectionConfidence_ = 0.0;
        lastFrequency_ = 0.0;
    } else {
        // Calculate pitch
        double pitchSamples = bestOffset;
        double period = blockSize / pitchSamples;
        lastFrequency_ = params_.sampleRate_ / period;
        
        // Calculate confidence
        double acVal = diff[bestOffset];
        double threshold = params_.threshold * std::exp(pitchSamples / 10.0);
        detectionConfidence_ = std::min(1.0, acVal / (threshold + 0.001));
        
        pitchPresent_ = detectionConfidence_ > 0.3;
    }
    
    // Store for next frame
    for (int offset = minOffset; offset <= maxOffset; offset++) {
        previousDifferenceFunc_[offset] = diff[offset];
    }
    
    // Validate pitch (smoothness check)
    if (pitchPresent_ && numSamples > 2 * blockSize) {
        // Simple stability check
        if (lastFrequency_ > 0.0) {
            double prevFreq = pitchPresent_ ? lastFrequency_ : 0.0;
            double freqDiff = std::abs(lastFrequency_ - prevFreq);
            
            // Check for vocal range (avoid 200-300 Hz where voice is common)
            bool isVocal = (lastFrequency_ >= 200.0 && lastFrequency_ <= 300.0);
            if (isVocal) {
                detectionConfidence_ *= 0.5;  // Penalize vocal frequencies
            }
            
            // Check smoothness
            if (prevFreq > 0.0 && freqDiff > pitchSmoothness_) {
                // Reject unstable pitch
                pitchPresent_ = false;
                detectionConfidence_ = 0.0;
            }
        }
    }
    
    // Callback
    if (detectionCallback_) {
        detectionCallback_(lastFrequency_, detectionConfidence_);
    }
    
    return pitchPresent_ ? lastFrequency_ : 0.0;
}

// ============================================================================
// Goertzel Pitch Detector Implementation
// ============================================================================

GoertzelPitchDetector::GoertzelPitchDetector(int sampleRate, 
                                             const std::vector<double>& frequencies)
    : PitchDetector(sampleRate),
      bestFrequencies_(),
      frequencyResults_() {
    
    params_.sampleRate = sampleRate;
    params_.frequencies = frequencies;
    params_.blockSize = 2048;
    params_.numBins = frequencies.empty() ? 512 : (int)frequencies.size();
    params_.threshold = 0.1;
    params_.snrThreshold = 6.0;
}

GoertzelPitchDetector::~GoertzelPitchDetector() = default;

void GoertzelPitchDetector::setFrequencies(const std::vector<double>& frequencies) {
    params_.frequencies = frequencies;
    params_.numBins = (int)frequencies.size();
    bestFrequencies_.clear();
    frequencyResults_.clear();
    
    // Pre-calculate bin indices
    int fftSize = params_.blockSize;
    for (double freq : params_.frequencies) {
        int bin = static_cast<int>(freq * fftSize / params_.sampleRate_);
        bin = std::max(0, std::min(bin - 1, fftSize / 2 - 1));
        bestFrequencies_.push_back(bin);
    }
    
    bestFrequencies_.sort();
}

double GoertzelPitchDetector::detectPitch(const float* audioBuffer, int numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store audio for Goertzel analysis
    int blockSize = params_.blockSize;
    std::vector<float> buffer(blockSize);
    
    for (int i = 0; i < blockSize && i < numSamples; i++) {
        buffer[i] = audioBuffer[i];
    }
    
    // Apply window
    for (int i = 0; i < blockSize; i++) {
        buffer[i] *= 0.5 * (1.0 + std::cos(2.0 * M_PI * i / blockSize));
    }
    
    // Calculate Goertzel for each target frequency
    std::vector<std::pair<double, double>> results;
    
    for (int bin : bestFrequencies_) {
        if (bin < 0 || bin >= blockSize / 2) continue;
        
        double realPart = 0.0;
        double imagPart = 0.0;
        double state1 = 0.0;
        double state2 = 0.0;
        
        // Goertzel recurrence
        double k = bin;
        double omega = 2.0 * M_PI * k / blockSize;
        double coeff = -2.0 * std::cos(omega);
        
        for (int n = 0; n < blockSize; n++) {
            double Wn = std::exp(std::complex<double>(0, omega * n));
            
            // Simplified Goertzel
            state2 = coeff * state1 - state2 + buffer[n];
            realPart -= state1;
            state1 = state2;
            
            imagPart -= Wn.imag() * state1 + Wn.real() * state2;
            realPart += Wn.real() * state1 + Wn.imag() * state2;
        }
        
        // Calculate magnitude
        double magnitude = std::sqrt(realPart * realPart + imagPart * imagPart);
        double dB = 10.0 * std::log10(std::max(magnitude, 1e-10));
        
        results.push_back({bin * params_.sampleRate_ / blockSize, dB});
    }
    
    // Find best frequency
    double bestFreq = 0.0;
    double bestDB = -100.0;
    
    for (const auto& result : results) {
        double freq = result.first;
        double db = result.second;
        
        if (db > bestDB) {
            bestDB = db;
            bestFreq = freq;
        }
    }
    
    lastFrequency_ = bestFreq;
    
    // Check threshold
    if (bestDB < params_.threshold) {
        pitchPresent_ = false;
        detectionConfidence_ = 0.0;
        return 0.0;
    }
    
    // Calculate SNR
    double totalEnergy = 0.0;
    for (const auto& r : results) {
        double mag = std::sqrt(std::pow(r.second / 10.0, 2));
        totalEnergy += mag;
    }
    
    double snr = 10.0 * std::log10((bestDB / 10.0) / (totalEnergy / results.size() + 1e-10));
    detectionConfidence_ = std::min(1.0, (snr - params_.snrThreshold) / 10.0 + 0.5);
    detectionConfidence_ = std::max(0.0, std::min(1.0, detectionConfidence_));
    
    pitchPresent_ = detectionConfidence_ > 0.3;
    
    if (detectionCallback_) {
        detectionCallback_(lastFrequency_, detectionConfidence_);
    }
    
    return pitchPresent_ ? lastFrequency_ : 0.0;
}

double GoertzelPitchDetector::getBestFrequency() const {
    if (bestFrequencies_.empty()) return 0.0;
    return bestFrequencies_.back() * params_.sampleRate_ / params_.blockSize;
}

double GoertzelPitchDetector::getFrequencyConfidence(double frequency, double threshold) {
    // Find closest bin
    int targetBin = static_cast<int>(frequency * params_.blockSize / params_.sampleRate_);
    targetBin = std::max(0, std::min(targetBin, (int)bestFrequencies_.size() - 1));
    
    if (targetBin < 0 || targetBin >= (int)bestFrequencies_.size()) {
        return 0.0;
    }
    
    double db = -1000.0;
    for (const auto& result : frequencyResults_) {
        if (std::abs(result.first - frequency) < 100.0) {
            db = result.second;
            break;
        }
    }
    
    if (db < threshold) {
        return 0.0;
    }
    
    return std::min(1.0, (db - threshold) / 10.0 + 0.5);
}

// ============================================================================
// FFT Spectral Pitch Detector Implementation
// ============================================================================

FFTPitchDetector::FFTPitchDetector(int sampleRate, int fftSize, double minFreq, double maxFreq)
    : PitchDetector(sampleRate), params_.fftSize = fftSize {
    
    params_.minFrequency = minFreq;
    params_.maxFrequency = maxFreq;
    params_.windowType = 1.0;  // Hann window
    params_.frequencyThreshold = 0.05;
    
    spectrum_.clear();
    currentPeaks_.clear();
}

FFTPitchDetector::~FFTPitchDetector() = default;

void FFTPitchDetector::computeFFTSimple(const float* input, float* real, float* imag, int size) {
    int N = size;
    
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

std::vector<double> FFTPitchDetector::computeFFT(const float* audioBuffer, int numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Use appropriate FFT implementation
    FFTFactory::select(params_.fftSize)->compute(audioBuffer, spectrum_);
    
    return spectrum_;
}

double FFTPitchDetector::detectPitch(const float* audioBuffer, int numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Compute spectrum
    computeFFT(audioBuffer, numSamples);
    
    // Find peaks in frequency range
    double minBin = params_.minFrequency * params_.fftSize_ / params_.sampleRate_;
    double maxBin = params_.maxFrequency * params_.fftSize_ / params_.sampleRate_;
    minBin = std::max(0, static_cast<int>(minBin));
    maxBin = std::min((int)spectrum_.size() - 1, static_cast<int>(maxBin));
    
    // Find dominant peak
    std::vector<SignalPeak> peaks = SpectrumAnalyzer::detectPeaks(
        spectrum_,
        params_.sampleRate_,
        params_.frequencyThreshold,
        params_.peakSearchWindow,
        2.0 * params_.frequencyResolution_);
    
    for (const auto& peak : peaks) {
        if (peak.frequency >= params_.minFrequency && peak.frequency <= params_.maxFrequency) {
            lastFrequency_ = peak.frequency;
            detectionConfidence_ = peak.confidence;
            pitchPresent_ = peak.confidence > 0.3;
            break;
        }
    }
    
    if (!pitchPresent_ && !peaks.empty()) {
        // Use highest peak in range
        lastFrequency_ = peaks[0].frequency;
        detectionConfidence_ = peaks[0].confidence;
        pitchPresent_ = detectionConfidence_ > 0.3;
    }
    
    currentPeaks_ = std::move(peaks);
    
    if (detectionCallback_) {
        detectionCallback_(lastFrequency_, detectionConfidence_);
    }
    
    return pitchPresent_ ? lastFrequency_ : 0.0;
}

const std::vector<double>& FFTPitchDetector::getSpectrum() const {
    return spectrum_;
}

std::vector<SignalPeak> FFTPitchDetector::getPeaks(double threshold) const {
    return currentPeaks_;
}

double FFTPitchDetector::detectDominantFrequency(const float* audioBuffer, 
                                                int startIndex, int endIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Extract segment
    int segmentSize = endIndex - startIndex;
    std::vector<float> segment(segmentSize);
    
    for (int i = 0; i < segmentSize && i < (int)audioBuffer.size(); i++) {
        segment[i] = audioBuffer[startIndex + i];
    }
    
    // Compute spectrum of segment
    auto spectrum = computeFFT(segment.data(), segment.size());
    
    // Find highest peak in range
    int minBin = params_.minFrequency * spectrum.size() / params_.sampleRate_;
    int maxBin = params_.maxFrequency * spectrum.size() / params_.sampleRate_;
    minBin = std::max(0, std::min(minBin, (int)spectrum.size() - 1));
    maxBin = std::min((int)spectrum.size() - 1, std::max(0, maxBin));
    
    double maxEnergy = 0.0;
    double dominantFreq = 0.0;
    
    for (int bin = minBin; bin < maxBin; bin++) {
        double magSq = spectrum[bin] * spectrum[bin];
        if (magSq > maxEnergy) {
            maxEnergy = magSq;
            dominantFreq = bin * params_.sampleRate_ / spectrum.size();
        }
    }
    
    return dominantFreq;
}

// ============================================================================
// Combined Pitch Detection Implementation
// ============================================================================

CombinedPitchDetector::CombinedPitchDetector(int sampleRate,
                                            const std::vector<PitchDetectionAlgorithm>& algorithms)
    : PitchDetector(sampleRate) {
    
    configureAlgorithms(algorithms);
}

CombinedPitchDetector::~CombinedPitchDetector() = default;

void CombinedPitchDetector::setAlgorithms(const std::vector<PitchDetectionAlgorithm>& algorithms) {
    std::lock_guard<std::mutex> lock(mutex_);
    configuredAlgorithms_ = algorithms;
    
    detectors_.clear();
    individualResults_.clear();
    finalResults_.clear();
    consensusResults_.clear();
    
    for (PitchDetectionAlgorithm alg : algorithms) {
        switch (alg) {
            case PitchDetectionAlgorithm::YIN:
                detectors_.push_back(std::make_unique<YINPitchDetector>(sampleRate_));
                break;
            case PitchDetectionAlgorithm::Goertzel:
                detectors_.push_back(std::make_unique<GoertzelPitchDetector>(sampleRate_));
                break;
            case PitchDetectionAlgorithm::FFT_SPECTRAL:
                detectors_.push_back(std::make_unique<FFTPitchDetector>(sampleRate_));
                break;
            case PitchDetectionAlgorithm::Auto_YIN:
                detectors_.push_back(std::make_unique<YINPitchDetector>(sampleRate_));
                break;
        }
    }
}

double CombinedPitchDetector::detectPitch(const float* audioBuffer, int numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<DetResult> allResults;
    
    // Run all detectors
    for (auto& detector : detectors_) {
        detector->detectPitch(audioBuffer, numSamples);
        
        DetResult result;
        result.method = PitchDetectionAlgorithm::YIN;  // Set based on detector type
        result.frequency = detector->getLastFrequency();
        result.confidence = detector->getConfidence();
        result.intensity = detector->getLastEnergy();  // If available
        
        allResults.push_back(result);
        individualResults_.push_back(result);
    }
    
    // Calculate consensus
    consensusResults_.clear();
    
    // Group results by method
    std::map<PitchDetectionAlgorithm, std::vector<DetResult>> methodResults;
    for (const auto& result : individualResults_) {
        methodResults[result.method].push_back(result);
    }
    
    // Find consensus across methods
    double consensusFreq = calculateConsensus(methodResults);
    
    lastFrequency_ = consensusFreq;
    detectionConfidence_ = 0.0;
    pitchPresent_ = consensusFreq > 0.0;
    
    if (detectionCallback_) {
        detectionCallback_(lastFrequency_, detectionConfidence_);
    }
    
    return pitchPresent_ ? lastFrequency_ : 0.0;
}

double CombinedPitchDetector::getCombinedConfidence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Average confidence across methods that agree
    double totalConfidence = 0.0;
    int agreeingMethods = 0;
    
    for (const auto& result : consensusResults_) {
        totalConfidence += result.confidence;
        agreeingMethods++;
    }
    
    return agreeingMethods > 0 ? totalConfidence / agreeingMethods : 0.0;
}

PitchDetectionAlgorithm CombinedPitchDetector::getBestMethod() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Return method with highest average confidence
    PitchDetectionAlgorithm bestMethod = PitchDetectionAlgorithm::YIN;
    double maxConfidence = 0.0;
    
    for (const auto& result : individualResults_) {
        double avgConf = 0.0;
        int count = 0;
        
        for (const auto& r : individualResults_) {
            if (r.method == result.method) {
                avgConf += r.confidence;
                count++;
            }
        }
        
        if (count > 0) {
            avgConf /= count;
            if (avgConf > maxConfidence) {
                maxConfidence = avgConf;
                bestMethod = result.method;
            }
        }
    }
    
    return bestMethod;
}

double CombinedPitchDetector::calculateConsensus(const std::vector<DetResult>& results) {
    if (results.empty()) return 0.0;
    
    // Use weighted average based on confidence
    double weightedSum = 0.0;
    double weightSum = 0.0;
    
    for (const auto& result : results) {
        // Weight by confidence
        double weight = result.confidence;
        weightedSum += result.frequency * weight;
        weightSum += weight;
    }
    
    if (weightSum == 0.0) {
        // All methods have 0 confidence - use median frequency
        std::vector<double> freqs;
        for (const auto& result : results) {
            if (result.frequency > 0.0) {
                freqs.push_back(result.frequency);
            }
        }
        
        if (freqs.empty()) return 0.0;
        
        std::sort(freqs.begin(), freqs.end());
        return freqs[freqs.size() / 2];
    }
    
    return weightedSum / weightSum;
}

// ============================================================================
// Tinnitus Detection Implementation
// ============================================================================

TinnitusDetector::TinnitusDetector(int sampleRate, 
                                  DetectionMethod method, 
                                  double minFreq, double maxFreq)
    : sampleRate_(sampleRate),
      minFreq_(minFreq),
      maxFreq_(maxFreq),
      detectionMethod_(method) {
    
    switch (method) {
        case DetectionMethod::AUTO:
            pitchDetector_ = std::make_unique<CombinedPitchDetector>(sampleRate, {
                PitchDetectionAlgorithm::YIN,
                PitchDetectionAlgorithm::FFT_SPECTRAL
            });
            break;
        case DetectionMethod::MANUAL:
            pitchDetector_ = std::make_unique<YINPitchDetector>(sampleRate, minFreq, maxFreq);
            break;
        case DetectionMethod::GUIDED_TEST:
            pitchDetector_ = std::make_unique<YINPitchDetector>(sampleRate, minFreq, maxFreq);
            testTone_ = std::make_unique<AudioBuffer>(sampleRate, 1024, 1);
            break;
    }
}

TinnitusDetector::~TinnitusDetector() = default;

void TinnitusDetector::setMethod(DetectionMethod newMethod) {
    detectionMethod_ = newMethod;
    
    switch (newMethod) {
        case DetectionMethod::AUTO:
            pitchDetector_ = std::make_unique<CombinedPitchDetector>(sampleRate_, {
                PitchDetectionAlgorithm::YIN,
                PitchDetectionAlgorithm::FFT_SPECTRAL
            });
            break;
        case DetectionMethod::MANUAL:
            pitchDetector_ = std::make_unique<YINPitchDetector>(sampleRate_, minFreq_, maxFreq_);
            break;
        case DetectionMethod::GUIDED_TEST:
            pitchDetector_ = std::make_unique<YINPitchDetector>(sampleRate_, minFreq_, maxFreq_);
            testTone_ = std::make_unique<AudioBuffer>(sampleRate_, 1024, 1);
            break;
    }
}

double TinnitusDetector::analyze(const float* audioBuffer, int numSamples) {
    if (audioBuffer == nullptr || numSamples < 1024) {
        return 0.0;
    }
    
    // Process audio through pitch detector
    double detectedFreq = pitchDetector_->detectPitch(audioBuffer, numSamples);
    
    if (detectedFreq == 0.0) {
        return 0.0;
    }
    
    // Validate detection (tinnitus is continuous or semi-continuous)
    int numValidDetections = 0;
    double totalConfidence = 0.0;
    
    for (int i = 0; i < numSamples; i += 1024) {
        double freq = pitchDetector_->detectPitch(audioBuffer + i, 1024);
        if (freq > 100.0) {
            numValidDetections++;
            totalConfidence += pitchDetector_->getConfidence();
        }
    }
    
    double detectionConfidence = numValidDetections > 0 ? 
                                totalConfidence / numValidDetections : 0.0;
    
    detectionConfidence = std::min(detectionConfidence, 1.0);
    
    if (detectionConfidence < 0.5) {
        return 0.0;  // Not confident enough
    }
    
    // Estimate characteristics
    std::string characteristics;
    
    // Check for fluctuation
    std::vector<double> frequencies;
    for (int i = 0; i < numSamples; i += 1024) {
        double freq = pitchDetector_->detectPitch(audioBuffer + i, 1024);
        if (freq > 100.0) {
            frequencies.push_back(freq);
        }
    }
    
    if (frequencies.size() > 5) {
        double freqRange = frequencies.back() - frequencies.front();
        if (freqRange < 100.0) {
            characteristics = "continuous";
        } else {
            characteristics = "fluctuating";
        }
    }
    
    // Store result
    tinnitusResult_ = {
        numValidDetections > 0,
        detectedFreq,
        detectionConfidence,
        0.0,  // intensity will be estimated from peak amplitude
        characteristics
    };
    
    return detectedFreq;
}

// Guided test mode
void TinnitusDetector::guidedTest() {
    if (testTone_ == nullptr) return;
    
    // Generate test tones across frequency range
    double freqStep = (maxFreq_ - minFreq_) / 20.0;
    
    for (double freq = minFreq_; freq <= maxFreq_; freq += freqStep) {
        float* data = (float*)testTone_->getSamples();
        int samples = testTone_->getFrameCount();
        int channels = testTone_->getChannels();
        int sampleRate = testTone_->getSampleRate();
        
        // Generate sine wave
        for (int i = 0; i < samples; i++) {
            double phase = 2.0 * M_PI * freq * i / sampleRate;
            for (int c = 0; c < channels; c++) {
                data[i * channels + c] = std::sin(phasephase) * 0.3;
            }
        }
        
        testTone_->markComplete();
    }
}

} // namespace dsp
} // namespace tinnotus