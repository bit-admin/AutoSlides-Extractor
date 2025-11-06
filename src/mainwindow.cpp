#include "mainwindow.h"
#include "postprocessor.h"
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

    setWindowTitle("AutoSlides Extractor v1.0.1");
    resize(960, 600);  // Double width for left/right split
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

    // Horizontal layout for left and right panels
    m_mainLayout = new QHBoxLayout();
    m_mainLayout->setSpacing(8);

    // Setup left and right panels
    setupLeftPanel();
    setupRightPanel();

    mainVerticalLayout->addLayout(m_mainLayout);

    // Setup status section (full width at bottom)
    setupStatusSection();
    mainVerticalLayout->addWidget(m_statusGroup);
}

void MainWindow::setupLeftPanel()
{
    m_leftPanel = new QWidget(this);
    m_leftLayout = new QVBoxLayout(m_leftPanel);
    m_leftLayout->setSpacing(8);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);

    // Simplified vertical layout
    setupOutputDirectorySection();  // Only output directory
    setupVideoInputSection();       // Add Videos + Remove + Settings button
    setupQueueSection();            // Increased height video list
    setupControlSection();          // Start/Pause/Reset buttons
    setupProgressSection();         // Progress bars (compact)

    m_mainLayout->addWidget(m_leftPanel);
}

void MainWindow::setupRightPanel()
{
    m_rightPanel = new QWidget(this);
    m_rightLayout = new QVBoxLayout(m_rightPanel);
    m_rightLayout->setSpacing(8);
    m_rightLayout->setContentsMargins(0, 0, 0, 0);

    setupPostProcessingSection();
    setupPostProcessingResultsSection();

    m_mainLayout->addWidget(m_rightPanel);
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

    m_leftLayout->addWidget(m_outputGroup);
}

