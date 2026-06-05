#include "mlclassificationtab.h"
#include "mlclassifier.h"
#include "configmanager.h"
#include "rangeslider.h"
#include "styledslider.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <algorithm>

namespace {
constexpr const char* kDefaultModel = ":/models/resources/models/slide_classifier_mobilenetv4_v1.onnx";
}

MLClassificationTab::MLClassificationTab(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(this);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === ML CLASSIFICATION SETTINGS ===
    QGroupBox* mlGroup = new QGroupBox("ML Classification Settings", this);
    QVBoxLayout* mlLayout = new QVBoxLayout(mlGroup);
    mlLayout->setContentsMargins(12, 12, 12, 12);
    mlLayout->setSpacing(8);

    // Model path selector
    QHBoxLayout* modelPathLayout = new QHBoxLayout();
    QLabel* modelPathLabel = new QLabel("Model Path:", this);
    m_modelPathEdit = new QLineEdit(this);
    m_modelPathEdit->setReadOnly(true);
    m_modelPathEdit->setPlaceholderText("Using built-in model");

    m_browseModelButton = new QPushButton("Browse...", this);
    m_browseModelButton->setFixedWidth(80);

    m_useDefaultModelButton = new QPushButton("Use Default", this);
    m_useDefaultModelButton->setFixedWidth(100);

    modelPathLayout->addWidget(modelPathLabel);
    modelPathLayout->addWidget(m_modelPathEdit, 1);
    modelPathLayout->addWidget(m_browseModelButton);
    modelPathLayout->addWidget(m_useDefaultModelButton);

    mlLayout->addLayout(modelPathLayout);

    QLabel* thresholdsLabel = new QLabel("ML Prediction Thresholds", this);
    thresholdsLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    mlLayout->addWidget(thresholdsLabel);

    QLabel* thresholdsHelpLabel = new QLabel(
        "The model outputs a confidence score for each class. For 'not_slide/may_be_slide' classes: Delete zone → always removed. Check zone → removed only if their 'slide' probability falls in the Delete zone. Keep zone (low confidence) → always kept.", this);
    thresholdsHelpLabel->setWordWrap(true);
    thresholdsHelpLabel->setStyleSheet("color: #666; font-size: 11px; margin-bottom: 4px;");
    mlLayout->addWidget(thresholdsHelpLabel);

    // === not_slide thresholds ===
    QLabel* notSlideLabel = new QLabel("'not_slide' (desktop, black screen, etc.)", this);
    notSlideLabel->setStyleSheet("font-weight: bold; margin-top: 4px;");
    mlLayout->addWidget(notSlideLabel);

    m_notSlideRangeSlider = new RangeSlider(Qt::Horizontal, this);
    m_notSlideRangeSlider->setRange(0, 100);
    m_notSlideRangeSlider->setLowerValue(75);  // Default 0.75
    m_notSlideRangeSlider->setUpperValue(90);  // Default 0.90
    m_notSlideRangeSlider->setZoneLabels("Keep", "Check", "Delete");
    m_notSlideRangeSlider->setMinimumHeight(50);
    mlLayout->addWidget(m_notSlideRangeSlider);

    // === may_be_slide thresholds ===
    {
        QHBoxLayout* maybeSlideLabelLayout = new QHBoxLayout();
        QLabel* maybeSlideLabel = new QLabel("'may_be_slide' (PPT edit, side screen)", this);
        maybeSlideLabel->setStyleSheet("font-weight: bold;");
        m_deleteMaybeSlidesCheckBox = new QCheckBox("Delete 'may_be_slide' images", this);
        maybeSlideLabelLayout->addWidget(maybeSlideLabel);
        maybeSlideLabelLayout->addStretch();
        maybeSlideLabelLayout->addWidget(m_deleteMaybeSlidesCheckBox);
        mlLayout->addLayout(maybeSlideLabelLayout);
    }

    m_maybeSlideRangeSlider = new RangeSlider(Qt::Horizontal, this);
    m_maybeSlideRangeSlider->setRange(0, 100);
    m_maybeSlideRangeSlider->setLowerValue(75);  // Default 0.75
    m_maybeSlideRangeSlider->setUpperValue(90);  // Default 0.90
    m_maybeSlideRangeSlider->setZoneLabels("Keep", "Check", "Delete");
    m_maybeSlideRangeSlider->setMinimumHeight(50);
    mlLayout->addWidget(m_maybeSlideRangeSlider);

    // === slide_max threshold (shared) ===
    QLabel* slideMaxLabel = new QLabel("Max 'slide' probability (for Check zone)", this);
    slideMaxLabel->setStyleSheet("font-weight: bold; margin-top: 4px;");
    mlLayout->addWidget(slideMaxLabel);

    m_slideMaxThresholdSlider = new StyledSlider(Qt::Horizontal, this);
    m_slideMaxThresholdSlider->setRange(0, 100);
    m_slideMaxThresholdSlider->setValue(25);  // Default 0.25
    m_slideMaxThresholdSlider->setMinimumHeight(50);
    mlLayout->addWidget(m_slideMaxThresholdSlider);

    tabLayout->addWidget(mlGroup);

    // === ML CLASSIFICATION TEST ===
    QGroupBox* mlTestGroup = new QGroupBox("Test ML Classification", this);
    QVBoxLayout* mlTestLayout = new QVBoxLayout(mlTestGroup);
    mlTestLayout->setContentsMargins(12, 12, 12, 12);
    mlTestLayout->setSpacing(6);

    m_testButton = new QPushButton("Select Image to Test", this);
    mlTestLayout->addWidget(m_testButton);

    m_testResultText = new QTextEdit(this);
    m_testResultText->setReadOnly(true);
    m_testResultText->setMinimumHeight(100);
    m_testResultText->setMaximumHeight(120);
    m_testResultText->setPlaceholderText("Classification results will appear here...");
    m_testResultText->setStyleSheet("font-family: monospace; font-size: 10px;");
    mlTestLayout->addWidget(m_testResultText);

    tabLayout->addWidget(mlTestGroup);
    tabLayout->addStretch();

    connect(m_browseModelButton, &QPushButton::clicked, this, &MLClassificationTab::onBrowseModel);
    connect(m_useDefaultModelButton, &QPushButton::clicked, this, &MLClassificationTab::onUseDefaultModel);
    connect(m_testButton, &QPushButton::clicked, this, &MLClassificationTab::onTest);
}

