#ifndef PDFMAKERDIALOG_H
#define PDFMAKERDIALOG_H

#include <QDialog>
#include <QStackedWidget>
#include <QTableWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include <QString>
#include <QStringList>
#include "slideitemwidget.h"

/**
 * @brief Dialog for browsing slide folders and managing slide images
 *
 * Provides a two-level navigation:
 * - Level 0: Folder table showing all slides_* folders
 * - Level 1: Image grid showing all slide_* images in a folder
 *
 * Supports drag-and-drop reordering of folders (in Custom order mode) and multi-selection.
 */
class PdfMakerDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param baseOutputDir Base output directory (e.g., ~/Downloads/SlidesExtractor)
     * @param parent Parent widget
     */
    explicit PdfMakerDialog(const QString& baseOutputDir,
                           QWidget *parent = nullptr);

protected:
    /**
     * @brief Event filter to handle drag-drop for table row reordering
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Drag state for row reordering
    int m_dragStartRow;        // Row where drag started (-1 if not dragging)
    QPoint m_dragStartPos;     // Mouse position where drag started

signals:
    /**
     * @brief Emitted to send status messages to main window
     * @param message Status message
     */
    void statusMessage(const QString& message);

    /**
     * @brief Emitted when files are deleted
     * @param count Number of files deleted
     */
    void filesDeleted(int count);

private slots:
    // Common slots
    void onRefresh();
    void onBackToFolders();

    // Folder level slots
    void onReviewFolder(int row);
    void onFolderCheckChanged(int row, int column);
    void onSelectAllFolders();
    void onDeselectAllFolders();
    void onOrderToggle();
    void onMoveUp();
    void onMoveDown();

    // Image level slots
    void onImageSelectionChanged();
    void onSelectAllImages();
    void onDeselectAllImages();
    void onDeleteSelected();

    // PDF generation slot
    void onMakePdf();
    void onOpenPdf();

private:
    // Setup methods
    void setupUI();
    void setupFoldersPage();
    void setupImagesPage();
    void connectSignals();

    // Data loading
    void loadFolders();
    void loadImages(const QString& folderPath);
    void populateFolderTable();

    // Sorting
    void naturalSort(QStringList& list);
    static bool naturalLessThan(const QString& a, const QString& b);

    // Helper methods
    void clearImageGrid();
    void updateFolderSelectionLabel();
    void updateImageSelectionLabel();
    void updateNavigationTitle();
    void updateOrderButton();
    void swapRows(int row1, int row2);
    int countImagesInFolder(const QString& folderPath);
    QList<SlideItemWidget*> getCheckedImageItems() const;
    QStringList getCheckedFolderPaths() const;
    QList<int> getCheckedFolderRows() const;
    QString stripSlidesPrefix(const QString& folderName);
    QStringList getImagesInFolder(const QString& folderPath);

    // Configuration
    QString m_baseOutputDir;
    QString m_currentFolderPath;  // Currently viewing folder (empty = folders level)
    bool m_customOrder;           // true = custom order (drag enabled), false = A-Z sort
    QStringList m_folderNames;    // Current folder list (for reordering)

    // UI - Common
    QStackedWidget* m_stackedWidget;
    QLabel* m_titleLabel;
    QPushButton* m_backButton;
    QPushButton* m_refreshButton;
    QPushButton* m_orderToggleButton;

    // UI - Folders Page (page 0)
    QWidget* m_foldersPage;
    QTableWidget* m_folderTable;
    QLabel* m_folderSelectionLabel;
    QPushButton* m_moveUpButton;
    QPushButton* m_moveDownButton;
    QPushButton* m_selectAllFoldersButton;
    QPushButton* m_deselectAllFoldersButton;
    QPushButton* m_closeFoldersButton;

    // UI - Action Bar (in header)
    QProgressBar* m_progressBar;
    QCheckBox* m_reduceFileSizeCheckbox;
    QLabel* m_resizeLabel;
    QComboBox* m_resizeComboBox;
    QLabel* m_qualityLabel;
    QSpinBox* m_jpegQualitySpinBox;
    QPushButton* m_makePdfButton;
    QPushButton* m_openPdfButton;
    QString m_lastPdfPath;  // Path to last generated PDF

    // UI - Images Page (page 1)
    QWidget* m_imagesPage;
    QScrollArea* m_scrollArea;
    QWidget* m_gridContainer;
    QGridLayout* m_gridLayout;
    QList<SlideItemWidget*> m_imageWidgets;
    QLabel* m_imageSelectionLabel;
    QPushButton* m_deleteButton;
    QPushButton* m_selectAllImagesButton;
    QPushButton* m_deselectAllImagesButton;
    QPushButton* m_closeImagesButton;

    // Table column indices
    enum FolderTableColumns {
        COL_SELECT = 0,
        COL_FOLDER_NAME = 1,
        COL_IMAGE_COUNT = 2,
        COL_REVIEW = 3,
        COL_HANDLE = 4
    };
};

#endif // PDFMAKERDIALOG_H
