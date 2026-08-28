#include <settings/ui/SettingsDialog.h>

#include <settings/SettingsManager.h>
#include <settings/SettingsProfile.h>
#include <tsre/Game.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString settingTitle(const QJsonObject &setting) {
    const QString name = setting.value("name").toString().trimmed();
    return name.isEmpty() ? setting.value("key").toString() : name;
}

QJsonObject parseObject(const QString &text, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        if (error) *error = parseError.error == QJsonParseError::NoError
                ? QString("JSON must contain one object.") : parseError.errorString();
        return QJsonObject();
    }
    return parsed.object();
}

QString jsonValueText(const QJsonValue &value) {
    QByteArray text = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    if (text.size() >= 2) {
        text.remove(0, 1);
        text.chop(1);
    }
    return QString::fromUtf8(text);
}

bool parseKeyValueText(const QString &text, QString *key, QJsonValue *value,
                       QString *error) {
    const QString separator = QStringLiteral(" : ");
    const int separatorPosition = text.indexOf(separator);
    if (separatorPosition <= 0) {
        if (error) *error = "Expected: setting.key : JSON value";
        return false;
    }
    const QString parsedKey = text.left(separatorPosition).trimmed();
    const QString valueText = text.mid(separatorPosition + separator.size()).trimmed();
    if (parsedKey.isEmpty() || valueText.isEmpty()) {
        if (error) *error = "Setting key and value must not be empty.";
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(
                QStringLiteral("[%1]").arg(valueText).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError
            || !parsed.isArray() || parsed.array().size() != 1) {
        if (error) *error = parseError.error == QJsonParseError::NoError
                ? QString("Value must be one JSON value.") : parseError.errorString();
        return false;
    }
    if (key) *key = parsedKey;
    if (value) *value = parsed.array().first();
    return true;
}
}

SettingsDialog::SettingsDialog(SettingsManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(new SettingsManager(this)),
      m_runtimeManager(manager),
      m_usedProfileFile(QDir::cleanPath(
          QFileInfo(manager->settingsFilePath()).absoluteFilePath())) {
    m_manager->registry() = manager->registry();
    QString initialLoadError;
    const bool initialProfileLoaded = m_manager->loadFile(
                manager->settingsFilePath(), &initialLoadError);
    Q_UNUSED(initialProfileLoaded);
    Q_UNUSED(initialLoadError);
    setWindowTitle(tr("Settings Editor"));
    setWindowFlags(windowFlags() | Qt::Tool);
    resize(1104, 680);
    setMinimumSize(840, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *menuBar = new QMenuBar(this);
    auto *profileMenu = menuBar->addMenu(tr("&Profile"));
    QAction *saveAction = profileMenu->addAction(tr("&Save"));
    saveAction->setShortcut(QKeySequence::Save);
    QAction *reloadAction = profileMenu->addAction(tr("&Reload"));
    reloadAction->setShortcut(QKeySequence::Refresh);
    profileMenu->addSeparator();
    QAction *loadAction = profileMenu->addAction(tr("&Load..."));
    QAction *saveAsAction = profileMenu->addAction(tr("Save &As..."));
    m_duplicateProfileAction = profileMenu->addAction(tr("&Duplicate..."));
    QAction *folderAction = profileMenu->addAction(tr("Open Profile &Folder"));
    profileMenu->addSeparator();
    QAction *closeAction = profileMenu->addAction(tr("&Close"));
    closeAction->setShortcut(QKeySequence::Close);

    auto *editMenu = menuBar->addMenu(tr("&Edit"));
    QAction *addAction = editMenu->addAction(tr("Add &Custom Setting..."));
    QAction *pasteKeyValueAction = editMenu->addAction(tr("Paste Key and &Value"));
    pasteKeyValueAction->setObjectName("paste-setting-key-value");
    editMenu->addSeparator();
    QAction *rawAction = editMenu->addAction(tr("Edit Raw Profile &JSON..."));

    auto *viewMenu = menuBar->addMenu(tr("&View"));
    m_showUnsupported = viewMenu->addAction(tr("Show &Unsupported Settings"));
    m_showUnsupported->setCheckable(true);
    m_showUnsupported->setChecked(true);
    m_showAdvanced = viewMenu->addAction(tr("Show &Advanced Settings"));
    m_showAdvanced->setCheckable(true);
    m_showAdvanced->setChecked(true);
    layout->setMenuBar(menuBar);

    auto *profileRow = new QHBoxLayout;
    profileRow->addWidget(new QLabel(tr("Profile:")));
    m_profileName = new QComboBox;
    m_profileName->setMinimumWidth(150);
    m_profileName->setStyleSheet("combobox-popup: 0;");
    profileRow->addWidget(m_profileName);
    profileRow->addWidget(new QLabel(tr("File:")));
    m_profilePath = new QLineEdit;
    m_profilePath->setReadOnly(true);
    profileRow->addWidget(m_profilePath, 2);
    profileRow->addWidget(new QLabel(tr("Search:")));
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search name, key, or description..."));
    const QIcon searchIcon = QIcon::fromTheme("edit-find");
    if (!searchIcon.isNull())
        m_search->addAction(searchIcon, QLineEdit::LeadingPosition);
    profileRow->addWidget(m_search, 1);
    layout->addLayout(profileRow);

    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs, 1);

    auto *bottom = new QHBoxLayout;
    m_statusLabel = new QLabel;
    bottom->addWidget(m_statusLabel, 1);
    auto *save = new QPushButton(tr("Save Profile"));
    m_applyRuntime = new QPushButton(tr("Apply to Running TSRE"));
    auto *close = new QPushButton(tr("Close"));
    save->setDefault(true);
    bottom->addWidget(save);
    bottom->addWidget(m_applyRuntime);
    bottom->addWidget(close);
    layout->addLayout(bottom);

    connect(m_search, &QLineEdit::textChanged, this, &SettingsDialog::applyFilters);
    connect(m_showUnsupported, &QAction::toggled, this, &SettingsDialog::applyFilters);
    connect(m_showAdvanced, &QAction::toggled, this, &SettingsDialog::applyFilters);
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &SettingsDialog::updateSearchPlacement);
    connect(loadAction, &QAction::triggered, this, &SettingsDialog::loadProfile);
    connect(saveAsAction, &QAction::triggered, this, &SettingsDialog::saveProfileAs);
    connect(m_duplicateProfileAction, &QAction::triggered,
            this, &SettingsDialog::duplicateProfile);
    connect(folderAction, &QAction::triggered, this, &SettingsDialog::openProfileFolder);
    connect(addAction, &QAction::triggered, this, &SettingsDialog::addCustomSetting);
    connect(pasteKeyValueAction, &QAction::triggered,
            this, &SettingsDialog::pasteSettingKeyValue);
    connect(rawAction, &QAction::triggered, this, &SettingsDialog::showRawDocument);
    connect(reloadAction, &QAction::triggered, this, &SettingsDialog::reloadProfile);
    connect(saveAction, &QAction::triggered, this, &SettingsDialog::saveProfile);
    connect(closeAction, &QAction::triggered, this, &SettingsDialog::close);
    connect(m_profileName, &QComboBox::activated,
            this, &SettingsDialog::switchProfile);
    connect(save, &QPushButton::clicked, this, &SettingsDialog::saveProfile);
    connect(m_applyRuntime, &QPushButton::clicked,
            this, &SettingsDialog::applyToRuntime);
    connect(close, &QPushButton::clicked, this, &SettingsDialog::close);
    rebuild();
}

