#include "tests.h"
#include "testdataobject.h"
#include "localrdp-shared/clip/clipreader_win32.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace LocalRdp::Clip;

static void writeFile(const QString& path, int size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QByteArray(size, 'q'));
}

void TestClipReader::textOffer()
{
    TestDataObject data;
    data.setText("Привет, ПК!");
    const LocalSnapshot s = readDataObject(&data, 7);
    QCOMPARE(s.offer.seq, quint64(7));
    QVERIFY(!s.excluded);
    QCOMPARE(*s.offer.textBytes, quint64(QString("Привет, ПК!").size() * 2));
    QCOMPARE(QString::fromUtf16((const char16_t*)s.text.constData(), s.text.size() / 2), QString("Привет, ПК!"));
    QVERIFY(!s.offer.imageBytes.has_value());
    QVERIFY(s.offer.files.isEmpty());
}

void TestClipReader::dibOffer()
{
    TestDataObject data;
    const QByteArray dib(4096, 'd');
    data.set(CF_DIB, dib);
    const LocalSnapshot s = readDataObject(&data, 1);
    QCOMPARE(*s.offer.imageBytes, quint64(4096));
    QCOMPARE(s.dib, dib);
}

void TestClipReader::pngOffer()
{
    TestDataObject data;
    const QByteArray png("\x89PNG\r\n\x1a\nfake", 12);
    data.set((CLIPFORMAT)RegisterClipboardFormatW(L"PNG"), png);
    const LocalSnapshot s = readDataObject(&data, 1);
    QCOMPARE(*s.offer.pngBytes, quint64(png.size()));
    QCOMPARE(s.png, png);
}

void TestClipReader::filesAndNestedFolders()
{
    QTemporaryDir dir;
    writeFile(dir.filePath("single.txt"), 10);
    writeFile(dir.filePath("Папка/a.bin"), 20);
    writeFile(dir.filePath("Папка/sub/b.bin"), 30);
    QDir().mkpath(dir.filePath("Папка/empty"));

    TestDataObject data;
    data.setFiles({ dir.filePath("single.txt"), dir.filePath("Папка") });
    const LocalSnapshot s = readDataObject(&data, 3);

    QStringList names;
    for (const FileEntry& e : s.offer.files) {
        names << QString("%1|%2|%3").arg(e.name).arg(e.size).arg(e.isDir);
    }
    names.sort();
    QCOMPARE(names, QStringList({
        "single.txt|10|0",
        "Папка\\a.bin|20|0",
        "Папка\\empty|0|1",
        "Папка\\sub\\b.bin|30|0",
        "Папка\\sub|0|1",
        "Папка|0|1",
    }));
    QCOMPARE(s.filePaths.size(), s.offer.files.size());
    for (int i = 0; i < s.offer.files.size(); i++) {
        if (s.offer.files[i].isDir) {
            QVERIFY(s.filePaths[i].isEmpty());
        } else {
            QVERIFY(QFile::exists(s.filePaths[i]));
            QCOMPARE(QFileInfo(s.filePaths[i]).size(), (qint64)s.offer.files[i].size);
        }
    }
    // A folder always comes before what is inside it (Explorer creates it first)
    for (int i = 0; i < s.offer.files.size(); i++) {
        const QString name = s.offer.files[i].name;
        const int slash = name.lastIndexOf('\\');
        if (slash > 0) {
            const QString parent = name.left(slash);
            bool seenBefore = false;
            for (int j = 0; j < i; j++) {
                seenBefore |= s.offer.files[j].name == parent;
            }
            QVERIFY2(seenBefore, qPrintable(name));
        }
    }
}

void TestClipReader::excludedFormatMarksExcluded()
{
    TestDataObject data;
    data.setText("secret password");
    data.set((CLIPFORMAT)RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"), QByteArray(4, 0));
    const LocalSnapshot s = readDataObject(&data, 1);
    QVERIFY(s.excluded);
    QVERIFY(s.text.isEmpty());
}

void TestClipReader::historyZeroMarksExcluded()
{
    TestDataObject data;
    data.setText("secret password");
    data.setDword(L"CanIncludeInClipboardHistory", 0);
    QVERIFY(readDataObject(&data, 1).excluded);

    TestDataObject allowed;
    allowed.setText("normal");
    allowed.setDword(L"CanIncludeInClipboardHistory", 1);
    QVERIFY(!readDataObject(&allowed, 1).excluded);
}

void TestClipReader::tooManyFilesGivesNoFiles()
{
    QTemporaryDir dir;
    for (int i = 0; i < kMaxFiles + 5; i++) {
        QFile f(dir.filePath(QString("many/f%1").arg(i)));
        if (i == 0) QDir().mkpath(dir.filePath("many"));
        f.open(QIODevice::WriteOnly);
    }
    TestDataObject data;
    data.setText("also text");
    data.setFiles({ dir.filePath("many") });
    const LocalSnapshot s = readDataObject(&data, 1);
    QVERIFY(s.offer.files.isEmpty());
    QVERIFY(s.offer.textBytes.has_value()); // the rest still works
}
