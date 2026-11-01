#ifndef PDFMAKERDIALOG_H
#define PDFMAKERDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include <QString>
#include <QStringList>
#include "videoexporter.h"
#include <QMap>

class QButtonGroup;
class QStackedWidget;

/**
 * @brief Slides Export: turn slide folders into PDFs or videos.
 *
 * PDF mode: pick one or more `slides_*` folders, optionally reorder them with
 * drag-and-drop in Custom mode, and generate a multi-page PDF.
 *
 * Video mode: each selected folder becomes one MP4 that follows its
 * timeline.json. Audio comes from a per-folder source (the original video,
 * found automatically when the timeline recorded it, or any audio file);
 * folders without a source are exported silent.
 */
class PdfMakerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PdfMakerDialog(const QString& baseOutputDir,
                           QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

    int m_dragStartRow;
    QPoint m_dragStartPos;

signals:
    void statusMessage(const QString& message);

private slots:
    void onRefresh();
    void onFolderCheckChanged(int row, int column);
    void onToggleSelectFolders();
    void onOrderToggle();
    void onMoveUp();
    void onMoveDown();
    void onAspectRatioChanged(int index);

    void onMakePdf();
    void onOpenPdf();

    void onModeChanged();
    void onMakeClicked();
    void onMakeVideo();
    void onVideoSettingChanged();

private:
    void setupUI();
    QWidget* createPdfPage();
    QWidget* createVideoPage();
    void connectSignals();

    bool isVideoMode() const;
    QTableWidget* currentTable() const;
    void populateVideoTable();
    void chooseAudioSource(const QString& folderPath);
    void clearAudioSource(const QString& folderPath);
    void updateAudioControls();
    void populateBitrates(VideoExportOptions::AudioMode codec, int selectKbps);
    VideoExportOptions videoOptionsFromUi() const;
    void applyVideoOptionsToUi(const VideoExportOptions& options);
    void setExporting(bool exporting);

    void loadFolders();
    void populateFolderTable();

    void updateFolderSelectionLabel();
    void updateOrderButton();
    void swapRows(int row1, int row2);
    int countImagesInFolder(const QString& folderPath);
    QStringList getCheckedFolderPaths() const;
    QStringList getCheckedFolderPaths(const QTableWidget* table) const;
    QList<int> getCheckedFolderRows() const;
    QString stripSlidesPrefix(const QString& folderName);
    QStringList getImagesInFolder(const QString& folderPath);

    QString m_baseOutputDir;
    bool m_customOrder;
    QStringList m_folderNames;

    QPushButton* m_refreshButton;
    QPushButton* m_orderToggleButton;

    QTableWidget* m_folderTable;
    QLabel* m_folderSelectionLabel;
    QPushButton* m_moveUpButton;
    QPushButton* m_moveDownButton;
    QPushButton* m_toggleSelectFoldersButton;
    QPushButton* m_closeFoldersButton;

    QProgressBar* m_progressBar;
    QComboBox* m_outputModeComboBox;
    QCheckBox* m_reduceFileSizeCheckbox;
    QLabel* m_aspectRatioLabel;
    QComboBox* m_aspectRatioComboBox;
    QLabel* m_resizeLabel;
    QComboBox* m_resizeComboBox;
    QLabel* m_qualityLabel;
    QSpinBox* m_jpegQualitySpinBox;
    QPushButton* m_makePdfButton;
    QPushButton* m_openPdfButton;
    QString m_lastPdfPath;
    bool m_lastOutputIsFolder;

    // Mode switch
    QButtonGroup* m_modeGroup = nullptr;
    QPushButton* m_pdfModeButton = nullptr;
    QPushButton* m_videoModeButton = nullptr;
    QStackedWidget* m_modeStack = nullptr;

    // Video mode
    QTableWidget* m_videoTable = nullptr;
    QComboBox* m_videoResolutionComboBox = nullptr;
    QComboBox* m_videoQualityComboBox = nullptr;
    QComboBox* m_videoRepeatComboBox = nullptr;
    QComboBox* m_audioModeComboBox = nullptr;
    QLabel* m_audioBitrateLabel = nullptr;
    QComboBox* m_audioBitrateComboBox = nullptr;
    QLabel* m_audioChannelsLabel = nullptr;
    QComboBox* m_audioChannelsComboBox = nullptr;
    QLabel* m_audioSampleRateLabel = nullptr;
    QComboBox* m_audioSampleRateComboBox = nullptr;
    QString m_bitrateCodec;                  // codec the bitrate list is filled for ("aac"/"opus")
    QMap<QString, QString> m_audioSources;   // folder path -> chosen audio source ("" = none)
    QString m_lastVideoPath;
    bool m_lastVideoIsFolder = false;
    bool m_exporting = false;
    bool m_cancelRequested = false;
    bool m_loadingVideoOptions = false;

    enum FolderTableColumns {
        COL_SELECT = 0,
        COL_FOLDER_NAME = 1,
        COL_IMAGE_COUNT = 2,
        COL_HANDLE = 3
    };

    enum VideoTableColumns {
        VCOL_SELECT = 0,
        VCOL_FOLDER_NAME = 1,
        VCOL_IMAGE_COUNT = 2,
        VCOL_AUDIO_SOURCE = 3,
        VCOL_AUDIO_ACTIONS = 4
    };
};

#endif // PDFMAKERDIALOG_H
