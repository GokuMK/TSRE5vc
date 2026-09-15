#include <TranslationManager.h>

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>

namespace {
bool languageTagMatches(const QString &tag, const QString &language)
{
    const QString normalized = tag.toLower().replace('_', '-');
    return normalized == language || normalized.startsWith(language + '-');
}
}

bool TranslationManager::IdFallbackTranslator::loadEnglishCatalog()
{
    return m_englishCatalog.load(QStringLiteral(":/i18n/tsre_en.qm"));
}

QString TranslationManager::IdFallbackTranslator::translate(
        const char *context, const char *sourceText,
        const char *disambiguation, int n) const
{
    if (!sourceText || (context && *context))
        return {};

    const QString english = m_englishCatalog.translate(
                context, sourceText, disambiguation, n);
    if (english.isEmpty())
        return {};

    // A missing ID must stay visible, but retaining its placeholders prevents
    // QString::arg warnings and keeps the associated runtime values visible.
    static const QRegularExpression placeholderPattern(
                QStringLiteral("%L?(?:[1-9][0-9]?|n)"));
    QStringList placeholders;
    auto match = placeholderPattern.globalMatch(english);
    while (match.hasNext()) {
        const QString placeholder = match.next().captured();
        if (!placeholders.contains(placeholder))
            placeholders.append(placeholder);
    }

    const QString id = QString::fromUtf8(sourceText);
    if (placeholders.isEmpty())
        return id;
    return id + QStringLiteral(" [") + placeholders.join(QStringLiteral(", "))
            + QLatin1Char(']');
}

QString TranslationManager::resolveLanguage(const QString &preference,
                                            const QStringList &systemUiLanguages)
{
    if (preference == QStringLiteral("pl"))
        return QStringLiteral("pl");
    if (preference == QStringLiteral("en"))
        return QStringLiteral("en");

    for (const QString &tag : systemUiLanguages) {
        if (languageTagMatches(tag, QStringLiteral("pl")))
            return QStringLiteral("pl");
        if (languageTagMatches(tag, QStringLiteral("en")))
            return QStringLiteral("en");
    }
    return QStringLiteral("en");
}

bool TranslationManager::install(QCoreApplication &application,
                                 const QString &preference,
                                 QString *effectiveLanguage,
                                 QString *error)
{
    if (!m_idFallbackTranslator.loadEnglishCatalog()) {
        if (error)
            *error = QStringLiteral("Cannot load the embedded English translation metadata catalogue");
        return false;
    }

    QString selected = resolveLanguage(preference, QLocale::system().uiLanguages());
    QString resource = QStringLiteral(":/i18n/tsre_%1.qm").arg(selected);

    if (!m_translator.load(resource)) {
        const QString requestedResource = resource;
        if (selected == QStringLiteral("en")) {
            if (error)
                *error = QStringLiteral("Cannot load the embedded English translation catalogue: %1")
                        .arg(resource);
            return false;
        }

        selected = QStringLiteral("en");
        resource = QStringLiteral(":/i18n/tsre_en.qm");
        if (!m_translator.load(resource)) {
            if (error)
                *error = QStringLiteral("Cannot load translation catalogues %1 or %2")
                        .arg(requestedResource, resource);
            return false;
        }
        if (error)
            *error = QStringLiteral("Cannot load %1; using embedded English")
                    .arg(requestedResource);
    } else if (error) {
        error->clear();
    }

    if (!application.installTranslator(&m_idFallbackTranslator)) {
        if (error)
            *error = QStringLiteral("Cannot install the missing-ID translation fallback");
        return false;
    }
    if (!application.installTranslator(&m_translator)) {
        application.removeTranslator(&m_idFallbackTranslator);
        if (error)
            *error = QStringLiteral("Cannot install the embedded translation catalogue: %1")
                    .arg(resource);
        return false;
    }

    m_language = selected;
    if (effectiveLanguage)
        *effectiveLanguage = selected;
    return true;
}

QString TranslationManager::language() const
{
    return m_language;
}
