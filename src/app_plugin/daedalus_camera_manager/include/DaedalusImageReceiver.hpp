#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app_plugin
{

// Daedalus TCP image receiver (talos tcp_image v1: 64-byte big-endian header + payload).
// We do not use the SDK TcpImageClient: its receiveAll is a blocking recv that cannot be
// interrupted, so a stalled simulator sender wedges it forever. Here we own the socket:
// 300ms without data aborts the connection and reconnects.
class DaedalusImageReceiver
{
public:
    struct Frame
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t format = 0;
        std::uint64_t source_sequence = 0;
        std::uint64_t capture_timestamp_ns = 0;
        std::vector<std::uint8_t> payload;
    };

    DaedalusImageReceiver(std::string host, std::uint16_t port);
    ~DaedalusImageReceiver();

    DaedalusImageReceiver(const DaedalusImageReceiver &) = delete;
    DaedalusImageReceiver &operator=(const DaedalusImageReceiver &) = delete;

    void start();
    bool connected() const { return m_connected.load(); }

    bool wait_frame(Frame &out, int timeout_ms);

private:
    void loop();
    bool read_one_frame(int fd);

    std::string m_host;
    std::uint16_t m_port;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    std::mutex m_mutex;
    std::condition_variable m_cv;
    Frame m_latest;
    std::uint64_t m_latest_seq = 0;
    std::uint64_t m_taken_seq = 0;
};

} // namespace app_plugin