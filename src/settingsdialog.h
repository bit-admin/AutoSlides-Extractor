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
#include <QTabWidget>
#include <QTableWidget>
#include <QFileDialog>
#include <QLineEdit>
#include <QSlider>
#include <QScrollArea>
#include <QMap>
#include <QTextEdit>
#include "configmanager.h"
#include "postprocessor.h"
#include "rangeslider.h"
#include "styledslider.h"

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppConfig& config, ConfigManager* configManager, int initialTab = 0, QWidget *parent = nullptr);

    AppConfig getConfig() const;
    QList<ExclusionEntry> getExclusionList() const { return m_exclusionList; }

signals:
    void statusMessage(const QString& message);

private slots:
    void onSSIMPresetChanged();
    void onDownsamplingToggled();
    void onDownsamplePresetChanged();
    void onOkClicked();
    void onCancelClicked();
    void onApplyClicked();
    void onRestoreDefaultsClicked();

    // Post-processing slots
    void onAddFromImageClicked();
    void onManualInputClicked();
    void onDeleteExclusionClicked();

    // ML Classification slots
#ifdef ONNX_AVAILABLE
    void onTestMLClassificationClicked();
#endif

private:
    void setupUI();
    void setupProcessingTab();
    void setupPostProcessingTab();
    void setupMLClassificationTab();
    void updateUIFromConfig();
    void updateConfigFromUI();
    void updateDownsampleDimensionsFromPreset();
    void updateDownsamplePresetFromDimensions();
    void updateExclusionTable();

    AppConfig m_config;
    AppConfig m_originalConfig;
    ConfigManager* m_configManager;
    QList<ExclusionEntry> m_exclusionList;

    // UI elements
    QVBoxLayout* m_mainLayout;
    QTabWidget* m_tabWidget;

    // Processing Tab
    QWidget* m_processingTab;

    // SSIM Settings Group
    QGroupBox* m_ssimGroup;
    QComboBox* m_ssimPresetCombo;
    QDoubleSpinBox* m_customSSIMSpinBox;
    QLabel* m_ssimHelpLabel;

    // Chunk Size Settings Group
    QGroupBox* m_chunkGroup;
    QSpinBox* m_chunkSizeSpinBox;
    QLabel* m_chunkHelpLabel;

    // Output Settings Group
    QGroupBox* m_outputGroup;
    QSpinBox* m_jpegQualitySpinBox;
    QLabel* m_outputHelpLabel;

    // Downsampling Settings Group
    QGroupBox* m_downsamplingGroup;
    QCheckBox* m_enableDownsamplingCheckBox;
    QComboBox* m_downsamplePresetCombo;
    QSpinBox* m_downsampleWidthSpinBox;
    QSpinBox* m_downsampleHeightSpinBox;
    QLabel* m_downsamplingHelpLabel;

    // Post-Processing Tab (pHash)
    QWidget* m_postProcessingTab;
    QSpinBox* m_hammingThresholdSpinBox;
    QTableWidget* m_exclusionTable;
    QPushButton* m_addFromImageButton;
    QPushButton* m_manualInputButton;

    // ML Classification Tab
#ifdef ONNX_AVAILABLE
    QWidget* m_mlClassificationTab;
    QCheckBox* m_mlDeleteMaybeSlidesCheckBox;
    QLineEdit* m_mlModelPathEdit;
    QPushButton* m_mlBrowseModelButton;
    QPushButton* m_mlUseDefaultModelButton;
    QPushButton* m_mlTestButton;
    QTextEdit* m_mlTestResultText;

    // Range sliders for 2-stage thresholds
    RangeSlider* m_mlNotSlideRangeSlider;
    RangeSlider* m_mlMaybeSlideRangeSlider;

    // Shared slide max threshold for medium confidence zone
    StyledSlider* m_mlSlideMaxThresholdSlider;
#endif

    // Dialog buttons
    QDialogButtonBox* m_buttonBox;
    QPushButton* m_applyButton;
    QPushButton* m_restoreDefaultsButton;
};

#endif // SETTINGSDIALOG_H