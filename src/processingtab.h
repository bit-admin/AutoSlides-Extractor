#ifndef PROCESSINGTAB_H
#define PROCESSINGTAB_H

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
struct AppConfig;

/**
 * @brief Settings "Processing" tab: SSIM threshold preset, downsampling,
 *        memory chunk size, and JPEG output quality.
 *
 * Self-contained — owns its widgets and the preset/downsample interaction
 * slots, with load()/store() reading and writing only its AppConfig fields.
 * Extracted from SettingsDialog.
 */
class ProcessingTab : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessingTab(QWidget* parent = nullptr);

    void load(const AppConfig& config);
    void store(AppConfig& config) const;

private slots:
    void onSSIMPresetChanged();
    void onDownsamplingToggled();
    void onDownsamplePresetChanged();
    void updateDownsampleDimensionsFromPreset();
    void updateDownsamplePresetFromDimensions();

private:
    QComboBox* m_ssimPresetCombo = nullptr;
    QDoubleSpinBox* m_customSSIMSpinBox = nullptr;
    QSpinBox* m_chunkSizeSpinBox = nullptr;
    QSpinBox* m_jpegQualitySpinBox = nullptr;
    QCheckBox* m_enableDownsamplingCheckBox = nullptr;
    QComboBox* m_downsamplePresetCombo = nullptr;
    QSpinBox* m_downsampleWidthSpinBox = nullptr;
    QSpinBox* m_downsampleHeightSpinBox = nullptr;
};

#endif // PROCESSINGTAB_H
