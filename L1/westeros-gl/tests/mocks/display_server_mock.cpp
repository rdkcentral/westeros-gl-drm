#include "display_server_mock.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

DisplayServerMock::DisplayServerMock(const std::string& runtimeDirectory,
                                     const std::string& responseBody,
                                     bool validHeader,
                                     bool sendResponse,
                                     bool validLength)
    : listenFd_(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0))
{
    if (listenFd_ < 0)
    {
        return;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string socketPath = runtimeDirectory + "/display";
    if (socketPath.size() >= sizeof(address.sun_path))
    {
        close(listenFd_);
        listenFd_ = -1;
        return;
    }
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);

    unlink(socketPath.c_str());
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listenFd_, 1) != 0)
    {
        close(listenFd_);
        listenFd_ = -1;
        return;
    }

    thread_ = std::thread(&DisplayServerMock::Serve, this, responseBody,
                          validHeader, sendResponse, validLength);
}

DisplayServerMock::~DisplayServerMock()
{
    if (thread_.joinable())
    {
        thread_.join();
    }
    if (listenFd_ >= 0)
    {
        close(listenFd_);
    }
}

void DisplayServerMock::Serve(const std::string& responseBody,
                              bool validHeader,
                              bool sendResponse,
                              bool validLength)
{
    pollfd listenEvent{listenFd_, POLLIN, 0};
    if (poll(&listenEvent, 1, 2000) <= 0)
    {
        return;
    }

    const int clientFd = accept(listenFd_, nullptr, nullptr);
    if (clientFd < 0)
    {
        return;
    }

    unsigned char header[3]{};
    size_t received = 0;
    while (received < sizeof(header))
    {
        const ssize_t count = recv(clientFd, header + received, sizeof(header) - received, 0);
        if (count <= 0)
        {
            close(clientFd);
            return;
        }
        received += static_cast<size_t>(count);
    }

    const size_t payloadSize = header[2];
    std::vector<unsigned char> payload(payloadSize);
    received = 0;
    while (received < payload.size())
    {
        const ssize_t count = recv(clientFd, payload.data() + received,
                                   payload.size() - received, 0);
        if (count <= 0)
        {
            close(clientFd);
            return;
        }
        received += static_cast<size_t>(count);
    }

    if (header[0] == 'D' && header[1] == 'S')
    {
        receivedRequest_ = true;
        if (!payload.empty() && payload.back() == '\0')
        {
            request_.assign(reinterpret_cast<const char*>(payload.data()), payload.size() - 1);
        }
    }

    if (!sendResponse)
    {
        close(clientFd);
        return;
    }

    const size_t responseSize = std::min<size_t>(responseBody.size(), 254);
    std::vector<unsigned char> responseFrame = {
        static_cast<unsigned char>(validHeader ? 'D' : 'X'),
        'S',
        static_cast<unsigned char>(responseSize + 1)};
    if (!validLength)
    {
        responseFrame[2] += 2;
    }
    responseFrame.insert(responseFrame.end(), responseBody.begin(), responseBody.begin() + responseSize);
    responseFrame.push_back('\0');
    size_t sent = 0;
    while (sent < responseFrame.size())
    {
        const ssize_t count = send(clientFd, responseFrame.data() + sent,
                                   responseFrame.size() - sent, MSG_NOSIGNAL);
        if (count <= 0)
        {
            break;
        }
        sent += static_cast<size_t>(count);
    }
    close(clientFd);
}

bool DisplayServerMock::WaitForRequest()
{
    if (thread_.joinable())
    {
        thread_.join();
    }
    return receivedRequest_;
}

std::string DisplayServerMock::Request() const
{
    return request_;
}