#ifndef CLITAB_H
#define CLITAB_H

#include <QWidget>

class QLabel;
class QPushButton;
class QPlainTextEdit;

/**
 * @brief Settings "CLI" tab: install/uninstall the SlidesExtractor command-line
 *        wrapper and show usage examples.
 *
 * Self-contained — does not read or write AppConfig. Status/feedback is emitted
 * via statusMessage() (project convention; no QMessageBox). Extracted from
 * SettingsDialog.
 */
class CliTab : public QWidget
{
    Q_OBJECT

public:
    explicit CliTab(QWidget* parent = nullptr);

signals:
    void statusMessage(const QString& message);

private slots:
    void refreshStatus();
    void onInstall();
    void onUninstall();
    void onCopyExample();
    void onCopyPathLine();

private:
    QLabel* m_statusLabel = nullptr;
    QLabel* m_pathHintLabel = nullptr;
    QPushButton* m_copyPathLineButton = nullptr;
    QPushButton* m_installButton = nullptr;
    QPushButton* m_uninstallButton = nullptr;
    QPlainTextEdit* m_exampleText = nullptr;
    QPushButton* m_copyExampleButton = nullptr;
};

#endif // CLITAB_H
