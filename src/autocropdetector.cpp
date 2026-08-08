#include "autocropdetector.h"

#include <QFile>
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <opencv2/opencv.hpp>
// OpenCV 5 moved arcLength/approxPolyDP/contourArea/boundingRect into geometry.hpp.
// OpenCV 4 keeps them in imgproc (already pulled by opencv.hpp).
#if defined(CV_VERSION_MAJOR) && (CV_VERSION_MAJOR >= 5)
#include <opencv2/geometry.hpp>
#endif
#include "imageiohelper.h"

namespace {

constexpr double SUPPORTED_ASPECTS[] = { 16.0 / 9.0, 4.0 / 3.0 };

struct Stripped { int top = 0; int bottom = 0; int left = 0; int right = 0; };

Stripped stripBlackBorders(const cv::Mat& gray, const AutoCropConfig& cfg)
{
    Stripped s;
    const int width = gray.cols;
    const int height = gray.rows;
    if (width <= 0 || height <= 0) return s;

    const int maxV = static_cast<int>(std::floor(height * cfg.maxBorderFrac));
    const int maxH = static_cast<int>(std::floor(width * cfg.maxBorderFrac));

    auto rowMean = [&](int row) {
        const uchar* p = gray.ptr<uchar>(row);
        long long sum = 0;
        for (int j = 0; j < width; ++j) sum += p[j];
        return static_cast<double>(sum) / width;
    };
    auto colMean = [&](int col) {
        long long sum = 0;
        for (int i = 0; i < height; ++i) sum += gray.at<uchar>(i, col);
        return static_cast<double>(sum) / height;
    };

    for (int i = 0; i < maxV; ++i) {
        if (rowMean(i) > cfg.blackThreshold) break;
        s.top = i + 1;
    }
    for (int i = height - 1; i > height - 1 - maxV; --i) {
        if (rowMean(i) > cfg.blackThreshold) break;
        s.bottom = height - i;
    }
    for (int j = 0; j < maxH; ++j) {
        if (colMean(j) > cfg.blackThreshold) break;
        s.left = j + 1;
    }
    for (int j = width - 1; j > width - 1 - maxH; --j) {
        if (colMean(j) > cfg.blackThreshold) break;
        s.right = width - j;
    }
    return s;
}

double scoreAspect(double aspect, double tolerance)
{
    double best = std::numeric_limits<double>::infinity();
    for (double a : SUPPORTED_ASPECTS) {
        double diff = std::abs(aspect - a) / a;
        if (diff < best) best = diff;
    }
    if (tolerance <= 0.0) return 0.0;
    return std::max(0.0, 1.0 - best / tolerance);
}

struct YoloDecoded {
    cv::Rect2f rect;
    float confidence = 0.0f;
};

std::vector<YoloDecoded> decodeYoloOutput(const float* output,
                                          const std::vector<int64_t>& dims,
                                          int srcW, int srcH,
                                          float scale, int padX, int padY,
                                          float confThreshold)
{
    std::vector<YoloDecoded> out;
    if (dims.size() != 3 || dims[0] != 1) return out;
    const int C = static_cast<int>(dims[1]);
    const int N = static_cast<int>(dims[2]);
    if (C < 5) return out;

    out.reserve(64);
    for (int i = 0; i < N; ++i) {
        const float cx = output[0 * N + i];
        const float cy = output[1 * N + i];
        const float w  = output[2 * N + i];
        const float h  = output[3 * N + i];
        float conf = output[4 * N + i];
        for (int c = 5; c < C; ++c) {
            const float v = output[c * N + i];
            if (v > conf) conf = v;
        }
        if (conf < confThreshold) continue;

        const float x1i = cx - w * 0.5f;
        const float y1i = cy - h * 0.5f;
        const float x2i = cx + w * 0.5f;
        const float y2i = cy + h * 0.5f;
        const float x1 = (x1i - padX) / scale;
        const float y1 = (y1i - padY) / scale;
        const float x2 = (x2i - padX) / scale;
        const float y2 = (y2i - padY) / scale;
        const float bx = std::max(0.0f, std::min(static_cast<float>(srcW), x1));
        const float by = std::max(0.0f, std::min(static_cast<float>(srcH), y1));
        const float ex = std::max(0.0f, std::min(static_cast<float>(srcW), x2));
        const float ey = std::max(0.0f, std::min(static_cast<float>(srcH), y2));
        const float bw = ex - bx;
        const float bh = ey - by;
        if (bw <= 1.0f || bh <= 1.0f) continue;

        out.push_back({ cv::Rect2f(bx, by, bw, bh), conf });
    }
    return out;
}

float iou(const cv::Rect2f& a, const cv::Rect2f& b)
{
    const float ax2 = a.x + a.width;
    const float ay2 = a.y + a.height;
    const float bx2 = b.x + b.width;
    const float by2 = b.y + b.height;
    const float ix1 = std::max(a.x, b.x);
    const float iy1 = std::max(a.y, b.y);
    const float ix2 = std::min(ax2, bx2);
    const float iy2 = std::min(ay2, by2);
    const float iw = std::max(0.0f, ix2 - ix1);
    const float ih = std::max(0.0f, iy2 - iy1);
    const float inter = iw * ih;
    const float un = a.width * a.height + b.width * b.height - inter;
    return un <= 0.0f ? 0.0f : inter / un;
}

std::vector<YoloDecoded> nms(std::vector<YoloDecoded> boxes, float iouThreshold)
{
    std::sort(boxes.begin(), boxes.end(),
              [](const YoloDecoded& a, const YoloDecoded& b) { return a.confidence > b.confidence; });
    std::vector<YoloDecoded> kept;
    kept.reserve(boxes.size());
    for (const auto& cand : boxes) {
        bool overlap = false;
        for (const auto& k : kept) {
            if (iou(cand.rect, k.rect) > iouThreshold) { overlap = true; break; }
        }
        if (!overlap) kept.push_back(cand);
    }
    return kept;
}

#ifdef ONNX_AVAILABLE
std::vector<std::string> executionProviderPriority()
{
    std::vector<std::string> providers;
#ifdef __APPLE__
    providers.push_back("CoreMLExecutionProvider");
#elif defined(_WIN32)
    providers.push_back("CUDAExecutionProvider");
    providers.push_back("DmlExecutionProvider");
#else
    providers.push_back("CUDAExecutionProvider");
#endif
    return providers;
}
#endif

} // namespace

