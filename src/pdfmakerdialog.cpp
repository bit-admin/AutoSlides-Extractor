#include "pdfmakerdialog.h"
#include "pdfexporter.h"
#include "videoexporter.h"
#include "naturalsorter.h"
#include <QButtonGroup>
#include <QStackedWidget>
#include <QToolButton>
#include <QCloseEvent>
#include <QSettings>
#include <QSignalBlocker>
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

    // Mode switch: PDF | Video
    QHBoxLayout* modeLayout = new QHBoxLayout();
    m_pdfModeButton = new QPushButton("PDF", this);
    m_videoModeButton = new QPushButton("Video", this);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    int modeId = 0;
    for (QPushButton* b : {m_pdfModeButton, m_videoModeButton}) {
        b->setCheckable(true);
        b->setMinimumWidth(140);
        m_modeGroup->addButton(b, modeId++);
    }
    m_pdfModeButton->setToolTip("Export slides as PDF documents");
    m_videoModeButton->setToolTip("Rebuild each folder as a small MP4 that follows its timeline, "
                                  "optionally with the lecture audio");
    if (!VideoExporter::isAvailable()) {
        m_videoModeButton->setEnabled(false);
        m_videoModeButton->setToolTip("This build has no H.264 encoder");
    }
    modeLayout->addStretch();
    modeLayout->addWidget(m_pdfModeButton);
    modeLayout->addWidget(m_videoModeButton);
    modeLayout->addStretch();
    mainLayout->addLayout(modeLayout);

    m_modeStack = new QStackedWidget(this);
    m_modeStack->addWidget(createPdfPage());
    m_modeStack->addWidget(createVideoPage());
    mainLayout->addWidget(m_modeStack, 1);

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

    // Reopen in the mode used last time
    QSettings settings("AutoSlidesExtractor", "AutoSlidesExtractor");
    const bool video = settings.value("slidesExport/mode").toString() == "video"
                       && m_videoModeButton->isEnabled();
    (video ? m_videoModeButton : m_pdfModeButton)->setChecked(true);
    m_modeStack->setCurrentIndex(video ? 1 : 0);
}