QWidget *SettingsDialog::createEditor(const QJsonObject &setting, Editor *record) {
    SettingType type;
    if (!settingTypeFromName(setting.value("type").toString(), &type)) {
        auto *label = new QLabel(tr("Unknown type - use JSON editor"));
        label->setEnabled(false);
        return label;
    }
    record->type = type;
    const QVariant current = settingVariantFromJson(setting.value("value"), type);
    const QJsonObject range = setting.value("range").toObject();

    if (type == SettingType::Bool) {
        auto *widget = new QCheckBox;
        widget->setChecked(current.toBool());
        record->value = [widget] { return QVariant(widget->isChecked()); };
        return widget;
    }
    if (type == SettingType::Int) {
        auto *widget = new QSpinBox;
        widget->setRange(range.value("minimum").toInt(-1000000000),
                         range.value("maximum").toInt(1000000000));
        widget->setSingleStep(qMax(1, range.value("step").toInt(1)));
        widget->setValue(current.toInt());
        record->value = [widget] { return QVariant(widget->value()); };
        return widget;
    }
    if (type == SettingType::Float) {
        auto *widget = new QDoubleSpinBox;
        widget->setDecimals(6);
        widget->setRange(range.value("minimum").toDouble(-1000000000.0),
                         range.value("maximum").toDouble(1000000000.0));
        widget->setSingleStep(range.value("step").toDouble(0.1));
        widget->setValue(current.toDouble());
        record->value = [widget] { return QVariant(widget->value()); };
        return widget;
    }
    if (type == SettingType::Enum) {
        auto *widget = new QComboBox;
        widget->setStyleSheet("combobox-popup: 0;");
        for (const QJsonValue &entry : setting.value("options").toArray()) {
            const QJsonObject option = entry.toObject();
            widget->addItem(option.value("name").toString(), option.value("value").toVariant());
        }
        const int selected = widget->findData(current);
        if (selected >= 0) widget->setCurrentIndex(selected);
        record->value = [widget] { return widget->currentData(); };
        return widget;
    }
    if (type == SettingType::MultilineString) {
        auto *widget = new QPlainTextEdit(current.toString());
        widget->setMaximumHeight(90);
        record->value = [widget] { return QVariant(widget->toPlainText()); };
        return widget;
    }

    auto *widget = new QLineEdit;
    if (type == SettingType::Secret) {
        record->secret = true;
        record->secretReference = current.toString();
        widget->setText(record->secretReference);
        widget->setReadOnly(true);
        widget->setToolTip(tr(
            "Secret key only. Edit the value for '%1' in the profile-local secrets.json file; "
            "the settings profile stores only this reference.").arg(record->secretReference));
        return widget;
    }
    if (type == SettingType::StringList) {
        widget->setText(current.toStringList().join("; "));
    } else {
        widget->setText(current.toString());
    }
    record->value = [widget, type] {
        if (type == SettingType::StringList) {
            QStringList values = widget->text().split(';', Qt::SkipEmptyParts);
            for (QString &value : values) value = value.trimmed();
            return QVariant(values);
        }
        return QVariant(widget->text());
    };

    if (type == SettingType::Path || type == SettingType::Directory) {
        auto *container = new QWidget;
        auto *row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(widget, 1);
        auto *choose = new QToolButton;
        choose->setFixedSize(28, 24);
        choose->setText("...");
        choose->setToolTip(type == SettingType::Directory
                           ? tr("Choose directory") : tr("Choose file"));
        row->addWidget(choose);
        connect(choose, &QToolButton::clicked, this, [this, widget, type] {
            QString selected;
            if (type == SettingType::Directory) {
                selected = QFileDialog::getExistingDirectory(
                            this, tr("Choose directory"), widget->text());
            } else {
                const QFileInfo current(widget->text());
                const QString start = current.isDir()
                        ? current.absoluteFilePath() : current.absolutePath();
                selected = QFileDialog::getOpenFileName(
                            this, tr("Choose file"), start);
            }
            if (!selected.isEmpty())
                widget->setText(QDir::cleanPath(selected));
        });
        return container;
    }

    if (type == SettingType::Color) {
        const bool nullable = setting.value("nullable").toBool(false);
        widget->setProperty("nullValue", nullable && setting.value("value").isNull());
        if (nullable)
            widget->setPlaceholderText(tr("Default"));
        auto *container = new QWidget;
        auto *row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(widget, 1);
        auto *swatch = new QPushButton;
        swatch->setFixedSize(34, 24);
        swatch->setToolTip(tr("Choose colour"));
        row->addWidget(swatch);
        auto updateSwatch = [swatch, widget](const QString &text) {
            if (widget->property("nullValue").toBool()) {
                swatch->setStyleSheet(QString());
                swatch->setText("-");
                swatch->setToolTip(QObject::tr("Use the application's default colour"));
                return;
            }
            swatch->setText(QString());
            const QColor color(text);
            if (!color.isValid()) {
                swatch->setStyleSheet(QString());
                swatch->setToolTip(QObject::tr("Invalid colour value"));
                return;
            }
            swatch->setStyleSheet(QString(
                "QPushButton { background-color: rgba(%1, %2, %3, %4);"
                " border: 1px solid palette(mid); border-radius: 2px; }"
                "QPushButton:hover { border: 2px solid palette(highlight); }")
                .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha()));
            swatch->setToolTip(QObject::tr("Choose colour: %1").arg(text));
        };
        updateSwatch(widget->text());
        connect(widget, &QLineEdit::textEdited, widget, [widget, updateSwatch](const QString &text) {
            widget->setProperty("nullValue", false);
            updateSwatch(text);
        });
        connect(widget, &QLineEdit::textChanged, swatch, updateSwatch);
        connect(swatch, &QPushButton::clicked, this, [widget] {
            const QColor color = QColorDialog::getColor(
                        QColor(widget->text()), widget, QObject::tr("Choose colour"),
                        QColorDialog::ShowAlphaChannel);
            if (color.isValid()) {
                widget->setProperty("nullValue", false);
                widget->setText(color.name(QColor::HexArgb));
            }
        });
        if (nullable) {
            auto *clearOverride = new QToolButton;
            clearOverride->setText(tr("Clear"));
            clearOverride->setFixedSize(42, 24);
            clearOverride->setToolTip(tr("Clear the override and use the default colour"));
            row->addWidget(clearOverride);
            connect(clearOverride, &QToolButton::clicked, this, [widget, updateSwatch] {
                widget->setProperty("nullValue", true);
                widget->clear();
                updateSwatch(QString());
            });
        }
        record->value = [widget] {
            return widget->property("nullValue").toBool()
                    ? QVariant() : QVariant(widget->text());
        };
        return container;
    }
    return widget;
}

