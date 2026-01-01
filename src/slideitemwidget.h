#ifndef SLIDEITEMWIDGET_H
#define SLIDEITEMWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

/**
 * @brief Custom widget for displaying a single slide image with checkbox and thumbnail
 *
 * Used in PdfMakerDialog for displaying slide images in a grid layout.
 */
class SlideItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SlideItemWidget(const QString& imagePath,
                            QWidget *parent = nullptr);

    /**
     * @brief Check if this item is selected
     */
    bool isChecked() const;

    /**
     * @brief Set the checked state
     */
    void setChecked(bool checked);

    /**
     * @brief Get the full image path
     */
    QString getImagePath() const { return m_imagePath; }

    /**
     * @brief Get the image filename (without path)
     */
    QString getImageName() const { return m_imageName; }

signals:
    /**
     * @brief Emitted when selection state changes
     */
    void selectionChanged();

protected:
    /**
     * @brief Handle mouse press to toggle selection
     */
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString m_imagePath;
    QString m_imageName;

    QCheckBox* m_checkbox;
    QLabel* m_thumbnailLabel;
    QLabel* m_nameLabel;
};

#endif // SLIDEITEMWIDGET_H
