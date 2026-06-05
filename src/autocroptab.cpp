#include "autocroptab.h"
#include "autocropdetector.h"
#include "autocroptestpreviewwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>

namespace {
QString autoCropBackendName(AutoCropResult::Backend backend)
{
    switch (backend) {
        case AutoCropResult::Backend::Canny: return "Canny";
        case AutoCropResult::Backend::Yolo: return "YOLO";
        case AutoCropResult::Backend::None:
        default: return "None";
    }
}
} // namespace

AutoCropTab::AutoCropTab(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(this);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === DETECTION MODE ===
    QGroupBox* modeGroup = new QGroupBox("Detection Mode", this);
    QGridLayout* modeLayout = new QGridLayout(modeGroup);
    modeLayout->setContentsMargins(12, 12, 12, 12);
    modeLayout->setSpacing(8);

    QLabel* modeLabel = new QLabel("Mode:", this);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem("Canny then YOLO", static_cast<int>(AutoCropMode::CannyThenYolo));
    m_modeCombo->addItem("Canny only", static_cast<int>(AutoCropMode::CannyOnly));
    m_modeCombo->addItem("YOLO only", static_cast<int>(AutoCropMode::YoloOnly));

    QLabel* yoloDescLabel = new QLabel(
        "Use YOLO model to detect slide areas automatically. AI can make mistakes.",
        this);
    yoloDescLabel->setWordWrap(true);
    yoloDescLabel->setStyleSheet("color: #666; font-size: 11px;");

    m_modelInfoLabel = new QLabel(this);
    m_modelInfoLabel->setWordWrap(true);
    m_modelInfoLabel->setStyleSheet("color: #888; font-size: 10px; font-style: italic;");

    QLabel* yoloReleaseLabel = new QLabel(
        "You can download the latest model from: "
        "<a href=\"https://github.com/bit-admin/slide-crop/releases\">GitHub Release</a>",
        this);
    yoloReleaseLabel->setWordWrap(true);
    yoloReleaseLabel->setOpenExternalLinks(true);
    yoloReleaseLabel->setStyleSheet("color: #555; font-size: 11px;");

    modeLayout->addWidget(modeLabel, 0, 0);
    modeLayout->addWidget(m_modeCombo, 0, 1);
    modeLayout->addWidget(yoloDescLabel, 1, 0, 1, 2);
    modeLayout->addWidget(yoloReleaseLabel, 2, 0, 1, 2);
    modeLayout->addWidget(m_modelInfoLabel, 3, 0, 1, 2);

    tabLayout->addWidget(modeGroup);

    // === CANNY ===
    QGroupBox* cannyGroup = new QGroupBox("Canny Edge Detection", this);
    QGridLayout* cannyLayout = new QGridLayout(cannyGroup);
    cannyLayout->setContentsMargins(12, 12, 12, 12);
    cannyLayout->setSpacing(8);

    QLabel* aspectLabel = new QLabel("Aspect Tolerance:", this);
    m_aspectToleranceSpin = new QDoubleSpinBox(this);
    m_aspectToleranceSpin->setRange(0.00, 0.50);
    m_aspectToleranceSpin->setDecimals(2);
    m_aspectToleranceSpin->setSingleStep(0.01);

    QLabel* cannyHelpLabel = new QLabel(
        "How much a candidate's aspect ratio may differ from 16:9 or 4:3 (relative). "
        "Higher values accept more rectangle shapes.",
        this);
    cannyHelpLabel->setWordWrap(true);
    cannyHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    cannyLayout->addWidget(aspectLabel, 0, 0);
    cannyLayout->addWidget(m_aspectToleranceSpin, 0, 1);
    cannyLayout->addWidget(cannyHelpLabel, 1, 0, 1, 2);

    tabLayout->addWidget(cannyGroup);

    // === YOLO ===
    QGroupBox* yoloGroup = new QGroupBox("YOLO Detector", this);
    QGridLayout* yoloLayout = new QGridLayout(yoloGroup);
    yoloLayout->setContentsMargins(12, 12, 12, 12);
    yoloLayout->setSpacing(8);

    QLabel* yoloConfLabel = new QLabel("Confidence Threshold:", this);
    m_yoloConfSpin = new QDoubleSpinBox(this);
    m_yoloConfSpin->setRange(0.0, 1.0);
    m_yoloConfSpin->setDecimals(2);
    m_yoloConfSpin->setSingleStep(0.05);

    QLabel* yoloPathLabel = new QLabel("Model Path:", this);
    QHBoxLayout* yoloPathLayout = new QHBoxLayout();
    m_yoloModelPathEdit = new QLineEdit(this);
    m_yoloModelPathEdit->setReadOnly(true);
    m_yoloModelPathEdit->setPlaceholderText("Using built-in model");
    m_yoloBrowseButton = new QPushButton("Browse...", this);
    m_yoloBrowseButton->setFixedWidth(80);
    m_yoloUseDefaultButton = new QPushButton("Use Default", this);
    m_yoloUseDefaultButton->setFixedWidth(100);
    yoloPathLayout->addWidget(yoloPathLabel);
    yoloPathLayout->addWidget(m_yoloModelPathEdit, 1);
    yoloPathLayout->addWidget(m_yoloBrowseButton);
    yoloPathLayout->addWidget(m_yoloUseDefaultButton);

    QLabel* yoloHelpLabel = new QLabel(
        "Lower confidence detects more candidate boxes (and more false positives).",
        this);
    yoloHelpLabel->setWordWrap(true);
    yoloHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    yoloLayout->addLayout(yoloPathLayout, 0, 0, 1, 2);

    yoloLayout->addWidget(yoloConfLabel, 1, 0);
    yoloLayout->addWidget(m_yoloConfSpin, 1, 1);
    yoloLayout->addWidget(yoloHelpLabel, 2, 0, 1, 2);

    tabLayout->addWidget(yoloGroup);

    // === TEST AUTO CROP ===
    QGroupBox* autoCropTestGroup = new QGroupBox("Test Auto Crop", this);
    QVBoxLayout* autoCropTestLayout = new QVBoxLayout(autoCropTestGroup);
    autoCropTestLayout->setContentsMargins(12, 12, 12, 12);
    autoCropTestLayout->setSpacing(8);

    QHBoxLayout* autoCropTestButtonLayout = new QHBoxLayout();
    autoCropTestButtonLayout->setSpacing(8);
    m_testCannyButton = new QPushButton("Test Canny...", this);
    m_testYoloButton = new QPushButton("Test YOLO...", this);
    autoCropTestButtonLayout->addWidget(m_testCannyButton, 1);
    autoCropTestButtonLayout->addWidget(m_testYoloButton, 1);

    m_testResultLabel = new QLabel("Select an image to preview the detected slide area.", this);
    m_testResultLabel->setWordWrap(true);
    m_testResultLabel->setStyleSheet("color: #666; font-size: 11px;");

    m_testPreview = new AutoCropTestPreviewWidget(this);
    m_testPreview->setToolTip("Click preview to open the annotated image in the default viewer.");
    m_testPreview->setOpenHandler([this](const QString& annotatedPath) {
        if (annotatedPath.isEmpty()) {
            emit statusMessage("Failed to create annotated preview image.");
        } else if (QDesktopServices::openUrl(QUrl::fromLocalFile(annotatedPath))) {
            emit statusMessage("Opened annotated preview image in the default viewer.");
        } else {
            emit statusMessage("Failed to open annotated preview image in the default viewer.");
        }
    });

    autoCropTestLayout->addLayout(autoCropTestButtonLayout);
    autoCropTestLayout->addWidget(m_testResultLabel);
    autoCropTestLayout->addWidget(m_testPreview, 1);

#ifndef ONNX_AVAILABLE
    m_testYoloButton->setEnabled(false);
    m_testYoloButton->setToolTip("YOLO test unavailable: ONNX Runtime not built.");
    m_testResultLabel->setText("Select an image to test Canny. YOLO test unavailable: ONNX Runtime not built.");
#endif

    tabLayout->addWidget(autoCropTestGroup);
    tabLayout->addStretch();

    connect(m_yoloBrowseButton, &QPushButton::clicked, this, [this]() {
        QString fileName = QFileDialog::getOpenFileName(this,
            "Select YOLO ONNX Model File",
            QString(),
            "ONNX Models (*.onnx);;All Files (*)");
        if (!fileName.isEmpty()) {
            m_yoloModelPathEdit->setText(fileName);
            QFileInfo fi(fileName);
            m_modelInfoLabel->setText(QString("Currently using: Custom - %1").arg(fi.fileName()));
        }
    });
    connect(m_yoloUseDefaultButton, &QPushButton::clicked, this, [this]() {
        m_yoloModelPathEdit->clear();
        m_yoloModelPathEdit->setPlaceholderText("Using built-in model");
        m_modelInfoLabel->setText("Currently using: Built-in - YOLOv8 (slide_detector_yolov8_v1.onnx)");
    });
    connect(m_testCannyButton, &QPushButton::clicked, this, [this]() {
        runTest(AutoCropMode::CannyOnly);
    });
    connect(m_testYoloButton, &QPushButton::clicked, this, [this]() {
        runTest(AutoCropMode::YoloOnly);
    });
}