AutoCropDetector::AutoCropDetector(const AutoCropConfig& cfg)
    : m_config(cfg)
#ifdef ONNX_AVAILABLE
    , m_memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
#endif
{
}

AutoCropDetector::~AutoCropDetector() = default;

void AutoCropDetector::updateConfig(const AutoCropConfig& cfg)
{
    const bool modelChanged = (cfg.yoloModelPath != m_config.yoloModelPath);
    m_config = cfg;
    if (modelChanged) {
#ifdef ONNX_AVAILABLE
        m_session.reset();
        m_sessionOptions.reset();
        m_env.reset();
        m_inputName.clear();
        m_outputName.clear();
        m_inputNames.clear();
        m_outputNames.clear();
#endif
        m_loadedModelPath.clear();
        m_yoloError.clear();
        m_sessionTried = false;
    }
}

bool AutoCropDetector::isYoloReady() const
{
#ifdef ONNX_AVAILABLE
    return m_session != nullptr;
#else
    return false;
#endif
}

AutoCropResult AutoCropDetector::detect(const QString& imagePath)
{
    cv::Mat img = ImageIOHelper::imreadUnicode(imagePath, cv::IMREAD_COLOR);
    if (img.empty()) {
        AutoCropResult r;
        r.errorMessage = QString("Failed to load image: %1").arg(imagePath);
        return r;
    }
    return detect(img);
}

