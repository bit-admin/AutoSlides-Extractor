#include "slideitemwidget.h"
#include <QMouseEvent>
#include <QFileInfo>

SlideItemWidget::SlideItemWidget(const QString& imagePath,
                                QWidget *parent)
    : QWidget(parent),
      m_imagePath(imagePath)
{
    QFileInfo fileInfo(imagePath);
    m_imageName = fileInfo.fileName();

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

    QPixmap pixmap(imagePath);
    if (!pixmap.isNull()) {
        QPixmap thumbnail = pixmap.scaled(280, 280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_thumbnailLabel->setPixmap(thumbnail);
    } else {
        m_thumbnailLabel->setText("No Preview");
    }

    layout->addWidget(m_thumbnailLabel, 0, Qt::AlignCenter);

    // Image name (without extension for cleaner display)
    QString displayName = fileInfo.completeBaseName();
    m_nameLabel = new QLabel(displayName, this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setStyleSheet("QLabel { font-weight: bold; }");
    m_nameLabel->setWordWrap(true);
    layout->addWidget(m_nameLabel);

    // Set tooltip with full filename
    setToolTip(m_imageName);

    // Set fixed size for the widget
    setFixedSize(300, 360);

    // Connect checkbox signal
    connect(m_checkbox, &QCheckBox::toggled, this, &SlideItemWidget::selectionChanged);

    // Note: Removed setCursor(Qt::PointingHandCursor) as it can cause crashes
    // on macOS when used in modal dialogs with many widgets (macOS ImageIO bug)
}

bool SlideItemWidget::isChecked() const
{
    return m_checkbox->isChecked();
}

void SlideItemWidget::setChecked(bool checked)
{
    m_checkbox->setChecked(checked);
}

void SlideItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Toggle checkbox when clicking anywhere on the widget
        m_checkbox->setChecked(!m_checkbox->isChecked());
    }
    QWidget::mousePressEvent(event);
}
