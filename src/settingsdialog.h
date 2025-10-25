#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include "configmanager.h"

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppConfig& config, QWidget *parent = nullptr);

    AppConfig getConfig() const;

private slots:
    void onSSIMPresetChanged();
    void onDownsamplingToggled();
    void onDownsamplePresetChanged();
    void onOkClicked();
    void onCancelClicked();
    void onApplyClicked();

private:
    void setupUI();
    void updateUIFromConfig();
    void updateConfigFromUI();
    void updateDownsampleDimensionsFromPreset();
    void updateDownsamplePresetFromDimensions();

    AppConfig m_config;
    AppConfig m_originalConfig;

    // UI elements
    QVBoxLayout* m_mainLayout;

    // SSIM Settings Group
    QGroupBox* m_ssimGroup;
    QComboBox* m_ssimPresetCombo;
    QDoubleSpinBox* m_customSSIMSpinBox;
    QLabel* m_ssimHelpLabel;

    // Chunk Size Settings Group
    QGroupBox* m_chunkGroup;
    QSpinBox* m_chunkSizeSpinBox;
    QLabel* m_chunkHelpLabel;

    // Downsampling Settings Group
    QGroupBox* m_downsamplingGroup;
    QCheckBox* m_enableDownsamplingCheckBox;
    QComboBox* m_downsamplePresetCombo;
    QSpinBox* m_downsampleWidthSpinBox;
    QSpinBox* m_downsampleHeightSpinBox;
    QLabel* m_downsamplingHelpLabel;

    // Dialog buttons
    QDialogButtonBox* m_buttonBox;
    QPushButton* m_applyButton;
};

#endif // SETTINGSDIALOG_H