void MainWindow::setupVideoInputSection()
{
    m_videoInputGroup = new QGroupBox("Video Input", m_leftPanel);
    QHBoxLayout* layout = new QHBoxLayout(m_videoInputGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_addVideosButton = new QPushButton("Add Videos", m_leftPanel);
    m_removeVideoButton = new QPushButton("Remove Selected", m_leftPanel);

    // Settings button with text
    m_settingsButton = new QPushButton("Settings", m_leftPanel);
    m_settingsButton->setToolTip("Settings");
    m_settingsButton->setFixedWidth(80);

    // Layout: Add Videos and Remove Selected take most space, Settings button is small
    layout->addWidget(m_addVideosButton, 1);
    layout->addWidget(m_removeVideoButton, 1);
    layout->addWidget(m_settingsButton);

    m_leftLayout->addWidget(m_videoInputGroup);
}

void MainWindow::setupControlSection()
{
    m_controlGroup = new QGroupBox("Control", m_leftPanel);
    QHBoxLayout* layout = new QHBoxLayout(m_controlGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_startButton = new QPushButton("Start", m_leftPanel);
    m_pauseButton = new QPushButton("Pause", m_leftPanel);
    m_resetButton = new QPushButton("Reset", m_leftPanel);

    // Make buttons share 1/3 width each
    layout->addWidget(m_startButton, 1);
    layout->addWidget(m_pauseButton, 1);
    layout->addWidget(m_resetButton, 1);

    m_leftLayout->addWidget(m_controlGroup);
}

void MainWindow::setupQueueSection()
{
    m_queueGroup = new QGroupBox("Processing Queue", m_leftPanel);
    QVBoxLayout* layout = new QVBoxLayout(m_queueGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_queueTable = new QTableWidget(m_leftPanel);
    m_queueTable->setColumnCount(4);
    QStringList headers = {"Filename", "Status", "Slides", "Time (s)"};
    m_queueTable->setHorizontalHeaderLabels(headers);
    m_queueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queueTable->setAlternatingRowColors(true);

    // Enable scrolling for the table
    m_queueTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_queueTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Optimize column widths for 480px width vertical layout
    QHeaderView* header = m_queueTable->horizontalHeader();
    header->setSectionResizeMode(COL_FILENAME, QHeaderView::Stretch);  // Filename: flexible
    header->setSectionResizeMode(COL_STATUS, QHeaderView::Fixed);      // Status: fixed width (wider)
    header->setSectionResizeMode(COL_SLIDES, QHeaderView::Fixed);      // Slides: fixed width
    header->setSectionResizeMode(COL_TIME, QHeaderView::Fixed);        // Time: fixed width

    // Adjusted column widths for 480px window width (Status column wider)
    m_queueTable->setColumnWidth(COL_STATUS, 120);   // Status: 120px (increased for better visibility)
    m_queueTable->setColumnWidth(COL_SLIDES, 60);    // Slides: 60px (slightly increased)
    m_queueTable->setColumnWidth(COL_TIME, 80);      // Time: 80px (increased)
    // Filename column will take remaining space (~200px)

    // Reduced height for better proportions
    m_queueTable->setMinimumHeight(140);  // Further reduced for more compact layout
    m_queueTable->setMaximumHeight(180);  // Reduced maximum height accordingly

    layout->addWidget(m_queueTable);

    m_leftLayout->addWidget(m_queueGroup);
}

void MainWindow::setupProgressSection()
{
    m_progressGroup = new QGroupBox("Progress", m_leftPanel);
    QVBoxLayout* layout = new QVBoxLayout(m_progressGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(3);  // Tighter spacing for compact layout

    // Bold font for labels (keeping for potential future use)
    QFont boldFont;
    boldFont.setBold(true);

    // FFmpeg Frame Extraction Progress
    m_frameExtractionLabel = new QLabel("Frame Extraction", m_leftPanel);
    m_frameExtractionProgressBar = new QProgressBar(m_leftPanel);
    m_frameExtractionProgressBar->setRange(0, 100);
    m_frameExtractionProgressBar->setValue(0);
    m_frameExtractionProgressBar->setMaximumHeight(14);  // More compact than original 18px
    m_frameExtractionPercentLabel = new QLabel("0%", m_leftPanel);
    m_frameExtractionPercentLabel->setMinimumWidth(35);

    layout->addWidget(m_frameExtractionLabel);
    QHBoxLayout* frameProgressLayout = new QHBoxLayout();
    frameProgressLayout->setSpacing(6);
    frameProgressLayout->addWidget(m_frameExtractionProgressBar);
    frameProgressLayout->addWidget(m_frameExtractionPercentLabel);
    layout->addLayout(frameProgressLayout);

    // Slide Processing Progress
    m_slideProcessingLabel = new QLabel("Slide Detection", m_leftPanel);
    m_slideProcessingProgressBar = new QProgressBar(m_leftPanel);
    m_slideProcessingProgressBar->setRange(0, 100);
    m_slideProcessingProgressBar->setValue(0);
    m_slideProcessingProgressBar->setMaximumHeight(14);  // More compact than original 18px
    m_slideProcessingPercentLabel = new QLabel("0%", m_leftPanel);
    m_slideProcessingPercentLabel->setMinimumWidth(35);

    layout->addWidget(m_slideProcessingLabel);
    QHBoxLayout* slideProgressLayout = new QHBoxLayout();
    slideProgressLayout->setSpacing(6);
    slideProgressLayout->addWidget(m_slideProcessingProgressBar);
    slideProgressLayout->addWidget(m_slideProcessingPercentLabel);
    layout->addLayout(slideProgressLayout);

    m_leftLayout->addWidget(m_progressGroup);
}

void MainWindow::setupStatusSection()
{
    m_statusGroup = new QGroupBox("Status Log", m_centralWidget);
    QVBoxLayout* layout = new QVBoxLayout(m_statusGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_statusText = new QTextEdit(m_centralWidget);
    m_statusText->setMinimumHeight(120);  // Increased from 100
    m_statusText->setMaximumHeight(160);  // Increased from 120
    m_statusText->setReadOnly(true);

    layout->addWidget(m_statusText);

    // Don't add to left layout anymore - will be added to main layout below panels
}

void MainWindow::setupPostProcessingSection()
{
    m_postProcessingGroup = new QGroupBox("Post-Processing", m_rightPanel);
    QVBoxLayout* layout = new QVBoxLayout(m_postProcessingGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    // First row: Enable Post-Processing, Manual Post-Processing, Settings
    QHBoxLayout* topButtonLayout = new QHBoxLayout();
    topButtonLayout->setSpacing(8);

    m_enablePostProcessingCheckBox = new QCheckBox("Enable Post-Processing", m_rightPanel);
    m_enablePostProcessingCheckBox->setChecked(true);
    topButtonLayout->addWidget(m_enablePostProcessingCheckBox);

    topButtonLayout->addStretch();

    m_manualPostProcessingButton = new QPushButton("Manual Post-Processing", m_rightPanel);
    m_manualPostProcessingButton->setToolTip("Manually post-process a folder of images");
    topButtonLayout->addWidget(m_manualPostProcessingButton);

    m_postProcessingSettingsButton = new QPushButton("Settings", m_rightPanel);
    m_postProcessingSettingsButton->setToolTip("Configure pHash threshold and exclusion list");
    m_postProcessingSettingsButton->setFixedWidth(80);
    topButtonLayout->addWidget(m_postProcessingSettingsButton);

    layout->addLayout(topButtonLayout);

    // Separator
    QFrame* line = new QFrame(m_rightPanel);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Delete redundant checkbox with help text
    m_deleteRedundantCheckBox = new QCheckBox("Delete Redundant Slides", m_rightPanel);
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

    m_rightLayout->addWidget(m_postProcessingGroup);
}

void MainWindow::setupPostProcessingResultsSection()
{
    m_postProcessingResultsGroup = new QGroupBox("Post-Processing Results", m_rightPanel);
    QVBoxLayout* layout = new QVBoxLayout(m_postProcessingResultsGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    QLabel* helpLabel = new QLabel("Videos and their post-processing statistics:", m_rightPanel);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("color: #666; font-size: 11px;");
    layout->addWidget(helpLabel);

    m_postProcessingTable = new QTableWidget(m_rightPanel);
    m_postProcessingTable->setColumnCount(3);
    QStringList headers = {"Video", "Slides Saved", "Moved to Trash"};
    m_postProcessingTable->setHorizontalHeaderLabels(headers);
    m_postProcessingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_postProcessingTable->setAlternatingRowColors(true);

    // Column widths
    QHeaderView* header = m_postProcessingTable->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Fixed);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    m_postProcessingTable->setColumnWidth(1, 100);
    m_postProcessingTable->setColumnWidth(2, 120);

    m_postProcessingTable->setMinimumHeight(200);

    layout->addWidget(m_postProcessingTable);

    m_rightLayout->addWidget(m_postProcessingResultsGroup);
}

void MainWindow::connectSignals()
{
    // Video input signals
    connect(m_addVideosButton, &QPushButton::clicked, this, &MainWindow::onAddVideosClicked);
    connect(m_removeVideoButton, &QPushButton::clicked, this, &MainWindow::onRemoveVideoClicked);
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);

    // Output directory signals
    connect(m_outputDirBrowseButton, &QPushButton::clicked, this, &MainWindow::onOutputDirBrowseClicked);
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, &MainWindow::saveConfiguration);

    // Control signals
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(m_resetButton, &QPushButton::clicked, this, &MainWindow::onResetClicked);

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
    connect(m_postProcessingSettingsButton, &QPushButton::clicked, this, &MainWindow::onPostProcessingSettingsClicked);
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

    // Clear post-processing results table
    updatePostProcessingTable();
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

    for (int i = 0; i < static_cast<int>(videos.size()); ++i) {
        const VideoQueueItem& video = videos[i];

        // Filename
        QTableWidgetItem* filenameItem = new QTableWidgetItem(video.fileName);
        m_queueTable->setItem(i, COL_FILENAME, filenameItem);

        // Status
        QTableWidgetItem* statusItem = new QTableWidgetItem(VideoQueue::getStatusString(video.status));
        m_queueTable->setItem(i, COL_STATUS, statusItem);

        // Slides
        QTableWidgetItem* slidesItem = new QTableWidgetItem(QString::number(video.extractedSlides));
        m_queueTable->setItem(i, COL_SLIDES, slidesItem);

        // Time
        QString timeStr = (video.processingTimeSeconds > 0) ?
                         QString::number(video.processingTimeSeconds, 'f', 1) : "-";
        QTableWidgetItem* timeItem = new QTableWidgetItem(timeStr);
        m_queueTable->setItem(i, COL_TIME, timeItem);

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

void MainWindow::updatePostProcessingTable()
{
    const auto& videos = m_videoQueue->getAllVideos();

    // Filter to only show completed videos
    QList<const VideoQueueItem*> completedVideos;
    for (const auto& video : videos) {
        if (video.status == ProcessingStatus::Completed) {
            completedVideos.append(&video);
        }
    }

    m_postProcessingTable->setRowCount(completedVideos.size());

    for (int i = 0; i < completedVideos.size(); i++) {
        const VideoQueueItem* video = completedVideos[i];

        // Video name
        QTableWidgetItem* nameItem = new QTableWidgetItem(video->fileName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_postProcessingTable->setItem(i, 0, nameItem);

        // Slides saved
        int slidesSaved = video->extractedSlides - video->movedToTrash;
        QTableWidgetItem* savedItem = new QTableWidgetItem(QString::number(slidesSaved));
        savedItem->setFlags(savedItem->flags() & ~Qt::ItemIsEditable);
        m_postProcessingTable->setItem(i, 1, savedItem);

        // Moved to trash
        QTableWidgetItem* trashItem = new QTableWidgetItem(QString::number(video->movedToTrash));
        trashItem->setFlags(trashItem->flags() & ~Qt::ItemIsEditable);
        m_postProcessingTable->setItem(i, 2, trashItem);
    }
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

    // Process the directory
    int movedCount = processor.processDirectory(
        video->outputDirectory,
        m_config.deleteRedundant,
        m_config.compareExcluded,
        m_config.hammingThreshold,
        exclusionList
    );

    // Update video statistics
    video->movedToTrash = movedCount;

    m_statusText->append(QString("Post-processing complete: %1 images moved to trash").arg(movedCount));

    // Update post-processing table
    updatePostProcessingTable();
}

void MainWindow::onEnablePostProcessingToggled()
{
    bool enabled = m_enablePostProcessingCheckBox->isChecked();
    m_deleteRedundantCheckBox->setEnabled(enabled);
    m_compareExcludedCheckBox->setEnabled(enabled);
    saveConfiguration();
}

void MainWindow::onPostProcessingSettingsClicked()
{
    // Open settings dialog with post-processing tab (tab index 1)
    SettingsDialog dialog(m_config, m_configManager.get(), 1, this);
    connect(&dialog, &SettingsDialog::statusMessage, this, [this](const QString& message) {
        m_statusText->append(message);
    });
    if (dialog.exec() == QDialog::Accepted) {
        m_config = dialog.getConfig();
        m_configManager->saveConfig(m_config);
        m_processingThread->updateConfig(m_config);
        m_statusText->append("Post-processing settings updated");
    }
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

    // Process the directory
    int movedCount = processor.processDirectory(
        dir,
        m_config.deleteRedundant,
        m_config.compareExcluded,
        m_config.hammingThreshold,
        exclusionList
    );

    m_statusText->append(QString("Manual post-processing complete: %1 images moved to trash").arg(movedCount));
}