AutoCropResult AutoCropDetector::detect(const cv::Mat& bgr)
{
    if (bgr.empty()) {
        AutoCropResult r;
        r.errorMessage = "Empty image";
        return r;
    }

    QElapsedTimer timer;
    timer.start();

    auto finalize = [&](AutoCropResult r) {
        r.durationMs = timer.nsecsElapsed() / 1e6;
        return r;
    };

    switch (m_config.mode) {
        case AutoCropMode::CannyOnly:
            return finalize(runCanny(bgr));
        case AutoCropMode::YoloOnly:
            return finalize(runYolo(bgr));
        case AutoCropMode::CannyThenYolo:
        default: {
            AutoCropResult cannyResult = runCanny(bgr);
            if (cannyResult.isValid()) return finalize(cannyResult);
            AutoCropResult yoloResult = runYolo(bgr);
            if (yoloResult.isValid()) return finalize(yoloResult);
            // No detection — return canny's (invalid) result so the user gets
            // any error message from the canny path.
            return finalize(cannyResult);
        }
    }
}

// ----------------------------------------------------------------------------
// Canny pipeline
// ----------------------------------------------------------------------------

AutoCropResult AutoCropDetector::runCanny(const cv::Mat& bgr) const
{
    AutoCropResult result;

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    const int fullW = gray.cols;
    const int fullH = gray.rows;

    Stripped stripped = stripBlackBorders(gray, m_config);
    const int innerW = fullW - stripped.left - stripped.right;
    const int innerH = fullH - stripped.top - stripped.bottom;
    if (innerW <= 0 || innerH <= 0) return result;

    cv::Mat inner = gray(cv::Rect(stripped.left, stripped.top, innerW, innerH));

    cv::Mat edges;
    cv::Canny(inner, edges,
              static_cast<double>(m_config.cannyLowThreshold),
              static_cast<double>(m_config.cannyHighThreshold));

    cv::Mat kernel = cv::Mat::ones(3, 3, CV_8U);
    cv::dilate(edges, edges, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const double innerArea = static_cast<double>(innerW) * innerH;
    double bestScore = -1.0;
    cv::Rect bestRect;

    for (const auto& cnt : contours) {
        if (cnt.size() < 4) continue;
        const double peri = cv::arcLength(cnt, true);
        std::vector<cv::Point> approx;
        cv::approxPolyDP(cnt, approx, 0.02 * peri, true);
        if (approx.size() != 4) continue;

        const cv::Rect rect = cv::boundingRect(cnt);
        const double area = static_cast<double>(rect.width) * rect.height;
        const double areaRatio = area / innerArea;
        if (areaRatio < m_config.areaRatioMin || areaRatio > m_config.areaRatioMax) continue;

        const double marginTop = static_cast<double>(rect.y) / innerH;
        const double marginBottom = static_cast<double>(innerH - rect.y - rect.height) / innerH;
        if (marginTop < m_config.marginFrac && marginBottom < m_config.marginFrac) continue;

        const double cArea = cv::contourArea(cnt, false);
        const double fill = (area > 0.0) ? (cArea / area) : 0.0;
        if (fill < m_config.fillRatioMin) continue;

        if (rect.height <= 0) continue;
        const double aspect = static_cast<double>(rect.width) / rect.height;
        const double aspectScore = scoreAspect(aspect, m_config.aspectTolerance);
        if (aspectScore <= 0.0) continue;

        const double score = areaRatio * aspectScore;
        if (score > bestScore) {
            bestScore = score;
            bestRect = rect;
        }
    }

    if (bestScore < 0.0) return result;

    // Translate from inner coords back to full-image coords.
    QRect bbox(bestRect.x + stripped.left,
               bestRect.y + stripped.top,
               bestRect.width,
               bestRect.height);
    bbox = bbox.intersected(QRect(0, 0, fullW, fullH));
    if (bbox.width() <= 0 || bbox.height() <= 0) return result;

    result.bbox = bbox;
    result.backend = AutoCropResult::Backend::Canny;
    return result;
}

// ----------------------------------------------------------------------------
// YOLO pipeline
// ----------------------------------------------------------------------------

bool AutoCropDetector::ensureYoloSession()
{
#ifdef ONNX_AVAILABLE
    if (m_session) return true;
    if (m_sessionTried && !m_yoloError.isEmpty()) {
        // Don't retry on the same path until the model path changes.
        return false;
    }
    m_sessionTried = true;

    const QString modelPath = m_config.yoloModelPath;
    if (modelPath.isEmpty()) {
        m_yoloError = "YOLO model path is empty";
        return false;
    }

    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "AutoCropDetector");
        m_sessionOptions = std::make_unique<Ort::SessionOptions>();
        m_sessionOptions->SetIntraOpNumThreads(4);
        m_sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Best-effort hardware acceleration; CPU is the unconditional fallback.
        for (const std::string& provider : executionProviderPriority()) {
            try {
                if (provider == "CoreMLExecutionProvider") {
#ifdef __APPLE__
                    std::unordered_map<std::string, std::string> coreml_options;
                    coreml_options["MLComputeUnits"] = "ALL";
                    coreml_options["ModelFormat"] = "MLProgram";
                    coreml_options["RequireStaticInputShapes"] = "0";
                    m_sessionOptions->AppendExecutionProvider("CoreML", coreml_options);
                    qInfo() << "AutoCropDetector: Using Core ML execution provider";
                    break;
#endif
                } else if (provider == "CUDAExecutionProvider") {
                    std::unordered_map<std::string, std::string> cuda_options;
                    cuda_options["device_id"] = "0";
                    m_sessionOptions->AppendExecutionProvider("CUDA", cuda_options);
                    qInfo() << "AutoCropDetector: Using CUDA execution provider";
                    break;
                } else if (provider == "DmlExecutionProvider") {
#ifdef _WIN32
                    std::unordered_map<std::string, std::string> dml_options;
                    dml_options["device_id"] = "0";
                    m_sessionOptions->AppendExecutionProvider("DML", dml_options);
                    qInfo() << "AutoCropDetector: Using DirectML execution provider";
                    break;
#endif
                }
            } catch (const std::exception& e) {
                qDebug() << "AutoCropDetector:" << QString::fromStdString(provider)
                         << "unavailable:" << e.what();
            }
        }

        if (modelPath.startsWith(":/") || modelPath.startsWith("qrc:")) {
            QFile modelFile(modelPath);
            if (!modelFile.open(QIODevice::ReadOnly)) {
                m_yoloError = QString("Failed to open YOLO model resource: %1").arg(modelPath);
                return false;
            }
            QByteArray modelData = modelFile.readAll();
            modelFile.close();
            m_session = std::make_unique<Ort::Session>(*m_env,
                                                       modelData.constData(),
                                                       modelData.size(),
                                                       *m_sessionOptions);
        } else {
#ifdef _WIN32
            std::wstring wPath = modelPath.toStdWString();
            m_session = std::make_unique<Ort::Session>(*m_env, wPath.c_str(), *m_sessionOptions);
#else
            std::string sPath = modelPath.toStdString();
            m_session = std::make_unique<Ort::Session>(*m_env, sPath.c_str(), *m_sessionOptions);
#endif
        }

        Ort::AllocatorWithDefaultOptions allocator;
        if (m_session->GetInputCount() < 1 || m_session->GetOutputCount() < 1) {
            m_yoloError = "YOLO model has unexpected input/output count";
            m_session.reset();
            return false;
        }

        Ort::AllocatedStringPtr inName = m_session->GetInputNameAllocated(0, allocator);
        Ort::AllocatedStringPtr outName = m_session->GetOutputNameAllocated(0, allocator);
        m_inputName = inName.get();
        m_outputName = outName.get();
        m_inputNames = { m_inputName.c_str() };
        m_outputNames = { m_outputName.c_str() };

        m_loadedModelPath = modelPath;
        m_yoloError.clear();
        return true;
    } catch (const std::exception& e) {
        m_yoloError = QString("Failed to create YOLO session: %1").arg(e.what());
        m_session.reset();
        return false;
    }
