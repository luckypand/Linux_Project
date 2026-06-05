#include "../src/net/TcpServer.hpp"
#include "../src/net/EventLoop.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <csignal>
#include <thread>
#include <fstream>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

using namespace std;

// ============================================================
// helper: read RSS from /proc/self/status (in KB)
// ============================================================
static size_t get_rss_kb() {
    ifstream f("/proc/self/status");
    if (!f) return 0;
    string line;
    while (getline(f, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            size_t rss = 0;
            sscanf(line.c_str() + 6, "%zu", &rss);
            return rss;
        }
    }
    return 0;
}

// ============================================================
// Server mode
// ============================================================
class EchoServer {
public:
    EchoServer(EventLoop* loop, const string& ip, uint16_t port, int numThreads)
        : server_(loop, ip, port, "EchoServer") {
        server_.setThreadNum(numThreads);
        server_.setConnectionCallback(bind(&EchoServer::onConnection, this, placeholders::_1));
        server_.setMessageCallback(bind(&EchoServer::onMessage, this, placeholders::_1, placeholders::_2));
    }

    void start() { server_.start(); }

    size_t connCount() const { return conn_count_.load(); }
    size_t msgCount()  const { return msg_count_.load(); }

private:
    void onConnection(const TcpServer::TcpConnectionPtr& conn) {
        if (conn->connected()) {
            conn_count_.fetch_add(1, memory_order_relaxed);
        } else {
            conn_count_.fetch_sub(1, memory_order_relaxed);
        }
    }

    void onMessage(const TcpServer::TcpConnectionPtr& conn, Buffer& buf) {
        string msg = buf.retrieveAsString();
        msg_count_.fetch_add(1, memory_order_relaxed);
        conn->send(msg);
    }

    TcpServer server_;
    atomic<size_t> conn_count_{0};
    atomic<size_t> msg_count_{0};
};

// ============================================================
// Client mode - lightweight stress client
//
// Uses a single epoll fd + timerfd to manage up to tens of
// thousands of connections with minimal per-connection overhead.
// ============================================================
class StressClient {
public:
    StressClient(const string& server_ip, uint16_t server_port,
                 int num_conn, int duration_sec, int heartbeat_sec)
        : server_ip_(server_ip), server_port_(server_port),
          num_conn_(num_conn), duration_sec_(duration_sec),
          heartbeat_sec_(heartbeat_sec) {}

    ~StressClient() {
        if (epfd_ >= 0) close(epfd_);
        if (timerfd_ >= 0) close(timerfd_);
    }

    void run() {
        bump_fd_limit();

        cout << "[client] target " << server_ip_ << ":" << server_port_
             << ", connections=" << num_conn_
             << ", duration=" << duration_sec_ << "s"
             << ", heartbeat_interval=" << heartbeat_sec_ << "s\n";
        cout << "[client] RSS before: " << get_rss_kb() << " KB\n";

        epfd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epfd_ < 0) { perror("epoll_create1"); return; }

        timerfd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if (timerfd_ < 0) { perror("timerfd_create"); return; }

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = timerfd_;
        epoll_ctl(epfd_, EPOLL_CTL_ADD, timerfd_, &ev);

        struct itimerspec its;
        its.it_value.tv_sec  = heartbeat_sec_;
        its.it_value.tv_nsec = 0;
        its.it_interval.tv_sec  = heartbeat_sec_;
        its.it_interval.tv_nsec = 0;
        timerfd_settime(timerfd_, 0, &its, nullptr);

        // --- Phase 1: create all connections (non-blocking) ---
        cout << "[client] Phase 1: creating " << num_conn_ << " sockets ...\n";
        conns_.reserve(num_conn_);

        struct sockaddr_in srv_addr;
        memset(&srv_addr, 0, sizeof(srv_addr));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port   = htons(server_port_);
        inet_pton(AF_INET, server_ip_.c_str(), &srv_addr.sin_addr);

        int created = 0;
        for (int i = 0; i < num_conn_; ++i) {
            int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            if (fd < 0) {
                cerr << "[client] socket() failed at index " << i
                     << ": " << strerror(errno) << "\n";
                break;
            }

            int snd = 4096, rcv = 4096;
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));
            setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));

            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            int ret = connect(fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
            if (ret < 0 && errno != EINPROGRESS) {
                close(fd);
                conns_.push_back({-1, false});
                continue;
            }

            ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
            ev.data.fd = fd;
            if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
                close(fd);
                conns_.push_back({-1, false});
                continue;
            }

            conns_.push_back({fd, false});
            ++created;
        }
        stats_.attempted = created;
        cout << "[client] " << created << " sockets created, entering event loop\n";

        // --- Phase 2: event loop ---
        start_time_ = chrono::steady_clock::now();
        last_stats_time_ = 0;
        event_loop();

        // --- Cleanup ---
        for (auto& c : conns_) {
            if (c.fd >= 0) { close(c.fd); c.fd = -1; }
        }
        print_final_stats();
    }

    void stop() { stop_ = true; }
    bool stopped() const { return stop_; }