void SettingsDialog::rebuild() {
    m_editors.clear();
    while (m_tabs->count() > 0) {
        QWidget *page = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete page;
    }
    m_resultsTab = nullptr;
    m_resultsLayout = nullptr;
    m_profileName->blockSignals(true);
    m_profileName->clear();
    const QString viewedFile = QDir::cleanPath(
                QFileInfo(m_manager->settingsFilePath()).absoluteFilePath());
    int viewedProfileIndex = -1;
    QSet<QString> listedFiles;
    auto sameFile = [](const QString &left, const QString &right) {
        return QDir::cleanPath(QFileInfo(left).absoluteFilePath())
                .compare(QDir::cleanPath(QFileInfo(right).absoluteFilePath()),
                         Qt::CaseInsensitive) == 0;
    };
    auto addProfile = [&](const QString &name, const QString &file, bool custom) {
        const QString absoluteFile = QDir::cleanPath(QFileInfo(file).absoluteFilePath());
        const QString identity = absoluteFile.toLower();
        if (listedFiles.contains(identity))
            return;
        listedFiles.insert(identity);
        QStringList markers;
        if (sameFile(absoluteFile, m_usedProfileFile))
            markers.append(tr("used"));
        if (sameFile(absoluteFile, viewedFile)
                && !sameFile(absoluteFile, m_usedProfileFile))
            markers.append(tr("viewing"));
        if (custom)
            markers.append(tr("custom"));
        const QString label = markers.isEmpty()
                ? name : tr("%1 (%2)").arg(name, markers.join(", "));
        m_profileName->addItem(label, absoluteFile);
        if (sameFile(absoluteFile, viewedFile))
            viewedProfileIndex = m_profileName->count() - 1;
    };
    for (const QString &profileName : SettingsProfile::portableProfileNames()) {
        const QString file = QDir(SettingsProfile::portableProfilesRoot())
                .filePath(profileName + "/settings.json");
        addProfile(profileName, file, false);
    }
    if (!listedFiles.contains(m_usedProfileFile.toLower()))
        addProfile(QFileInfo(m_usedProfileFile).dir().dirName(), m_usedProfileFile, true);
    if (!listedFiles.contains(viewedFile.toLower()))
        addProfile(m_manager->profileName(), viewedFile, true);
    m_profileName->setCurrentIndex(viewedProfileIndex);
    m_profileName->setToolTip(tr(
        "Selected profile is open only for viewing and editing.\n"
        "Profile used at application startup: %1").arg(m_usedProfileFile));
    m_profileName->blockSignals(false);
    m_duplicateProfileAction->setEnabled(
                SettingsProfile::isPortableProfileFile(viewedFile));
    m_profilePath->setText(m_manager->settingsFilePath());
    m_profilePath->setToolTip(m_manager->settingsFilePath());
    m_applyRuntime->setEnabled(isViewingUsedProfile());
    m_applyRuntime->setToolTip(isViewingUsedProfile()
            ? tr("Apply the editor values to this running TSRE session without saving them.")
            : tr("Only the profile used to start this TSRE session can be applied."));

    QHash<QString, QVBoxLayout *> groupLayouts;
    QHash<QString, QVBoxLayout *> sectionLayouts;
    QHash<QString, QWidget *> sectionHeadings;
    QHash<QString, QWidget *> sectionWidgets;
    QHash<QString, QJsonObject> groupObjects;
    auto createHeader = [this] {
        auto *header = new QWidget;
        auto *grid = new QGridLayout(header);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setHorizontalSpacing(10);
        const QStringList titles{tr("Used"), tr("Setting"), tr("Value"),
                                 tr("Description"), tr("JSON")};
        for (int column = 0; column < titles.size(); ++column) {
            auto *label = new QLabel(titles.at(column));
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
            label->setAlignment(column == 0 || column == 4
                                ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
            if (column == 0)
                label->setToolTip(tr("Used by the running TSRE build"));
            grid->addWidget(label, 0, column);
        }
        grid->setColumnMinimumWidth(0, 48);
        grid->setColumnMinimumWidth(1, 155);
        grid->setColumnMinimumWidth(2, 145);
        grid->setColumnMinimumWidth(3, 230);
        grid->setColumnMinimumWidth(4, 44);
        grid->setColumnStretch(1, 2);
        grid->setColumnStretch(2, 1);
        grid->setColumnStretch(3, 5);
        header->setAutoFillBackground(true);
        QPalette palette = header->palette();
        palette.setColor(QPalette::Window, palette.color(QPalette::AlternateBase));
        header->setPalette(palette);
        return header;
    };

    for (const QJsonValue &entry : m_manager->groupsArray()) {
        const QJsonObject group = entry.toObject();
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(0, 0, 0, 0);
        tabLayout->setSpacing(0);
        tabLayout->addWidget(createHeader());
        auto *content = new QWidget;
        auto *rows = new QVBoxLayout(content);
        rows->setContentsMargins(4, 4, 4, 4);
        rows->setSpacing(2);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(content);
        tabLayout->addWidget(scroll, 1);
        m_tabs->addTab(tab, group.value("name").toString(group.value("id").toString()));
        const QString groupId = group.value("id").toString();
        groupLayouts.insert(groupId, rows);
        groupObjects.insert(groupId, group);
    }
    bool needsOtherTab = false;
    for (const QJsonValue &entry : m_manager->settingsArray()) {
        if (!groupLayouts.contains(entry.toObject().value("group").toString())) {
            needsOtherTab = true;
            break;
        }
    }
    if (needsOtherTab) {
        auto *tab = new QWidget;
        auto *tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(0, 0, 0, 0);
        tabLayout->setSpacing(0);
        tabLayout->addWidget(createHeader());
        auto *content = new QWidget;
        auto *rows = new QVBoxLayout(content);
        rows->setContentsMargins(4, 4, 4, 4);
        rows->setSpacing(2);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(content);
        tabLayout->addWidget(scroll, 1);
        m_tabs->addTab(tab, tr("Other"));
        groupLayouts.insert(QString(), rows);
    }

    auto sectionKey = [](const QString &group, const QString &subgroup) {
        return group + QChar(0x1f) + subgroup;
    };
    auto ensureSection = [&](const QString &groupId, const QString &subgroupId) -> QVBoxLayout * {
        const QString key = sectionKey(groupId, subgroupId);
        if (sectionLayouts.contains(key))
            return sectionLayouts.value(key);
        QVBoxLayout *groupRows = groupLayouts.value(groupId, groupLayouts.value(QString()));
        if (!groupRows)
            return nullptr;

        QString title = subgroupId.isEmpty() ? tr("General") : subgroupId;
        QString description;
        for (const QJsonValue &entry : groupObjects.value(groupId).value("subgroups").toArray()) {
            const QJsonObject subgroup = entry.toObject();
            if (subgroup.value("id").toString() == subgroupId) {
                title = subgroup.value("name").toString(title);
                description = subgroup.value("description").toString();
                break;
            }
        }
        auto *heading = new QWidget;
        auto *headingLayout = new QGridLayout(heading);
        headingLayout->setContentsMargins(4, 7, 4, 3);
        headingLayout->setHorizontalSpacing(10);
        headingLayout->setColumnMinimumWidth(0, 48);
        headingLayout->setColumnMinimumWidth(1, 155);
        headingLayout->setColumnMinimumWidth(2, 145);
        headingLayout->setColumnMinimumWidth(3, 230);
        headingLayout->setColumnMinimumWidth(4, 44);
        headingLayout->setColumnStretch(1, 2);
        headingLayout->setColumnStretch(2, 1);
        headingLayout->setColumnStretch(3, 5);
        auto *headingLabel = new QLabel(title);
        QFont headingFont = headingLabel->font();
        headingFont.setBold(true);
        headingLabel->setFont(headingFont);
        headingLabel->setStyleSheet(QString("QLabel { color : %1; }")
                                    .arg(Game::StyleMainLabel));
        headingLabel->setToolTip(description);
        headingLayout->addWidget(headingLabel, 0, 1, 1, 3);
        heading->hide();
        groupRows->addWidget(heading);
        sectionHeadings.insert(key, heading);

        auto *section = new QWidget;
        auto *rows = new QVBoxLayout(section);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(2);
        section->hide();
        groupRows->addWidget(section);
        sectionLayouts.insert(key, rows);
        sectionWidgets.insert(key, section);
        return rows;
    };
    for (auto group = groupObjects.constBegin(); group != groupObjects.constEnd(); ++group) {
        for (const QJsonValue &entry : group.value().value("subgroups").toArray())
            ensureSection(group.key(), entry.toObject().value("id").toString());
    }

    m_resultsTab = new QWidget;
    auto *resultsTabLayout = new QVBoxLayout(m_resultsTab);
    resultsTabLayout->setContentsMargins(0, 0, 0, 0);
    resultsTabLayout->setSpacing(0);
    resultsTabLayout->addWidget(createHeader());
    auto *resultsContent = new QWidget;
    m_resultsLayout = new QVBoxLayout(resultsContent);
    m_resultsLayout->setContentsMargins(4, 4, 4, 4);
    m_resultsLayout->setSpacing(2);
    auto *resultsScroll = new QScrollArea;
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setFrameShape(QFrame::NoFrame);
    resultsScroll->setWidget(resultsContent);
    resultsTabLayout->addWidget(resultsScroll, 1);
    m_tabs->addTab(m_resultsTab, tr("Results"));

    for (const QJsonValue &entry : m_manager->settingsArray()) {
        const QJsonObject setting = entry.toObject();
        Editor record;
        record.key = setting.value("key").toString();
        auto *row = new QWidget;
        auto *rowLayout = new QGridLayout(row);
        rowLayout->setContentsMargins(4, 3, 4, 3);
        rowLayout->setHorizontalSpacing(10);
        auto *supported = new QCheckBox;
        supported->setObjectName(QStringLiteral("setting-support:") + record.key);
        supported->setEnabled(false);
        supported->setFixedWidth(48);
        SettingType implementedType;
        SettingType storedType;
        SettingsManager::SupportState state = SettingsManager::Unsupported;
        if (m_runtimeManager->registry().supportedType(record.key, &implementedType)) {
            state = settingTypeFromName(setting.value("type").toString(), &storedType)
                    && storedType == implementedType
                    ? SettingsManager::Supported : SettingsManager::TypeMismatch;
        }
        supported->setChecked(state == SettingsManager::Supported);
        supported->setToolTip(state == SettingsManager::Supported ? tr("Supported by this build")
                              : state == SettingsManager::TypeMismatch ? tr("Known key, incompatible type")
                                                                      : tr("Not registered by this build"));
        rowLayout->addWidget(supported, 0, 0, Qt::AlignCenter);

        auto *name = new QLabel(settingTitle(setting));
        name->setMinimumWidth(155);
        name->setWordWrap(true);
        name->setToolTip(tr("Key: %1").arg(record.key));
        rowLayout->addWidget(name, 0, 1);

        QWidget *editorWidget = createEditor(setting, &record);
        editorWidget->setMinimumWidth(145);
        rowLayout->addWidget(editorWidget, 0, 2);

        auto *description = new QLabel(setting.value("description").toString());
        description->setMinimumWidth(230);
        description->setWordWrap(true);
        description->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rowLayout->addWidget(description, 0, 3);

        auto *metadata = new QToolButton;
        metadata->setText("...");
        metadata->setFixedWidth(44);
        metadata->setToolTip(tr("Setting actions"));
        auto *settingMenu = new QMenu(metadata);
        QAction *viewJson = settingMenu->addAction(tr("View JSON..."));
        viewJson->setObjectName(QStringLiteral("view-setting-json:") + record.key);
        QAction *copyKeyValue = settingMenu->addAction(tr("Copy Key and Value"));
        copyKeyValue->setObjectName(QStringLiteral("copy-setting-key-value:") + record.key);
        connect(viewJson, &QAction::triggered, this,
                [this, key = record.key] { editSettingMetadata(key); });
        connect(copyKeyValue, &QAction::triggered, this,
                [this, key = record.key] { copySettingKeyValue(key); });
        metadata->setMenu(settingMenu);
        metadata->setPopupMode(QToolButton::InstantPopup);
        rowLayout->addWidget(metadata, 0, 4, Qt::AlignCenter);

        row->setProperty("searchText", QString("%1 %2 %3")
                         .arg(settingTitle(setting), record.key,
                              setting.value("description").toString()).toLower());
        row->setProperty("supported", state == SettingsManager::Supported);
        row->setProperty("advanced", setting.value("advanced").toBool());
        rowLayout->setColumnMinimumWidth(0, 48);
        rowLayout->setColumnMinimumWidth(1, 155);
        rowLayout->setColumnMinimumWidth(2, 145);
        rowLayout->setColumnMinimumWidth(3, 230);
        rowLayout->setColumnMinimumWidth(4, 44);
        rowLayout->setColumnStretch(1, 2);
        rowLayout->setColumnStretch(2, 1);
        rowLayout->setColumnStretch(3, 5);
        record.row = row;

        const QString groupId = setting.value("group").toString();
        const QString subgroupId = setting.value("subgroup").toString();
        QVBoxLayout *rows = ensureSection(groupLayouts.contains(groupId) ? groupId : QString(),
                                          subgroupId);
        if (!rows)
            continue;
        record.homeLayout = rows;
        record.sectionId = sectionKey(
                groupLayouts.contains(groupId) ? groupId : QString(), subgroupId);
        record.sectionHeading = sectionHeadings.value(record.sectionId);
        record.sectionWidget = sectionWidgets.value(record.sectionId);
        record.homeOrder = rows->count();
        m_editors.append(record);
        row->setAutoFillBackground(true);
        row->setProperty("stripeSurface", row->palette().color(QPalette::Window));
        rows->addWidget(row);
    }
    for (QVBoxLayout *rows : groupLayouts)
        rows->addStretch(1);
    m_resultsLayout->addStretch(1);
    applyFilters();
    const int errors = std::count_if(m_manager->issues().cbegin(), m_manager->issues().cend(),
                                     [](const SettingsIssue &issue) {
        return issue.severity == SettingsIssue::Error;
    });
    m_statusLabel->setText(tr("%1 settings | %2 validation error(s)%3")
                           .arg(m_editors.size()).arg(errors)
                           .arg(m_manager->isModified() ? tr(" | unsaved changes") : QString()));
}

void SettingsDialog::applyFilters() {
    const QString text = m_search->text().trimmed().toLower();
    const bool searching = !text.isEmpty();
    int resultCount = 0;
    for (const Editor &editor : m_editors) {
        const bool passesView = (m_showUnsupported->isChecked()
                                 || editor.row->property("supported").toBool())
                && (m_showAdvanced->isChecked()
                    || !editor.row->property("advanced").toBool());
        const bool matches = editor.row->property("searchText").toString().contains(text);
        editor.row->setProperty("passesView", passesView);
        editor.row->setProperty("searchMatch", searching && matches);
        if (searching && passesView && matches)
            ++resultCount;
    }
    const int resultsIndex = m_tabs->indexOf(m_resultsTab);
    if (resultsIndex < 0)
        return;
    m_tabs->setTabText(resultsIndex, searching
                       ? tr("Results (%1)").arg(resultCount) : tr("Results"));
    m_tabs->setTabEnabled(resultsIndex, searching);
    if (searching)
        m_tabs->setCurrentIndex(resultsIndex);
    else if (m_tabs->currentIndex() == resultsIndex && m_tabs->count() > 1)
        m_tabs->setCurrentIndex(0);
    updateSearchPlacement();
}

void SettingsDialog::updateSearchPlacement() {
    if (!m_resultsTab || !m_resultsLayout)
        return;

    // A normal tab must always contain its complete set of rows. Restore all
    // result widgets synchronously before a normal tab can be painted.
    for (const Editor &editor : m_editors) {
        if (editor.row->parentWidget() != editor.homeLayout->parentWidget())
            editor.homeLayout->insertWidget(editor.homeOrder, editor.row);
    }

    const bool showResults = m_tabs->currentWidget() == m_resultsTab
            && !m_search->text().trimmed().isEmpty();
    QSet<QWidget *> headings;
    QSet<QWidget *> sections;
    for (const Editor &editor : m_editors) {
        if (editor.sectionHeading)
            headings.insert(editor.sectionHeading);
        if (editor.sectionWidget)
            sections.insert(editor.sectionWidget);
    }
    for (QWidget *heading : headings)
        heading->setVisible(false);
    for (QWidget *section : sections)
        section->setVisible(false);
    for (const Editor &editor : m_editors) {
        const bool passesView = editor.row->property("passesView").toBool();
        const bool matches = editor.row->property("searchMatch").toBool();
        if (showResults && passesView && matches) {
            m_resultsLayout->insertWidget(qMax(0, m_resultsLayout->count() - 1), editor.row);
            editor.row->show();
        } else {
            editor.row->setVisible(passesView);
            if (!showResults && passesView && editor.sectionHeading) {
                editor.sectionHeading->setVisible(true);
                if (editor.sectionWidget)
                    editor.sectionWidget->setVisible(true);
            }
        }
    }

    // Stripe the rows in the order users can actually see them. Subgroups
    // rearrange catalogue rows, and filters can remove rows between two others,
    // so construction-time indices cannot reliably alternate the colours.
    QHash<QString, int> stripeCounts;
    for (const Editor &editor : m_editors) {
        const bool passesView = editor.row->property("passesView").toBool();
        const bool matches = editor.row->property("searchMatch").toBool();
        if ((!showResults && !passesView) || (showResults && (!passesView || !matches)))
            continue;
        const QString stripeId = showResults ? QStringLiteral("results") : editor.sectionId;
        const int rowNumber = stripeCounts.value(stripeId, 0);
        stripeCounts.insert(stripeId, rowNumber + 1);
        const QColor surface = editor.row->property("stripeSurface").value<QColor>();
        const QColor stripe = surface.lightnessF() < 0.5
                ? surface.lighter(rowNumber % 2 == 0 ? 128 : 110)
                : surface.darker(rowNumber % 2 == 0 ? 100 : 115);
        QPalette rowPalette = editor.row->palette();
        rowPalette.setColor(QPalette::Window, stripe);
        editor.row->setPalette(rowPalette);
    }
}

bool SettingsDialog::applyEditors(QString *error) {
    for (const Editor &editor : m_editors) {
        if (!editor.value) continue;
        if (editor.secret) {
            if (!m_manager->setSecretValue(editor.secretReference, editor.value().toString(), error))
                return false;
        } else if (!m_manager->setValue(editor.key, editor.value(), error)) {
            return false;
        }
    }
    return true;
}

bool SettingsDialog::hasEditorChanges() const {
    for (const Editor &editor : m_editors) {
        if (!editor.value) continue;
        if (editor.secret) {
            if (editor.value().toString() != m_manager->secretValue(editor.secretReference))
                return true;
        } else if (editor.value() != m_manager->value(editor.key)) {
            return true;
        }
    }
    return false;
}

bool SettingsDialog::isViewingUsedProfile() const {
    return QDir::cleanPath(QFileInfo(m_manager->settingsFilePath()).absoluteFilePath())
            .compare(m_usedProfileFile, Qt::CaseInsensitive) == 0;
}

void SettingsDialog::saveProfile() {
    QString error;
    if (!applyEditors(&error)) { showError(tr("Cannot apply settings"), error); return; }
    if (!m_manager->save(&error)) {
        if (m_manager->hasExternalChange()) {
            const auto answer = QMessageBox::question(this, tr("Profile changed externally"),
                    tr("The settings file changed on disk. Overwrite it with the editor values?"));
            if (answer == QMessageBox::Yes && m_manager->save(&error, true)) { rebuild(); return; }
        }
        showError(tr("Cannot save profile"), error);
        return;
    }
    rebuild();
}

void SettingsDialog::applyToRuntime() {
    if (!isViewingUsedProfile()) {
        showError(tr("Cannot apply profile"),
                  tr("Only the profile used to start this TSRE session can be applied."));
        return;
    }
    QString error;
    if (!applyEditors(&error)) {
        showError(tr("Cannot apply settings"), error);
        return;
    }
    QStringList changed;
    if (!m_runtimeManager->applyProfileToRuntime(m_manager->document(),
                                                  &changed, &error)) {
        showError(tr("Cannot apply settings"), error);
        return;
    }
    int dynamic = 0;
    QSet<QString> pending;
    for (const QString &key : changed) {
        const SettingsDefinition *definition =
                m_runtimeManager->registry().definition(key);
        if (!definition || definition->apply == "dynamic")
            ++dynamic;
        else
            pending.insert(definition->apply);
    }
    QStringList result;
    result.append(tr("%1 runtime setting(s) applied").arg(dynamic));
    if (pending.contains("routeReload")) result.append(tr("route reload required"));
    if (pending.contains("rendererRestart")) result.append(tr("renderer restart required"));
    if (pending.contains("applicationRestart")) result.append(tr("application restart required"));
    m_statusLabel->setText(result.join(tr(" | ")));
}

void SettingsDialog::reloadProfile() {
    if (!confirmDiscardChanges()) return;
    QString error;
    if (!m_manager->reload(&error)) showError(tr("Cannot reload profile"), error);
    else rebuild();
}

void SettingsDialog::loadProfile() {
    if (!confirmDiscardChanges()) return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Load settings profile"),
            m_manager->profileDirectory(), tr("JSON settings (*.json);;All files (*)"));
    if (file.isEmpty()) return;
    QString error;
    if (!m_manager->loadFile(file, &error)) showError(tr("Cannot load profile"), error);
    else rebuild();
}

