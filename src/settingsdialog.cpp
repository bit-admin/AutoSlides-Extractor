#include "settingsdialog.h"
#include "clitab.h"
#include "autocroptab.h"
#include "processingtab.h"
#include "postprocessingtab.h"
#include "mlclassificationtab.h"
#include <QApplication>
#include <QDir>
#include <QInputDialog>
#include <QHeaderView>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QUrl>

SettingsDialog::SettingsDialog(const AppConfig& config, ConfigManager* configManager, int initialTab, QWidget *parent)
    : QDialog(parent), m_config(config), m_originalConfig(config), m_configManager(configManager)
{
    setWindowTitle("Settings");
    setModal(true);
    resize(590, 700);

    setupUI();
    updateUIFromConfig();
    m_postProcessingTab->setExclusionList(m_configManager->loadExclusionList());

    // Set initial tab
    if (m_tabWidget && initialTab >= 0 && initialTab < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(initialTab);
    }
}

AppConfig SettingsDialog::getConfig() const
{
    return m_config;
}

QList<ExclusionEntry> SettingsDialog::getExclusionList() const
{
    return m_postProcessingTab->exclusionList();
}

void SettingsDialog::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(8);
    m_mainLayout->setContentsMargins(12, 12, 12, 12);

    // Create scroll area for the entire content
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Create tab widget inside scroll area
    m_tabWidget = new QTabWidget();
    scrollArea->setWidget(m_tabWidget);
    m_mainLayout->addWidget(scrollArea, 1);  // stretch factor 1 to take available space

    // Setup tabs
    setupProcessingTab();
    setupPostProcessingTab();
    setupMLClassificationTab();
    setupAutoCropTab();
    setupCLITab();

    // === DIALOG BUTTONS ===
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_applyButton = new QPushButton("Apply", this);
    m_restoreDefaultsButton = new QPushButton("Restore Defaults", this);
    m_buttonBox->addButton(m_applyButton, QDialogButtonBox::ApplyRole);
    m_buttonBox->addButton(m_restoreDefaultsButton, QDialogButtonBox::ResetRole);

    m_mainLayout->addWidget(m_buttonBox);

    // Connect signals
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onOkClicked);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::onCancelClicked);
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApplyClicked);
    connect(m_restoreDefaultsButton, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaultsClicked);
}

void SettingsDialog::setupProcessingTab()
{
    m_processingTab = new ProcessingTab();
    m_tabWidget->addTab(m_processingTab, "Processing");
}

void SettingsDialog::setupPostProcessingTab()
{
    m_postProcessingTab = new PostProcessingTab();
    connect(m_postProcessingTab, &PostProcessingTab::statusMessage, this, &SettingsDialog::statusMessage);
    m_tabWidget->addTab(m_postProcessingTab, "Post-Processing (pHash)");
}

void SettingsDialog::setupMLClassificationTab()
{
#ifdef ONNX_AVAILABLE
    m_mlClassificationTab = new MLClassificationTab();
    m_tabWidget->addTab(m_mlClassificationTab, "Post-Processing (ML)");
#endif
}

void SettingsDialog::setupAutoCropTab()
{
    m_autoCropTab = new AutoCropTab();
    connect(m_autoCropTab, &AutoCropTab::statusMessage, this, &SettingsDialog::statusMessage);
    m_tabWidget->addTab(m_autoCropTab, "Auto Crop");
}

void SettingsDialog::setupCLITab()
{
    m_cliTab = new CliTab();
    connect(m_cliTab, &CliTab::statusMessage, this, &SettingsDialog::statusMessage);
    m_tabWidget->addTab(m_cliTab, "CLI");
}

void SettingsDialog::updateUIFromConfig()
{
    // Processing settings (SSIM / downsampling / chunk / output)
    m_processingTab->load(m_config);

    // Post-processing settings
    m_postProcessingTab->load(m_config);

    // ML Classification settings
#ifdef ONNX_AVAILABLE
    if (m_mlClassificationTab) {
        m_mlClassificationTab->load(m_config);
    }
#endif

    // Auto-crop settings
    if (m_autoCropTab) {
        m_autoCropTab->load(m_config.autoCrop);
    }
}

void SettingsDialog::updateConfigFromUI()
{
    // Processing settings (SSIM / downsampling / chunk / output)
    m_processingTab->store(m_config);

    // Post-processing settings
    m_postProcessingTab->store(m_config);

    // ML Classification settings
#ifdef ONNX_AVAILABLE
    if (m_mlClassificationTab) {
        m_mlClassificationTab->store(m_config);
    }
#endif

    // Auto-crop settings
    if (m_autoCropTab) {
        m_autoCropTab->store(m_config.autoCrop);
    }
}

void SettingsDialog::onOkClicked()
{
    updateConfigFromUI();
    m_configManager->saveExclusionList(m_postProcessingTab->exclusionList());
    accept();
}

void SettingsDialog::onCancelClicked()
{
    m_config = m_originalConfig; // Restore original config
    reject();
}

void SettingsDialog::onApplyClicked()
{
    updateConfigFromUI();
    m_configManager->saveExclusionList(m_postProcessingTab->exclusionList());
    // Don't close dialog, just apply changes
}

void SettingsDialog::onRestoreDefaultsClicked()
{
    // Create default config
    AppConfig defaultConfig;
    m_config = defaultConfig;

    // Restore default exclusion list
    m_postProcessingTab->setExclusionList(PostProcessor::getDefaultExclusionList());

    // All tabs' defaults are applied via updateUIFromConfig() below
    // (m_config was already reset to defaults above).
    updateUIFromConfig();

    // Save the defaults
    m_configManager->saveConfig(m_config);
    m_configManager->saveExclusionList(m_postProcessingTab->exclusionList());
}
