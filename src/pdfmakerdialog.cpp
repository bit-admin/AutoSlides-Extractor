#include "pdfmakerdialog.h"
#include "pdfexporter.h"
#include "naturalsorter.h"
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
#include <QMouseEvent>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QFrame>

PdfMakerDialog::PdfMakerDialog(const QString& baseOutputDir,
                               QWidget *parent)
    : QDialog(parent),
      m_baseOutputDir(baseOutputDir),
      m_customOrder(false),
      m_dragStartRow(-1),
      m_lastOutputIsFolder(false)
{
    setupUI();
    connectSignals();
    loadFolders();
}

void PdfMakerDialog::setupUI()
{
    setWindowTitle("Slides Export");
    resize(900, 800);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // First header row: PDF generation controls (left-aligned), output mode pinned far right
    QHBoxLayout* headerLayout = new QHBoxLayout();

    m_reduceFileSizeCheckbox = new QCheckBox("Reduce File Size", this);
    m_reduceFileSizeCheckbox->setChecked(true);
    headerLayout->addWidget(m_reduceFileSizeCheckbox);

    m_aspectRatioLabel = new QLabel("Aspect Ratio:", this);
    headerLayout->addWidget(m_aspectRatioLabel);

    m_aspectRatioComboBox = new QComboBox(this);
    m_aspectRatioComboBox->addItem("16:9", QString("16:9"));
    m_aspectRatioComboBox->addItem("4:3", QString("4:3"));
    m_aspectRatioComboBox->setCurrentIndex(0);
    headerLayout->addWidget(m_aspectRatioComboBox);

    m_resizeLabel = new QLabel("Resize:", this);
    headerLayout->addWidget(m_resizeLabel);

    m_resizeComboBox = new QComboBox(this);
    headerLayout->addWidget(m_resizeComboBox);
    onAspectRatioChanged(m_aspectRatioComboBox->currentIndex());

    m_qualityLabel = new QLabel("JPEG Quality:", this);
    headerLayout->addWidget(m_qualityLabel);

    m_jpegQualitySpinBox = new QSpinBox(this);
    m_jpegQualitySpinBox->setRange(1, 100);
    m_jpegQualitySpinBox->setValue(50);
    m_jpegQualitySpinBox->setSuffix("%");
    headerLayout->addWidget(m_jpegQualitySpinBox);

    headerLayout->addStretch();

    QLabel* outputModeLabel = new QLabel("Output:", this);
    headerLayout->addWidget(outputModeLabel);

    m_outputModeComboBox = new QComboBox(this);
    m_outputModeComboBox->addItem("One Combined PDF", QString("combined"));
    m_outputModeComboBox->addItem("One PDF per Folder", QString("batch"));
    m_outputModeComboBox->setCurrentIndex(0);
    headerLayout->addWidget(m_outputModeComboBox);

    connect(m_reduceFileSizeCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_aspectRatioLabel->setEnabled(checked);
        m_aspectRatioComboBox->setEnabled(checked);
        m_resizeLabel->setEnabled(checked);
        m_resizeComboBox->setEnabled(checked);
        m_qualityLabel->setEnabled(checked);
        m_jpegQualitySpinBox->setEnabled(checked);
    });

    mainLayout->addLayout(headerLayout);

    // Separator between the two header lines
    QFrame* headerSeparator = new QFrame(this);
    headerSeparator->setFrameShape(QFrame::HLine);
    headerSeparator->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(headerSeparator);

    // Second header row: ordering controls — equal width across the whole line
    QHBoxLayout* orderLayout = new QHBoxLayout();

    m_orderToggleButton = new QPushButton("Order: A-Z", this);
    m_orderToggleButton->setToolTip("Toggle between A-Z sorting and custom order");
    orderLayout->addWidget(m_orderToggleButton, 1);

    m_moveUpButton = new QPushButton("Move Up", this);
    m_moveUpButton->setEnabled(false);
    orderLayout->addWidget(m_moveUpButton, 1);

    m_moveDownButton = new QPushButton("Move Down", this);
    m_moveDownButton->setEnabled(false);
    orderLayout->addWidget(m_moveDownButton, 1);

    mainLayout->addLayout(orderLayout);

    // Folder table
    m_folderTable = new QTableWidget(this);
    m_folderTable->setColumnCount(4);
    m_folderTable->setHorizontalHeaderLabels({"", "Folder Name", "Count", "Drag"});
    m_folderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_folderTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_folderTable->setAlternatingRowColors(true);
    m_folderTable->verticalHeader()->setVisible(false);
    m_folderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QHeaderView* header = m_folderTable->horizontalHeader();
    header->setSectionResizeMode(COL_SELECT, QHeaderView::Fixed);
    header->setSectionResizeMode(COL_FOLDER_NAME, QHeaderView::Stretch);
    header->setSectionResizeMode(COL_IMAGE_COUNT, QHeaderView::Fixed);
    header->setSectionResizeMode(COL_HANDLE, QHeaderView::Fixed);
    m_folderTable->setColumnWidth(COL_SELECT, 40);
    m_folderTable->setColumnWidth(COL_IMAGE_COUNT, 80);
    m_folderTable->setColumnWidth(COL_HANDLE, 50);

    m_folderTable->viewport()->installEventFilter(this);
    m_folderTable->viewport()->setMouseTracking(true);

    mainLayout->addWidget(m_folderTable);

    // Progress bar row: [Progress bar] [Open PDF]
    QHBoxLayout* progressLayout = new QHBoxLayout();

    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(16);
    m_progressBar->setValue(0);
    progressLayout->addWidget(m_progressBar);

    m_openPdfButton = new QPushButton("Open PDF", this);
    m_openPdfButton->setEnabled(false);
    progressLayout->addWidget(m_openPdfButton);

    mainLayout->addLayout(progressLayout);

    // Button section
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_folderSelectionLabel = new QLabel("Selected: 0 folders", this);
    buttonLayout->addWidget(m_folderSelectionLabel);

    m_toggleSelectFoldersButton = new QPushButton("Select All", this);
    buttonLayout->addWidget(m_toggleSelectFoldersButton);

    buttonLayout->addStretch();

    m_refreshButton = new QPushButton("Refresh", this);
    buttonLayout->addWidget(m_refreshButton);

    m_closeFoldersButton = new QPushButton("Close", this);
    buttonLayout->addWidget(m_closeFoldersButton);

    m_makePdfButton = new QPushButton("Make PDF", this);
    buttonLayout->addWidget(m_makePdfButton);

    mainLayout->addLayout(buttonLayout);
}

