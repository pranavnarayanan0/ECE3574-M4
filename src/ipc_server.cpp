
#include "aiws/ipc_server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <string>
#include <utility>
#include <vector>

namespace aiws {

namespace {

constexpr std::uint32_t MAX_PATH_BYTES = 65536;
constexpr int POLL_INTERVAL_MS = 100;
constexpr int COMMUNICATION_TIMEOUT_MS = 5000;

using Clock = std::chrono::steady_clock;

// Convert a 32-bit number into four network-order bytes.
void encode_length(std::uint32_t value, unsigned char* bytes) {
    bytes[0] = static_cast<unsigned char>(value >> 24);
    bytes[1] = static_cast<unsigned char>(value >> 16);
    bytes[2] = static_cast<unsigned char>(value >> 8);
    bytes[3] = static_cast<unsigned char>(value);
}

// Convert four network-order bytes back into a number.
std::uint32_t decode_length(const unsigned char* bytes) {
    return (std::uint32_t(bytes[0]) << 24) |
           (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) |
            std::uint32_t(bytes[3]);
}

// Read exactly count bytes, even if they arrive in pieces.
bool read_exact(int fd, void* destination, std::size_t count,
                const std::atomic<bool>& running,
                Clock::time_point deadline) {
    auto* buffer = static_cast<unsigned char*>(destination);
    std::size_t received = 0;

    while (received < count && running.load()) {
        if (Clock::now() >= deadline) {
            return false;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        int result = poll(&pfd, 1, POLL_INTERVAL_MS);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (result == 0) {
            continue;
        }

        ssize_t n = recv(fd, buffer + received,
                         count - received, MSG_DONTWAIT);

        if (n > 0) {
            received += static_cast<std::size_t>(n);
        } else if (n == 0) {
            return false;
        } else if (errno != EINTR &&
                   errno != EAGAIN &&
                   errno != EWOULDBLOCK) {
            return false;
        }
    }

    return received == count;
}

// Write the entire response, including partial writes.
bool write_exact(int fd, const void* source, std::size_t count,
                 const std::atomic<bool>& running,
                 Clock::time_point deadline) {
    const auto* buffer = static_cast<const unsigned char*>(source);
    std::size_t sent = 0;

    while (sent < count && running.load()) {
        if (Clock::now() >= deadline) {
            return false;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;

        int result = poll(&pfd, 1, POLL_INTERVAL_MS);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (result == 0) {
            continue;
        }

        ssize_t n = send(fd, buffer + sent, count - sent,
                         MSG_NOSIGNAL | MSG_DONTWAIT);

        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n == 0) {
            return false;
        } else if (errno != EINTR &&
                   errno != EAGAIN &&
                   errno != EWOULDBLOCK) {
            return false;
        }
    }

    return sent == count;
}

// Send one status byte, four length bytes, and an error message.
void send_reply(int fd, bool accepted,
                const std::string& message,
                const std::atomic<bool>& running,
                Clock::time_point deadline) {
    std::vector<unsigned char> reply(5 + message.size());

    reply[0] = accepted ? 0 : 1;

    encode_length(
        static_cast<std::uint32_t>(message.size()),
        reply.data() + 1
    );

    std::copy(message.begin(), message.end(), reply.begin() + 5);

    write_exact(fd, reply.data(), reply.size(), running, deadline);
}

// Check that the requested file can be opened for reading.
bool file_is_readable(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos) {
        return false;
    }

    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return false;
    }

    struct stat info{};
    bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode);

    close(fd);
    return valid;
}

} // anonymous namespace

IpcServer::IpcServer(
    const std::string& socket_path,
    RequestHandler handler)
    : socket_path_(socket_path),
      handler_(std::move(handler)) {
}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    if (running_ || listen_fd_ != -1 || !handler_) {
        return false;
    }

    // Unix socket paths have a fixed maximum size.
    sockaddr_un address{};

    if (socket_path_.empty() ||
        socket_path_.size() >= sizeof(address.sun_path)) {
        return false;
    }

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);

    if (listen_fd_ < 0) {
        return false;
    }

    address.sun_family = AF_UNIX;

    std::memcpy(
        address.sun_path,
        socket_path_.c_str(),
        socket_path_.size() + 1
    );

    // Do not delete an existing socket belonging to another process.
    if (bind(listen_fd_,
             reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 8) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }

    running_ = true;

    try {
        server_thread_ = std::thread(&IpcServer::serve, this);
    } catch (...) {
        running_ = false;
        close(listen_fd_);
        listen_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }

    return true;
}

void IpcServer::serve() {
    while (running_) {
        pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;

        int ready = poll(&pfd, 1, POLL_INTERVAL_MS);

        if (!running_) {
            break;
        }

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            continue;
        }

        if (!(pfd.revents & POLLIN)) {
            break;
        }

        int client_fd = accept4(
            listen_fd_, nullptr, nullptr,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );

        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }

        auto deadline = Clock::now() +
            std::chrono::milliseconds(COMMUNICATION_TIMEOUT_MS);

        // First read the four-byte path length.
        unsigned char header[4]{};

        if (!read_exact(client_fd, header, sizeof(header),
                        running_, deadline)) {
            close(client_fd);
            continue;
        }

        std::uint32_t length = decode_length(header);

        if (length == 0 || length > MAX_PATH_BYTES) {
            send_reply(client_fd, false, "Invalid path length",
                       running_, deadline);
            close(client_fd);
            continue;
        }

        // Read the exact number of path bytes.
        std::string path(length, '\0');

        if (!read_exact(client_fd, path.data(), path.size(),
                        running_, deadline)) {
            close(client_fd);
            continue;
        }

        if (!file_is_readable(path)) {
            send_reply(client_fd, false,
                       "File is not readable",
                       running_, deadline);
            close(client_fd);
            continue;
        }

        bool accepted = false;
        std::string error_message;

        try {
            // The handler must enqueue the GUI update, not wait for it.
            accepted = handler_(path);

            if (!accepted) {
                error_message = "Document request rejected";
            }
        } catch (...) {
            accepted = false;
            error_message = "Error handling document request";
        }

        send_reply(client_fd, accepted, error_message,
                   running_, deadline);

        close(client_fd);
    }
}

void IpcServer::stop() {
    running_ = false;

    // Polling wakes periodically, allowing the server to stop.
    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    if (listen_fd_ != -1) {
        close(listen_fd_);
        listen_fd_ = -1;

        // Remove only the socket path that this server created.
        unlink(socket_path_.c_str());
    }
}

} // namespace aiws
