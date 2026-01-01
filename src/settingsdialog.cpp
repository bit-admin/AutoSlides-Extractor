#include "settingsdialog.h"
#include "phashcalculator.h"
#include "mlclassifier.h"
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileInfo>
#include <algorithm>

SettingsDialog::SettingsDialog(const AppConfig& config, ConfigManager* configManager, int initialTab, QWidget *parent)
    : QDialog(parent), m_config(config), m_originalConfig(config), m_configManager(configManager)
{
    setWindowTitle("Settings");
    setModal(true);
    resize(550, 700);

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

    // Create scroll area for the entire content
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Create tab widget inside scroll area
    m_tabWidget = new QTabWidget();
    scrollArea->setWidget(m_tabWidget);
    m_mainLayout->addWidget(scrollArea, 1);  // stretch factor 1 to take available space

    // Setup tabs
    setupProcessingTab();
    setupPostProcessingTab();
    setupMLClassificationTab();

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

    // ML Classification signals
#ifdef ONNX_AVAILABLE
    connect(m_mlBrowseModelButton, &QPushButton::clicked, this, [this]() {
        QString fileName = QFileDialog::getOpenFileName(this,
            "Select ONNX Model File",
            QString(),
            "ONNX Models (*.onnx);;All Files (*)");

        if (!fileName.isEmpty()) {
            m_mlModelPathEdit->setText(fileName);
        }
    });

    connect(m_mlUseDefaultModelButton, &QPushButton::clicked, this, [this]() {
        m_mlModelPathEdit->clear();
        m_mlModelPathEdit->setPlaceholderText("Using built-in model");
    });

    connect(m_mlTestButton, &QPushButton::clicked, this, &SettingsDialog::onTestMLClassificationClicked);
#endif
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

    // === OUTPUT SETTINGS ===
    m_outputGroup = new QGroupBox("Output Settings", m_processingTab);
    QGridLayout* outputLayout = new QGridLayout(m_outputGroup);
    outputLayout->setContentsMargins(12, 12, 12, 12);
    outputLayout->setSpacing(8);

    QLabel* jpegQualityLabel = new QLabel("JPEG Quality:", m_processingTab);
    m_jpegQualitySpinBox = new QSpinBox(m_processingTab);
    m_jpegQualitySpinBox->setRange(1, 100);
    m_jpegQualitySpinBox->setSingleStep(5);
    m_jpegQualitySpinBox->setValue(95);

    m_outputHelpLabel = new QLabel("Higher values produce better quality images but larger file sizes.", m_processingTab);
    m_outputHelpLabel->setWordWrap(true);
    m_outputHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    outputLayout->addWidget(jpegQualityLabel, 0, 0);
    outputLayout->addWidget(m_jpegQualitySpinBox, 0, 1);
    outputLayout->addWidget(m_outputHelpLabel, 1, 0, 1, 2);

    tabLayout->addWidget(m_outputGroup);
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

    m_tabWidget->addTab(m_postProcessingTab, "Post-Processing (pHash)");

    // Update table
    updateExclusionTable();
}