void PdfMakerDialog::connectSignals()
{
    connect(m_refreshButton, &QPushButton::clicked, this, &PdfMakerDialog::onRefresh);
    connect(m_orderToggleButton, &QPushButton::clicked, this, &PdfMakerDialog::onOrderToggle);

    connect(m_folderTable, &QTableWidget::cellChanged, this, &PdfMakerDialog::onFolderCheckChanged);
    connect(m_toggleSelectFoldersButton, &QPushButton::clicked, this, &PdfMakerDialog::onToggleSelectFolders);
    connect(m_moveUpButton, &QPushButton::clicked, this, &PdfMakerDialog::onMoveUp);
    connect(m_moveDownButton, &QPushButton::clicked, this, &PdfMakerDialog::onMoveDown);
    connect(m_closeFoldersButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_makePdfButton, &QPushButton::clicked, this, &PdfMakerDialog::onMakePdf);
    connect(m_openPdfButton, &QPushButton::clicked, this, &PdfMakerDialog::onOpenPdf);
    connect(m_aspectRatioComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PdfMakerDialog::onAspectRatioChanged);

    connect(m_folderTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateOrderButton();
    });
}

void PdfMakerDialog::loadFolders()
{
    QDir outputDir(m_baseOutputDir);
    m_folderNames = outputDir.entryList(
        QStringList() << "slides_*",
        QDir::Dirs | QDir::NoDotAndDotDot
    );

    if (!m_customOrder) {
        NaturalSorter::sort(m_folderNames);
    }

    populateFolderTable();
}

