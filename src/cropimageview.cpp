#include "cropimageview.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QPen>
#include <QBrush>
#include <algorithm>

CropImageView::CropImageView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 240);
    // Background is dark so letterboxing is visible.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30));
    setPalette(pal);
}

void CropImageView::setImage(const QPixmap& pixmap)
{
    m_pixmap = pixmap;
    clearSelection();
    update();
}

void CropImageView::setSelectionMode(bool enabled)
{
    if (m_selectionMode == enabled) {
        return;
    }
    m_selectionMode = enabled;
    if (!enabled) {
        m_dragging = false;
    }
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void CropImageView::setInitialSelection(const QRect& imageRect)
{
    if (m_pixmap.isNull()) {
        m_selectionImageRect = QRect();
        return;
    }
    QRect r = imageRect;
    r = r.intersected(m_pixmap.rect());
    if (r.width() <= 0 || r.height() <= 0) {
        m_selectionImageRect = QRect();
    } else {
        m_selectionImageRect = r;
    }
    update();
}

QRect CropImageView::selectionImageRect() const
{
    return m_selectionImageRect;
}

bool CropImageView::hasSelection() const
{
    return m_selectionImageRect.isValid()
        && m_selectionImageRect.width() > 0
        && m_selectionImageRect.height() > 0;
}

void CropImageView::clearSelection()
{
    m_selectionImageRect = QRect();
    m_dragging = false;
    update();
}

void CropImageView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

QRect CropImageView::imageDisplayRect() const
{
    if (m_pixmap.isNull()) {
        return QRect();
    }
    QSize avail = size();
    QSize imgSize = m_pixmap.size();
    QSize scaled = imgSize.scaled(avail, Qt::KeepAspectRatio);

    int x = (avail.width() - scaled.width()) / 2;
    int y = (avail.height() - scaled.height()) / 2;
    return QRect(QPoint(x, y), scaled);
}

QRect CropImageView::widgetRectToImageRect(const QRect& wr) const
{
    if (m_pixmap.isNull()) {
        return QRect();
    }
    QRect display = imageDisplayRect();
    if (display.isEmpty()) {
        return QRect();
    }

    qreal sx = static_cast<qreal>(m_pixmap.width()) / display.width();
    qreal sy = static_cast<qreal>(m_pixmap.height()) / display.height();

    int ix = static_cast<int>(std::round((wr.x() - display.x()) * sx));
    int iy = static_cast<int>(std::round((wr.y() - display.y()) * sy));
    int iw = static_cast<int>(std::round(wr.width() * sx));
    int ih = static_cast<int>(std::round(wr.height() * sy));

    QRect out(ix, iy, iw, ih);
    return out.intersected(m_pixmap.rect());
}

QRect CropImageView::imageRectToWidgetRect(const QRect& ir) const
{
    if (m_pixmap.isNull()) {
        return QRect();
    }
    QRect display = imageDisplayRect();
    if (display.isEmpty()) {
        return QRect();
    }

    qreal sx = static_cast<qreal>(display.width()) / m_pixmap.width();
    qreal sy = static_cast<qreal>(display.height()) / m_pixmap.height();

    int wx = display.x() + static_cast<int>(std::round(ir.x() * sx));
    int wy = display.y() + static_cast<int>(std::round(ir.y() * sy));
    int ww = static_cast<int>(std::round(ir.width() * sx));
    int wh = static_cast<int>(std::round(ir.height() * sy));

    return QRect(wx, wy, ww, wh);
}

QRect CropImageView::normalizedClampedWidgetRect(const QPoint& a, const QPoint& b) const
{
    QRect display = imageDisplayRect();
    if (display.isEmpty()) {
        return QRect();
    }

    auto clamp = [&](QPoint p) {
        p.setX(std::clamp(p.x(), display.left(), display.right()));
        p.setY(std::clamp(p.y(), display.top(), display.bottom()));
        return p;
    };

    QPoint ca = clamp(a);
    QPoint cb = clamp(b);

    int x1 = std::min(ca.x(), cb.x());
    int y1 = std::min(ca.y(), cb.y());
    int x2 = std::max(ca.x(), cb.x());
    int y2 = std::max(ca.y(), cb.y());

    return QRect(x1, y1, x2 - x1, y2 - y1);
}

void CropImageView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());

    if (m_pixmap.isNull()) {
        p.setPen(QColor(180, 180, 180));
        p.drawText(rect(), Qt::AlignCenter, "No image");
        return;
    }

    QRect display = imageDisplayRect();
    p.drawPixmap(display, m_pixmap);

    // Determine which rect to draw: live drag or stored selection.
    QRect overlayWidget;
    if (m_dragging) {
        overlayWidget = normalizedClampedWidgetRect(m_dragStart, m_dragCurrent);
    } else if (hasSelection()) {
        overlayWidget = imageRectToWidgetRect(m_selectionImageRect).intersected(display);
    }

    if (m_selectionMode || hasSelection()) {
        // Dim outside selection, draw bounding box.
        if (overlayWidget.isValid() && overlayWidget.width() > 0 && overlayWidget.height() > 0) {
            QRegion outside(display);
            outside = outside.subtracted(QRegion(overlayWidget));
            p.save();
            p.setClipRegion(outside);
            p.fillRect(display, QColor(0, 0, 0, 120));
            p.restore();

            QPen pen(QColor(255, 255, 255));
            pen.setWidth(1);
            p.setPen(pen);
            p.drawRect(overlayWidget.adjusted(0, 0, -1, -1));

            // Inner accent line for visibility.
            QPen accent(QColor(33, 150, 243));
            accent.setStyle(Qt::DashLine);
            accent.setWidth(1);
            p.setPen(accent);
            p.drawRect(overlayWidget.adjusted(0, 0, -1, -1));
        } else if (m_selectionMode) {
            // Crop mode active but no selection yet — dim the whole image faintly.
            p.fillRect(display, QColor(0, 0, 0, 60));
        }
    }
}

void CropImageView::mousePressEvent(QMouseEvent* event)
{
    if (!m_selectionMode || event->button() != Qt::LeftButton || m_pixmap.isNull()) {
        QWidget::mousePressEvent(event);
        return;
    }

    QRect display = imageDisplayRect();
    if (!display.contains(event->pos())) {
        // Allow starting outside; the rect will clamp.
    }

    m_dragging = true;
    m_dragStart = event->pos();
    m_dragCurrent = event->pos();
    update();
}

void CropImageView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        m_dragCurrent = event->pos();
        update();
    }
}

void CropImageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_dragging = false;
    m_dragCurrent = event->pos();

    QRect widgetRect = normalizedClampedWidgetRect(m_dragStart, m_dragCurrent);
    if (widgetRect.width() < 4 || widgetRect.height() < 4) {
        // Treat tiny drags as a clear.
        m_selectionImageRect = QRect();
    } else {
        m_selectionImageRect = widgetRectToImageRect(widgetRect);
        if (m_selectionImageRect.width() <= 0 || m_selectionImageRect.height() <= 0) {
            m_selectionImageRect = QRect();
        }
    }
    update();
}
