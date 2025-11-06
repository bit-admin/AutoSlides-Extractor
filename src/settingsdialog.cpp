#include "settingsdialog.h"
#include "phashcalculator.h"
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>

SettingsDialog::SettingsDialog(const AppConfig& config, ConfigManager* configManager, int initialTab, QWidget *parent)
    : QDialog(parent), m_config(config), m_originalConfig(config), m_configManager(configManager)
{
    setWindowTitle("Settings");
    setModal(true);
    resize(550, 600);

    // Load exclusion list
    m_exclusionList = m_configManager->loadExclusionList();

    setupUI();
    updateUIFromConfig();

    // Set initial tab
    if (m_tabWidget && initialTab >= 0 && initialTab < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(initialTab);
    }
}

AppConfig SettingsDialog::getConfig() const
{
    return m_config;
}

void SettingsDialog::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(8);
    m_mainLayout->setContentsMargins(12, 12, 12, 12);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);
    m_mainLayout->addWidget(m_tabWidget);

    // Setup tabs
    setupProcessingTab();
    setupPostProcessingTab();

    // === DIALOG BUTTONS ===
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_applyButton = new QPushButton("Apply", this);
    m_restoreDefaultsButton = new QPushButton("Restore Defaults", this);
    m_buttonBox->addButton(m_applyButton, QDialogButtonBox::ApplyRole);
    m_buttonBox->addButton(m_restoreDefaultsButton, QDialogButtonBox::ResetRole);

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
    connect(m_restoreDefaultsButton, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaultsClicked);

    // Post-processing signals
    connect(m_addFromImageButton, &QPushButton::clicked, this, &SettingsDialog::onAddFromImageClicked);
    connect(m_manualInputButton, &QPushButton::clicked, this, &SettingsDialog::onManualInputClicked);
}

