#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>

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

    setWindowTitle("AutoSlides Extractor v1.0.0");
    resize(480, 600);  // More compact layout without progress bars
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

    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setSpacing(8);
    m_mainLayout->setContentsMargins(12, 12, 12, 12);

    // Simplified vertical layout
    setupOutputDirectorySection();  // Only output directory
    setupVideoInputSection();       // Add Videos + Remove + Settings button
    setupQueueSection();            // Increased height video list
    setupControlSection();          // Start/Pause/Reset buttons
    setupProgressSection();         // Progress bars (compact)
    setupStatusSection();           // Status log only
}

void MainWindow::setupOutputDirectorySection()
{
    m_outputGroup = new QGroupBox("Output Directory", this);
    QHBoxLayout* layout = new QHBoxLayout(m_outputGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_outputDirEdit = new QLineEdit(this);
    m_outputDirBrowseButton = new QPushButton("Browse", this);
    m_outputDirBrowseButton->setFixedWidth(70);

    layout->addWidget(m_outputDirEdit, 1);
    layout->addWidget(m_outputDirBrowseButton);

    m_mainLayout->addWidget(m_outputGroup);
}

void MainWindow::setupVideoInputSection()
{
    m_videoInputGroup = new QGroupBox("Video Input", this);
    QHBoxLayout* layout = new QHBoxLayout(m_videoInputGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_addVideosButton = new QPushButton("Add Videos", this);
    m_removeVideoButton = new QPushButton("Remove Selected", this);

    // Settings button with text
    m_settingsButton = new QPushButton("Settings", this);
    m_settingsButton->setToolTip("Settings");
    m_settingsButton->setFixedWidth(80);

    // Layout: Add Videos and Remove Selected take most space, Settings button is small
    layout->addWidget(m_addVideosButton, 1);
    layout->addWidget(m_removeVideoButton, 1);
    layout->addWidget(m_settingsButton);

    m_mainLayout->addWidget(m_videoInputGroup);
}

void MainWindow::setupControlSection()
{
    m_controlGroup = new QGroupBox("Control", this);
    QHBoxLayout* layout = new QHBoxLayout(m_controlGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_startButton = new QPushButton("Start", this);
    m_pauseButton = new QPushButton("Pause", this);
    m_resetButton = new QPushButton("Reset", this);

    // Make buttons share 1/3 width each
    layout->addWidget(m_startButton, 1);
    layout->addWidget(m_pauseButton, 1);
    layout->addWidget(m_resetButton, 1);

    m_mainLayout->addWidget(m_controlGroup);
}

void MainWindow::setupQueueSection()
{
    m_queueGroup = new QGroupBox("Processing Queue", this);
    QVBoxLayout* layout = new QVBoxLayout(m_queueGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_queueTable = new QTableWidget(this);
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

    m_mainLayout->addWidget(m_queueGroup);
}

void MainWindow::setupProgressSection()
{
    m_progressGroup = new QGroupBox("Progress", this);
    QVBoxLayout* layout = new QVBoxLayout(m_progressGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(3);  // Tighter spacing for compact layout

    // Bold font for labels (keeping for potential future use)
    QFont boldFont;
    boldFont.setBold(true);

    // FFmpeg Frame Extraction Progress
    m_frameExtractionLabel = new QLabel("Frame Extraction", this);
    m_frameExtractionProgressBar = new QProgressBar(this);
    m_frameExtractionProgressBar->setRange(0, 100);
    m_frameExtractionProgressBar->setValue(0);
    m_frameExtractionProgressBar->setMaximumHeight(14);  // More compact than original 18px
    m_frameExtractionPercentLabel = new QLabel("0%", this);
    m_frameExtractionPercentLabel->setMinimumWidth(35);

    layout->addWidget(m_frameExtractionLabel);
    QHBoxLayout* frameProgressLayout = new QHBoxLayout();
    frameProgressLayout->setSpacing(6);
    frameProgressLayout->addWidget(m_frameExtractionProgressBar);
    frameProgressLayout->addWidget(m_frameExtractionPercentLabel);
    layout->addLayout(frameProgressLayout);

    // Slide Processing Progress
    m_slideProcessingLabel = new QLabel("Slide Detection", this);
    m_slideProcessingProgressBar = new QProgressBar(this);
    m_slideProcessingProgressBar->setRange(0, 100);
    m_slideProcessingProgressBar->setValue(0);
    m_slideProcessingProgressBar->setMaximumHeight(14);  // More compact than original 18px
    m_slideProcessingPercentLabel = new QLabel("0%", this);
    m_slideProcessingPercentLabel->setMinimumWidth(35);

    layout->addWidget(m_slideProcessingLabel);
    QHBoxLayout* slideProgressLayout = new QHBoxLayout();
    slideProgressLayout->setSpacing(6);
    slideProgressLayout->addWidget(m_slideProcessingProgressBar);
    slideProgressLayout->addWidget(m_slideProcessingPercentLabel);
    layout->addLayout(slideProgressLayout);

    m_mainLayout->addWidget(m_progressGroup);
}

void MainWindow::setupStatusSection()
{
    m_statusGroup = new QGroupBox("Status Log", this);
    QVBoxLayout* layout = new QVBoxLayout(m_statusGroup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    m_statusText = new QTextEdit(this);
    m_statusText->setMinimumHeight(120);  // Increased from 100
    m_statusText->setMaximumHeight(160);  // Increased from 120
    m_statusText->setReadOnly(true);

    layout->addWidget(m_statusText);

    m_mainLayout->addWidget(m_statusGroup);
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
}

void MainWindow::loadConfiguration()
{
    m_config = m_configManager->loadConfig();
    // Only update output directory in main window
    m_outputDirEdit->setText(m_config.outputDirectory);
}

void MainWindow::saveConfiguration()
{
    // Only save output directory from main window
    m_config.outputDirectory = m_outputDirEdit->text();
    m_configManager->saveConfig(m_config);
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_processingThread && m_processingThread->isProcessing()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "Processing in Progress",
            "Video processing is currently running. Do you want to stop and exit?",
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            m_processingThread->stopProcessing();
            m_processingThread->wait(5000);
            saveConfiguration();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        saveConfiguration();
        event->accept();
    }
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
        QMessageBox::information(this, "No Videos", "Please add videos to the queue first.");
        return;
    }

    // Save output directory and update processing thread with current config
    saveConfiguration();
    m_processingThread->updateConfig(m_config);
    m_processingThread->startProcessing();
}

void MainWindow::onPauseClicked()
{
    m_processingThread->pauseProcessing();
}

void MainWindow::onResetClicked()
{
    if (m_processingThread->isProcessing()) {
        QMessageBox::information(this, "Processing Active", "Cannot reset while processing is active. Please pause first.");
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Reset Queue",
        "This will clear all completed and error items from the queue. Continue?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_videoQueue->clearCompleted();
        m_statusText->append("Queue reset - cleared completed and error items");
    }
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
    SettingsDialog dialog(m_config, this);
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
        m_frameExtractionProgressBar->setValue(static_cast<int>(percentage));
        m_frameExtractionPercentLabel->setText(QString("%1%").arg(percentage, 0, 'f', 1));
    } else {
        m_frameExtractionProgressBar->setValue(0);
        m_frameExtractionPercentLabel->setText("0%");
    }
}

void MainWindow::updateSlideProcessingProgress(int videoIndex, double percentage)
{
    // Update slide processing progress
    if (percentage >= 0) {
        m_slideProcessingProgressBar->setValue(static_cast<int>(percentage));
        m_slideProcessingPercentLabel->setText(QString("%1%").arg(percentage, 0, 'f', 1));
    } else {
        m_slideProcessingProgressBar->setValue(0);
        m_slideProcessingPercentLabel->setText("0%");
    }
}

void MainWindow::resetProgressBars(int videoIndex)
{
    // Reset both progress bars
    m_frameExtractionProgressBar->setValue(0);
    m_frameExtractionPercentLabel->setText("0%");
    m_slideProcessingProgressBar->setValue(0);
    m_slideProcessingPercentLabel->setText("0%");
}

