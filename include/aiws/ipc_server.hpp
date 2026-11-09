#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace aiws {

class IpcServer {
public:
    using RequestHandler =
        std::function<bool(const std::string&)>;

    explicit IpcServer(
        const std::string& socket_path,
        RequestHandler handler
    );

    ~IpcServer();

    bool start();
    void stop();

private:
    void serve();

    std::string socket_path_;
    RequestHandler handler_;

    std::thread server_thread_;
    std::atomic<bool> running_{false};

    int listen_fd_{-1};
};

} // namespace aiws