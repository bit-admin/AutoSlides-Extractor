#include "pdfmakerdialog.h"
#include "trashmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QDebug>
#include <QRegularExpression>
#include <QMap>
#include <QVariant>
#include <QCheckBox>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QPdfWriter>
#include <QPainter>
#include <QBuffer>
#include <QPageSize>
#include <QDesktopServices>
#include <QUrl>

PdfMakerDialog::PdfMakerDialog(const QString& baseOutputDir,
                               QWidget *parent)
    : QDialog(parent),
      m_baseOutputDir(baseOutputDir),
      m_customOrder(false),
      m_dragStartRow(-1)
{
    setupUI();
    connectSignals();
    loadFolders();
    updateNavigationTitle();
}

void PdfMakerDialog::setupUI()
{
    setWindowTitle("PDF Maker");
    resize(1000, 800);  // Same as TrashReviewDialog

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Header row: [< Back] [Order toggle] [Refresh] --- [Reduce file size config] [Make PDF]
    QHBoxLayout* headerLayout = new QHBoxLayout();

    m_backButton = new QPushButton("< Back", this);
    m_backButton->setVisible(false);  // Hidden on folders level
    headerLayout->addWidget(m_backButton);

    m_titleLabel = new QLabel("", this);  // Empty, used for images level title
    m_titleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; }");
    m_titleLabel->setVisible(false);
    headerLayout->addWidget(m_titleLabel);

    m_orderToggleButton = new QPushButton("Order: A-Z", this);
    m_orderToggleButton->setToolTip("Toggle between A-Z sorting and custom order");
    headerLayout->addWidget(m_orderToggleButton);

    m_refreshButton = new QPushButton("Refresh", this);
    headerLayout->addWidget(m_refreshButton);

    m_moveUpButton = new QPushButton("Move Up", this);
    m_moveUpButton->setEnabled(false);
    m_moveUpButton->setVisible(false);  // Hidden in A-Z mode
    headerLayout->addWidget(m_moveUpButton);

    m_moveDownButton = new QPushButton("Move Down", this);
    m_moveDownButton->setEnabled(false);
    m_moveDownButton->setVisible(false);  // Hidden in A-Z mode
    headerLayout->addWidget(m_moveDownButton);

    headerLayout->addStretch();

    // PDF generation controls in header
    m_reduceFileSizeCheckbox = new QCheckBox("Reduce File Size", this);
    m_reduceFileSizeCheckbox->setChecked(true);
    headerLayout->addWidget(m_reduceFileSizeCheckbox);

    m_resizeLabel = new QLabel("Resize:", this);
    headerLayout->addWidget(m_resizeLabel);

    m_resizeComboBox = new QComboBox(this);
    m_resizeComboBox->addItem("Original", 0);
    m_resizeComboBox->addItem("1080p (1920×1080)", 1080);
    m_resizeComboBox->addItem("720p (1280×720)", 720);
    m_resizeComboBox->addItem("480p (854×480)", 480);
    m_resizeComboBox->addItem("360p (640×360)", 360);
    m_resizeComboBox->setCurrentIndex(2);  // Default to 720p
    headerLayout->addWidget(m_resizeComboBox);

    m_qualityLabel = new QLabel("JPEG Quality:", this);
    headerLayout->addWidget(m_qualityLabel);

    m_jpegQualitySpinBox = new QSpinBox(this);
    m_jpegQualitySpinBox->setRange(1, 100);
    m_jpegQualitySpinBox->setValue(50);
    m_jpegQualitySpinBox->setSuffix("%");
    headerLayout->addWidget(m_jpegQualitySpinBox);

    // Connect checkbox to enable/disable resize and quality controls
    connect(m_reduceFileSizeCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_resizeLabel->setEnabled(checked);
        m_resizeComboBox->setEnabled(checked);
        m_qualityLabel->setEnabled(checked);
        m_jpegQualitySpinBox->setEnabled(checked);
    });

    mainLayout->addLayout(headerLayout);

    // Stacked widget for folder/image levels
    m_stackedWidget = new QStackedWidget(this);

    setupFoldersPage();
    setupImagesPage();

    m_stackedWidget->addWidget(m_foldersPage);   // Index 0
    m_stackedWidget->addWidget(m_imagesPage);    // Index 1

    mainLayout->addWidget(m_stackedWidget);
}

