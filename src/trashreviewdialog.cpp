#include "trashreviewdialog.h"
#include "trashmanager.h"
#include "trashmetadata.h"
#include "trashitemwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
// QMessageBox removed to prevent crashes - use statusMessage signal instead
#include <QDebug>

TrashReviewDialog::TrashReviewDialog(const QString& baseOutputDir,
                                   bool emptyTrashToSystemTrash,
                                   QWidget *parent)
    : QDialog(parent),
      m_baseOutputDir(baseOutputDir),
      m_emptyTrashToSystemTrash(emptyTrashToSystemTrash),
      m_restoredCount(0)
{
    setupUI();
    loadTrashEntries();
}

void TrashReviewDialog::setupUI()
{
    setWindowTitle("Trash Review");
    resize(1000, 800);  // Wider to accommodate 3 images per row

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Filter section
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(new QLabel("Filter by Video:", this));

    m_videoFilterCombo = new QComboBox(this);
    m_videoFilterCombo->addItem("All Videos", "");
    filterLayout->addWidget(m_videoFilterCombo);

    filterLayout->addWidget(new QLabel("Filter by Method:", this));

    m_methodFilterCombo = new QComboBox(this);
    m_methodFilterCombo->addItem("All Methods", "");
    m_methodFilterCombo->addItem("pHash", "phash");
    m_methodFilterCombo->addItem("ML", "ml");
    m_methodFilterCombo->addItem("Manual", "manual");
    filterLayout->addWidget(m_methodFilterCombo);

    filterLayout->addStretch();

    m_refreshButton = new QPushButton("Refresh", this);
    filterLayout->addWidget(m_refreshButton);

    mainLayout->addLayout(filterLayout);

    // Scroll area with grid layout for thumbnails
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridContainer = new QWidget();
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setSpacing(15);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_scrollArea->setWidget(m_gridContainer);
    mainLayout->addWidget(m_scrollArea);

    // Button section
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_selectionLabel = new QLabel("Selected: 0 items", this);
    buttonLayout->addWidget(m_selectionLabel);

    m_selectAllButton = new QPushButton("Select All", this);
    buttonLayout->addWidget(m_selectAllButton);

    m_deselectAllButton = new QPushButton("Deselect All", this);
    buttonLayout->addWidget(m_deselectAllButton);

    buttonLayout->addStretch();

    m_closeButton = new QPushButton("Close", this);
    buttonLayout->addWidget(m_closeButton);

    m_emptyTrashButton = new QPushButton("Empty Trash", this);
    buttonLayout->addWidget(m_emptyTrashButton);

    m_restoreButton = new QPushButton("Restore Selected", this);
    m_restoreButton->setEnabled(false);
    buttonLayout->addWidget(m_restoreButton);

    mainLayout->addLayout(buttonLayout);

    // Connect signals
    connect(m_videoFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrashReviewDialog::onFilterChanged);
    connect(m_methodFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrashReviewDialog::onFilterChanged);
    connect(m_refreshButton, &QPushButton::clicked, this, &TrashReviewDialog::onRefresh);
    connect(m_restoreButton, &QPushButton::clicked, this, &TrashReviewDialog::onRestoreSelected);
    connect(m_selectAllButton, &QPushButton::clicked, this, &TrashReviewDialog::onSelectAll);
    connect(m_deselectAllButton, &QPushButton::clicked, this, &TrashReviewDialog::onDeselectAll);
    connect(m_emptyTrashButton, &QPushButton::clicked, this, &TrashReviewDialog::onEmptyTrash);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void TrashReviewDialog::loadTrashEntries()
{
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);

    // Populate video filter dropdown
    QStringList videoNames = TrashMetadata::getUniqueVideoNames(m_allEntries);
    m_videoFilterCombo->clear();
    m_videoFilterCombo->addItem("All Videos", "");
    for (const QString& videoName : videoNames) {
        m_videoFilterCombo->addItem(videoName, videoName);
    }

    // Apply filters to populate list
    applyFilters();
}

