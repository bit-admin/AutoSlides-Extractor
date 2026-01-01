#ifndef MLCLASSIFIER_H
#define MLCLASSIFIER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QJsonDocument>
#include <QJsonArray>
#include <memory>

#ifdef ONNX_AVAILABLE
#include <onnxruntime_cxx_api.h>
#endif

/**
 * @brief Result of ML classification for a single image
 */
struct ClassificationResult {
    QString imagePath;
    QString predictedClass;
    float confidence;
    QMap<QString, float> classProbabilities;  // All classes with their probabilities
    bool error;
    QString errorMessage;

    ClassificationResult()
        : confidence(0.0f), error(false) {}
};

/**
 * @brief ML-based slide classifier using ONNX Runtime
 *
 * This class loads a MobileNetV4 ONNX model and classifies slide images
 * using prefix-based categories:
 * - "slide": Always kept
 * - "not_slide_*": Removed if confidence >= threshold
 * - "may_be_slide_*": Optionally removed based on user setting
 *
 * Supports hardware acceleration via:
 * - macOS: Core ML
 * - Windows: CUDA, DirectML
 * - Linux: CUDA
 * - Fallback: CPU
 */
class MLClassifier {
public:
    /**
     * @brief Execution provider types for hardware acceleration
     */
    enum class ExecutionProvider {
        Auto,       // Automatically select best available
        CoreML,     // macOS Core ML
        CUDA,       // NVIDIA CUDA
        DirectML,   // Windows DirectML
        CPU         // CPU fallback
    };

    /**
     * @brief Constructor
     * @param modelPath Path to ONNX model file (can be Qt resource path)
     * @param preferredProvider Preferred execution provider (default: Auto)
     */
    explicit MLClassifier(const QString& modelPath,
                         ExecutionProvider preferredProvider = ExecutionProvider::Auto);

    /**
     * @brief Destructor
     */
    ~MLClassifier();

    /**
     * @brief Check if ONNX Runtime is available
     * @return true if ONNX Runtime was compiled in
     */
    static bool isAvailable();

    /**
     * @brief Check if classifier is initialized and ready
     * @return true if model loaded successfully
     */
    bool isInitialized() const;

    /**
     * @brief Get error message if initialization failed
     * @return Error message, empty if no error
     */
    QString getErrorMessage() const;

    /**
     * @brief Get the active execution provider name
     * @return Name of the execution provider being used
     */
    QString getActiveExecutionProvider() const;

    /**
     * @brief Classify a single image
     * @param imagePath Path to image file
     * @return Classification result with predicted class and probabilities
     */
    ClassificationResult classifySingle(const QString& imagePath);

    /**
     * @brief Classify multiple images in batch
     * @param imagePaths List of image file paths
     * @return List of classification results
     */
    QVector<ClassificationResult> classifyBatch(const QStringList& imagePaths);

    /**
     * @brief 2-stage classification thresholds for a category
     */
    struct CategoryThresholds {
        float highThreshold;   // High confidence: delete if >= this (default: 0.9)
        float lowThreshold;    // Low confidence boundary (default: 0.75)

        CategoryThresholds(float high = 0.9f, float low = 0.75f)
            : highThreshold(high), lowThreshold(low) {}
    };

    /**
     * @brief Determine if image should be kept based on 2-stage classification
     *
     * 2-stage logic:
     * - If confidence >= highThreshold -> DELETE
     * - If confidence in [lowThreshold, highThreshold) AND slide probability <= slideMaxThreshold -> DELETE
     * - Else -> KEEP
     *
     * @param result Classification result
     * @param notSlideThresholds Thresholds for not_slide classes
     * @param maybeSlideThresholds Thresholds for may_be_slide classes
     * @param slideMaxThreshold Max slide probability for medium confidence deletion (default: 0.25)
     * @param deleteMaybeSlides Whether to delete "may_be_slide" classes
     * @return true if image should be kept, false if should be removed
     */
    static bool shouldKeepImage(const ClassificationResult& result,
                               const CategoryThresholds& notSlideThresholds,
                               const CategoryThresholds& maybeSlideThresholds,
                               float slideMaxThreshold,
                               bool deleteMaybeSlides);

