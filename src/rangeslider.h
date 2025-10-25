#ifndef RANGESLIDER_H
#define RANGESLIDER_H

#include <QWidget>

class RangeSlider : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int minimum READ minimum WRITE setMinimum)
    Q_PROPERTY(int maximum READ maximum WRITE setMaximum)
    Q_PROPERTY(int lowerValue READ lowerValue WRITE setLowerValue NOTIFY lowerValueChanged)
    Q_PROPERTY(int upperValue READ upperValue WRITE setUpperValue NOTIFY upperValueChanged)

public:
    explicit RangeSlider(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = nullptr);

    // Range
    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    void setMinimum(int min);
    void setMaximum(int max);
    void setRange(int min, int max);

    // Values
    int lowerValue() const { return m_lowerValue; }
    int upperValue() const { return m_upperValue; }

    // Zone labels (optional, displayed in the colored zones)
    void setZoneLabels(const QString& lowZone, const QString& midZone, const QString& highZone);

    // Zone colors
    void setZoneColors(const QColor& lowZone, const QColor& midZone, const QColor& highZone);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

public slots:
    void setLowerValue(int value);
    void setUpperValue(int value);

signals:
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
    void rangeChanged(int lower, int upper);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum Handle { NoHandle, LowerHandle, UpperHandle };

    int valueFromPosition(int pos) const;
    int positionFromValue(int value) const;
    QRect handleRect(Handle handle) const;
    QRect grooveRect() const;

    Qt::Orientation m_orientation;
    int m_minimum;
    int m_maximum;
    int m_lowerValue;
    int m_upperValue;

    Handle m_pressedHandle;
    int m_clickOffset;

    // Zone labels
    QString m_lowZoneLabel;
    QString m_midZoneLabel;
    QString m_highZoneLabel;

    // Zone colors
    QColor m_lowZoneColor;
    QColor m_midZoneColor;
    QColor m_highZoneColor;

    static const int HANDLE_WIDTH = 8;
    static const int HANDLE_HEIGHT = 18;
    static const int GROOVE_HEIGHT = 4;
    static const int MARGIN = 6;
    static const int LABEL_HEIGHT = 16;  // Height for zone labels above the bar
    static const int VALUE_HEIGHT = 14;  // Height for value labels below the bar
};

#endif // RANGESLIDER_H
