#ifndef TRASHREVIEWDIALOG_H
#define TRASHREVIEWDIALOG_H

#include <QDialog>
#include <QScrollArea>
#include <QWidget>
#include <QGridLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include <QList>
#include "trashentry.h"
#include "trashitemwidget.h"

/**
 * @brief Dialog for reviewing and restoring trashed slide images
 *
 * Displays thumbnails of trashed images with filtering options.
 * Users can select images to restore or empty the entire trash.
 */
class TrashReviewDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param baseOutputDir Base output directory (e.g., ~/Downloads/SlidesExtractor)
     * @param emptyTrashToSystemTrash Configuration: move to system trash when emptying
     * @param parent Parent widget
     */
    explicit TrashReviewDialog(const QString& baseOutputDir,
                              bool emptyTrashToSystemTrash,
                              QWidget *parent = nullptr);

    /**
     * @brief Get the number of files restored
     * @return Number of files restored during this session
     */
    int getRestoredCount() const { return m_restoredCount; }

signals:
    /**
     * @brief Emitted when files are restored
     * @param count Number of files restored
     */
    void filesRestored(int count);

    /**
     * @brief Emitted when trash is emptied
     */
    void trashEmptied();

    /**
     * @brief Emitted to send status messages to main window
     * @param message Status message
     */
    void statusMessage(const QString& message);

private slots:
    /**
     * @brief Handle restore selected button click
     */
    void onRestoreSelected();

    /**
     * @brief Handle select all button click
     */
    void onSelectAll();

    /**
     * @brief Handle deselect all button click
     */
    void onDeselectAll();

    /**
     * @brief Handle empty trash button click
     */
    void onEmptyTrash();

    /**
     * @brief Handle refresh button click
     */
    void onRefresh();

    /**
     * @brief Handle filter changes
     */
    void onFilterChanged();

    /**
     * @brief Handle selection changes
     */
    void onItemSelectionChanged();

private:
    /**
     * @brief Setup the UI components
     */
    void setupUI();

    /**
     * @brief Load trash entries and populate list
     */
    void loadTrashEntries();

    /**
     * @brief Apply current filters and update list
     */
    void applyFilters();

    /**
     * @brief Update selection status label
     */
    void updateSelectionLabel();

    /**
     * @brief Get all checked item widgets
     */
    QList<TrashItemWidget*> getCheckedItems() const;

    /**
     * @brief Clear all item widgets from grid
     */
    void clearGrid();

    // Configuration
    QString m_baseOutputDir;
    bool m_emptyTrashToSystemTrash;

    // Data
    QList<TrashEntry> m_allEntries;      // All trash entries
    QList<TrashEntry> m_filteredEntries; // Filtered entries
    int m_restoredCount;

    // UI Components
    QComboBox* m_videoFilterCombo;
    QComboBox* m_methodFilterCombo;
    QPushButton* m_refreshButton;
    QScrollArea* m_scrollArea;
    QWidget* m_gridContainer;
    QGridLayout* m_gridLayout;
    QList<TrashItemWidget*> m_itemWidgets;
    QLabel* m_selectionLabel;
    QPushButton* m_restoreButton;
    QPushButton* m_selectAllButton;
    QPushButton* m_deselectAllButton;
    QPushButton* m_emptyTrashButton;
    QPushButton* m_closeButton;
};

#endif // TRASHREVIEWDIALOG_H
