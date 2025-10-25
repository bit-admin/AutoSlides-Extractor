#ifndef POSTPROCESSINGTAB_H
#define POSTPROCESSINGTAB_H

#include <QWidget>
#include <QList>
#include "postprocessor.h"   // ExclusionEntry

class QSpinBox;
class QTableWidget;
class QPushButton;
struct AppConfig;

/**
 * @brief Settings "Post-Processing (pHash)" tab: Hamming distance threshold and
 *        the editable pHash exclusion list.
 *
 * Owns the exclusion list (load/edit/expose); the dialog persists it via
 * ConfigManager. Hamming threshold round-trips through load()/store(). Errors
 * surface via statusMessage (no QMessageBox). Extracted from SettingsDialog.
 */
class PostProcessingTab : public QWidget
{
    Q_OBJECT

public:
    explicit PostProcessingTab(QWidget* parent = nullptr);

    void load(const AppConfig& config);        // hamming threshold
    void store(AppConfig& config) const;       // hamming threshold

    void setExclusionList(const QList<ExclusionEntry>& list);
    QList<ExclusionEntry> exclusionList() const { return m_exclusionList; }

signals:
    void statusMessage(const QString& message);

private slots:
    void onAddFromImage();
    void onManualInput();

private:
    void rebuildTable();

    QSpinBox* m_hammingThresholdSpinBox = nullptr;
    QTableWidget* m_exclusionTable = nullptr;
    QPushButton* m_addFromImageButton = nullptr;
    QPushButton* m_manualInputButton = nullptr;
    QList<ExclusionEntry> m_exclusionList;
};

#endif // POSTPROCESSINGTAB_H
