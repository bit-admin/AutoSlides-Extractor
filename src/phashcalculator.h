#ifndef PHASHCALCULATOR_H
#define PHASHCALCULATOR_H

#include <opencv2/opencv.hpp>
#include <QString>
#include <cstdint>
#include <vector>

/**
 * @brief 256-bit Perceptual Hash (pHash) Calculator
 *
 * Implements a 256-bit perceptual hash using DCT (Discrete Cosine Transform)
 * for image similarity detection. This is useful for detecting duplicate or
 * near-duplicate images in post-processing.
 */
class PHashCalculator
{
public:
    /**
     * @brief Calculate 256-bit perceptual hash of an image
     * @param imagePath Path to the image file
     * @return 256-bit hash as a 32-byte array (returns empty vector on error)
     */
    static std::vector<uint8_t> calculatePHash(const QString& imagePath);

    /**
     * @brief Calculate 256-bit perceptual hash from OpenCV Mat
     * @param image OpenCV Mat image (will be converted to grayscale if needed)
     * @return 256-bit hash as a 32-byte array (returns empty vector on error)
     */
    static std::vector<uint8_t> calculatePHash(const cv::Mat& image);

    /**
     * @brief Calculate Hamming distance between two pHashes
     * @param hash1 First hash (32 bytes)
     * @param hash2 Second hash (32 bytes)
     * @return Hamming distance (number of differing bits), or -1 on error
     */
    static int hammingDistance(const std::vector<uint8_t>& hash1, const std::vector<uint8_t>& hash2);

    /**
     * @brief Convert hash bytes to hex string
     * @param hash Hash as byte array
     * @return Hex string representation
     */
    static QString hashToHexString(const std::vector<uint8_t>& hash);

    /**
     * @brief Convert hex string to hash bytes
     * @param hexString Hex string representation
     * @return Hash as byte array (returns empty vector on error)
     */
    static std::vector<uint8_t> hexStringToHash(const QString& hexString);

private:
    /**
     * @brief Apply 2D Discrete Cosine Transform
     * @param input Input matrix (grayscale image)
     * @param output Output DCT coefficients
     */
    static void applyDCT(const cv::Mat& input, cv::Mat& output);

    /**
     * @brief Calculate median of a matrix
     * @param mat Input matrix
     * @return Median value
     */
    static double calculateMedian(const cv::Mat& mat);

    // Constants for pHash calculation
    static constexpr int HASH_SIDE_DIM = 16;  // 16x16 = 256 bits
    static constexpr int DCT_SIDE_DIM = 64;   // 64x64 DCT (4x hash dimension)
};

#endif // PHASHCALCULATOR_H
