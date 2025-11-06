#include "phashcalculator.h"
#include <algorithm>
#include <cmath>

std::vector<uint8_t> PHashCalculator::calculatePHash(const QString& imagePath)
{
    // Load image
    cv::Mat image = cv::imread(imagePath.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        return std::vector<uint8_t>();
    }

    return calculatePHash(image);
}

std::vector<uint8_t> PHashCalculator::calculatePHash(const cv::Mat& image)
{
    if (image.empty()) {
        return std::vector<uint8_t>();
    }

    // Step 1: Convert to grayscale
    cv::Mat grayscale;
    if (image.channels() == 3 || image.channels() == 4) {
        cv::cvtColor(image, grayscale, cv::COLOR_BGR2GRAY);
    } else {
        grayscale = image.clone();
    }

    // Step 2: Resize to DCT_SIDE_DIM x DCT_SIDE_DIM
    cv::Mat resized;
    cv::resize(grayscale, resized, cv::Size(DCT_SIDE_DIM, DCT_SIDE_DIM), 0, 0, cv::INTER_LINEAR);

    // Step 3: Convert to float for DCT
    cv::Mat floatImage;
    resized.convertTo(floatImage, CV_32F);

    // Step 4: Apply DCT
    cv::Mat dctCoeffs;
    applyDCT(floatImage, dctCoeffs);

    // Step 5: Extract low-frequency coefficients (top-left HASH_SIDE_DIM x HASH_SIDE_DIM)
    cv::Mat lowFreq = dctCoeffs(cv::Rect(0, 0, HASH_SIDE_DIM, HASH_SIDE_DIM)).clone();

    // Step 6: Remove DC component (top-left coefficient)
    std::vector<float> acCoeffs;
    acCoeffs.reserve(HASH_SIDE_DIM * HASH_SIDE_DIM - 1);
    for (int i = 0; i < HASH_SIDE_DIM; i++) {
        for (int j = 0; j < HASH_SIDE_DIM; j++) {
            if (i == 0 && j == 0) continue;  // Skip DC component
            acCoeffs.push_back(lowFreq.at<float>(i, j));
        }
    }

    // Step 7: Calculate median
    double median = 0.0;
    if (!acCoeffs.empty()) {
        std::vector<float> sortedCoeffs = acCoeffs;
        std::sort(sortedCoeffs.begin(), sortedCoeffs.end());
        size_t mid = sortedCoeffs.size() / 2;
        if (sortedCoeffs.size() % 2 == 0) {
            median = (sortedCoeffs[mid - 1] + sortedCoeffs[mid]) / 2.0;
        } else {
            median = sortedCoeffs[mid];
        }
    }

    // Step 8: Generate hash bits (compare each AC coefficient with median)
    std::vector<uint8_t> hash(32, 0);  // 256 bits = 32 bytes
    int bitIndex = 0;
    for (float coeff : acCoeffs) {
        if (coeff >= median) {
            int byteIndex = bitIndex / 8;
            int bitOffset = 7 - (bitIndex % 8);  // MSB first
            hash[byteIndex] |= (1 << bitOffset);
        }
        bitIndex++;
    }

    return hash;
}

void PHashCalculator::applyDCT(const cv::Mat& input, cv::Mat& output)
{
    // OpenCV's dct function performs 2D DCT
    cv::dct(input, output);
}

double PHashCalculator::calculateMedian(const cv::Mat& mat)
{
    std::vector<float> values;
    values.reserve(mat.rows * mat.cols);

    for (int i = 0; i < mat.rows; i++) {
        for (int j = 0; j < mat.cols; j++) {
            values.push_back(mat.at<float>(i, j));
        }
    }

    if (values.empty()) return 0.0;

    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;

    if (values.size() % 2 == 0) {
        return (values[mid - 1] + values[mid]) / 2.0;
    } else {
        return values[mid];
    }
}

int PHashCalculator::hammingDistance(const std::vector<uint8_t>& hash1, const std::vector<uint8_t>& hash2)
{
    if (hash1.size() != 32 || hash2.size() != 32) {
        return -1;  // Invalid hash size
    }

    int distance = 0;
    for (size_t i = 0; i < 32; i++) {
        uint8_t xorResult = hash1[i] ^ hash2[i];
        // Count set bits using Brian Kernighan's algorithm
        while (xorResult) {
            distance++;
            xorResult &= (xorResult - 1);
        }
    }

    return distance;
}

QString PHashCalculator::hashToHexString(const std::vector<uint8_t>& hash)
{
    if (hash.size() != 32) {
        return QString();
    }

    QString hexString;
    hexString.reserve(64);  // 32 bytes = 64 hex characters

    for (uint8_t byte : hash) {
        hexString.append(QString("%1").arg(byte, 2, 16, QChar('0')));
    }

    return hexString;
}

std::vector<uint8_t> PHashCalculator::hexStringToHash(const QString& hexString)
{
    if (hexString.length() != 64) {
        return std::vector<uint8_t>();
    }

    std::vector<uint8_t> hash;
    hash.reserve(32);

    for (int i = 0; i < 64; i += 2) {
        bool ok;
        uint8_t byte = hexString.mid(i, 2).toUInt(&ok, 16);
        if (!ok) {
            return std::vector<uint8_t>();
        }
        hash.push_back(byte);
    }

    return hash;
}
