
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-header.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstdint>

using namespace ns3;

static uint64_t g_tcpRetransmissions = 0;

static void
TcpRetransmissionTrace(Ptr<const Packet> packet,
                       const TcpHeader &header,
                       const Address &localAddress,
                       const Address &peerAddress,
                       Ptr<const TcpSocketBase> socket)
{
    ++g_tcpRetransmissions;
}

static void
ConnectTcpRetransmissionTrace()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/Retransmission",
        MakeCallback(&TcpRetransmissionTrace));
}

struct ExperimentConfig
{
    double bottleneckMbps;
    double udpMbps;
    uint32_t queuePackets;
    uint32_t bottleneckDelayMs;
    std::string name;
};

static void
RunExperiment(const ExperimentConfig &cfg, std::ofstream &csv)
{
    g_tcpRetransmissions = 0;

    const double SIMULATION_TIME = 20.0;
    const double APP_START = 1.0;
    const double APP_STOP = 19.0;
    const double MEASUREMENT_TIME = APP_STOP - APP_START;

    const uint16_t TCP_PORT = 5000;
    const uint16_t UDP_PORT = 6000;

    // Configure TCP segment size.
    Config::SetDefault(
        "ns3::TcpSocket::SegmentSize",
        UintegerValue(1024));

    // Create four end nodes and two routers.
    Ptr<Node> n0 = CreateObject<Node>();
    Ptr<Node> n1 = CreateObject<Node>();
    Ptr<Node> n2 = CreateObject<Node>();
    Ptr<Node> n3 = CreateObject<Node>();
    Ptr<Node> r1 = CreateObject<Node>();
    Ptr<Node> r2 = CreateObject<Node>();

    NodeContainer allNodes;
    allNodes.Add(n0);
    allNodes.Add(n1);
    allNodes.Add(n2);
    allNodes.Add(n3);
    allNodes.Add(r1);
    allNodes.Add(r2);

    // Install Internet stack on all six nodes.
    InternetStackHelper internet;
    internet.Install(allNodes);

    // Access links: 10 Mbps, 2 ms, DropTail queue of 50 packets.
    PointToPointHelper access;
    access.SetDeviceAttribute(
        "DataRate", StringValue("10Mbps"));
    access.SetChannelAttribute(
        "Delay", StringValue("2ms"));
    access.SetQueue(
        "ns3::DropTailQueue",
        "MaxSize", StringValue("50p"));

    // Bottleneck link: configurable bandwidth, delay, and queue size.
    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute(
        "DataRate",
        StringValue(std::to_string(cfg.bottleneckMbps) + "Mbps"));
    bottleneck.SetChannelAttribute(
        "Delay",
        TimeValue(MilliSeconds(cfg.bottleneckDelayMs)));
    bottleneck.SetQueue(
        "ns3::DropTailQueue",
        "MaxSize",
        StringValue(std::to_string(cfg.queuePackets) + "p"));

    // Topology:
    //
    // Node 0 ----\
    //             Router 1 ===== Router 2 ---- Node 2
    // Node 1 ----/    bottleneck              Node 3
    //
    // TCP: Node 0 -> Node 3
    // UDP: Node 1 -> Node 2

    NetDeviceContainer d0r1;
    NetDeviceContainer d1r1;
    NetDeviceContainer dr1r2;
    NetDeviceContainer dr2n2;
    NetDeviceContainer dr2n3;

    NodeContainer link;

    link.Add(n0);
    link.Add(r1);
    d0r1 = access.Install(link);
    link = NodeContainer();

    link.Add(n1);
    link.Add(r1);
    d1r1 = access.Install(link);
    link = NodeContainer();

    link.Add(r1);
    link.Add(r2);
    dr1r2 = bottleneck.Install(link);
    link = NodeContainer();

    link.Add(r2);
    link.Add(n2);
    dr2n2 = access.Install(link);
    link = NodeContainer();

    link.Add(r2);
    link.Add(n3);
    dr2n3 = access.Install(link);

    // Assign separate IPv4 subnets to each link.
    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer if0r1 = address.Assign(d0r1);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer if1r1 = address.Assign(d1r1);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ifr1r2 = address.Assign(dr1r2);

    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer ifr2n2 = address.Assign(dr2n2);

    address.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer ifr2n3 = address.Assign(dr2n3);

    // Enable routing between all subnets.
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // TCP receiver on Node 3, port 5000.
    PacketSinkHelper tcpSinkHelper(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), TCP_PORT));

    ApplicationContainer tcpSink =
        tcpSinkHelper.Install(n3);

    tcpSink.Start(Seconds(0.0));
    tcpSink.Stop(Seconds(SIMULATION_TIME));

    // UDP receiver on Node 2, port 6000.
    PacketSinkHelper udpSinkHelper(
        "ns3::UdpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), UDP_PORT));

    ApplicationContainer udpSink =
        udpSinkHelper.Install(n2);

    udpSink.Start(Seconds(0.0));
    udpSink.Stop(Seconds(SIMULATION_TIME));

    // TCP sender: Node 0 -> Node 3.
    OnOffHelper tcpClient(
        "ns3::TcpSocketFactory",
        InetSocketAddress(ifr2n3.GetAddress(1), TCP_PORT));

    tcpClient.SetAttribute(
        "DataRate", StringValue("10Mbps"));
    tcpClient.SetAttribute(
        "PacketSize", UintegerValue(1024));
    tcpClient.SetAttribute(
        "OnTime",
        StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    tcpClient.SetAttribute(
        "OffTime",
        StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer tcpApp =
        tcpClient.Install(n0);

    tcpApp.Start(Seconds(APP_START));
    tcpApp.Stop(Seconds(APP_STOP));

    // UDP sender: Node 1 -> Node 2.
    OnOffHelper udpClient(
        "ns3::UdpSocketFactory",
        InetSocketAddress(ifr2n2.GetAddress(1), UDP_PORT));

    udpClient.SetAttribute(
        "DataRate",
        StringValue(std::to_string(cfg.udpMbps) + "Mbps"));
    udpClient.SetAttribute(
        "PacketSize", UintegerValue(512));
    udpClient.SetAttribute(
        "OnTime",
        StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    udpClient.SetAttribute(
        "OffTime",
        StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer udpApp =
        udpClient.Install(n1);

    udpApp.Start(Seconds(APP_START));
    udpApp.Stop(Seconds(APP_STOP));

    // Attach TCP retransmission trace after the TCP socket is created.
    Simulator::Schedule(
        Seconds(1.2), &ConnectTcpRetransmissionTrace);

    // Enable PCAP tracing on both ends of the bottleneck.
    const std::string prefix =
        "tcpudp-" + cfg.name;

    bottleneck.EnablePcap(
        prefix, dr1r2.Get(0), true);
    bottleneck.EnablePcap(
        prefix, dr1r2.Get(1), true);

    // FlowMonitor records packet-level flow statistics.
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.Install(allNodes);

    Simulator::Stop(Seconds(SIMULATION_TIME));
    Simulator::Run();

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(
            flowHelper.GetClassifier());

    FlowMonitor::FlowStatsContainer stats =
        monitor->GetFlowStats();

    double tcpTxPackets = 0;
    double tcpRxPackets = 0;
    double tcpRxBytes = 0;
    double tcpDelaySum = 0;

    double udpTxPackets = 0;
    double udpRxPackets = 0;
    double udpRxBytes = 0;
    double udpDelaySum = 0;

    for (const auto &entry : stats)
    {
        Ipv4FlowClassifier::FiveTuple tuple =
            classifier->FindFlow(entry.first);

        const FlowMonitor::FlowStats &s = entry.second;

        // Match only forward application flows.
        if (tuple.destinationPort == TCP_PORT &&
            tuple.protocol == 6)
        {
            tcpTxPackets = s.txPackets;
            tcpRxPackets = s.rxPackets;
            tcpRxBytes = s.rxBytes;
            tcpDelaySum = s.delaySum.GetSeconds();
        }
        else if (tuple.destinationPort == UDP_PORT &&
                 tuple.protocol == 17)
        {
            udpTxPackets = s.txPackets;
            udpRxPackets = s.rxPackets;
            udpRxBytes = s.rxBytes;
            udpDelaySum = s.delaySum.GetSeconds();
        }
    }

    double tcpLossPercent = 0.0;
    double udpLossPercent = 0.0;

    if (tcpTxPackets > 0)
        tcpLossPercent =
            100.0 * (tcpTxPackets - tcpRxPackets) / tcpTxPackets;

    if (udpTxPackets > 0)
        udpLossPercent =
            100.0 * (udpTxPackets - udpRxPackets) / udpTxPackets;

    double tcpThroughput =
        tcpRxBytes * 8.0 /
        (MEASUREMENT_TIME * 1000000.0);

    double udpThroughput =
        udpRxBytes * 8.0 /
        (MEASUREMENT_TIME * 1000000.0);

    double tcpAvgDelayMs = 0.0;
    double udpAvgDelayMs = 0.0;

    if (tcpRxPackets > 0)
        tcpAvgDelayMs =
            1000.0 * tcpDelaySum / tcpRxPackets;

    if (udpRxPackets > 0)
        udpAvgDelayMs =
            1000.0 * udpDelaySum / udpRxPackets;

    double tcpRetransmissionsPerSecond =
        g_tcpRetransmissions / MEASUREMENT_TIME;

    std::cout << std::fixed << std::setprecision(3);

    std::cout << "\n========================================\n";
    std::cout << "Experiment: " << cfg.name << "\n";
    std::cout << "Bottleneck bandwidth: "
              << cfg.bottleneckMbps << " Mbps\n";
    std::cout << "UDP offered rate: "
              << cfg.udpMbps << " Mbps\n";
    std::cout << "Queue size: "
              << cfg.queuePackets << " packets\n";
    std::cout << "Bottleneck delay: "
              << cfg.bottleneckDelayMs << " ms\n";

    std::cout << "\nTCP Results (Node 0 -> Node 3)\n";
    std::cout << "TX packets: " << tcpTxPackets << "\n";
    std::cout << "RX packets: " << tcpRxPackets << "\n";
    std::cout << "Packet loss: " << tcpLossPercent << " %\n";
    std::cout << "Throughput: " << tcpThroughput << " Mbps\n";
    std::cout << "Average delay: " << tcpAvgDelayMs << " ms\n";
    std::cout << "Retransmission events: "
              << g_tcpRetransmissions << "\n";
    std::cout << "Retransmissions per second: "
              << tcpRetransmissionsPerSecond << "\n";

    std::cout << "\nUDP Results (Node 1 -> Node 2)\n";
    std::cout << "TX packets: " << udpTxPackets << "\n";
    std::cout << "RX packets: " << udpRxPackets << "\n";
    std::cout << "Packet loss: " << udpLossPercent << " %\n";
    std::cout << "Throughput: " << udpThroughput << " Mbps\n";
    std::cout << "Average delay: " << udpAvgDelayMs << " ms\n";

    // One row per experiment, including both flows.
    csv << cfg.name << ","
        << cfg.bottleneckMbps << ","
        << cfg.udpMbps << ","
        << cfg.queuePackets << ","
        << cfg.bottleneckDelayMs << ","
        << tcpTxPackets << ","
        << tcpRxPackets << ","
        << tcpLossPercent << ","
        << tcpThroughput << ","
        << tcpAvgDelayMs << ","
        << g_tcpRetransmissions << ","
        << tcpRetransmissionsPerSecond << ","
        << udpTxPackets << ","
        << udpRxPackets << ","
        << udpLossPercent << ","
        << udpThroughput << ","
        << udpAvgDelayMs << "\n";

    csv.flush();

    Simulator::Destroy();
}

int
main(int argc, char *argv[])
{
    // The default run includes all required and optional experiments.
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    std::ofstream csv("tcp_udp_results.csv");

    if (!csv.is_open())
    {
        std::cerr << "Cannot create tcp_udp_results.csv\n";
        return 1;
    }

    csv << "Experiment,BottleneckMbps,UDP_Mbps,QueuePackets,"
        << "BottleneckDelayMs,TCP_TxPackets,TCP_RxPackets,"
        << "TCP_LossPercent,TCP_ThroughputMbps,TCP_AvgDelayMs,"
        << "TCP_Retransmissions,TCP_RetransmissionsPerSecond,"
        << "UDP_TxPackets,UDP_RxPackets,UDP_LossPercent,"
        << "UDP_ThroughputMbps,UDP_AvgDelayMs\n";

    // Experiment 1: bottleneck bandwidth.
    RunExperiment({2.0, 1.0, 50, 10, "bw_2Mbps"}, csv);
    RunExperiment({5.0, 1.0, 50, 10, "bw_5Mbps"}, csv);
    RunExperiment({10.0, 1.0, 50, 10, "bw_10Mbps"}, csv);

    // Experiment 2: UDP sending rate.
    RunExperiment({2.0, 0.5, 50, 10, "udp_500Kbps"}, csv);
    RunExperiment({2.0, 1.0, 50, 10, "udp_1Mbps"}, csv);
    RunExperiment({2.0, 2.0, 50, 10, "udp_2Mbps"}, csv);
    RunExperiment({2.0, 5.0, 50, 10, "udp_5Mbps"}, csv);

    // Experiment 3: bottleneck queue size.
    RunExperiment({2.0, 1.0, 20, 10, "queue_20"}, csv);
    RunExperiment({2.0, 1.0, 50, 10, "queue_50"}, csv);
    RunExperiment({2.0, 1.0, 100, 10, "queue_100"}, csv);

    // Experiment 4: bottleneck propagation delay.
    RunExperiment({2.0, 1.0, 50, 5, "delay_5ms"}, csv);
    RunExperiment({2.0, 1.0, 50, 10, "delay_10ms"}, csv);
    RunExperiment({2.0, 1.0, 50, 20, "delay_20ms"}, csv);
    RunExperiment({2.0, 1.0, 50, 50, "delay_50ms"}, csv);

    csv.close();

    std::cout << "\nAll experiments completed.\n";
    std::cout << "Results saved in tcp_udp_results.csv\n";
    std::cout << "PCAP files use the prefix tcpudp-*.pcap\n";

    return 0;
}
