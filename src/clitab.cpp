#include "clitab.h"
#include "cliinstaller.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QClipboard>

CliTab::CliTab(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* tabLayout = new QVBoxLayout(this);
    tabLayout->setSpacing(12);
    tabLayout->setContentsMargins(12, 12, 12, 12);

    // === INSTALLATION GROUP ===
    QGroupBox* installGroup = new QGroupBox("Command-Line Tool Installation", this);
    QVBoxLayout* installLayout = new QVBoxLayout(installGroup);
    installLayout->setContentsMargins(12, 12, 12, 12);
    installLayout->setSpacing(8);

    QLabel* installHelpLabel = new QLabel(
        "Install the 'SlidesExtractor' command so it can be used from a terminal "
        "to run extractions without opening this app. The wrapper points at the "
        "currently-running app — reinstall after moving the application.", this);
    installHelpLabel->setWordWrap(true);
    installHelpLabel->setStyleSheet("color: #666; font-size: 11px;");
    installLayout->addWidget(installHelpLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    installLayout->addWidget(m_statusLabel);

    m_pathHintLabel = new QLabel(this);
    m_pathHintLabel->setWordWrap(true);
    m_pathHintLabel->setStyleSheet("color: #b35c00; font-size: 11px;");
    m_pathHintLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathHintLabel->setVisible(false);
    installLayout->addWidget(m_pathHintLabel);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    m_installButton = new QPushButton("Install CLI", this);
    m_uninstallButton = new QPushButton("Uninstall CLI", this);
    m_copyPathLineButton = new QPushButton("Copy export line", this);
    m_copyPathLineButton->setVisible(false);
    buttonRow->addWidget(m_installButton);
    buttonRow->addWidget(m_uninstallButton);
    buttonRow->addWidget(m_copyPathLineButton);
    buttonRow->addStretch();
    installLayout->addLayout(buttonRow);

    tabLayout->addWidget(installGroup);

    // === USAGE GROUP ===
    QGroupBox* usageGroup = new QGroupBox("Usage Examples", this);
    QVBoxLayout* usageLayout = new QVBoxLayout(usageGroup);
    usageLayout->setContentsMargins(12, 12, 12, 12);
    usageLayout->setSpacing(8);

    QLabel* usageHelpLabel = new QLabel(
        "Only --video and --output are required. Other flags default to the values "
        "saved in this Settings dialog. Removed slides go to the application's review-able "
        "trash, so you can later open this app and use Slides Review.", this);
    usageHelpLabel->setWordWrap(true);
    usageHelpLabel->setStyleSheet("color: #666; font-size: 11px;");
    usageLayout->addWidget(usageHelpLabel);

    m_exampleText = new QPlainTextEdit(this);
    m_exampleText->setReadOnly(true);
    m_exampleText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_exampleText->setMinimumHeight(180);
    m_exampleText->setPlainText(
        "# Basic extraction (defaults from this Settings dialog)\n"
        "SlidesExtractor --video lecture.mp4 --output ~/Slides\n"
        "\n"
        "# All three post-processing phases (uses the saved exclusion list from this dialog)\n"
        "SlidesExtractor --video lecture.mp4 --output ~/Slides \\\n"
        "    --phash-redundant \\\n"
        "    --phash-exclusion \\\n"
        "    --ml-classify\n"
        "\n"
        "# Phase 2 with an ad-hoc exclusion list (not saved back to GUI)\n"
        "SlidesExtractor --video lecture.mp4 --output ~/Slides \\\n"
        "    --phash-exclusion-hashes <64-char-hex>,<64-char-hex>\n"
        "\n"
        "# Override SSIM threshold and JPEG quality for one run\n"
        "SlidesExtractor --video lecture.mp4 --output ~/Slides \\\n"
        "    --ssim-threshold 0.999 --jpeg-quality 80");
    usageLayout->addWidget(m_exampleText);

    QHBoxLayout* copyRow = new QHBoxLayout();
    copyRow->addStretch();
    m_copyExampleButton = new QPushButton("Copy Example", this);
    copyRow->addWidget(m_copyExampleButton);
    usageLayout->addLayout(copyRow);

    tabLayout->addWidget(usageGroup);
    tabLayout->addStretch();

    connect(m_installButton, &QPushButton::clicked, this, &CliTab::onInstall);
    connect(m_uninstallButton, &QPushButton::clicked, this, &CliTab::onUninstall);
    connect(m_copyExampleButton, &QPushButton::clicked, this, &CliTab::onCopyExample);
    connect(m_copyPathLineButton, &QPushButton::clicked, this, &CliTab::onCopyPathLine);

    refreshStatus();
}

void CliTab::refreshStatus()
{
    if (!m_statusLabel) return;

    CliInstaller::State state = CliInstaller::installState();
    QString location = CliInstaller::installLocation();

    switch (state) {
    case CliInstaller::State::NotInstalled:
        m_statusLabel->setText(QString("<b>Status:</b> Not installed.<br><b>Will install at:</b> %1").arg(location));
        m_installButton->setText("Install CLI");
        m_installButton->setEnabled(true);
        m_installButton->setVisible(true);
        m_uninstallButton->setEnabled(false);
        break;
    case CliInstaller::State::Installed:
        m_statusLabel->setText(QString("<b>Status:</b> Installed.<br><b>Location:</b> %1").arg(location));
        m_installButton->setText("Reinstall");
        m_installButton->setEnabled(true);
        m_installButton->setVisible(true);
        m_uninstallButton->setEnabled(true);
        break;
    case CliInstaller::State::Stale:
        m_statusLabel->setText(QString(
            "<b>Status:</b> Out of date — the installed wrapper points at a different app location.<br>"
            "<b>Location:</b> %1<br>Click Reinstall to update it.").arg(location));
        m_installButton->setText("Reinstall");
        m_installButton->setEnabled(true);
        m_installButton->setVisible(true);
        m_uninstallButton->setEnabled(true);
        break;
    }

    bool inPath = CliInstaller::isInstallDirInPath();
    bool showHint = (state != CliInstaller::State::NotInstalled) && !inPath;
    if (showHint) {
        QString hintHtml = CliInstaller::pathHint().toHtmlEscaped();
        hintHtml.replace('\n', QStringLiteral("<br>"));
        m_pathHintLabel->setText("<b>Note:</b> the install directory does not appear on your PATH.<br>" +
                                 hintHtml);
    }
    m_pathHintLabel->setVisible(showHint);
    m_copyPathLineButton->setVisible(showHint && !CliInstaller::pathExportLine().isEmpty());
}

void CliTab::onCopyPathLine()
{
    QString line = CliInstaller::pathExportLine();
    if (line.isEmpty()) return;
    QGuiApplication::clipboard()->setText(line);
    emit statusMessage("Copied PATH export line to clipboard.");
}

void CliTab::onInstall()
{
    QString err;
    if (!CliInstaller::install(&err)) {
        emit statusMessage("Install CLI failed: " + err);
        refreshStatus();
        return;
    }

    QString message = "Installed CLI at " + CliInstaller::installLocation() + ".";
    if (!CliInstaller::isInstallDirInPath()) {
        message += " " + CliInstaller::pathHint().replace('\n', ' ');
    } else {
#ifdef Q_OS_WIN
        message += " Open a new terminal to use it.";
#else
        message += " Available in any new terminal.";
#endif
    }
    emit statusMessage(message);
    refreshStatus();
}

void CliTab::onUninstall()
{
    // No confirmation dialog: project convention avoids QMessageBox (see CLAUDE.md).
    QString err;
    if (!CliInstaller::uninstall(&err)) {
        emit statusMessage("Uninstall CLI failed: " + err);
    } else {
        emit statusMessage("Uninstalled SlidesExtractor CLI.");
    }
    refreshStatus();
}

void CliTab::onCopyExample()
{
    if (!m_exampleText) return;
    QString text = m_exampleText->textCursor().selectedText();
    if (text.isEmpty()) {
        text = m_exampleText->toPlainText();
    } else {
        // QTextCursor::selectedText uses U+2029 paragraph separators; normalize to '\n'
        text.replace(QChar(0x2029), QLatin1Char('\n'));
    }
    QGuiApplication::clipboard()->setText(text);
    emit statusMessage("CLI example copied to clipboard.");
}