private:
    struct Conn {
        int  fd;
        bool connected;   // TCP handshake completed
    };

    struct Stats {
        int    attempted   = 0;
        int    established = 0;
        int    failed      = 0;
        int    closed      = 0;
        size_t heartbeats  = 0;
        size_t bytes_recv  = 0;
    };

    void bump_fd_limit() {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
            rl.rlim_cur = rl.rlim_max;
            if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
                cerr << "[client] warning: failed to raise RLIMIT_NOFILE\n";
            }
        }
    }

    void event_loop() {
        struct epoll_event events[1024];

        while (!stop_) {
            int n = epoll_wait(epfd_, events, 1024, 100);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) { perror("epoll_wait"); break; }

            auto now = chrono::steady_clock::now();
            int elapsed = chrono::duration_cast<chrono::seconds>(now - start_time_).count();
            if (elapsed >= duration_sec_) {
                cout << "\n[client] duration reached, stopping\n";
                break;
            }

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;

                if (fd == timerfd_) {
                    handle_timer(elapsed);
                } else {
                    handle_socket_event(fd, events[i].events);
                }
            }
        }
    }

    void handle_timer(int elapsed) {
        uint64_t exp;
        read(timerfd_, &exp, sizeof(exp));

        // send heartbeat to all established connections
        static const char kHeartbeat[] = "PING\n";
        static const size_t kHbLen = sizeof(kHeartbeat) - 1;

        for (auto& c : conns_) {
            if (c.fd >= 0 && c.connected) {
                ssize_t sent = send(c.fd, kHeartbeat, kHbLen,
                                    MSG_DONTWAIT | MSG_NOSIGNAL);
                if (sent > 0) stats_.heartbeats++;
            }
        }

        // print stats every ~5 s
        if (elapsed - last_stats_time_ >= 5) {
            last_stats_time_ = elapsed;
            int active = 0;
            for (auto& c : conns_) if (c.fd >= 0 && c.connected) ++active;

            cout << "[client] t=" << elapsed << "s"
                 << "  active=" << active
                 << "  est=" << stats_.established
                 << "  fail=" << stats_.failed
                 << "  closed=" << stats_.closed
                 << "  hb=" << stats_.heartbeats
                 << "  rss=" << get_rss_kb() << "KB\n";
        }
    }

    void handle_socket_event(int fd, uint32_t revents) {
        // locate the connection record
        Conn* conn = nullptr;
        for (auto& c : conns_) {
            if (c.fd == fd) { conn = &c; break; }
        }
        if (!conn) return;

        if (revents & (EPOLLERR | EPOLLHUP)) {
            if (conn->connected) stats_.closed++;
            else                 stats_.failed++;
            epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            conn->fd        = -1;
            conn->connected = false;
            return;
        }

        if (revents & EPOLLOUT) {
            // non-blocking connect completed
            int       err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);

            if (err == 0) {
                conn->connected = true;
                stats_.established++;

                struct epoll_event mod_ev;
                mod_ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
                mod_ev.data.fd = fd;
                epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &mod_ev);
            } else {
                stats_.failed++;
                epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                conn->fd = -1;
            }
            return;
        }

        if (revents & EPOLLIN) {
            // drain echo data
            char buf[4096];
            while (true) {
                ssize_t r = read(fd, buf, sizeof(buf));
                if (r > 0) {
                    stats_.bytes_recv += r;
                } else if (r == 0) {
                    // peer closed
                    stats_.closed++;
                    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    conn->fd        = -1;
                    conn->connected = false;
                    break;
                } else {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    stats_.closed++;
                    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    conn->fd        = -1;
                    conn->connected = false;
                    break;
                }
            }
        }
    }

    void print_final_stats() {
        auto now = chrono::steady_clock::now();
        int  elapsed = chrono::duration_cast<chrono::seconds>(now - start_time_).count();

        cout << "\n========================================\n";
        cout << "  Stress Test Results\n";
        cout << "========================================\n";
        cout << "  Duration:           " << elapsed          << " s\n";
        cout << "  Connections (try):  " << stats_.attempted   << "\n";
        cout << "  Established:        " << stats_.established << "\n";
        cout << "  Failed:             " << stats_.failed      << "\n";
        cout << "  Closed:             " << stats_.closed      << "\n";
        cout << "  Heartbeats sent:    " << stats_.heartbeats  << "\n";
        cout << "  Bytes received:     " << stats_.bytes_recv
             << " (" << (stats_.bytes_recv / 1024) << " KB)\n";
        cout << "  RSS final:          " << get_rss_kb()      << " KB\n";
        cout << "========================================\n";
    }

    string    server_ip_;
    uint16_t  server_port_;
    int       num_conn_;
    int       duration_sec_;
    int       heartbeat_sec_;

    vector<Conn> conns_;
    int  epfd_    = -1;
    int  timerfd_ = -1;
    Stats stats_;
    chrono::steady_clock::time_point start_time_;
    int  last_stats_time_ = 0;
    bool stop_ = false;
};