QWidget* PdfMakerDialog::createVideoPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto addCombo = [&](QHBoxLayout* row, const QString& label, QLabel** labelOut,
                        const QList<QPair<QString, QVariant>>& items, const QString& tip) {
        QLabel* l = new QLabel(label, page);
        row->addWidget(l);
        QComboBox* c = new QComboBox(page);
        for (const auto& it : items) {
            c->addItem(it.first, it.second);
        }
        c->setToolTip(tip);
        l->setToolTip(tip);
        row->addWidget(c);
        if (labelOut) {
            *labelOut = l;
        }
        return c;
    };

    // Row 1: picture
    QHBoxLayout* videoRow = new QHBoxLayout();
    m_videoResolutionComboBox = addCombo(videoRow, "Resolution:", nullptr,
        {{"Original", 0}, {"1080p", 1080}, {"720p", 720}, {"480p", 480}},
        "Video height. Slides are never scaled up; other aspect ratios are letterboxed.");
    m_videoQualityComboBox = addCombo(videoRow, "Quality:", nullptr,
        {{"High (CRF 18)", 18}, {"Standard (CRF 23)", 23}, {"Small (CRF 28)", 28}},
        "H.264 quality. The picture is a tiny part of the file; audio decides the size.");
    m_videoRepeatComboBox = addCombo(videoRow, "Repeat Frame:", nullptr,
        {{"Off", 0}, {"Every 30 s", 30}, {"Every 1 min", 60}, {"Every 2 min", 120},
         {"Every 5 min", 300}, {"Every 10 min", 600}},
        "Show the current slide again at this interval. Some players show the next frame "
        "after a seek; repeats keep them close. Off = one frame per slide change (smallest).");
    videoRow->addStretch();
    layout->addLayout(videoRow);

    // Row 2: audio
    QHBoxLayout* audioRow = new QHBoxLayout();
    m_audioModeComboBox = addCombo(audioRow, "Audio:", nullptr,
        {{"Copy original", "copy"}, {"Compress (AAC)", "compress"}},
        "Used for folders with an audio source. Copy keeps the original track unchanged; "
        "Compress re-encodes it to AAC with the settings on the right.");
    m_audioBitrateComboBox = addCombo(audioRow, "Bitrate:", &m_audioBitrateLabel,
        {{"24 kbps", 24}, {"32 kbps", 32}, {"48 kbps", 48}, {"64 kbps", 64},
         {"96 kbps", 96}, {"128 kbps", 128}},
        "AAC bitrate. 32–48 kbps mono is plenty for speech.");
    m_audioChannelsComboBox = addCombo(audioRow, "Channels:", &m_audioChannelsLabel,
        {{"Mono", 1}, {"Stereo", 2}}, "Mono halves the bitrate needed for the same quality.");
    m_audioSampleRateComboBox = addCombo(audioRow, "Sample Rate:", &m_audioSampleRateLabel,
        {{"Original", 0}, {"48 kHz", 48000}, {"44.1 kHz", 44100}, {"32 kHz", 32000},
         {"24 kHz", 24000}, {"16 kHz", 16000}},
        "Original keeps the source rate (at most 48 kHz). Lower rates suit low bitrates.");
    audioRow->addStretch();
    layout->addLayout(audioRow);

    QFrame* separator = new QFrame(page);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    QLabel* hint = new QLabel(
        "Each folder becomes one MP4 that follows its timeline. Audio comes from the source "
        "chosen per folder (the original video is filled in when the timeline recorded it); "
        "folders without a source are exported silent.", page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_videoTable = new QTableWidget(page);
    m_videoTable->setColumnCount(5);
    m_videoTable->setHorizontalHeaderLabels({"", "Folder Name", "Count", "Audio Source", ""});
    m_videoTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_videoTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_videoTable->setAlternatingRowColors(true);
    m_videoTable->verticalHeader()->setVisible(false);
    m_videoTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QHeaderView* header = m_videoTable->horizontalHeader();
    header->setSectionResizeMode(VCOL_SELECT, QHeaderView::Fixed);
    header->setSectionResizeMode(VCOL_FOLDER_NAME, QHeaderView::Stretch);
    header->setSectionResizeMode(VCOL_IMAGE_COUNT, QHeaderView::Fixed);
    header->setSectionResizeMode(VCOL_AUDIO_SOURCE, QHeaderView::Stretch);
    header->setSectionResizeMode(VCOL_AUDIO_ACTIONS, QHeaderView::ResizeToContents);
    m_videoTable->setColumnWidth(VCOL_SELECT, 40);
    m_videoTable->setColumnWidth(VCOL_IMAGE_COUNT, 80);
    layout->addWidget(m_videoTable);

    applyVideoOptionsToUi(VideoExportOptions::loadSaved());
    return page;
}

QWidget* PdfMakerDialog::createPdfPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

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

    layout->addLayout(headerLayout);

    // Separator between the two header lines
    QFrame* headerSeparator = new QFrame(this);
    headerSeparator->setFrameShape(QFrame::HLine);
    headerSeparator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(headerSeparator);

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

    layout->addLayout(orderLayout);

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

    layout->addWidget(m_folderTable);

    return page;
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

    connect(m_makePdfButton, &QPushButton::clicked, this, &PdfMakerDialog::onMakeClicked);
    connect(m_openPdfButton, &QPushButton::clicked, this, &PdfMakerDialog::onOpenPdf);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &PdfMakerDialog::onModeChanged);

    connect(m_videoTable, &QTableWidget::cellChanged, this, &PdfMakerDialog::onFolderCheckChanged);
    connect(m_videoTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != VCOL_AUDIO_SOURCE) {
            return;
        }
        if (QTableWidgetItem* item = m_videoTable->item(row, VCOL_SELECT)) {
            if (item->flags() & Qt::ItemIsEnabled) {
                chooseAudioSource(item->data(Qt::UserRole).toString());
            }
        }
    });
    for (QComboBox* c : {m_videoResolutionComboBox, m_videoQualityComboBox, m_videoRepeatComboBox,
                         m_audioModeComboBox, m_audioBitrateComboBox, m_audioChannelsComboBox,
                         m_audioSampleRateComboBox}) {
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &PdfMakerDialog::onVideoSettingChanged);
    }
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
    populateVideoTable();
    onModeChanged();
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
    return getCheckedFolderPaths(m_folderTable);
}