void SettingsDialog::saveProfileAs() {
    QString suggested = QFileInfo(m_manager->settingsFilePath()).fileName();
    const QString file = QFileDialog::getSaveFileName(this, tr("Save settings profile as"),
            QDir(m_manager->profileDirectory()).filePath(suggested), tr("JSON settings (*.json)"));
    if (file.isEmpty()) return;
    QString error;
    if (!applyEditors(&error) || !m_manager->saveAs(file, &error))
        showError(tr("Cannot save profile"), error);
    else rebuild();
}

void SettingsDialog::duplicateProfile() {
    QString sourceName;
    if (!SettingsProfile::isPortableProfileFile(m_manager->settingsFilePath(), &sourceName)) {
        showError(tr("Cannot duplicate profile"),
                  tr("Only a managed profile under the portable profiles directory can be duplicated."));
        return;
    }
    if (m_manager->isModified() || hasEditorChanges()) {
        if (QMessageBox::question(this, tr("Save before duplicating?"),
                tr("The current profile has unsaved changes. Save them before creating the duplicate?"),
                QMessageBox::Save | QMessageBox::Cancel) != QMessageBox::Save)
            return;
        QString error;
        if (!applyEditors(&error) || !m_manager->save(&error)) {
            showError(tr("Cannot save profile"), error);
            return;
        }
    }

    bool accepted = false;
    const QString newName = QInputDialog::getText(
                this, tr("Duplicate profile"), tr("New profile name:"),
                QLineEdit::Normal, sourceName + "-copy", &accepted).trimmed();
    if (!accepted)
        return;
    QString newSettingsFile;
    QString error;
    if (!SettingsProfile::duplicatePortableProfile(
                m_manager->settingsFilePath(), newName, &newSettingsFile, &error)) {
        showError(tr("Cannot duplicate profile"), error);
        return;
    }
    if (!m_manager->loadFile(newSettingsFile, &error)) {
        showError(tr("Profile was duplicated but cannot be opened"), error);
        rebuild();
        return;
    }
    rebuild();
}