// ============================================================
// Signal handling
// ============================================================
static volatile sig_atomic_t g_running = 1;
static void on_signal(int) { g_running = 0; }

// ============================================================
// Usage
// ============================================================
static void usage(const char* prog) {
    cout << "Usage:\n"
         << "  Server:  " << prog << " [--port PORT] [--threads N]\n"
         << "  Client:  " << prog << " --mode client [options]\n"
         << "\nCommon options:\n"
         << "  --port PORT          Server listen port (default: 8080)\n"
         << "  --threads N          Sub-reactor threads (default: 4)\n"
         << "\nClient options:\n"
         << "  --mode MODE          'server' or 'client' (default: server)\n"
         << "  --server-ip IP       Server IP (default: 127.0.0.1)\n"
         << "  --server-port PORT   Server port (default: 8080)\n"
         << "  --connections N      Connections to create (default: 10000)\n"
         << "  --duration S         Test duration in seconds (default: 600)\n"
         << "  --heartbeat S        Heartbeat interval in seconds (default: 1)\n"
         << "\nExamples:\n"
         << "  # Scenario 1 - leak test: start server, then client\n"
         << "  " << prog << " --port 8080 --threads 4 &\n"
         << "  " << prog << " --mode client --connections 10000 --duration 600\n"
         << "\n"
         << "  # Scenario 2 - throughput test: start server, run tcpkali\n"
         << "  " << prog << " --port 8080 --threads 4\n"
         << "  # tcpkali -c 100 -T 60 127.0.0.1:8080\n";
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    string   mode         = "server";
    uint16_t port         = 8080;
    int      threads      = 4;
    string   server_ip    = "127.0.0.1";
    uint16_t server_port  = 8080;
    int      num_conn     = 10000;
    int      duration_sec = 600;
    int      heartbeat_sec = 1;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(stoi(argv[++i]));
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = stoi(argv[++i]);
        } else if (arg == "--server-ip" && i + 1 < argc) {
            server_ip = argv[++i];
        } else if (arg == "--server-port" && i + 1 < argc) {
            server_port = static_cast<uint16_t>(stoi(argv[++i]));
        } else if (arg == "--connections" && i + 1 < argc) {
            num_conn = stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration_sec = stoi(argv[++i]);
        } else if (arg == "--heartbeat" && i + 1 < argc) {
            heartbeat_sec = stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        }
    }

    if (mode == "client") {
        // ==================== Client Mode (Scenario 1) ====================
        cout << "[client] StressClient starting\n";

        StressClient client(server_ip, server_port, num_conn,
                            duration_sec, heartbeat_sec);
        thread t([&]() { client.run(); });

        // wait for signal or thread completion
        while (g_running && !client.stopped()) {
            this_thread::sleep_for(chrono::milliseconds(500));
        }

        if (!g_running) {
            cout << "\n[client] signal caught, stopping ...\n";
            client.stop();
        }
        t.join();

    } else {
        // ==================== Server Mode (Scenario 2) ====================
        cout << "[server] listening on 0.0.0.0:" << port
             << " with " << threads << " sub-reactor threads\n";
        cout << "[server] RSS start: " << get_rss_kb() << " KB\n";

        EventLoop  loop;
        EchoServer server(&loop, "0.0.0.0", port, threads);
        server.start();

        // watcher thread: periodic stats + clean shutdown on signal
        thread watcher([&]() {
            while (g_running) {
                this_thread::sleep_for(chrono::seconds(10));
                if (g_running) {
                    cout << "[server] conns=" << server.connCount()
                         << "  msgs=" << server.msgCount()
                         << "  rss=" << get_rss_kb() << "KB\n";
                }
            }
            loop.quit();  // wake epoll_wait so loop() returns
        });

        loop.loop();       // blocks until quit()

        g_running = 0;     // tell watcher to stop
        watcher.join();

        cout << "[server] final: conns=" << server.connCount()
             << "  msgs=" << server.msgCount()
             << "  rss=" << get_rss_kb() << "KB\n";
    }
    cout << "[done]\n";
    return 0;
}
