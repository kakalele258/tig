// Daedalus control helper.
//   snap <out.ppm>                : grab one frame (prints seq + uniformity)
//   scene <armor|energy|outpost|shooting_range>
//   reset
//   rune <off|small|large> [pending...] -- [activated...]
//   aim <yaw> <pitch> [out.ppm]
#include <daedalus_sim_sdk/scene_control_client.hpp>
#include <daedalus_sim_sdk/tcp_image_client.hpp>
#include <daedalus_sim_sdk/udp_gimbal_client.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace daedalus::sim::sdk::v1;

static const char *statusName(SceneControlStatus s)
{
    switch (s)
    {
    case SceneControlStatus::Ok: return "Ok";
    case SceneControlStatus::InvalidRequest: return "InvalidRequest";
    case SceneControlStatus::Unsupported: return "Unsupported";
    case SceneControlStatus::NotReady: return "NotReady";
    default: return "InternalError";
    }
}

static void dump(const char *op, const ClientResult<SceneControlResponse> &r)
{
    if (!r.ok())
    {
        std::printf("[%s] FAILED: %s\n", op, r.status.message.c_str());
        return;
    }
    std::printf("[%s] status=%s frame=%llu msg=%s\n", op, statusName(r.value->status),
                (unsigned long long)r.value->applied_frame_seq, r.value->message.c_str());
}

static std::unique_ptr<SceneControlClient> makeClient()
{
    SceneControlOptions opt;
    opt.endpoint = UdpEndpoint{"127.0.0.1", kUdpSceneControlPort};
    opt.timeout = std::chrono::milliseconds(6000);
    opt.session_id = "rp-rune-demo";
    auto c = std::make_unique<SceneControlClient>(opt);
    dump("create_session", c->createSession());
    return c;
}


