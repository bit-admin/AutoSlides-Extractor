#include "reviewslidesdialog.h"
#include "trashmanager.h"
#include "trashmetadata.h"
#include "cropmanager.h"
#include "cropmetadata.h"
#include "cropimageview.h"
#include "autocropdetector.h"
#include "postprocessor.h"
#include "configmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QMap>
#include <QVariant>
#include <QResizeEvent>
#include <QPixmap>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <cmath>

ReviewSlidesDialog::ReviewSlidesDialog(const QString& baseOutputDir,
                                       bool emptyTrashToSystemTrash,
                                       int jpegQuality,
                                       const AutoCropConfig& autoCropConfig,
                                       QWidget* parent)
    : QDialog(parent),
      m_baseOutputDir(baseOutputDir),
      m_emptyTrashToSystemTrash(emptyTrashToSystemTrash),
      m_jpegQuality(jpegQuality),
      m_autoCropConfig(autoCropConfig),
      m_thumbnailWidth(ReviewItemWidget::kDefaultThumbnailWidth),
      m_lastLaidOutColumns(0)
{
    setupUI();
    connectSignals();
    loadFolders();
}

ReviewSlidesDialog::~ReviewSlidesDialog() = default;

void ReviewSlidesDialog::setupUI()
{
    setWindowTitle("Slides Review");
    resize(1010, 800);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    // Each inner page already has its own QVBoxLayout with default margins;
    // letting this outer layout add another layer of margins on top doubles
    // the spacing around the page content (most visibly: the bottom row sits
    // higher than in dialogs without a QStackedWidget). Drop the outer margins
    // so the page layout is the sole source of dialog-edge padding.
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);

    setupFoldersPage();
    setupImagesPage();
    setupViewerPage();

    m_stack->addWidget(m_foldersPage);
    m_stack->addWidget(m_imagesPage);
    m_stack->addWidget(m_viewerPage);
    m_stack->setCurrentIndex(0);

    mainLayout->addWidget(m_stack);
}

void ReviewSlidesDialog::setupFoldersPage()
{
    m_foldersPage = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_foldersPage);

    QHBoxLayout* topLayout = new QHBoxLayout();
    QLabel* heading = new QLabel("Folders", m_foldersPage);
    heading->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; }");
    topLayout->addWidget(heading);
    topLayout->addStretch();
    layout->addLayout(topLayout);

    m_folderTable = new QTableWidget(m_foldersPage);
    m_folderTable->setColumnCount(5);
    m_folderTable->setHorizontalHeaderLabels({"", "Folder Name", "Extracted", "Removed", ""});
    m_folderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_folderTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folderTable->setAlternatingRowColors(true);
    m_folderTable->verticalHeader()->setVisible(false);
    m_folderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QHeaderView* header = m_folderTable->horizontalHeader();
    header->setSectionResizeMode(F_SELECT, QHeaderView::Fixed);
    header->setSectionResizeMode(F_NAME, QHeaderView::Stretch);
    header->setSectionResizeMode(F_EXTRACTED, QHeaderView::Fixed);
    header->setSectionResizeMode(F_REMOVED, QHeaderView::Fixed);
    header->setSectionResizeMode(F_REVIEW, QHeaderView::Fixed);
    m_folderTable->setColumnWidth(F_SELECT, 40);
    m_folderTable->setColumnWidth(F_EXTRACTED, 90);
    m_folderTable->setColumnWidth(F_REMOVED, 90);
    m_folderTable->setColumnWidth(F_REVIEW, 90);

    layout->addWidget(m_folderTable);

    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_folderSelectionLabel = new QLabel("Selected: 0 folders", m_foldersPage);
    buttonLayout->addWidget(m_folderSelectionLabel);

    m_toggleSelectFoldersButton = new QPushButton("Select All", m_foldersPage);
    buttonLayout->addWidget(m_toggleSelectFoldersButton);

    buttonLayout->addStretch();

    m_foldersRefreshButton = new QPushButton("Refresh", m_foldersPage);
    buttonLayout->addWidget(m_foldersRefreshButton);

    m_closeFoldersButton = new QPushButton("Close", m_foldersPage);
    buttonLayout->addWidget(m_closeFoldersButton);

    m_deleteFolderButton = new QPushButton("Delete Folder", m_foldersPage);
    m_deleteFolderButton->setEnabled(false);
    m_deleteFolderButton->setToolTip("Move the selected folders and their trash entries to the system trash");
    buttonLayout->addWidget(m_deleteFolderButton);

    m_emptyTrashFoldersButton = new QPushButton("Empty Trash", m_foldersPage);
    m_emptyTrashFoldersButton->setEnabled(false);
    m_emptyTrashFoldersButton->setToolTip("Permanently empty the trash for the selected folders");
    buttonLayout->addWidget(m_emptyTrashFoldersButton);

    layout->addLayout(buttonLayout);
}

