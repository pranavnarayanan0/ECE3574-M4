#include <QtTest/QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
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
    void initTestCase();
    void testExitWithIdleWorker();
    void testDocumentAddition();
    void testBackgroundBuild();
    void testStaleBuildRejection();
    void testResetDuringBuild();
    void testResetReleasesWorkerForNextBuild();
    void testRepeatedBuilds();
    void testBuildFailureRecovery();
};

void TestM4::initTestCase()
{
    // The child executes only testExitWithIdleWorker().
    if (qEnvironmentVariableIsSet("AIWS_IDLE_EXIT_CHILD")) {
        return;
    }

    // Check shutdown before the other tests. Several of them destroy
    // windows with idle workers and could hang if shutdown is broken.
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("AIWS_IDLE_EXIT_CHILD", "1");

    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(
        QCoreApplication::applicationFilePath(),
        QStringList{"testExitWithIdleWorker"}
    );

    QVERIFY2(child.waitForStarted(3000), qPrintable(child.errorString()));

    if (!child.waitForFinished(8000)) {
        child.kill();
        child.waitForFinished(2000);
        QFAIL("The idle-exit check hung. Check that shutdown wakes the "
              "worker before joining it.");
    }

    const QByteArray output = child.readAll();
    QVERIFY2(
        child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
        output.constData()
    );
}

void TestM4::testExitWithIdleWorker()
{
    if (!qEnvironmentVariableIsSet("AIWS_IDLE_EXIT_CHILD")) {
        QSKIP("The parent already checked idle shutdown in initTestCase.");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = createDocument(
        directory.path(),
        "idle.txt",
        "alpha beta gamma"
    );
    QVERIFY(!path.isEmpty());

    MainWindow window;
    QVERIFY(window.add_document_file(path));
    QVERIFY(startBuild(window));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Corpus built successfully"),
        3000
    );

    // Completing the first build leaves the reusable worker idle.
    // The parent enforces a deadline if shutdown blocks here.
    window.request_shutdown();
}

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

    // Modify the workspace during the active build.
    QVERIFY(window.add_document_file(second));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Workspace changed during build"),
        3000
    );

    QVERIFY(!hasLabelText(window, "Corpus built successfully"));
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

    QVERIFY(QMetaObject::invokeMethod(
        &window,
        "reset_application",
        Qt::DirectConnection
    ));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Workspace changed during build"),
        3000
    );

    QVERIFY(!hasLabelText(window, "Corpus built successfully"));
}

void TestM4::testResetReleasesWorkerForNextBuild()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    MainWindow window;
    window.set_build_delay_ms(250);

    // An uninterrupted build takes at least five seconds.
    for (int i = 0; i < 20; ++i) {
        const QString path = createDocument(
            directory.path(),
            QString("old_%1.txt").arg(i),
            "alpha beta gamma"
        );

        QVERIFY(!path.isEmpty());
        QVERIFY(window.add_document_file(path));
    }

    const QString freshPath = createDocument(
        directory.path(),
        "fresh.txt",
        "fresh document"
    );
    QVERIFY(!freshPath.isEmpty());

    QVERIFY(startBuild(window));
    QVERIFY(hasLabelText(window, "Building corpus..."));

    // Give the worker time to pick up the old build before Reset.
    QTest::qWait(500);

    // Include Reset itself in the time limit. A blocking Reset would
    // also make the user wait for the obsolete build.
    QElapsedTimer elapsed;
    elapsed.start();

    QVERIFY(QMetaObject::invokeMethod(
        &window,
        "reset_application",
        Qt::DirectConnection
    ));

    window.set_build_delay_ms(0);
    QVERIFY(window.add_document_file(freshPath));
    QVERIFY(startBuild(window));

    // Process Qt events so the fresh build can report completion.
    constexpr int deadlineMs = 2000;
    while (elapsed.elapsed() < deadlineMs &&
           !hasLabelText(window, "Corpus built successfully")) {
        QTest::qWait(20);
    }

    QVERIFY2(
        hasLabelText(window, "Corpus built successfully") &&
            elapsed.elapsed() < deadlineMs,
        "Reset and the fresh build took too long; the worker may still "
        "be processing the obsolete workspace."
    );
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

    // ProcessingCore::rebuild() rejects duplicate document IDs.
    QVERIFY(window.add_document_file(path));
    QVERIFY(window.add_document_file(path));

    // Automatically dismiss the expected build-failure dialog.
    QTimer dialogCloser;

    QObject::connect(
        &dialogCloser,
        &QTimer::timeout,
        []() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);

                if (message && message->isVisible()) {
                    message->accept();
                }
            }
        }
    );

    dialogCloser.start(25);
    QVERIFY(startBuild(window));

    QTRY_VERIFY_WITH_TIMEOUT(
        hasLabelText(window, "Corpus build failed."),
        2500
    );

    dialogCloser.stop();
}

QTEST_MAIN(TestM4)

#include "student_tests.moc"