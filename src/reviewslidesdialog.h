#ifndef REVIEWSLIDESDIALOG_H
#define REVIEWSLIDESDIALOG_H

#include <QDialog>
#include <QStackedWidget>
#include <QTableWidget>
#include <QScrollArea>
#include <QWidget>
#include <QGridLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include "trashentry.h"
#include "cropentry.h"
#include "reviewitemwidget.h"
#include "configmanager.h"

class QResizeEvent;
class CropImageView;
class AutoCropDetector;

/**
 * @brief Two-level dialog for reviewing extracted and removed slides.
 *
 * Level 1 — folders table with extracted / removed counts and per-row Review button.
 * Level 2 — single-folder grid mixing extracted and removed slides, with show/method
 * filters and per-state actions (Restore for removed, Delete for extracted).
 */
class ReviewSlidesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReviewSlidesDialog(const QString& baseOutputDir,
                                bool emptyTrashToSystemTrash,
                                int jpegQuality,
                                const AutoCropConfig& autoCropConfig,
                                QWidget* parent = nullptr);
    ~ReviewSlidesDialog();

signals:
    void statusMessage(const QString& message);
    void filesRestored(int count);
    void filesDeleted(int count);
    void trashEmptied();

private slots:
    // Folders page
    void onReviewFolder(int row);
    void onFolderCheckChanged(int row, int column);
    void onToggleSelectFolders();
    void onEmptyTrashFolders();
    void onDeleteSelectedFolders();
    void onRefreshFolders();

    // Images page
    void onBackToFolders();
    void onShowFilterChanged();
    void onMethodFilterChanged();
    void onToggleSelectImages();
    void onRestoreSelected();
    void onDeleteSelected();
    void onEmptyTrashCurrentFolder();
    void onItemSelectionChanged();
    void onThumbnailSizeChanged(int value);
    void onSetBaselineFromItem(ReviewItemWidget* item);
    void onApplyBaseline();
    void onAutoCropSelected();
    void onRestoreCropSelected();
    void onRemoveDuplicates();
    void onRefreshImages();

    // Viewer page
    void onViewerBack();
    void onStartCrop();
    void onApplyCrop();
    void onCancelCrop();
    void onRestoreCrop();
    void onAutoCrop();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    enum FolderCols {
        F_SELECT = 0,
        F_NAME = 1,
        F_EXTRACTED = 2,
        F_REMOVED = 3,
        F_REVIEW = 4
    };

    enum ShowFilter {
        ShowBoth = 0,
        ShowExtractedOnly = 1,
        ShowRemovedOnly = 2
    };

    void setupUI();
    void setupFoldersPage();
    void setupImagesPage();
    void setupViewerPage();
    void connectSignals();

    void loadCropEntries();
    // Folder name a trash entry belongs to: originalFolder, or "slides_<video>".
    static QString folderNameForEntry(const TrashEntry& entry);
    // Refill m_currentFolderRemoved from m_allEntries for m_currentFolderName.
    void rebuildCurrentFolderRemoved();
    // Reload trash + crop metadata, rebuild the removed list, and rebuild the grid.
    void reloadCurrentFolderItems();
    bool isLivePathCropped(const QString& livePath, CropEntry* outEntry = nullptr) const;
    void enterViewerPage(ReviewItemWidget* item);
    void refreshViewerImage();
    void updateViewerButtons();
    void setViewerCropMode(bool selecting);
    void updateBaselineButtonState();
    void resetBaselineState();
    void refreshWindowTitleForCurrentPage();

    void loadFolders();
    void populateFolderTable();
    QStringList enumerateAllFolderNames() const;
    int countExtractedInFolder(const QString& folderPath) const;
    int countRemovedInFolder(const QString& folderName) const;
    QStringList getCheckedFolderNames() const;
    void updateFolderSelectionLabel();
    QString stripSlidesPrefix(const QString& folderName) const;

    void enterImagesPage(const QString& folderName);
    void loadFolderItems();
    void clearImageGrid();
    void applyImageFilters();
    QList<ReviewItemWidget*> getCheckedItems() const;
    void updateImageSelectionLabel();
    void updateImageActionButtons();
    void relayoutGrid();
    int computeColumns() const;

    QString m_baseOutputDir;
    bool m_emptyTrashToSystemTrash;
    int m_jpegQuality;
    AutoCropConfig m_autoCropConfig;
    std::unique_ptr<AutoCropDetector> m_autoCropDetector;

    QList<TrashEntry> m_allEntries;
    QList<CropEntry> m_allCropEntries;
    QString m_currentFolderName;
    QList<TrashEntry> m_currentFolderRemoved;
    QList<ReviewItemWidget*> m_itemWidgets;
    int m_thumbnailWidth;
    int m_lastLaidOutColumns;

    // Crop-baseline state — captured from a cropped slide, applied to other extracted slides.
    bool m_baselineActive = false;
    QRect m_baselineRectImagePx;   // rect in source-image pixels
    QSize m_baselineSourceSize;    // dimensions of the source original image
    QString m_baselineSourceLabel; // display name of the source for status messages

    // Viewer state
    QString m_viewerLivePath;       // for Extracted: live slide_*.jpg path
    QString m_viewerTrashedPath;    // for Removed: .extractorTrash path
    bool m_viewerIsExtracted = false;
    bool m_viewerIsCropped = false;
    bool m_viewerSelecting = false;

    QStackedWidget* m_stack;

    // Folders page
    QWidget* m_foldersPage;
    QTableWidget* m_folderTable;
    QPushButton* m_foldersRefreshButton;
    QLabel* m_folderSelectionLabel;
    QPushButton* m_toggleSelectFoldersButton;
    QPushButton* m_emptyTrashFoldersButton;
    QPushButton* m_deleteFolderButton;
    QPushButton* m_closeFoldersButton;

    // Images page
    QWidget* m_imagesPage;
    QPushButton* m_backButton;
    QComboBox* m_showFilterCombo;
    QComboBox* m_methodFilterCombo;
    QSlider* m_thumbSizeSlider;
    QScrollArea* m_scrollArea;
    QWidget* m_gridContainer;
    QGridLayout* m_gridLayout;
    QLabel* m_imageSelectionLabel;
    QPushButton* m_toggleSelectImagesButton;
    QPushButton* m_restoreButton;
    QPushButton* m_deleteButton;
    QPushButton* m_emptyTrashFolderButton;
    QPushButton* m_imagesRefreshButton;
    QPushButton* m_closeImagesButton;
    QPushButton* m_applyBaselineButton;
    QPushButton* m_autoCropSelectedButton;
    QPushButton* m_restoreCropSelectedButton;
    QPushButton* m_removeDuplicateButton;

    // Viewer page
    QWidget* m_viewerPage;
    CropImageView* m_cropView;
    QLabel* m_viewerTitleLabel;
    QPushButton* m_viewerBackButton;
    QPushButton* m_cropButton;
    QPushButton* m_autoCropButton;
    QPushButton* m_restoreCropButton;
    QPushButton* m_recropButton;
    QPushButton* m_applyCropButton;
    QPushButton* m_cancelCropButton;
};

#endif // REVIEWSLIDESDIALOG_H
