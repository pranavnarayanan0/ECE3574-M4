
#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_server.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QString>

#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <string>

// Function-pointer callback for Exit
void confirm_exit(QWidget* parent) {
    QMessageBox::StandardButton answer =
        QMessageBox::question(
            parent,
            "Confirm Exit",
            "Are you sure you want to exit?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

    if (answer == QMessageBox::Yes) {
        auto* window =
            qobject_cast<aiws::gui::MainWindow*>(parent);

        if (window != nullptr) {
            window->request_shutdown();
        }
    }
}

// Default socket path required by CR-1
std::string default_socket_path() {
    const char* user = std::getenv("USER");

    if (user != nullptr && *user != '\0') {
        return "/tmp/aiws-" + std::string(user) + ".sock";
    }

    return "/tmp/aiws-" +
           std::to_string(getuid()) + ".sock";
}

int main(int argc, char* argv[]) {
    std::string socket_path = default_socket_path();
    int build_delay = 0;

    // Parse M4 and CR-1 command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];

        if (argument == "--ipc-socket") {
            if (i + 1 >= argc) {
                std::cerr << "Missing socket path\n";
                return 1;
            }

            socket_path = argv[++i];
        }
        else if (argument == "--build-delay-ms") {
            if (i + 1 >= argc) {
                std::cerr << "Missing build delay\n";
                return 1;
            }

            const char* value = argv[++i];
            char* end = nullptr;

            errno = 0;
            long parsed = std::strtol(value, &end, 10);

            if (errno != 0 || end == value ||
                *end != '\0' ||
                parsed < 0 || parsed > INT_MAX) {
                std::cerr << "Invalid build delay\n";
                return 1;
            }

            build_delay = static_cast<int>(parsed);
        }
    }

    QApplication app(argc, argv);

    aiws::gui::MainWindow window;

    window.set_exit_callback(&confirm_exit);
    window.set_build_delay_ms(build_delay);

    // CR-1 socket server
    aiws::IpcServer server(
        socket_path,
        [&window](const std::string& path) -> bool {

            // Queue document addition on the GUI thread.
            // The server thread must not modify GUI widgets.
            bool queued = QMetaObject::invokeMethod(
                &window,
                [&window, path]() {
                    window.add_document_file(
                        QString::fromUtf8(
                            path.data(),
                            static_cast<qsizetype>(path.size())
                        )
                    );
                },
                Qt::QueuedConnection
            );

            // Return immediately without waiting for the GUI.
            return queued;
        }
    );

    if (!server.start()) {
        std::cerr << "Failed to start IPC server\n";
        return 1;
    }

    window.show();

    int result = app.exec();

    // Stop receiving requests and remove the socket.
    server.stop();

    return result;
}
