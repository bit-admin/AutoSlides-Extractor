#include "styledslider.h"
#include <QPainter>
#include <QMouseEvent>

StyledSlider::StyledSlider(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_minimum(0)
    , m_maximum(100)
    , m_value(50)
    , m_pressed(false)
    , m_clickOffset(0)
    , m_barColor(QColor(100, 149, 237))  // Cornflower blue
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StyledSlider::setMinimum(int min)
{
    if (min < m_maximum) {
        m_minimum = min;
        if (m_value < m_minimum) setValue(m_minimum);
        update();
    }
}

void StyledSlider::setMaximum(int max)
{
    if (max > m_minimum) {
        m_maximum = max;
        if (m_value > m_maximum) setValue(m_maximum);
        update();
    }
}

void StyledSlider::setRange(int min, int max)
{
    if (min < max) {
        m_minimum = min;
        m_maximum = max;
        if (m_value < m_minimum) m_value = m_minimum;
        if (m_value > m_maximum) m_value = m_maximum;
        update();
    }
}

void StyledSlider::setValue(int value)
{
    value = qBound(m_minimum, value, m_maximum);
    if (value != m_value) {
        m_value = value;
        emit valueChanged(m_value);
        update();
    }
}

void StyledSlider::setLabel(const QString& label)
{
    m_label = label;
    update();
}

void StyledSlider::setBarColor(const QColor& color)
{
    m_barColor = color;
    update();
}

QSize StyledSlider::minimumSizeHint() const
{
    return QSize(100, LABEL_HEIGHT + HANDLE_HEIGHT + VALUE_HEIGHT + MARGIN * 2);
}

QSize StyledSlider::sizeHint() const
{
    return QSize(200, LABEL_HEIGHT + HANDLE_HEIGHT + VALUE_HEIGHT + MARGIN * 2);
}

QRect StyledSlider::grooveRect() const
{
    int y = LABEL_HEIGHT + (HANDLE_HEIGHT - GROOVE_HEIGHT) / 2;
    return QRect(MARGIN + HANDLE_WIDTH / 2, y, width() - MARGIN * 2 - HANDLE_WIDTH, GROOVE_HEIGHT);
}

int StyledSlider::positionFromValue(int value) const
{
    QRect groove = grooveRect();
    double ratio = double(value - m_minimum) / double(m_maximum - m_minimum);
    return groove.left() + int(ratio * groove.width());
}

int StyledSlider::valueFromPosition(int pos) const
{
    QRect groove = grooveRect();
    double ratio = double(pos - groove.left()) / double(groove.width());
    ratio = qBound(0.0, ratio, 1.0);
    return m_minimum + int(ratio * (m_maximum - m_minimum));
}

QRect StyledSlider::handleRect() const
{
    int pos = positionFromValue(m_value);
    int y = LABEL_HEIGHT;
    return QRect(pos - HANDLE_WIDTH / 2, y, HANDLE_WIDTH, HANDLE_HEIGHT);
}

void StyledSlider::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect groove = grooveRect();
    int valuePos = positionFromValue(m_value);

    // Draw two-zone bar: Red (delete) on left, Green (keep) on right
    painter.setPen(Qt::NoPen);

    // Left zone (0 to value) - Red (delete)
    QRect leftZoneRect(groove.left(), groove.top(), valuePos - groove.left(), groove.height());
    painter.setBrush(QColor(244, 67, 54));  // Red
    painter.drawRoundedRect(leftZoneRect, 2, 2);

    // Right zone (value to max) - Green (keep)
    QRect rightZoneRect(valuePos, groove.top(), groove.right() - valuePos, groove.height());
    painter.setBrush(QColor(76, 175, 80));  // Green
    painter.drawRoundedRect(rightZoneRect, 2, 2);

    // Draw zone labels ABOVE the bar
    painter.setPen(palette().color(QPalette::Text));
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    int labelY = 0;
    int labelH = LABEL_HEIGHT;

    // "Delete" label on left zone
    if (leftZoneRect.width() > 10) {
        QRect labelRect(leftZoneRect.left(), labelY, leftZoneRect.width(), labelH);
        painter.drawText(labelRect, Qt::AlignCenter | Qt::AlignBottom, "Delete");
    }

    // "Keep" label on right zone
    if (rightZoneRect.width() > 10) {
        QRect labelRect(rightZoneRect.left(), labelY, rightZoneRect.width(), labelH);
        painter.drawText(labelRect, Qt::AlignCenter | Qt::AlignBottom, "Keep");
    }

    // Draw handle
    QRect rect = handleRect();

    // Handle shadow
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawRoundedRect(rect.adjusted(1, 1, 1, 1), 3, 3);

    // Handle body
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    if (m_pressed) {
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

    // Draw value label below handle
    painter.setPen(palette().color(QPalette::Text));
    font.setPointSize(9);
    painter.setFont(font);

    QString valueText = QString::number(m_value / 100.0, 'f', 2);
    painter.drawText(QRect(rect.left() - 10, rect.bottom() + 2, 30, VALUE_HEIGHT),
                     Qt::AlignCenter, valueText);
}

void StyledSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QRect hRect = handleRect();

        if (hRect.contains(event->pos())) {
            m_pressed = true;
            m_clickOffset = event->pos().x() - positionFromValue(m_value);
        } else {
            // Click on track - move handle there
            setValue(valueFromPosition(event->pos().x()));
            m_pressed = true;
            m_clickOffset = 0;
        }
        update();
    }
}

void StyledSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressed) {
        int value = valueFromPosition(event->pos().x() - m_clickOffset);
        setValue(value);
    } else {
        // Update cursor when hovering over handle
        QRect hRect = handleRect();
        if (hRect.contains(event->pos())) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void StyledSlider::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    m_pressed = false;
    update();
}
