#include "DaedalusImageReceiver.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>

#include <glog/logging.h>

namespace
{

constexpr std::uint32_t kWireMagic = 0x54494D47U;
constexpr std::size_t kHeaderBytes = 64;
constexpr int kStallAbortMs = 5000;

std::atomic<long long> g_rx_bytes{0};
std::atomic<long long> g_frames{0};
std::atomic<long long> g_eagain{0};

std::int64_t now_ms()
{
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint16_t rd_u16(const std::uint8_t *p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

std::uint32_t rd_u32(const std::uint8_t *p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint64_t rd_u64(const std::uint8_t *p)
{
    return (static_cast<std::uint64_t>(rd_u32(p)) << 32) | static_cast<std::uint64_t>(rd_u32(p + 4));
}

} // namespace

namespace app_plugin
{

DaedalusImageReceiver::DaedalusImageReceiver(std::string host, std::uint16_t port)
    : m_host(std::move(host)), m_port(port)
{
}

DaedalusImageReceiver::~DaedalusImageReceiver()
{
    m_stop.store(true);
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void DaedalusImageReceiver::start()
{
    if (m_thread.joinable())
        return;
    m_thread = std::thread([this]() { loop(); });
    std::thread([]() {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            LOG(INFO) << "[diag] rx_bytes=" << g_rx_bytes.load() << " frames=" << g_frames.load()
                      << " eagain=" << g_eagain.load();
        }
    }).detach();
}

bool DaedalusImageReceiver::read_one_frame(int fd)
{
    std::uint8_t header[kHeaderBytes];
    std::size_t got = 0;
    std::int64_t last_progress = now_ms();
    while (got < kHeaderBytes)
    {
        const ssize_t n = ::recv(fd, header + got, kHeaderBytes - got, 0);
        if (n > 0)
        {
            g_rx_bytes.fetch_add(n);
            got += static_cast<std::size_t>(n);
            last_progress = now_ms();
            continue;
        }
        if (n == 0)
            return false;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            g_eagain.fetch_add(1);
            if (m_stop.load())
                return false;
            if (now_ms() - last_progress > kStallAbortMs)
            {
                LOG(WARNING) << "[DaedalusImageReceiver] header stalled after " << got << " bytes";
                return false;
            }
            continue;
        }
        LOG(WARNING) << "[DaedalusImageReceiver] recv header error errno=" << errno;
        return false;
    }

    const std::uint32_t magic = rd_u32(header + 0);
    const std::uint16_t version = rd_u16(header + 4);
    const std::uint16_t header_bytes = rd_u16(header + 6);
    const std::uint16_t format = rd_u16(header + 8);
    const std::uint16_t flags = rd_u16(header + 10);
    const std::uint32_t width = rd_u32(header + 12);
    const std::uint32_t height = rd_u32(header + 16);
    const std::uint32_t payload_bytes = rd_u32(header + 20);
    const std::uint64_t sequence = rd_u64(header + 32);
    const std::uint64_t timestamp_ns = rd_u64(header + 40);

    if (magic != kWireMagic || version != 1 || flags != 0 || header_bytes < kHeaderBytes ||
        width == 0 || height == 0 || payload_bytes == 0 || payload_bytes > 64U * 1024U * 1024U)
    {
        LOG(WARNING) << "[DaedalusImageReceiver] bad header magic=" << std::hex << magic << std::dec
                     << " ver=" << version << " hdr=" << header_bytes << " flags=" << flags
                     << " w=" << width << " h=" << height << " payload=" << payload_bytes;
        return false;
    }

    if (header_bytes > kHeaderBytes)
    {
        std::vector<std::uint8_t> extra(header_bytes - kHeaderBytes);
        std::size_t filled = 0;
        while (filled < extra.size())
        {
            const ssize_t n = ::recv(fd, extra.data() + filled, extra.size() - filled, 0);
            if (n > 0)
            {
                g_rx_bytes.fetch_add(n);
                filled += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                g_eagain.fetch_add(1);
                if (m_stop.load())
                    return false;
                continue;
            }
            return false;
        }
    }

    std::vector<std::uint8_t> payload(payload_bytes);
    std::size_t filled = 0;
    last_progress = now_ms();
    while (filled < payload.size())
    {
        const ssize_t n = ::recv(fd, payload.data() + filled, payload.size() - filled, 0);
        if (n > 0)
        {
            g_rx_bytes.fetch_add(n);
            filled += static_cast<std::size_t>(n);
            last_progress = now_ms();
            continue;
        }
        if (n == 0)
            return false;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            g_eagain.fetch_add(1);
            if (m_stop.load())
                return false;
            if (now_ms() - last_progress > kStallAbortMs)
            {
                LOG(WARNING) << "[DaedalusImageReceiver] payload stalled at " << filled << '/'
                             << payload.size();
                return false;
            }
            continue;
        }
        LOG(WARNING) << "[DaedalusImageReceiver] recv payload error errno=" << errno;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latest.width = width;
        m_latest.height = height;
        m_latest.format = format;
        m_latest.source_sequence = sequence;
        m_latest.capture_timestamp_ns = timestamp_ns;
        m_latest.payload.swap(payload);
        m_latest_seq = sequence;
    }
    m_cv.notify_all();
    g_frames.fetch_add(1);
    return true;
}

void DaedalusImageReceiver::loop()
{
    while (!m_stop.load())
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        if (::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) != 1)
        {
            LOG(WARNING) << "[DaedalusImageReceiver] bad host " << m_host;
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 300000;
        if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
            LOG(WARNING) << "[DaedalusImageReceiver] SO_RCVTIMEO failed errno=" << errno;
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        int rcvbuf = 8 * 1024 * 1024;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        m_connected.store(true);
        LOG(INFO) << "[DaedalusImageReceiver] connected to " << m_host << ':' << m_port;

        while (!m_stop.load())
        {
            if (!read_one_frame(fd))
                break;
        }

        m_connected.store(false);
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        if (!m_stop.load())
            LOG(WARNING) << "[DaedalusImageReceiver] connection dropped, reconnecting";
    }
}

bool DaedalusImageReceiver::wait_frame(Frame &out, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_latest_seq == m_taken_seq)
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                      [this]() { return m_latest_seq != m_taken_seq || m_stop.load(); });
    }
    if (m_latest_seq == m_taken_seq)
        return false;
    out.width = m_latest.width;
    out.height = m_latest.height;
    out.format = m_latest.format;
    out.source_sequence = m_latest.source_sequence;
    out.capture_timestamp_ns = m_latest.capture_timestamp_ns;
    out.payload = m_latest.payload;
    m_taken_seq = m_latest_seq;
    return true;
}

} // namespace app_plugin