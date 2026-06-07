#include "ns3/blind-connect-helper.h"
#include "ns3/blind-connect-app.h"

namespace ns3 {

BlindConnectHelper::BlindConnectHelper()
{
    m_factory.SetTypeId("ns3::BlindConnectApp");
}

void
BlindConnectHelper::SetAttribute(std::string name,
                                 const AttributeValue &value)
{
    m_factory.Set(name, value);
}

// ==============================================================
// 【核心修改点】：这里必须是 3 个参数，与 .h 文件严格保持一致
// ==============================================================
ApplicationContainer
BlindConnectHelper::Install(Ptr<Node> node,
                            Ptr<NetDevice> staDev,
                            Ptr<NetDevice> adhocDev)
{
    ApplicationContainer apps;

    Ptr<BlindConnectApp> app = m_factory.Create<BlindConnectApp>();

    // 分别判断并注入两张物理网卡
    if (staDev) {
        app->SetStaDevice(DynamicCast<WifiNetDevice>(staDev));
    }
    if (adhocDev) {
        app->SetAdhocDevice(DynamicCast<WifiNetDevice>(adhocDev));
    }

    node->AddApplication(app);
    app->SetStartTime(Seconds(1.0));

    apps.Add(app);
    return apps;
}

} // namespace ns3
