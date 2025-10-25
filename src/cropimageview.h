#ifndef CROPIMAGEVIEW_H
#define CROPIMAGEVIEW_H

#include <QWidget>
#include <QPixmap>
#include <QRect>

/**
 * @brief Single-image viewer with optional rubber-band crop selection.
 *
 * Displays a QPixmap scaled-to-fit (preserving aspect ratio) inside the
 * widget, with letterboxing as needed. When `selectionMode` is enabled,
 * click-and-drag draws a crop rectangle in widget coordinates that the
 * widget converts to original-image pixel coordinates on demand.
 *
 * The widget tracks two coordinate spaces:
 *   - widget pixels: for paint and mouse events
 *   - image pixels: original-image coordinates (the public selection API)
 */
class CropImageView : public QWidget
{
    Q_OBJECT

public:
    explicit CropImageView(QWidget* parent = nullptr);

    /** Replaces the displayed image. Clears any selection. */
    void setImage(const QPixmap& pixmap);

    /** When true, mouse drags draw / replace a selection rectangle. */
    void setSelectionMode(bool enabled);
    bool selectionMode() const { return m_selectionMode; }

    /** Sets an initial selection in image-pixel coordinates (for recrop). */
    void setInitialSelection(const QRect& imageRect);

    /** Current selection in image-pixel coordinates, or invalid rect if none. */
    QRect selectionImageRect() const;

    /** True if a usable selection exists (>=1 px in each dimension). */
    bool hasSelection() const;

    void clearSelection();

    QSize sizeHint() const override { return QSize(800, 600); }
    QSize minimumSizeHint() const override { return QSize(320, 240); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect imageDisplayRect() const;          // where the image is drawn, in widget coords
    QRect widgetRectToImageRect(const QRect& wr) const;
    QRect imageRectToWidgetRect(const QRect& ir) const;
    QRect normalizedClampedWidgetRect(const QPoint& a, const QPoint& b) const;

    QPixmap m_pixmap;          // original (full-size) pixmap
    bool m_selectionMode = false;

    bool m_dragging = false;
    QPoint m_dragStart;        // widget coords
    QPoint m_dragCurrent;      // widget coords

    // Selection in IMAGE pixel coordinates (canonical). Invalid means no selection.
    QRect m_selectionImageRect;
};

#endif // CROPIMAGEVIEW_H