void PdfMakerDialog::setupFoldersPage()
{
    m_foldersPage = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_foldersPage);

    // Folder table
    m_folderTable = new QTableWidget(m_foldersPage);
    m_folderTable->setColumnCount(5);
    m_folderTable->setHorizontalHeaderLabels({"", "Folder Name", "Count", "Images Grid", "Drag"});
    m_folderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_folderTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folderTable->setAlternatingRowColors(true);
    m_folderTable->verticalHeader()->setVisible(false);
    m_folderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Column widths
    QHeaderView* header = m_folderTable->horizontalHeader();
    header->setSectionResizeMode(COL_SELECT, QHeaderView::Fixed);
    header->setSectionResizeMode(COL_FOLDER_NAME, QHeaderView::Stretch);
    header->setSectionResizeMode(COL_IMAGE_COUNT, QHeaderView::Fixed);
    header->setSectionResizeMode(COL_REVIEW, QHeaderView::Fixed);
    header->setSectionResizeMode(COL_HANDLE, QHeaderView::Fixed);
    m_folderTable->setColumnWidth(COL_SELECT, 40);
    m_folderTable->setColumnWidth(COL_IMAGE_COUNT, 80);
    m_folderTable->setColumnWidth(COL_REVIEW, 80);
    m_folderTable->setColumnWidth(COL_HANDLE, 50);

    // Install event filter for mouse-based row reordering (via handle column)
    m_folderTable->viewport()->installEventFilter(this);
    m_folderTable->viewport()->setMouseTracking(true);

    layout->addWidget(m_folderTable);

    // Progress bar row: [Progress bar] [Open PDF]
    QHBoxLayout* progressLayout = new QHBoxLayout();

    m_progressBar = new QProgressBar(m_foldersPage);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(16);  // Thinner progress bar
    m_progressBar->setValue(0);
    progressLayout->addWidget(m_progressBar);

    m_openPdfButton = new QPushButton("Open PDF", m_foldersPage);
    m_openPdfButton->setEnabled(false);  // Disabled until a PDF is made
    progressLayout->addWidget(m_openPdfButton);

    layout->addLayout(progressLayout);

    // Button section
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_folderSelectionLabel = new QLabel("Selected: 0 folders", m_foldersPage);
    buttonLayout->addWidget(m_folderSelectionLabel);

    m_selectAllFoldersButton = new QPushButton("Select All", m_foldersPage);
    buttonLayout->addWidget(m_selectAllFoldersButton);

    m_deselectAllFoldersButton = new QPushButton("Deselect All", m_foldersPage);
    buttonLayout->addWidget(m_deselectAllFoldersButton);

    buttonLayout->addStretch();

    m_closeFoldersButton = new QPushButton("Close", m_foldersPage);
    buttonLayout->addWidget(m_closeFoldersButton);

    m_makePdfButton = new QPushButton("Make PDF", m_foldersPage);
    buttonLayout->addWidget(m_makePdfButton);

    layout->addLayout(buttonLayout);
}

void PdfMakerDialog::setupImagesPage()
{
    m_imagesPage = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_imagesPage);

    // Scroll area with grid layout for thumbnails
    m_scrollArea = new QScrollArea(m_imagesPage);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridContainer = new QWidget();
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setSpacing(15);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_scrollArea->setWidget(m_gridContainer);
    layout->addWidget(m_scrollArea);

    // Button section (single row with selection label)
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // Selection status label on the left
    m_imageSelectionLabel = new QLabel("Selected: 0 images", m_imagesPage);
    buttonLayout->addWidget(m_imageSelectionLabel);

    m_selectAllImagesButton = new QPushButton("Select All", m_imagesPage);
    buttonLayout->addWidget(m_selectAllImagesButton);

    m_deselectAllImagesButton = new QPushButton("Deselect All", m_imagesPage);
    buttonLayout->addWidget(m_deselectAllImagesButton);

    buttonLayout->addStretch();

    m_closeImagesButton = new QPushButton("Close", m_imagesPage);
    buttonLayout->addWidget(m_closeImagesButton);

    m_deleteButton = new QPushButton("Delete Selected", m_imagesPage);
    m_deleteButton->setEnabled(false);
    buttonLayout->addWidget(m_deleteButton);

    layout->addLayout(buttonLayout);
}

