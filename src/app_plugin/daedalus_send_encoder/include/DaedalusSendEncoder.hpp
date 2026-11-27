#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "SendEncoder.hpp"
#include "daedalus_sim_sdk/udp_gimbal_client.hpp"

namespace app_plugin
{

// 把火控解算出的 yaw/pitch 与开火标志下发给 Daedalus 仿真器（UDP 5601）。
class DaedalusSendEncoder final : public SendEncoder
{
public:
    void process(const app::Context &context) override;

private:
    using FireSubscriber = LatestBuffer<FireResult>::Subscriber;

    void initialize(const app::Context &context);

    std::optional<FireSubscriber> m_input;
    std::unique_ptr<daedalus::sim::sdk::v1::UdpGimbalClient> m_client;
    std::string m_host = std::string(daedalus::sim::sdk::v1::kDefaultHost);
    std::uint16_t m_port = daedalus::sim::sdk::v1::kUdpCommandPort;
    std::uint64_t m_sent = 0;
    std::uint64_t m_held = 0;
    bool m_initialized = false;
    bool m_fire_active = false;
};

} // namespace app_plugin
