#include "smoke/journal.hpp"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace smoke {
namespace {

// Escape newlines so one journal line is always one key.
std::string escape(const std::string& key)
{
    std::string out;
    out.reserve(key.size());
    for (char c : key)
    {
        if (c == '\n')
        {
            out += "\\n";
        }
        else if (c == '\\')
        {
            out += "\\\\";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

}  // namespace

// The lock file holds the owner PID; a lock left by a process that no longer
// exists (kill -9, crash) may be reclaimed.
bool Journal::owner_is_dead() const
{
    std::ifstream in(lock_path_);
    long long pid = 0;
    if (!(in >> pid) || pid <= 0)
    {
        return true;  // unreadable or empty lock: nothing to honor
    }
#ifdef _WIN32
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (handle == nullptr)
    {
        return true;
    }
    DWORD code = 0;
    const bool exited =
        ::GetExitCodeProcess(handle, &code) && code != STILL_ACTIVE;
    ::CloseHandle(handle);
    return exited;
#else
    return ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH;
#endif
}

Journal::~Journal()
{
    close();
}

bool Journal::open(const std::string& journal_path)
{
    lock_path_ = journal_path + ".lock";
    for (int attempt = 0; attempt < 2; ++attempt)
    {
#ifdef _WIN32
        HANDLE handle = CreateFileA(
            lock_path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            // Record the owner PID so a crashed owner can be detected later.
            const std::string owner = std::to_string(::_getpid()) + "\n";
            DWORD written = 0;
            WriteFile(handle, owner.c_str(), static_cast<DWORD>(owner.size()),
                      &written, nullptr);
            CloseHandle(handle);
            break;
        }
        if (::GetLastError() != ERROR_FILE_EXISTS)
        {
            lock_path_.clear();
            return false;
        }
#else
        const int fd = ::open(lock_path_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0)
        {
            const std::string owner = std::to_string(::getpid()) + "\n";
            ssize_t unused = ::write(fd, owner.c_str(), owner.size());
            (void)unused;
            ::close(fd);
            break;
        }
        if (errno != EEXIST)
        {
            lock_path_.clear();
            return false;
        }
#endif
        // Lock file exists: if its recorded owner is gone, remove and retry.
        if (!owner_is_dead())
        {
            lock_path_.clear();
            return false;
        }
        std::remove(lock_path_.c_str());
    }
    if (lock_path_.empty())
    {
        return false;
    }

    {
        std::ifstream in(journal_path);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty())
            {
                keys_.insert(line);
                ++count_;
            }
        }
    }
    out_.open(journal_path, std::ios::app);
    if (!out_.is_open())
    {
        close();
        return false;
    }
    return true;
}

bool Journal::append(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    out_ << escape(key) << '\n';
    out_.flush();
    if (!out_)
    {
        return false;
    }
    keys_.insert(key);
    ++count_;
    return true;
}

bool Journal::append_if_absent(const std::string& key)
{
    // One lock covers check and append so concurrent retries of the same
    // event cannot both be accepted.
    std::lock_guard<std::mutex> lock(mutex_);
    if (keys_.count(key) != 0)
    {
        return false;
    }
    out_ << escape(key) << '\n';
    out_.flush();
    if (!out_)
    {
        return false;
    }
    keys_.insert(key);
    ++count_;
    return true;
}

bool Journal::seen(const std::string& key) const
{
    return keys_.count(key) != 0;
}

void Journal::close()
{
    if (out_.is_open())
    {
        out_.close();
    }
    if (!lock_path_.empty())
    {
        std::remove(lock_path_.c_str());
        lock_path_.clear();
    }
}

}  // namespace smoke
