#include "configmanager.h"
#include "postprocessor.h"
#include <QDir>
#include <QCoreApplication>

// Configuration keys
const QString ConfigManager::KEY_OUTPUT_DIR = "outputDirectory";
const QString ConfigManager::KEY_FRAME_INTERVAL = "frameInterval";
const QString ConfigManager::KEY_SSIM_PRESET = "ssimPreset";
const QString ConfigManager::KEY_CUSTOM_SSIM_THRESHOLD = "customSSIMThreshold";
const QString ConfigManager::KEY_ENABLE_VERIFICATION = "enableVerification";
const QString ConfigManager::KEY_VERIFICATION_COUNT = "verificationCount";
const QString ConfigManager::KEY_ENABLE_DOWNSAMPLING = "enableDownsampling";
const QString ConfigManager::KEY_DOWNSAMPLE_WIDTH = "downsampleWidth";
const QString ConfigManager::KEY_DOWNSAMPLE_HEIGHT = "downsampleHeight";
const QString ConfigManager::KEY_CHUNK_SIZE = "chunkSize";
const QString ConfigManager::KEY_JPEG_QUALITY = "jpegQuality";
const QString ConfigManager::KEY_ENABLE_POST_PROCESSING = "enablePostProcessing";
const QString ConfigManager::KEY_DELETE_REDUNDANT = "deleteRedundant";
const QString ConfigManager::KEY_COMPARE_EXCLUDED = "compareExcluded";
const QString ConfigManager::KEY_HAMMING_THRESHOLD = "hammingThreshold";
const QString ConfigManager::KEY_EXCLUSION_LIST_SIZE = "exclusionListSize";
const QString ConfigManager::KEY_EXCLUSION_REMARK = "exclusionRemark";
const QString ConfigManager::KEY_EXCLUSION_HASH = "exclusionHash";
const QString ConfigManager::KEY_ENABLE_ML_CLASSIFICATION = "enableMLClassification";
const QString ConfigManager::KEY_ML_DELETE_MAYBE_SLIDES = "mlDeleteMaybeSlides";
const QString ConfigManager::KEY_ML_MODEL_PATH = "mlModelPath";
const QString ConfigManager::KEY_ML_EXECUTION_PROVIDER = "mlExecutionProvider";
const QString ConfigManager::KEY_ML_NOT_SLIDE_HIGH_THRESHOLD = "mlNotSlideHighThreshold";
const QString ConfigManager::KEY_ML_NOT_SLIDE_LOW_THRESHOLD = "mlNotSlideLowThreshold";
const QString ConfigManager::KEY_ML_MAYBE_SLIDE_HIGH_THRESHOLD = "mlMaybeSlideHighThreshold";
const QString ConfigManager::KEY_ML_MAYBE_SLIDE_LOW_THRESHOLD = "mlMaybeSlideLowThreshold";
const QString ConfigManager::KEY_ML_SLIDE_MAX_THRESHOLD = "mlSlideMaxThreshold";

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    // On macOS, this will use com.autoslidesextractor.AutoSlidesExtractor.plist
    m_settings = new QSettings("AutoSlidesExtractor", "AutoSlidesExtractor", this);
}

