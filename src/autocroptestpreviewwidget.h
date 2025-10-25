#ifndef AUTOCROPTESTPREVIEWWIDGET_H
#define AUTOCROPTESTPREVIEWWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <functional>

class QPaintEvent;
class QMouseEvent;
class QPainter;

/**
 * @brief Preview pane for the Settings → Auto Crop "Test" panel.
 *
 * Renders a test image scaled to fit, overlays either the detected bounding
 * box (red rectangle) or a red cross when no slide was detected, and lets the
 * user click to open a saved annotated copy via an injected open handler.
 * Extracted verbatim from settingsdialog.cpp.
 */
class AutoCropTestPreviewWidget : public QWidget
{
public:
    explicit AutoCropTestPreviewWidget(QWidget* parent = nullptr);

    void setOpenHandler(std::function<void(const QString&)> handler);
    void setPreview(const QPixmap& pixmap, const QString& imagePath, const QRect& bbox, bool showCross);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void drawOverlay(QPainter& painter, const QRect& targetRect) const;
    QString annotatedPreviewPath();
    bool saveAnnotatedPreview(const QString& outPath) const;
    QRect imageDisplayRect() const;
    QRect imageRectToWidgetRect(const QRect& imageRect) const;

    QPixmap m_pixmap;
    QString m_imagePath;
    QString m_annotatedPath;
    QRect m_bbox;
    std::function<void(const QString&)> m_openHandler;
    bool m_showCross = false;
};

#endif // AUTOCROPTESTPREVIEWWIDGET_H