void SettingsDialog::setupProcessingTab()
{
    m_processingTab = new QWidget();
    QVBoxLayout* tabLayout = new QVBoxLayout(m_processingTab);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === SSIM THRESHOLD SETTINGS ===
    m_ssimGroup = new QGroupBox("SSIM Threshold", m_processingTab);
    QGridLayout* ssimLayout = new QGridLayout(m_ssimGroup);
    ssimLayout->setContentsMargins(12, 12, 12, 12);
    ssimLayout->setSpacing(8);

    // SSIM Preset
    QLabel* ssimPresetLabel = new QLabel("Preset:", m_processingTab);
    m_ssimPresetCombo = new QComboBox(m_processingTab);
    m_ssimPresetCombo->addItem("Strict (0.999)", static_cast<int>(SSIMPreset::Strict));
    m_ssimPresetCombo->addItem("Normal (0.9985)", static_cast<int>(SSIMPreset::Normal));
    m_ssimPresetCombo->addItem("Loose (0.998)", static_cast<int>(SSIMPreset::Loose));
    m_ssimPresetCombo->addItem("Custom", static_cast<int>(SSIMPreset::Custom));

    // Custom SSIM value
    QLabel* customSSIMLabel = new QLabel("Custom Value:", m_processingTab);
    m_customSSIMSpinBox = new QDoubleSpinBox(m_processingTab);
    m_customSSIMSpinBox->setRange(0.900, 0.9999);
    m_customSSIMSpinBox->setDecimals(4);
    m_customSSIMSpinBox->setSingleStep(0.0001);
    m_customSSIMSpinBox->setEnabled(false);

    // Help text
    m_ssimHelpLabel = new QLabel("Higher global structural similarity threshold indicate stricter matching. Note that a minor change of 0.001 can significantly impact performance.", m_processingTab);
    m_ssimHelpLabel->setWordWrap(true);
    m_ssimHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    ssimLayout->addWidget(ssimPresetLabel, 0, 0);
    ssimLayout->addWidget(m_ssimPresetCombo, 0, 1);
    ssimLayout->addWidget(customSSIMLabel, 1, 0);
    ssimLayout->addWidget(m_customSSIMSpinBox, 1, 1);
    ssimLayout->addWidget(m_ssimHelpLabel, 2, 0, 1, 2);

    tabLayout->addWidget(m_ssimGroup);

    // === DOWNSAMPLING SETTINGS ===
    m_downsamplingGroup = new QGroupBox("Downsampling", m_processingTab);
    QGridLayout* downsamplingLayout = new QGridLayout(m_downsamplingGroup);
    downsamplingLayout->setContentsMargins(12, 12, 12, 12);
    downsamplingLayout->setSpacing(8);

    // Enable downsampling checkbox
    m_enableDownsamplingCheckBox = new QCheckBox("Enable Downsampling", m_processingTab);

    // Preset combo
    QLabel* presetLabel = new QLabel("Preset:", m_processingTab);
    m_downsamplePresetCombo = new QComboBox(m_processingTab);
    m_downsamplePresetCombo->addItem("480p");
    m_downsamplePresetCombo->addItem("360p");
    m_downsamplePresetCombo->addItem("270p");
    m_downsamplePresetCombo->addItem("Custom");

    // Custom dimensions
    QLabel* widthLabel = new QLabel("Width:", m_processingTab);
    m_downsampleWidthSpinBox = new QSpinBox(m_processingTab);
    m_downsampleWidthSpinBox->setRange(160, 1920);
    m_downsampleWidthSpinBox->setSingleStep(10);

    QLabel* heightLabel = new QLabel("Height:", m_processingTab);
    m_downsampleHeightSpinBox = new QSpinBox(m_processingTab);
    m_downsampleHeightSpinBox->setRange(90, 1080);
    m_downsampleHeightSpinBox->setSingleStep(10);

    // Help text
    m_downsamplingHelpLabel = new QLabel("Downsampling with anti-aliasing is performed to mitigate artifacts when calculating SSIM, thereby improving image detection accuracy.", m_processingTab);
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

    tabLayout->addWidget(m_downsamplingGroup);

    // === CHUNK SIZE SETTINGS ===
    m_chunkGroup = new QGroupBox("Memory Optimization", m_processingTab);
    QGridLayout* chunkLayout = new QGridLayout(m_chunkGroup);
    chunkLayout->setContentsMargins(12, 12, 12, 12);
    chunkLayout->setSpacing(8);

    QLabel* chunkSizeLabel = new QLabel("Chunk Size:", m_processingTab);
    m_chunkSizeSpinBox = new QSpinBox(m_processingTab);
    m_chunkSizeSpinBox->setRange(100, 2000);
    m_chunkSizeSpinBox->setSingleStep(50);
    m_chunkSizeSpinBox->setSuffix(" frames");

    m_chunkHelpLabel = new QLabel("Number of frames processed at once. Smaller values use less memory but may be slower. Larger values are faster but use more memory.", m_processingTab);
    m_chunkHelpLabel->setWordWrap(true);
    m_chunkHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    chunkLayout->addWidget(chunkSizeLabel, 0, 0);
    chunkLayout->addWidget(m_chunkSizeSpinBox, 0, 1);
    chunkLayout->addWidget(m_chunkHelpLabel, 1, 0, 1, 2);

    tabLayout->addWidget(m_chunkGroup);
    tabLayout->addStretch();

    m_tabWidget->addTab(m_processingTab, "Processing");
}