void ReviewSlidesDialog::setupImagesPage()
{
    m_imagesPage = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_imagesPage);

    // Top bar: Back + folder title + filters
    QHBoxLayout* topLayout = new QHBoxLayout();

    m_backButton = new QPushButton("< Back", m_imagesPage);
    topLayout->addWidget(m_backButton);

    topLayout->addWidget(new QLabel("Show:", m_imagesPage));
    m_showFilterCombo = new QComboBox(m_imagesPage);
    m_showFilterCombo->addItem("Context", static_cast<int>(ShowBoth));
    m_showFilterCombo->addItem("Extracted Only", static_cast<int>(ShowExtractedOnly));
    m_showFilterCombo->addItem("Removed Only", static_cast<int>(ShowRemovedOnly));
    topLayout->addWidget(m_showFilterCombo);

    topLayout->addWidget(new QLabel("Reason:", m_imagesPage));
    m_methodFilterCombo = new QComboBox(m_imagesPage);
    m_methodFilterCombo->addItem("All Reasons", "");
    m_methodFilterCombo->addItem("pHash - Duplicate", "phash_duplicate");
    m_methodFilterCombo->addItem("pHash - Excluded",  "phash_excluded");
    m_methodFilterCombo->addItem("ML - Not Slide",    "ml_not_slide");
    m_methodFilterCombo->addItem("ML - May Be Slide", "ml_maybe_slide");
    m_methodFilterCombo->addItem("Manual",            "manual");
    topLayout->addWidget(m_methodFilterCombo);

    topLayout->addStretch();

    m_applyBaselineButton = new QPushButton("Apply Baseline", m_imagesPage);
    m_applyBaselineButton->setVisible(false);
    m_applyBaselineButton->setToolTip("Apply the captured crop baseline to all selected extracted slides");
    topLayout->addWidget(m_applyBaselineButton);

    m_autoCropSelectedButton = new QPushButton("Auto Crop", m_imagesPage);
    m_autoCropSelectedButton->setEnabled(false);
    m_autoCropSelectedButton->setToolTip(
        "Auto-detect and crop selected extracted (non-cropped) slides. "
        "Also restores and crops removed ML - May Be Slide items when a slide area is detected; "
        "items where no slide is detected are skipped.");
    topLayout->addWidget(m_autoCropSelectedButton);

    m_restoreCropSelectedButton = new QPushButton("Restore Crop", m_imagesPage);
    m_restoreCropSelectedButton->setEnabled(false);
    m_restoreCropSelectedButton->setToolTip(
        "Restore originals for all selected cropped slides (skips non-cropped slides)");
    topLayout->addWidget(m_restoreCropSelectedButton);

    m_removeDuplicateButton = new QPushButton("Remove Duplicate", m_imagesPage);
    m_removeDuplicateButton->setToolTip(
        "Re-run pHash duplicate removal against this folder's extracted slides "
        "(useful after cropping introduces new visual duplicates)");
    topLayout->addWidget(m_removeDuplicateButton);

    layout->addLayout(topLayout);

    // Grid in scroll area
    m_scrollArea = new QScrollArea(m_imagesPage);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridContainer = new QWidget();
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setSpacing(15);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_scrollArea->setWidget(m_gridContainer);
    layout->addWidget(m_scrollArea);

    // Bottom button bar
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_imageSelectionLabel = new QLabel("Selected: 0 items", m_imagesPage);
    buttonLayout->addWidget(m_imageSelectionLabel);

    m_toggleSelectImagesButton = new QPushButton("Select All", m_imagesPage);
    buttonLayout->addWidget(m_toggleSelectImagesButton);

    buttonLayout->addStretch();

    m_imagesRefreshButton = new QPushButton("Refresh", m_imagesPage);
    m_imagesRefreshButton->setToolTip("Re-scan extracted slides and trash entries for this folder");
    buttonLayout->addWidget(m_imagesRefreshButton);

    m_closeImagesButton = new QPushButton("Close", m_imagesPage);
    buttonLayout->addWidget(m_closeImagesButton);

    m_emptyTrashFolderButton = new QPushButton("Empty Trash", m_imagesPage);
    m_emptyTrashFolderButton->setToolTip("Permanently empty the trash for this folder");
    buttonLayout->addWidget(m_emptyTrashFolderButton);

    m_deleteButton = new QPushButton("Delete Selected", m_imagesPage);
    m_deleteButton->setEnabled(false);
    m_deleteButton->setToolTip("Move selected extracted slides to the trash");
    buttonLayout->addWidget(m_deleteButton);

    m_restoreButton = new QPushButton("Restore Selected", m_imagesPage);
    m_restoreButton->setEnabled(false);
    m_restoreButton->setToolTip("Restore selected removed slides to their original folder");
    buttonLayout->addWidget(m_restoreButton);

    m_thumbSizeSlider = new QSlider(Qt::Horizontal, m_imagesPage);
    m_thumbSizeSlider->setRange(ReviewItemWidget::kMinThumbnailWidth,
                                ReviewItemWidget::kMaxThumbnailWidth);
    m_thumbSizeSlider->setSingleStep(20);
    m_thumbSizeSlider->setPageStep(40);
    m_thumbSizeSlider->setValue(m_thumbnailWidth);
    m_thumbSizeSlider->setFixedWidth(120);
    m_thumbSizeSlider->setFixedHeight(m_restoreButton->sizeHint().height());
    m_thumbSizeSlider->setToolTip("Thumbnail size");
    buttonLayout->addWidget(m_thumbSizeSlider);

    layout->addLayout(buttonLayout);
}

void ReviewSlidesDialog::setupViewerPage()
{
    m_viewerPage = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_viewerPage);

    // Top bar: Back + title
    QHBoxLayout* topLayout = new QHBoxLayout();
    m_viewerBackButton = new QPushButton("< Back", m_viewerPage);
    topLayout->addWidget(m_viewerBackButton);

    m_viewerTitleLabel = new QLabel("", m_viewerPage);
    m_viewerTitleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; }");
    topLayout->addWidget(m_viewerTitleLabel);

    topLayout->addStretch();
    layout->addLayout(topLayout);

    // Image view
    m_cropView = new CropImageView(m_viewerPage);
    layout->addWidget(m_cropView, 1);

    // Action bar
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->addStretch();

    m_cropButton = new QPushButton("Crop", m_viewerPage);
    m_cropButton->setToolTip("Draw a crop rectangle on this slide");
    actionLayout->addWidget(m_cropButton);

    m_autoCropButton = new QPushButton("Auto Crop", m_viewerPage);
    m_autoCropButton->setToolTip("Detect the slide automatically and pre-fill the crop rectangle");
    actionLayout->addWidget(m_autoCropButton);

    m_restoreCropButton = new QPushButton("Restore Crop", m_viewerPage);
    m_restoreCropButton->setToolTip("Revert to the original (uncropped) image");
    actionLayout->addWidget(m_restoreCropButton);

    m_recropButton = new QPushButton("Recrop", m_viewerPage);
    m_recropButton->setToolTip("Re-edit the crop starting from the original image");
    actionLayout->addWidget(m_recropButton);

    m_applyCropButton = new QPushButton("Apply Crop", m_viewerPage);
    m_applyCropButton->setToolTip("Save the cropped image (replaces the live slide)");
    actionLayout->addWidget(m_applyCropButton);

    m_cancelCropButton = new QPushButton("Cancel", m_viewerPage);
    m_cancelCropButton->setToolTip("Cancel the crop selection");
    actionLayout->addWidget(m_cancelCropButton);

    layout->addLayout(actionLayout);
}

void ReviewSlidesDialog::connectSignals()
{
    // Folders page
    connect(m_foldersRefreshButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onRefreshFolders);
    connect(m_folderTable, &QTableWidget::cellChanged, this, &ReviewSlidesDialog::onFolderCheckChanged);
    connect(m_toggleSelectFoldersButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onToggleSelectFolders);
    connect(m_emptyTrashFoldersButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onEmptyTrashFolders);
    connect(m_deleteFolderButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onDeleteSelectedFolders);
    connect(m_closeFoldersButton, &QPushButton::clicked, this, &QDialog::accept);

    // Images page
    connect(m_backButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onBackToFolders);
    connect(m_showFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ReviewSlidesDialog::onShowFilterChanged);
    connect(m_methodFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ReviewSlidesDialog::onMethodFilterChanged);
    connect(m_toggleSelectImagesButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onToggleSelectImages);
    connect(m_restoreButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onRestoreSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onDeleteSelected);
    connect(m_emptyTrashFolderButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onEmptyTrashCurrentFolder);
    connect(m_imagesRefreshButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onRefreshImages);
    connect(m_closeImagesButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_thumbSizeSlider, &QSlider::valueChanged,
            this, &ReviewSlidesDialog::onThumbnailSizeChanged);
    connect(m_applyBaselineButton, &QPushButton::clicked,
            this, &ReviewSlidesDialog::onApplyBaseline);
    connect(m_autoCropSelectedButton, &QPushButton::clicked,
            this, &ReviewSlidesDialog::onAutoCropSelected);
    connect(m_restoreCropSelectedButton, &QPushButton::clicked,
            this, &ReviewSlidesDialog::onRestoreCropSelected);
    connect(m_removeDuplicateButton, &QPushButton::clicked,
            this, &ReviewSlidesDialog::onRemoveDuplicates);

    // Viewer page
    connect(m_viewerBackButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onViewerBack);
    connect(m_cropButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onStartCrop);
    connect(m_recropButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onStartCrop);
    connect(m_applyCropButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onApplyCrop);
    connect(m_cancelCropButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onCancelCrop);
    connect(m_restoreCropButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onRestoreCrop);
    connect(m_autoCropButton, &QPushButton::clicked, this, &ReviewSlidesDialog::onAutoCrop);
}

// ==================== Folder enumeration ====================