AppConfig ConfigManager::loadConfig()
{
    AppConfig config;

    config.outputDirectory = m_settings->value(KEY_OUTPUT_DIR, config.outputDirectory).toString();
    config.frameInterval = m_settings->value(KEY_FRAME_INTERVAL, config.frameInterval).toDouble();

    QString presetName = m_settings->value(KEY_SSIM_PRESET, "Normal").toString();
    config.ssimPreset = getPresetFromName(presetName);

    config.customSSIMThreshold = m_settings->value(KEY_CUSTOM_SSIM_THRESHOLD, config.customSSIMThreshold).toDouble();
    // Verification settings are now hardcoded (enableVerification=true, verificationCount=3)
    config.enableVerification = true;
    config.verificationCount = 3;
    config.enableDownsampling = m_settings->value(KEY_ENABLE_DOWNSAMPLING, config.enableDownsampling).toBool();
    config.downsampleWidth = m_settings->value(KEY_DOWNSAMPLE_WIDTH, config.downsampleWidth).toInt();
    config.downsampleHeight = m_settings->value(KEY_DOWNSAMPLE_HEIGHT, config.downsampleHeight).toInt();
    config.chunkSize = m_settings->value(KEY_CHUNK_SIZE, config.chunkSize).toInt();
    config.jpegQuality = m_settings->value(KEY_JPEG_QUALITY, config.jpegQuality).toInt();

    // Load post-processing settings
    config.enablePostProcessing = m_settings->value(KEY_ENABLE_POST_PROCESSING, config.enablePostProcessing).toBool();
    config.deleteRedundant = m_settings->value(KEY_DELETE_REDUNDANT, config.deleteRedundant).toBool();
    config.compareExcluded = m_settings->value(KEY_COMPARE_EXCLUDED, config.compareExcluded).toBool();
    config.hammingThreshold = m_settings->value(KEY_HAMMING_THRESHOLD, config.hammingThreshold).toInt();

    // Load ML classification settings
    config.enableMLClassification = m_settings->value(KEY_ENABLE_ML_CLASSIFICATION, config.enableMLClassification).toBool();
    config.mlDeleteMaybeSlides = m_settings->value(KEY_ML_DELETE_MAYBE_SLIDES, config.mlDeleteMaybeSlides).toBool();
    config.mlModelPath = m_settings->value(KEY_ML_MODEL_PATH, config.mlModelPath).toString();
    config.mlExecutionProvider = m_settings->value(KEY_ML_EXECUTION_PROVIDER, config.mlExecutionProvider).toString();
    config.mlNotSlideHighThreshold = m_settings->value(KEY_ML_NOT_SLIDE_HIGH_THRESHOLD, config.mlNotSlideHighThreshold).toFloat();
    config.mlNotSlideLowThreshold = m_settings->value(KEY_ML_NOT_SLIDE_LOW_THRESHOLD, config.mlNotSlideLowThreshold).toFloat();
    config.mlMaybeSlideHighThreshold = m_settings->value(KEY_ML_MAYBE_SLIDE_HIGH_THRESHOLD, config.mlMaybeSlideHighThreshold).toFloat();
    config.mlMaybeSlideLowThreshold = m_settings->value(KEY_ML_MAYBE_SLIDE_LOW_THRESHOLD, config.mlMaybeSlideLowThreshold).toFloat();
    config.mlSlideMaxThreshold = m_settings->value(KEY_ML_SLIDE_MAX_THRESHOLD, config.mlSlideMaxThreshold).toFloat();

    // Application trash settings are hardcoded:
    // - Always use application trash
    // - Always move to system trash when emptying
    // - Never auto-delete trash (retention = 0)

    return config;
}

void ConfigManager::saveConfig(const AppConfig& config)
{
    m_settings->setValue(KEY_OUTPUT_DIR, config.outputDirectory);
    m_settings->setValue(KEY_FRAME_INTERVAL, config.frameInterval);
    m_settings->setValue(KEY_SSIM_PRESET, getPresetName(config.ssimPreset));
    m_settings->setValue(KEY_CUSTOM_SSIM_THRESHOLD, config.customSSIMThreshold);
    // Verification settings are now hardcoded, no need to save them
    m_settings->setValue(KEY_ENABLE_DOWNSAMPLING, config.enableDownsampling);
    m_settings->setValue(KEY_DOWNSAMPLE_WIDTH, config.downsampleWidth);
    m_settings->setValue(KEY_DOWNSAMPLE_HEIGHT, config.downsampleHeight);
    m_settings->setValue(KEY_CHUNK_SIZE, config.chunkSize);
    m_settings->setValue(KEY_JPEG_QUALITY, config.jpegQuality);

    // Save post-processing settings
    m_settings->setValue(KEY_ENABLE_POST_PROCESSING, config.enablePostProcessing);
    m_settings->setValue(KEY_DELETE_REDUNDANT, config.deleteRedundant);
    m_settings->setValue(KEY_COMPARE_EXCLUDED, config.compareExcluded);
    m_settings->setValue(KEY_HAMMING_THRESHOLD, config.hammingThreshold);

    // Save ML classification settings
    m_settings->setValue(KEY_ENABLE_ML_CLASSIFICATION, config.enableMLClassification);
    m_settings->setValue(KEY_ML_DELETE_MAYBE_SLIDES, config.mlDeleteMaybeSlides);
    m_settings->setValue(KEY_ML_MODEL_PATH, config.mlModelPath);
    m_settings->setValue(KEY_ML_EXECUTION_PROVIDER, config.mlExecutionProvider);
    m_settings->setValue(KEY_ML_NOT_SLIDE_HIGH_THRESHOLD, config.mlNotSlideHighThreshold);
    m_settings->setValue(KEY_ML_NOT_SLIDE_LOW_THRESHOLD, config.mlNotSlideLowThreshold);
    m_settings->setValue(KEY_ML_MAYBE_SLIDE_HIGH_THRESHOLD, config.mlMaybeSlideHighThreshold);
    m_settings->setValue(KEY_ML_MAYBE_SLIDE_LOW_THRESHOLD, config.mlMaybeSlideLowThreshold);
    m_settings->setValue(KEY_ML_SLIDE_MAX_THRESHOLD, config.mlSlideMaxThreshold);

    // Application trash settings are hardcoded, no need to save them

    m_settings->sync();
}