void SettingsDialog::setupPostProcessingTab()
{
    m_postProcessingTab = new QWidget();
    QVBoxLayout* tabLayout = new QVBoxLayout(m_postProcessingTab);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === HAMMING THRESHOLD SETTINGS ===
    QGroupBox* thresholdGroup = new QGroupBox("pHash Hamming Distance Threshold", m_postProcessingTab);
    QGridLayout* thresholdLayout = new QGridLayout(thresholdGroup);
    thresholdLayout->setContentsMargins(12, 12, 12, 12);
    thresholdLayout->setSpacing(8);

    QLabel* thresholdLabel = new QLabel("Threshold:", m_postProcessingTab);
    m_hammingThresholdSpinBox = new QSpinBox(m_postProcessingTab);
    m_hammingThresholdSpinBox->setRange(0, 50);
    m_hammingThresholdSpinBox->setValue(10);

    QLabel* thresholdHelpLabel = new QLabel("Lower Hamming distance threshold indicate stricter matching.", m_postProcessingTab);
    thresholdHelpLabel->setWordWrap(true);
    thresholdHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    thresholdLayout->addWidget(thresholdLabel, 0, 0);
    thresholdLayout->addWidget(m_hammingThresholdSpinBox, 0, 1);
    thresholdLayout->addWidget(thresholdHelpLabel, 1, 0, 1, 2);

    tabLayout->addWidget(thresholdGroup);

    // === EXCLUSION LIST ===
    QGroupBox* exclusionGroup = new QGroupBox("pHash Excluded List", m_postProcessingTab);
    QVBoxLayout* exclusionLayout = new QVBoxLayout(exclusionGroup);
    exclusionLayout->setContentsMargins(12, 12, 12, 12);
    exclusionLayout->setSpacing(8);

    QLabel* exclusionHelpLabel = new QLabel("Images with pHash matching these entries will be automatically moved to trash during post-processing.", m_postProcessingTab);
    exclusionHelpLabel->setWordWrap(true);
    exclusionHelpLabel->setStyleSheet("color: #666; font-size: 11px;");
    exclusionLayout->addWidget(exclusionHelpLabel);

    // Exclusion table
    m_exclusionTable = new QTableWidget(m_postProcessingTab);
    m_exclusionTable->setColumnCount(3);
    m_exclusionTable->setHorizontalHeaderLabels({"Remark", "Hash", "Delete"});
    m_exclusionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_exclusionTable->setAlternatingRowColors(true);
    m_exclusionTable->horizontalHeader()->setStretchLastSection(false);
    m_exclusionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_exclusionTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_exclusionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_exclusionTable->setColumnWidth(0, 120);
    m_exclusionTable->setColumnWidth(2, 60);
    m_exclusionTable->setMinimumHeight(200);

    exclusionLayout->addWidget(m_exclusionTable);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_addFromImageButton = new QPushButton("Add from Image", m_postProcessingTab);
    m_manualInputButton = new QPushButton("Manual Input", m_postProcessingTab);

    buttonLayout->addWidget(m_addFromImageButton, 1);  // 50% width
    buttonLayout->addWidget(m_manualInputButton, 1);   // 50% width

    exclusionLayout->addLayout(buttonLayout);

    tabLayout->addWidget(exclusionGroup);
    tabLayout->addStretch();

    m_tabWidget->addTab(m_postProcessingTab, "Post-Processing");

    // Update table
    updateExclusionTable();
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

    // Post-processing settings
    m_hammingThresholdSpinBox->setValue(m_config.hammingThreshold);
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

    // Post-processing settings
    m_config.hammingThreshold = m_hammingThresholdSpinBox->value();
}

