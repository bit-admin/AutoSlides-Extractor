#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QDir>
#include <QList>

enum class SSIMPreset {
    Strict,
    Normal,
    Loose,
    Custom
};

enum class AutoCropMode {
    CannyThenYolo,
    CannyOnly,
    YoloOnly
};

struct AutoCropConfig {
    AutoCropMode mode = AutoCropMode::CannyThenYolo;

    // Canny pipeline parameters
    float aspectTolerance = 0.05f;     // tolerance against {16/9, 4/3}
    float blackThreshold = 20.0f;      // 0-255: pixel value below this counts as "black border"
    float maxBorderFrac = 0.10f;       // max fraction of width/height to strip as black border
    int   cannyLowThreshold = 20;
    int   cannyHighThreshold = 60;
    float areaRatioMin = 0.08f;        // min contour-bounding-box area ratio against inner area
    float areaRatioMax = 0.95f;
    float marginFrac = 0.02f;          // require non-zero top OR bottom margin >= this fraction
    float fillRatioMin = 0.85f;        // contour-area / bounding-box-area lower bound

    // YOLO pipeline parameters
    float yoloConfidenceThreshold = 0.25f;
    float yoloIouThreshold = 0.45f;
    int   yoloInputSize = 640;
    QString yoloModelPath = ":/models/resources/models/slide_detector_yolov8_v1.onnx";
};

// Forward declaration
struct ExclusionEntry;

struct AppConfig {
    QString outputDirectory;
    double frameInterval;
    SSIMPreset ssimPreset;
    double customSSIMThreshold;
    bool enableVerification;
    int verificationCount;
    bool enableDownsampling;
    int downsampleWidth;
    int downsampleHeight;
    int chunkSize;

    // Output settings
    int jpegQuality;

    // Post-processing settings
    bool enablePostProcessing;
    bool deleteRedundant;
    bool compareExcluded;
    int hammingThreshold;

    // ML Classification settings
    bool enableMLClassification;
    bool mlDeleteMaybeSlides;  // true = delete may_be_slide images (default: true)
    QString mlModelPath;
    QString mlExecutionProvider;

    // 2-stage classification thresholds for not_slide
    float mlNotSlideHighThreshold;   // High confidence: delete if >= this (default: 0.9)
    float mlNotSlideLowThreshold;    // Low confidence boundary (default: 0.75)

    // 2-stage classification thresholds for may_be_slide
    float mlMaybeSlideHighThreshold; // High confidence: delete if >= this (default: 0.9)
    float mlMaybeSlideLowThreshold;  // Low confidence boundary (default: 0.75)

    // Shared threshold for slide class in medium confidence zone
    float mlSlideMaxThreshold;       // Delete if slide probability <= this (default: 0.25)

    // Auto-crop settings (Slides Review viewer auto crop)
    AutoCropConfig autoCrop;

    // Default values
    AppConfig() :
        outputDirectory(QDir::homePath() + "/Downloads/SlidesExtractor"),
        frameInterval(2.0),
        ssimPreset(SSIMPreset::Normal),
        customSSIMThreshold(0.9985),
        enableVerification(true),
        verificationCount(3),
        enableDownsampling(true),
        downsampleWidth(480),
        downsampleHeight(270),
        chunkSize(100),
        jpegQuality(95),
        enablePostProcessing(true),
        deleteRedundant(true),
        compareExcluded(true),
        hammingThreshold(10),
        enableMLClassification(true),
        mlDeleteMaybeSlides(true),  // Default: delete may_be_slide images
        mlModelPath(":/models/resources/models/slide_classifier_mobilenetv4_v1.onnx"),
        mlExecutionProvider("Auto"),
        mlNotSlideHighThreshold(0.9f),   // High confidence threshold
        mlNotSlideLowThreshold(0.75f),   // Low confidence boundary
        mlMaybeSlideHighThreshold(0.9f), // High confidence threshold
        mlMaybeSlideLowThreshold(0.75f), // Low confidence boundary
        mlSlideMaxThreshold(0.25f)       // Slide max for medium confidence zone
    {
    }
};

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);

    /**
     * Load configuration from persistent storage
     * @return AppConfig structure with loaded settings
     */
    AppConfig loadConfig();

    /**
     * Save configuration to persistent storage
     * @param config Configuration to save
     */
    void saveConfig(const AppConfig& config);

    /**
     * Get SSIM threshold value based on preset
     * @param preset SSIM preset type
     * @param customValue Custom threshold value (used when preset is Custom)
     * @return SSIM threshold value
     */
    static double getSSIMThreshold(SSIMPreset preset, double customValue = 0.9985);

    /**
     * Get preset name as string
     * @param preset SSIM preset type
     * @return Preset name
     */
    static QString getPresetName(SSIMPreset preset);

    /**
     * Get preset from string name
     * @param name Preset name
     * @return SSIM preset type
     */
    static SSIMPreset getPresetFromName(const QString& name);

    /**
     * Load exclusion list from settings
     * @return List of exclusion entries
     */
    QList<ExclusionEntry> loadExclusionList();

    /**
     * Save exclusion list to settings
     * @param exclusionList List of exclusion entries to save
     */
    void saveExclusionList(const QList<ExclusionEntry>& exclusionList);

private:
    QSettings* m_settings;

    // Configuration keys
    static const QString KEY_OUTPUT_DIR;
    static const QString KEY_FRAME_INTERVAL;
    static const QString KEY_SSIM_PRESET;
    static const QString KEY_CUSTOM_SSIM_THRESHOLD;
    static const QString KEY_ENABLE_VERIFICATION;
    static const QString KEY_VERIFICATION_COUNT;
    static const QString KEY_ENABLE_DOWNSAMPLING;
    static const QString KEY_DOWNSAMPLE_WIDTH;
    static const QString KEY_DOWNSAMPLE_HEIGHT;
    static const QString KEY_CHUNK_SIZE;
    static const QString KEY_JPEG_QUALITY;
    static const QString KEY_ENABLE_POST_PROCESSING;
    static const QString KEY_DELETE_REDUNDANT;
    static const QString KEY_COMPARE_EXCLUDED;
    static const QString KEY_HAMMING_THRESHOLD;
    static const QString KEY_EXCLUSION_LIST_SIZE;
    static const QString KEY_EXCLUSION_REMARK;
    static const QString KEY_EXCLUSION_HASH;
    static const QString KEY_ENABLE_ML_CLASSIFICATION;
    static const QString KEY_ML_DELETE_MAYBE_SLIDES;
    static const QString KEY_ML_MODEL_PATH;
    static const QString KEY_ML_EXECUTION_PROVIDER;
    static const QString KEY_ML_NOT_SLIDE_HIGH_THRESHOLD;
    static const QString KEY_ML_NOT_SLIDE_LOW_THRESHOLD;
    static const QString KEY_ML_MAYBE_SLIDE_HIGH_THRESHOLD;
    static const QString KEY_ML_MAYBE_SLIDE_LOW_THRESHOLD;
    static const QString KEY_ML_SLIDE_MAX_THRESHOLD;

    // Auto-crop keys (only the user-configurable parameters; the rest stay
    // at compiled-in defaults from AutoCropConfig{}).
    static const QString KEY_AUTOCROP_MODE;
    static const QString KEY_AUTOCROP_ASPECT_TOLERANCE;
    static const QString KEY_AUTOCROP_YOLO_CONFIDENCE;
    static const QString KEY_AUTOCROP_YOLO_MODEL_PATH;
};

#endif // CONFIGMANAGER_H