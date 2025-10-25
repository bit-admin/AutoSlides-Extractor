#include "settingsdialog.h"
#include <QApplication>

SettingsDialog::SettingsDialog(const AppConfig& config, QWidget *parent)
    : QDialog(parent), m_config(config), m_originalConfig(config)
{
    setWindowTitle("Settings");
    setModal(true);
    resize(450, 540);  // Further increased to ensure all text is visible

    setupUI();
    updateUIFromConfig();
}

AppConfig SettingsDialog::getConfig() const
{
    return m_config;
}

void SettingsDialog::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(12);
    m_mainLayout->setContentsMargins(16, 16, 16, 16);

    // === SSIM THRESHOLD SETTINGS ===
    m_ssimGroup = new QGroupBox("SSIM Threshold", this);
    QGridLayout* ssimLayout = new QGridLayout(m_ssimGroup);
    ssimLayout->setContentsMargins(12, 12, 12, 12);
    ssimLayout->setSpacing(8);

    // SSIM Preset
    QLabel* ssimPresetLabel = new QLabel("Preset:", this);
    m_ssimPresetCombo = new QComboBox(this);
    m_ssimPresetCombo->addItem("Strict (0.999)", static_cast<int>(SSIMPreset::Strict));
    m_ssimPresetCombo->addItem("Normal (0.9985)", static_cast<int>(SSIMPreset::Normal));
    m_ssimPresetCombo->addItem("Loose (0.998)", static_cast<int>(SSIMPreset::Loose));
    m_ssimPresetCombo->addItem("Custom", static_cast<int>(SSIMPreset::Custom));

    // Custom SSIM value
    QLabel* customSSIMLabel = new QLabel("Custom Value:", this);
    m_customSSIMSpinBox = new QDoubleSpinBox(this);
    m_customSSIMSpinBox->setRange(0.900, 0.9999);
    m_customSSIMSpinBox->setDecimals(4);
    m_customSSIMSpinBox->setSingleStep(0.0001);
    m_customSSIMSpinBox->setEnabled(false);

    // Help text
    m_ssimHelpLabel = new QLabel("Higher global structural similarity threshold indicate stricter matching. Note that a minor change of 0.001 can significantly impact performance.", this);
    m_ssimHelpLabel->setWordWrap(true);
    m_ssimHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    ssimLayout->addWidget(ssimPresetLabel, 0, 0);
    ssimLayout->addWidget(m_ssimPresetCombo, 0, 1);
    ssimLayout->addWidget(customSSIMLabel, 1, 0);
    ssimLayout->addWidget(m_customSSIMSpinBox, 1, 1);
    ssimLayout->addWidget(m_ssimHelpLabel, 2, 0, 1, 2);

    m_mainLayout->addWidget(m_ssimGroup);

    // === DOWNSAMPLING SETTINGS ===
    m_downsamplingGroup = new QGroupBox("Downsampling", this);
    QGridLayout* downsamplingLayout = new QGridLayout(m_downsamplingGroup);
    downsamplingLayout->setContentsMargins(12, 12, 12, 12);
    downsamplingLayout->setSpacing(8);

    // Enable downsampling checkbox
    m_enableDownsamplingCheckBox = new QCheckBox("Enable Downsampling", this);

    // Preset combo
    QLabel* presetLabel = new QLabel("Preset:", this);
    m_downsamplePresetCombo = new QComboBox(this);
    m_downsamplePresetCombo->addItem("480p");
    m_downsamplePresetCombo->addItem("360p");
    m_downsamplePresetCombo->addItem("270p");
    m_downsamplePresetCombo->addItem("Custom");

    // Custom dimensions
    QLabel* widthLabel = new QLabel("Width:", this);
    m_downsampleWidthSpinBox = new QSpinBox(this);
    m_downsampleWidthSpinBox->setRange(160, 1920);
    m_downsampleWidthSpinBox->setSingleStep(10);

    QLabel* heightLabel = new QLabel("Height:", this);
    m_downsampleHeightSpinBox = new QSpinBox(this);
    m_downsampleHeightSpinBox->setRange(90, 1080);
    m_downsampleHeightSpinBox->setSingleStep(10);

    // Help text
    m_downsamplingHelpLabel = new QLabel("Downsampling with anti-aliasing is performed to mitigate artifacts when calculating SSIM, thereby improving image detection accuracy.", this);
    m_downsamplingHelpLabel->setWordWrap(true);
    m_downsamplingHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    downsamplingLayout->addWidget(m_enableDownsamplingCheckBox, 0, 0, 1, 2);
    downsamplingLayout->addWidget(presetLabel, 1, 0);
    downsamplingLayout->addWidget(m_downsamplePresetCombo, 1, 1);
    downsamplingLayout->addWidget(widthLabel, 2, 0);
    downsamplingLayout->addWidget(m_downsampleWidthSpinBox, 2, 1);
    downsamplingLayout->addWidget(heightLabel, 3, 0);
    downsamplingLayout->addWidget(m_downsampleHeightSpinBox, 3, 1);
    downsamplingLayout->addWidget(m_downsamplingHelpLabel, 4, 0, 1, 2);

    m_mainLayout->addWidget(m_downsamplingGroup);

    // === CHUNK SIZE SETTINGS ===
    m_chunkGroup = new QGroupBox("Memory Optimization", this);
    QGridLayout* chunkLayout = new QGridLayout(m_chunkGroup);
    chunkLayout->setContentsMargins(12, 12, 12, 12);
    chunkLayout->setSpacing(8);

    QLabel* chunkSizeLabel = new QLabel("Chunk Size:", this);
    m_chunkSizeSpinBox = new QSpinBox(this);
    m_chunkSizeSpinBox->setRange(100, 2000);
    m_chunkSizeSpinBox->setSingleStep(50);
    m_chunkSizeSpinBox->setSuffix(" frames");

    m_chunkHelpLabel = new QLabel("Number of frames processed at once. Smaller values use less memory but may be slower. Larger values are faster but use more memory.", this);
    m_chunkHelpLabel->setWordWrap(true);
    m_chunkHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    chunkLayout->addWidget(chunkSizeLabel, 0, 0);
    chunkLayout->addWidget(m_chunkSizeSpinBox, 0, 1);
    chunkLayout->addWidget(m_chunkHelpLabel, 1, 0, 1, 2);

    m_mainLayout->addWidget(m_chunkGroup);

    // === DIALOG BUTTONS ===
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_applyButton = new QPushButton("Apply", this);
    m_buttonBox->addButton(m_applyButton, QDialogButtonBox::ApplyRole);

    m_mainLayout->addWidget(m_buttonBox);

    // Connect signals
    connect(m_ssimPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onSSIMPresetChanged);
    connect(m_enableDownsamplingCheckBox, &QCheckBox::toggled,
            this, &SettingsDialog::onDownsamplingToggled);
    connect(m_downsamplePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onDownsamplePresetChanged);
    connect(m_downsampleWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::updateDownsamplePresetFromDimensions);
    connect(m_downsampleHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::updateDownsamplePresetFromDimensions);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onOkClicked);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::onCancelClicked);
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApplyClicked);
}

