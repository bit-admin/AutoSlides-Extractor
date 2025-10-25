#include "configmanager.h"
#include <QDir>

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

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
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