void AutoCropTab::load(const AutoCropConfig& config)
{
    m_baseConfig = config;

    const int idx = m_modeCombo->findData(static_cast<int>(config.mode));
    m_modeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_aspectToleranceSpin->setValue(config.aspectTolerance);
    m_yoloConfSpin->setValue(config.yoloConfidenceThreshold);

    if (config.yoloModelPath.startsWith(":/")) {
        m_yoloModelPathEdit->clear();
        m_yoloModelPathEdit->setPlaceholderText("Using built-in model");
    } else {
        m_yoloModelPathEdit->setText(config.yoloModelPath);
    }

    QString modelName;
    if (config.yoloModelPath.startsWith(":/")) {
        modelName = "Built-in - YOLOv8 (slide_detector_yolov8_v1.onnx)";
    } else {
        QFileInfo fi(config.yoloModelPath);
        modelName = "Custom - " + fi.fileName();
    }
    m_modelInfoLabel->setText(QString("Currently using: %1").arg(modelName));
}

void AutoCropTab::store(AutoCropConfig& config) const
{
    config.mode = static_cast<AutoCropMode>(m_modeCombo->currentData().toInt());
    config.aspectTolerance = static_cast<float>(m_aspectToleranceSpin->value());
    config.yoloConfidenceThreshold = static_cast<float>(m_yoloConfSpin->value());

    const QString text = m_yoloModelPathEdit->text();
    config.yoloModelPath = text.isEmpty()
        ? QString(":/models/resources/models/slide_detector_yolov8_v1.onnx")
        : text;
}

