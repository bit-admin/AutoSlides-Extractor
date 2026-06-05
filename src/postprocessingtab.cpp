#include "postprocessingtab.h"
#include "phashcalculator.h"
#include "configmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>

PostProcessingTab::PostProcessingTab(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(this);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === HAMMING THRESHOLD SETTINGS ===
    QGroupBox* thresholdGroup = new QGroupBox("pHash Hamming Distance Threshold", this);
    QGridLayout* thresholdLayout = new QGridLayout(thresholdGroup);
    thresholdLayout->setContentsMargins(12, 12, 12, 12);
    thresholdLayout->setSpacing(8);

    QLabel* thresholdLabel = new QLabel("Threshold:", this);
    m_hammingThresholdSpinBox = new QSpinBox(this);
    m_hammingThresholdSpinBox->setRange(0, 50);
    m_hammingThresholdSpinBox->setValue(10);

    QLabel* thresholdHelpLabel = new QLabel("Lower Hamming distance threshold indicate stricter matching.", this);
    thresholdHelpLabel->setWordWrap(true);
    thresholdHelpLabel->setStyleSheet("color: #666; font-size: 11px;");

    thresholdLayout->addWidget(thresholdLabel, 0, 0);
    thresholdLayout->addWidget(m_hammingThresholdSpinBox, 0, 1);
    thresholdLayout->addWidget(thresholdHelpLabel, 1, 0, 1, 2);

    tabLayout->addWidget(thresholdGroup);

    // === EXCLUSION LIST ===
    QGroupBox* exclusionGroup = new QGroupBox("pHash Excluded List", this);
    QVBoxLayout* exclusionLayout = new QVBoxLayout(exclusionGroup);
    exclusionLayout->setContentsMargins(12, 12, 12, 12);
    exclusionLayout->setSpacing(8);

    QLabel* exclusionHelpLabel = new QLabel("Images with pHash matching these entries will be automatically moved to trash during post-processing.", this);
    exclusionHelpLabel->setWordWrap(true);
    exclusionHelpLabel->setStyleSheet("color: #666; font-size: 11px;");
    exclusionLayout->addWidget(exclusionHelpLabel);

    m_exclusionTable = new QTableWidget(this);
    m_exclusionTable->setColumnCount(3);
    m_exclusionTable->setHorizontalHeaderLabels({"Remark", "Hash", "Delete"});
    m_exclusionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_exclusionTable->setAlternatingRowColors(true);
    m_exclusionTable->horizontalHeader()->setStretchLastSection(false);
    m_exclusionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_exclusionTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_exclusionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_exclusionTable->setColumnWidth(0, 120);
    m_exclusionTable->setColumnWidth(2, 60);
    m_exclusionTable->setMinimumHeight(200);

    exclusionLayout->addWidget(m_exclusionTable);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_addFromImageButton = new QPushButton("Add from Image", this);
    m_manualInputButton = new QPushButton("Manual Input", this);
    buttonLayout->addWidget(m_addFromImageButton, 1);  // 50% width
    buttonLayout->addWidget(m_manualInputButton, 1);   // 50% width
    exclusionLayout->addLayout(buttonLayout);

    tabLayout->addWidget(exclusionGroup);

    connect(m_addFromImageButton, &QPushButton::clicked, this, &PostProcessingTab::onAddFromImage);
    connect(m_manualInputButton, &QPushButton::clicked, this, &PostProcessingTab::onManualInput);

    rebuildTable();
}

void PostProcessingTab::load(const AppConfig& config)
{
    m_hammingThresholdSpinBox->setValue(config.hammingThreshold);
}

void PostProcessingTab::store(AppConfig& config) const
{
    config.hammingThreshold = m_hammingThresholdSpinBox->value();
}

void PostProcessingTab::setExclusionList(const QList<ExclusionEntry>& list)
{
    m_exclusionList = list;
    rebuildTable();
}

void PostProcessingTab::rebuildTable()
{
    m_exclusionTable->setRowCount(m_exclusionList.size());

    for (int i = 0; i < m_exclusionList.size(); i++) {
        QTableWidgetItem* remarkItem = new QTableWidgetItem(m_exclusionList[i].remark);
        remarkItem->setFlags(remarkItem->flags() & ~Qt::ItemIsEditable);
        m_exclusionTable->setItem(i, 0, remarkItem);

        QTableWidgetItem* hashItem = new QTableWidgetItem(m_exclusionList[i].hashHex);
        hashItem->setFlags(hashItem->flags() & ~Qt::ItemIsEditable);
        m_exclusionTable->setItem(i, 1, hashItem);

        QPushButton* deleteButton = new QPushButton("Delete", this);
        connect(deleteButton, &QPushButton::clicked, this, [this, i]() {
            if (i < m_exclusionList.size()) {
                m_exclusionList.removeAt(i);
                rebuildTable();
            }
        });
        m_exclusionTable->setCellWidget(i, 2, deleteButton);
    }
}

void PostProcessingTab::onAddFromImage()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        "Select Image",
        QString(),
        "Image Files (*.jpg *.jpeg *.png *.bmp);;All Files (*)");

    if (filePath.isEmpty()) {
        return;
    }

    std::vector<uint8_t> hash = PHashCalculator::calculatePHash(filePath);
    if (hash.empty()) {
        emit statusMessage("Error: Failed to calculate pHash for the selected image.");
        return;
    }

    bool ok;
    QString remark = QInputDialog::getText(this, "Add Exclusion Entry",
                                          "Enter a remark for this entry:",
                                          QLineEdit::Normal, "", &ok);
    if (!ok || remark.isEmpty()) {
        remark = "Custom";
    }

    QString hashHex = PHashCalculator::hashToHexString(hash);
    m_exclusionList.append(ExclusionEntry(remark, hashHex));
    rebuildTable();
}

void PostProcessingTab::onManualInput()
{
    bool ok;
    QString hashHex = QInputDialog::getText(this, "Manual Input",
                                           "Enter 256-bit pHash (64 hex characters):",
                                           QLineEdit::Normal, "", &ok);

    if (!ok || hashHex.isEmpty()) {
        return;
    }

    if (hashHex.length() != 64) {
        emit statusMessage("Error: Hash must be exactly 64 hexadecimal characters.");
        return;
    }

    std::vector<uint8_t> hash = PHashCalculator::hexStringToHash(hashHex);
    if (hash.empty()) {
        emit statusMessage("Error: Invalid hexadecimal string.");
        return;
    }

    QString remark = QInputDialog::getText(this, "Add Exclusion Entry",
                                          "Enter a remark for this entry:",
                                          QLineEdit::Normal, "", &ok);
    if (!ok || remark.isEmpty()) {
        remark = "Custom";
    }

    m_exclusionList.append(ExclusionEntry(remark, hashHex));
    rebuildTable();
}
