/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/
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