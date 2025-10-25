#ifndef IMAGEIOHELPER_H
#define IMAGEIOHELPER_H

#include <QString>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <vector>

/**
 * Helper functions for Unicode-safe image I/O on Windows
 *
 * OpenCV's cv::imwrite() and cv::imread() do not support Unicode paths on Windows.
 * These helper functions use cv::imencode()/cv::imdecode() with Qt's file APIs
 * to properly handle Unicode paths on all platforms.
 */
class ImageIOHelper
{
public:
    /**
     * Write an image to disk with Unicode path support
     * @param filePath Path to save the image (supports Unicode)
     * @param image OpenCV Mat to save
     * @param params Compression parameters (e.g., JPEG quality)
     * @return true if successful, false otherwise
     */
    static bool imwriteUnicode(const QString& filePath, const cv::Mat& image,
                               const std::vector<int>& params = std::vector<int>())
    {
        if (image.empty()) {
            return false;
        }

        // Determine file extension
        QString ext = filePath.right(4).toLower();
        if (!ext.startsWith('.')) {
            ext = ".jpg"; // Default to JPEG
        }

        // Encode image to memory buffer
        std::vector<uchar> buffer;
        if (!cv::imencode(ext.toStdString(), image, buffer, params)) {
            return false;
        }

        // Write buffer to file using Qt's Unicode-aware file APIs
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }

        qint64 written = file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        file.close();

        return written == static_cast<qint64>(buffer.size());
    }

    /**
     * Read an image from disk with Unicode path support
     * @param filePath Path to read the image from (supports Unicode)
     * @param flags OpenCV imread flags (e.g., cv::IMREAD_COLOR)
     * @return OpenCV Mat (empty if failed)
     */
    static cv::Mat imreadUnicode(const QString& filePath, int flags = cv::IMREAD_COLOR)
    {
        // Read file into memory buffer using Qt's Unicode-aware file APIs
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return cv::Mat();
        }

        QByteArray fileData = file.readAll();
        file.close();

        if (fileData.isEmpty()) {
            return cv::Mat();
        }

        // Decode image from memory buffer
        std::vector<uchar> buffer(fileData.begin(), fileData.end());
        cv::Mat image = cv::imdecode(buffer, flags);

        return image;
    }

    /**
     * Read an image from disk with Unicode path support (std::string overload)
     * @param filePath Path to read the image from (UTF-8 encoded std::string)
     * @param flags OpenCV imread flags (e.g., cv::IMREAD_COLOR)
     * @return OpenCV Mat (empty if failed)
     */
    static cv::Mat imreadUnicode(const std::string& filePath, int flags = cv::IMREAD_COLOR)
    {
        return imreadUnicode(QString::fromUtf8(filePath.c_str()), flags);
    }
};

#endif // IMAGEIOHELPER_H