void PdfMakerDialog::connectSignals()
{
    // Common
    connect(m_refreshButton, &QPushButton::clicked, this, &PdfMakerDialog::onRefresh);
    connect(m_backButton, &QPushButton::clicked, this, &PdfMakerDialog::onBackToFolders);
    connect(m_orderToggleButton, &QPushButton::clicked, this, &PdfMakerDialog::onOrderToggle);

    // Folder level
    connect(m_folderTable, &QTableWidget::cellChanged, this, &PdfMakerDialog::onFolderCheckChanged);
    connect(m_selectAllFoldersButton, &QPushButton::clicked, this, &PdfMakerDialog::onSelectAllFolders);
    connect(m_deselectAllFoldersButton, &QPushButton::clicked, this, &PdfMakerDialog::onDeselectAllFolders);
    connect(m_moveUpButton, &QPushButton::clicked, this, &PdfMakerDialog::onMoveUp);
    connect(m_moveDownButton, &QPushButton::clicked, this, &PdfMakerDialog::onMoveDown);
    connect(m_closeFoldersButton, &QPushButton::clicked, this, &QDialog::accept);

    // Action bar connections
    connect(m_makePdfButton, &QPushButton::clicked, this, &PdfMakerDialog::onMakePdf);
    connect(m_openPdfButton, &QPushButton::clicked, this, &PdfMakerDialog::onOpenPdf);

    // Update move buttons when selection changes
    connect(m_folderTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_customOrder) {
            int currentRow = m_folderTable->currentRow();
            m_moveUpButton->setEnabled(currentRow > 0);
            m_moveDownButton->setEnabled(currentRow >= 0 && currentRow < m_folderTable->rowCount() - 1);
        }
    });

    // Image level
    connect(m_deleteButton, &QPushButton::clicked, this, &PdfMakerDialog::onDeleteSelected);
    connect(m_selectAllImagesButton, &QPushButton::clicked, this, &PdfMakerDialog::onSelectAllImages);
    connect(m_deselectAllImagesButton, &QPushButton::clicked, this, &PdfMakerDialog::onDeselectAllImages);
    connect(m_closeImagesButton, &QPushButton::clicked, this, &QDialog::accept);
}

void PdfMakerDialog::loadFolders()
{
    QDir outputDir(m_baseOutputDir);
    m_folderNames = outputDir.entryList(
        QStringList() << "slides_*",
        QDir::Dirs | QDir::NoDotAndDotDot
    );

    // Apply natural sorting (always for initial load, and when in A-Z mode)
    if (!m_customOrder) {
        naturalSort(m_folderNames);
    }

    populateFolderTable();
}

void PdfMakerDialog::populateFolderTable()
{
    // Block signals to avoid triggering cellChanged during population
    m_folderTable->blockSignals(true);
    m_folderTable->setRowCount(0);

    QDir outputDir(m_baseOutputDir);

    for (int i = 0; i < m_folderNames.size(); ++i) {
        const QString& folderName = m_folderNames[i];
        QString folderPath = outputDir.filePath(folderName);
        int imageCount = countImagesInFolder(folderPath);

        int row = m_folderTable->rowCount();
        m_folderTable->insertRow(row);

        // Column 0: Checkbox
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(Qt::UserRole, folderPath);  // Store full path
        m_folderTable->setItem(row, COL_SELECT, checkItem);

        // Column 1: Folder name (without "slides_" prefix)
        QString displayName = stripSlidesPrefix(folderName);
        QTableWidgetItem* nameItem = new QTableWidgetItem(displayName);
        nameItem->setToolTip(folderPath);
        m_folderTable->setItem(row, COL_FOLDER_NAME, nameItem);

        // Column 2: Image count
        QTableWidgetItem* countItem = new QTableWidgetItem(QString::number(imageCount));
        countItem->setTextAlignment(Qt::AlignCenter);
        m_folderTable->setItem(row, COL_IMAGE_COUNT, countItem);

        // Column 3: Review button
        QPushButton* reviewButton = new QPushButton("Review", m_folderTable);
        reviewButton->setProperty("row", row);
        connect(reviewButton, &QPushButton::clicked, this, [this, row]() {
            onReviewFolder(row);
        });
        m_folderTable->setCellWidget(row, COL_REVIEW, reviewButton);

        // Column 4: Handle (grip icon for drag) - rightmost column
        QTableWidgetItem* handleItem = new QTableWidgetItem("::::");
        handleItem->setTextAlignment(Qt::AlignCenter);
        handleItem->setFlags(Qt::ItemIsEnabled);  // Not selectable, just visual
        handleItem->setToolTip("Select row and use Move Up/Down buttons to reorder");
        m_folderTable->setItem(row, COL_HANDLE, handleItem);
    }

    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();
}

