#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
static std::string last_error() { return std::to_string(static_cast<int>(WSAGetLastError())); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
static void close_socket(socket_t s) { ::close(s); }
static std::string last_error() { return std::strerror(errno); }
#endif

#include "smoke/core.hpp"
#include "smoke/journal.hpp"
#include "smoke/protocol.hpp"

namespace {

struct ClientState
{
    socket_t sock = kInvalidSocket;
    std::uint64_t id = 0;
    std::string recv_buffer;
    std::deque<std::string> send_queue;
    std::mutex send_mutex;
    std::condition_variable send_cv;
    bool writer_running = false;
    bool closing = false;
    std::uint64_t idle_deadline_ms = 0;
    std::size_t pending_requests = 0;
    bool idle_timer_armed = false;
};

std::atomic<bool> g_shutdown{false};
std::atomic<std::uint64_t> g_next_client_id{1};

void signal_handler(int)
{
    g_shutdown.store(true);
}

std::int64_t now_ms()
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void install_signal_handlers()
{
#ifdef _WIN32
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#else
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
    // SIGPIPE would kill the process on a peer close mid-send.
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

// Direct reply path for responses generated inside a reader thread
// (protocol-limit rejections and busy backpressure).
void emit_locked_response(const std::shared_ptr<ClientState>& target,
                          const smoke::Response& response)
{
    std::lock_guard<std::mutex> lock(target->send_mutex);
    if (target->send_queue.size() < 1024)
    {
        target->send_queue.push_back(response.json + "\n");
        target->send_cv.notify_one();
    }
}

}  // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif
    install_signal_handlers();

    std::string bind_host = "127.0.0.1";
    int port = 8765;
    std::string journal_path = "events.journal";
    std::string ready_path;
    std::size_t workers = 2;
    std::size_t queue_capacity = 1024;
    std::size_t max_clients = 256;
    std::int64_t idle_ms = 30000;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&i, argc, argv]() -> std::string {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << argv[i] << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        try
        {
            if (arg == "--bind") bind_host = next();
            else if (arg == "--port") port = std::stoi(next());
            else if (arg == "--journal") journal_path = next();
            else if (arg == "--ready-file") ready_path = next();
            else if (arg == "--workers") workers = static_cast<std::size_t>(std::stoul(next()));
            else if (arg == "--queue-capacity") queue_capacity = static_cast<std::size_t>(std::stoul(next()));
            else if (arg == "--max-clients") max_clients = static_cast<std::size_t>(std::stoul(next()));
            else if (arg == "--idle-ms") idle_ms = std::stoll(next());
            else
            {
                std::cerr << "unknown argument: " << arg << "\n";
                return 2;
            }
        }
        catch (const std::exception&)
        {
            std::cerr << "invalid value for " << arg << "\n";
            return 2;
        }
    }
    if (workers == 0 || workers > 64 || queue_capacity == 0 || queue_capacity > 100000 ||
        max_clients == 0 || max_clients > 4096 || port < 0 || port > 65535 ||
        idle_ms < 0 || idle_ms > 3600000)
    {
        std::cerr << "argument out of range\n";
        return 2;
    }

    smoke::Journal journal;
    if (!journal.open(journal_path))
    {
        std::cerr << "journal is owned by another process or cannot be opened: "
                  << journal_path << "\n";
        return 3;
    }

    smoke::Gateway gateway(workers, queue_capacity);

    std::mutex clients_mutex;
    std::map<std::uint64_t, std::shared_ptr<ClientState>> clients;
    std::condition_variable clients_cv;

    auto emit = [&](const smoke::Response& response) {
        std::shared_ptr<ClientState> target;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            const auto it = clients.find(response.client_id);
            if (it != clients.end())
            {
                target = it->second;
            }
        }
        if (!target)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(target->send_mutex);
        if (target->send_queue.size() < 1024)
        {
            target->send_queue.push_back(response.json + "\n");
            target->send_cv.notify_one();
        }
        // A full send queue means the client stopped reading; its writer will
        // drop the connection and the pending count drains there.
    };

    if (!gateway.start(&journal, emit))
    {
        std::cerr << "gateway failed to start\n";
        return 1;
    }

    socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket)
    {
        std::cerr << "socket() failed: " << last_error() << "\n";
        return 1;
    }
    const int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, bind_host.c_str(), &address.sin_addr) != 1)
    {
        std::cerr << "invalid bind address: " << bind_host << "\n";
        return 2;
    }
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        std::cerr << "bind() failed: " << last_error() << "\n";
        return 1;
    }
    if (::listen(listener, 64) != 0)
    {
        std::cerr << "listen() failed: " << last_error() << "\n";
        return 1;
    }
    socklen_t address_length = sizeof(address);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_length);
    const std::uint16_t bound_port = ntohs(address.sin_port);

    // Announce readiness only after the listener is accepting; the port may be 0.
    if (!ready_path.empty())
    {
        std::ofstream ready(ready_path);
        ready << "{\"port\":" << bound_port << "}\n";
    }
    std::cout << "smoke_gateway listening on " << bind_host << ":" << bound_port
              << " journal=" << journal_path << " workers=" << workers
              << " queue=" << queue_capacity << " max_clients=" << max_clients
              << " idle_ms=" << idle_ms << std::endl;

    // Per-client reader threads. Bounded by --max-clients.
    std::vector<std::thread> reader_threads;
    std::vector<std::thread> writer_threads;
    std::mutex threads_mutex;

    auto finish_client = [&](std::uint64_t id) {
        std::shared_ptr<ClientState> target;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            const auto it = clients.find(id);
            if (it == clients.end())
            {
                return;
            }
            target = it->second;
            clients.erase(it);
        }
        {
            std::lock_guard<std::mutex> lock(target->send_mutex);
            target->closing = true;
            target->send_cv.notify_all();
        }
        close_socket(target->sock);
        target->sock = kInvalidSocket;
    };

    while (!g_shutdown.load())
    {
        sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        socket_t conn = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (conn == kInvalidSocket)
        {
            if (g_shutdown.load())
            {
                break;
            }
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (clients.size() >= max_clients)
            {
                // Overload refusal: close immediately.
                close_socket(conn);
                continue;
            }
        }
        const int nodelay = 1;
        ::setsockopt(conn, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
#ifdef _WIN32
        u_long nonblocking = 1;
        ::ioctlsocket(conn, FIONBIO, &nonblocking);
#else
        const int flags = ::fcntl(conn, F_GETFL, 0);
        ::fcntl(conn, F_SETFL, flags | O_NONBLOCK);
#endif

        auto client = std::make_shared<ClientState>();
        client->sock = conn;
        client->id = g_next_client_id.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client->id] = client;
        }

        // Writer thread: drains the client's send queue.
        writer_threads.emplace_back([client, &finish_client]() {
            while (true)
            {
                std::string payload;
                {
                    std::unique_lock<std::mutex> lock(client->send_mutex);
                    client->send_cv.wait(lock, [&] {
                        return client->closing || !client->send_queue.empty();
                    });
                    if (client->send_queue.empty())
                    {
                        return;
                    }
                    payload = std::move(client->send_queue.front());
                    client->send_queue.pop_front();
                }
                const char* data = payload.data();
                std::size_t remaining = payload.size();
                while (remaining > 0)
                {
                    const int sent = ::send(client->sock, data, static_cast<int>(remaining), 0);
                    if (sent > 0)
                    {
                        data += sent;
                        remaining -= static_cast<std::size_t>(sent);
                        continue;
                    }
#ifdef _WIN32
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
#else
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
#endif
                    {
                        // Peer TCP window full: retry shortly rather than
                        // queueing unbounded data.
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }
                    finish_client(client->id);
                    return;
                }
            }
        });

        // Reader thread: parses lines, enqueues requests, enforces idle limit.
        reader_threads.emplace_back([&gateway, client, idle_ms, &finish_client]() {
            const std::uint64_t client_id = client->id;
            const socket_t sock = client->sock;
            // Idle means no bytes for idle_ms: a half-open or stalled client
            // must not hold its thread forever, but active clients stay.
            std::int64_t last_activity = now_ms();
            while (true)
            {
                const std::size_t newline = client->recv_buffer.find('\n');
                if (newline != std::string::npos)
                {
                    std::string line = client->recv_buffer.substr(0, newline);
                    client->recv_buffer.erase(0, newline + 1);
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    if (line.size() > smoke::kMaxLineLength)
                    {
                        // Reject and drop this client rather than buffer further.
                        smoke::Response response;
                        response.client_id = client_id;
                        response.json = "{\"status\":\"invalid\"}";
                        emit_locked_response(client, response);
                        finish_client(client_id);
                        return;
                    }
                    if (!line.empty())
                    {
                        smoke::Request request;
                        request.client_id = client_id;
                        request.line = std::move(line);
                        if (!gateway.enqueue(std::move(request)))
                        {
                            smoke::Response response;
                            response.client_id = client_id;
                            response.json = "{\"status\":\"busy\"}";
                            emit_locked_response(client, response);
                            continue;
                        }
                    }
                    continue;
                }
                char chunk[4096];
                const int received = ::recv(sock, chunk, sizeof(chunk), 0);
                if (received < 0)
                {
#ifdef _WIN32
                    if (WSAGetLastError() == WSAEWOULDBLOCK ||
                        WSAGetLastError() == WSAETIMEDOUT)
#else
                    if (errno == EAGAIN || errno == EWOULDBLOCK ||
                        errno == EINTR)
#endif
                    {
                        if (now_ms() - last_activity > idle_ms)
                        {
                            finish_client(client_id);
                            return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }
                    finish_client(client_id);
                    return;
                }
                if (received == 0)
                {
                    finish_client(client_id);
                    return;
                }
                last_activity = now_ms();
                client->recv_buffer.append(chunk, static_cast<std::size_t>(received));
                if (client->recv_buffer.size() > smoke::kMaxLineLength + 64)
                {
                    smoke::Response response;
                    response.client_id = client_id;
                    response.json = "{\"status\":\"invalid\"}";
                    emit_locked_response(client, response);
                    finish_client(client_id);
                    return;
                }
            }
        });
    }

    std::cout << "shutting down\n";
    close_socket(listener);
    gateway.stop();
    for (std::thread& thread : reader_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    for (std::thread& thread : writer_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& entry : clients)
        {
            close_socket(entry.second->sock);
        }
        clients.clear();
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