AutoCropConfig AutoCropTab::currentTestConfig(AutoCropMode forcedMode) const
{
    AutoCropConfig cfg = m_baseConfig;
    cfg.mode = forcedMode;
    cfg.aspectTolerance = static_cast<float>(m_aspectToleranceSpin->value());
    cfg.yoloConfidenceThreshold = static_cast<float>(m_yoloConfSpin->value());

    const QString modelPath = m_yoloModelPathEdit->text();
    cfg.yoloModelPath = modelPath.isEmpty()
        ? QString(":/models/resources/models/slide_detector_yolov8_v1.onnx")
        : modelPath;

    return cfg;
}

void AutoCropTab::runTest(AutoCropMode forcedMode)
{
    const QString backendName = (forcedMode == AutoCropMode::YoloOnly) ? "YOLO" : "Canny";
    const QString imagePath = QFileDialog::getOpenFileName(this,
        QString("Select Image to Test %1 Auto Crop").arg(backendName),
        QString(),
        "Images (*.jpg *.jpeg *.png *.bmp);;All Files (*)");

    if (imagePath.isEmpty()) {
        return;
    }

    const QFileInfo imageInfo(imagePath);
    QPixmap previewPixmap(imagePath);

    m_testResultLabel->setText(QString("Testing %1 on %2...").arg(backendName, imageInfo.fileName()));
    m_testPreview->setPreview(previewPixmap, imagePath, QRect(), previewPixmap.isNull());
    emit statusMessage(QString("Testing %1 auto crop...").arg(backendName));

    QApplication::setOverrideCursor(Qt::WaitCursor);
    AutoCropDetector detector(currentTestConfig(forcedMode));
    const AutoCropResult result = detector.detect(imagePath);
    QApplication::restoreOverrideCursor();

    if (previewPixmap.isNull()) {
        m_testResultLabel->setText(QString("%1: failed to load preview for %2.")
            .arg(backendName, imageInfo.fileName()));
        emit statusMessage(QString("%1 auto crop test failed to load image.").arg(backendName));
        return;
    }

    if (result.isValid()) {
        m_testPreview->setPreview(previewPixmap, imagePath, result.bbox, false);
        m_testResultLabel->setText(QString("%1 | %2 | x=%3 y=%4 w=%5 h=%6 | %7 ms")
            .arg(imageInfo.fileName(),
                 autoCropBackendName(result.backend))
            .arg(result.bbox.x())
            .arg(result.bbox.y())
            .arg(result.bbox.width())
            .arg(result.bbox.height())
            .arg(result.durationMs, 0, 'f', 1));
        emit statusMessage(QString("%1 auto crop detected a slide area.").arg(autoCropBackendName(result.backend)));
        return;
    }

    m_testPreview->setPreview(previewPixmap, imagePath, QRect(), true);

    const QString reason = result.errorMessage.isEmpty()
        ? QString("No slide area detected.")
        : result.errorMessage;
    m_testResultLabel->setText(QString("%1 | %2 | %3")
        .arg(imageInfo.fileName(), backendName, reason));
    emit statusMessage(QString("%1 auto crop did not detect a slide area.").arg(backendName));
}
