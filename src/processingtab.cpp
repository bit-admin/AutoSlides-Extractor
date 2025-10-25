#include "processingtab.h"
#include "configmanager.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>

ProcessingTab::ProcessingTab(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(this);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === SSIM THRESHOLD SETTINGS ===
    QGroupBox* ssimGroup = new QGroupBox("SSIM Threshold", this);
    QGridLayout* ssimLayout = new QGridLayout(ssimGroup);
    ssimLayout->setContentsMargins(12, 12, 12, 12);
    ssimLayout->setSpacing(8);

    QLabel* ssimPresetLabel = new QLabel("Preset:", this);
    m_ssimPresetCombo = new QComboBox(this);
    m_ssimPresetCombo->addItem("Strict (0.999)", static_cast<int>(SSIMPreset::Strict));
    m_ssimPresetCombo->addItem("Normal (0.9985)", static_cast<int>(SSIMPreset::Normal));
    m_ssimPresetCombo->addItem("Loose (0.998)", static_cast<int>(SSIMPreset::Loose));
    m_ssimPresetCombo->addItem("Custom", static_cast<int>(SSIMPreset::Custom));

    QLabel* customSSIMLabel = new QLabel("Custom Value:", this);
    m_customSSIMSpinBox = new QDoubleSpinBox(this);
    m_customSSIMSpinBox->setRange(0.900, 0.9999);
    m_customSSIMSpinBox->setDecimals(4);
    m_customSSIMSpinBox->setSingleStep(0.0001);
    m_customSSIMSpinBox->setEnabled(false);

    QLabel* ssimHelpLabel = new QLabel("Higher global structural similarity threshold indicate stricter matching. Note that a minor change of 0.001 can significantly impact performance.", this);
    ssimHelpLabel->setWordWrap(true);
    ssimHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    ssimLayout->addWidget(ssimPresetLabel, 0, 0);
    ssimLayout->addWidget(m_ssimPresetCombo, 0, 1);
    ssimLayout->addWidget(customSSIMLabel, 1, 0);
    ssimLayout->addWidget(m_customSSIMSpinBox, 1, 1);
    ssimLayout->addWidget(ssimHelpLabel, 2, 0, 1, 2);

    tabLayout->addWidget(ssimGroup);

    // === DOWNSAMPLING SETTINGS ===
    QGroupBox* downsamplingGroup = new QGroupBox("Downsampling", this);
    QGridLayout* downsamplingLayout = new QGridLayout(downsamplingGroup);
    downsamplingLayout->setContentsMargins(12, 12, 12, 12);
    downsamplingLayout->setSpacing(8);

    m_enableDownsamplingCheckBox = new QCheckBox("Enable Downsampling", this);

    QLabel* presetLabel = new QLabel("Preset:", this);
    m_downsamplePresetCombo = new QComboBox(this);
    m_downsamplePresetCombo->addItem("480p");
    m_downsamplePresetCombo->addItem("360p");
    m_downsamplePresetCombo->addItem("270p");
    m_downsamplePresetCombo->addItem("Custom");

    QLabel* widthLabel = new QLabel("Width:", this);
    m_downsampleWidthSpinBox = new QSpinBox(this);
    m_downsampleWidthSpinBox->setRange(160, 1920);
    m_downsampleWidthSpinBox->setSingleStep(10);

    QLabel* heightLabel = new QLabel("Height:", this);
    m_downsampleHeightSpinBox = new QSpinBox(this);
    m_downsampleHeightSpinBox->setRange(90, 1080);
    m_downsampleHeightSpinBox->setSingleStep(10);

    QLabel* downsamplingHelpLabel = new QLabel("Downsampling with anti-aliasing is performed to mitigate artifacts when calculating SSIM, thereby improving image detection accuracy.", this);
    downsamplingHelpLabel->setWordWrap(true);
    downsamplingHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    downsamplingLayout->addWidget(m_enableDownsamplingCheckBox, 0, 0, 1, 2);
    downsamplingLayout->addWidget(presetLabel, 1, 0);
    downsamplingLayout->addWidget(m_downsamplePresetCombo, 1, 1);
    downsamplingLayout->addWidget(widthLabel, 2, 0);
    downsamplingLayout->addWidget(m_downsampleWidthSpinBox, 2, 1);
    downsamplingLayout->addWidget(heightLabel, 3, 0);
    downsamplingLayout->addWidget(m_downsampleHeightSpinBox, 3, 1);
    downsamplingLayout->addWidget(downsamplingHelpLabel, 4, 0, 1, 2);

    tabLayout->addWidget(downsamplingGroup);

    // === CHUNK SIZE SETTINGS ===
    QGroupBox* chunkGroup = new QGroupBox("Memory Optimization", this);
    QGridLayout* chunkLayout = new QGridLayout(chunkGroup);
    chunkLayout->setContentsMargins(12, 12, 12, 12);
    chunkLayout->setSpacing(8);

    QLabel* chunkSizeLabel = new QLabel("Chunk Size:", this);
    m_chunkSizeSpinBox = new QSpinBox(this);
    m_chunkSizeSpinBox->setRange(100, 2000);
    m_chunkSizeSpinBox->setSingleStep(50);
    m_chunkSizeSpinBox->setSuffix(" frames");

    QLabel* chunkHelpLabel = new QLabel("Number of frames processed at once. Smaller values use less memory but may be slower. Larger values are faster but use more memory.", this);
    chunkHelpLabel->setWordWrap(true);
    chunkHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    chunkLayout->addWidget(chunkSizeLabel, 0, 0);
    chunkLayout->addWidget(m_chunkSizeSpinBox, 0, 1);
    chunkLayout->addWidget(chunkHelpLabel, 1, 0, 1, 2);

    tabLayout->addWidget(chunkGroup);

    // === OUTPUT SETTINGS ===
    QGroupBox* outputGroup = new QGroupBox("Output Settings", this);
    QGridLayout* outputLayout = new QGridLayout(outputGroup);
    outputLayout->setContentsMargins(12, 12, 12, 12);
    outputLayout->setSpacing(8);

    QLabel* jpegQualityLabel = new QLabel("JPEG Quality:", this);
    m_jpegQualitySpinBox = new QSpinBox(this);
    m_jpegQualitySpinBox->setRange(1, 100);
    m_jpegQualitySpinBox->setSingleStep(5);
    m_jpegQualitySpinBox->setValue(95);

    m_writeTimelineCheckBox = new QCheckBox("Write timeline.json (media time map)", this);
    m_writeTimelineCheckBox->setChecked(true);

    QLabel* outputHelpLabel = new QLabel(
        "Higher JPEG values produce better quality images but larger file sizes. "
        "timeline.json records media times for each saved slide so players can map "
        "video time to the current image.",
        this);
    outputHelpLabel->setWordWrap(true);
    outputHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    outputLayout->addWidget(jpegQualityLabel, 0, 0);
    outputLayout->addWidget(m_jpegQualitySpinBox, 0, 1);
    outputLayout->addWidget(m_writeTimelineCheckBox, 1, 0, 1, 2);
    outputLayout->addWidget(outputHelpLabel, 2, 0, 1, 2);

    tabLayout->addWidget(outputGroup);
    tabLayout->addStretch();

    connect(m_ssimPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProcessingTab::onSSIMPresetChanged);
    connect(m_enableDownsamplingCheckBox, &QCheckBox::toggled,
            this, &ProcessingTab::onDownsamplingToggled);
    connect(m_downsamplePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProcessingTab::onDownsamplePresetChanged);
    connect(m_downsampleWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProcessingTab::updateDownsamplePresetFromDimensions);
    connect(m_downsampleHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProcessingTab::updateDownsamplePresetFromDimensions);
}