static bool grabFrame(TcpImageFrame &out, std::uint64_t after_seq, bool first)
{
    static std::unique_ptr<TcpImageClient> client;
    if (!client)
    {
        client = std::make_unique<TcpImageClient>(UdpEndpoint{"127.0.0.1", kTcpImagePort});
        if (!client->connect().ok()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
    auto res = client->waitForLatest(first ? 0 : after_seq, std::chrono::milliseconds(2000));
    if (!res.ok()) return false;
    out = *res.value;
    return true;
}

static int take_snapshot(const char *path)
{
    TcpImageClient img(UdpEndpoint{"127.0.0.1", kTcpImagePort});
    auto st = img.connect();
    if (!st.ok())
    {
        std::printf("[snapshot] connect failed: %s\n", st.message.c_str());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    auto res = img.waitForLatest(0, std::chrono::milliseconds(3000));
    if (!res.ok())
    {
        std::printf("[snapshot] no frame: %s\n", res.status.message.c_str());
        return 1;
    }
    const TcpImageFrame &f = *res.value;
    const int channels = (f.header.format == tcp_image::PixelFormat::Rgba32) ? 4 : 3;
    const std::size_t w = f.header.width, h = f.header.height;
    // uniformity: sample every 97th pixel, track min/max luminance
    int lo = 255, hi = 0;
    double sum = 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < w * h; i += 97)
    {
        const std::size_t s = i * static_cast<std::size_t>(channels);
        const int lum = (f.payload[s] + f.payload[s + 1] + f.payload[s + 2]) / 3;
        if (lum < lo) lo = lum;
        if (lum > hi) hi = lum;
        sum += lum;
        ++n;
    }
    std::size_t red_px = 0, blue_px = 0, bright_px = 0;
    std::size_t rx0 = w, rx1 = 0, ry0 = h, ry1 = 0;
    for (std::size_t i = 0; i < w * h; i += 3)
    {
        const std::size_t s0 = i * static_cast<std::size_t>(channels);
        const int r = f.payload[s0], g = f.payload[s0 + 1], b = f.payload[s0 + 2];
        if (r > 150 && g < 100 && b < 100)
        {
            ++red_px;
            const std::size_t px = i % w, py = i / w;
            if (px < rx0) rx0 = px;
            if (px > rx1) rx1 = px;
            if (py < ry0) ry0 = py;
            if (py > ry1) ry1 = py;
        }
        if (b > 150 && r < 110 && g < 170) ++blue_px;
        if (r + g + b > 480) ++bright_px;
    }
    std::printf("[snapshot] seq=%llu %zux%zu ch=%d mean=%.1f min=%d max=%d red=%zu blue=%zu bright=%zu\n",
                (unsigned long long)f.header.source_sequence, w, h, channels, sum / n, lo, hi,
                red_px, blue_px, bright_px);
    if (red_px > 50)
        std::printf("[red-bbox] x=%zu..%zu y=%zu..%zu margins L=%zu R=%zu T=%zu B=%zu\n", rx0, rx1,
                    ry0, ry1, rx0, w - 1 - rx1, ry0, h - 1 - ry1);
    FILE *fp = std::fopen(path, "wb");
    if (fp == nullptr) return 1;
    std::fprintf(fp, "P6\n%zu %zu\n255\n", w, h);
    std::vector<unsigned char> rgb(w * h * 3U);
    for (std::size_t i = 0, o = 0; o + 2 < rgb.size(); ++i, o += 3)
    {
        const std::size_t s = i * static_cast<std::size_t>(channels);
        rgb[o] = f.payload[s];
        rgb[o + 1] = f.payload[s + 1];
        rgb[o + 2] = f.payload[s + 2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), fp);
    std::fclose(fp);
    std::printf("[snapshot] written %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    const std::string cmd = (argc > 1) ? argv[1] : "snap";

    if (cmd == "snap")
        return take_snapshot(argc > 2 ? argv[2] : "/mnt/d/WSL/_work/snap.ppm");

    if (cmd == "aim")
    {
        UdpGimbalClient gimbal(UdpEndpoint{"127.0.0.1", kUdpCommandPort});
        UdpGimbalCommand c;
        c.yaw_deg = static_cast<float>(std::atof(argv[2]));
        c.pitch_deg = static_cast<float>(std::atof(argv[3]));
        c.fire_advice = false;
        for (int i = 0; i < 5; ++i)
        {
            auto st = gimbal.send(c);
            if (!st.ok()) std::printf("[aim] send failed: %s\n", st.message.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::printf("[aim] yaw=%.1f pitch=%.1f\n", c.yaw_deg.value(), c.pitch_deg.value());
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        if (argc > 4) return take_snapshot(argv[4]);
        return 0;
    }

    if (cmd == "center")
    {
        UdpGimbalClient gimbal(UdpEndpoint{"127.0.0.1", kUdpCommandPort});
        double yaw = (argc > 2) ? std::atof(argv[2]) : 60.0;
        double pitch = (argc > 3) ? std::atof(argv[3]) : 85.0;
        const double fx = 1303.67532368147;
        TcpImageFrame f;
        bool first = true;
        std::uint64_t seq = 0;
        for (int iter = 0; iter < 10; ++iter)
        {
            UdpGimbalCommand c;
            c.yaw_deg = static_cast<float>(yaw);
            c.pitch_deg = static_cast<float>(pitch);
            c.fire_advice = false;
            for (int k = 0; k < 3; ++k)
            {
                (void)gimbal.send(c);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (!grabFrame(f, seq, first)) { std::printf("[center] no frame\n"); return 1; }
            first = false;
            seq = f.header.source_sequence;
            const int channels = (f.header.format == tcp_image::PixelFormat::Rgba32) ? 4 : 3;
            double sx = 0, sy = 0;
            std::size_t n = 0;
            for (std::size_t y = 0; y < f.header.height; ++y)
                for (std::size_t x = 0; x < f.header.width; x += 2)
                {
                    const std::size_t s0 = (y * f.header.width + x) * static_cast<std::size_t>(channels);
                    const int r = f.payload[s0], g = f.payload[s0 + 1], b = f.payload[s0 + 2];
                    if (r > 150 && g < 105 && b < 105) { sx += (double)x; sy += (double)y; ++n; }
                }
            if (n < 150) { std::printf("[center] iter=%d red=%zu too few, widening search\n", iter, n); yaw += 12.0; continue; }
            const double cx = sx / (double)n, cy = sy / (double)n;
            const double dx = cx - f.header.width * 0.5;
            const double dy = cy - f.header.height * 0.5;
            const double yaw_err = std::atan2(dx, fx) * 180.0 / M_PI;
            const double pitch_err = std::atan2(-dy, fx) * 180.0 / M_PI;
            std::printf("[center] iter=%d red=%zu centroid=(%.0f,%.0f) err=(yaw %.2f, pitch %.2f) -> yaw=%.1f pitch=%.1f\n",
                        iter, n, cx, cy, yaw_err, pitch_err, yaw, pitch);
            if (std::fabs(yaw_err) < 1.0 && std::fabs(pitch_err) < 1.0)
            {
                std::printf("[center] converged yaw=%.1f pitch=%.1f\n", yaw, pitch);
                break;
            }
            yaw += yaw_err;
            pitch += pitch_err;
        }
        if (argc > 4) return take_snapshot(argv[4]);
        return 0;
    }

    auto client_ptr = makeClient();
    SceneControlClient &client = *client_ptr;

    if (cmd == "scene")
    {
        const std::string s = (argc > 2) ? argv[2] : "energy";
        SceneMode m = SceneMode::Energy;
        if (s == "armor") m = SceneMode::Armor;
        else if (s == "outpost") m = SceneMode::Outpost;
        else if (s == "shooting_range") m = SceneMode::ShootingRange;
        dump("set_scene", client.setScene(m));
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        return 0;
    }
    if (cmd == "reset")
    {
        dump("reset_scene", client.resetScene());
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        return 0;
    }
    if (cmd == "rune")
    {
        RuneState r;
        const std::string m = (argc > 2) ? argv[2] : "large";
        r.mode = (m == "off") ? RuneMode::Off : (m == "small" ? RuneMode::Small : RuneMode::Large);
        bool after_sep = false;
        for (int i = 3; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--") == 0)
            {
                after_sep = true;
                continue;
            }
            const auto v = static_cast<std::uint8_t>(std::atoi(argv[i]));
            (after_sep ? r.activated_targets : r.pending_targets).push_back(v);
        }
        std::printf("[rune] mode=%s pending=%zu activated=%zu\n", m.c_str(),
                    r.pending_targets.size(), r.activated_targets.size());
        dump("set_rune_state", client.setRuneState(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        return 0;
    }
    std::printf("unknown command %s\n", cmd.c_str());
    return 1;
}