QStringList ReviewSlidesDialog::enumerateAllFolderNames() const
{
    QSet<QString> folderSet;

    QDir outputDir(m_baseOutputDir);
    QStringList diskFolders = outputDir.entryList(
        QStringList() << "slides_*",
        QDir::Dirs | QDir::NoDotAndDotDot
    );
    for (const QString& f : diskFolders) {
        folderSet.insert(f);
    }

    for (const TrashEntry& entry : m_allEntries) {
        QString folderName = entry.originalFolder;
        if (folderName.isEmpty()) {
            folderName = QString("slides_%1").arg(entry.videoName);
        }
        if (!folderName.isEmpty()) {
            folderSet.insert(folderName);
        }
    }

    QStringList result = folderSet.values();
    naturalSort(result);
    return result;
}

int ReviewSlidesDialog::countExtractedInFolder(const QString& folderPath) const
{
    QDir folder(folderPath);
    if (!folder.exists()) {
        return 0;
    }
    return folder.entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
        QDir::Files
    ).count();
}

int ReviewSlidesDialog::countRemovedInFolder(const QString& folderName) const
{
    int count = 0;
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == folderName) {
            count++;
        }
    }
    return count;
}

QString ReviewSlidesDialog::stripSlidesPrefix(const QString& folderName) const
{
    if (folderName.startsWith("slides_")) {
        return folderName.mid(7);
    }
    return folderName;
}

void ReviewSlidesDialog::loadFolders()
{
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();

    populateFolderTable();
}

void ReviewSlidesDialog::loadCropEntries()
{
    QString cropDir = CropManager::cropDirectory(m_baseOutputDir);
    m_allCropEntries = CropMetadata::getEntries(cropDir);
}

bool ReviewSlidesDialog::isLivePathCropped(const QString& livePath, CropEntry* outEntry) const
{
    QFileInfo info(livePath);
    QString liveFilename = info.fileName();
    QString liveFolder = info.dir().dirName();

    QRegularExpression re("^slide_(.+)_(\\d{3})\\.jpg$");
    QRegularExpressionMatch match = re.match(liveFilename);
    if (!match.hasMatch()) return false;

    QString videoName = match.captured(1);
    QString slideIndex = match.captured(2);

    for (const CropEntry& e : m_allCropEntries) {
        if (e.videoName == videoName && e.slideIndex == slideIndex
            && (e.originalFolder == liveFolder || e.originalFolder.isEmpty())) {
            if (outEntry) *outEntry = e;
            return true;
        }
    }
    return false;
}

void ReviewSlidesDialog::populateFolderTable()
{
    m_folderTable->blockSignals(true);
    m_folderTable->setRowCount(0);

    QStringList folderNames = enumerateAllFolderNames();
    QDir outputDir(m_baseOutputDir);

    for (const QString& folderName : folderNames) {
        QString folderPath = outputDir.filePath(folderName);
        int extractedCount = countExtractedInFolder(folderPath);
        int removedCount = countRemovedInFolder(folderName);

        int row = m_folderTable->rowCount();
        m_folderTable->insertRow(row);

        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(Qt::UserRole, folderName);
        m_folderTable->setItem(row, F_SELECT, checkItem);

        QString displayName = stripSlidesPrefix(folderName);
        QTableWidgetItem* nameItem = new QTableWidgetItem(displayName);
        nameItem->setToolTip(folderPath);
        m_folderTable->setItem(row, F_NAME, nameItem);

        QTableWidgetItem* extractedItem = new QTableWidgetItem(QString::number(extractedCount));
        extractedItem->setTextAlignment(Qt::AlignCenter);
        m_folderTable->setItem(row, F_EXTRACTED, extractedItem);

        QTableWidgetItem* removedItem = new QTableWidgetItem(QString::number(removedCount));
        removedItem->setTextAlignment(Qt::AlignCenter);
        if (removedCount > 0) {
            removedItem->setForeground(QColor("#d32f2f"));
        }
        m_folderTable->setItem(row, F_REMOVED, removedItem);

        QPushButton* reviewButton = new QPushButton("Review", m_folderTable);
        connect(reviewButton, &QPushButton::clicked, this, [this, row]() {
            onReviewFolder(row);
        });
        m_folderTable->setCellWidget(row, F_REVIEW, reviewButton);
    }

    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();

    // Remember which folder was last reviewed so navigating back lands on it.
    if (!m_currentFolderName.isEmpty()) {
        for (int i = 0; i < m_folderTable->rowCount(); ++i) {
            QTableWidgetItem* item = m_folderTable->item(i, F_SELECT);
            if (item && item->data(Qt::UserRole).toString() == m_currentFolderName) {
                m_folderTable->selectRow(i);
                m_folderTable->scrollToItem(item, QAbstractItemView::EnsureVisible);
                break;
            }
        }
    }
}

QStringList ReviewSlidesDialog::getCheckedFolderNames() const
{
    QStringList names;
    for (int i = 0; i < m_folderTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, F_SELECT);
        if (item && item->checkState() == Qt::Checked) {
            names.append(item->data(Qt::UserRole).toString());
        }
    }
    return names;
}

void ReviewSlidesDialog::updateFolderSelectionLabel()
{
    int n = getCheckedFolderNames().count();
    m_folderSelectionLabel->setText(QString("Selected: %1 folders").arg(n));
    m_emptyTrashFoldersButton->setEnabled(n > 0);
    m_deleteFolderButton->setEnabled(n > 0);

    const int rowCount = m_folderTable->rowCount();
    const bool allChecked = (rowCount > 0 && n == rowCount);
    m_toggleSelectFoldersButton->setText(allChecked ? "Deselect All" : "Select All");
}

// ==================== Folders-page slots ====================

void ReviewSlidesDialog::onFolderCheckChanged(int row, int column)
{
    Q_UNUSED(row);
    if (column == F_SELECT) {
        updateFolderSelectionLabel();
    }
}

void ReviewSlidesDialog::onToggleSelectFolders()
{
    const int rowCount = m_folderTable->rowCount();
    int checkedCount = 0;
    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, F_SELECT);
        if (item && item->checkState() == Qt::Checked) ++checkedCount;
    }
    const Qt::CheckState newState = (rowCount > 0 && checkedCount == rowCount)
                                        ? Qt::Unchecked : Qt::Checked;
    m_folderTable->blockSignals(true);
    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, F_SELECT);
        if (item) {
            item->setCheckState(newState);
        }
    }
    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();
}

void ReviewSlidesDialog::onRefreshFolders()
{
    loadFolders();
}

void ReviewSlidesDialog::onRefreshImages()
{
    if (m_currentFolderName.isEmpty()) return;

    // Re-scan trash metadata + crop metadata + folder grid for this folder.
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();
    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
}

void ReviewSlidesDialog::onEmptyTrashFolders()
{
    QStringList selected = getCheckedFolderNames();
    if (selected.isEmpty()) {
        return;
    }

    QString action = m_emptyTrashToSystemTrash ? "moving to system trash" : "deleting";
    emit statusMessage(QString("Emptying trash for %1 folder(s) (%2)...")
                       .arg(selected.size())
                       .arg(action));

    int totalRemoved = 0;
    for (const QString& folderName : selected) {
        totalRemoved += TrashManager::emptyApplicationTrashForFolder(
            m_baseOutputDir, folderName, m_emptyTrashToSystemTrash);
    }

    if (totalRemoved > 0) {
        emit statusMessage(QString("Emptied trash: %1 image(s) %2")
                           .arg(totalRemoved)
                           .arg(m_emptyTrashToSystemTrash ? "moved to system trash" : "deleted"));
        emit trashEmptied();
    } else {
        emit statusMessage("No items removed from trash");
    }

    loadFolders();
}