QString PdfMakerDialog::stripSlidesPrefix(const QString& folderName)
{
    if (folderName.startsWith("slides_")) {
        return folderName.mid(7);  // Remove "slides_" (7 characters)
    }
    return folderName;
}

void PdfMakerDialog::loadImages(const QString& folderPath)
{
    clearImageGrid();

    QDir folder(folderPath);
    QStringList images = folder.entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
        QDir::Files
    );

    // Sort images A-Z (natural sort for consistency)
    naturalSort(images);

    // Populate grid with 3 items per row
    int row = 0;
    int col = 0;
    const int COLUMNS = 3;

    for (const QString& imageName : images) {
        QString imagePath = folder.filePath(imageName);
        SlideItemWidget* itemWidget = new SlideItemWidget(imagePath, m_gridContainer);

        // Connect selection changed signal
        connect(itemWidget, &SlideItemWidget::selectionChanged,
                this, &PdfMakerDialog::onImageSelectionChanged);

        m_gridLayout->addWidget(itemWidget, row, col);
        m_imageWidgets.append(itemWidget);

        col++;
        if (col >= COLUMNS) {
            col = 0;
            row++;
        }
    }

    updateImageSelectionLabel();
}

int PdfMakerDialog::countImagesInFolder(const QString& folderPath)
{
    QDir folder(folderPath);
    return folder.entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
        QDir::Files
    ).count();
}

void PdfMakerDialog::clearImageGrid()
{
    for (SlideItemWidget* widget : m_imageWidgets) {
        m_gridLayout->removeWidget(widget);
        delete widget;
    }
    m_imageWidgets.clear();
}

QList<SlideItemWidget*> PdfMakerDialog::getCheckedImageItems() const
{
    QList<SlideItemWidget*> checkedItems;
    for (SlideItemWidget* widget : m_imageWidgets) {
        if (widget->isChecked()) {
            checkedItems.append(widget);
        }
    }
    return checkedItems;
}

QStringList PdfMakerDialog::getCheckedFolderPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_folderTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, COL_SELECT);
        if (item && item->checkState() == Qt::Checked) {
            paths.append(item->data(Qt::UserRole).toString());
        }
    }
    return paths;
}

QList<int> PdfMakerDialog::getCheckedFolderRows() const
{
    QList<int> rows;
    for (int i = 0; i < m_folderTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, COL_SELECT);
        if (item && item->checkState() == Qt::Checked) {
            rows.append(i);
        }
    }
    return rows;
}

void PdfMakerDialog::updateFolderSelectionLabel()
{
    QStringList selected = getCheckedFolderPaths();
    m_folderSelectionLabel->setText(QString("Selected: %1 folders").arg(selected.count()));
}

void PdfMakerDialog::updateImageSelectionLabel()
{
    int selectedCount = getCheckedImageItems().count();
    m_imageSelectionLabel->setText(QString("Selected: %1 images").arg(selectedCount));
    m_deleteButton->setEnabled(selectedCount > 0);
}

void PdfMakerDialog::updateNavigationTitle()
{
    if (m_currentFolderPath.isEmpty()) {
        // Folders level - show PDF controls, hide back/title
        m_titleLabel->setVisible(false);
        m_backButton->setVisible(false);
        m_orderToggleButton->setVisible(true);
        m_refreshButton->setVisible(true);
        m_reduceFileSizeCheckbox->setVisible(true);
        m_resizeLabel->setVisible(true);
        m_resizeComboBox->setVisible(true);
        m_qualityLabel->setVisible(true);
        m_jpegQualitySpinBox->setVisible(true);
    } else {
        // Images level - show back/title, hide PDF controls
        QFileInfo folderInfo(m_currentFolderPath);
        QString displayName = stripSlidesPrefix(folderInfo.fileName());
        m_titleLabel->setText(displayName);
        m_titleLabel->setVisible(true);
        m_backButton->setVisible(true);
        m_orderToggleButton->setVisible(false);
        m_refreshButton->setVisible(true);
        m_reduceFileSizeCheckbox->setVisible(false);
        m_resizeLabel->setVisible(false);
        m_resizeComboBox->setVisible(false);
        m_qualityLabel->setVisible(false);
        m_jpegQualitySpinBox->setVisible(false);
    }
}

