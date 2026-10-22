#include "tests.h"

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTranslator>

// Every Moonlight Desktop text, dtr("...") in the sources and our strings in
// StreamSegue.qml, must be translated in app/languages/desktop_ru.qm (built from
// desktop_ru.ts with lrelease).

namespace {

struct Text {
    QString context;
    QByteArray source;
    QByteArray disambiguation;
};

const QString kRoot = QStringLiteral(MOONLIGHT_SOURCE_DIR);

// Our texts in StreamSegue.qml (the rest of that file is Moonlight's own)
const char* const kQmlTexts[] = {
    "Lost the connection to the host. Reconnecting (attempt %1 of %2)…",
    "Reconnecting with the new settings…",
    "Lost the connection to the host. Reconnecting…",
    "Cancel",
};

QVector<Text> dtrTexts()
{
    static const QRegularExpression call(
        QStringLiteral(R"re(\bdtr\(\s*"((?:[^"\\]|\\.)*)"\s*(?:,\s*"((?:[^"\\]|\\.)*)"\s*)?\))re"));
    QVector<Text> texts;
    QStringList files;
    for (const QString& dir : { QStringLiteral("/app/streaming/localrdp"), QStringLiteral("/app/localrdp-shared") }) {
        QDirIterator it(kRoot + dir, { "*.cpp", "*.h" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            files << it.next();
        }
    }
    files << kRoot + "/app/streaming/session.cpp" << kRoot + "/app/backend/computermanager.cpp";
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString code = QString::fromUtf8(f.readAll());
        auto m = call.globalMatch(code);
        while (m.hasNext()) {
            const auto match = m.next();
            texts << Text{ "MoonlightDesktop", match.captured(1).toUtf8(), match.captured(2).toUtf8() };
        }
    }
    return texts;
}

QVector<Text> allTexts()
{
    QVector<Text> texts = dtrTexts();
    for (const char* t : kQmlTexts) {
        texts << Text{ "StreamSegue", QByteArray(t), QByteArray() };
    }
    return texts;
}

QSet<QString> placeholders(const QString& s)
{
    static const QRegularExpression arg(QStringLiteral("%\\d"));
    QSet<QString> found;
    auto m = arg.globalMatch(s);
    while (m.hasNext()) {
        found << m.next().captured(0);
    }
    return found;
}

}

void TestTranslations::sourcesScanned()
{
    // The scanner must see the texts (catches a broken pattern or path)
    QVERIFY2(dtrTexts().size() >= 70, qPrintable(QString::number(dtrTexts().size())));
    // The QML texts are still in StreamSegue.qml
    QFile qml(kRoot + "/app/gui/StreamSegue.qml");
    QVERIFY(qml.open(QIODevice::ReadOnly));
    const QString code = QString::fromUtf8(qml.readAll());
    for (const char* t : kQmlTexts) {
        QVERIFY2(code.contains(QStringLiteral("qsTr(\"%1\")").arg(QString::fromUtf8(t))), t);
    }
}

void TestTranslations::everyTextHasRussian()
{
    QTranslator ru;
    QVERIFY(ru.load(kRoot + "/app/languages/desktop_ru.qm"));
    QStringList missing;
    for (const Text& t : allTexts()) {
        const QString translated = ru.translate(t.context.toUtf8().constData(), t.source.constData(),
                                                t.disambiguation.isEmpty() ? nullptr : t.disambiguation.constData());
        if (translated.isEmpty()) {
            missing << QString::fromUtf8(t.source);
        }
    }
    QVERIFY2(missing.isEmpty(), qPrintable("No Russian for: " + missing.join(" | ")));
}

void TestTranslations::systemLanguageNameFindsIt()
{
    // The app loads "desktop_" + QLocale::system().name(), e.g. desktop_ru_RU
    QTranslator ru;
    QVERIFY(ru.load(kRoot + "/app/languages/desktop_ru_RU"));
    QCOMPARE(ru.translate("MoonlightDesktop", "Mouse"), QStringLiteral("Мышь"));
    // English: no file, the texts stay as written
    QTranslator en;
    QVERIFY(!en.load(kRoot + "/app/languages/desktop_en_US"));
}

void TestTranslations::placeholdersKept()
{
    QTranslator ru;
    QVERIFY(ru.load(kRoot + "/app/languages/desktop_ru.qm"));
    QStringList wrong;
    for (const Text& t : allTexts()) {
        const QString source = QString::fromUtf8(t.source);
        const QString translated = ru.translate(t.context.toUtf8().constData(), t.source.constData(),
                                                t.disambiguation.isEmpty() ? nullptr : t.disambiguation.constData());
        if (!translated.isEmpty() && placeholders(translated) != placeholders(source)) {
            wrong << source;
        }
    }
    QVERIFY2(wrong.isEmpty(), qPrintable("Placeholders differ: " + wrong.join(" | ")));
}
