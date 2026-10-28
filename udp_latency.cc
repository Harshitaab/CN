
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include <iostream>
#include <string>

using namespace ns3;

void RunExperiment(uint32_t delayMs, uint32_t numberOfFlows)
{
    const uint16_t PORT1 = 5000;
    const uint16_t PORT2 = 6000;
    const uint32_t PACKET_SIZE = 1024;
    const double TEST_DURATION = 10.0;

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute(
        "Delay", TimeValue(MilliSeconds(delayMs)));

    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // UDP Server 1: port 5000
    UdpServerHelper server1(PORT1);
    ApplicationContainer serverApp1 = server1.Install(nodes.Get(1));
    serverApp1.Start(Seconds(0.5));
    serverApp1.Stop(Seconds(11.1));

    Ptr<UdpServer> serverPtr1 =
        DynamicCast<UdpServer>(serverApp1.Get(0));

    // UDP Client 1: sends to port 5000
    UdpClientHelper client1(
        interfaces.GetAddress(1), PORT1);

    client1.SetAttribute("PacketSize",
                         UintegerValue(PACKET_SIZE));
    client1.SetAttribute("Interval",
                         TimeValue(MilliSeconds(1)));
    client1.SetAttribute("MaxPackets",
                         UintegerValue(1000000));

    ApplicationContainer clientApp1 = client1.Install(nodes.Get(0));
    clientApp1.Start(Seconds(1.0));
    clientApp1.Stop(Seconds(11.0));

    // Optional second UDP flow on port 6000
    Ptr<UdpServer> serverPtr2 = nullptr;

    if (numberOfFlows == 2)
    {
        UdpServerHelper server2(PORT2);
        ApplicationContainer serverApp2 =
            server2.Install(nodes.Get(1));

        serverApp2.Start(Seconds(0.5));
        serverApp2.Stop(Seconds(11.1));

        serverPtr2 = DynamicCast<UdpServer>(serverApp2.Get(0));

        UdpClientHelper client2(
            interfaces.GetAddress(1), PORT2);

        client2.SetAttribute("PacketSize",
                             UintegerValue(PACKET_SIZE));
        client2.SetAttribute("Interval",
                             TimeValue(MilliSeconds(1)));
        client2.SetAttribute("MaxPackets",
                             UintegerValue(1000000));

        ApplicationContainer clientApp2 =
            client2.Install(nodes.Get(0));

        clientApp2.Start(Seconds(1.0));
        clientApp2.Stop(Seconds(11.0));
    }

    // Enable PCAP capture for Wireshark
    std::string prefix =
        "udp-latency-" + std::to_string(delayMs) +
        "ms-" + std::to_string(numberOfFlows) + "flow";

    p2p.EnablePcap(prefix, devices.Get(0), true);

    Simulator::Stop(Seconds(11.2));
    Simulator::Run();

    // Throughput = received bits / measurement time
    double throughput1 =
        (serverPtr1->GetReceived() * PACKET_SIZE * 8.0) /
        (TEST_DURATION * 1000000.0);

    std::cout << delayMs << " ms\t"
              << numberOfFlows << "\t"
              << throughput1 << " Mbps";

    if (numberOfFlows == 2 && serverPtr2 != nullptr)
    {
        double throughput2 =
            (serverPtr2->GetReceived() * PACKET_SIZE * 8.0) /
            (TEST_DURATION * 1000000.0);

        std::cout << "\t" << throughput2 << " Mbps";
    }

    std::cout << std::endl;

    Simulator::Destroy();
}

int main(int argc, char *argv[])
{
    std::cout << "\n===== ONE UDP FLOW =====\n";
    std::cout << "Latency\tFlows\tFlow 1 Throughput\n";

    RunExperiment(2, 1);
    RunExperiment(5, 1);
    RunExperiment(10, 1);
    RunExperiment(20, 1);

    std::cout << "\n===== TWO UDP FLOWS =====\n";
    std::cout << "Latency\tFlows\tFlow 1\tFlow 2\n";

    RunExperiment(2, 2);
    RunExperiment(5, 2);
    RunExperiment(10, 2);
    RunExperiment(20, 2);

    return 0;
}