QStringList PdfMakerDialog::getCheckedFolderPaths(const QTableWidget* table) const
{
    // Column 0 holds the checkbox (and the folder path) in both tables
    QStringList paths;
    for (int i = 0; i < table->rowCount(); ++i) {
        QTableWidgetItem* item = table->item(i, 0);
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
    const QTableWidget* table = currentTable();
    const int n = getCheckedFolderPaths(table).count();
    m_folderSelectionLabel->setText(QString("Selected: %1 folders").arg(n));

    int selectable = 0;
    for (int i = 0; i < table->rowCount(); ++i) {
        QTableWidgetItem* item = table->item(i, 0);
        if (item && (item->flags() & Qt::ItemIsEnabled)) {
            ++selectable;
        }
    }
    const bool allChecked = (selectable > 0 && n == selectable);
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
    QTableWidget* table = currentTable();
    const int rowCount = table->rowCount();
    int selectable = 0;
    int checkedCount = 0;
    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = table->item(i, 0);
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) continue;
        ++selectable;
        if (item->checkState() == Qt::Checked) ++checkedCount;
    }
    const Qt::CheckState newState = (selectable > 0 && checkedCount == selectable)
                                        ? Qt::Unchecked : Qt::Checked;
    table->blockSignals(true);
    for (int i = 0; i < rowCount; ++i) {
        QTableWidgetItem* item = table->item(i, 0);
        if (item && (item->flags() & Qt::ItemIsEnabled)) {
            item->setCheckState(newState);
        }
    }
    table->blockSignals(false);
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
    const QString path = isVideoMode() ? m_lastVideoPath : m_lastPdfPath;
    if (!path.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

// ==================== Video mode ====================

bool PdfMakerDialog::isVideoMode() const
{
    return m_modeStack && m_modeStack->currentIndex() == 1;
}

QTableWidget* PdfMakerDialog::currentTable() const
{
    return isVideoMode() ? m_videoTable : m_folderTable;
}

void PdfMakerDialog::onModeChanged()
{
    const bool video = m_videoModeButton->isChecked();
    m_modeStack->setCurrentIndex(video ? 1 : 0);
    QSettings("AutoSlidesExtractor", "AutoSlidesExtractor")
        .setValue("slidesExport/mode", video ? "video" : "pdf");

    if (!m_exporting) {
        m_makePdfButton->setText(video ? "Make Video" : "Make PDF");
    }
    const QString lastPath = video ? m_lastVideoPath : m_lastPdfPath;
    const bool lastIsFolder = video ? m_lastVideoIsFolder : m_lastOutputIsFolder;
    m_openPdfButton->setText(lastIsFolder ? "Open Folder" : (video ? "Open Video" : "Open PDF"));
    m_openPdfButton->setEnabled(!lastPath.isEmpty());
    updateFolderSelectionLabel();
}

void PdfMakerDialog::onMakeClicked()
{
    if (m_exporting) {
        m_cancelRequested = true;
        m_makePdfButton->setEnabled(false);
        emit statusMessage("Cancelling...");
        return;
    }
    if (isVideoMode()) {
        onMakeVideo();
    } else {
        onMakePdf();
    }
}

void PdfMakerDialog::applyVideoOptionsToUi(const VideoExportOptions& o)
{
    m_loadingVideoOptions = true;
    auto select = [](QComboBox* c, const QVariant& value, int fallbackIndex) {
        const int i = c->findData(value);
        c->setCurrentIndex(i >= 0 ? i : fallbackIndex);
    };
    select(m_videoResolutionComboBox, o.targetHeight, 1);
    select(m_videoQualityComboBox, o.crf, 1);
    select(m_videoRepeatComboBox, o.repeatSeconds, 3);
    select(m_audioModeComboBox, VideoExportOptions::audioModeToString(o.audioMode), 1);
    select(m_audioBitrateComboBox, o.audioBitrateKbps, 2);
    select(m_audioChannelsComboBox, o.audioChannels, 0);
    select(m_audioSampleRateComboBox, o.audioSampleRate, 0);
    m_loadingVideoOptions = false;
    updateAudioControls();
}

VideoExportOptions PdfMakerDialog::videoOptionsFromUi() const
{
    VideoExportOptions o;
    o.targetHeight = m_videoResolutionComboBox->currentData().toInt();
    o.crf = m_videoQualityComboBox->currentData().toInt();
    o.repeatSeconds = m_videoRepeatComboBox->currentData().toInt();
    o.audioMode = VideoExportOptions::audioModeFromString(m_audioModeComboBox->currentData().toString(),
                                                          VideoExportOptions::AudioMode::Compress);
    o.audioBitrateKbps = m_audioBitrateComboBox->currentData().toInt();
    o.audioChannels = m_audioChannelsComboBox->currentData().toInt();
    o.audioSampleRate = m_audioSampleRateComboBox->currentData().toInt();
    return o;
}

void PdfMakerDialog::updateAudioControls()
{
    const bool compress = m_audioModeComboBox->currentData().toString() == "compress";
    for (QWidget* w : std::initializer_list<QWidget*>{
             m_audioBitrateLabel, m_audioBitrateComboBox, m_audioChannelsLabel,
             m_audioChannelsComboBox, m_audioSampleRateLabel, m_audioSampleRateComboBox}) {
        w->setEnabled(compress);
    }
}

void PdfMakerDialog::onVideoSettingChanged()
{
    if (m_loadingVideoOptions) {
        return;
    }
    updateAudioControls();
    videoOptionsFromUi().save();
}

void PdfMakerDialog::populateVideoTable()
{
    // Keep check states across refreshes
    const QStringList checked = getCheckedFolderPaths(m_videoTable);

    m_videoTable->blockSignals(true);
    m_videoTable->setRowCount(0);
    QDir outputDir(m_baseOutputDir);

    for (const QString& folderName : m_folderNames) {
        const QString folderPath = outputDir.filePath(folderName);
        const bool hasTimeline = VideoExporter::hasTimeline(folderPath);
        if (!m_audioSources.contains(folderPath)) {
            m_audioSources.insert(folderPath, VideoExporter::recordedSourceVideo(folderPath));
        }
        const QString source = m_audioSources.value(folderPath);

        const int row = m_videoTable->rowCount();
        m_videoTable->insertRow(row);

        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setFlags(hasTimeline ? (Qt::ItemIsUserCheckable | Qt::ItemIsEnabled) : Qt::ItemIsUserCheckable);
        checkItem->setCheckState(hasTimeline && checked.contains(folderPath) ? Qt::Checked : Qt::Unchecked);
        checkItem->setData(Qt::UserRole, folderPath);
        m_videoTable->setItem(row, VCOL_SELECT, checkItem);

        QTableWidgetItem* nameItem = new QTableWidgetItem(stripSlidesPrefix(folderName));
        nameItem->setToolTip(folderPath);
        if (!hasTimeline) {
            nameItem->setFlags(Qt::NoItemFlags);
        }
        m_videoTable->setItem(row, VCOL_FOLDER_NAME, nameItem);

        QTableWidgetItem* countItem = new QTableWidgetItem(QString::number(countImagesInFolder(folderPath)));
        countItem->setTextAlignment(Qt::AlignCenter);
        if (!hasTimeline) {
            countItem->setFlags(Qt::NoItemFlags);
        }
        m_videoTable->setItem(row, VCOL_IMAGE_COUNT, countItem);

        QTableWidgetItem* audioItem = new QTableWidgetItem();
        if (!hasTimeline) {
            audioItem->setText("No timeline.json (extract with the timeline on)");
            audioItem->setFlags(Qt::NoItemFlags);
        } else if (source.isEmpty()) {
            audioItem->setText("None (silent)");
            audioItem->setToolTip("Double-click or use Choose... to add the original video or an audio file");
        } else {
            audioItem->setText(QFileInfo(source).fileName());
            audioItem->setToolTip(source);
        }
        m_videoTable->setItem(row, VCOL_AUDIO_SOURCE, audioItem);

        QWidget* actions = new QWidget(m_videoTable);
        QHBoxLayout* actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(4, 0, 4, 0);
        actionsLayout->setSpacing(4);
        QToolButton* chooseButton = new QToolButton(actions);
        chooseButton->setText("Choose...");
        chooseButton->setToolTip("Choose the original video or an audio file");
        QToolButton* clearButton = new QToolButton(actions);
        clearButton->setText("Clear");
        clearButton->setToolTip("Export this folder without audio");
        chooseButton->setEnabled(hasTimeline);
        clearButton->setEnabled(hasTimeline && !source.isEmpty());
        actionsLayout->addWidget(chooseButton);
        actionsLayout->addWidget(clearButton);
        connect(chooseButton, &QToolButton::clicked, this, [this, folderPath]() { chooseAudioSource(folderPath); });
        connect(clearButton, &QToolButton::clicked, this, [this, folderPath]() { clearAudioSource(folderPath); });
        m_videoTable->setCellWidget(row, VCOL_AUDIO_ACTIONS, actions);
    }

    m_videoTable->blockSignals(false);
    updateFolderSelectionLabel();
}

void PdfMakerDialog::chooseAudioSource(const QString& folderPath)
{
    QString startDir = m_audioSources.value(folderPath);
    if (startDir.isEmpty()) {
        startDir = m_baseOutputDir;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose Audio Source", startDir,
        "Video or audio (*.mp4 *.m4v *.mov *.mkv *.webm *.avi *.wmv *.flv *.ts *.mts "
        "*.m4a *.aac *.mp3 *.wav *.flac *.ogg *.opus *.wma);;All files (*)");
    if (path.isEmpty()) {
        return;
    }
    m_audioSources[folderPath] = path;
    populateVideoTable();
}

void PdfMakerDialog::clearAudioSource(const QString& folderPath)
{
    m_audioSources[folderPath] = QString();
    populateVideoTable();
}

void PdfMakerDialog::setExporting(bool exporting)
{
    m_exporting = exporting;
    m_cancelRequested = false;
    m_modeStack->setEnabled(!exporting);
    m_pdfModeButton->setEnabled(!exporting);
    m_videoModeButton->setEnabled(!exporting && VideoExporter::isAvailable());
    m_refreshButton->setEnabled(!exporting);
    m_closeFoldersButton->setEnabled(!exporting);
    m_toggleSelectFoldersButton->setEnabled(!exporting);
    m_openPdfButton->setEnabled(!exporting && !(isVideoMode() ? m_lastVideoPath : m_lastPdfPath).isEmpty());
    m_makePdfButton->setEnabled(true);
    m_makePdfButton->setText(exporting ? "Cancel" : (isVideoMode() ? "Make Video" : "Make PDF"));
}

void PdfMakerDialog::closeEvent(QCloseEvent* event)
{
    if (m_exporting) {
        // Finish (or cancel) the export first; the loop below is still running
        m_cancelRequested = true;
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void PdfMakerDialog::reject()
{
    if (m_exporting) {
        m_cancelRequested = true; // Esc while exporting cancels instead of closing
        return;
    }
    QDialog::reject();
}

void PdfMakerDialog::onMakeVideo()
{
    const QStringList folders = getCheckedFolderPaths(m_videoTable);
    if (folders.isEmpty()) {
        emit statusMessage("Please select at least one folder");
        return;
    }

    const VideoExportOptions baseOptions = videoOptionsFromUi();
    baseOptions.save();

    // One folder: pick the file. Several: pick a new folder (as for batch PDFs).
    QString singlePath;
    QString batchDir;
    if (folders.size() == 1) {
        const QString defaultName = stripSlidesPrefix(QFileInfo(folders.first()).fileName()) + ".mp4";
        singlePath = QFileDialog::getSaveFileName(this, "Save Video", m_baseOutputDir + "/" + defaultName,
                                                  "MP4 Video (*.mp4)");
        if (singlePath.isEmpty()) {
            return;
        }
        if (!singlePath.endsWith(".mp4", Qt::CaseInsensitive)) {
            singlePath += ".mp4";
        }
    } else {
        QString chosen = QFileDialog::getSaveFileName(this, "Choose Folder Name for Videos",
                                                      m_baseOutputDir + "/Videos", QStringLiteral("Folder (*)"),
                                                      nullptr, QFileDialog::DontConfirmOverwrite);
        if (chosen.isEmpty()) {
            return;
        }
        if (chosen.endsWith(".mp4", Qt::CaseInsensitive)) {
            chosen.chop(4);
        }
        QDir().mkpath(chosen);
        if (!QDir(chosen).exists()) {
            emit statusMessage(QString("Failed to create output folder: %1").arg(chosen));
            return;
        }
        batchDir = chosen;
    }

    setExporting(true);
    m_progressBar->setRange(0, 1000 * int(folders.size()));
    m_progressBar->setValue(0);

    int written = 0;
    QString lastWritten;
    bool cancelled = false;
    for (int i = 0; i < folders.size() && !cancelled; ++i) {
        const QString& folder = folders[i];
        const QString name = stripSlidesPrefix(QFileInfo(folder).fileName());
        const QString outPath = singlePath.isEmpty() ? QDir(batchDir).filePath(name + ".mp4") : singlePath;

        VideoExportOptions options = baseOptions;
        options.audioSourcePath = m_audioSources.value(folder);
        if (options.audioSourcePath.isEmpty()) {
            options.audioMode = VideoExportOptions::AudioMode::None;
        }

        emit statusMessage(QString("Making video %1/%2: %3").arg(i + 1).arg(folders.size()).arg(name));
        const VideoExportResult result = VideoExporter::exportFolder(
            folder, outPath, options, [this, i](double fraction) {
                m_progressBar->setValue(int((i + fraction) * 1000));
                QApplication::processEvents();
                return !m_cancelRequested;
            });

        for (const QString& w : result.warnings) {
            emit statusMessage(QString("%1: %2").arg(name, w));
        }
        if (result.cancelled) {
            cancelled = true;
        } else if (!result.ok) {
            emit statusMessage(QString("Video failed for %1: %2").arg(name, result.error));
        } else {
            ++written;
            lastWritten = outPath;
            emit statusMessage(QString("Wrote %1 (%2 MB, audio: %3)")
                                   .arg(QFileInfo(outPath).fileName())
                                   .arg(QFileInfo(outPath).size() / 1048576.0, 0, 'f', 1)
                                   .arg(result.audioInfo));
        }
        m_progressBar->setValue(1000 * (i + 1));
    }

    if (written > 0) {
        m_lastVideoIsFolder = singlePath.isEmpty();
        m_lastVideoPath = m_lastVideoIsFolder ? batchDir : lastWritten;
    }
    setExporting(false);
    onModeChanged();

    if (cancelled) {
        emit statusMessage(QString("Video export cancelled (%1 written)").arg(written));
    } else if (folders.size() > 1) {
        emit statusMessage(QString("Batch complete: %1 videos in %2").arg(written).arg(batchDir));
    }
}