void PdfMakerDialog::updateOrderButton()
{
    if (m_customOrder) {
        m_orderToggleButton->setText("Order: Custom (Drag to Reorder)");
        m_moveUpButton->setVisible(true);
        m_moveDownButton->setVisible(true);
    } else {
        m_orderToggleButton->setText("Order: A-Z");
        m_moveUpButton->setVisible(false);
        m_moveDownButton->setVisible(false);
    }
    // Handle column is always visible
}

void PdfMakerDialog::swapRows(int row1, int row2)
{
    if (row1 < 0 || row2 < 0 || row1 >= m_folderTable->rowCount() || row2 >= m_folderTable->rowCount()) {
        return;
    }

    // Swap in the data model
    m_folderNames.swapItemsAt(row1, row2);

    // Repopulate table (simpler than swapping widgets)
    populateFolderTable();

    // Restore selection to the moved row
    m_folderTable->selectRow(row2);
}

// ==================== Natural Sorting ====================

/**
 * @brief Tokenize a string into alternating text and number segments
 *
 * Also handles special Chinese patterns:
 * - 星期[一二三四五六日] -> weekday numbers 1-7
 */
static QList<QVariant> tokenizeForSort(const QString& str)
{
    QList<QVariant> tokens;
    QString currentText;
    int i = 0;

    // Chinese weekday mapping
    static QMap<QChar, int> weekdayMap = {
        {QChar(0x4E00), 1},  // 一 Monday
        {QChar(0x4E8C), 2},  // 二 Tuesday
        {QChar(0x4E09), 3},  // 三 Wednesday
        {QChar(0x56DB), 4},  // 四 Thursday
        {QChar(0x4E94), 5},  // 五 Friday
        {QChar(0x516D), 6},  // 六 Saturday
        {QChar(0x65E5), 7},  // 日 Sunday
    };

    // English weekday mapping (case insensitive)
    static QMap<QString, int> englishWeekdayMap = {
        {"monday", 1}, {"mon", 1},
        {"tuesday", 2}, {"tue", 2}, {"tues", 2},
        {"wednesday", 3}, {"wed", 3},
        {"thursday", 4}, {"thu", 4}, {"thur", 4}, {"thurs", 4},
        {"friday", 5}, {"fri", 5},
        {"saturday", 6}, {"sat", 6},
        {"sunday", 7}, {"sun", 7},
    };

    // English month mapping
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
        // Check for Chinese weekday pattern: 星期X
        if (i + 2 < str.length() &&
            str.mid(i, 2) == QString::fromUtf8("星期")) {
            // Flush current text
            if (!currentText.isEmpty()) {
                tokens.append(currentText);
                currentText.clear();
            }
            // Add the 星期 prefix
            tokens.append(QString::fromUtf8("星期"));

            QChar weekdayChar = str.at(i + 2);
            if (weekdayMap.contains(weekdayChar)) {
                tokens.append(weekdayMap[weekdayChar]);
                i += 3;
                continue;
            }
        }

        // Check for number
        if (str.at(i).isDigit()) {
            // Flush current text
            if (!currentText.isEmpty()) {
                tokens.append(currentText);
                currentText.clear();
            }

            // Extract full number
            QString numStr;
            while (i < str.length() && str.at(i).isDigit()) {
                numStr += str.at(i);
                ++i;
            }
            tokens.append(numStr.toInt());
            continue;
        }

        // Check for English weekday/month at word boundary
        if (str.at(i).isLetter()) {
            // Extract word
            QString word;
            while (i < str.length() && str.at(i).isLetter()) {
                word += str.at(i);
                ++i;
            }

            QString lowerWord = word.toLower();

            // Check English weekdays
            if (englishWeekdayMap.contains(lowerWord)) {
                if (!currentText.isEmpty()) {
                    tokens.append(currentText);
                    currentText.clear();
                }
                tokens.append(QString("__weekday__"));
                tokens.append(englishWeekdayMap[lowerWord]);
                continue;
            }

            // Check English months
            if (monthMap.contains(lowerWord)) {
                if (!currentText.isEmpty()) {
                    tokens.append(currentText);
                    currentText.clear();
                }
                tokens.append(QString("__month__"));
                tokens.append(monthMap[lowerWord]);
                continue;
            }

            // Regular word - add to current text
            currentText += word;
            continue;
        }

        // Other character
        currentText += str.at(i);
        ++i;
    }

    // Flush remaining text
    if (!currentText.isEmpty()) {
        tokens.append(currentText);
    }

    return tokens;
}

