#ifndef WESTEROS_GL_DRM_DISPLAY_SERVER_MOCK_H
#define WESTEROS_GL_DRM_DISPLAY_SERVER_MOCK_H

#include <string>
#include <thread>

class DisplayServerMock
{
public:
    DisplayServerMock(const std::string& runtimeDirectory,
                      const std::string& responseBody,
                      bool validHeader = true,
                      bool sendResponse = true,
                      bool validLength = true);
    ~DisplayServerMock();

    DisplayServerMock(const DisplayServerMock&) = delete;
    DisplayServerMock& operator=(const DisplayServerMock&) = delete;

    bool WaitForRequest();
    std::string Request() const;

private:
    void Serve(const std::string& responseBody,
               bool validHeader,
               bool sendResponse,
               bool validLength);

    int listenFd_;
    std::thread thread_;
    std::string request_;
    bool receivedRequest_ = false;
};

#endif