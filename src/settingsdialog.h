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

class QPlainTextEdit;
class CliTab;
class AutoCropTab;
class ProcessingTab;
class PostProcessingTab;
class MLClassificationTab;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppConfig& config, ConfigManager* configManager, int initialTab = 0, QWidget *parent = nullptr);

    AppConfig getConfig() const;
    QList<ExclusionEntry> getExclusionList() const;

signals:
    void statusMessage(const QString& message);

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onApplyClicked();
    void onRestoreDefaultsClicked();

private:
    void setupUI();
    void setupProcessingTab();
    void setupPostProcessingTab();
    void setupMLClassificationTab();
    void setupAutoCropTab();
    void setupCLITab();
    void updateUIFromConfig();
    void updateConfigFromUI();

    AppConfig m_config;
    AppConfig m_originalConfig;
    ConfigManager* m_configManager;

    // UI elements
    QVBoxLayout* m_mainLayout;
    QTabWidget* m_tabWidget;

    // Processing Tab (self-contained widget)
    ProcessingTab* m_processingTab = nullptr;

    // Post-Processing Tab (self-contained widget)
    PostProcessingTab* m_postProcessingTab = nullptr;

    // ML Classification Tab (self-contained widget; only created when ONNX_AVAILABLE)
#ifdef ONNX_AVAILABLE
    MLClassificationTab* m_mlClassificationTab = nullptr;
#endif

    // Auto Crop Tab (self-contained widget)
    AutoCropTab* m_autoCropTab = nullptr;

    // CLI Tab (self-contained widget)
    CliTab* m_cliTab = nullptr;

    // Dialog buttons
    QDialogButtonBox* m_buttonBox;
    QPushButton* m_applyButton;
    QPushButton* m_restoreDefaultsButton;
};

#endif // SETTINGSDIALOG_H