void SettingsDialog::setupMLClassificationTab()
{
#ifdef ONNX_AVAILABLE
    m_mlClassificationTab = new QWidget();
    QVBoxLayout* tabLayout = new QVBoxLayout(m_mlClassificationTab);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === ML CLASSIFICATION SETTINGS ===
    QGroupBox* mlGroup = new QGroupBox("ML Classification Settings", m_mlClassificationTab);
    QVBoxLayout* mlLayout = new QVBoxLayout(mlGroup);
    mlLayout->setContentsMargins(12, 12, 12, 12);
    mlLayout->setSpacing(8);

    // Model path selector
    QHBoxLayout* modelPathLayout = new QHBoxLayout();
    QLabel* modelPathLabel = new QLabel("Model Path:", m_mlClassificationTab);
    m_mlModelPathEdit = new QLineEdit(m_mlClassificationTab);
    m_mlModelPathEdit->setReadOnly(true);
    m_mlModelPathEdit->setPlaceholderText("Using built-in model");

    m_mlBrowseModelButton = new QPushButton("Browse...", m_mlClassificationTab);
    m_mlBrowseModelButton->setFixedWidth(80);

    m_mlUseDefaultModelButton = new QPushButton("Use Default", m_mlClassificationTab);
    m_mlUseDefaultModelButton->setFixedWidth(100);

    modelPathLayout->addWidget(modelPathLabel);
    modelPathLayout->addWidget(m_mlModelPathEdit, 1);
    modelPathLayout->addWidget(m_mlBrowseModelButton);
    modelPathLayout->addWidget(m_mlUseDefaultModelButton);

    mlLayout->addLayout(modelPathLayout);

    // Threshold sliders section with clearer explanation
    QLabel* thresholdsLabel = new QLabel("ML Prediction Thresholds", m_mlClassificationTab);
    thresholdsLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    mlLayout->addWidget(thresholdsLabel);

    QLabel* thresholdsHelpLabel = new QLabel(
        "The model outputs a confidence score for each class. For 'not_slide/may_be_slide' classes: Delete zone → always removed. Check zone → removed only if their 'slide' probability falls in the Delete zone. Keep zone (low confidence) → always kept.", m_mlClassificationTab);
    thresholdsHelpLabel->setWordWrap(true);
    thresholdsHelpLabel->setStyleSheet("color: #666; font-size: 11px; margin-bottom: 4px;");
    mlLayout->addWidget(thresholdsHelpLabel);

    // === not_slide thresholds ===
    QLabel* notSlideLabel = new QLabel("'not_slide' (desktop, black screen, etc.)", m_mlClassificationTab);
    notSlideLabel->setStyleSheet("font-weight: bold; margin-top: 4px;");
    mlLayout->addWidget(notSlideLabel);

    m_mlNotSlideRangeSlider = new RangeSlider(Qt::Horizontal, m_mlClassificationTab);
    m_mlNotSlideRangeSlider->setRange(0, 100);
    m_mlNotSlideRangeSlider->setLowerValue(75);  // Default 0.75
    m_mlNotSlideRangeSlider->setUpperValue(90);  // Default 0.90
    m_mlNotSlideRangeSlider->setZoneLabels("Keep", "Check", "Delete");
    m_mlNotSlideRangeSlider->setMinimumHeight(50);
    mlLayout->addWidget(m_mlNotSlideRangeSlider);

    // === may_be_slide thresholds ===
    {
        QHBoxLayout* maybeSlideLabelLayout = new QHBoxLayout();
        QLabel* maybeSlideLabel = new QLabel("'may_be_slide' (PPT edit, side screen)", m_mlClassificationTab);
        maybeSlideLabel->setStyleSheet("font-weight: bold;");
        m_mlDeleteMaybeSlidesCheckBox = new QCheckBox("Delete 'may_be_slide' images", m_mlClassificationTab);
        maybeSlideLabelLayout->addWidget(maybeSlideLabel);
        maybeSlideLabelLayout->addStretch();
        maybeSlideLabelLayout->addWidget(m_mlDeleteMaybeSlidesCheckBox);
        mlLayout->addLayout(maybeSlideLabelLayout);
    }

    m_mlMaybeSlideRangeSlider = new RangeSlider(Qt::Horizontal, m_mlClassificationTab);
    m_mlMaybeSlideRangeSlider->setRange(0, 100);
    m_mlMaybeSlideRangeSlider->setLowerValue(75);  // Default 0.75
    m_mlMaybeSlideRangeSlider->setUpperValue(90);  // Default 0.90
    m_mlMaybeSlideRangeSlider->setZoneLabels("Keep", "Check", "Delete");
    m_mlMaybeSlideRangeSlider->setMinimumHeight(50);
    mlLayout->addWidget(m_mlMaybeSlideRangeSlider);

    // === slide_max threshold (shared) ===
    QLabel* slideMaxLabel = new QLabel("Max 'slide' probability (for Check zone)", m_mlClassificationTab);
    slideMaxLabel->setStyleSheet("font-weight: bold; margin-top: 4px;");
    mlLayout->addWidget(slideMaxLabel);

    m_mlSlideMaxThresholdSlider = new StyledSlider(Qt::Horizontal, m_mlClassificationTab);
    m_mlSlideMaxThresholdSlider->setRange(0, 100);
    m_mlSlideMaxThresholdSlider->setValue(25);  // Default 0.25
    m_mlSlideMaxThresholdSlider->setMinimumHeight(50);
    mlLayout->addWidget(m_mlSlideMaxThresholdSlider);

    tabLayout->addWidget(mlGroup);

    // === ML CLASSIFICATION TEST ===
    QGroupBox* mlTestGroup = new QGroupBox("Test ML Classification", m_mlClassificationTab);
    QVBoxLayout* mlTestLayout = new QVBoxLayout(mlTestGroup);
    mlTestLayout->setContentsMargins(12, 12, 12, 12);
    mlTestLayout->setSpacing(6);

    // Test button
    m_mlTestButton = new QPushButton("Select Image to Test", m_mlClassificationTab);
    mlTestLayout->addWidget(m_mlTestButton);

    // Result text area
    m_mlTestResultText = new QTextEdit(m_mlClassificationTab);
    m_mlTestResultText->setReadOnly(true);
    m_mlTestResultText->setMinimumHeight(100);
    m_mlTestResultText->setMaximumHeight(120);
    m_mlTestResultText->setPlaceholderText("Classification results will appear here...");
    m_mlTestResultText->setStyleSheet("font-family: monospace; font-size: 10px;");
    mlTestLayout->addWidget(m_mlTestResultText);

    tabLayout->addWidget(mlTestGroup);
    tabLayout->addStretch();

    m_tabWidget->addTab(m_mlClassificationTab, "Post-Processing (ML)");
#endif
}

