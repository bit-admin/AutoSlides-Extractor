#include "mlclassifier.h"
#include <QFile>
#include <QDebug>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifdef ONNX_AVAILABLE
#include <opencv2/opencv.hpp>
#include "imageiohelper.h"
#endif

// Define class prefixes for decision logic
// Note: No trailing underscore so both exact matches (e.g., "not_slide")
// and prefixed matches (e.g., "not_slide_desktop") will work
const QString MLClassifier::PREFIX_SLIDE = "slide";
const QString MLClassifier::PREFIX_NOT_SLIDE = "not_slide";
const QString MLClassifier::PREFIX_MAYBE_SLIDE = "may_be_slide";

MLClassifier::MLClassifier(const QString& modelPath, ExecutionProvider preferredProvider)
    : m_initialized(false)
#ifdef ONNX_AVAILABLE
    , m_memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
#endif
{
#ifdef ONNX_AVAILABLE
    m_initialized = initializeSession(modelPath, preferredProvider);
    if (!m_initialized) {
        qWarning() << "MLClassifier: Failed to initialize:" << m_errorMessage;
    }
#else
    Q_UNUSED(modelPath);
    Q_UNUSED(preferredProvider);
    m_errorMessage = "ONNX Runtime not available (compiled without ONNX support)";
    qWarning() << m_errorMessage;
#endif
}

MLClassifier::~MLClassifier() {
#ifdef ONNX_AVAILABLE
    // Unique pointers will automatically clean up
#endif
}

bool MLClassifier::isAvailable() {
#ifdef ONNX_AVAILABLE
    return true;
#else
    return false;
#endif
}

bool MLClassifier::isInitialized() const {
    return m_initialized;
}

QString MLClassifier::getErrorMessage() const {
    return m_errorMessage;
}

QString MLClassifier::getActiveExecutionProvider() const {
#ifdef ONNX_AVAILABLE
    return m_activeProvider;
#else
    return "Not Available";
#endif
}

ClassificationResult MLClassifier::classifySingle(const QString& imagePath) {
    ClassificationResult result;
    result.imagePath = imagePath;

#ifdef ONNX_AVAILABLE
    if (!m_initialized) {
        result.error = true;
        result.errorMessage = "Classifier not initialized: " + m_errorMessage;
        return result;
    }

    try {
        // Preprocess image
        std::vector<float> inputTensor;
        if (!preprocessImage(imagePath, inputTensor)) {
            result.error = true;
            result.errorMessage = "Failed to preprocess image";
            return result;
        }

        // Run inference
        std::vector<float> outputTensor;
        if (!runInference(inputTensor, outputTensor)) {
            result.error = true;
            result.errorMessage = "Failed to run inference";
            return result;
        }

        // Apply softmax to get probabilities
        std::vector<float> probabilities = softmax(outputTensor);

        // Find predicted class (highest probability)
        auto maxIt = std::max_element(probabilities.begin(), probabilities.end());
        int predictedIdx = std::distance(probabilities.begin(), maxIt);
        float confidence = *maxIt;

        // Fill result
        result.predictedClass = m_classNames[predictedIdx];
        result.confidence = confidence;

        // Fill all class probabilities
        for (int i = 0; i < m_classNames.size(); ++i) {
            result.classProbabilities[m_classNames[i]] = probabilities[i];
        }

        result.error = false;

    } catch (const std::exception& e) {
        result.error = true;
        result.errorMessage = QString("Exception during classification: %1").arg(e.what());
        qWarning() << "MLClassifier::classifySingle:" << result.errorMessage;
    }
#else
    result.error = true;
    result.errorMessage = "ONNX Runtime not available";
#endif

    return result;
}

QVector<ClassificationResult> MLClassifier::classifyBatch(const QStringList& imagePaths) {
    QVector<ClassificationResult> results;
    results.reserve(imagePaths.size());

    // Process images one by one
    // TODO: Implement true batch processing for better performance
    for (const QString& imagePath : imagePaths) {
        results.append(classifySingle(imagePath));
    }

    return results;
}