void SettingsDialog::switchProfile(int index) {
    if (index < 0)
        return;
    const QString selectedFile = m_profileName->itemData(index).toString();
    const QString currentFile = QDir::cleanPath(
                QFileInfo(m_manager->settingsFilePath()).absoluteFilePath());
    if (QDir::cleanPath(QFileInfo(selectedFile).absoluteFilePath())
            .compare(currentFile, Qt::CaseInsensitive) == 0)
        return;
    if (!confirmDiscardChanges()) {
        rebuild();
        return;
    }
    QString error;
    if (!m_manager->loadFile(selectedFile, &error)) {
        showError(tr("Cannot switch profile"), error);
        rebuild();
        return;
    }
    rebuild();
}

void SettingsDialog::openProfileFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_manager->profileDirectory()));
}

bool SettingsDialog::exchangeJson(
        const QString &title, const QJsonObject &initial,
        const std::function<bool(const QJsonObject &, QString *)> &apply) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(650, 520);
    auto *layout = new QVBoxLayout(&dialog);
    const QString original = QString::fromUtf8(
                QJsonDocument(initial).toJson(QJsonDocument::Indented));
    auto *edit = new QPlainTextEdit(original);
    edit->setReadOnly(true);
    layout->addWidget(edit);
    auto *buttons = new QDialogButtonBox;
    auto *copy = buttons->addButton(tr("Copy JSON"), QDialogButtonBox::ActionRole);
    auto *paste = buttons->addButton(tr("Paste JSON"), QDialogButtonBox::ActionRole);
    auto *applyChanges = buttons->addButton(tr("Replace Editor JSON"), QDialogButtonBox::ApplyRole);
    auto *close = buttons->addButton(QDialogButtonBox::Close);
    applyChanges->setEnabled(false);
    layout->addWidget(buttons);
    connect(copy, &QPushButton::clicked, &dialog, [edit] {
        QApplication::clipboard()->setText(edit->toPlainText());
    });
    connect(paste, &QPushButton::clicked, &dialog, [edit, applyChanges, original] {
        edit->setPlainText(QApplication::clipboard()->text());
        applyChanges->setEnabled(edit->toPlainText() != original);
    });
    connect(applyChanges, &QPushButton::clicked, &dialog, [&] {
        QString error;
        const QJsonObject object = parseObject(edit->toPlainText(), &error);
        if (object.isEmpty() || !apply(object, &error))
            showError(tr("Invalid JSON"), error);
        else dialog.accept();
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted;
}

void SettingsDialog::editSettingMetadata(const QString &key) {
    if (exchangeJson(tr("Setting object: %1").arg(key), m_manager->settingObject(key),
                     [this, key](const QJsonObject &object, QString *error) {
        return m_manager->replaceSettingObject(key, object, error);
    }))
        rebuild();
}

void SettingsDialog::copySettingKeyValue(const QString &key) {
    const QJsonObject setting = m_manager->settingObject(key);
    if (setting.isEmpty()) {
        showError(tr("Cannot copy setting"), tr("Unknown setting key: %1").arg(key));
        return;
    }

    QJsonValue value = setting.value("value");
    for (const Editor &editor : m_editors) {
        if (editor.key != key || !editor.value || editor.secret)
            continue;
        value = settingJsonFromVariant(editor.value(), editor.type);
        break;
    }

    QApplication::clipboard()->setText(
                QStringLiteral("%1 : %2").arg(key, jsonValueText(value)));
    m_statusLabel->setText(tr("Copied key and value: %1").arg(key));
}

void SettingsDialog::pasteSettingKeyValue() {
    QString error;
    QString key;
    QJsonValue value;
    if (!parseKeyValueText(QApplication::clipboard()->text(), &key, &value, &error)) {
        showError(tr("Cannot paste key and value"), error);
        return;
    }

    const QJsonObject setting = m_manager->settingObject(key);
    SettingType type;
    if (setting.isEmpty()) {
        showError(tr("Cannot paste key and value"),
                  tr("This profile does not contain setting: %1").arg(key));
        return;
    }
    if (!settingTypeFromName(setting.value("type").toString(), &type)
            || !settingTypeAcceptsJson(type, value)
            || (value.isNull() && !setting.value("nullable").toBool())) {
        showError(tr("Cannot paste key and value"),
                  tr("Clipboard value is incompatible with setting: %1").arg(key));
        return;
    }

    // Preserve edits already made in other rows before rebuilding the table.
    if (!applyEditors(&error)
            || !m_manager->setValue(key, settingVariantFromJson(value, type), &error)) {
        showError(tr("Cannot paste key and value"), error);
        return;
    }
    rebuild();
    m_statusLabel->setText(tr("Pasted value for: %1").arg(key));
}

void SettingsDialog::addCustomSetting() {
    QJsonObject object{{"key", "custom.setting"}, {"name", "Custom setting"},
        {"type", "string"}, {"value", ""}, {"default", ""},
        {"group", "advanced"}, {"description", "User-defined setting"},
        {"apply", "restart"}, {"advanced", true}};
    if (exchangeJson(tr("Add custom setting"), object,
                     [this](const QJsonObject &candidate, QString *error) {
        return m_manager->addSettingObject(candidate, error);
    }))
        rebuild();
}

void SettingsDialog::showRawDocument() {
    if (exchangeJson(tr("Raw settings JSON"), m_manager->document(),
                     [this](const QJsonObject &candidate, QString *error) {
        return m_manager->replaceDocument(candidate, error);
    }))
        rebuild();
}

bool SettingsDialog::confirmDiscardChanges() {
    if (!m_manager->isModified() && !hasEditorChanges()) return true;
    return QMessageBox::question(this, tr("Discard changes?"),
            tr("This profile has unsaved changes. Discard them?")) == QMessageBox::Yes;
}

void SettingsDialog::closeEvent(QCloseEvent *event) {
    if (!m_manager->isModified() && !hasEditorChanges()) {
        event->accept();
        return;
    }
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }

    // This dialog is hidden and reused by RouteEditorWindow. Ordinary control
    // edits have not yet reached m_manager, so checking only isModified() leaves
    // their widget values alive when the same dialog is shown again. Restore the
    // complete editor state for every confirmed discard.
    QString error;
    if (!m_manager->reload(&error)) {
        showError(tr("Cannot discard changes"), error);
        event->ignore();
        return;
    }
    rebuild();
    event->accept();
}

void SettingsDialog::showError(const QString &title, const QString &message) {
    QMessageBox::critical(this, title, message.isEmpty() ? tr("Unknown error") : message);
}
