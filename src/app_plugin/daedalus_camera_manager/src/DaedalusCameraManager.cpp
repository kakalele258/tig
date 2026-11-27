#include "DaedalusCameraManager.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>

#include <glog/logging.h>
#include <opencv2/imgproc.hpp>

#include "DaedalusGimbalState.hpp"
#include "class_loader.hpp"
#include "time/time.hpp"

namespace
{

std::string env_or_default(const char *name, const std::string &fallback)
{
    const char *value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::string(value) : fallback;
}

std::uint16_t env_port(const char *name, std::uint16_t fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return fallback;
    const long parsed = std::strtol(value, nullptr, 10);
    if (parsed <= 0 || parsed > 65535)
        return fallback;
    return static_cast<std::uint16_t>(parsed);
}

double env_double(const char *name, double fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return fallback;
    return std::strtod(value, nullptr);
}

constexpr std::uint32_t kPixelFormatRgb24 = 1;
constexpr std::uint32_t kPixelFormatRgba32 = 2;

} // namespace

namespace app_plugin
{

void DaedalusCameraManager::initialize_endpoints(const app::Context &context)
{
    if (!m_ecs_input)
        m_ecs_input.emplace(context.get_channel_subscriber<ECSData>(this, {}, true));
    if (!m_output)
        m_output.emplace(context.get_buffer_publisher<InputFrame>(this));

    if (!m_receiver)
    {
        m_host = env_or_default("DAEDALUS_HOST", m_host);
        m_port = env_port("DAEDALUS_IMAGE_PORT", m_port);
        m_receiver = std::make_unique<DaedalusImageReceiver>(m_host, m_port);
        m_receiver->start();
        LOG(INFO) << "[DaedalusCameraManager] TCP image endpoint " << m_host << ':' << m_port;
    }

    if (!m_initialized_state)
    {
        const double init_yaw = env_double("DAEDALUS_INITIAL_YAW_DEG", 0.0);
        const double init_pitch = env_double("DAEDALUS_INITIAL_PITCH_DEG", 0.0);
        set_daedalus_gimbal_deg(init_yaw, init_pitch);
        LOG(INFO) << "[DaedalusCameraManager] gimbal estimate initialized: yaw_deg=" << init_yaw
                  << " pitch_deg=" << init_pitch;
        m_initialized_state = true;
    }
}

bool DaedalusCameraManager::pump_frame(cv::Mat &image)
{
    // The runtime drives one process() call per pipeline tick, so this call has to end with a
    // frame: blocking keeps the tick rate locked to the simulator frame rate instead of
    // letting the pipeline spin on "no new frame yet".
    DaedalusImageReceiver::Frame frame;
    int waited_ms = 0;
    while (!m_receiver->wait_frame(frame, 200))
    {
        waited_ms += 200;
        if (waited_ms % 1000 == 0)
            LOG(WARNING) << "[DaedalusCameraManager] waiting for first frame (" << waited_ms / 1000
                         << "s) connected=" << m_receiver->connected();
    }

    int channels = 0;
    int conversion = -1;
    if (frame.format == kPixelFormatRgb24)
    {
        channels = 3;
        conversion = cv::COLOR_RGB2BGR;
    }
    else if (frame.format == kPixelFormatRgba32)
    {
        channels = 4;
        conversion = cv::COLOR_RGBA2BGR;
    }
    else
    {
        LOG(WARNING) << "[DaedalusCameraManager] unsupported pixel format "
                     << static_cast<int>(frame.format);
        return false;
    }

    const std::size_t expected = static_cast<std::size_t>(frame.width) *
                                 static_cast<std::size_t>(frame.height) *
                                 static_cast<std::size_t>(channels);
    if (frame.payload.size() < expected)
    {
        LOG(WARNING) << "[DaedalusCameraManager] payload size " << frame.payload.size() << " < "
                     << expected << " (channels=" << channels << ')';
        return false;
    }

    const cv::Mat packed(static_cast<int>(frame.height), static_cast<int>(frame.width),
                         CV_8UC(channels), frame.payload.data());
    cv::cvtColor(packed, image, conversion);

    if (m_frames % 30 == 0)
        LOG(INFO) << "[DaedalusCameraManager] frame " << m_frames
                  << " seq=" << frame.source_sequence << " size=" << frame.width << 'x'
                  << frame.height << " format=" << frame.format
                  << " capture_ns=" << frame.capture_timestamp_ns;
    ++m_frames;
    return true;
}

void DaedalusCameraManager::process(const app::Context &context)
{
    initialize_endpoints(context);

    cv::Mat image;
    if (!pump_frame(image))
        return;

    const std::shared_ptr<const ECSData> ecs_data = m_ecs_input->wait_next();

    ECSData ecs = *ecs_data;
    ecs.yaw = daedalus_gimbal_state().yaw_deg.load();
    ecs.pitch = daedalus_gimbal_state().pitch_deg.load();
    ecs.roll = 0.0;

    InputFrame frame;
    frame.ecs_data = ecs;
    frame.timestamp = timetool::now();
    frame.img = std::move(image);
    m_output->push(std::move(frame));
}

} // namespace app_plugin

REGISTER_PLUGIN("CameraManager", app_plugin::DaedalusCameraManager)