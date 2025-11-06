#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QProgressBar>
#include <QTextEdit>
#include <QFileDialog>
#include <QTimer>
#include <memory>

#include "videoqueue.h"
#include "processingthread.h"
#include "configmanager.h"
#include "hardwaredecoder.h"
#include "settingsdialog.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAddVideosClicked();
    void onRemoveVideoClicked();
    void onStartClicked();
    void onPauseClicked();
    void onResetClicked();
    void onOutputDirBrowseClicked();
    void onSettingsClicked();

    // Post-processing slots
    void onEnablePostProcessingToggled();
    void onPostProcessingSettingsClicked();
    void onManualPostProcessingClicked();

    // Processing thread slots
    void onProcessingStarted();
    void onProcessingPaused();
    void onProcessingStopped();
    void onVideoProcessingStarted(int videoIndex);
    void onVideoProcessingCompleted(int videoIndex, int slidesExtracted);
    void onVideoProcessingError(int videoIndex, const QString& error);
    void onFrameExtractionProgress(int videoIndex, double percentage);
    void onSSIMCalculationProgress(int videoIndex, int current, int total);
    void onSlideDetectionProgress(int videoIndex, int current, int total);
    void onVideoInfoLogged(int videoIndex, const QString& info);

    // Video queue slots
    void onVideoAdded(int index);
    void onVideoRemoved(int index);
    void onStatusChanged(int index, ProcessingStatus status);
    void onStatisticsUpdated(int index);
    void onQueueCleared();

    // UI update timer
    void updateUI();

private:
    void setupUI();
    void setupLeftPanel();
    void setupRightPanel();
    void setupVideoInputSection();
    void setupOutputDirectorySection();
    void setupControlSection();
    void setupQueueSection();
    void setupProgressSection();
    void setupStatusSection();
    void setupPostProcessingSection();
    void setupPostProcessingResultsSection();

    void loadConfiguration();
    void saveConfiguration();
    void updateControlButtons();
    void updateQueueTable();
    void updatePostProcessingTable();
    void updateFrameExtractionProgress(int videoIndex, double percentage);
    void updateSlideProcessingProgress(int videoIndex, double percentage);
    void resetProgressBars(int videoIndex);
    void connectSignals();
    void performPostProcessing(int videoIndex);

    // UI Components
    QWidget* m_centralWidget;
    QHBoxLayout* m_mainLayout;

    // Left Panel
    QWidget* m_leftPanel;
    QVBoxLayout* m_leftLayout;

    // Video Input Section
    QGroupBox* m_videoInputGroup;
    QPushButton* m_addVideosButton;
    QPushButton* m_removeVideoButton;
    QPushButton* m_settingsButton;

    // Output Directory Section
    QGroupBox* m_outputGroup;
    QLineEdit* m_outputDirEdit;
    QPushButton* m_outputDirBrowseButton;

    // Control Section
    QGroupBox* m_controlGroup;
    QPushButton* m_startButton;
    QPushButton* m_pauseButton;
    QPushButton* m_resetButton;

    // Queue Section
    QGroupBox* m_queueGroup;
    QTableWidget* m_queueTable;

    // Progress Section
    QGroupBox* m_progressGroup;
    QLabel* m_frameExtractionLabel;
    QProgressBar* m_frameExtractionProgressBar;
    QLabel* m_frameExtractionPercentLabel;
    QLabel* m_slideProcessingLabel;
    QProgressBar* m_slideProcessingProgressBar;
    QLabel* m_slideProcessingPercentLabel;

    // Status Section
    QGroupBox* m_statusGroup;
    QTextEdit* m_statusText;

    // Right Panel - Post-Processing
    QWidget* m_rightPanel;
    QVBoxLayout* m_rightLayout;

    // Post-Processing Controls
    QGroupBox* m_postProcessingGroup;
    QCheckBox* m_enablePostProcessingCheckBox;
    QCheckBox* m_deleteRedundantCheckBox;
    QCheckBox* m_compareExcludedCheckBox;
    QPushButton* m_postProcessingSettingsButton;
    QPushButton* m_manualPostProcessingButton;

    // Post-Processing Results
    QGroupBox* m_postProcessingResultsGroup;
    QTableWidget* m_postProcessingTable;

    // Backend components
    std::unique_ptr<VideoQueue> m_videoQueue;
    std::unique_ptr<ProcessingThread> m_processingThread;
    std::unique_ptr<ConfigManager> m_configManager;
    AppConfig m_config;

    // UI update timer
    QTimer* m_uiUpdateTimer;

    // Progress tracking to prevent shaking
    int m_lastFrameExtractionProgress = -1;
    int m_lastSlideProcessingProgress = -1;

    // Table columns
    enum QueueTableColumns {
        COL_FILENAME = 0,
        COL_STATUS = 1,
        COL_SLIDES = 2,
        COL_TIME = 3
    };
};

#endif // MAINWINDOW_H