#include "mainwindow.h"
#include "postprocessor.h"
#include "trashreviewdialog.h"
#include "pdfmakerdialog.h"
#include "trashmanager.h"
#include <QApplication>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QFrame>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Initialize backend components
    m_videoQueue = std::make_unique<VideoQueue>(this);
    m_processingThread = std::make_unique<ProcessingThread>(m_videoQueue.get(), this);
    m_configManager = std::make_unique<ConfigManager>(this);

    // Setup UI
    setupUI();

    // Load configuration
    loadConfiguration();

    // Connect signals
    connectSignals();

    // Setup UI update timer
    m_uiUpdateTimer = new QTimer(this);
    connect(m_uiUpdateTimer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_uiUpdateTimer->start(500); // Update every 500ms

    // Initial UI update
    updateControlButtons();
    updateQueueTable();

    setWindowTitle("AutoSlides Extractor v1.1.0");
    resize(960, 720);  // Double width for left/right split
    setMinimumSize(960, 700);  // Set current size as minimum size
}

MainWindow::~MainWindow()
{
    if (m_processingThread && m_processingThread->isRunning()) {
        m_processingThread->stopProcessing();
        m_processingThread->wait(5000);
    }
}

void MainWindow::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    // Main vertical layout
    QVBoxLayout* mainVerticalLayout = new QVBoxLayout(m_centralWidget);
    mainVerticalLayout->setSpacing(8);
    mainVerticalLayout->setContentsMargins(12, 12, 12, 12);

    // Horizontal layout for left and right panels (top section)
    m_mainLayout = new QHBoxLayout();
    m_mainLayout->setSpacing(8);

    // Setup left and right panels
    setupLeftPanel();
    setupRightPanel();

    mainVerticalLayout->addLayout(m_mainLayout, 0);  // No stretch - fixed height top section

    // Setup and add queue section (combined table - full width)
    setupQueueSection();
    mainVerticalLayout->addWidget(m_queueGroup, 1);  // Stretch factor to allow expansion

    // Setup and add progress section (full width, horizontal layout)
    setupProgressSection();
    mainVerticalLayout->addWidget(m_progressGroup, 0);  // No stretch - fixed height

    // Setup status section (full width at bottom)
    setupStatusSection();
    mainVerticalLayout->addWidget(m_statusGroup, 0);  // No stretch - keep status log at minimum height
}

void MainWindow::setupLeftPanel()
{
    m_leftPanel = new QWidget(this);
    m_leftLayout = new QVBoxLayout(m_leftPanel);
    m_leftLayout->setSpacing(8);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);

    // Simplified vertical layout - fixed height sections only
    setupOutputDirectorySection();  // Output directory - fixed height
    setupVideoInputSection();       // Add Videos + Remove button - fixed height
    setupControlSection();          // Start/Pause/Reset + Settings/PDF/Trash buttons - fixed height

    // Add stretch to push everything to the top and align with right panel
    m_leftLayout->addStretch(1);

    // Copyright label centered in the remaining space
    QLabel* copyrightLabel = new QLabel(
        "Copyright (c) bit-admin. <a href=\"https://github.com/bit-admin/AutoSlides-Extractor\">GitHub Repo</a>",
        m_leftPanel);
    copyrightLabel->setOpenExternalLinks(true);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet("color: #888; font-size: 11px;");
    m_leftLayout->addWidget(copyrightLabel);

    m_leftLayout->addStretch(1);

    m_mainLayout->addWidget(m_leftPanel, 1);  // Allow left panel to expand
}

void MainWindow::setupRightPanel()
{
    m_rightPanel = new QWidget(this);
    m_rightLayout = new QVBoxLayout(m_rightPanel);
    m_rightLayout->setSpacing(8);
    m_rightLayout->setContentsMargins(0, 0, 0, 0);

    setupPostProcessingSection();  // Fixed height

    // Add stretch to push everything to the top and align with left panel
    m_rightLayout->addStretch(1);

    m_mainLayout->addWidget(m_rightPanel, 1);  // Allow right panel to expand
}

