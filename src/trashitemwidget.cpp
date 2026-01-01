#include "trashitemwidget.h"
#include <QMouseEvent>
#include <QStyle>

TrashItemWidget::TrashItemWidget(const TrashEntry& entry,
                                const QString& baseOutputDir,
                                QWidget *parent)
    : QWidget(parent),
      m_entry(entry),
      m_trashedFilename(entry.trashedFilename)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // Checkbox at the top
    m_checkbox = new QCheckBox(this);
    layout->addWidget(m_checkbox, 0, Qt::AlignLeft);

    // Thumbnail in the center
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setFixedSize(280, 280);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_thumbnailLabel->setStyleSheet("QLabel { background-color: #f0f0f0; border: 1px solid #ccc; }");

    QPixmap thumbnail = entry.getThumbnail(baseOutputDir, 280);
    if (!thumbnail.isNull()) {
        m_thumbnailLabel->setPixmap(thumbnail);
    } else {
        m_thumbnailLabel->setText("No Preview");
    }

    layout->addWidget(m_thumbnailLabel, 0, Qt::AlignCenter);

    // Image name
    m_nameLabel = new QLabel(entry.getDisplayName(), this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setStyleSheet("QLabel { font-weight: bold; }");
    layout->addWidget(m_nameLabel);

    // Method label
    m_methodLabel = new QLabel(entry.getMethodDisplayName(), this);
    m_methodLabel->setAlignment(Qt::AlignCenter);
    m_methodLabel->setStyleSheet("QLabel { color: #666; font-size: 11px; }");
    layout->addWidget(m_methodLabel);

    // Set tooltip with full details
    QString tooltip = QString("Video: %1\nSlide: %2\nMethod: %3\nReason: %4\nDate: %5")
                        .arg(entry.videoName)
                        .arg(entry.slideIndex)
                        .arg(entry.getMethodDisplayName())
                        .arg(entry.reason)
                        .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss"));
    setToolTip(tooltip);

    // Set fixed size for the widget
    setFixedSize(300, 380);

    // Connect checkbox signal
    connect(m_checkbox, &QCheckBox::toggled, this, &TrashItemWidget::selectionChanged);

    // Note: Removed setCursor(Qt::PointingHandCursor) as it can cause crashes
    // on macOS when used in modal dialogs with many widgets (macOS ImageIO bug)
}

bool TrashItemWidget::isChecked() const
{
    return m_checkbox->isChecked();
}

void TrashItemWidget::setChecked(bool checked)
{
    m_checkbox->setChecked(checked);
}

void TrashItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Toggle checkbox when clicking anywhere on the widget
        m_checkbox->setChecked(!m_checkbox->isChecked());
    }
    QWidget::mousePressEvent(event);
}
