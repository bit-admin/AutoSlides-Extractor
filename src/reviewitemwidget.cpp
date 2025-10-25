#include "reviewitemwidget.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QFileInfo>
#include <cmath>

ReviewItemWidget::ReviewItemWidget(const TrashEntry& entry,
                                   const QString& baseOutputDir,
                                   QWidget* parent)
    : QWidget(parent),
      m_state(Removed),
      m_entry(entry),
      m_trashedFilename(entry.trashedFilename),
      m_thumbnailWidth(kDefaultThumbnailWidth)
{
    buildLayout();

    m_thumbnailLabel->setStyleSheet(
        "QLabel { background-color: #f0f0f0; border: 2px solid #d32f2f; }");

    m_originalPixmap = QPixmap(entry.getTrashedPath(baseOutputDir));

    m_nameLabel->setText(QString("#%1").arg(entry.slideIndex));
    m_methodLabel->setText(QString("Removed · %1").arg(entry.getCategoryDisplayName()));
    m_methodLabel->setStyleSheet("QLabel { color: #d32f2f; font-size: 12px; }");

    QString tooltip = QString("Video: %1\nSlide: %2\nReason: %3\nDetail: %4\nDate: %5")
                        .arg(entry.videoName)
                        .arg(entry.slideIndex)
                        .arg(entry.getCategoryDisplayName())
                        .arg(entry.reason)
                        .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss"));
    setToolTip(tooltip);

    setThumbnailWidth(m_thumbnailWidth);
}

ReviewItemWidget::ReviewItemWidget(const QString& imagePath,
                                   QWidget* parent)
    : QWidget(parent),
      m_state(Extracted),
      m_imagePath(imagePath),
      m_thumbnailWidth(kDefaultThumbnailWidth)
{
    buildLayout();

    m_thumbnailLabel->setStyleSheet(
        "QLabel { background-color: #f0f0f0; border: 1px solid #ccc; }");

    m_originalPixmap = QPixmap(imagePath);

    QFileInfo fileInfo(imagePath);
    QString basename = fileInfo.completeBaseName();
    int lastUnderscore = basename.lastIndexOf('_');
    QString index = (lastUnderscore >= 0) ? basename.mid(lastUnderscore + 1) : basename;

    m_nameLabel->setText(QString("#%1").arg(index));
    m_methodLabel->setText("Extracted");
    m_methodLabel->setStyleSheet("QLabel { color: palette(text); font-size: 12px; }");
    setToolTip(fileInfo.fileName());

    setThumbnailWidth(m_thumbnailWidth);
}

void ReviewItemWidget::buildLayout()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    m_checkbox = new QCheckBox(this);
    m_viewButton = new QPushButton("View", this);
    m_viewButton->setFixedHeight(22);
    m_viewButton->setMinimumWidth(56);
    m_viewButton->setToolTip("View this slide full-window");
    m_viewButton->setCursor(Qt::PointingHandCursor);

    m_setBaselineButton = new QPushButton("Set as Baseline", this);
    m_setBaselineButton->setFixedHeight(22);
    m_setBaselineButton->setToolTip("Capture this slide's crop area as a baseline for batch cropping");
    m_setBaselineButton->setCursor(Qt::PointingHandCursor);
    m_setBaselineButton->setVisible(false);

    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(m_checkbox);
    topRow->addStretch();
    topRow->addWidget(m_setBaselineButton);
    topRow->addWidget(m_viewButton);
    layout->addLayout(topRow);

    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setFrameStyle(QFrame::Box | QFrame::Plain);
    layout->addWidget(m_thumbnailLabel, 0, Qt::AlignCenter);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAlignment(Qt::AlignVCenter);
    m_nameLabel->setStyleSheet("QLabel { font-weight: bold; }");

    m_methodLabel = new QLabel(this);
    m_methodLabel->setAlignment(Qt::AlignVCenter);

    m_cropLabel = new QLabel(this);
    m_cropLabel->setAlignment(Qt::AlignVCenter);
    m_cropLabel->setText("Cropped");
    m_cropLabel->setStyleSheet("QLabel { color: #1565c0; font-size: 12px; font-weight: bold; }");
    m_cropLabel->setVisible(false);

    QHBoxLayout* infoRow = new QHBoxLayout();
    infoRow->setContentsMargins(0, 0, 0, 0);
    infoRow->setSpacing(12);
    infoRow->addStretch();
    infoRow->addWidget(m_nameLabel);
    infoRow->addWidget(m_methodLabel);
    infoRow->addWidget(m_cropLabel);
    infoRow->addStretch();
    layout->addLayout(infoRow);

    connect(m_checkbox, &QCheckBox::toggled, this, &ReviewItemWidget::selectionChanged);
    connect(m_viewButton, &QPushButton::clicked, this, &ReviewItemWidget::viewClicked);
    connect(m_setBaselineButton, &QPushButton::clicked, this, &ReviewItemWidget::setBaselineClicked);
}

void ReviewItemWidget::setCropped(bool cropped)
{
    m_cropped = cropped;
    if (m_cropLabel) {
        // Only Extracted slides can be cropped.
        m_cropLabel->setVisible(cropped && m_state == Extracted);
    }
    if (m_setBaselineButton) {
        m_setBaselineButton->setVisible(cropped && m_state == Extracted);
    }
    // Refresh the thumbnail from disk so the displayed image reflects the
    // current (possibly cropped) live file.
    if (m_state == Extracted && !m_imagePath.isEmpty()) {
        m_originalPixmap = QPixmap(m_imagePath);
        applyThumbnail();
    }
}

void ReviewItemWidget::setThumbnailWidth(int width)
{
    width = std::clamp(width, kMinThumbnailWidth, kMaxThumbnailWidth);
    m_thumbnailWidth = width;

    int thumbHeight = static_cast<int>(std::round(width * 9.0 / 16.0));
    m_thumbnailLabel->setFixedSize(width, thumbHeight);

    // Widget is the thumbnail plus surrounding padding for checkbox + index/status row.
    setFixedSize(width + 20, thumbHeight + 80);

    applyThumbnail();
}

void ReviewItemWidget::applyThumbnail()
{
    int thumbHeight = static_cast<int>(std::round(m_thumbnailWidth * 9.0 / 16.0));
    if (!m_originalPixmap.isNull()) {
        QPixmap scaled = m_originalPixmap.scaled(m_thumbnailWidth, thumbHeight,
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);
        m_thumbnailLabel->setPixmap(scaled);
    } else {
        m_thumbnailLabel->setText("No Preview");
    }
}

bool ReviewItemWidget::isChecked() const
{
    return m_checkbox->isChecked();
}

void ReviewItemWidget::setChecked(bool checked)
{
    m_checkbox->setChecked(checked);
}

void ReviewItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_checkbox->setChecked(!m_checkbox->isChecked());
    }
    QWidget::mousePressEvent(event);
}
