
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/sixlowpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/propagation-module.h"

#include <iostream>
#include <fstream>
#include <iomanip>

using namespace ns3;

void RunWifi(double speed)
{
    std::cout << "\n========== WI-FI EXPERIMENT ==========\n";
    std::cout << "Mobile speed: " << speed << " m/s\n";

    // Create one gateway and three mobile nodes.
    NodeContainer nodes;
    nodes.Create(4);

    // Configure Wi-Fi channel and propagation loss.
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay(
        "ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss(
        "ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // Configure Wi-Fi standard and data rate.
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue("ErpOfdmRate6Mbps"),
        "ControlMode", StringValue("ErpOfdmRate6Mbps"));

    // Use ad-hoc Wi-Fi communication.
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // Gateway stays at the center of the campus.
    MobilityHelper gatewayMobility;
    gatewayMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");
    gatewayMobility.Install(nodes.Get(0));
    nodes.Get(0)->GetObject<MobilityModel>()->SetPosition(
        Vector(50, 50, 0));

    // Assign starting positions to mobile nodes.
    Ptr<ListPositionAllocator> positions =
        CreateObject<ListPositionAllocator>();
    positions->Add(Vector(10, 10, 0));
    positions->Add(Vector(20, 20, 0));
    positions->Add(Vector(80, 80, 0));

    // RandomWalk mobility with the selected speed.
    MobilityHelper mobility;
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel(
        "ns3::RandomWalk2dMobilityModel",
        "Mode", StringValue("Time"),
        "Time", TimeValue(Seconds(1)),
        "Speed", StringValue(
            "ns3::ConstantRandomVariable[Constant=" +
            std::to_string(speed) + "]"),
        "Bounds", RectangleValue(Rectangle(0, 100, 0, 100)));
    mobility.Install(NodeContainer(nodes.Get(1),
                                   nodes.Get(2),
                                   nodes.Get(3)));

    // Install IP networking.
    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // Gateway listens for UDP traffic on port 9000.
    UdpServerHelper server(9000);
    ApplicationContainer serverApp = server.Install(nodes.Get(0));
    serverApp.Start(Seconds(1));
    serverApp.Stop(Seconds(20));

    // Mobile nodes send UDP packets to the gateway.
    for (uint32_t i = 1; i <= 3; i++)
    {
        UdpClientHelper client(interfaces.GetAddress(0), 9000);
        client.SetAttribute("MaxPackets", UintegerValue(1000));
        client.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
        client.SetAttribute("PacketSize", UintegerValue(512));

        ApplicationContainer app = client.Install(nodes.Get(i));
        app.Start(Seconds(2));
        app.Stop(Seconds(19));
    }

    // Collect throughput, packet loss and delay statistics.
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    // Enable packet capture for Wireshark.
    phy.EnablePcap("assignment11-wifi", devices);

    Simulator::Stop(Seconds(20));
    Simulator::Run();
    monitor->CheckForLostPackets();

    double tx = 0, rx = 0, bytes = 0, delay = 0;

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    auto stats = monitor->GetFlowStats();

    for (auto it = stats.begin(); it != stats.end(); ++it)
    {
        auto tuple = classifier->FindFlow(it->first);

        if (tuple.destinationPort == 9000)
        {
            tx += it->second.txPackets;
            rx += it->second.rxPackets;
            bytes += it->second.rxBytes;
            delay += it->second.delaySum.GetSeconds();
        }
    }

    double throughput = bytes * 8.0 / 17.0 / 1e6;
    double loss = tx > 0 ? (tx - rx) * 100.0 / tx : 0;
    double avgDelay = rx > 0 ? delay * 1000.0 / rx : 0;

    // Estimate received power at the final mobile positions.
    Ptr<FriisPropagationLossModel> friis =
        CreateObject<FriisPropagationLossModel>();
    friis->SetAttribute("Frequency", DoubleValue(2.4e9));

    Ptr<MobilityModel> gateway =
        nodes.Get(0)->GetObject<MobilityModel>();

    double powerSum = 0;
    for (uint32_t i = 1; i <= 3; i++)
    {
        Ptr<MobilityModel> mobile =
            nodes.Get(i)->GetObject<MobilityModel>();

        // Estimated received power for a 16 dBm transmitter.
        double power = friis->CalcRxPower(16, mobile, gateway);
        powerSum += power;
        std::cout << "Node " << i
                  << " estimated received power: "
                  << power << " dBm\n";
    }

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Throughput: " << throughput << " Mbps\n";
    std::cout << "Packet loss: " << loss << " %\n";
    std::cout << "Average delay: " << avgDelay << " ms\n";
    std::cout << "Average received power: "
              << powerSum / 3 << " dBm\n";

    std::ofstream csv("wifi_speed_" +
                      std::to_string((int)speed) + ".csv");
    csv << "Speed,Throughput_Mbps,PacketLoss_percent,"
           "Delay_ms,EstimatedPower_dBm\n";
    csv << speed << "," << throughput << ","
        << loss << "," << avgDelay << ","
        << powerSum / 3 << "\n";
    csv.close();

    Simulator::Destroy();
}

void RunLrWpan(double speed)
{
    std::cout << "\n========== ZIGBEE-STYLE LR-WPAN ==========\n";
    std::cout << "Mobile speed: " << speed << " m/s\n";

    // IEEE 802.15.4 devices are used as the Zigbee-style radio layer.
    // This is not a complete Zigbee protocol stack.

    NodeContainer nodes;
    nodes.Create(2); // coordinator and one mobile sensor

    // Place the coordinator and mobile sensor.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions =
        CreateObject<ListPositionAllocator>();
    positions->Add(Vector(50, 50, 0));
    positions->Add(Vector(10, 10, 0));
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // Configure IEEE 802.15.4 PHY and MAC.
    LrWpanHelper lrWpan;
    NetDeviceContainer devices = lrWpan.Install(nodes);
    lrWpan.AssociateToPan(devices, 0);

    // Apply mobility to the sensor.
    Ptr<ConstantVelocityMobilityModel> moving =
        CreateObject<ConstantVelocityMobilityModel>();
    nodes.Get(1)->AggregateObject(moving);
    moving->SetPosition(Vector(10, 10, 0));
    moving->SetVelocity(Vector(speed, 0, 0));

    // Run the radio-layer simulation.
    // IP applications are not configured in this minimal LR-WPAN
    // example, so IP throughput/delay are not reported here.
    Simulator::Stop(Seconds(20));
    Simulator::Run();

    std::cout << "LR-WPAN radio simulation completed.\n";
    std::cout << "IP throughput, loss, and delay need an "
                 "application/stack implementation.\n";

    Simulator::Destroy();
}

int main(int argc, char *argv[])
{
    // Run all three speeds for Wi-Fi and LR-WPAN.
    double speeds[] = {1.0, 5.0, 10.0};

    for (double speed : speeds)
    {
        RunWifi(speed);
        RunLrWpan(speed);
    }

    std::cout << "\nBluetooth is not simulated: "
                 "a compatible Bluetooth model is required.\n";
    std::cout << "All experiments completed.\n";

    return 0;
}