void SettingsDialog::updateUIFromConfig()
{
    // SSIM settings
    m_ssimPresetCombo->setCurrentIndex(static_cast<int>(m_config.ssimPreset));
    m_customSSIMSpinBox->setValue(m_config.customSSIMThreshold);
    onSSIMPresetChanged(); // Update custom spinbox state

    // Chunk size
    m_chunkSizeSpinBox->setValue(m_config.chunkSize);

    // Downsampling settings
    m_enableDownsamplingCheckBox->setChecked(m_config.enableDownsampling);
    m_downsampleWidthSpinBox->setValue(m_config.downsampleWidth);
    m_downsampleHeightSpinBox->setValue(m_config.downsampleHeight);
    updateDownsamplePresetFromDimensions();
    onDownsamplingToggled(); // Update enabled state
}

void SettingsDialog::updateConfigFromUI()
{
    // SSIM settings
    m_config.ssimPreset = static_cast<SSIMPreset>(m_ssimPresetCombo->currentData().toInt());
    m_config.customSSIMThreshold = m_customSSIMSpinBox->value();

    // Chunk size
    m_config.chunkSize = m_chunkSizeSpinBox->value();

    // Downsampling settings
    m_config.enableDownsampling = m_enableDownsamplingCheckBox->isChecked();
    m_config.downsampleWidth = m_downsampleWidthSpinBox->value();
    m_config.downsampleHeight = m_downsampleHeightSpinBox->value();
}

void SettingsDialog::onSSIMPresetChanged()
{
    SSIMPreset preset = static_cast<SSIMPreset>(m_ssimPresetCombo->currentData().toInt());
    bool isCustom = (preset == SSIMPreset::Custom);

    m_customSSIMSpinBox->setEnabled(isCustom);

    if (!isCustom) {
        // Update custom value to match preset
        double presetValue = ConfigManager::getSSIMThreshold(preset);
        m_customSSIMSpinBox->setValue(presetValue);
    }
}

void SettingsDialog::onDownsamplingToggled()
{
    bool enabled = m_enableDownsamplingCheckBox->isChecked();
    m_downsamplePresetCombo->setEnabled(enabled);
    m_downsampleWidthSpinBox->setEnabled(enabled);
    m_downsampleHeightSpinBox->setEnabled(enabled);
}

void SettingsDialog::onDownsamplePresetChanged()
{
    updateDownsampleDimensionsFromPreset();
}

void SettingsDialog::updateDownsampleDimensionsFromPreset()
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

void SettingsDialog::updateDownsamplePresetFromDimensions()
{
    int width = m_downsampleWidthSpinBox->value();
    int height = m_downsampleHeightSpinBox->value();

    // Check if dimensions match any preset
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

void SettingsDialog::onOkClicked()
{
    updateConfigFromUI();
    accept();
}

void SettingsDialog::onCancelClicked()
{
    m_config = m_originalConfig; // Restore original config
    reject();
}

void SettingsDialog::onApplyClicked()
{
    updateConfigFromUI();
    // Don't close dialog, just apply changes
}