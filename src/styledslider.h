#ifndef STYLEDSLIDER_H
#define STYLEDSLIDER_H

#include <QWidget>

class StyledSlider : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int minimum READ minimum WRITE setMinimum)
    Q_PROPERTY(int maximum READ maximum WRITE setMaximum)
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)

public:
    explicit StyledSlider(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = nullptr);

    // Range
    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    void setMinimum(int min);
    void setMaximum(int max);
    void setRange(int min, int max);

    // Value
    int value() const { return m_value; }

    // Label above the bar
    void setLabel(const QString& label);

    // Bar color
    void setBarColor(const QColor& color);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

public slots:
    void setValue(int value);

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int valueFromPosition(int pos) const;
    int positionFromValue(int value) const;
    QRect handleRect() const;
    QRect grooveRect() const;

    Qt::Orientation m_orientation;
    int m_minimum;
    int m_maximum;
    int m_value;

    bool m_pressed;
    int m_clickOffset;

    QString m_label;
    QColor m_barColor;

    static const int HANDLE_WIDTH = 8;
    static const int HANDLE_HEIGHT = 18;
    static const int GROOVE_HEIGHT = 4;
    static const int MARGIN = 6;
    static const int LABEL_HEIGHT = 16;
    static const int VALUE_HEIGHT = 14;
};

#endif // STYLEDSLIDER_H
