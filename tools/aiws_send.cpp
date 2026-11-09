
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <climits>
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(2);
constexpr auto COMM_TIMEOUT = std::chrono::seconds(5);

// Wait for socket readiness without exceeding the deadline.
bool wait_for_socket(int fd, short events, Deadline deadline) {
    while (true) {
        auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(deadline - Clock::now());

        if (remaining.count() <= 0) {
            return false;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = events;

        int result = poll(
            &pfd,
            1,
            static_cast<int>(
                std::min<long long>(
                    remaining.count(),
                    INT_MAX
                )
            )
        );

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (result == 0) {
            return false;
        }

        if (pfd.revents & POLLNVAL) {
            return false;
        }

        if (pfd.revents & (events | POLLERR | POLLHUP)) {
            return true;
        }
    }
}

// Connect using a nonblocking socket and a deadline.
bool connect_with_timeout(
    int fd,
    const sockaddr_un& address,
    Deadline deadline
) {
    int result = connect(
        fd,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );

    if (result == 0) {
        return true;
    }

    if (errno != EINPROGRESS &&
        errno != EINTR) {
        return false;
    }

    if (!wait_for_socket(fd, POLLOUT, deadline)) {
        return false;
    }

    int socket_error = 0;
    socklen_t length = sizeof(socket_error);

    if (getsockopt(
            fd,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &length
        ) < 0) {
        return false;
    }

    return socket_error == 0;
}

// Send every byte, even if send() writes only part of the data.
bool send_all(
    int fd,
    const void* data,
    std::size_t size,
    Deadline deadline
) {
    const char* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;

    while (sent < size) {
        if (!wait_for_socket(fd, POLLOUT, deadline)) {
            return false;
        }

        ssize_t n = send(
            fd,
            bytes + sent,
            size - sent,
            MSG_NOSIGNAL | MSG_DONTWAIT
        );

        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        }
        else if (n == 0) {
            return false;
        }
        else if (errno != EINTR &&
                 errno != EAGAIN &&
                 errno != EWOULDBLOCK) {
            return false;
        }
    }

    return true;
}

// Receive exactly the requested number of bytes.
bool receive_all(
    int fd,
    void* data,
    std::size_t size,
    Deadline deadline
) {
    char* bytes = static_cast<char*>(data);
    std::size_t received = 0;

    while (received < size) {
        if (!wait_for_socket(fd, POLLIN, deadline)) {
            return false;
        }

        ssize_t n = recv(
            fd,
            bytes + received,
            size - received,
            MSG_DONTWAIT
        );

        if (n > 0) {
            received += static_cast<std::size_t>(n);
        }
        else if (n == 0) {
            return false;
        }
        else if (errno != EINTR &&
                 errno != EAGAIN &&
                 errno != EWOULDBLOCK) {
            return false;
        }
    }

    return true;
}

// Decode a 4-byte big-endian length.
std::uint32_t decode_length(const unsigned char* bytes) {
    return (std::uint32_t(bytes[0]) << 24) |
           (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) |
            std::uint32_t(bytes[3]);
}

} // namespace

int main(int argc, char* argv[]) {

    // Determine the default socket path.
    std::string socket_path;

    const char* user = std::getenv("USER");

    if (user && *user) {
        socket_path = "/tmp/aiws-" +
                      std::string(user) + ".sock";
    }
    else {
        socket_path = "/tmp/aiws-" +
                      std::to_string(getuid()) + ".sock";
    }

    // Parse optional --socket argument.
    int index = 1;

    if (argc > 1 &&
        std::strcmp(argv[index], "--socket") == 0) {

        if (argc <= index + 1) {
            std::cerr << "Missing socket path\n";
            return 1;
        }

        socket_path = argv[index + 1];
        index += 2;
    }

    // Validate command syntax.
    if (argc - index != 2 ||
        std::strcmp(argv[index], "add") != 0) {

        std::cerr
            << "Usage: aiws_send [--socket PATH] add FILE\n";

        return 1;
    }

    // Resolve relative paths using the sender's directory.
    std::string path;

    try {
        path = std::filesystem::absolute(
            std::filesystem::path(argv[index + 1])
        ).string();
    }
    catch (...) {
        std::cerr << "Invalid file path\n";
        return 1;
    }

    // Prepare the Unix-domain socket address.
    sockaddr_un address{};

    if (socket_path.empty() ||
        socket_path.size() >= sizeof(address.sun_path)) {

        std::cerr << "Invalid socket path\n";
        return 2;
    }

    address.sun_family = AF_UNIX;

    std::memcpy(
        address.sun_path,
        socket_path.c_str(),
        socket_path.size() + 1
    );

    // Create the socket.
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        std::cerr << "Cannot create socket\n";
        return 2;
    }

    // Configure nonblocking operation.
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {

        std::cerr << "Cannot configure socket\n";
        close(fd);
        return 2;
    }

    // Connect within two seconds.
    Deadline connect_deadline =
        Clock::now() + CONNECT_TIMEOUT;

    if (!connect_with_timeout(
            fd,
            address,
            connect_deadline)) {

        std::cerr << "Cannot connect to AI Workspace\n";
        close(fd);
        return 2;
    }

    // Five-second deadline for the entire request/reply.
    Deadline communication_deadline =
        Clock::now() + COMM_TIMEOUT;

    // Reject paths that exceed the server's protocol limit.
    if (path.empty() || path.size() > 65536) {
        std::cerr << "Invalid document path length\n";
        close(fd);
        return 1;
    }

    // Encode the path length as four big-endian bytes.
    std::uint32_t length =
        static_cast<std::uint32_t>(path.size());

    unsigned char header[4] = {
        static_cast<unsigned char>(length >> 24),
        static_cast<unsigned char>(length >> 16),
        static_cast<unsigned char>(length >> 8),
        static_cast<unsigned char>(length)
    };

    // Send the header followed by the file path.
    if (!send_all(
            fd,
            header,
            sizeof(header),
            communication_deadline) ||

        !send_all(
            fd,
            path.data(),
            path.size(),
            communication_deadline)) {

        std::cerr << "Failed to send request\n";
        close(fd);
        return 2;
    }

    // Receive response:
    // 1 status byte + 4 message-length bytes.
    unsigned char reply_header[5]{};

    if (!receive_all(
            fd,
            reply_header,
            sizeof(reply_header),
            communication_deadline)) {

        std::cerr << "Failed to receive response\n";
        close(fd);
        return 2;
    }

    std::uint32_t message_length =
        decode_length(reply_header + 1);

    if (message_length > 65536) {
        std::cerr << "Invalid server response\n";
        close(fd);
        return 2;
    }

    // Receive the optional response message.
    std::string message(message_length, '\0');

    if (!receive_all(
            fd,
            message.data(),
            message.size(),
            communication_deadline)) {

        std::cerr << "Incomplete server response\n";
        close(fd);
        return 2;
    }

    close(fd);

    // Exit code 0: request accepted.
    if (reply_header[0] == 0) {
        return 0;
    }

    // Exit code 1: request rejected.
    if (reply_header[0] == 1) {
        std::cerr << message << '\n';
        return 1;
    }

    // Exit code 2: communication/protocol failure.
    std::cerr << "Unknown server status\n";
    return 2;
}