void ProcessingTab::load(const AppConfig& config)
{
    m_ssimPresetCombo->setCurrentIndex(static_cast<int>(config.ssimPreset));
    m_customSSIMSpinBox->setValue(config.customSSIMThreshold);
    onSSIMPresetChanged(); // Update custom spinbox state

    m_chunkSizeSpinBox->setValue(config.chunkSize);
    m_jpegQualitySpinBox->setValue(config.jpegQuality);
    m_writeTimelineCheckBox->setChecked(config.writeTimeline);

    m_enableDownsamplingCheckBox->setChecked(config.enableDownsampling);
    m_downsampleWidthSpinBox->setValue(config.downsampleWidth);
    m_downsampleHeightSpinBox->setValue(config.downsampleHeight);
    updateDownsamplePresetFromDimensions();
    onDownsamplingToggled(); // Update enabled state
}

void ProcessingTab::store(AppConfig& config) const
{
    config.ssimPreset = static_cast<SSIMPreset>(m_ssimPresetCombo->currentData().toInt());
    config.customSSIMThreshold = m_customSSIMSpinBox->value();
    config.chunkSize = m_chunkSizeSpinBox->value();
    config.jpegQuality = m_jpegQualitySpinBox->value();
    config.writeTimeline = m_writeTimelineCheckBox->isChecked();
    config.enableDownsampling = m_enableDownsamplingCheckBox->isChecked();
    config.downsampleWidth = m_downsampleWidthSpinBox->value();
    config.downsampleHeight = m_downsampleHeightSpinBox->value();
}

void ProcessingTab::onSSIMPresetChanged()
{
    SSIMPreset preset = static_cast<SSIMPreset>(m_ssimPresetCombo->currentData().toInt());
    bool isCustom = (preset == SSIMPreset::Custom);

    m_customSSIMSpinBox->setEnabled(isCustom);

    if (!isCustom) {
        double presetValue = ConfigManager::getSSIMThreshold(preset);
        m_customSSIMSpinBox->setValue(presetValue);
    }
}

void ProcessingTab::onDownsamplingToggled()
{
    bool enabled = m_enableDownsamplingCheckBox->isChecked();
    m_downsamplePresetCombo->setEnabled(enabled);
    m_downsampleWidthSpinBox->setEnabled(enabled);
    m_downsampleHeightSpinBox->setEnabled(enabled);
}

void ProcessingTab::onDownsamplePresetChanged()
{
    updateDownsampleDimensionsFromPreset();
}

void ProcessingTab::updateDownsampleDimensionsFromPreset()
{
    int presetIndex = m_downsamplePresetCombo->currentIndex();

    switch (presetIndex) {
        case 0: // 480p
            m_downsampleWidthSpinBox->setValue(854);
            m_downsampleHeightSpinBox->setValue(480);
            break;
        case 1: // 360p
            m_downsampleWidthSpinBox->setValue(640);
            m_downsampleHeightSpinBox->setValue(360);
            break;
        case 2: // 270p
            m_downsampleWidthSpinBox->setValue(480);
            m_downsampleHeightSpinBox->setValue(270);
            break;
        case 3: // Custom - don't change values
            break;
    }
}

void ProcessingTab::updateDownsamplePresetFromDimensions()
{
    int width = m_downsampleWidthSpinBox->value();
    int height = m_downsampleHeightSpinBox->value();

    if (width == 854 && height == 480) {
        m_downsamplePresetCombo->setCurrentIndex(0); // 480p
    } else if (width == 640 && height == 360) {
        m_downsamplePresetCombo->setCurrentIndex(1); // 360p
    } else if (width == 480 && height == 270) {
        m_downsamplePresetCombo->setCurrentIndex(2); // 270p
    } else {
        m_downsamplePresetCombo->setCurrentIndex(3); // Custom
    }
}