void PdfMakerDialog::populateFolderTable()
{
    m_folderTable->blockSignals(true);
    m_folderTable->setRowCount(0);

    QDir outputDir(m_baseOutputDir);

    for (int i = 0; i < m_folderNames.size(); ++i) {
        const QString& folderName = m_folderNames[i];
        QString folderPath = outputDir.filePath(folderName);
        int imageCount = countImagesInFolder(folderPath);

        int row = m_folderTable->rowCount();
        m_folderTable->insertRow(row);

        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(Qt::UserRole, folderPath);
        m_folderTable->setItem(row, COL_SELECT, checkItem);

        QString displayName = stripSlidesPrefix(folderName);
        QTableWidgetItem* nameItem = new QTableWidgetItem(displayName);
        nameItem->setToolTip(folderPath);
        m_folderTable->setItem(row, COL_FOLDER_NAME, nameItem);

        QTableWidgetItem* countItem = new QTableWidgetItem(QString::number(imageCount));
        countItem->setTextAlignment(Qt::AlignCenter);
        m_folderTable->setItem(row, COL_IMAGE_COUNT, countItem);

        QTableWidgetItem* handleItem = new QTableWidgetItem("::::");
        handleItem->setTextAlignment(Qt::AlignCenter);
        handleItem->setFlags(Qt::ItemIsEnabled);
        handleItem->setToolTip("Select row and use Move Up/Down buttons to reorder");
        m_folderTable->setItem(row, COL_HANDLE, handleItem);
    }

    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();
}

QString PdfMakerDialog::stripSlidesPrefix(const QString& folderName)
{
    if (folderName.startsWith("slides_")) {
        return folderName.mid(7);
    }
    return folderName;
}

int PdfMakerDialog::countImagesInFolder(const QString& folderPath)
{
    QDir folder(folderPath);
    return folder.entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png",
        QDir::Files
    ).count();
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
    const int n = selected.count();
    m_folderSelectionLabel->setText(QString("Selected: %1 folders").arg(n));

    const int rowCount = m_folderTable->rowCount();
    const bool allChecked = (rowCount > 0 && n == rowCount);
    m_toggleSelectFoldersButton->setText(allChecked ? "Deselect All" : "Select All");
}

void PdfMakerDialog::updateOrderButton()
{
    m_orderToggleButton->setText(m_customOrder
        ? "Order: Custom (Drag to Reorder)"
        : "Order: A-Z");

    const int currentRow = m_folderTable->currentRow();
    const int rowCount = m_folderTable->rowCount();
    m_moveUpButton->setEnabled(m_customOrder && currentRow > 0);
    m_moveDownButton->setEnabled(m_customOrder
        && currentRow >= 0 && currentRow < rowCount - 1);
}

void PdfMakerDialog::swapRows(int row1, int row2)
{
    if (row1 < 0 || row2 < 0 || row1 >= m_folderTable->rowCount() || row2 >= m_folderTable->rowCount()) {
        return;
    }

    m_folderNames.swapItemsAt(row1, row2);
    populateFolderTable();
    m_folderTable->selectRow(row2);
}

// ==================== Slots ====================

void PdfMakerDialog::onRefresh()
{
    loadFolders();
}

void PdfMakerDialog::onFolderCheckChanged(int row, int column)
{
    Q_UNUSED(row);
    if (column == COL_SELECT) {
        updateFolderSelectionLabel();
    }
}

void PdfMakerDialog::onToggleSelectFolders()
{
    const int rowCount = m_folderTable->rowCount();
    int checkedCount = 0;
    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, COL_SELECT);
        if (item && item->checkState() == Qt::Checked) ++checkedCount;
    }
    const Qt::CheckState newState = (rowCount > 0 && checkedCount == rowCount)
                                        ? Qt::Unchecked : Qt::Checked;
    m_folderTable->blockSignals(true);
    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = m_folderTable->item(i, COL_SELECT);
        if (item) {
            item->setCheckState(newState);
        }
    }
    m_folderTable->blockSignals(false);
    updateFolderSelectionLabel();
}

