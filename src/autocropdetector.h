#ifndef AUTOCROPDETECTOR_H
#define AUTOCROPDETECTOR_H

#include <QString>
#include <QRect>
#include <memory>
#include <vector>

#include "configmanager.h"

#ifdef ONNX_AVAILABLE
#include <onnxruntime_cxx_api.h>
#endif

namespace cv { class Mat; }

/**
 * @brief Result of an auto-crop detection.
 *
 * `bbox` is in original-image pixel coordinates and is intended to be passed
 * directly to CropManager::applyCrop or CropImageView::setInitialSelection.
 * If detection fails, `bbox` is invalid (`QRect()`) and `backend` is `None`.
 */
struct AutoCropResult {
    enum class Backend { None, Canny, Yolo };

    QRect bbox;
    Backend backend = Backend::None;
    double durationMs = 0.0;
    QString errorMessage;

    bool isValid() const { return bbox.isValid() && bbox.width() > 0 && bbox.height() > 0; }
};

/**
 * @brief Detects the slide bounding box in an image, using either a classical
 *        Canny pipeline or a YOLOv8 ONNX model (or both, in fallback order).
 *
 * Mirrors the Electron auto-crop worker (REFERENCE/autoCrop.worker.ts):
 * - **Canny** strips black borders, runs Canny + dilate + findContours, then
 *   filters rectangular contours by area, aspect, margin and fill ratio.
 * - **YOLO** letterboxes to a fixed input size, runs the ONNX model, decodes
 *   the [1, C, N] tensor and applies NMS.
 *
 * The ONNX session is lazily initialised on first YOLO call. If ONNX is not
 * available (compile-time `ONNX_AVAILABLE` not set) or session creation fails,
 * YOLO calls return an invalid result; Canny continues to work.
 */
class AutoCropDetector
{
public:
    explicit AutoCropDetector(const AutoCropConfig& cfg);
    ~AutoCropDetector();

    AutoCropDetector(const AutoCropDetector&) = delete;
    AutoCropDetector& operator=(const AutoCropDetector&) = delete;

    /** Detection on a file path (Unicode-safe via ImageIOHelper). */
    AutoCropResult detect(const QString& imagePath);

    /** Detection on a BGR cv::Mat (the canonical OpenCV format). */
    AutoCropResult detect(const cv::Mat& bgr);

    /** Replace the active config. Recreates the YOLO session if the model path changed. */
    void updateConfig(const AutoCropConfig& cfg);

    /** True if ONNX support is compiled in and the YOLO session is loaded. */
    bool isYoloReady() const;

    /** Last YOLO error message (empty if none). */
    QString yoloError() const { return m_yoloError; }

private:
    AutoCropConfig m_config;

    // ----- Canny -----
    AutoCropResult runCanny(const cv::Mat& bgr) const;

    // ----- YOLO -----
    AutoCropResult runYolo(const cv::Mat& bgr);
    bool ensureYoloSession();

#ifdef ONNX_AVAILABLE
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::SessionOptions> m_sessionOptions;
    std::unique_ptr<Ort::Session> m_session;
    Ort::MemoryInfo m_memoryInfo;
    std::string m_inputName;
    std::string m_outputName;
    std::vector<const char*> m_inputNames;
    std::vector<const char*> m_outputNames;
#endif
    QString m_yoloError;
    QString m_loadedModelPath;
    bool m_sessionTried = false;
};

#endif // AUTOCROPDETECTOR_H
