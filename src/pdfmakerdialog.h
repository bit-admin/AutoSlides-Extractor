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

/**
 * @brief Dialog for browsing slide folders and exporting them as PDF documents.
 *
 * Single-page folder picker: pick one or more `slides_*` folders, optionally reorder
 * them with drag-and-drop in Custom mode, and generate a multi-page PDF.
 */
class PdfMakerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PdfMakerDialog(const QString& baseOutputDir,
                           QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

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

private:
    void setupUI();
    void connectSignals();

    void loadFolders();
    void populateFolderTable();

    void updateFolderSelectionLabel();
    void updateOrderButton();
    void swapRows(int row1, int row2);
    int countImagesInFolder(const QString& folderPath);
    QStringList getCheckedFolderPaths() const;
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

    enum FolderTableColumns {
        COL_SELECT = 0,
        COL_FOLDER_NAME = 1,
        COL_IMAGE_COUNT = 2,
        COL_HANDLE = 3
    };
};

#endif // PDFMAKERDIALOG_H