void PdfMakerDialog::onAspectRatioChanged(int index)
{
    const QString ratio = m_aspectRatioComboBox->itemData(index).toString();
    const bool fourThree = (ratio == "4:3");

    int previousHeight = m_resizeComboBox->count() > 0
        ? m_resizeComboBox->currentData().toInt()
        : 720;

    QSignalBlocker blocker(m_resizeComboBox);
    m_resizeComboBox->clear();
    m_resizeComboBox->addItem("Original", 0);
    if (fourThree) {
        m_resizeComboBox->addItem("1080p (1440×1080)", 1080);
        m_resizeComboBox->addItem("720p (960×720)", 720);
        m_resizeComboBox->addItem("480p (640×480)", 480);
        m_resizeComboBox->addItem("360p (480×360)", 360);
    } else {
        m_resizeComboBox->addItem("1080p (1920×1080)", 1080);
        m_resizeComboBox->addItem("720p (1280×720)", 720);
        m_resizeComboBox->addItem("480p (854×480)", 480);
        m_resizeComboBox->addItem("360p (640×360)", 360);
    }

    int restoreIndex = m_resizeComboBox->findData(previousHeight);
    m_resizeComboBox->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : 2);
}

void PdfMakerDialog::onOrderToggle()
{
    m_customOrder = !m_customOrder;
    updateOrderButton();

    if (!m_customOrder) {
        NaturalSorter::sort(m_folderNames);
        populateFolderTable();
    }
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

bool PdfMakerDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_folderTable->viewport()) {

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QModelIndex index = m_folderTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == COL_HANDLE) {
                    m_dragStartRow = index.row();
                    m_dragStartPos = mouseEvent->pos();
                    m_folderTable->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
            }
        }

        if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

            QModelIndex index = m_folderTable->indexAt(mouseEvent->pos());
            if (index.isValid() && index.column() == COL_HANDLE) {
                if (m_dragStartRow < 0) {
                    m_folderTable->setCursor(Qt::OpenHandCursor);
                }
            } else if (m_dragStartRow < 0) {
                m_folderTable->unsetCursor();
            }

            if (m_dragStartRow >= 0 && (mouseEvent->buttons() & Qt::LeftButton)) {
                if ((mouseEvent->pos() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                    if (!m_customOrder) {
                        m_customOrder = true;
                        updateOrderButton();
                    }

                    QModelIndex destIndex = m_folderTable->indexAt(mouseEvent->pos());
                    int destRow = destIndex.isValid() ? destIndex.row() : -1;

                    if (destRow >= 0 && destRow != m_dragStartRow) {
                        QString movedFolder = m_folderNames.takeAt(m_dragStartRow);
                        m_folderNames.insert(destRow, movedFolder);

                        populateFolderTable();

                        m_dragStartRow = destRow;
                        m_folderTable->selectRow(destRow);
                    }
                }
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_dragStartRow >= 0) {
                m_dragStartRow = -1;
                m_folderTable->unsetCursor();

                QModelIndex index = m_folderTable->indexAt(mouseEvent->pos());
                if (index.isValid() && index.column() == COL_HANDLE) {
                    m_folderTable->setCursor(Qt::OpenHandCursor);
                }
                return true;
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
    NaturalSorter::sort(images);

    QStringList fullPaths;
    for (const QString& imageName : images) {
        fullPaths.append(folder.filePath(imageName));
    }
    return fullPaths;
}

void PdfMakerDialog::onMakePdf()
{
    QStringList selectedFolders = getCheckedFolderPaths();

    if (selectedFolders.isEmpty()) {
        emit statusMessage("Please select at least one folder");
        return;
    }

    int totalImages = 0;
    for (const QString& folderPath : selectedFolders) {
        totalImages += countImagesInFolder(folderPath);
    }

    if (totalImages == 0) {
        emit statusMessage("No images found in selected folders");
        return;
    }

    const bool batchMode = (m_outputModeComboBox->currentData().toString() == "batch");

    QString combinedSavePath;
    QString batchOutputDir;

    if (batchMode) {
        QString chosen = QFileDialog::getSaveFileName(
            this,
            "Choose Folder Name for Batch PDFs",
            m_baseOutputDir + "/PDFs",
            QStringLiteral("Folder (*)"),
            nullptr,
            QFileDialog::DontConfirmOverwrite
        );
        if (chosen.isEmpty()) {
            return;
        }
        if (chosen.endsWith(".pdf", Qt::CaseInsensitive)) {
            chosen.chop(4);
        }
        QDir().mkpath(chosen);
        if (!QDir(chosen).exists()) {
            emit statusMessage(QString("Failed to create output folder: %1").arg(chosen));
            return;
        }
        batchOutputDir = chosen;
    } else {
        QFileInfo firstFolder(selectedFolders.first());
        QString defaultName = stripSlidesPrefix(firstFolder.fileName()) + ".pdf";

        combinedSavePath = QFileDialog::getSaveFileName(
            this,
            "Save PDF",
            m_baseOutputDir + "/" + defaultName,
            "PDF Files (*.pdf)"
        );

        if (combinedSavePath.isEmpty()) {
            return;
        }

        if (!combinedSavePath.endsWith(".pdf", Qt::CaseInsensitive)) {
            combinedSavePath += ".pdf";
        }
    }

    emit statusMessage(batchMode ? "Generating PDFs..." : "Generating PDF...");

    m_progressBar->setRange(0, totalImages);
    m_progressBar->setValue(0);
    m_makePdfButton->setEnabled(false);

    PdfExportOptions options;
    options.targetHeight = m_resizeComboBox->currentData().toInt();
    options.reduceSize = m_reduceFileSizeCheckbox->isChecked();
    options.jpegQuality = m_jpegQualitySpinBox->value();
    const QString aspectRatioStr = m_aspectRatioComboBox->currentData().toString();
    options.targetAspect = (aspectRatioStr == "4:3") ? (4.0 / 3.0) : (16.0 / 9.0);

    int processedImages = 0;
    auto onImageProcessed = [&]() {
        processedImages++;
        m_progressBar->setValue(processedImages);
        QApplication::processEvents();
    };

    // Flatten the selected folders into one ordered image list, then render.
    auto writePdf = [&](const QString& outputPath, const QStringList& folders) -> int {
        QStringList imagePaths;
        for (const QString& fp : folders) {
            imagePaths.append(getImagesInFolder(fp));
        }
        return PdfExporter::exportToPdf(imagePaths, outputPath, options, onImageProcessed);
    };

    if (batchMode) {
        int filesWritten = 0;
        QString lastWritten;
        QDir outDir(batchOutputDir);
        for (const QString& folderPath : selectedFolders) {
            QFileInfo info(folderPath);
            QString pdfName = stripSlidesPrefix(info.fileName()) + ".pdf";
            QString outPath = outDir.filePath(pdfName);
            int pages = writePdf(outPath, {folderPath});
            if (pages > 0) {
                filesWritten++;
                lastWritten = outPath;
                emit statusMessage(QString("Wrote %1 (%2 pages)").arg(pdfName).arg(pages));
            }
        }

        if (filesWritten > 0) {
            m_lastPdfPath = batchOutputDir;
            m_lastOutputIsFolder = true;
            m_openPdfButton->setText("Open Folder");
            m_openPdfButton->setEnabled(true);
        }

        m_makePdfButton->setEnabled(true);
        emit statusMessage(
            QString("Batch complete: %1 PDFs in %2").arg(filesWritten).arg(batchOutputDir));
    } else {
        int pages = writePdf(combinedSavePath, selectedFolders);

        if (pages > 0) {
            m_lastPdfPath = combinedSavePath;
            m_lastOutputIsFolder = false;
            m_openPdfButton->setText("Open PDF");
            m_openPdfButton->setEnabled(true);
        }

        m_makePdfButton->setEnabled(true);
        emit statusMessage(
            QString("PDF saved: %1 (%2 pages)").arg(combinedSavePath).arg(pages));
    }
}

void PdfMakerDialog::onOpenPdf()
{
    if (!m_lastPdfPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastPdfPath));
    }
}
