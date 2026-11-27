#pragma once

#include <atomic>

namespace app_plugin
{

// Daedalus 桥接插件之间共享的云台姿态估计（度）。
// 相机插件把它当作当前云台姿态填进 ECSData，下发插件在每次成功下发后更新它。
// yaw: 0 度 = 车体正前方；pitch: 0 度 = 水平，正数抬头。
struct DaedalusGimbalState
{
    std::atomic<double> yaw_deg{0.0};
    std::atomic<double> pitch_deg{0.0};
};

inline DaedalusGimbalState &daedalus_gimbal_state()
{
    static DaedalusGimbalState state;
    return state;
}

inline void set_daedalus_gimbal_deg(double yaw_deg, double pitch_deg)
{
    daedalus_gimbal_state().yaw_deg.store(yaw_deg);
    daedalus_gimbal_state().pitch_deg.store(pitch_deg);
}

} // namespace app_plugin
