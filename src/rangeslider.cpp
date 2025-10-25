#include "rangeslider.h"
#include <QPainter>
#include <QMouseEvent>
#include <QStyleOptionSlider>
#include <QApplication>

RangeSlider::RangeSlider(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_minimum(0)
    , m_maximum(100)
    , m_lowerValue(25)
    , m_upperValue(75)
    , m_pressedHandle(NoHandle)
    , m_clickOffset(0)
    , m_lowZoneColor(QColor(76, 175, 80))    // Green - keep
    , m_midZoneColor(QColor(255, 193, 7))    // Yellow/Amber - check
    , m_highZoneColor(QColor(244, 67, 54))   // Red - delete
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RangeSlider::setMinimum(int min)
{
    if (min < m_maximum) {
        m_minimum = min;
        if (m_lowerValue < m_minimum) setLowerValue(m_minimum);
        if (m_upperValue < m_minimum) setUpperValue(m_minimum);
        update();
    }
}

void RangeSlider::setMaximum(int max)
{
    if (max > m_minimum) {
        m_maximum = max;
        if (m_lowerValue > m_maximum) setLowerValue(m_maximum);
        if (m_upperValue > m_maximum) setUpperValue(m_maximum);
        update();
    }
}

void RangeSlider::setRange(int min, int max)
{
    if (min < max) {
        m_minimum = min;
        m_maximum = max;
        if (m_lowerValue < m_minimum) m_lowerValue = m_minimum;
        if (m_lowerValue > m_maximum) m_lowerValue = m_maximum;
        if (m_upperValue < m_minimum) m_upperValue = m_minimum;
        if (m_upperValue > m_maximum) m_upperValue = m_maximum;
        update();
    }
}

void RangeSlider::setLowerValue(int value)
{
    value = qBound(m_minimum, value, m_upperValue);
    if (value != m_lowerValue) {
        m_lowerValue = value;
        emit lowerValueChanged(m_lowerValue);
        emit rangeChanged(m_lowerValue, m_upperValue);
        update();
    }
}

void RangeSlider::setUpperValue(int value)
{
    value = qBound(m_lowerValue, value, m_maximum);
    if (value != m_upperValue) {
        m_upperValue = value;
        emit upperValueChanged(m_upperValue);
        emit rangeChanged(m_lowerValue, m_upperValue);
        update();
    }
}

void RangeSlider::setZoneLabels(const QString& lowZone, const QString& midZone, const QString& highZone)
{
    m_lowZoneLabel = lowZone;
    m_midZoneLabel = midZone;
    m_highZoneLabel = highZone;
    update();
}

void RangeSlider::setZoneColors(const QColor& lowZone, const QColor& midZone, const QColor& highZone)
{
    m_lowZoneColor = lowZone;
    m_midZoneColor = midZone;
    m_highZoneColor = highZone;
    update();
}

QSize RangeSlider::minimumSizeHint() const
{
    return QSize(100, LABEL_HEIGHT + HANDLE_HEIGHT + VALUE_HEIGHT + MARGIN * 2);
}

QSize RangeSlider::sizeHint() const
{
    return QSize(200, LABEL_HEIGHT + HANDLE_HEIGHT + VALUE_HEIGHT + MARGIN * 2);
}

QRect RangeSlider::grooveRect() const
{
    int y = LABEL_HEIGHT + (HANDLE_HEIGHT - GROOVE_HEIGHT) / 2;
    return QRect(MARGIN + HANDLE_WIDTH / 2, y, width() - MARGIN * 2 - HANDLE_WIDTH, GROOVE_HEIGHT);
}

int RangeSlider::positionFromValue(int value) const
{
    QRect groove = grooveRect();
    double ratio = double(value - m_minimum) / double(m_maximum - m_minimum);
    return groove.left() + int(ratio * groove.width());
}

int RangeSlider::valueFromPosition(int pos) const
{
    QRect groove = grooveRect();
    double ratio = double(pos - groove.left()) / double(groove.width());
    ratio = qBound(0.0, ratio, 1.0);
    return m_minimum + int(ratio * (m_maximum - m_minimum));
}

QRect RangeSlider::handleRect(Handle handle) const
{
    int pos;
    if (handle == LowerHandle) {
        pos = positionFromValue(m_lowerValue);
    } else {
        pos = positionFromValue(m_upperValue);
    }
    int y = LABEL_HEIGHT;  // Handles start below labels
    return QRect(pos - HANDLE_WIDTH / 2, y, HANDLE_WIDTH, HANDLE_HEIGHT);
}

void RangeSlider::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect groove = grooveRect();
    int lowerPos = positionFromValue(m_lowerValue);
    int upperPos = positionFromValue(m_upperValue);

    // Draw groove background
    painter.setPen(Qt::NoPen);

    // Low zone (0 to lower) - Green (keep)
    QRect lowZoneRect(groove.left(), groove.top(), lowerPos - groove.left(), groove.height());
    painter.setBrush(m_lowZoneColor);
    painter.drawRoundedRect(lowZoneRect, 2, 2);

    // Mid zone (lower to upper) - Yellow (check slide prob)
    QRect midZoneRect(lowerPos, groove.top(), upperPos - lowerPos, groove.height());
    painter.setBrush(m_midZoneColor);
    painter.drawRect(midZoneRect);

    // High zone (upper to max) - Red (delete)
    QRect highZoneRect(upperPos, groove.top(), groove.right() - upperPos, groove.height());
    painter.setBrush(m_highZoneColor);
    painter.drawRoundedRect(highZoneRect, 2, 2);

    // Draw zone labels ABOVE the bar
    painter.setPen(palette().color(QPalette::Text));
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    int labelY = 0;
    int labelH = LABEL_HEIGHT;

    if (!m_lowZoneLabel.isEmpty()) {
        QRect labelRect(lowZoneRect.left(), labelY, lowZoneRect.width(), labelH);
        painter.drawText(labelRect, Qt::AlignCenter | Qt::AlignBottom, m_lowZoneLabel);
    }
    if (!m_midZoneLabel.isEmpty()) {
        QRect labelRect(midZoneRect.left(), labelY, midZoneRect.width(), labelH);
        painter.drawText(labelRect, Qt::AlignCenter | Qt::AlignBottom, m_midZoneLabel);
    }
    if (!m_highZoneLabel.isEmpty()) {
        QRect labelRect(highZoneRect.left(), labelY, highZoneRect.width(), labelH);
        painter.drawText(labelRect, Qt::AlignCenter | Qt::AlignBottom, m_highZoneLabel);
    }

    // Draw handles
    auto drawHandle = [&](Handle h) {
        QRect rect = handleRect(h);
        bool pressed = (m_pressedHandle == h);

        // Handle shadow
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 30));
        painter.drawRoundedRect(rect.adjusted(1, 1, 1, 1), 3, 3);

        // Handle body
        QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
        if (pressed) {
            gradient.setColorAt(0, QColor(200, 200, 200));
            gradient.setColorAt(1, QColor(160, 160, 160));
        } else {
            gradient.setColorAt(0, QColor(250, 250, 250));
            gradient.setColorAt(1, QColor(220, 220, 220));
        }
        painter.setBrush(gradient);
        painter.setPen(QPen(QColor(180, 180, 180), 1));
        painter.drawRoundedRect(rect, 3, 3);

        // Handle grip lines
        painter.setPen(QPen(QColor(160, 160, 160), 1));
        int cx = rect.center().x();
        int cy = rect.center().y();
        painter.drawLine(cx - 1, cy - 3, cx - 1, cy + 3);
        painter.drawLine(cx + 1, cy - 3, cx + 1, cy + 3);
    };

    drawHandle(LowerHandle);
    drawHandle(UpperHandle);

    // Draw value labels below handles
    painter.setPen(palette().color(QPalette::Text));
    font.setPointSize(9);
    painter.setFont(font);

    QString lowerText = QString::number(m_lowerValue / 100.0, 'f', 2);
    QString upperText = QString::number(m_upperValue / 100.0, 'f', 2);

    QRect lowerRect = handleRect(LowerHandle);
    QRect upperRect = handleRect(UpperHandle);

    painter.drawText(QRect(lowerRect.left() - 10, lowerRect.bottom() + 2, 30, 15),
                     Qt::AlignCenter, lowerText);
    painter.drawText(QRect(upperRect.left() - 10, upperRect.bottom() + 2, 30, 15),
                     Qt::AlignCenter, upperText);
}

void RangeSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QRect lowerRect = handleRect(LowerHandle);
        QRect upperRect = handleRect(UpperHandle);

        // Check which handle was clicked (upper handle has priority if overlapping)
        if (upperRect.contains(event->pos())) {
            m_pressedHandle = UpperHandle;
            m_clickOffset = event->pos().x() - positionFromValue(m_upperValue);
        } else if (lowerRect.contains(event->pos())) {
            m_pressedHandle = LowerHandle;
            m_clickOffset = event->pos().x() - positionFromValue(m_lowerValue);
        } else {
            // Click on track - move nearest handle
            int clickValue = valueFromPosition(event->pos().x());
            if (qAbs(clickValue - m_lowerValue) < qAbs(clickValue - m_upperValue)) {
                setLowerValue(clickValue);
                m_pressedHandle = LowerHandle;
            } else {
                setUpperValue(clickValue);
                m_pressedHandle = UpperHandle;
            }
            m_clickOffset = 0;
        }
        update();
    }
}

void RangeSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressedHandle != NoHandle) {
        int value = valueFromPosition(event->pos().x() - m_clickOffset);
        if (m_pressedHandle == LowerHandle) {
            setLowerValue(value);
        } else {
            setUpperValue(value);
        }
    } else {
        // Update cursor when hovering over handles
        QRect lowerRect = handleRect(LowerHandle);
        QRect upperRect = handleRect(UpperHandle);
        if (lowerRect.contains(event->pos()) || upperRect.contains(event->pos())) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void RangeSlider::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    m_pressedHandle = NoHandle;
    update();
}