void SettingsDialog::updateUIFromConfig()
{
    // SSIM settings
    m_ssimPresetCombo->setCurrentIndex(static_cast<int>(m_config.ssimPreset));
    m_customSSIMSpinBox->setValue(m_config.customSSIMThreshold);
    onSSIMPresetChanged(); // Update custom spinbox state

    // Chunk size
    m_chunkSizeSpinBox->setValue(m_config.chunkSize);

    // Output settings
    m_jpegQualitySpinBox->setValue(m_config.jpegQuality);

    // Downsampling settings
    m_enableDownsamplingCheckBox->setChecked(m_config.enableDownsampling);
    m_downsampleWidthSpinBox->setValue(m_config.downsampleWidth);
    m_downsampleHeightSpinBox->setValue(m_config.downsampleHeight);
    updateDownsamplePresetFromDimensions();
    onDownsamplingToggled(); // Update enabled state

    // Post-processing settings
    m_hammingThresholdSpinBox->setValue(m_config.hammingThreshold);

    // ML Classification settings
#ifdef ONNX_AVAILABLE
    m_mlDeleteMaybeSlidesCheckBox->setChecked(m_config.mlDeleteMaybeSlides);

    // Update model path
    if (m_config.mlModelPath.startsWith(":/")) {
        m_mlModelPathEdit->clear();
        m_mlModelPathEdit->setPlaceholderText("Using built-in model");
    } else {
        m_mlModelPathEdit->setText(m_config.mlModelPath);
    }

    // Update 2-stage threshold range sliders
    m_mlNotSlideRangeSlider->setUpperValue(static_cast<int>(m_config.mlNotSlideHighThreshold * 100));
    m_mlNotSlideRangeSlider->setLowerValue(static_cast<int>(m_config.mlNotSlideLowThreshold * 100));
    m_mlMaybeSlideRangeSlider->setUpperValue(static_cast<int>(m_config.mlMaybeSlideHighThreshold * 100));
    m_mlMaybeSlideRangeSlider->setLowerValue(static_cast<int>(m_config.mlMaybeSlideLowThreshold * 100));
    m_mlSlideMaxThresholdSlider->setValue(static_cast<int>(m_config.mlSlideMaxThreshold * 100));
#endif
}