#else
    m_yoloError = "ONNX Runtime not available";
    return false;
#endif
}

AutoCropResult AutoCropDetector::runYolo(const cv::Mat& bgr)
{
    AutoCropResult result;
#ifdef ONNX_AVAILABLE
    if (!ensureYoloSession()) {
        result.errorMessage = m_yoloError;
        return result;
    }

    const int srcW = bgr.cols;
    const int srcH = bgr.rows;
    const int size = std::max(32, m_config.yoloInputSize);

    // Letterbox to size x size with gray (114) padding, preserving aspect.
    const float scale = std::min(static_cast<float>(size) / srcW,
                                 static_cast<float>(size) / srcH);
    const int newW = std::max(1, static_cast<int>(std::round(srcW * scale)));
    const int newH = std::max(1, static_cast<int>(std::round(srcH * scale)));
    const int padX = (size - newW) / 2;
    const int padY = (size - newH) / 2;

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);

    cv::Mat padded(size, size, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(padX, padY, newW, newH)));

    // BGR -> RGB and convert to CHW float32 in [0,1].
    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    cv::Mat rgbFloat;
    rgb.convertTo(rgbFloat, CV_32F, 1.0 / 255.0);

    const int area = size * size;
    std::vector<float> tensor(static_cast<size_t>(3) * area);
    // HWC -> CHW
    std::vector<cv::Mat> channels(3);
    for (int c = 0; c < 3; ++c) {
        channels[c] = cv::Mat(size, size, CV_32F, tensor.data() + static_cast<size_t>(c) * area);
    }
    cv::split(rgbFloat, channels);

    try {
        std::vector<int64_t> inputShape = { 1, 3, size, size };
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            m_memoryInfo,
            tensor.data(),
            tensor.size(),
            inputShape.data(),
            inputShape.size());

        std::vector<Ort::Value> outputs = m_session->Run(
            Ort::RunOptions{ nullptr },
            m_inputNames.data(),
            &inputTensor,
            1,
            m_outputNames.data(),
            1);

        if (outputs.empty()) {
            result.errorMessage = "YOLO produced no output";
            return result;
        }

        Ort::TensorTypeAndShapeInfo info = outputs[0].GetTensorTypeAndShapeInfo();
        std::vector<int64_t> dims = info.GetShape();
        const float* output = outputs[0].GetTensorData<float>();

        std::vector<YoloDecoded> raw = decodeYoloOutput(
            output, dims,
            srcW, srcH,
            scale, padX, padY,
            m_config.yoloConfidenceThreshold);

        std::vector<YoloDecoded> kept = nms(std::move(raw), m_config.yoloIouThreshold);
        if (kept.empty()) return result;

        const cv::Rect2f& best = kept.front().rect;
        QRect bbox(static_cast<int>(std::round(best.x)),
                   static_cast<int>(std::round(best.y)),
                   static_cast<int>(std::round(best.width)),
                   static_cast<int>(std::round(best.height)));
        bbox = bbox.intersected(QRect(0, 0, srcW, srcH));
        if (bbox.width() <= 0 || bbox.height() <= 0) return result;

        result.bbox = bbox;
        result.backend = AutoCropResult::Backend::Yolo;
        return result;
    } catch (const std::exception& e) {
        result.errorMessage = QString("YOLO inference failed: %1").arg(e.what());
        return result;
    }
#else
    Q_UNUSED(bgr);
    result.errorMessage = "ONNX Runtime not available";
    return result;
#endif
}