void MainWindow::setupOutputDirectorySection()
{
    m_outputGroup = new QGroupBox("Output Directory", m_leftPanel);
    QHBoxLayout* layout = new QHBoxLayout(m_outputGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_outputDirEdit = new QLineEdit(m_leftPanel);
    m_outputDirBrowseButton = new QPushButton("Browse", m_leftPanel);
    m_outputDirBrowseButton->setFixedWidth(70);

    layout->addWidget(m_outputDirEdit, 1);
    layout->addWidget(m_outputDirBrowseButton);

    m_leftLayout->addWidget(m_outputGroup, 0);  // No vertical stretch - fixed height
}

void MainWindow::setupVideoInputSection()
{
    m_videoInputGroup = new QGroupBox("Video Input", m_leftPanel);
    QHBoxLayout* layout = new QHBoxLayout(m_videoInputGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_addVideosButton = new QPushButton("Add Videos", m_leftPanel);
    m_removeVideoButton = new QPushButton("Remove Selected", m_leftPanel);

    layout->addWidget(m_addVideosButton, 1);
    layout->addWidget(m_removeVideoButton, 1);

    m_leftLayout->addWidget(m_videoInputGroup, 0);  // No vertical stretch - fixed height
}

void MainWindow::setupControlSection()
{
    m_controlGroup = new QGroupBox("Control", m_leftPanel);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_controlGroup);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(6);

    // Row 1: Start, Pause, Reset
    QHBoxLayout* row1Layout = new QHBoxLayout();
    row1Layout->setSpacing(8);

    m_startButton = new QPushButton("Start", m_leftPanel);
    m_pauseButton = new QPushButton("Pause", m_leftPanel);
    m_resetButton = new QPushButton("Reset", m_leftPanel);

    row1Layout->addWidget(m_startButton, 1);
    row1Layout->addWidget(m_pauseButton, 1);
    row1Layout->addWidget(m_resetButton, 1);

    mainLayout->addLayout(row1Layout);

    // Divider between rows
    QFrame* divider = new QFrame(m_leftPanel);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(divider);

    // Row 2: Settings, PDF Maker, Review Trash
    QHBoxLayout* row2Layout = new QHBoxLayout();
    row2Layout->setSpacing(8);

    m_settingsButton = new QPushButton("Settings", m_leftPanel);
    m_settingsButton->setToolTip("Open settings dialog");
    m_pdfMakerButton = new QPushButton("PDF Maker", m_leftPanel);
    m_pdfMakerButton->setToolTip("Browse slide folders and create PDF");
    m_reviewTrashButton = new QPushButton("Review Trash", m_leftPanel);
    m_reviewTrashButton->setToolTip("Review and restore slides that were moved to trash");

    row2Layout->addWidget(m_settingsButton, 1);
    row2Layout->addWidget(m_pdfMakerButton, 1);
    row2Layout->addWidget(m_reviewTrashButton, 1);

    mainLayout->addLayout(row2Layout);

    m_leftLayout->addWidget(m_controlGroup, 0);  // No vertical stretch - fixed height
}

void MainWindow::setupQueueSection()
{
    m_queueGroup = new QGroupBox("Processing Queue", m_centralWidget);
    QVBoxLayout* layout = new QVBoxLayout(m_queueGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_queueTable = new QTableWidget(m_centralWidget);
    m_queueTable->setColumnCount(7);
    QStringList headers = {"Filename", "Status", "Time (s)", "Extracted", "- pHash", "- ML", "Saved"};
    m_queueTable->setHorizontalHeaderLabels(headers);
    m_queueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queueTable->setAlternatingRowColors(true);

    // Enable scrolling for the table
    m_queueTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_queueTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Optimize column widths for full-width table
    QHeaderView* header = m_queueTable->horizontalHeader();
    header->setSectionResizeMode(COL_FILENAME, QHeaderView::Stretch);  // Filename: flexible
    header->setSectionResizeMode(COL_STATUS, QHeaderView::Fixed);      // Status: fixed width
    header->setSectionResizeMode(COL_TIME, QHeaderView::Fixed);        // Time: fixed width
    header->setSectionResizeMode(COL_EXTRACTED, QHeaderView::Fixed);   // Extracted: fixed width
    header->setSectionResizeMode(COL_PHASH, QHeaderView::Fixed);       // - pHash: fixed width
    header->setSectionResizeMode(COL_ML, QHeaderView::Fixed);          // - ML: fixed width
    header->setSectionResizeMode(COL_SAVED, QHeaderView::Fixed);       // Saved: fixed width

    // Column widths
    m_queueTable->setColumnWidth(COL_STATUS, 120);     // Status: 120px
    m_queueTable->setColumnWidth(COL_TIME, 70);        // Time: 70px
    m_queueTable->setColumnWidth(COL_EXTRACTED, 75);   // Extracted: 75px
    m_queueTable->setColumnWidth(COL_PHASH, 60);       // - pHash: 60px
    m_queueTable->setColumnWidth(COL_ML, 50);          // - ML: 50px
    m_queueTable->setColumnWidth(COL_SAVED, 55);       // Saved: 55px

    m_queueTable->setMinimumHeight(140);

    layout->addWidget(m_queueTable);
}

void MainWindow::setupProgressSection()
{
    m_progressGroup = new QGroupBox("Progress", m_centralWidget);
    QHBoxLayout* mainLayout = new QHBoxLayout(m_progressGroup);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(16);

    // Left side: Frame Extraction Progress
    QVBoxLayout* frameLayout = new QVBoxLayout();
    frameLayout->setSpacing(3);

    m_frameExtractionLabel = new QLabel("Frame Extraction", m_centralWidget);
    m_frameExtractionProgressBar = new QProgressBar(m_centralWidget);
    m_frameExtractionProgressBar->setRange(0, 100);
    m_frameExtractionProgressBar->setValue(0);
    m_frameExtractionProgressBar->setMaximumHeight(14);
    m_frameExtractionPercentLabel = new QLabel("0%", m_centralWidget);
    m_frameExtractionPercentLabel->setMinimumWidth(35);

    frameLayout->addWidget(m_frameExtractionLabel);
    QHBoxLayout* frameProgressLayout = new QHBoxLayout();
    frameProgressLayout->setSpacing(6);
    frameProgressLayout->addWidget(m_frameExtractionProgressBar);
    frameProgressLayout->addWidget(m_frameExtractionPercentLabel);
    frameLayout->addLayout(frameProgressLayout);

    mainLayout->addLayout(frameLayout, 1);

    // Right side: Slide Detection Progress
    QVBoxLayout* slideLayout = new QVBoxLayout();
    slideLayout->setSpacing(3);

    m_slideProcessingLabel = new QLabel("Slide Detection", m_centralWidget);
    m_slideProcessingProgressBar = new QProgressBar(m_centralWidget);
    m_slideProcessingProgressBar->setRange(0, 100);
    m_slideProcessingProgressBar->setValue(0);
    m_slideProcessingProgressBar->setMaximumHeight(14);
    m_slideProcessingPercentLabel = new QLabel("0%", m_centralWidget);
    m_slideProcessingPercentLabel->setMinimumWidth(35);

    slideLayout->addWidget(m_slideProcessingLabel);
    QHBoxLayout* slideProgressLayout = new QHBoxLayout();
    slideProgressLayout->setSpacing(6);
    slideProgressLayout->addWidget(m_slideProcessingProgressBar);
    slideProgressLayout->addWidget(m_slideProcessingPercentLabel);
    slideLayout->addLayout(slideProgressLayout);

    mainLayout->addLayout(slideLayout, 1);
}

void MainWindow::setupStatusSection()
{
    m_statusGroup = new QGroupBox("Status Log", m_centralWidget);
    QVBoxLayout* layout = new QVBoxLayout(m_statusGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_statusText = new QTextEdit(m_centralWidget);
    m_statusText->setMinimumHeight(120);  // Increased from 100
    // Removed maximum height to allow expansion
    m_statusText->setReadOnly(true);

    layout->addWidget(m_statusText);

    // Don't add to left layout anymore - will be added to main layout below panels
}

void MainWindow::setupPostProcessingSection()
{
    m_postProcessingGroup = new QGroupBox("Post-Processing", m_rightPanel);
    m_postProcessingGroup->setMinimumHeight(180);  // Ensure enough height for all content
    QVBoxLayout* layout = new QVBoxLayout(m_postProcessingGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    // First row: Enable Post-Processing, Manual Post-Processing
    QHBoxLayout* topButtonLayout = new QHBoxLayout();
    topButtonLayout->setSpacing(8);

    m_enablePostProcessingCheckBox = new QCheckBox("Enable Post-Processing", m_rightPanel);
    m_enablePostProcessingCheckBox->setChecked(true);
    topButtonLayout->addWidget(m_enablePostProcessingCheckBox);

    topButtonLayout->addStretch();

    m_manualPostProcessingButton = new QPushButton("Manual Post-Processing", m_rightPanel);
    m_manualPostProcessingButton->setToolTip("Manually post-process a folder of images");
    topButtonLayout->addWidget(m_manualPostProcessingButton);

    layout->addLayout(topButtonLayout);

    // Separator
    QFrame* line = new QFrame(m_rightPanel);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Delete redundant checkbox with help text
    m_deleteRedundantCheckBox = new QCheckBox("Delete Redundant Slides with pHash", m_rightPanel);
    m_deleteRedundantCheckBox->setChecked(true);
    layout->addWidget(m_deleteRedundantCheckBox);

    QLabel* redundantHelpLabel = new QLabel("Remove duplicate slides by keeping only the first occurrence.", m_rightPanel);
    redundantHelpLabel->setWordWrap(true);
    redundantHelpLabel->setStyleSheet("color: #666; font-size: 11px; margin-left: 20px;");
    layout->addWidget(redundantHelpLabel);

    // Compare with excluded list checkbox with help text
    m_compareExcludedCheckBox = new QCheckBox("Compare with pHash Excluded List", m_rightPanel);
    m_compareExcludedCheckBox->setChecked(true);
    layout->addWidget(m_compareExcludedCheckBox);

    QLabel* excludedHelpLabel = new QLabel("Remove slides matching predefined exclusion patterns.", m_rightPanel);
    excludedHelpLabel->setWordWrap(true);
    excludedHelpLabel->setStyleSheet("color: #666; font-size: 11px; margin-left: 20px;");
    layout->addWidget(excludedHelpLabel);

    // ML Classification separator
    QFrame* mlLine = new QFrame(m_rightPanel);
    mlLine->setFrameShape(QFrame::HLine);
    mlLine->setFrameShadow(QFrame::Sunken);
    layout->addWidget(mlLine);

    // ML Classification checkbox with help text
    m_enableMLClassificationCheckBox = new QCheckBox("Enable ML Classification", m_rightPanel);
#ifdef ONNX_AVAILABLE
    m_enableMLClassificationCheckBox->setChecked(true);
#else
    m_enableMLClassificationCheckBox->setChecked(false);
    m_enableMLClassificationCheckBox->setEnabled(false);
    m_enableMLClassificationCheckBox->setToolTip("ONNX Runtime not available - ML classification disabled");
#endif
    layout->addWidget(m_enableMLClassificationCheckBox);

    QLabel* mlHelpLabel = new QLabel("Use AI model to classify and remove non-slide images. AI can make mistakes.", m_rightPanel);
    mlHelpLabel->setWordWrap(true);
    mlHelpLabel->setStyleSheet("color: #666; font-size: 11px; margin-left: 20px;");
    layout->addWidget(mlHelpLabel);

    // Configuration instructions
    QLabel* mlConfigLabel = new QLabel("You can download the latest model from: "
                                       "<a href=\"https://github.com/bit-admin/slide-classifier/releases\">GitHub Release</a>",
                                       m_rightPanel);
    mlConfigLabel->setWordWrap(true);
    mlConfigLabel->setOpenExternalLinks(true);
    mlConfigLabel->setStyleSheet("color: #555; font-size: 11px; margin-left: 20px;");
    layout->addWidget(mlConfigLabel);

    // Model info label
    m_mlModelInfoLabel = new QLabel(m_rightPanel);
    m_mlModelInfoLabel->setWordWrap(true);
    m_mlModelInfoLabel->setStyleSheet("color: #888; font-size: 10px; margin-left: 20px; font-style: italic;");
    layout->addWidget(m_mlModelInfoLabel);
    updateMLModelInfo();

    m_rightLayout->addWidget(m_postProcessingGroup, 0);  // No vertical stretch - fixed height
}

void MainWindow::connectSignals()
{
    // Video input signals
    connect(m_addVideosButton, &QPushButton::clicked, this, &MainWindow::onAddVideosClicked);
    connect(m_removeVideoButton, &QPushButton::clicked, this, &MainWindow::onRemoveVideoClicked);

    // Output directory signals
    connect(m_outputDirBrowseButton, &QPushButton::clicked, this, &MainWindow::onOutputDirBrowseClicked);
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, &MainWindow::saveConfiguration);

    // Control signals
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(m_resetButton, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(m_pdfMakerButton, &QPushButton::clicked, this, &MainWindow::onPdfMakerClicked);
    connect(m_reviewTrashButton, &QPushButton::clicked, this, &MainWindow::onReviewTrashClicked);

    // Processing thread signals
    connect(m_processingThread.get(), &ProcessingThread::processingStarted, this, &MainWindow::onProcessingStarted);
    connect(m_processingThread.get(), &ProcessingThread::processingPaused, this, &MainWindow::onProcessingPaused);
    connect(m_processingThread.get(), &ProcessingThread::processingStopped, this, &MainWindow::onProcessingStopped);
    connect(m_processingThread.get(), &ProcessingThread::videoProcessingStarted, this, &MainWindow::onVideoProcessingStarted);
    connect(m_processingThread.get(), &ProcessingThread::videoProcessingCompleted, this, &MainWindow::onVideoProcessingCompleted);
    connect(m_processingThread.get(), &ProcessingThread::videoProcessingError, this, &MainWindow::onVideoProcessingError);
    connect(m_processingThread.get(), &ProcessingThread::frameExtractionProgress, this, &MainWindow::onFrameExtractionProgress);
    connect(m_processingThread.get(), &ProcessingThread::ssimCalculationProgress, this, &MainWindow::onSSIMCalculationProgress);
    connect(m_processingThread.get(), &ProcessingThread::slideDetectionProgress, this, &MainWindow::onSlideDetectionProgress);
    connect(m_processingThread.get(), &ProcessingThread::videoInfoLogged, this, &MainWindow::onVideoInfoLogged);

    // Video queue signals
    connect(m_videoQueue.get(), &VideoQueue::videoAdded, this, &MainWindow::onVideoAdded);
    connect(m_videoQueue.get(), &VideoQueue::videoRemoved, this, &MainWindow::onVideoRemoved);
    connect(m_videoQueue.get(), &VideoQueue::statusChanged, this, &MainWindow::onStatusChanged);
    connect(m_videoQueue.get(), &VideoQueue::statisticsUpdated, this, &MainWindow::onStatisticsUpdated);
    connect(m_videoQueue.get(), &VideoQueue::queueCleared, this, &MainWindow::onQueueCleared);

    // Post-processing signals
    connect(m_enablePostProcessingCheckBox, &QCheckBox::toggled, this, &MainWindow::onEnablePostProcessingToggled);
    connect(m_deleteRedundantCheckBox, &QCheckBox::toggled, this, &MainWindow::saveConfiguration);
    connect(m_compareExcludedCheckBox, &QCheckBox::toggled, this, &MainWindow::saveConfiguration);
#ifdef ONNX_AVAILABLE
    connect(m_enableMLClassificationCheckBox, &QCheckBox::toggled, this, &MainWindow::saveConfiguration);
#endif
    connect(m_manualPostProcessingButton, &QPushButton::clicked, this, &MainWindow::onManualPostProcessingClicked);
}

void MainWindow::loadConfiguration()
{
    m_config = m_configManager->loadConfig();
    // Update output directory in main window
    m_outputDirEdit->setText(m_config.outputDirectory);

    // Update post-processing checkboxes
    m_enablePostProcessingCheckBox->setChecked(m_config.enablePostProcessing);
    m_deleteRedundantCheckBox->setChecked(m_config.deleteRedundant);
    m_compareExcludedCheckBox->setChecked(m_config.compareExcluded);

#ifdef ONNX_AVAILABLE
    m_enableMLClassificationCheckBox->setChecked(m_config.enableMLClassification);
#endif

    // Update ML model info
    updateMLModelInfo();

    // Update checkbox enabled states
    onEnablePostProcessingToggled();
}

void MainWindow::saveConfiguration()
{
    // Save output directory from main window
    m_config.outputDirectory = m_outputDirEdit->text();

    // Save post-processing settings
    m_config.enablePostProcessing = m_enablePostProcessingCheckBox->isChecked();
    m_config.deleteRedundant = m_deleteRedundantCheckBox->isChecked();
    m_config.compareExcluded = m_compareExcludedCheckBox->isChecked();

#ifdef ONNX_AVAILABLE
    m_config.enableMLClassification = m_enableMLClassificationCheckBox->isChecked();
#endif

    m_configManager->saveConfig(m_config);
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_processingThread && m_processingThread->isProcessing()) {
        m_processingThread->forceStop();
        m_processingThread->wait(5000);
    }
    saveConfiguration();
    event->accept();
}

// Slot implementations
void MainWindow::onAddVideosClicked()
{
    QStringList fileNames = QFileDialog::getOpenFileNames(this,
        "Select Video Files",
        QString(),
        "Video Files (*.mp4 *.avi *.mov *.mkv *.wmv *.flv *.webm);;All Files (*)");

    for (const QString& fileName : fileNames) {
        int index = m_videoQueue->addVideo(fileName);
        if (index >= 0) {
            m_statusText->append(QString("Added video: %1").arg(QFileInfo(fileName).fileName()));
        } else {
            m_statusText->append(QString("Failed to add video: %1").arg(QFileInfo(fileName).fileName()));
        }
    }
}

void MainWindow::onRemoveVideoClicked()
{
    int currentRow = m_queueTable->currentRow();
    if (currentRow >= 0) {
        VideoQueueItem* video = m_videoQueue->getVideo(currentRow);
        if (video) {
            QString fileName = video->fileName;
            if (m_videoQueue->removeVideo(currentRow)) {
                m_statusText->append(QString("Removed video: %1").arg(fileName));
            } else {
                m_statusText->append(QString("Cannot remove video (currently processing): %1").arg(fileName));
            }
        }
    }
}

void MainWindow::onStartClicked()
{
    if (m_videoQueue->isEmpty()) {
        return;
    }

    // Reset any error videos back to queued status for retry
    m_videoQueue->resetErrorVideos();

    // Save output directory and update processing thread with current config
    saveConfiguration();
    m_processingThread->updateConfig(m_config);
    m_processingThread->startProcessing();
}

void MainWindow::onPauseClicked()
{
    m_processingThread->forceStop();
}

void MainWindow::onResetClicked()
{
    if (m_processingThread->isProcessing()) {
        return;
    }

    m_videoQueue->clearCompleted();
    m_statusText->append("Queue reset - cleared completed and error items");

    // Update queue table to reflect cleared items
    updateQueueTable();
}

void MainWindow::onOutputDirBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        "Select Output Directory",
        m_outputDirEdit->text());

    if (!dir.isEmpty()) {
        m_outputDirEdit->setText(dir);
    }
}

void MainWindow::onSettingsClicked()
{
    SettingsDialog dialog(m_config, m_configManager.get(), 0, this);
    connect(&dialog, &SettingsDialog::statusMessage, this, [this](const QString& message) {
        m_statusText->append(message);
    });
    if (dialog.exec() == QDialog::Accepted) {
        m_config = dialog.getConfig();
        m_configManager->saveConfig(m_config);
        // Update processing thread with new configuration
        m_processingThread->updateConfig(m_config);
        m_statusText->append("Settings updated");
    }
}

// Processing thread slots
void MainWindow::onProcessingStarted()
{
    m_statusText->append("Processing started");
    updateControlButtons();
}

void MainWindow::onProcessingPaused()
{
    m_statusText->append("Processing paused");
    updateControlButtons();
}

void MainWindow::onProcessingStopped()
{
    m_statusText->append("Processing stopped");
    updateControlButtons();
    resetProgressBars(-1);
}

void MainWindow::onVideoProcessingStarted(int videoIndex)
{
    VideoQueueItem* video = m_videoQueue->getVideo(videoIndex);
    if (video) {
        m_statusText->append(QString("Started processing: %1").arg(video->fileName));
        resetProgressBars(videoIndex);
    }
}

void MainWindow::onVideoProcessingCompleted(int videoIndex, int slidesExtracted)
{
    VideoQueueItem* video = m_videoQueue->getVideo(videoIndex);
    if (video) {
        m_statusText->append(QString("Completed: %1 (%2 slides extracted)")
                           .arg(video->fileName).arg(slidesExtracted));

        // Perform post-processing if enabled
        if (m_config.enablePostProcessing) {
            performPostProcessing(videoIndex);
        }
    }
}

void MainWindow::onVideoProcessingError(int videoIndex, const QString& error)
{
    VideoQueueItem* video = m_videoQueue->getVideo(videoIndex);
    if (video) {
        m_statusText->append(QString("Error processing %1: %2")
                           .arg(video->fileName).arg(error));
    }
}

void MainWindow::onFrameExtractionProgress(int videoIndex, double percentage)
{
    updateFrameExtractionProgress(videoIndex, percentage);
}

void MainWindow::onSSIMCalculationProgress(int videoIndex, int current, int total)
{
    double percentage = (total > 0) ? (double)current / total * 100.0 : 0.0;
    updateSlideProcessingProgress(videoIndex, percentage);
}

void MainWindow::onSlideDetectionProgress(int videoIndex, int current, int total)
{
    double percentage = (total > 0) ? (double)current / total * 100.0 : 0.0;
    updateSlideProcessingProgress(videoIndex, percentage);
}

void MainWindow::onVideoInfoLogged(int videoIndex, const QString& info)
{
    Q_UNUSED(videoIndex)
    m_statusText->append(info);
}

// Video queue slots
void MainWindow::onVideoAdded(int index)
{
    Q_UNUSED(index)
    updateQueueTable();
    updateControlButtons();
}

void MainWindow::onVideoRemoved(int index)
{
    Q_UNUSED(index)
    updateQueueTable();
    updateControlButtons();
}

void MainWindow::onStatusChanged(int index, ProcessingStatus status)
{
    Q_UNUSED(index)
    Q_UNUSED(status)
    updateQueueTable();
}

void MainWindow::onStatisticsUpdated(int index)
{
    Q_UNUSED(index)
    updateQueueTable();
}

void MainWindow::onQueueCleared()
{
    updateQueueTable();
    updateControlButtons();
}

void MainWindow::updateUI()
{
    // This is called periodically to update the UI
    updateControlButtons();
}

void MainWindow::updateControlButtons()
{
    bool isProcessing = m_processingThread->isProcessing();
    bool hasVideos = !m_videoQueue->isEmpty();
    bool hasQueuedVideos = (m_videoQueue->getNextToProcess() >= 0);

    m_startButton->setEnabled(!isProcessing && hasQueuedVideos);
    m_pauseButton->setEnabled(isProcessing);
    m_resetButton->setEnabled(!isProcessing && hasVideos);

    // Update remove button
    int currentRow = m_queueTable->currentRow();
    bool canRemove = false;
    if (currentRow >= 0) {
        VideoQueueItem* video = m_videoQueue->getVideo(currentRow);
        if (video) {
            ProcessingStatus status = video->status;
            canRemove = (status != ProcessingStatus::FFmpegHandling &&
                        status != ProcessingStatus::SSIMCalculating &&
                        status != ProcessingStatus::ImageProcessing);
        }
    }
    m_removeVideoButton->setEnabled(canRemove);
}

void MainWindow::updateQueueTable()
{
    const auto& videos = m_videoQueue->getAllVideos();
    m_queueTable->setRowCount(static_cast<int>(videos.size()));

    bool ppEnabled = m_config.enablePostProcessing;

    for (int i = 0; i < static_cast<int>(videos.size()); ++i) {
        const VideoQueueItem& video = videos[i];

        // Filename
        QTableWidgetItem* filenameItem = new QTableWidgetItem(video.fileName);
        m_queueTable->setItem(i, COL_FILENAME, filenameItem);

        // Status
        QTableWidgetItem* statusItem = new QTableWidgetItem(VideoQueue::getStatusString(video.status));
        m_queueTable->setItem(i, COL_STATUS, statusItem);

        // Time
        QString timeStr = (video.processingTimeSeconds > 0) ?
                         QString::number(video.processingTimeSeconds, 'f', 1) : "-";
        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        m_queueTable->setItem(i, COL_TIME, timeItem);

        // Extracted
        QTableWidgetItem* extractedItem = new QTableWidgetItem(QString::number(video.extractedSlides));
        m_queueTable->setItem(i, COL_EXTRACTED, extractedItem);

        // - pHash (show "-" if PP disabled or not completed yet)
        QString pHashStr;
        if (!ppEnabled) {
            pHashStr = "-";
        } else if (video.status == ProcessingStatus::Completed) {
            pHashStr = QString::number(video.movedByPHash);
        } else {
            pHashStr = "-";
        }
        QTableWidgetItem* pHashItem = new QTableWidgetItem(pHashStr);
        m_queueTable->setItem(i, COL_PHASH, pHashItem);

        // - ML (show "-" if PP disabled or not completed yet)
        QString mlStr;
        if (!ppEnabled) {
            mlStr = "-";
        } else if (video.status == ProcessingStatus::Completed) {
            mlStr = QString::number(video.movedByML);
        } else {
            mlStr = "-";
        }
        QTableWidgetItem* mlItem = new QTableWidgetItem(mlStr);
        m_queueTable->setItem(i, COL_ML, mlItem);

        // Saved (extracted - removed, show "-" if PP disabled or not completed)
        QString savedStr;
        if (!ppEnabled) {
            savedStr = "-";
        } else if (video.status == ProcessingStatus::Completed) {
            int saved = video.extractedSlides - video.movedToTrash;
            savedStr = QString::number(saved);
        } else {
            savedStr = "-";
        }
        QTableWidgetItem* savedItem = new QTableWidgetItem(savedStr);
        m_queueTable->setItem(i, COL_SAVED, savedItem);

        // Color coding based on status - adapted for dark/light themes
        QColor rowColor;
        QPalette palette = this->palette();
        QColor baseColor = palette.color(QPalette::Base);
        QColor alternateColor = palette.color(QPalette::AlternateBase);

        switch (video.status) {
            case ProcessingStatus::Queued:
                // Use slightly darker/lighter than base depending on theme
                rowColor = alternateColor;
                break;
            case ProcessingStatus::FFmpegHandling:
            case ProcessingStatus::SSIMCalculating:
            case ProcessingStatus::ImageProcessing:
                // Yellow tint - adapt to theme
                if (palette.color(QPalette::WindowText).lightness() > 128) {
                    // Dark theme - use darker yellow
                    rowColor = QColor(80, 80, 20);
                } else {
                    // Light theme - use light yellow
                    rowColor = QColor(255, 255, 200);
                }
                break;
            case ProcessingStatus::Completed:
                // Green tint - adapt to theme
                if (palette.color(QPalette::WindowText).lightness() > 128) {
                    // Dark theme - use darker green
                    rowColor = QColor(20, 80, 20);
                } else {
                    // Light theme - use light green
                    rowColor = QColor(200, 255, 200);
                }
                break;
            case ProcessingStatus::Error:
                // Red tint - adapt to theme
                if (palette.color(QPalette::WindowText).lightness() > 128) {
                    // Dark theme - use darker red
                    rowColor = QColor(80, 20, 20);
                } else {
                    // Light theme - use light red
                    rowColor = QColor(255, 200, 200);
                }
                break;
        }

        for (int col = 0; col < m_queueTable->columnCount(); ++col) {
            if (m_queueTable->item(i, col)) {
                m_queueTable->item(i, col)->setBackground(rowColor);
            }
        }
    }

    // Note: Column widths are set in setupQueueSection() and should not be auto-resized
}

void MainWindow::updateFrameExtractionProgress(int videoIndex, double percentage)
{
    // Update frame extraction progress
    if (percentage >= 0) {
        int intPercentage = static_cast<int>(percentage);
        // Only update if the integer percentage has changed to prevent shaking
        if (intPercentage != m_lastFrameExtractionProgress) {
            m_lastFrameExtractionProgress = intPercentage;
            m_frameExtractionProgressBar->setValue(intPercentage);
            m_frameExtractionPercentLabel->setText(QString("%1%").arg(intPercentage));
        }
    } else {
        m_lastFrameExtractionProgress = -1;
        m_frameExtractionProgressBar->setValue(0);
        m_frameExtractionPercentLabel->setText("0%");
    }
}

void MainWindow::updateSlideProcessingProgress(int videoIndex, double percentage)
{
    // Update slide processing progress
    if (percentage >= 0) {
        int intPercentage = static_cast<int>(percentage);
        // Only update if the integer percentage has changed to prevent shaking
        if (intPercentage != m_lastSlideProcessingProgress) {
            m_lastSlideProcessingProgress = intPercentage;
            m_slideProcessingProgressBar->setValue(intPercentage);
            m_slideProcessingPercentLabel->setText(QString("%1%").arg(intPercentage));
        }
    } else {
        m_lastSlideProcessingProgress = -1;
        m_slideProcessingProgressBar->setValue(0);
        m_slideProcessingPercentLabel->setText("0%");
    }
}

void MainWindow::resetProgressBars(int videoIndex)
{
    // Reset both progress bars
    m_lastFrameExtractionProgress = -1;
    m_lastSlideProcessingProgress = -1;
    m_frameExtractionProgressBar->setValue(0);
    m_frameExtractionPercentLabel->setText("0%");
    m_slideProcessingProgressBar->setValue(0);
    m_slideProcessingPercentLabel->setText("0%");
}

void MainWindow::updateMLModelInfo()
{
#ifdef ONNX_AVAILABLE
    QString modelPath = m_config.mlModelPath;
    QString modelName;

    if (modelPath.startsWith(":/")) {
        modelName = "Built-in - MobileNetV4 (slide_classifier_mobilenetv4_v1.onnx)";
    } else {
        QFileInfo fileInfo(modelPath);
        modelName = "Custom - " + fileInfo.fileName();
    }

    m_mlModelInfoLabel->setText(QString("Currently using: %1").arg(modelName));
#else
    m_mlModelInfoLabel->setText("ONNX Runtime not available - ML classification disabled");
#endif
}

void MainWindow::performPostProcessing(int videoIndex)
{
    if (!m_config.enablePostProcessing) {
        return;
    }

    VideoQueueItem* video = m_videoQueue->getVideo(videoIndex);
    if (!video || video->outputDirectory.isEmpty()) {
        return;
    }

    m_statusText->append(QString("Starting post-processing for: %1").arg(video->fileName));

    // Load exclusion list
    QList<ExclusionEntry> exclusionList = m_configManager->loadExclusionList();

    // Create post-processor
    PostProcessor processor;

    // Connect to processor signals for ML classification logging
    connect(&processor, &PostProcessor::imageMovedToTrash, this, [this](const QString& filePath, const QString& reason) {
        if (reason.startsWith("ML:")) {
            QFileInfo fileInfo(filePath);
            m_statusText->append(QString("  ML removed: %1 - %2").arg(fileInfo.fileName()).arg(reason));
        }
    });

    // Connect to ML classification started signal
    connect(&processor, &PostProcessor::mlClassificationStarted, this, [this](const QString& executionProvider) {
        m_statusText->append(QString("ML Classification: Enabled (Using %1)").arg(executionProvider));
    });

    // Connect to ML classification failed signal
    connect(&processor, &PostProcessor::mlClassificationFailed, this, [this](const QString& errorMessage) {
        m_statusText->append(QString("ML Classification: Failed - %1").arg(errorMessage));
    });

    // Process the directory
    PostProcessingResult result = processor.processDirectory(
        video->outputDirectory,
        m_config.deleteRedundant,
        m_config.compareExcluded,
        m_config.hammingThreshold,
        exclusionList,
        m_config.enableMLClassification,
        m_config.mlModelPath,
        m_config.mlNotSlideHighThreshold,
        m_config.mlNotSlideLowThreshold,
        m_config.mlMaybeSlideHighThreshold,
        m_config.mlMaybeSlideLowThreshold,
        m_config.mlSlideMaxThreshold,
        m_config.mlDeleteMaybeSlides,
        m_config.mlExecutionProvider,
        true,  // useApplicationTrash
        m_config.outputDirectory
    );

    // Update video statistics
    video->movedToTrash = result.totalRemoved;
    video->movedByPHash = result.removedByPHash;
    video->movedByML = result.removedByML;

    m_statusText->append(QString("Post-processing complete: %1 images moved to trash (%2 by pHash, %3 by ML)")
        .arg(result.totalRemoved)
        .arg(result.removedByPHash)
        .arg(result.removedByML));

    // Update queue table with post-processing results
    updateQueueTable();
}

void MainWindow::onEnablePostProcessingToggled()
{
    bool enabled = m_enablePostProcessingCheckBox->isChecked();
    m_deleteRedundantCheckBox->setEnabled(enabled);
    m_compareExcludedCheckBox->setEnabled(enabled);
#ifdef ONNX_AVAILABLE
    m_enableMLClassificationCheckBox->setEnabled(enabled);
#endif
    saveConfiguration();
}

void MainWindow::onManualPostProcessingClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        "Select Folder with Images",
        m_outputDirEdit->text());

    if (dir.isEmpty()) {
        return;
    }

    m_statusText->append(QString("Starting manual post-processing for: %1").arg(dir));

    // Load exclusion list
    QList<ExclusionEntry> exclusionList = m_configManager->loadExclusionList();

    // Create post-processor
    PostProcessor processor;

    // Connect to processor signals for ML classification logging
    connect(&processor, &PostProcessor::imageMovedToTrash, this, [this](const QString& filePath, const QString& reason) {
        if (reason.startsWith("ML:")) {
            QFileInfo fileInfo(filePath);
            m_statusText->append(QString("  ML removed: %1 - %2").arg(fileInfo.fileName()).arg(reason));
        }
    });

    // Connect to ML classification started signal
    connect(&processor, &PostProcessor::mlClassificationStarted, this, [this](const QString& executionProvider) {
        m_statusText->append(QString("ML Classification: Enabled (Using %1)").arg(executionProvider));
    });

    // Connect to ML classification failed signal
    connect(&processor, &PostProcessor::mlClassificationFailed, this, [this](const QString& errorMessage) {
        m_statusText->append(QString("ML Classification: Failed - %1").arg(errorMessage));
    });

    // Process the directory
    PostProcessingResult result = processor.processDirectory(
        dir,
        m_config.deleteRedundant,
        m_config.compareExcluded,
        m_config.hammingThreshold,
        exclusionList,
        m_config.enableMLClassification,
        m_config.mlModelPath,
        m_config.mlNotSlideHighThreshold,
        m_config.mlNotSlideLowThreshold,
        m_config.mlMaybeSlideHighThreshold,
        m_config.mlMaybeSlideLowThreshold,
        m_config.mlSlideMaxThreshold,
        m_config.mlDeleteMaybeSlides,
        m_config.mlExecutionProvider,
        true,  // useApplicationTrash
        m_config.outputDirectory
    );

    m_statusText->append(QString("Manual post-processing complete: %1 images moved to trash (%2 by pHash, %3 by ML)")
        .arg(result.totalRemoved)
        .arg(result.removedByPHash)
        .arg(result.removedByML));
}

void MainWindow::onReviewTrashClicked()
{
    // Open trash review dialog
    TrashReviewDialog dialog(m_config.outputDirectory, true /* emptyTrashToSystemTrash */, this);

    // Connect signals
    connect(&dialog, &TrashReviewDialog::statusMessage, this, [this](const QString& message) {
        m_statusText->append(message);
    });

    connect(&dialog, &TrashReviewDialog::filesRestored, this, [this](int count) {
        m_statusText->append(QString("Restored %1 file(s) from trash").arg(count));
    });

    connect(&dialog, &TrashReviewDialog::trashEmptied, this, [this]() {
        m_statusText->append("Trash emptied");
    });

    dialog.exec();
}

void MainWindow::onPdfMakerClicked()
{
    // Open PDF Maker dialog
    PdfMakerDialog dialog(m_config.outputDirectory, this);

    // Connect signals
    connect(&dialog, &PdfMakerDialog::statusMessage, this, [this](const QString& message) {
        m_statusText->append(message);
    });

    connect(&dialog, &PdfMakerDialog::filesDeleted, this, [this](int count) {
        m_statusText->append(QString("Deleted %1 file(s)").arg(count));
    });

    dialog.exec();
}