bool PdfMakerDialog::naturalLessThan(const QString& a, const QString& b)
{
    QList<QVariant> tokensA = tokenizeForSort(a);
    QList<QVariant> tokensB = tokenizeForSort(b);

    int len = qMin(tokensA.size(), tokensB.size());
    for (int i = 0; i < len; ++i) {
        const QVariant& va = tokensA[i];
        const QVariant& vb = tokensB[i];

        // Both are integers - compare numerically
        if (va.typeId() == QMetaType::Int && vb.typeId() == QMetaType::Int) {
            if (va.toInt() != vb.toInt()) {
                return va.toInt() < vb.toInt();
            }
            continue;
        }

        // Both are strings - compare as strings
        if (va.typeId() == QMetaType::QString && vb.typeId() == QMetaType::QString) {
            QString sa = va.toString();
            QString sb = vb.toString();
            int cmp = QString::localeAwareCompare(sa, sb);
            if (cmp != 0) {
                return cmp < 0;
            }
            continue;
        }

        // Mixed types - numbers come before strings
        if (va.typeId() == QMetaType::Int) {
            return true;  // Number < String
        }
        if (vb.typeId() == QMetaType::Int) {
            return false;  // String > Number
        }
    }

    // All compared elements are equal, shorter list comes first
    return tokensA.size() < tokensB.size();
}

void PdfMakerDialog::naturalSort(QStringList& list)
{
    std::sort(list.begin(), list.end(), naturalLessThan);
}

// ==================== Slots ====================

void PdfMakerDialog::onRefresh()
{
    if (m_currentFolderPath.isEmpty()) {
        loadFolders();
    } else {
        loadImages(m_currentFolderPath);
    }
}

void PdfMakerDialog::onBackToFolders()
{
    m_currentFolderPath.clear();
    m_stackedWidget->setCurrentIndex(0);
    updateNavigationTitle();
    // Don't reload folders to preserve custom order
}

void PdfMakerDialog::onReviewFolder(int row)
{
    QTableWidgetItem* item = m_folderTable->item(row, COL_SELECT);
    if (!item) return;

    QString folderPath = item->data(Qt::UserRole).toString();
    m_currentFolderPath = folderPath;
    m_stackedWidget->setCurrentIndex(1);
    updateNavigationTitle();
    loadImages(folderPath);
}

void PdfMakerDialog::onFolderCheckChanged(int row, int column)
{
    if (column == COL_SELECT) {
        updateFolderSelectionLabel();
    }
}

void PdfMakerDialog::onSelectAllFolders()
{
    m_folderTable->blockSignals(true);
    for (int i = 0; i < m_folderTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, COL_SELECT);
        if (item) {
            item->setCheckState(Qt::Checked);
        }
    }
    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();
}

void PdfMakerDialog::onDeselectAllFolders()
{
    m_folderTable->blockSignals(true);
    for (int i = 0; i < m_folderTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, COL_SELECT);
        if (item) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();
}

void PdfMakerDialog::onOrderToggle()
{
    m_customOrder = !m_customOrder;
    updateOrderButton();

    if (!m_customOrder) {
        // Switching to A-Z mode - re-sort
        naturalSort(m_folderNames);
        populateFolderTable();
    }
    // In custom mode, keep current order
}

void PdfMakerDialog::onMoveUp()
{
    int currentRow = m_folderTable->currentRow();
    if (currentRow > 0) {
        swapRows(currentRow, currentRow - 1);
    }
}

void PdfMakerDialog::onMoveDown()
{
    int currentRow = m_folderTable->currentRow();
    if (currentRow >= 0 && currentRow < m_folderTable->rowCount() - 1) {
        swapRows(currentRow, currentRow + 1);
    }
}