void ReviewSlidesDialog::onDeleteSelectedFolders()
{
    QStringList selected = getCheckedFolderNames();
    if (selected.isEmpty()) {
        return;
    }

    emit statusMessage(QString("Deleting %1 folder(s) to system trash...").arg(selected.size()));

    int trashItemsMoved = 0;
    int cropItemsMoved = 0;
    int foldersMoved = 0;
    int folderFailures = 0;

    QDir outputDir(m_baseOutputDir);

    for (const QString& folderName : selected) {
        // Move the corresponding trash entries (and their metadata) to the system trash first
        trashItemsMoved += TrashManager::emptyApplicationTrashForFolder(
            m_baseOutputDir, folderName, true /* moveToSystemTrash */);

        // Move the corresponding crop-backup entries (and their metadata) to system trash
        cropItemsMoved += CropManager::clearCropsForFolder(
            m_baseOutputDir, folderName, true /* moveToSystemTrash */);

        // Move the live folder itself if it still exists on disk
        QString folderPath = outputDir.filePath(folderName);
        if (QFileInfo::exists(folderPath)) {
            if (QFile::moveToTrash(folderPath)) {
                foldersMoved++;
            } else {
                folderFailures++;
                qWarning() << "ReviewSlidesDialog: Failed to move folder to system trash:" << folderPath;
            }
        }

        // If we just removed the folder we last reviewed, drop the saved selection.
        if (m_currentFolderName == folderName) {
            m_currentFolderName.clear();
        }
    }

    if (folderFailures == 0) {
        emit statusMessage(QString("Moved %1 folder(s), %2 trash item(s), %3 crop backup(s) to system trash")
                           .arg(foldersMoved).arg(trashItemsMoved).arg(cropItemsMoved));
    } else {
        emit statusMessage(QString("Moved %1 folder(s) (%2 failed), %3 trash item(s), %4 crop backup(s) to system trash")
                           .arg(foldersMoved).arg(folderFailures).arg(trashItemsMoved).arg(cropItemsMoved));
    }

    if (trashItemsMoved > 0 || foldersMoved > 0) {
        emit trashEmptied();
    }

    loadFolders();
}

void ReviewSlidesDialog::onReviewFolder(int row)
{
    QTableWidgetItem* item = m_folderTable->item(row, F_SELECT);
    if (!item) return;

    QString folderName = item->data(Qt::UserRole).toString();
    enterImagesPage(folderName);
}

// ==================== Images page ====================

void ReviewSlidesDialog::enterImagesPage(const QString& folderName)
{
    m_currentFolderName = folderName;
    setWindowTitle(QString("Slides Review — %1").arg(stripSlidesPrefix(folderName)));

    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == folderName) {
            m_currentFolderRemoved.append(entry);
        }
    }

    m_stack->setCurrentIndex(1);
    loadFolderItems();
}

void ReviewSlidesDialog::clearImageGrid()
{
    for (ReviewItemWidget* widget : m_itemWidgets) {
        m_gridLayout->removeWidget(widget);
        delete widget;
    }
    m_itemWidgets.clear();
}

void ReviewSlidesDialog::loadFolderItems()
{
    clearImageGrid();

    int showFilter = m_showFilterCombo->currentData().toInt();
    QString methodFilter = m_methodFilterCombo->currentData().toString();

    QDir outputDir(m_baseOutputDir);
    QString folderPath = outputDir.filePath(m_currentFolderName);

    // Merge extracted + removed into a single list keyed by the original
    // slide filename so they interleave in slide-index order regardless of state.
    struct UnifiedItem {
        QString sortKey;
        bool extracted;
        QString imagePath;
        TrashEntry entry;
    };
    QList<UnifiedItem> items;

    if (showFilter != ShowRemovedOnly) {
        QDir folder(folderPath);
        if (folder.exists()) {
            QStringList images = folder.entryList(
                QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
                QDir::Files
            );
            for (const QString& imageName : images) {
                items.append({imageName, true, folder.filePath(imageName), TrashEntry()});
            }
        }
    }

    if (showFilter != ShowExtractedOnly) {
        for (const TrashEntry& entry : m_currentFolderRemoved) {
            if (!methodFilter.isEmpty() && entry.category != methodFilter) {
                continue;
            }
            QString sortKey = QString("slide_%1_%2.jpg").arg(entry.videoName, entry.slideIndex);
            items.append({sortKey, false, QString(), entry});
        }
    }

    std::sort(items.begin(), items.end(), [](const UnifiedItem& a, const UnifiedItem& b) {
        return naturalLessThan(a.sortKey, b.sortKey);
    });

    for (const UnifiedItem& item : items) {
        ReviewItemWidget* w = item.extracted
            ? new ReviewItemWidget(item.imagePath, m_gridContainer)
            : new ReviewItemWidget(item.entry, m_baseOutputDir, m_gridContainer);
        w->setThumbnailWidth(m_thumbnailWidth);
        if (item.extracted && isLivePathCropped(item.imagePath)) {
            w->setCropped(true);
        }
        connect(w, &ReviewItemWidget::selectionChanged,
                this, &ReviewSlidesDialog::onItemSelectionChanged);
        connect(w, &ReviewItemWidget::viewClicked, this, [this, w]() {
            enterViewerPage(w);
        });
        connect(w, &ReviewItemWidget::setBaselineClicked, this, [this, w]() {
            onSetBaselineFromItem(w);
        });
        m_itemWidgets.append(w);
    }

    m_lastLaidOutColumns = 0;
    relayoutGrid();

    m_methodFilterCombo->setEnabled(showFilter != ShowExtractedOnly);

    updateImageSelectionLabel();
    updateImageActionButtons();
}

int ReviewSlidesDialog::computeColumns() const
{
    int viewport = m_scrollArea->viewport()->width();
    int spacing = m_gridLayout->spacing();
    QMargins m = m_gridLayout->contentsMargins();
    int avail = viewport - m.left() - m.right();
    int itemWidth = m_thumbnailWidth + 20;
    if (itemWidth <= 0) {
        return 2;
    }
    int n = (avail + spacing) / (itemWidth + spacing);
    return std::max(2, n);
}

void ReviewSlidesDialog::relayoutGrid()
{
    int columns = computeColumns();
    if (columns == m_lastLaidOutColumns && !m_itemWidgets.isEmpty() &&
        m_gridLayout->itemAt(0) != nullptr) {
        return;
    }
    m_lastLaidOutColumns = columns;

    for (ReviewItemWidget* w : m_itemWidgets) {
        m_gridLayout->removeWidget(w);
    }

    int row = 0;
    int col = 0;
    for (ReviewItemWidget* w : m_itemWidgets) {
        m_gridLayout->addWidget(w, row, col);
        col++;
        if (col >= columns) { col = 0; row++; }
    }
}

void ReviewSlidesDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_stack && m_stack->currentIndex() == 1 && !m_itemWidgets.isEmpty()) {
        relayoutGrid();
    }
}

void ReviewSlidesDialog::onThumbnailSizeChanged(int value)
{
    m_thumbnailWidth = value;
    for (ReviewItemWidget* w : m_itemWidgets) {
        w->setThumbnailWidth(value);
    }
    m_lastLaidOutColumns = 0;
    relayoutGrid();
}

