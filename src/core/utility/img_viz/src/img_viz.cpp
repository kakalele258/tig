#include "img_viz.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

ImgViz &ImgViz::instance()
{
    static ImgViz instance;
    return instance;
}

ImgViz::~ImgViz()
{
    State expected = State::Enabled;
    m_state.compare_exchange_strong(expected, State::Stopping);

    if (m_worker && m_worker->joinable())
        m_worker->join();
}

void ImgViz::init(bool enabled)
{
    auto &self = instance();
    State expected = State::Uninitialized;
    if (!self.m_state.compare_exchange_strong(expected, enabled ? State::Enabled : State::Disabled))
        throw std::logic_error("img_viz::init() can only be called once");
}

bool ImgViz::enabled()
{
    return instance().m_state.load(std::memory_order_relaxed) == State::Enabled;
}

void ImgViz::enqueue_image_copy(std::string_view window_name, const cv::Mat &image)
{
    if (image.empty())
        return;

    enqueue_image_zero_copy(window_name, image.clone());
}

void ImgViz::enqueue_image_zero_copy(std::string_view window_name, const cv::Mat &image)
{
    auto &self = instance();
    if (image.empty() || self.m_state.load(std::memory_order_relaxed) != State::Enabled)
        return;

    {
        std::lock_guard lock(self.m_mutex);
        auto &window = self.m_windows[std::string(window_name)];
        window.image = image;
        window.is_dirty = true;
    }
    self.ensure_worker();
}


namespace
{

struct VizDumpConfig
{
    bool enabled = false;
    std::string dir;
    std::vector<std::string> windows;
    int every = 1;
};

const VizDumpConfig &viz_dump_config()
{
    static const VizDumpConfig config = []()
    {
        VizDumpConfig c;
        if (const char *dir = std::getenv("RP_VIZ_DUMP_DIR"); dir != nullptr && *dir != 0)
        {
            c.dir = dir;
            c.enabled = true;
        }
        if (const char *list = std::getenv("RP_VIZ_DUMP_WINDOWS"); list != nullptr && *list != 0)
        {
            const std::string raw(list);
            std::size_t pos = 0;
            while (true)
            {
                const std::size_t next = raw.find(0x2C, pos);
                c.windows.emplace_back(raw.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
                if (next == std::string::npos) break;
                pos = next + 1;
            }
        }
        if (const char *every = std::getenv("RP_VIZ_DUMP_EVERY"); every != nullptr && *every != 0)
            c.every = std::max(1, std::atoi(every));
        return c;
    }();
    return config;
}

void viz_maybe_dump(const std::string &name, const cv::Mat &image)
{
    const VizDumpConfig &config = viz_dump_config();
    if (!config.enabled) return;
    if (!config.windows.empty() &&
        std::find(config.windows.begin(), config.windows.end(), name) == config.windows.end())
        return;

    static std::mutex mutex;
    static std::unordered_map<std::string, int> counters;
    std::lock_guard lock(mutex);
    const int index = counters[name]++;
    if (index % config.every != 0) return;

    std::string safe = name;
    for (char &ch : safe)
        if (ch == 0x2F) ch = 0x5F;
    const std::filesystem::path dir = std::filesystem::path(config.dir) / safe;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    char file[64];
    std::snprintf(file, sizeof(file), "%05d.jpg", index);
    const std::filesystem::path path = dir / file;
    if (!cv::imwrite(path.string(), image))
        std::fprintf(stderr, "[ImgViz] failed to dump %s\n", path.c_str());
}

} // namespace

void ImgViz::ensure_worker()
{
    if (m_state.load(std::memory_order_relaxed) != State::Enabled)
        return;

    std::lock_guard lock(m_mutex);
    if (!m_worker)
        m_worker = std::make_unique<std::thread>(&ImgViz::visualize, this);
}

void ImgViz::visualize()
{
    while (m_state.load(std::memory_order_relaxed) != State::Stopping)
    {
        std::vector<std::pair<std::string, cv::Mat>> to_draw;
        {
            std::lock_guard lock(m_mutex);
            for (auto &[name, window] : m_windows)
            {
                if (window.is_dirty)
                {
                    to_draw.emplace_back(name, std::move(window.image));
                    window.is_dirty = false;
                }
            }
        }

        for (const auto &[name, image] : to_draw)
        {
            if (!image.empty())
            {
                cv::namedWindow(name, cv::WINDOW_NORMAL);
                cv::imshow(name, image);
                viz_maybe_dump(name, image);
            }
        }

        cv::waitKeyEx(1);
    }
}