void TrashReviewDialog::applyFilters()
{
    // Get filter values
    QString videoFilter = m_videoFilterCombo->currentData().toString();
    QString methodFilter = m_methodFilterCombo->currentData().toString();

    // Apply filters
    m_filteredEntries = m_allEntries;

    if (!videoFilter.isEmpty()) {
        m_filteredEntries = TrashMetadata::filterByVideo(m_filteredEntries, videoFilter);
    }

    if (!methodFilter.isEmpty()) {
        m_filteredEntries = TrashMetadata::filterByMethod(m_filteredEntries, methodFilter);
    }

    // Clear existing widgets
    clearGrid();

    // Populate grid with 3 items per row
    int row = 0;
    int col = 0;
    const int COLUMNS = 3;

    for (const TrashEntry& entry : m_filteredEntries) {
        TrashItemWidget* itemWidget = new TrashItemWidget(entry, m_baseOutputDir, m_gridContainer);

        // Connect selection changed signal
        connect(itemWidget, &TrashItemWidget::selectionChanged,
                this, &TrashReviewDialog::onItemSelectionChanged);

        m_gridLayout->addWidget(itemWidget, row, col);
        m_itemWidgets.append(itemWidget);

        col++;
        if (col >= COLUMNS) {
            col = 0;
            row++;
        }
    }

    updateSelectionLabel();
}

void TrashReviewDialog::clearGrid()
{
    // Delete all item widgets
    for (TrashItemWidget* widget : m_itemWidgets) {
        m_gridLayout->removeWidget(widget);
        delete widget;
    }
    m_itemWidgets.clear();
}

QList<TrashItemWidget*> TrashReviewDialog::getCheckedItems() const
{
    QList<TrashItemWidget*> checkedItems;
    for (TrashItemWidget* widget : m_itemWidgets) {
        if (widget->isChecked()) {
            checkedItems.append(widget);
        }
    }
    return checkedItems;
}

void TrashReviewDialog::updateSelectionLabel()
{
    int selectedCount = getCheckedItems().count();
    m_selectionLabel->setText(QString("Selected: %1 items").arg(selectedCount));
    m_restoreButton->setEnabled(selectedCount > 0);
}

void TrashReviewDialog::onItemSelectionChanged()
{
    updateSelectionLabel();
}

void TrashReviewDialog::onRestoreSelected()
{
    QList<TrashItemWidget*> checkedItems = getCheckedItems();

    if (checkedItems.isEmpty()) {
        return;
    }

    // Restore without confirmation - log to UI instead
    int count = checkedItems.count();
    emit statusMessage(QString("Restoring %1 selected image(s)...").arg(count));

    // Restore each selected item
    int successCount = 0;
    int failCount = 0;
    QStringList failedFiles;

    for (TrashItemWidget* itemWidget : checkedItems) {
        QString trashedFilename = itemWidget->getTrashedFilename();

        if (TrashManager::restoreFromApplicationTrash(trashedFilename, m_baseOutputDir)) {
            successCount++;
        } else {
            failCount++;
            failedFiles.append(trashedFilename);
        }
    }

    // Update restored count
    m_restoredCount += successCount;

    // Log result to UI
    if (failCount == 0) {
        emit statusMessage(QString("Successfully restored %1 image(s)").arg(successCount));
    } else {
        emit statusMessage(QString("Restored %1 image(s), %2 failed - check log for details").arg(successCount).arg(failCount));
        qWarning() << "TrashReviewDialog: Failed to restore" << failCount << "files:" << failedFiles;
    }

    // Emit signal
    if (successCount > 0) {
        emit filesRestored(successCount);
    }

    // Refresh the list
    onRefresh();
}

void TrashReviewDialog::onSelectAll()
{
    for (TrashItemWidget* widget : m_itemWidgets) {
        widget->setChecked(true);
    }
}

void TrashReviewDialog::onDeselectAll()
{
    for (TrashItemWidget* widget : m_itemWidgets) {
        widget->setChecked(false);
    }
}

void TrashReviewDialog::onEmptyTrash()
{
    int count = m_allEntries.size();

    if (count == 0) {
        emit statusMessage("Trash is already empty");
        return;
    }

    // Empty without confirmation - log to UI instead
    QString action = m_emptyTrashToSystemTrash ? "move to system trash" : "permanently delete";
    emit statusMessage(QString("Emptying trash: will %1 all %2 image(s)...").arg(action).arg(count));

    // Empty trash
    int removedCount = TrashManager::emptyApplicationTrash(m_baseOutputDir, m_emptyTrashToSystemTrash);

    // Log result to UI
    if (removedCount > 0) {
        emit statusMessage(QString("Emptied trash: %1 image(s) %2")
                          .arg(removedCount)
                          .arg(m_emptyTrashToSystemTrash ? "moved to system trash" : "deleted"));
        emit trashEmptied();
    } else {
        emit statusMessage("Failed to empty trash - check log for details");
        qWarning() << "TrashReviewDialog: Failed to empty trash";
    }

    // Refresh the list
    onRefresh();
}

void TrashReviewDialog::onRefresh()
{
    loadTrashEntries();
}

void TrashReviewDialog::onFilterChanged()
{
    applyFilters();
}
