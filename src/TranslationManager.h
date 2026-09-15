#ifndef TSRE_TRANSLATIONMANAGER_H
#define TSRE_TRANSLATIONMANAGER_H

#include <QString>
#include <QStringList>
#include <QTranslator>

class QCoreApplication;

class TranslationManager
{
public:
    static QString resolveLanguage(const QString &preference,
                                   const QStringList &systemUiLanguages);

    bool install(QCoreApplication &application, const QString &preference,
                 QString *effectiveLanguage = nullptr, QString *error = nullptr);

    QString language() const;

private:
    class IdFallbackTranslator final : public QTranslator
    {
    public:
        bool loadEnglishCatalog();
        QString translate(const char *context, const char *sourceText,
                          const char *disambiguation = nullptr,
                          int n = -1) const override;

    private:
        QTranslator m_englishCatalog;
    };

    IdFallbackTranslator m_idFallbackTranslator;
    QTranslator m_translator;
    QString m_language;
};

#endif
