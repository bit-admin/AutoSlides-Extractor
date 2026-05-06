#ifndef REVIEWITEMWIDGET_H
#define REVIEWITEMWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include "trashentry.h"

/**
 * @brief Unified slide-item widget used by ReviewSlidesDialog.
 *
 * Renders either an extracted slide (live `slide_*.jpg` on disk) or a removed
 * slide (entry in `.extractorTrash/`). Removed items get a red border so users
 * can distinguish them at a glance.
 */
class ReviewItemWidget : public QWidget
{
    Q_OBJECT

public:
    enum State { Extracted, Removed };

    /**
     * @brief Removed-state constructor.
     */
    ReviewItemWidget(const TrashEntry& entry,
                     const QString& baseOutputDir,
                     QWidget* parent = nullptr);

    /**
     * @brief Extracted-state constructor.
     */
    ReviewItemWidget(const QString& imagePath,
                     QWidget* parent = nullptr);

    State state() const { return m_state; }

    bool isChecked() const;
    void setChecked(bool checked);

    QString getImagePath() const { return m_imagePath; }
    QString getTrashedFilename() const { return m_trashedFilename; }
    const TrashEntry& getEntry() const { return m_entry; }

    void setThumbnailWidth(int width);
    int thumbnailWidth() const { return m_thumbnailWidth; }

    /** Show or hide the "Cropped" badge and reload the thumbnail from disk. */
    void setCropped(bool cropped);
    bool isCropped() const { return m_cropped; }

    static constexpr int kDefaultThumbnailWidth = 280;
    static constexpr int kMinThumbnailWidth = 160;
    static constexpr int kMaxThumbnailWidth = 480;

signals:
    void selectionChanged();
    void viewClicked();
    void setBaselineClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void buildLayout();
    void applyThumbnail();

    State m_state;
    TrashEntry m_entry;
    QString m_imagePath;
    QString m_trashedFilename;

    QPixmap m_originalPixmap;
    int m_thumbnailWidth;

    QCheckBox* m_checkbox;
    QPushButton* m_viewButton = nullptr;
    QPushButton* m_setBaselineButton = nullptr;
    QLabel* m_thumbnailLabel;
    QLabel* m_nameLabel;
    QLabel* m_methodLabel;
    QLabel* m_cropLabel = nullptr;
    bool m_cropped = false;
};

#endif // REVIEWITEMWIDGET_H