bool MLClassifier::shouldKeepImage(const ClassificationResult& result,
                                  const CategoryThresholds& notSlideThresholds,
                                  const CategoryThresholds& maybeSlideThresholds,
                                  float slideMaxThreshold,
                                  bool deleteMaybeSlides) {
    if (result.error) {
        // If classification failed, keep the image by default (conservative approach)
        return true;
    }

    const QString& predictedClass = result.predictedClass;
    float confidence = result.confidence;

    // Get slide probability for medium confidence zone check
    float slideProb = result.classProbabilities.value(PREFIX_SLIDE, 0.0f);

    // Helper lambda for 2-stage logic
    auto shouldDelete = [&](const CategoryThresholds& thresholds) -> bool {
        // Stage 1: High confidence - delete immediately
        if (confidence >= thresholds.highThreshold) {
            return true;
        }
        // Stage 2: Medium confidence - check slide probability
        if (confidence >= thresholds.lowThreshold && slideProb <= slideMaxThreshold) {
            return true;
        }
        // Low confidence or slide probability too high - keep
        return false;
    };

    // Check class prefix and apply corresponding logic
    if (predictedClass == PREFIX_SLIDE) {
        // "slide" class - always keep
        return true;
    } else if (predictedClass.startsWith(PREFIX_NOT_SLIDE)) {
        // "not_slide" classes - apply 2-stage logic
        return !shouldDelete(notSlideThresholds);
    } else if (predictedClass.startsWith(PREFIX_MAYBE_SLIDE)) {
        // "may_be_slide" classes - apply 2-stage logic if enabled
        if (deleteMaybeSlides) {
            return !shouldDelete(maybeSlideThresholds);
        }
        return true;  // Keep if deletion not enabled
    }

    // Unknown class - keep by default (conservative approach)
    return true;
}

QStringList MLClassifier::getClassPrefixes() {
    return {PREFIX_SLIDE, "not_slide", "may_be_slide"};
}

QMap<QString, float> MLClassifier::getDefaultThresholds() {
    QMap<QString, float> thresholds;
    thresholds["not_slide"] = DEFAULT_THRESHOLD;
    thresholds["may_be_slide"] = DEFAULT_THRESHOLD;
    return thresholds;
}

QStringList MLClassifier::getClassNames() const {
#ifdef ONNX_AVAILABLE
    return m_classNames;
#else
    return {};
#endif
}

QString MLClassifier::executionProviderToString(ExecutionProvider provider) {
    switch (provider) {
        case ExecutionProvider::Auto:     return "Auto";
        case ExecutionProvider::CoreML:   return "CoreML";
        case ExecutionProvider::CUDA:     return "CUDA";
        case ExecutionProvider::DirectML: return "DirectML";
        case ExecutionProvider::CPU:      return "CPU";
        default:                          return "Unknown";
    }
}

MLClassifier::ExecutionProvider MLClassifier::stringToExecutionProvider(const QString& providerStr) {
    if (providerStr == "CoreML")   return ExecutionProvider::CoreML;
    if (providerStr == "CUDA")     return ExecutionProvider::CUDA;
    if (providerStr == "DirectML") return ExecutionProvider::DirectML;
    if (providerStr == "CPU")      return ExecutionProvider::CPU;
    return ExecutionProvider::Auto;
}

#ifdef ONNX_AVAILABLE