void SettingsDialog::updateConfigFromUI()
{
    // SSIM settings
    m_config.ssimPreset = static_cast<SSIMPreset>(m_ssimPresetCombo->currentData().toInt());
    m_config.customSSIMThreshold = m_customSSIMSpinBox->value();

    // Chunk size
    m_config.chunkSize = m_chunkSizeSpinBox->value();

    // Output settings
    m_config.jpegQuality = m_jpegQualitySpinBox->value();

    // Downsampling settings
    m_config.enableDownsampling = m_enableDownsamplingCheckBox->isChecked();
    m_config.downsampleWidth = m_downsampleWidthSpinBox->value();
    m_config.downsampleHeight = m_downsampleHeightSpinBox->value();

    // Post-processing settings
    m_config.hammingThreshold = m_hammingThresholdSpinBox->value();

    // ML Classification settings
#ifdef ONNX_AVAILABLE
    m_config.mlDeleteMaybeSlides = m_mlDeleteMaybeSlidesCheckBox->isChecked();

    // Update model path
    if (m_mlModelPathEdit->text().isEmpty()) {
        m_config.mlModelPath = ":/models/resources/models/slide_classifier_mobilenetv4_v1.onnx";
    } else {
        m_config.mlModelPath = m_mlModelPathEdit->text();
    }

    // Update 2-stage threshold values from range sliders
    m_config.mlNotSlideHighThreshold = m_mlNotSlideRangeSlider->upperValue() / 100.0f;
    m_config.mlNotSlideLowThreshold = m_mlNotSlideRangeSlider->lowerValue() / 100.0f;
    m_config.mlMaybeSlideHighThreshold = m_mlMaybeSlideRangeSlider->upperValue() / 100.0f;
    m_config.mlMaybeSlideLowThreshold = m_mlMaybeSlideRangeSlider->lowerValue() / 100.0f;
    m_config.mlSlideMaxThreshold = m_mlSlideMaxThresholdSlider->value() / 100.0f;
#endif
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

#ifdef ONNX_AVAILABLE
void SettingsDialog::onTestMLClassificationClicked()
{
    // Select image file
    QString imagePath = QFileDialog::getOpenFileName(this,
        "Select Image to Test",
        QString(),
        "Images (*.jpg *.jpeg *.png *.bmp);;All Files (*)");

    if (imagePath.isEmpty()) {
        return;
    }

    m_mlTestResultText->clear();
    m_mlTestResultText->append("Testing ML Classification...");
    m_mlTestResultText->append(QString("Image: %1").arg(QFileInfo(imagePath).fileName()));
    m_mlTestResultText->append("----------------------------------------");

    // Get current model path
    QString modelPath = m_mlModelPathEdit->text();
    if (modelPath.isEmpty()) {
        modelPath = ":/models/resources/models/slide_classifier_mobilenetv4_v1.onnx";
    }

    // Get current 2-stage thresholds from range sliders
    float notSlideHighThreshold = m_mlNotSlideRangeSlider->upperValue() / 100.0f;
    float notSlideLowThreshold = m_mlNotSlideRangeSlider->lowerValue() / 100.0f;
    float maybeSlideHighThreshold = m_mlMaybeSlideRangeSlider->upperValue() / 100.0f;
    float maybeSlideLowThreshold = m_mlMaybeSlideRangeSlider->lowerValue() / 100.0f;
    float slideMaxThreshold = m_mlSlideMaxThresholdSlider->value() / 100.0f;

    // Get delete maybe slides setting
    bool deleteMaybeSlides = m_mlDeleteMaybeSlidesCheckBox->isChecked();

    // Initialize classifier
    MLClassifier classifier(modelPath, MLClassifier::ExecutionProvider::Auto);

    if (!classifier.isInitialized()) {
        m_mlTestResultText->append("ERROR: Failed to initialize ML classifier");
        m_mlTestResultText->append(QString("Reason: %1").arg(classifier.getErrorMessage()));
        return;
    }

    m_mlTestResultText->append(QString("Execution Provider: %1").arg(classifier.getActiveExecutionProvider()));
    m_mlTestResultText->append("");

    // Classify the image
    ClassificationResult result = classifier.classifySingle(imagePath);

    if (result.error) {
        m_mlTestResultText->append(QString("X ERROR: %1").arg(result.errorMessage));
        return;
    }

    // Display results
    m_mlTestResultText->append("CLASSIFICATION RESULTS:");
    m_mlTestResultText->append("----------------------------------------");
    m_mlTestResultText->append(QString("Predicted Class: %1").arg(result.predictedClass));
    m_mlTestResultText->append(QString("Confidence: %1 (%2%)")
        .arg(result.confidence, 0, 'f', 4)
        .arg(result.confidence * 100, 0, 'f', 2));
    m_mlTestResultText->append("");

    // Display all class probabilities (sorted by confidence)
    m_mlTestResultText->append("ALL CLASS PROBABILITIES:");
    m_mlTestResultText->append("----------------------------------------");

    // Sort by probability (descending)
    QList<QPair<QString, float>> sortedProbs;
    for (auto it = result.classProbabilities.constBegin(); it != result.classProbabilities.constEnd(); ++it) {
        sortedProbs.append(qMakePair(it.key(), it.value()));
    }
    std::sort(sortedProbs.begin(), sortedProbs.end(), [](const QPair<QString, float>& a, const QPair<QString, float>& b) {
        return a.second > b.second;
    });

    for (const auto& pair : sortedProbs) {
        QString className = pair.first;
        float probability = pair.second;

        QString indicator;
        if (className == result.predictedClass) {
            indicator = " <- PREDICTED";
        } else {
            indicator = "";
        }

        m_mlTestResultText->append(QString("  %1: %2%%3")
            .arg(className)
            .arg(probability * 100, 0, 'f', 2)
            .arg(indicator));
    }

    // Get slide probability for 2-stage logic explanation
    float slideProb = result.classProbabilities.value("slide", 0.0f);

    // Decision logic using 2-stage thresholds
    m_mlTestResultText->append("");
    m_mlTestResultText->append("2-STAGE DECISION:");
    m_mlTestResultText->append("----------------------------------------");

    MLClassifier::CategoryThresholds notSlideThresholds(notSlideHighThreshold, notSlideLowThreshold);
    MLClassifier::CategoryThresholds maybeSlideThresholds(maybeSlideHighThreshold, maybeSlideLowThreshold);

    bool shouldKeep = MLClassifier::shouldKeepImage(result, notSlideThresholds,
                                                    maybeSlideThresholds, slideMaxThreshold,
                                                    deleteMaybeSlides);

    if (shouldKeep) {
        m_mlTestResultText->append("[KEEP] This image would be KEPT");
    } else {
        m_mlTestResultText->append("[REMOVE] This image would be MOVED TO TRASH");
    }

    // Explain the decision
    m_mlTestResultText->append("");
    m_mlTestResultText->append("REASON:");
    if (result.predictedClass == "slide") {
        m_mlTestResultText->append("  - Classified as 'slide' (always kept)");
    } else if (result.predictedClass.startsWith("not_slide")) {
        float conf = result.confidence;
        if (conf >= notSlideHighThreshold) {
            m_mlTestResultText->append(QString("  - High confidence: %1 >= %2 (high threshold)")
                .arg(conf, 0, 'f', 4)
                .arg(notSlideHighThreshold, 0, 'f', 2));
            m_mlTestResultText->append("  - Removed immediately (Stage 1)");
        } else if (conf >= notSlideLowThreshold) {
            m_mlTestResultText->append(QString("  - Medium confidence: %1 in [%2, %3)")
                .arg(conf, 0, 'f', 4)
                .arg(notSlideLowThreshold, 0, 'f', 2)
                .arg(notSlideHighThreshold, 0, 'f', 2));
            if (slideProb <= slideMaxThreshold) {
                m_mlTestResultText->append(QString("  - Slide prob %1 <= %2 (slide_max) -> Removed (Stage 2)")
                    .arg(slideProb, 0, 'f', 4)
                    .arg(slideMaxThreshold, 0, 'f', 2));
            } else {
                m_mlTestResultText->append(QString("  - Slide prob %1 > %2 (slide_max) -> Kept (Stage 2 failed)")
                    .arg(slideProb, 0, 'f', 4)
                    .arg(slideMaxThreshold, 0, 'f', 2));
            }
        } else {
            m_mlTestResultText->append(QString("  - Low confidence: %1 < %2 (low threshold)")
                .arg(conf, 0, 'f', 4)
                .arg(notSlideLowThreshold, 0, 'f', 2));
            m_mlTestResultText->append("  - Kept by default");
        }
    } else if (result.predictedClass.startsWith("may_be_slide")) {
        if (!deleteMaybeSlides) {
            m_mlTestResultText->append("  - 'Delete may_be_slide images' is DISABLED");
            m_mlTestResultText->append("  - Kept regardless of confidence");
        } else {
            float conf = result.confidence;
            if (conf >= maybeSlideHighThreshold) {
                m_mlTestResultText->append(QString("  - High confidence: %1 >= %2 (high threshold)")
                    .arg(conf, 0, 'f', 4)
                    .arg(maybeSlideHighThreshold, 0, 'f', 2));
                m_mlTestResultText->append("  - Removed immediately (Stage 1)");
            } else if (conf >= maybeSlideLowThreshold) {
                m_mlTestResultText->append(QString("  - Medium confidence: %1 in [%2, %3)")
                    .arg(conf, 0, 'f', 4)
                    .arg(maybeSlideLowThreshold, 0, 'f', 2)
                    .arg(maybeSlideHighThreshold, 0, 'f', 2));
                if (slideProb <= slideMaxThreshold) {
                    m_mlTestResultText->append(QString("  - Slide prob %1 <= %2 (slide_max) -> Removed (Stage 2)")
                        .arg(slideProb, 0, 'f', 4)
                        .arg(slideMaxThreshold, 0, 'f', 2));
                } else {
                    m_mlTestResultText->append(QString("  - Slide prob %1 > %2 (slide_max) -> Kept (Stage 2 failed)")
                        .arg(slideProb, 0, 'f', 4)
                        .arg(slideMaxThreshold, 0, 'f', 2));
                }
            } else {
                m_mlTestResultText->append(QString("  - Low confidence: %1 < %2 (low threshold)")
                    .arg(conf, 0, 'f', 4)
                    .arg(maybeSlideLowThreshold, 0, 'f', 2));
                m_mlTestResultText->append("  - Kept by default");
            }
        }
    }

    m_mlTestResultText->append("");
    m_mlTestResultText->append("Test completed successfully!");
}
#endif

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

    m_jpegQualitySpinBox->setValue(m_config.jpegQuality);

    m_enableDownsamplingCheckBox->setChecked(m_config.enableDownsampling);
    m_downsampleWidthSpinBox->setValue(m_config.downsampleWidth);
    m_downsampleHeightSpinBox->setValue(m_config.downsampleHeight);
    updateDownsamplePresetFromDimensions();
    onDownsamplingToggled();

    // Update UI to reflect defaults - Post-processing tab
    m_hammingThresholdSpinBox->setValue(m_config.hammingThreshold);
    updateExclusionTable();

    // Update UI to reflect defaults - ML Classification tab
#ifdef ONNX_AVAILABLE
    m_mlDeleteMaybeSlidesCheckBox->setChecked(m_config.mlDeleteMaybeSlides);
    m_mlModelPathEdit->clear();
    m_mlModelPathEdit->setPlaceholderText("Using built-in model");

    // Range sliders use 0-100 scale (values are stored as 0.0-1.0)
    m_mlNotSlideRangeSlider->setLowerValue(static_cast<int>(m_config.mlNotSlideLowThreshold * 100));
    m_mlNotSlideRangeSlider->setUpperValue(static_cast<int>(m_config.mlNotSlideHighThreshold * 100));

    m_mlMaybeSlideRangeSlider->setLowerValue(static_cast<int>(m_config.mlMaybeSlideLowThreshold * 100));
    m_mlMaybeSlideRangeSlider->setUpperValue(static_cast<int>(m_config.mlMaybeSlideHighThreshold * 100));

    m_mlSlideMaxThresholdSlider->setValue(static_cast<int>(m_config.mlSlideMaxThreshold * 100));
#endif

    // Save the defaults
    m_configManager->saveConfig(m_config);
    m_configManager->saveExclusionList(m_exclusionList);
}
