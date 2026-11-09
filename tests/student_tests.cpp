
#include <QtTest/QtTest>

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include "aiws/gui/main_window.hpp"

using aiws::gui::MainWindow;

class TestM4 : public QObject {
    Q_OBJECT

private:
    static QString createDocument(
        const QString& directory,
        const QString& name,
        const QString& contents)
    {
        const QString path = directory + "/" + name;

        QFile file(path);

        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }

        if (file.write(contents.toUtf8()) < 0) {
            return {};
        }

        return path;
    }

    static bool startBuild(MainWindow& window)
    {
        return QMetaObject::invokeMethod(
            &window,
            "build_corpus",
            Qt::DirectConnection
        );
    }

    static bool hasLabelText(
        const MainWindow& window,
        const QString& expected)
    {
        for (QLabel* label : window.findChildren<QLabel*>()) {
            if (label->text().contains(expected)) {
                return true;
            }
        }

        return false;
    }

private slots:
    void testDocumentAddition();
    void testBackgroundBuild();
    void testStaleBuildRejection();
    void testResetDuringBuild();
    void testRepeatedBuilds();
    void testBuildFailureRecovery();
};

void TestM4::testDocumentAddition()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = createDocument(
        directory.path(),
        "document.txt",
        "alpha beta gamma"
    );

    QVERIFY(!path.isEmpty());

    MainWindow window;

    QVERIFY(window.add_document_file(path));

    // A nonexistent file must be rejected.
    QVERIFY(!window.add_document_file(
        directory.path() + "/missing.txt"
    ));
}

void TestM4::testBackgroundBuild()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = createDocument(
        directory.path(),
        "document.txt",
        "alpha beta gamma delta"
    );

    QVERIFY(!path.isEmpty());

    MainWindow window;
    window.set_build_delay_ms(300);

    QVERIFY(window.add_document_file(path));
    QVERIFY(startBuild(window));

    // Build Corpus should return while the worker is active.
    QVERIFY(hasLabelText(window, "Building"));

    // Verify that the GUI event loop continues to process events.
    bool processed = false;

    QTimer::singleShot(0, &window, [&processed]() {
        processed = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(processed, 200);

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Corpus built successfully"),
        3000
    );
}

void TestM4::testStaleBuildRejection()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString first = createDocument(
        directory.path(),
        "first.txt",
        "alpha beta gamma"
    );

    const QString second = createDocument(
        directory.path(),
        "second.txt",
        "delta epsilon zeta"
    );

    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    MainWindow window;
    window.set_build_delay_ms(300);

    QVERIFY(window.add_document_file(first));
    QVERIFY(startBuild(window));

    // Modify the Workspace during the active build.
    QVERIFY(window.add_document_file(second));

    // A completed build from the old Workspace must be rejected.
    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(
            window,
            "Workspace changed during build"
        ),
        3000
    );

    QVERIFY(!hasLabelText(
        window,
        "Corpus built successfully"
    ));
}

void TestM4::testResetDuringBuild()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = createDocument(
        directory.path(),
        "document.txt",
        "alpha beta gamma"
    );

    QVERIFY(!path.isEmpty());

    MainWindow window;
    window.set_build_delay_ms(300);

    QVERIFY(window.add_document_file(path));
    QVERIFY(startBuild(window));

    // Reset while a background build is running.
    QVERIFY(QMetaObject::invokeMethod(
        &window,
        "reset_application",
        Qt::DirectConnection
    ));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(
            window,
            "Workspace changed during build"
        ),
        3000
    );

    QVERIFY(!hasLabelText(
        window,
        "Corpus built successfully"
    ));
}

void TestM4::testRepeatedBuilds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = createDocument(
        directory.path(),
        "document.txt",
        "alpha beta gamma"
    );

    QVERIFY(!path.isEmpty());

    MainWindow window;
    window.set_build_delay_ms(150);

    QVERIFY(window.add_document_file(path));

    QVERIFY(startBuild(window));

    // Repeated build requests should not launch competing workers.
    QVERIFY(startBuild(window));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Corpus built successfully"),
        3000
    );

    // A new build after completion should also succeed.
    QVERIFY(startBuild(window));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Building corpus..."),
        1000
    );

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Corpus built successfully"),
        3000
    );
}

void TestM4::testBuildFailureRecovery()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = createDocument(
        directory.path(),
        "duplicate.txt",
        "alpha beta gamma"
    );

    QVERIFY(!path.isEmpty());

    MainWindow window;
    window.set_build_delay_ms(100);

    // Adding the same file twice should produce duplicate IDs.
    // ProcessingCore::rebuild() rejects duplicate document IDs.
    QVERIFY(window.add_document_file(path));
    QVERIFY(window.add_document_file(path));

    // Automatically dismiss the expected build-failure dialog.
    QTimer dialogCloser;

    QObject::connect(
        &dialogCloser,
        &QTimer::timeout,
        []() {
            for (QWidget* widget :
                 QApplication::topLevelWidgets()) {

                auto* message =
                    qobject_cast<QMessageBox*>(widget);

                if (message && message->isVisible()) {
                    message->accept();
                }
            }
        }
    );

    dialogCloser.start(25);

    QVERIFY(startBuild(window));

    // A failed build must be reported instead of leaving
    // the application permanently in its building state.
    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Corpus build failed."),
        2500
    );

    dialogCloser.stop();
}

QTEST_MAIN(TestM4)

#include "student_tests.moc"