void PdfMakerDialog::onImageSelectionChanged()
{
    updateImageSelectionLabel();
}

void PdfMakerDialog::onSelectAllImages()
{
    for (SlideItemWidget* widget : m_imageWidgets) {
        widget->setChecked(true);
    }
}

void PdfMakerDialog::onDeselectAllImages()
{
    for (SlideItemWidget* widget : m_imageWidgets) {
        widget->setChecked(false);
    }
}

void PdfMakerDialog::onDeleteSelected()
{
    QList<SlideItemWidget*> checkedItems = getCheckedImageItems();

    if (checkedItems.isEmpty()) {
        return;
    }

    int count = checkedItems.count();
    emit statusMessage(QString("Moving %1 image(s) to trash...").arg(count));

    int successCount = 0;
    int failCount = 0;

    for (SlideItemWidget* widget : checkedItems) {
        QString imagePath = widget->getImagePath();

        if (TrashManager::moveToApplicationTrash(
                imagePath,
                m_baseOutputDir,
                "manual",           // method
                "User deleted"      // reason
            )) {
            successCount++;
        } else {
            failCount++;
            qWarning() << "PdfMakerDialog: Failed to move to trash:" << imagePath;
        }
    }

    // Log result
    if (failCount == 0) {
        emit statusMessage(QString("Moved %1 image(s) to trash").arg(successCount));
    } else {
        emit statusMessage(QString("Moved %1 image(s) to trash, %2 failed").arg(successCount).arg(failCount));
    }

    if (successCount > 0) {
        emit filesDeleted(successCount);
    }

    // Refresh current folder view
    loadImages(m_currentFolderPath);
}

bool PdfMakerDialog::eventFilter(QObject* watched, QEvent* event)
{
    // Handle mouse events on the table viewport for row reordering
    if (watched == m_folderTable->viewport()) {

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                // Check if click is on the handle column
                QModelIndex index = m_folderTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == COL_HANDLE) {
                    m_dragStartRow = index.row();
                    m_dragStartPos = mouseEvent->pos();
                    m_folderTable->setCursor(Qt::ClosedHandCursor);
                    return true;  // Consume the event
                }
            }
        }

        if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

            // Update cursor when hovering over handle column
            QModelIndex index = m_folderTable->indexAt(mouseEvent->pos());
            if (index.isValid() && index.column() == COL_HANDLE) {
                if (m_dragStartRow < 0) {
                    m_folderTable->setCursor(Qt::OpenHandCursor);
                }
            } else if (m_dragStartRow < 0) {
                m_folderTable->unsetCursor();
            }

            // Handle dragging
            if (m_dragStartRow >= 0 && (mouseEvent->buttons() & Qt::LeftButton)) {
                // Check if we've moved enough to consider it a drag
                if ((mouseEvent->pos() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                    // Auto-switch to Custom mode when dragging starts
                    if (!m_customOrder) {
                        m_customOrder = true;
                        updateOrderButton();
                    }

                    // Find the row under the current mouse position
                    QModelIndex destIndex = m_folderTable->indexAt(mouseEvent->pos());
                    int destRow = destIndex.isValid() ? destIndex.row() : -1;

                    if (destRow >= 0 && destRow != m_dragStartRow) {
                        // Move the folder in our data model
                        QString movedFolder = m_folderNames.takeAt(m_dragStartRow);
                        m_folderNames.insert(destRow, movedFolder);

                        // Repopulate the table
                        populateFolderTable();

                        // Update drag start row to the new position
                        m_dragStartRow = destRow;

                        // Select the moved row
                        m_folderTable->selectRow(destRow);
                    }
                }
                return true;  // Consume the event
            }
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_dragStartRow >= 0) {
                m_dragStartRow = -1;
                m_folderTable->unsetCursor();

                // Check if still over handle column
                QModelIndex index = m_folderTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == COL_HANDLE) {
                    m_folderTable->setCursor(Qt::OpenHandCursor);
                }
                return true;  // Consume the event
            }
        }
    }

    return QDialog::eventFilter(watched, event);
}

QStringList PdfMakerDialog::getImagesInFolder(const QString& folderPath)
{
    QDir folder(folderPath);
    QStringList images = folder.entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
        QDir::Files
    );
    naturalSort(images);

    // Convert to full paths
    QStringList fullPaths;
    for (const QString& imageName : images) {
        fullPaths.append(folder.filePath(imageName));
    }
    return fullPaths;
}