void MLClassificationTab::load(const AppConfig& config)
{
    m_deleteMaybeSlidesCheckBox->setChecked(config.mlDeleteMaybeSlides);

    if (config.mlModelPath.startsWith(":/")) {
        m_modelPathEdit->clear();
        m_modelPathEdit->setPlaceholderText("Using built-in model");
    } else {
        m_modelPathEdit->setText(config.mlModelPath);
    }

    m_notSlideRangeSlider->setUpperValue(static_cast<int>(config.mlNotSlideHighThreshold * 100));
    m_notSlideRangeSlider->setLowerValue(static_cast<int>(config.mlNotSlideLowThreshold * 100));
    m_maybeSlideRangeSlider->setUpperValue(static_cast<int>(config.mlMaybeSlideHighThreshold * 100));
    m_maybeSlideRangeSlider->setLowerValue(static_cast<int>(config.mlMaybeSlideLowThreshold * 100));
    m_slideMaxThresholdSlider->setValue(static_cast<int>(config.mlSlideMaxThreshold * 100));
}

void MLClassificationTab::store(AppConfig& config) const
{
    config.mlDeleteMaybeSlides = m_deleteMaybeSlidesCheckBox->isChecked();

    config.mlModelPath = m_modelPathEdit->text().isEmpty()
        ? QString(kDefaultModel)
        : m_modelPathEdit->text();

    config.mlNotSlideHighThreshold = m_notSlideRangeSlider->upperValue() / 100.0f;
    config.mlNotSlideLowThreshold = m_notSlideRangeSlider->lowerValue() / 100.0f;
    config.mlMaybeSlideHighThreshold = m_maybeSlideRangeSlider->upperValue() / 100.0f;
    config.mlMaybeSlideLowThreshold = m_maybeSlideRangeSlider->lowerValue() / 100.0f;
    config.mlSlideMaxThreshold = m_slideMaxThresholdSlider->value() / 100.0f;
}

