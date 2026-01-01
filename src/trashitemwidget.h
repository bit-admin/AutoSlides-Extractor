#ifndef TRASHITEMWIDGET_H
#define TRASHITEMWIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include "trashentry.h"

/**
 * @brief Custom widget for displaying a single trash item with checkbox and thumbnail
 */
class TrashItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrashItemWidget(const TrashEntry& entry,
                            const QString& baseOutputDir,
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
     * @brief Get the trashed filename
     */
    QString getTrashedFilename() const { return m_trashedFilename; }

    /**
     * @brief Get the trash entry
     */
    const TrashEntry& getEntry() const { return m_entry; }

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
    TrashEntry m_entry;
    QString m_trashedFilename;

    QCheckBox* m_checkbox;
    QLabel* m_thumbnailLabel;
    QLabel* m_nameLabel;
    QLabel* m_methodLabel;
};

#endif // TRASHITEMWIDGET_H