void ReviewSlidesDialog::applyImageFilters()
{
    loadFolderItems();
}

QList<ReviewItemWidget*> ReviewSlidesDialog::getCheckedItems() const
{
    QList<ReviewItemWidget*> checked;
    for (ReviewItemWidget* w : m_itemWidgets) {
        if (w->isChecked()) {
            checked.append(w);
        }
    }
    return checked;
}

void ReviewSlidesDialog::updateImageSelectionLabel()
{
    int n = getCheckedItems().count();
    m_imageSelectionLabel->setText(QString("Selected: %1 items").arg(n));

    const int total = m_itemWidgets.count();
    const bool allChecked = (total > 0 && n == total);
    m_toggleSelectImagesButton->setText(allChecked ? "Deselect All" : "Select All");
}

void ReviewSlidesDialog::updateImageActionButtons()
{
    int extractedChecked = 0;
    int removedChecked = 0;
    int removedMaybeSlide = 0;       // ml_maybe_slide removed items (auto-crop eligible)
    int removedOtherChecked = 0;     // any removed item with a different category
    for (ReviewItemWidget* w : m_itemWidgets) {
        if (!w->isChecked()) continue;
        if (w->state() == ReviewItemWidget::Extracted) {
            extractedChecked++;
        } else {
            removedChecked++;
            if (w->getEntry().category == "ml_maybe_slide") {
                removedMaybeSlide++;
            } else {
                removedOtherChecked++;
            }
        }
    }
    m_deleteButton->setEnabled(extractedChecked > 0);
    m_restoreButton->setEnabled(removedChecked > 0);
    const bool extractedOnly = (extractedChecked > 0 && removedChecked == 0);
    // Auto Crop: enable iff every checked item is either Extracted or removed ml_maybe_slide,
    // and at least one item is checked.
    const bool autoCropOk = (removedOtherChecked == 0) &&
                            ((extractedChecked + removedMaybeSlide) > 0);
    m_autoCropSelectedButton->setEnabled(autoCropOk);
    m_restoreCropSelectedButton->setEnabled(extractedOnly);
    updateBaselineButtonState();
}

// ==================== Images-page slots ====================

void ReviewSlidesDialog::onBackToFolders()
{
    // Keep m_currentFolderName set so populateFolderTable can re-select the row.
    m_currentFolderRemoved.clear();
    clearImageGrid();
    resetBaselineState();
    loadFolders();
    m_stack->setCurrentIndex(0);
    setWindowTitle("Slides Review");
}

void ReviewSlidesDialog::onShowFilterChanged()
{
    applyImageFilters();
}

void ReviewSlidesDialog::onMethodFilterChanged()
{
    applyImageFilters();
}

void ReviewSlidesDialog::onToggleSelectImages()
{
    const int total = m_itemWidgets.count();
    int checkedCount = 0;
    for (ReviewItemWidget* w : m_itemWidgets) {
        if (w->isChecked()) ++checkedCount;
    }
    const bool selectAll = !(total > 0 && checkedCount == total);
    for (ReviewItemWidget* w : m_itemWidgets) {
        w->setChecked(selectAll);
    }
}

void ReviewSlidesDialog::onItemSelectionChanged()
{
    updateImageSelectionLabel();
    updateImageActionButtons();
}

void ReviewSlidesDialog::onRestoreSelected()
{
    QList<ReviewItemWidget*> checked = getCheckedItems();

    int successCount = 0;
    int failCount = 0;
    int skipped = 0;
    QStringList failedFiles;

    for (ReviewItemWidget* w : checked) {
        if (w->state() != ReviewItemWidget::Removed) {
            skipped++;
            continue;
        }
        QString trashedFilename = w->getTrashedFilename();
        if (TrashManager::restoreFromApplicationTrash(trashedFilename, m_baseOutputDir)) {
            successCount++;
        } else {
            failCount++;
            failedFiles.append(trashedFilename);
        }
    }

    if (successCount == 0 && failCount == 0) {
        emit statusMessage("No removed items selected to restore");
        return;
    }

    if (failCount == 0) {
        emit statusMessage(QString("Restored %1 image(s)").arg(successCount));
    } else {
        emit statusMessage(QString("Restored %1 image(s), %2 failed - check log for details")
                           .arg(successCount).arg(failCount));
        qWarning() << "ReviewSlidesDialog: Failed to restore" << failCount << "files:" << failedFiles;
    }

    if (successCount > 0) {
        emit filesRestored(successCount);
    }

    // Reload trash data and rebuild current folder grid
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();
    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
}

void ReviewSlidesDialog::onDeleteSelected()
{
    QList<ReviewItemWidget*> checked = getCheckedItems();

    int successCount = 0;
    int failCount = 0;
    int skipped = 0;

    for (ReviewItemWidget* w : checked) {
        if (w->state() != ReviewItemWidget::Extracted) {
            skipped++;
            continue;
        }
        QString imagePath = w->getImagePath();
        if (TrashManager::moveToApplicationTrash(imagePath, m_baseOutputDir, "manual", "manual", "User deleted")) {
            successCount++;
        } else {
            failCount++;
            qWarning() << "ReviewSlidesDialog: Failed to move to trash:" << imagePath;
        }
    }

    if (successCount == 0 && failCount == 0) {
        emit statusMessage("No extracted items selected to delete");
        return;
    }

    if (failCount == 0) {
        emit statusMessage(QString("Deleted %1 image(s)").arg(successCount));
    } else {
        emit statusMessage(QString("Deleted %1 image(s), %2 failed").arg(successCount).arg(failCount));
    }

    if (successCount > 0) {
        emit filesDeleted(successCount);
    }

    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();
    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
}

void ReviewSlidesDialog::onEmptyTrashCurrentFolder()
{
    if (m_currentFolderName.isEmpty()) return;

    if (m_currentFolderRemoved.isEmpty()) {
        emit statusMessage("No removed items in this folder");
        return;
    }

    QString action = m_emptyTrashToSystemTrash ? "moving to system trash" : "deleting";
    emit statusMessage(QString("Emptying trash for this folder (%1)...").arg(action));

    int removed = TrashManager::emptyApplicationTrashForFolder(
        m_baseOutputDir, m_currentFolderName, m_emptyTrashToSystemTrash);

    if (removed > 0) {
        emit statusMessage(QString("Emptied trash: %1 image(s) %2")
                           .arg(removed)
                           .arg(m_emptyTrashToSystemTrash ? "moved to system trash" : "deleted"));
        emit trashEmptied();
    } else {
        emit statusMessage("No items removed from trash");
    }

    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();
    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
}

// ==================== Viewer page ====================

void ReviewSlidesDialog::enterViewerPage(ReviewItemWidget* item)
{
    if (!item) return;

    m_viewerSelecting = false;

    if (item->state() == ReviewItemWidget::Extracted) {
        m_viewerIsExtracted = true;
        m_viewerLivePath = item->getImagePath();
        m_viewerTrashedPath.clear();

        QFileInfo info(m_viewerLivePath);
        m_viewerTitleLabel->setText(info.fileName());
        setWindowTitle(QString("Slides Review — %1 — %2")
                       .arg(stripSlidesPrefix(m_currentFolderName), info.fileName()));
        m_viewerIsCropped = isLivePathCropped(m_viewerLivePath);
    } else {
        m_viewerIsExtracted = false;
        m_viewerLivePath.clear();
        const TrashEntry& entry = item->getEntry();
        m_viewerTrashedPath = entry.getTrashedPath(m_baseOutputDir);
        m_viewerTitleLabel->setText(QString("%1 (Removed)").arg(entry.getDisplayName()));
        setWindowTitle(QString("Slides Review — %1 — %2 (Removed)")
                       .arg(stripSlidesPrefix(m_currentFolderName), entry.getDisplayName()));
        m_viewerIsCropped = false;
    }

    refreshViewerImage();
    updateViewerButtons();
    m_stack->setCurrentIndex(2);
}