void MLClassificationTab::onBrowseModel()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Select ONNX Model File",
        QString(),
        "ONNX Models (*.onnx);;All Files (*)");

    if (!fileName.isEmpty()) {
        m_modelPathEdit->setText(fileName);
    }
}

void MLClassificationTab::onUseDefaultModel()
{
    m_modelPathEdit->clear();
    m_modelPathEdit->setPlaceholderText("Using built-in model");
}

void MLClassificationTab::onTest()
{
    QString imagePath = QFileDialog::getOpenFileName(this,
        "Select Image to Test",
        QString(),
        "Images (*.jpg *.jpeg *.png *.bmp);;All Files (*)");

    if (imagePath.isEmpty()) {
        return;
    }

    m_testResultText->clear();
    m_testResultText->append("Testing ML Classification...");
    m_testResultText->append(QString("Image: %1").arg(QFileInfo(imagePath).fileName()));
    m_testResultText->append("----------------------------------------");

    QString modelPath = m_modelPathEdit->text();
    if (modelPath.isEmpty()) {
        modelPath = kDefaultModel;
    }

    float notSlideHighThreshold = m_notSlideRangeSlider->upperValue() / 100.0f;
    float notSlideLowThreshold = m_notSlideRangeSlider->lowerValue() / 100.0f;
    float maybeSlideHighThreshold = m_maybeSlideRangeSlider->upperValue() / 100.0f;
    float maybeSlideLowThreshold = m_maybeSlideRangeSlider->lowerValue() / 100.0f;
    float slideMaxThreshold = m_slideMaxThresholdSlider->value() / 100.0f;

    bool deleteMaybeSlides = m_deleteMaybeSlidesCheckBox->isChecked();

    MLClassifier classifier(modelPath, MLClassifier::ExecutionProvider::Auto);

    if (!classifier.isInitialized()) {
        m_testResultText->append("ERROR: Failed to initialize ML classifier");
        m_testResultText->append(QString("Reason: %1").arg(classifier.getErrorMessage()));
        return;
    }

    m_testResultText->append(QString("Execution Provider: %1").arg(classifier.getActiveExecutionProvider()));
    m_testResultText->append("");

    ClassificationResult result = classifier.classifySingle(imagePath);

    if (result.error) {
        m_testResultText->append(QString("X ERROR: %1").arg(result.errorMessage));
        return;
    }

    m_testResultText->append("CLASSIFICATION RESULTS:");
    m_testResultText->append("----------------------------------------");
    m_testResultText->append(QString("Predicted Class: %1").arg(result.predictedClass));
    m_testResultText->append(QString("Confidence: %1 (%2%)")
        .arg(result.confidence, 0, 'f', 4)
        .arg(result.confidence * 100, 0, 'f', 2));
    m_testResultText->append("");

    m_testResultText->append("ALL CLASS PROBABILITIES:");
    m_testResultText->append("----------------------------------------");

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

        QString indicator = (className == result.predictedClass) ? " <- PREDICTED" : "";

        m_testResultText->append(QString("  %1: %2%%3")
            .arg(className)
            .arg(probability * 100, 0, 'f', 2)
            .arg(indicator));
    }

    // Decision logic + explanation (lives in MLClassifier, next to shouldKeepImage)
    m_testResultText->append("");
    m_testResultText->append("2-STAGE DECISION:");
    m_testResultText->append("----------------------------------------");

    MLClassifier::CategoryThresholds notSlideThresholds(notSlideHighThreshold, notSlideLowThreshold);
    MLClassifier::CategoryThresholds maybeSlideThresholds(maybeSlideHighThreshold, maybeSlideLowThreshold);

    const QStringList decisionLines = MLClassifier::explainDecision(
        result, notSlideThresholds, maybeSlideThresholds, slideMaxThreshold, deleteMaybeSlides);
    for (const QString& line : decisionLines) {
        m_testResultText->append(line);
    }

    m_testResultText->append("");
    m_testResultText->append("Test completed successfully!");
}