    /**
     * @brief Get class prefixes used for classification decisions
     * @return List of class prefixes: "slide", "not_slide", "may_be_slide"
     */
    static QStringList getClassPrefixes();

    /**
     * @brief Get default confidence thresholds for class prefixes
     * @return Map of prefix to default threshold (0.8 for not_slide and may_be_slide)
     */
    static QMap<QString, float> getDefaultThresholds();

    /**
     * @brief Get class names from the loaded model
     * @return List of class names (empty if not initialized)
     */
    QStringList getClassNames() const;

    /**
     * @brief Convert execution provider enum to string
     * @param provider Execution provider enum
     * @return String representation
     */
    static QString executionProviderToString(ExecutionProvider provider);

    /**
     * @brief Convert string to execution provider enum
     * @param providerStr String representation
     * @return Execution provider enum
     */
    static ExecutionProvider stringToExecutionProvider(const QString& providerStr);

private:
#ifdef ONNX_AVAILABLE
    /**
     * @brief Initialize ONNX Runtime session with execution providers
     * @param modelPath Path to ONNX model
     * @param preferredProvider Preferred execution provider
     * @return true if initialization successful
     */
    bool initializeSession(const QString& modelPath, ExecutionProvider preferredProvider);

    /**
     * @brief Get available execution providers for current platform
     * @param preferredProvider Preferred provider
     * @return Ordered list of providers to try
     */
    std::vector<std::string> getExecutionProviderPriority(ExecutionProvider preferredProvider);

    /**
     * @brief Preprocess image for model input
     * @param imagePath Path to image file
     * @param inputTensor Output tensor data (1x3x256x256)
     * @return true if preprocessing successful
     */
    bool preprocessImage(const QString& imagePath, std::vector<float>& inputTensor);

    /**
     * @brief Apply ImageNet normalization to image tensor
     * @param tensor Input/output tensor data
     */
    void normalizeImageNet(std::vector<float>& tensor);

    /**
     * @brief Run inference on preprocessed input
     * @param inputTensor Input tensor data
     * @param outputTensor Output tensor data (7 class logits)
     * @return true if inference successful
     */
    bool runInference(const std::vector<float>& inputTensor,
                     std::vector<float>& outputTensor);

    /**
     * @brief Apply softmax to convert logits to probabilities
     * @param logits Input logits
     * @return Probabilities (sum to 1.0)
     */
    std::vector<float> softmax(const std::vector<float>& logits);

    // ONNX Runtime members
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::unique_ptr<Ort::SessionOptions> m_sessionOptions;
    Ort::MemoryInfo m_memoryInfo;

    // Model metadata
    std::string m_inputName;   // Store actual string
    std::string m_outputName;  // Store actual string
    std::vector<const char*> m_inputNames;   // Pointers to m_inputName
    std::vector<const char*> m_outputNames;  // Pointers to m_outputName
    std::vector<int64_t> m_inputShape;   // [1, 3, 256, 256]
    std::vector<int64_t> m_outputShape;  // [1, num_classes]

    // Class names from model (populated during initialization)
    QStringList m_classNames;

    // Active execution provider
    QString m_activeProvider;
#endif

    // ImageNet normalization constants
    static constexpr float IMAGENET_MEAN[3] = {0.485f, 0.456f, 0.406f};
    static constexpr float IMAGENET_STD[3] = {0.229f, 0.224f, 0.225f};

    // Model input size
    static constexpr int INPUT_WIDTH = 256;
    static constexpr int INPUT_HEIGHT = 256;
    static constexpr int INPUT_CHANNELS = 3;

    // Class prefixes for decision logic
    static const QString PREFIX_SLIDE;
    static const QString PREFIX_NOT_SLIDE;
    static const QString PREFIX_MAYBE_SLIDE;

    // Default threshold for classification decisions
    static constexpr float DEFAULT_THRESHOLD = 0.8f;

    // Initialization state
    bool m_initialized;
    QString m_errorMessage;
};

#endif // MLCLASSIFIER_H