void PdfMakerDialog::onMakePdf()
{
    // Get selected folders in table order
    QStringList selectedFolders = getCheckedFolderPaths();

    if (selectedFolders.isEmpty()) {
        emit statusMessage("Please select at least one folder");
        return;
    }

    // Get default filename from first selected folder
    QFileInfo firstFolder(selectedFolders.first());
    QString defaultName = stripSlidesPrefix(firstFolder.fileName()) + ".pdf";

    // Show save dialog - default to output directory
    QString savePath = QFileDialog::getSaveFileName(
        this,
        "Save PDF",
        m_baseOutputDir + "/" + defaultName,
        "PDF Files (*.pdf)"
    );

    if (savePath.isEmpty()) {
        return;  // User cancelled
    }

    // Ensure .pdf extension
    if (!savePath.endsWith(".pdf", Qt::CaseInsensitive)) {
        savePath += ".pdf";
    }

    emit statusMessage("Generating PDF...");

    // Count total images for progress
    int totalImages = 0;
    for (const QString& folderPath : selectedFolders) {
        totalImages += countImagesInFolder(folderPath);
    }

    if (totalImages == 0) {
        emit statusMessage("No images found in selected folders");
        return;
    }

    // Setup progress bar
    m_progressBar->setRange(0, totalImages);
    m_progressBar->setValue(0);
    m_makePdfButton->setEnabled(false);  // Disable while generating

    // Get resize setting
    int targetHeight = m_resizeComboBox->currentData().toInt();
    bool reduceSize = m_reduceFileSizeCheckbox->isChecked();
    int jpegQuality = m_jpegQualitySpinBox->value();

    // Get first image to determine initial page size
    QImage firstImage;
    for (const QString& folderPath : selectedFolders) {
        QStringList images = getImagesInFolder(folderPath);
        if (!images.isEmpty()) {
            firstImage = QImage(images.first());
            break;
        }
    }

    if (firstImage.isNull()) {
        emit statusMessage("Failed to load first image");
        m_makePdfButton->setEnabled(true);
        return;
    }

    // Apply resize to first image if needed
    if (targetHeight > 0 && firstImage.height() > targetHeight) {
        firstImage = firstImage.scaledToHeight(targetHeight, Qt::SmoothTransformation);
    }

    // Create PDF with initial page size from first image
    QPdfWriter writer(savePath);
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setPageSize(QPageSize(firstImage.size(), QPageSize::Point));

    QPainter painter(&writer);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    bool isFirstPage = true;
    int processedImages = 0;

    for (const QString& folderPath : selectedFolders) {
        QStringList images = getImagesInFolder(folderPath);

        for (const QString& imagePath : images) {
            QImage image(imagePath);

            if (image.isNull()) {
                qWarning() << "PdfMakerDialog: Failed to load image:" << imagePath;
                continue;
            }

            // Apply resize if needed
            if (targetHeight > 0 && image.height() > targetHeight) {
                image = image.scaledToHeight(targetHeight, Qt::SmoothTransformation);
            }

            // Optionally reduce quality
            if (reduceSize) {
                QBuffer buffer;
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "JPEG", jpegQuality);
                buffer.close();
                buffer.open(QIODevice::ReadOnly);
                image.loadFromData(buffer.data());
            }

            // Set page size to match image
            if (!isFirstPage) {
                writer.newPage();
            }
            writer.setPageSize(QPageSize(image.size(), QPageSize::Point));

            // Draw image to fill the page
            QRect pageRect(0, 0,
                           writer.width(),
                           writer.height());
            painter.drawImage(pageRect, image);

            isFirstPage = false;
            processedImages++;

            // Update progress bar
            m_progressBar->setValue(processedImages);
            QApplication::processEvents();  // Keep UI responsive
        }
    }

    painter.end();

    // Store path and enable Open PDF button
    m_lastPdfPath = savePath;
    m_openPdfButton->setEnabled(true);

    // Re-enable Make PDF button
    m_makePdfButton->setEnabled(true);

    emit statusMessage(QString("PDF saved: %1 (%2 pages)").arg(savePath).arg(processedImages));
}

void PdfMakerDialog::onOpenPdf()
{
    if (!m_lastPdfPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastPdfPath));
    }
}