void ReviewSlidesDialog::refreshViewerImage()
{
    QString path = m_viewerIsExtracted ? m_viewerLivePath : m_viewerTrashedPath;
    QPixmap pix(path);
    m_cropView->setImage(pix);
    m_cropView->setSelectionMode(false);
}

void ReviewSlidesDialog::setViewerCropMode(bool selecting)
{
    m_viewerSelecting = selecting;
    m_cropView->setSelectionMode(selecting);
    if (!selecting) {
        m_cropView->clearSelection();
    }
    updateViewerButtons();
}

void ReviewSlidesDialog::updateViewerButtons()
{
    bool extracted = m_viewerIsExtracted;
    bool cropped = m_viewerIsCropped;
    bool selecting = m_viewerSelecting;

    // Selecting state: only Apply / Cancel are visible. Back is hidden during selection
    // to prevent accidental data loss; user must explicitly Cancel.
    m_cropButton->setVisible(extracted && !cropped && !selecting);
    m_autoCropButton->setVisible(extracted && !selecting);
    m_restoreCropButton->setVisible(extracted && cropped && !selecting);
    m_recropButton->setVisible(extracted && cropped && !selecting);
    m_applyCropButton->setVisible(extracted && selecting);
    m_cancelCropButton->setVisible(extracted && selecting);

    m_viewerBackButton->setEnabled(!selecting);
}

void ReviewSlidesDialog::onViewerBack()
{
    if (m_viewerSelecting) return; // Should not be reachable when selecting (button disabled).
    // Refresh metadata + folder grid so any thumbnail changes (cropped image + badge) show up.
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();

    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
    m_stack->setCurrentIndex(1);
    setWindowTitle(QString("Slides Review — %1").arg(stripSlidesPrefix(m_currentFolderName)));
}

void ReviewSlidesDialog::onStartCrop()
{
    if (!m_viewerIsExtracted) return;

    setViewerCropMode(true);

    // For recrop, pre-populate the previous selection rect.
    if (m_viewerIsCropped) {
        CropEntry entry;
        if (isLivePathCropped(m_viewerLivePath, &entry)) {
            // Show the original (uncropped) image so the user can see the full
            // canvas while re-selecting.
            QString backupPath = entry.getBackupPath(m_baseOutputDir);
            if (QFile::exists(backupPath)) {
                QPixmap backup(backupPath);
                if (!backup.isNull()) {
                    m_cropView->setImage(backup);
                    m_cropView->setSelectionMode(true);
                }
            }
            m_cropView->setInitialSelection(entry.cropRect());
        }
    }
}

void ReviewSlidesDialog::onApplyCrop()
{
    if (!m_viewerIsExtracted || !m_viewerSelecting) return;

    if (!m_cropView->hasSelection()) {
        emit statusMessage("Draw a crop rectangle before applying");
        return;
    }

    QRect rect = m_cropView->selectionImageRect();
    if (!CropManager::applyCrop(m_viewerLivePath, m_baseOutputDir, rect, m_jpegQuality)) {
        emit statusMessage("Failed to apply crop");
        return;
    }

    emit statusMessage(QString("Cropped %1 to %2x%3")
                       .arg(QFileInfo(m_viewerLivePath).fileName())
                       .arg(rect.width()).arg(rect.height()));

    // Reload crop metadata so isLivePathCropped() reflects the new state.
    loadCropEntries();
    m_viewerIsCropped = true;

    // Show the cropped live file in the viewer.
    setViewerCropMode(false);
    refreshViewerImage();
    updateViewerButtons();
}

void ReviewSlidesDialog::onCancelCrop()
{
    if (!m_viewerSelecting) return;
    setViewerCropMode(false);
    // If we were recropping we showed the backup image — restore the live view.
    refreshViewerImage();
    updateViewerButtons();
}

void ReviewSlidesDialog::onRestoreCrop()
{
    if (!m_viewerIsExtracted || !m_viewerIsCropped) return;

    CropEntry entry;
    if (!isLivePathCropped(m_viewerLivePath, &entry)) {
        emit statusMessage("No crop entry to restore");
        return;
    }

    if (!CropManager::restoreCrop(entry.backupFilename, m_baseOutputDir)) {
        emit statusMessage("Failed to restore original");
        return;
    }

    emit statusMessage(QString("Restored original for %1").arg(entry.getDisplayName()));

    loadCropEntries();
    m_viewerIsCropped = false;
    refreshViewerImage();
    updateViewerButtons();
}

void ReviewSlidesDialog::onAutoCrop()
{
    if (!m_viewerIsExtracted) return;

    // Choose the source path: if the slide is already cropped, run detection
    // against the original backup (so the user can re-select from the full
    // canvas); otherwise run against the live image.
    QString sourcePath = m_viewerLivePath;
    QPixmap sourcePixmap;
    if (m_viewerIsCropped) {
        CropEntry entry;
        if (isLivePathCropped(m_viewerLivePath, &entry)) {
            const QString backupPath = entry.getBackupPath(m_baseOutputDir);
            if (QFile::exists(backupPath)) {
                sourcePath = backupPath;
                sourcePixmap.load(backupPath);
            }
        }
    }

    if (!m_autoCropDetector) {
        m_autoCropDetector = std::make_unique<AutoCropDetector>(m_autoCropConfig);
    } else {
        m_autoCropDetector->updateConfig(m_autoCropConfig);
    }

    AutoCropResult result = m_autoCropDetector->detect(sourcePath);

    // Show the same image we ran detection against, then enter selection mode.
    if (m_viewerIsCropped && !sourcePixmap.isNull()) {
        m_cropView->setImage(sourcePixmap);
    }
    setViewerCropMode(true);

    if (result.isValid()) {
        m_cropView->setInitialSelection(result.bbox);
        const char* backendName =
            (result.backend == AutoCropResult::Backend::Canny) ? "canny" :
            (result.backend == AutoCropResult::Backend::Yolo)  ? "yolo"  : "none";
        emit statusMessage(QString("Auto crop: %1 detected %2x%3 (%4 ms) — review and click Apply Crop")
                           .arg(QString::fromLatin1(backendName))
                           .arg(result.bbox.width())
                           .arg(result.bbox.height())
                           .arg(QString::number(result.durationMs, 'f', 0)));
    } else {
        QString detail = result.errorMessage.isEmpty()
            ? QStringLiteral("no slide detected — draw manually")
            : QString("detection failed: %1").arg(result.errorMessage);
        emit statusMessage(QString("Auto crop: %1").arg(detail));
    }
}

// ==================== Crop baseline ====================

