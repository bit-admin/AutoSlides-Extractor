#include "autocroptestpreviewwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QImage>
#include <QDir>
#include <QPalette>
#include <algorithm>
#include <cmath>

AutoCropTestPreviewWidget::AutoCropTestPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30));
    setPalette(pal);
}

void AutoCropTestPreviewWidget::setOpenHandler(std::function<void(const QString&)> handler)
{
    m_openHandler = std::move(handler);
}

void AutoCropTestPreviewWidget::setPreview(const QPixmap& pixmap, const QString& imagePath,
                                           const QRect& bbox, bool showCross)
{
    m_pixmap = pixmap;
    m_imagePath = imagePath;
    m_annotatedPath.clear();
    m_bbox = bbox;
    m_showCross = showCross;
    setCursor(!m_pixmap.isNull() && !m_imagePath.isEmpty() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void AutoCropTestPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    if (m_pixmap.isNull()) {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter, "No image selected");
        return;
    }

    const QRect display = imageDisplayRect();
    painter.drawPixmap(display, m_pixmap);

    if (!m_showCross && m_bbox.isValid() && m_bbox.width() > 0 && m_bbox.height() > 0) {
        const QRect box = imageRectToWidgetRect(m_bbox).intersected(display);
        if (box.isValid() && box.width() > 0 && box.height() > 0) {
            drawOverlay(painter, box);
        }
        return;
    }

    drawOverlay(painter, display);
}

void AutoCropTestPreviewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton
        && !m_pixmap.isNull()
        && !m_imagePath.isEmpty()
        && imageDisplayRect().contains(event->pos())
        && m_openHandler) {
        const QString path = annotatedPreviewPath();
        m_openHandler(saveAnnotatedPreview(path) ? path : QString());
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void AutoCropTestPreviewWidget::drawOverlay(QPainter& painter, const QRect& targetRect) const
{
    const int penWidth = std::max(3, std::min(targetRect.width(), targetRect.height()) / 160);
    QPen redPen(QColor(220, 0, 0));
    redPen.setWidth(penWidth);
    painter.setPen(redPen);

    if (!m_showCross && m_bbox.isValid() && m_bbox.width() > 0 && m_bbox.height() > 0) {
        painter.drawRect(targetRect.adjusted(penWidth / 2,
                                             penWidth / 2,
                                             -penWidth / 2,
                                             -penWidth / 2));
        return;
    }

    painter.drawLine(targetRect.topLeft(), targetRect.bottomRight());
    painter.drawLine(targetRect.topRight(), targetRect.bottomLeft());
}

QString AutoCropTestPreviewWidget::annotatedPreviewPath()
{
    if (!m_annotatedPath.isEmpty()) {
        return m_annotatedPath;
    }

    m_annotatedPath = QDir(QDir::tempPath() + "/AutoSlidesExtractor")
        .filePath("autocrop_test_preview.png");
    return m_annotatedPath;
}

bool AutoCropTestPreviewWidget::saveAnnotatedPreview(const QString& outPath) const
{
    if (m_pixmap.isNull()) {
        return false;
    }

    QImage annotated = m_pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (annotated.isNull()) {
        return false;
    }

    QPainter painter(&annotated);
    const QRect overlayRect = (!m_showCross && m_bbox.isValid() && m_bbox.width() > 0 && m_bbox.height() > 0)
        ? m_bbox.intersected(annotated.rect())
        : annotated.rect();
    if (overlayRect.isValid() && overlayRect.width() > 0 && overlayRect.height() > 0) {
        drawOverlay(painter, overlayRect);
    }
    painter.end();

    QDir tempDir(QDir::tempPath() + "/AutoSlidesExtractor");
    if (!tempDir.exists() && !tempDir.mkpath(".")) {
        return false;
    }

    return annotated.save(outPath, "PNG");
}

QRect AutoCropTestPreviewWidget::imageDisplayRect() const
{
    if (m_pixmap.isNull()) {
        return QRect();
    }

    const QSize scaled = m_pixmap.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - scaled.width()) / 2,
                        (height() - scaled.height()) / 2),
                 scaled);
}

QRect AutoCropTestPreviewWidget::imageRectToWidgetRect(const QRect& imageRect) const
{
    if (m_pixmap.isNull()) {
        return QRect();
    }

    const QRect display = imageDisplayRect();
    if (display.isEmpty()) {
        return QRect();
    }

    const qreal sx = static_cast<qreal>(display.width()) / m_pixmap.width();
    const qreal sy = static_cast<qreal>(display.height()) / m_pixmap.height();

    return QRect(display.x() + static_cast<int>(std::round(imageRect.x() * sx)),
                 display.y() + static_cast<int>(std::round(imageRect.y() * sy)),
                 static_cast<int>(std::round(imageRect.width() * sx)),
                 static_cast<int>(std::round(imageRect.height() * sy)));
}
