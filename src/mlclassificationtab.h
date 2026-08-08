#ifndef MLCLASSIFICATIONTAB_H
#define MLCLASSIFICATIONTAB_H

#include <QWidget>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class RangeSlider;
class StyledSlider;
struct AppConfig;

/**
 * @brief Settings "Post-Processing (ML)" tab: model path, 2-stage confidence
 *        thresholds, and an interactive single-image classification test.
 *
 * Compiles unconditionally (depends only on always-built MLClassifier), but the
 * dialog only creates/adds it when ONNX_AVAILABLE — matching the original
 * behaviour of hiding ML config when no runtime is present. load()/store()
 * round-trip the ML fields of AppConfig. Extracted from SettingsDialog.
 */
class MLClassificationTab : public QWidget
{
    Q_OBJECT

public:
    explicit MLClassificationTab(QWidget* parent = nullptr);

    void load(const AppConfig& config);
    void store(AppConfig& config) const;

private slots:
    void onBrowseModel();
    void onUseDefaultModel();
    void onTest();
    void updateMaybeSlideOptionEnabled();

private:
    QCheckBox* m_deleteMaybeSlidesCheckBox = nullptr;
    QCheckBox* m_autoCropMaybeSlidesCheckBox = nullptr;
    QCheckBox* m_postCropDedupCheckBox = nullptr;
    QLineEdit* m_modelPathEdit = nullptr;
    QPushButton* m_browseModelButton = nullptr;
    QPushButton* m_useDefaultModelButton = nullptr;
    QPushButton* m_testButton = nullptr;
    QTextEdit* m_testResultText = nullptr;
    RangeSlider* m_notSlideRangeSlider = nullptr;
    RangeSlider* m_maybeSlideRangeSlider = nullptr;
    StyledSlider* m_slideMaxThresholdSlider = nullptr;
};

#endif // MLCLASSIFICATIONTAB_H