void ReviewSlidesDialog::onSetBaselineFromItem(ReviewItemWidget* item)
{
    if (!item || item->state() != ReviewItemWidget::Extracted || !item->isCropped()) return;

    CropEntry entry;
    if (!isLivePathCropped(item->getImagePath(), &entry)) return;

    QPixmap backupPix(entry.getBackupPath(m_baseOutputDir));
    if (backupPix.isNull()) return;

    m_baselineActive = true;
    m_baselineRectImagePx = entry.cropRect();
    m_baselineSourceSize = backupPix.size();
    m_baselineSourceLabel = entry.getDisplayName();

    item->setChecked(false);
    m_applyBaselineButton->setVisible(true);
    emit statusMessage(QString("Baseline captured from %1 — select other extracted slides and click Apply Baseline")
                       .arg(m_baselineSourceLabel));
}

void ReviewSlidesDialog::onApplyBaseline()
{
    if (!m_baselineActive) return;

    QList<ReviewItemWidget*> targets;
    for (ReviewItemWidget* w : m_itemWidgets) {
        if (w->isChecked() && w->state() == ReviewItemWidget::Extracted && !w->isCropped()) {
            targets.append(w);
        }
    }
    if (targets.isEmpty()) {
        emit statusMessage("No extracted non-cropped slides selected");
        return;
    }

    int successCount = 0;
    for (ReviewItemWidget* w : targets) {
        QString livePath = w->getImagePath();
        QPixmap targetPix(livePath);
        if (targetPix.isNull()) continue;

        QSize targetSize = targetPix.size();
        double sx = static_cast<double>(targetSize.width()) / m_baselineSourceSize.width();
        double sy = static_cast<double>(targetSize.height()) / m_baselineSourceSize.height();

        QRect scaledRect(
            static_cast<int>(std::round(m_baselineRectImagePx.x() * sx)),
            static_cast<int>(std::round(m_baselineRectImagePx.y() * sy)),
            static_cast<int>(std::round(m_baselineRectImagePx.width() * sx)),
            static_cast<int>(std::round(m_baselineRectImagePx.height() * sy))
        );

        scaledRect = scaledRect.intersected(QRect(0, 0, targetSize.width(), targetSize.height()));
        if (scaledRect.isEmpty()) continue;

        if (CropManager::applyCrop(livePath, m_baseOutputDir, scaledRect, m_jpegQuality)) {
            successCount++;
        }
    }

    emit statusMessage(QString("Applied baseline crop to %1 of %2 slides")
                       .arg(successCount).arg(targets.size()));

    loadCropEntries();
    resetBaselineState();
    loadFolderItems();
}

void ReviewSlidesDialog::onAutoCropSelected()
{
    QList<ReviewItemWidget*> targets;
    for (ReviewItemWidget* w : m_itemWidgets) {
        if (!w->isChecked()) continue;
        if (w->state() == ReviewItemWidget::Extracted) {
            if (!w->isCropped()) targets.append(w);
        } else if (w->getEntry().category == "ml_maybe_slide") {
            // Removed by ML as may_be_slide — eligible for "restore + auto crop on detection"
            targets.append(w);
        }
    }
    if (targets.isEmpty()) {
        emit statusMessage("No eligible slides selected (extracted non-cropped or removed ML - May Be Slide)");
        return;
    }

    if (!m_autoCropDetector) {
        m_autoCropDetector = std::make_unique<AutoCropDetector>(m_autoCropConfig);
    } else {
        m_autoCropDetector->updateConfig(m_autoCropConfig);
    }

    int successCount = 0;
    int restoredCount = 0;
    int notDetectedCount = 0;
    int failedCount = 0;
    for (ReviewItemWidget* w : targets) {
        if (w->state() == ReviewItemWidget::Extracted) {
            const QString livePath = w->getImagePath();
            AutoCropResult result = m_autoCropDetector->detect(livePath);
            if (!result.isValid()) {
                notDetectedCount++;
                continue;
            }
            if (CropManager::applyCrop(livePath, m_baseOutputDir, result.bbox, m_jpegQuality)) {
                successCount++;
            } else {
                failedCount++;
            }
        } else {
            // Removed ml_maybe_slide: detect on trashed file; if found, restore then crop.
            const TrashEntry& entry = w->getEntry();
            const QString trashedPath = entry.getTrashedPath(m_baseOutputDir);
            AutoCropResult result = m_autoCropDetector->detect(trashedPath);
            if (!result.isValid()) {
                notDetectedCount++;
                continue;
            }
            const QString originalPath = entry.getOriginalPath(m_baseOutputDir);
            if (!TrashManager::restoreFromApplicationTrash(entry.trashedFilename, m_baseOutputDir)) {
                failedCount++;
                continue;
            }
            if (!CropManager::applyCrop(originalPath, m_baseOutputDir, result.bbox, m_jpegQuality)) {
                // File is restored but crop failed — count as failed; user can crop manually.
                failedCount++;
                continue;
            }
            restoredCount++;
            successCount++;
        }
    }

    QString msg = QString("Auto crop: cropped %1 of %2 selected").arg(successCount).arg(targets.size());
    if (restoredCount > 0) msg += QString(" (%1 restored from trash)").arg(restoredCount);
    if (notDetectedCount > 0) msg += QString(" (%1 skipped, no slide detected)").arg(notDetectedCount);
    if (failedCount > 0) msg += QString(" (%1 failed to apply)").arg(failedCount);
    emit statusMessage(msg);

    // Reload trash metadata + current-folder removed list so any items that were
    // restored out of trash disappear from the grid (otherwise stale entries would
    // render as "no preview" alongside the new extracted thumbnails).
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();
    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
}

void ReviewSlidesDialog::onRestoreCropSelected()
{
    QList<CropEntry> targets;
    for (ReviewItemWidget* w : m_itemWidgets) {
        if (!w->isChecked() || w->state() != ReviewItemWidget::Extracted || !w->isCropped()) continue;
        CropEntry entry;
        if (isLivePathCropped(w->getImagePath(), &entry)) {
            targets.append(entry);
        }
    }
    if (targets.isEmpty()) {
        emit statusMessage("No cropped slides selected");
        return;
    }

    int successCount = 0;
    for (const CropEntry& entry : targets) {
        if (CropManager::restoreCrop(entry.backupFilename, m_baseOutputDir)) {
            successCount++;
        }
    }

    emit statusMessage(QString("Restored %1 of %2 cropped slides")
                       .arg(successCount).arg(targets.size()));

    loadCropEntries();
    loadFolderItems();
}

