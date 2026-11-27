#include "DaedalusSendEncoder.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

#include <glog/logging.h>

#include "DaedalusGimbalState.hpp"
#include "class_loader.hpp"

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

} // namespace

namespace app_plugin
{

void DaedalusSendEncoder::initialize(const app::Context &context)
{
    if (!m_input)
        m_input.emplace(context.get_buffer_subscriber<FireResult>(this));

    if (!m_client)
    {
        m_host = env_or_default("DAEDALUS_HOST", m_host);
        m_port = env_port("DAEDALUS_COMMAND_PORT", m_port);
        m_client = std::make_unique<daedalus::sim::sdk::v1::UdpGimbalClient>(
            daedalus::sim::sdk::v1::UdpEndpoint{m_host, m_port});
        LOG(INFO) << "[DaedalusSendEncoder] UDP gimbal endpoint " << m_host << ':' << m_port;
    }

    m_initialized = true;
}

void DaedalusSendEncoder::process(const app::Context &context)
{
    if (!m_initialized)
        initialize(context);

    const FireResult result = m_input->wait_pop();

    if (!std::isfinite(result.yaw) || !std::isfinite(result.pitch))
    {
        LOG(ERROR) << "[DaedalusSendEncoder] rejected non-finite result: yaw_deg=" << result.yaw
                   << ", pitch_deg=" << result.pitch;
        return;
    }

    if (!result.is_find_target)
    {
        if (m_held % 150 == 0)
            LOG(INFO) << "[DaedalusSendEncoder] hold: no target, keep current gimbal pose (held="
                      << m_held << ')';
        ++m_held;
        return;
    }

    daedalus::sim::sdk::v1::UdpGimbalCommand command;
    // Daedalus: gimbal pitch 90 deg == level, >90 looks up, <90 looks down.
    // RP-26Rune FireResult uses 0 deg == level, so shift by +90 here.
    float sim_pitch = static_cast<float>(result.pitch) + 90.0F;
    if (sim_pitch < 45.0F) sim_pitch = 45.0F;
    if (sim_pitch > 135.0F) sim_pitch = 135.0F;
    command.yaw_deg = static_cast<float>(result.yaw);
    command.pitch_deg = sim_pitch;
    command.fire_advice = result.is_enable_fire;

    const daedalus::sim::sdk::v1::ClientStatus status = m_client->send(command);
    if (!status.ok())
    {
        LOG(WARNING) << "[DaedalusSendEncoder] send failed: " << status.message;
        return;
    }

    set_daedalus_gimbal_deg(static_cast<double>(command.yaw_deg.value()),
                            static_cast<double>(command.pitch_deg.value()) - 90.0);

    if (result.is_enable_fire != m_fire_active)
    {
        m_fire_active = result.is_enable_fire;
        LOG(INFO) << "[DaedalusSendEncoder] fire_advice -> " << (m_fire_active ? "ON" : "OFF")
                  << " (mode=" << static_cast<int>(result.mode)
                  << ", yaw=" << command.yaw_deg.value() << ", pitch=" << command.pitch_deg.value()
                  << ')';
    }

    if (m_sent % 120 == 0)
        LOG(INFO) << "[DaedalusSendEncoder] sent " << m_sent << " commands, last yaw="
                  << command.yaw_deg.value() << " pitch=" << command.pitch_deg.value();
    ++m_sent;
}

} // namespace app_plugin

REGISTER_PLUGIN("SendEncoder", app_plugin::DaedalusSendEncoder)