double ConfigManager::getSSIMThreshold(SSIMPreset preset, double customValue)
{
    switch (preset) {
        case SSIMPreset::Strict:
            return 0.999;
        case SSIMPreset::Normal:
            return 0.9985;
        case SSIMPreset::Loose:
            return 0.998;
        case SSIMPreset::Custom:
            return customValue;
        default:
            return 0.9985;
    }
}

QString ConfigManager::getPresetName(SSIMPreset preset)
{
    switch (preset) {
        case SSIMPreset::Strict:
            return "Strict";
        case SSIMPreset::Normal:
            return "Normal";
        case SSIMPreset::Loose:
            return "Loose";
        case SSIMPreset::Custom:
            return "Custom";
        default:
            return "Normal";
    }
}

SSIMPreset ConfigManager::getPresetFromName(const QString& name)
{
    if (name == "Strict") {
        return SSIMPreset::Strict;
    } else if (name == "Normal") {
        return SSIMPreset::Normal;
    } else if (name == "Loose") {
        return SSIMPreset::Loose;
    } else if (name == "Custom") {
        return SSIMPreset::Custom;
    } else {
        return SSIMPreset::Normal;
    }
}

QList<ExclusionEntry> ConfigManager::loadExclusionList()
{
    QList<ExclusionEntry> list;

    // Check if exclusion list has ever been saved
    if (!m_settings->contains(KEY_EXCLUSION_LIST_SIZE)) {
        // First run - return default list
        return PostProcessor::getDefaultExclusionList();
    }

    int size = m_settings->value(KEY_EXCLUSION_LIST_SIZE, 0).toInt();

    // If size is 0, user has explicitly cleared the list - return empty
    if (size == 0) {
        return list;
    }

    for (int i = 0; i < size; i++) {
        QString remarkKey = QString("%1_%2").arg(KEY_EXCLUSION_REMARK).arg(i);
        QString hashKey = QString("%1_%2").arg(KEY_EXCLUSION_HASH).arg(i);

        QString remark = m_settings->value(remarkKey).toString();
        QString hash = m_settings->value(hashKey).toString();

        if (!hash.isEmpty()) {
            list.append(ExclusionEntry(remark, hash));
        }
    }

    return list;
}

void ConfigManager::saveExclusionList(const QList<ExclusionEntry>& exclusionList)
{
    // Clear existing list
    int oldSize = m_settings->value(KEY_EXCLUSION_LIST_SIZE, 0).toInt();
    for (int i = 0; i < oldSize; i++) {
        QString remarkKey = QString("%1_%2").arg(KEY_EXCLUSION_REMARK).arg(i);
        QString hashKey = QString("%1_%2").arg(KEY_EXCLUSION_HASH).arg(i);
        m_settings->remove(remarkKey);
        m_settings->remove(hashKey);
    }

    // Save new list
    m_settings->setValue(KEY_EXCLUSION_LIST_SIZE, exclusionList.size());

    for (int i = 0; i < exclusionList.size(); i++) {
        QString remarkKey = QString("%1_%2").arg(KEY_EXCLUSION_REMARK).arg(i);
        QString hashKey = QString("%1_%2").arg(KEY_EXCLUSION_HASH).arg(i);

        m_settings->setValue(remarkKey, exclusionList[i].remark);
        m_settings->setValue(hashKey, exclusionList[i].hashHex);
    }

    m_settings->sync();
}