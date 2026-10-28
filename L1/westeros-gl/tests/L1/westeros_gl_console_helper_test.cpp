#include <gtest/gtest.h>

#include "display_server_mock.h"
// The helper is compiled as C; its header does not declare C++ linkage guards.
extern "C" {
#include "westeros-gl-console-helper.h"
}

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace
{
class RuntimeDirectory
{
public:
    RuntimeDirectory()
    {
        char pattern[] = "/tmp/westeros-gl-drm-l1-XXXXXX";
        char* created = mkdtemp(pattern);
        if (created)
        {
            path_ = created;
        }
    }

    ~RuntimeDirectory()
    {
        if (!path_.empty())
        {
            unlink((path_ + "/display").c_str());
            rmdir(path_.c_str());
        }
    }

    const std::string& Path() const { return path_; }

private:
    std::string path_;
};

class EnvironmentVariable
{
public:
    explicit EnvironmentVariable(const char* name) : name_(name), wasSet_(getenv(name) != nullptr)
    {
        if (wasSet_)
        {
            previous_ = getenv(name);
        }
    }

    ~EnvironmentVariable()
    {
        if (wasSet_)
        {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else
        {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    std::string previous_;
    bool wasSet_;
};
}

TEST(WesterosGlConsoleHelper, SendsFramedCommandAndParsesSuccessfulResponse)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    EnvironmentVariable restoreDebug("WESTEROS_GL_DEBUG");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_DEBUG", "3", 1));
    DisplayServerMock server(runtime.Path(), "0:accepted");

    char command[] = "get-mode";
    char* response = nullptr;
    EXPECT_EQ(0, WstGLConsoleCommand(command, &response));
    ASSERT_TRUE(server.WaitForRequest());
    EXPECT_EQ("get-mode", server.Request());
    ASSERT_NE(nullptr, response);
    EXPECT_STREQ("0:accepted", response);
    free(response);
}

TEST(WesterosGlConsoleHelper, ReturnsServerStatusAndResponseBody)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));
    DisplayServerMock server(runtime.Path(), "7:unsupported");

    char command[] = "set-mode";
    char* response = nullptr;
    EXPECT_EQ(7, WstGLConsoleCommand(command, &response));
    ASSERT_TRUE(server.WaitForRequest());
    ASSERT_NE(nullptr, response);
    EXPECT_STREQ("7:unsupported", response);
    free(response);
}

TEST(WesterosGlConsoleHelper, PreservesDefaultStatusForNonNumericResponse)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));
    DisplayServerMock server(runtime.Path(), "unsupported");

    char command[] = "get-mode";
    char* response = nullptr;
    EXPECT_EQ(0, WstGLConsoleCommand(command, &response));
    ASSERT_TRUE(server.WaitForRequest());
    ASSERT_NE(nullptr, response);
    EXPECT_STREQ("unsupported", response);
    free(response);
}

TEST(WesterosGlConsoleHelper, RejectsResponseWithInvalidProtocolHeader)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));
    DisplayServerMock server(runtime.Path(), "0:ignored", false);

    char command[] = "query";
    char* response = nullptr;
    EXPECT_EQ(-1, WstGLConsoleCommand(command, &response));
    ASSERT_TRUE(server.WaitForRequest());
    EXPECT_EQ("query", server.Request());
    EXPECT_EQ(nullptr, response);
}

TEST(WesterosGlConsoleHelper, RejectsResponseWithInconsistentLength)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));
    DisplayServerMock server(runtime.Path(), "0:short", true, true, false);

    char command[] = "query";
    char* response = nullptr;
    EXPECT_EQ(-1, WstGLConsoleCommand(command, &response));
    ASSERT_TRUE(server.WaitForRequest());
    EXPECT_EQ(nullptr, response);
}

TEST(WesterosGlConsoleHelper, FailsWhenServerClosesWithoutResponding)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));
    DisplayServerMock server(runtime.Path(), "", true, false);

    char command[] = "query";
    char* response = nullptr;
    EXPECT_EQ(-1, WstGLConsoleCommand(command, &response));
    ASSERT_TRUE(server.WaitForRequest());
    EXPECT_EQ("query", server.Request());
    EXPECT_EQ(nullptr, response);
}

TEST(WesterosGlConsoleHelper, FailsWhenRuntimeDirectoryIsNotSet)
{
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, unsetenv("XDG_RUNTIME_DIR"));

    char command[] = "query";
    char* response = nullptr;
    EXPECT_EQ(-1, WstGLConsoleCommand(command, &response));
    EXPECT_EQ(nullptr, response);
}

TEST(WesterosGlConsoleHelper, RejectsOverlongSocketPath)
{
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    const std::string tooLong(200, 'x');
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", tooLong.c_str(), 1));

    char command[] = "query";
    char* response = nullptr;
    EXPECT_EQ(-1, WstGLConsoleCommand(command, &response));
    EXPECT_EQ(nullptr, response);
}

TEST(WesterosGlConsoleHelper, FailsWhenDisplaySocketCannotBeConnected)
{
    RuntimeDirectory runtime;
    ASSERT_FALSE(runtime.Path().empty());
    EnvironmentVariable restore("XDG_RUNTIME_DIR");
    ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", runtime.Path().c_str(), 1));

    char command[] = "query";
    char* response = nullptr;
    EXPECT_EQ(-1, WstGLConsoleCommand(command, &response));
    EXPECT_EQ(nullptr, response);
}