bool MLClassifier::initializeSession(const QString& modelPath, ExecutionProvider preferredProvider) {
    try {
        // Create ONNX Runtime environment
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MLClassifier");

        // Create session options
        m_sessionOptions = std::make_unique<Ort::SessionOptions>();
        m_sessionOptions->SetIntraOpNumThreads(4);
        m_sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Get execution provider priority
        std::vector<std::string> providers = getExecutionProviderPriority(preferredProvider);

        // Try each execution provider in order
        bool providerAdded = false;
        for (const std::string& provider : providers) {
            try {
                if (provider == "CoreMLExecutionProvider") {
#ifdef __APPLE__
                    // Try to append CoreML provider with proper configuration
                    try {
                        std::unordered_map<std::string, std::string> coreml_options;
                        // Use ALL compute units (CPU + GPU + Neural Engine)
                        coreml_options["MLComputeUnits"] = "ALL";
                        // Use MLProgram format for better performance (requires macOS 12+)
                        coreml_options["ModelFormat"] = "MLProgram";
                        // Allow dynamic input shapes
                        coreml_options["RequireStaticInputShapes"] = "0";

                        m_sessionOptions->AppendExecutionProvider("CoreML", coreml_options);
                        m_activeProvider = "Core ML";
                        providerAdded = true;
                        qInfo() << "MLClassifier: Using Core ML execution provider (MLProgram format)";
                        break;
                    } catch (const std::exception& e) {
                        qDebug() << "MLClassifier: Core ML provider not available:" << e.what();
                        qDebug() << "  Trying next option...";
                    }
#endif
                } else if (provider == "CUDAExecutionProvider") {
                    try {
                        std::unordered_map<std::string, std::string> cuda_options;
                        cuda_options["device_id"] = "0";
                        cuda_options["gpu_mem_limit"] = "2147483648";  // 2GB limit
                        cuda_options["arena_extend_strategy"] = "kSameAsRequested";
                        m_sessionOptions->AppendExecutionProvider("CUDA", cuda_options);
                        m_activeProvider = "CUDA";
                        providerAdded = true;
                        qInfo() << "MLClassifier: Using CUDA execution provider";
                        break;
                    } catch (const std::exception& e) {
                        qDebug() << "MLClassifier: CUDA provider not available:" << e.what();
                        qDebug() << "  Trying next option...";
                    }
                } else if (provider == "DmlExecutionProvider") {
#ifdef _WIN32
                    try {
                        std::unordered_map<std::string, std::string> dml_options;
                        dml_options["device_id"] = "0";
                        m_sessionOptions->AppendExecutionProvider("DML", dml_options);
                        m_activeProvider = "DirectML";
                        providerAdded = true;
                        qInfo() << "MLClassifier: Using DirectML execution provider";
                        break;
                    } catch (const std::exception& e) {
                        qDebug() << "MLClassifier: DirectML provider not available:" << e.what();
                        qDebug() << "  Trying next option...";
                    }
#endif
                }
            } catch (const std::exception& e) {
                qDebug() << "MLClassifier: Failed to add" << QString::fromStdString(provider)
                        << ":" << e.what();
                // Continue to next provider
            }
        }

        // Fallback to CPU if no hardware accelerator available
        if (!providerAdded) {
            m_activeProvider = "CPU";
            qInfo() << "MLClassifier: Using CPU execution provider (fallback)";
        }

        // Handle Qt resource paths
        QString actualModelPath = modelPath;
        QByteArray modelData;
        if (modelPath.startsWith(":/") || modelPath.startsWith("qrc:")) {
            // Load from Qt resources into memory
            QFile modelFile(modelPath);
            if (!modelFile.open(QIODevice::ReadOnly)) {
                m_errorMessage = QString("Failed to open model file from resources: %1").arg(modelPath);
                return false;
            }
            modelData = modelFile.readAll();
            modelFile.close();

            // Create session from memory
            m_session = std::make_unique<Ort::Session>(*m_env,
                                                       modelData.constData(),
                                                       modelData.size(),
                                                       *m_sessionOptions);
        } else {
            // Load from file system
#ifdef _WIN32
            std::wstring wModelPath = modelPath.toStdWString();
            m_session = std::make_unique<Ort::Session>(*m_env, wModelPath.c_str(), *m_sessionOptions);
#else
            std::string stdModelPath = modelPath.toStdString();
            m_session = std::make_unique<Ort::Session>(*m_env, stdModelPath.c_str(), *m_sessionOptions);
#endif
        }

        // Get input/output names and shapes
        Ort::AllocatorWithDefaultOptions allocator;

        // Input info
        size_t numInputNodes = m_session->GetInputCount();
        if (numInputNodes != 1) {
            m_errorMessage = QString("Expected 1 input node, got %1").arg(numInputNodes);
            return false;
        }

        Ort::AllocatedStringPtr inputNameAllocated = m_session->GetInputNameAllocated(0, allocator);
        m_inputName = std::string(inputNameAllocated.get());
        m_inputNames.clear();
        m_inputNames.push_back(m_inputName.c_str());

        Ort::TypeInfo inputTypeInfo = m_session->GetInputTypeInfo(0);
        auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        m_inputShape = inputTensorInfo.GetShape();

        // Output info
        size_t numOutputNodes = m_session->GetOutputCount();
        if (numOutputNodes != 1) {
            m_errorMessage = QString("Expected 1 output node, got %1").arg(numOutputNodes);
            return false;
        }

        Ort::AllocatedStringPtr outputNameAllocated = m_session->GetOutputNameAllocated(0, allocator);
        m_outputName = std::string(outputNameAllocated.get());
        m_outputNames.clear();
        m_outputNames.push_back(m_outputName.c_str());

        Ort::TypeInfo outputTypeInfo = m_session->GetOutputTypeInfo(0);
        auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
        m_outputShape = outputTensorInfo.GetShape();

        // Verify shapes
        if (m_inputShape.size() != 4 || m_inputShape[1] != 3 ||
            m_inputShape[2] != INPUT_HEIGHT || m_inputShape[3] != INPUT_WIDTH) {
            m_errorMessage = QString("Unexpected input shape");
            return false;
        }

        // Read class names from model metadata
        m_classNames.clear();
        bool metadataFound = false;

        try {
            Ort::ModelMetadata metadata = m_session->GetModelMetadata();

            // Get all custom metadata keys
            std::vector<Ort::AllocatedStringPtr> keys = metadata.GetCustomMetadataMapKeysAllocated(allocator);

            for (size_t i = 0; i < keys.size(); i++) {
                std::string key = keys[i].get();

                if (key == "class_names") {
                    Ort::AllocatedStringPtr valuePtr = metadata.LookupCustomMetadataMapAllocated(key.c_str(), allocator);
                    if (valuePtr) {
                        QString jsonStr = QString::fromStdString(valuePtr.get());
                        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());

                        if (doc.isArray()) {
                            QJsonArray arr = doc.array();
                            for (const QJsonValue& val : arr) {
                                if (val.isString()) {
                                    m_classNames.append(val.toString());
                                }
                            }
                            metadataFound = true;
                            qInfo() << "MLClassifier: Loaded" << m_classNames.size() << "class names from model metadata";
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            qWarning() << "MLClassifier: Failed to read model metadata:" << e.what();
        }

        // Error if no class_names metadata found
        if (!metadataFound || m_classNames.isEmpty()) {
            m_errorMessage = QString("Model does not contain class_names metadata. Please re-export the model with embedded metadata using convert_to_onnx.py");
            return false;
        }

        // Validate output shape matches class count
        if (m_outputShape.size() != 2 || m_outputShape[1] != m_classNames.size()) {
            m_errorMessage = QString("Output shape mismatch: model has %1 outputs but metadata specifies %2 classes")
                .arg(m_outputShape[1])
                .arg(m_classNames.size());
            return false;
        }

        qInfo() << "MLClassifier: Model loaded successfully";
        qInfo() << "  Input shape:" << m_inputShape[0] << "x" << m_inputShape[1]
                << "x" << m_inputShape[2] << "x" << m_inputShape[3];
        qInfo() << "  Output shape:" << m_outputShape[0] << "x" << m_outputShape[1];
        qInfo() << "  Classes:" << m_classNames;
        qInfo() << "  Execution provider:" << m_activeProvider;

        return true;

    } catch (const std::exception& e) {
        m_errorMessage = QString("Failed to initialize ONNX session: %1").arg(e.what());
        return false;
    }
}

std::vector<std::string> MLClassifier::getExecutionProviderPriority(ExecutionProvider preferredProvider) {
    std::vector<std::string> providers;

    if (preferredProvider == ExecutionProvider::Auto) {
        // Platform-specific automatic selection
#ifdef __APPLE__
        providers.push_back("CoreMLExecutionProvider");
#elif defined(_WIN32)
        providers.push_back("CUDAExecutionProvider");
        providers.push_back("DmlExecutionProvider");
#else // Linux
        providers.push_back("CUDAExecutionProvider");
#endif
    } else {
        // User-specified provider
        switch (preferredProvider) {
            case ExecutionProvider::CoreML:
                providers.push_back("CoreMLExecutionProvider");
                break;
            case ExecutionProvider::CUDA:
                providers.push_back("CUDAExecutionProvider");
                break;
            case ExecutionProvider::DirectML:
                providers.push_back("DmlExecutionProvider");
                break;
            case ExecutionProvider::CPU:
                // CPU is always available as fallback
                break;
            default:
                break;
        }
    }

    return providers;
}

bool MLClassifier::preprocessImage(const QString& imagePath, std::vector<float>& inputTensor) {
    try {
        // Load image using OpenCV with Unicode support
        cv::Mat image = ImageIOHelper::imreadUnicode(imagePath);
        if (image.empty()) {
            qWarning() << "MLClassifier: Failed to load image:" << imagePath;
            return false;
        }

        // Convert BGR to RGB
        cv::Mat imageRGB;
        cv::cvtColor(image, imageRGB, cv::COLOR_BGR2RGB);

        // Resize to 256x256 using INTER_AREA interpolation
        // INTER_AREA is the correct method for downsampling to match PIL's LANCZOS and Sharp's lanczos3
        // Testing confirmed: INTER_AREA produces RGB(141,55,43) vs Sharp's RGB(140,55,43) - nearly identical!
        // Other methods produce significantly different results (CUBIC: 168,59,48 / LANCZOS4: 168,62,50)
        cv::Mat imageResized;
        cv::resize(imageRGB, imageResized, cv::Size(INPUT_WIDTH, INPUT_HEIGHT), 0, 0, cv::INTER_AREA);

        // Convert to float and normalize to [0, 1]
        cv::Mat imageFloat;
        imageResized.convertTo(imageFloat, CV_32F, 1.0 / 255.0);

        // Allocate tensor (NCHW format: 1 x 3 x 256 x 256)
        inputTensor.resize(1 * INPUT_CHANNELS * INPUT_HEIGHT * INPUT_WIDTH);

        // Convert HWC to CHW and copy to tensor
        for (int c = 0; c < INPUT_CHANNELS; ++c) {
            for (int h = 0; h < INPUT_HEIGHT; ++h) {
                for (int w = 0; w < INPUT_WIDTH; ++w) {
                    int tensorIdx = c * INPUT_HEIGHT * INPUT_WIDTH + h * INPUT_WIDTH + w;
                    inputTensor[tensorIdx] = imageFloat.at<cv::Vec3f>(h, w)[c];
                }
            }
        }

        // Apply ImageNet normalization
        normalizeImageNet(inputTensor);

        return true;

    } catch (const std::exception& e) {
        qWarning() << "MLClassifier: Exception in preprocessImage:" << e.what();
        return false;
    }
}

void MLClassifier::normalizeImageNet(std::vector<float>& tensor) {
    int pixelsPerChannel = INPUT_HEIGHT * INPUT_WIDTH;

    for (int c = 0; c < INPUT_CHANNELS; ++c) {
        float mean = IMAGENET_MEAN[c];
        float std = IMAGENET_STD[c];

        for (int i = 0; i < pixelsPerChannel; ++i) {
            int idx = c * pixelsPerChannel + i;
            tensor[idx] = (tensor[idx] - mean) / std;
        }
    }
}

bool MLClassifier::runInference(const std::vector<float>& inputTensor,
                               std::vector<float>& outputTensor) {
    try {
        // Create input tensor
        std::vector<int64_t> inputShape = {1, INPUT_CHANNELS, INPUT_HEIGHT, INPUT_WIDTH};
        Ort::Value inputOrtTensor = Ort::Value::CreateTensor<float>(
            m_memoryInfo,
            const_cast<float*>(inputTensor.data()),
            inputTensor.size(),
            inputShape.data(),
            inputShape.size()
        );

        // Run inference
        std::vector<Ort::Value> outputTensors = m_session->Run(
            Ort::RunOptions{nullptr},
            m_inputNames.data(),
            &inputOrtTensor,
            1,
            m_outputNames.data(),
            1
        );

        // Extract output
        float* outputData = outputTensors[0].GetTensorMutableData<float>();
        size_t outputSize = m_classNames.size();
        outputTensor.assign(outputData, outputData + outputSize);

        return true;

    } catch (const std::exception& e) {
        qWarning() << "MLClassifier: Exception in runInference:" << e.what();
        return false;
    }
}

std::vector<float> MLClassifier::softmax(const std::vector<float>& logits) {
    std::vector<float> probabilities(logits.size());

    // Find max for numerical stability
    float maxLogit = *std::max_element(logits.begin(), logits.end());

    // Compute exp(x - max) and sum
    float sum = 0.0f;
    for (size_t i = 0; i < logits.size(); ++i) {
        probabilities[i] = std::exp(logits[i] - maxLogit);
        sum += probabilities[i];
    }

    // Normalize
    for (size_t i = 0; i < probabilities.size(); ++i) {
        probabilities[i] /= sum;
    }

    return probabilities;
}

#endif // ONNX_AVAILABLE
