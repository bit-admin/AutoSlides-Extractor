#ifndef AUTOCROPTAB_H
#define AUTOCROPTAB_H

#include <QWidget>
#include "configmanager.h"   // AutoCropConfig, AutoCropMode

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
class AutoCropTestPreviewWidget;

/**
 * @brief Settings "Auto Crop" tab: detection mode, Canny/YOLO parameters, and
 *        an interactive test panel that runs AutoCropDetector on a chosen image.
 *
 * Owns its slice of AutoCropConfig (load/store) and the non-UI pipeline
 * parameters carried through from the loaded config. Extracted from
 * SettingsDialog. Feedback is emitted via statusMessage (no QMessageBox).
 */
class AutoCropTab : public QWidget
{
    Q_OBJECT

public:
    explicit AutoCropTab(QWidget* parent = nullptr);

    // Populate widgets from a config (also retains the full config as the base
    // for test runs, so hardcoded Canny/YOLO params survive round-trips).
    void load(const AutoCropConfig& config);
    // Write the user-editable fields back into config.
    void store(AutoCropConfig& config) const;

signals:
    void statusMessage(const QString& message);

private:
    AutoCropConfig currentTestConfig(AutoCropMode forcedMode) const;
    void runTest(AutoCropMode forcedMode);

    AutoCropConfig m_baseConfig;   // last loaded config (source of non-UI params)

    QComboBox* m_modeCombo = nullptr;
    QDoubleSpinBox* m_aspectToleranceSpin = nullptr;
    QDoubleSpinBox* m_yoloConfSpin = nullptr;
    QLineEdit* m_yoloModelPathEdit = nullptr;
    QPushButton* m_yoloBrowseButton = nullptr;
    QPushButton* m_yoloUseDefaultButton = nullptr;
    QLabel* m_modelInfoLabel = nullptr;
    QPushButton* m_testCannyButton = nullptr;
    QPushButton* m_testYoloButton = nullptr;
    QLabel* m_testResultLabel = nullptr;
    AutoCropTestPreviewWidget* m_testPreview = nullptr;
};

#endif // AUTOCROPTAB_H
