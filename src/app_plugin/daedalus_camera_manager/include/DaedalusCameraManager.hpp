#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "CameraManager.hpp"
#include "DaedalusImageReceiver.hpp"

namespace app_plugin
{

// Pulls images from the Daedalus simulator (TCP 5602, latest-only), converts them to BGR
// and publishes InputFrame. Same shape as ReplayCameraManager so the algorithm layer does
// not care whether the image comes from a video file or the simulator.
class DaedalusCameraManager final : public CameraManager
{
public:
    void process(const app::Context &context) override;

private:
    using EcsSubscriber = LatestChannel<ECSData>::Subscriber;
    using FramePublisher = LatestBuffer<InputFrame>::Publisher;

    void initialize_endpoints(const app::Context &context);
    bool pump_frame(cv::Mat &image);

    std::optional<EcsSubscriber> m_ecs_input;
    std::optional<FramePublisher> m_output;

    std::unique_ptr<DaedalusImageReceiver> m_receiver;
    std::string m_host = "127.0.0.1";
    std::uint16_t m_port = 5602;
    std::uint64_t m_frames = 0;
    bool m_initialized_state = false;
};

} // namespace app_plugin