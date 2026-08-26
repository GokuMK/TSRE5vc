#ifndef TSRE_SETTINGSDIALOG_H
#define TSRE_SETTINGSDIALOG_H

#include <settings/SettingsTypes.h>

#include <QDialog>
#include <QVector>
#include <functional>

class QAction;
class QComboBox;
class QLineEdit;
class QTabWidget;
class QLabel;
class QVBoxLayout;
class SettingsManager;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsManager *manager, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void rebuild();
    void applyFilters();
    void updateSearchPlacement();
    void saveProfile();
    void reloadProfile();
    void loadProfile();
    void saveProfileAs();
    void duplicateProfile();
    void switchProfile(int index);
    void openProfileFolder();
    void addCustomSetting();
    void showRawDocument();

private:
    struct Editor {
        QString key;
        SettingType type = SettingType::String;
        QWidget *row = nullptr;
        QWidget *sectionHeading = nullptr;
        QWidget *sectionWidget = nullptr;
        QString sectionId;
        QVBoxLayout *homeLayout = nullptr;
        int homeOrder = 0;
        std::function<QVariant()> value;
        bool secret = false;
        QString secretReference;
    };

    SettingsManager *m_manager;
    QString m_usedProfileFile;
    QLineEdit *m_search;
    QAction *m_showUnsupported;
    QAction *m_showAdvanced;
    QTabWidget *m_tabs;
    QComboBox *m_profileName;
    QLineEdit *m_profilePath;
    QAction *m_duplicateProfileAction;
    QLabel *m_statusLabel;
    QWidget *m_resultsTab = nullptr;
    QVBoxLayout *m_resultsLayout = nullptr;
    QVector<Editor> m_editors;

    QWidget *createEditor(const QJsonObject &setting, Editor *editor);
    void editSettingMetadata(const QString &key);
    bool exchangeJson(const QString &title, const QJsonObject &initial,
                      const std::function<bool(const QJsonObject &, QString *)> &apply);
    bool applyEditors(QString *error);
    bool hasEditorChanges() const;
    bool confirmDiscardChanges();
    void showError(const QString &title, const QString &message);
};

#endif