void ReviewSlidesDialog::onRemoveDuplicates()
{
    if (m_currentFolderName.isEmpty()) {
        emit statusMessage("No folder open");
        return;
    }

    // Pre-scan to short-circuit on empty folders and to report "X of N" counts.
    QDir folder(QDir(m_baseOutputDir).filePath(m_currentFolderName));
    QStringList names = folder.entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
        QDir::Files);
    if (names.isEmpty()) {
        emit statusMessage("No extracted slides in this folder");
        return;
    }

    // Hamming threshold is read fresh on each click so a Settings change while
    // the dialog is open takes effect immediately.
    ConfigManager cfg;
    const AppConfig appCfg = cfg.loadConfig();

    // Reuse processDirectory with only the duplicate phase enabled. The ML
    // threshold defaults are unused because enableMLClassification is false.
    PostProcessor processor;
    PostProcessingResult result = processor.processDirectory(
        folder.absolutePath(),
        /*deleteRedundant=*/ true,
        /*compareExcluded=*/ false,
        appCfg.hammingThreshold,
        /*exclusionList=*/ {},
        /*enableMLClassification=*/ false,
        /*mlModelPath=*/ QString(),
        /*mlNotSlideHigh=*/ 0.9f,
        /*mlNotSlideLow=*/ 0.75f,
        /*mlMaybeSlideHigh=*/ 0.9f,
        /*mlMaybeSlideLow=*/ 0.75f,
        /*mlSlideMax=*/ 0.25f,
        /*mlDeleteMaybeSlides=*/ true,
        /*mlExecutionProvider=*/ "Auto",
        /*useApplicationTrash=*/ true,
        m_baseOutputDir);

    emit statusMessage(QString("Remove Duplicate: %1 of %2 slides moved to trash (Hamming ≤ %3)")
                       .arg(result.removedByPHash).arg(names.size()).arg(appCfg.hammingThreshold));

    // Reload trash metadata + current-folder removed list, then refresh the grid
    // so newly-trashed items appear and the extracted thumbnails disappear.
    QString trashDir = TrashManager::getTrashDirectory(m_baseOutputDir);
    m_allEntries = TrashMetadata::getEntries(trashDir);
    loadCropEntries();
    m_currentFolderRemoved.clear();
    for (const TrashEntry& entry : m_allEntries) {
        QString entryFolder = entry.originalFolder;
        if (entryFolder.isEmpty()) {
            entryFolder = QString("slides_%1").arg(entry.videoName);
        }
        if (entryFolder == m_currentFolderName) {
            m_currentFolderRemoved.append(entry);
        }
    }
    loadFolderItems();
}

void ReviewSlidesDialog::updateBaselineButtonState()
{
    if (m_baselineActive) {
        bool hasTargets = false;
        for (ReviewItemWidget* w : m_itemWidgets) {
            if (w->isChecked() && w->state() == ReviewItemWidget::Extracted && !w->isCropped()) {
                hasTargets = true;
                break;
            }
        }
        m_applyBaselineButton->setEnabled(hasTargets);
    }
}

void ReviewSlidesDialog::resetBaselineState()
{
    m_baselineActive = false;
    m_baselineRectImagePx = QRect();
    m_baselineSourceSize = QSize();
    m_baselineSourceLabel.clear();
    m_applyBaselineButton->setVisible(false);
    m_applyBaselineButton->setEnabled(true);
    updateBaselineButtonState();
}

void ReviewSlidesDialog::refreshWindowTitleForCurrentPage()
{
    if (m_stack->currentIndex() == 0) {
        setWindowTitle("Slides Review");
    } else if (m_stack->currentIndex() == 1) {
        setWindowTitle(QString("Slides Review — %1").arg(stripSlidesPrefix(m_currentFolderName)));
    }
    // Viewer page sets its own title with filename in enterViewerPage.
}

// ==================== Natural sorting (duplicated from PdfMakerDialog) ====================

static QList<QVariant> tokenizeForReviewSort(const QString& str)
{
    QList<QVariant> tokens;
    QString currentText;
    int i = 0;

    static QMap<QChar, int> weekdayMap = {
        {QChar(0x4E00), 1},
        {QChar(0x4E8C), 2},
        {QChar(0x4E09), 3},
        {QChar(0x56DB), 4},
        {QChar(0x4E94), 5},
        {QChar(0x516D), 6},
        {QChar(0x65E5), 7},
    };

    static QMap<QString, int> englishWeekdayMap = {
        {"monday", 1}, {"mon", 1},
        {"tuesday", 2}, {"tue", 2}, {"tues", 2},
        {"wednesday", 3}, {"wed", 3},
        {"thursday", 4}, {"thu", 4}, {"thur", 4}, {"thurs", 4},
        {"friday", 5}, {"fri", 5},
        {"saturday", 6}, {"sat", 6},
        {"sunday", 7}, {"sun", 7},
    };

    static QMap<QString, int> monthMap = {
        {"january", 1}, {"jan", 1},
        {"february", 2}, {"feb", 2},
        {"march", 3}, {"mar", 3},
        {"april", 4}, {"apr", 4},
        {"may", 5},
        {"june", 6}, {"jun", 6},
        {"july", 7}, {"jul", 7},
        {"august", 8}, {"aug", 8},
        {"september", 9}, {"sep", 9}, {"sept", 9},
        {"october", 10}, {"oct", 10},
        {"november", 11}, {"nov", 11},
        {"december", 12}, {"dec", 12},
    };

    while (i < str.length()) {
        if (i + 2 < str.length() &&
            str.mid(i, 2) == QString::fromUtf8("星期")) {
            if (!currentText.isEmpty()) {
                tokens.append(currentText);
                currentText.clear();
            }
            tokens.append(QString::fromUtf8("星期"));

            QChar weekdayChar = str.at(i + 2);
            if (weekdayMap.contains(weekdayChar)) {
                tokens.append(weekdayMap[weekdayChar]);
                i += 3;
                continue;
            }
        }

        if (str.at(i).isDigit()) {
            if (!currentText.isEmpty()) {
                tokens.append(currentText);
                currentText.clear();
            }
            QString numStr;
            while (i < str.length() && str.at(i).isDigit()) {
                numStr += str.at(i);
                ++i;
            }
            tokens.append(numStr.toInt());
            continue;
        }

        if (str.at(i).isLetter()) {
            QString word;
            while (i < str.length() && str.at(i).isLetter()) {
                word += str.at(i);
                ++i;
            }
            QString lowerWord = word.toLower();
            if (englishWeekdayMap.contains(lowerWord)) {
                if (!currentText.isEmpty()) {
                    tokens.append(currentText);
                    currentText.clear();
                }
                tokens.append(QString("__weekday__"));
                tokens.append(englishWeekdayMap[lowerWord]);
                continue;
            }
            if (monthMap.contains(lowerWord)) {
                if (!currentText.isEmpty()) {
                    tokens.append(currentText);
                    currentText.clear();
                }
                tokens.append(QString("__month__"));
                tokens.append(monthMap[lowerWord]);
                continue;
            }
            currentText += word;
            continue;
        }

        currentText += str.at(i);
        ++i;
    }

    if (!currentText.isEmpty()) {
        tokens.append(currentText);
    }

    return tokens;
}

bool ReviewSlidesDialog::naturalLessThan(const QString& a, const QString& b)
{
    QList<QVariant> tokensA = tokenizeForReviewSort(a);
    QList<QVariant> tokensB = tokenizeForReviewSort(b);

    int len = qMin(tokensA.size(), tokensB.size());
    for (int i = 0; i < len; ++i) {
        const QVariant& va = tokensA[i];
        const QVariant& vb = tokensB[i];

        if (va.typeId() == QMetaType::Int && vb.typeId() == QMetaType::Int) {
            if (va.toInt() != vb.toInt()) {
                return va.toInt() < vb.toInt();
            }
            continue;
        }

        if (va.typeId() == QMetaType::QString && vb.typeId() == QMetaType::QString) {
            QString sa = va.toString();
            QString sb = vb.toString();
            int cmp = QString::localeAwareCompare(sa, sb);
            if (cmp != 0) {
                return cmp < 0;
            }
            continue;
        }

        if (va.typeId() == QMetaType::Int) {
            return true;
        }
        if (vb.typeId() == QMetaType::Int) {
            return false;
        }
    }

    return tokensA.size() < tokensB.size();
}

void ReviewSlidesDialog::naturalSort(QStringList& list)
{
    std::sort(list.begin(), list.end(), naturalLessThan);
}