void SettingsDialog::updateExclusionTable()
{
    m_exclusionTable->setRowCount(m_exclusionList.size());

    for (int i = 0; i < m_exclusionList.size(); i++) {
        // Remark
        QTableWidgetItem* remarkItem = new QTableWidgetItem(m_exclusionList[i].remark);
        remarkItem->setFlags(remarkItem->flags() & ~Qt::ItemIsEditable);
        m_exclusionTable->setItem(i, 0, remarkItem);

        // Hash
        QTableWidgetItem* hashItem = new QTableWidgetItem(m_exclusionList[i].hashHex);
        hashItem->setFlags(hashItem->flags() & ~Qt::ItemIsEditable);
        m_exclusionTable->setItem(i, 1, hashItem);

        // Delete button
        QPushButton* deleteButton = new QPushButton("Delete", m_postProcessingTab);
        connect(deleteButton, &QPushButton::clicked, this, [this, i]() {
            if (i < m_exclusionList.size()) {
                m_exclusionList.removeAt(i);
                updateExclusionTable();
            }
        });
        m_exclusionTable->setCellWidget(i, 2, deleteButton);
    }
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

void SettingsDialog::onAddFromImageClicked()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        "Select Image",
        QString(),
        "Image Files (*.jpg *.jpeg *.png *.bmp);;All Files (*)");

    if (filePath.isEmpty()) {
        return;
    }

    // Calculate pHash
    std::vector<uint8_t> hash = PHashCalculator::calculatePHash(filePath);
    if (hash.empty()) {
        emit statusMessage("Error: Failed to calculate pHash for the selected image.");
        return;
    }

    // Ask for remark
    bool ok;
    QString remark = QInputDialog::getText(this, "Add Exclusion Entry",
                                          "Enter a remark for this entry:",
                                          QLineEdit::Normal, "", &ok);
    if (!ok || remark.isEmpty()) {
        remark = "Custom";
    }

    // Add to list
    QString hashHex = PHashCalculator::hashToHexString(hash);
    m_exclusionList.append(ExclusionEntry(remark, hashHex));
    updateExclusionTable();
}

void SettingsDialog::onManualInputClicked()
{
    bool ok;
    QString hashHex = QInputDialog::getText(this, "Manual Input",
                                           "Enter 256-bit pHash (64 hex characters):",
                                           QLineEdit::Normal, "", &ok);

    if (!ok || hashHex.isEmpty()) {
        return;
    }

    // Validate hash
    if (hashHex.length() != 64) {
        emit statusMessage("Error: Hash must be exactly 64 hexadecimal characters.");
        return;
    }

    std::vector<uint8_t> hash = PHashCalculator::hexStringToHash(hashHex);
    if (hash.empty()) {
        emit statusMessage("Error: Invalid hexadecimal string.");
        return;
    }

    // Ask for remark
    QString remark = QInputDialog::getText(this, "Add Exclusion Entry",
                                          "Enter a remark for this entry:",
                                          QLineEdit::Normal, "", &ok);
    if (!ok || remark.isEmpty()) {
        remark = "Custom";
    }

    // Add to list
    m_exclusionList.append(ExclusionEntry(remark, hashHex));
    updateExclusionTable();
}

void SettingsDialog::onDeleteExclusionClicked()
{
    // Handled by individual delete buttons in table
}

void SettingsDialog::onOkClicked()
{
    updateConfigFromUI();
    m_configManager->saveExclusionList(m_exclusionList);
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
    m_configManager->saveExclusionList(m_exclusionList);
    // Don't close dialog, just apply changes
}

void SettingsDialog::onRestoreDefaultsClicked()
{
    // Create default config
    AppConfig defaultConfig;
    m_config = defaultConfig;

    // Restore default exclusion list
    m_exclusionList = PostProcessor::getDefaultExclusionList();

    // Update UI to reflect defaults - Processing tab
    m_ssimPresetCombo->setCurrentIndex(static_cast<int>(m_config.ssimPreset));
    m_customSSIMSpinBox->setValue(m_config.customSSIMThreshold);
    onSSIMPresetChanged();

    m_chunkSizeSpinBox->setValue(m_config.chunkSize);

    m_enableDownsamplingCheckBox->setChecked(m_config.enableDownsampling);
    m_downsampleWidthSpinBox->setValue(m_config.downsampleWidth);
    m_downsampleHeightSpinBox->setValue(m_config.downsampleHeight);
    updateDownsamplePresetFromDimensions();
    onDownsamplingToggled();

    // Update UI to reflect defaults - Post-processing tab
    m_hammingThresholdSpinBox->setValue(m_config.hammingThreshold);
    updateExclusionTable();

    // Save the defaults
    m_configManager->saveConfig(m_config);
    m_configManager->saveExclusionList(m_exclusionList);
}
