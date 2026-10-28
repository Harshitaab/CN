
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>

using namespace ns3;

// Number of vehicles transmitting to the traffic server.
static const uint32_t NUM_VEHICLES = 5;

// TCP congestion window trace: record time and window in bytes.
static void
CwndChange(Ptr<OutputStreamWrapper> stream,
           uint32_t oldCwnd,
           uint32_t newCwnd)
{
    *stream->GetStream()
        << Simulator::Now().GetSeconds() << "\t"
        << newCwnd << std::endl;
}

// Connect a congestion-window trace to a vehicle's TCP socket.
static void
TraceCwnd(uint32_t nodeId,
          Ptr<OutputStreamWrapper> stream)
{
    std::string path =
        "/NodeList/" + std::to_string(nodeId) +
        "/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow";

    Config::ConnectWithoutContext(
        path, MakeBoundCallback(&CwndChange, stream));
}

int
main(int argc, char *argv[])
{
    // Select the TCP congestion-control algorithm from the command line.
    // Examples: ns3::TcpNewReno, ns3::TcpCubic, ns3::TcpBbr
    std::string tcpType = "ns3::TcpNewReno";

    // Output offered load from each vehicle.
    std::string vehicleRate = "2Mbps";

    // Shared bottleneck parameters.
    std::string bottleneckRate = "2Mbps";
    std::string bottleneckDelay = "10ms";
    uint32_t queuePackets = 50;

    double simulationTime = 20.0;
    uint16_t port = 5000;

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpType",
                 "TCP algorithm: ns3::TcpNewReno, ns3::TcpCubic, ns3::TcpBbr",
                 tcpType);
    cmd.AddValue("vehicleRate",
                 "Sending rate for each vehicle",
                 vehicleRate);
    cmd.AddValue("bottleneckRate",
                 "Roadside-to-server bottleneck bandwidth",
                 bottleneckRate);
    cmd.AddValue("bottleneckDelay",
                 "Bottleneck propagation delay",
                 bottleneckDelay);
    cmd.AddValue("queuePackets",
                 "Bottleneck DropTail queue size in packets",
                 queuePackets);
    cmd.Parse(argc, argv);

    // Check that the requested TCP implementation exists in this NS-3 build.
    TypeId tcpTid;
    if (!TypeId::LookupByNameFailSafe(tcpType, &tcpTid))
    {
        std::cerr << "ERROR: TCP type " << tcpType
                  << " is unavailable in this NS-3 installation.\n";
        std::cerr << "Try ns3::TcpNewReno or ns3::TcpCubic, "
                  << "or check whether your version supports ns3::TcpBbr.\n";
        return 1;
    }

    // Apply the selected congestion-control algorithm before installing stacks.
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(tcpTid));

    // TCP segment size and default initial congestion window.
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(1024));

    // Create five vehicles, one roadside router, and one server.
    NodeContainer vehicles;
    vehicles.Create(NUM_VEHICLES);

    Ptr<Node> rsu = CreateObject<Node>();
    Ptr<Node> server = CreateObject<Node>();

    NodeContainer allNodes;
    allNodes.Add(vehicles);
    allNodes.Add(rsu);
    allNodes.Add(server);

    InternetStackHelper internet;
    internet.Install(allNodes);

    // Access links: vehicle-to-RSU, 10 Mbps with 2 ms delay.
    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    access.SetChannelAttribute("Delay", StringValue("2ms"));
    access.SetQueue(
        "ns3::DropTailQueue<Packet>",
        "MaxSize",
        QueueSizeValue(QueueSize("50p")));

    // Shared bottleneck: RSU-to-server link.
    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute(
        "DataRate", StringValue(bottleneckRate));
    bottleneck.SetChannelAttribute(
        "Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue(
        "ns3::DropTailQueue<Packet>",
        "MaxSize",
        QueueSizeValue(
            QueueSize(std::to_string(queuePackets) + "p")));

    // Connect every vehicle to the roadside router.
    std::vector<Ipv4Address> vehicleAddresses;
    Ipv4AddressHelper address;

    for (uint32_t i = 0; i < NUM_VEHICLES; ++i)
    {
        NodeContainer link(vehicles.Get(i), rsu);
        NetDeviceContainer devices = access.Install(link);

        // Every point-to-point link needs a distinct subnet.
        std::string subnet =
            "10.1." + std::to_string(i + 1) + ".0";

        address.SetBase(subnet.c_str(), "255.255.255.0");
        Ipv4InterfaceContainer interfaces =
            address.Assign(devices);

        vehicleAddresses.push_back(interfaces.GetAddress(0));
    }

    // Connect the roadside router to the traffic-control server.
    NodeContainer backhaul(rsu, server);
    NetDeviceContainer backhaulDevices =
        bottleneck.Install(backhaul);

    address.SetBase("10.1.10.0", "255.255.255.0");
    Ipv4InterfaceContainer backhaulInterfaces =
        address.Assign(backhaulDevices);

    Ipv4Address serverAddress = backhaulInterfaces.GetAddress(1);

    // Populate routes so every vehicle can reach the server.
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // TCP server listens for all five vehicle connections.
    PacketSinkHelper sinkHelper(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));

    ApplicationContainer sinkApp = sinkHelper.Install(server);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    // Five TCP OnOff applications generate traffic to the same server.
    for (uint32_t i = 0; i < NUM_VEHICLES; ++i)
    {
        OnOffHelper client(
            "ns3::TcpSocketFactory",
            InetSocketAddress(serverAddress, port));

        client.SetAttribute(
            "DataRate", StringValue(vehicleRate));
        client.SetAttribute(
            "PacketSize", UintegerValue(1024));

        // Continuous traffic: OnTime=1 second, OffTime=0 seconds.
        client.SetAttribute(
            "OnTime",
            StringValue(
                "ns3::ConstantRandomVariable[Constant=1]"));
        client.SetAttribute(
            "OffTime",
            StringValue(
                "ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer app =
            client.Install(vehicles.Get(i));

        app.Start(Seconds(1.0));
        app.Stop(Seconds(19.0));
    }

    // Enable PCAP packet capture on both ends of the bottleneck.
    bottleneck.EnablePcap(
        "vanet-bottleneck",
        backhaulDevices.Get(0), true);
    bottleneck.EnablePcap(
        "vanet-bottleneck",
        backhaulDevices.Get(1), true);

    // Congestion-window files: time (seconds), cwnd (bytes).
    AsciiTraceHelper ascii;
    for (uint32_t i = 0; i < NUM_VEHICLES; ++i)
    {
        std::string filename =
            "cwnd_" + std::to_string(i) + ".dat";

        Ptr<OutputStreamWrapper> stream =
            ascii.CreateFileStream(filename);

        // Connect after applications have created their TCP sockets.
        Simulator::Schedule(
            Seconds(1.1), &TraceCwnd, vehicles.Get(i)->GetId(), stream);
    }

    // FlowMonitor collects transmitted/received packets and delays.
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    std::cout << "\n====================================\n";
    std::cout << "VANET TCP CONGESTION CONTROL SIMULATION\n";
    std::cout << "TCP algorithm: " << tcpType << "\n";
    std::cout << "Vehicles: " << NUM_VEHICLES << "\n";
    std::cout << "Per-vehicle offered rate: " << vehicleRate << "\n";
    std::cout << "Bottleneck bandwidth: " << bottleneckRate << "\n";
    std::cout << "Bottleneck delay: " << bottleneckDelay << "\n";
    std::cout << "Queue size: " << queuePackets << " packets\n";
    std::cout << "Simulation time: " << simulationTime << " seconds\n";
    std::cout << "====================================\n";

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(
            flowHelper.GetClassifier());

    FlowMonitor::FlowStatsContainer stats =
        monitor->GetFlowStats();

    double totalTx = 0.0;
    double totalRx = 0.0;
    double totalRxBytes = 0.0;
    double totalDelay = 0.0;

    // Calculate throughput for each vehicle and Jain's fairness index.
    std::vector<double> flowThroughputs(NUM_VEHICLES, 0.0);

    // Applications transmit from 1 to 19 seconds: 18-second window.
    const double measurementTime = 18.0;

    std::string safeName = tcpType;
    for (char &c : safeName)
    {
        if (c == ':' || c == '<' || c == '>')
            c = '_';
    }

    std::ofstream csv("vanet_" + safeName + ".csv");
    csv << "Vehicle,TxPackets,RxPackets,LossPercent,"
        << "ThroughputMbps,AverageDelayMs\n";

    std::cout << "\nPER-VEHICLE RESULTS\n";
    std::cout << "Vehicle\tTX\tRX\tLoss(%)\tThroughput(Mbps)"
              << "\tDelay(ms)\n";

    for (const auto &entry : stats)
    {
        Ipv4FlowClassifier::FiveTuple tuple =
            classifier->FindFlow(entry.first);

        // Ignore reverse TCP ACK flows and unrelated traffic.
        if (tuple.destinationPort != port ||
            tuple.protocol != 6 ||
            tuple.destinationAddress != serverAddress)
        {
            continue;
        }

        int vehicleIndex = -1;
        for (uint32_t i = 0; i < NUM_VEHICLES; ++i)
        {
            if (tuple.sourceAddress == vehicleAddresses[i])
            {
                vehicleIndex = static_cast<int>(i);
                break;
            }
        }

        if (vehicleIndex < 0)
            continue;

        const FlowMonitor::FlowStats &s = entry.second;

        double tx = static_cast<double>(s.txPackets);
        double rx = static_cast<double>(s.rxPackets);

        double lossPercent = 0.0;
        if (tx > 0)
            lossPercent = 100.0 * (tx - rx) / tx;

        double throughput =
            s.rxBytes * 8.0 /
            (measurementTime * 1000000.0);

        double delayMs = 0.0;
        if (s.rxPackets > 0)
            delayMs =
                1000.0 * s.delaySum.GetSeconds() / s.rxPackets;

        flowThroughputs[vehicleIndex] = throughput;

        totalTx += tx;
        totalRx += rx;
        totalRxBytes += s.rxBytes;
        totalDelay += s.delaySum.GetSeconds();

        std::cout << vehicleIndex << "\t"
                  << s.txPackets << "\t"
                  << s.rxPackets << "\t"
                  << std::fixed << std::setprecision(2)
                  << lossPercent << "\t\t"
                  << throughput << "\t\t"
                  << delayMs << "\n";

        csv << vehicleIndex << ","
            << s.txPackets << ","
            << s.rxPackets << ","
            << lossPercent << ","
            << throughput << ","
            << delayMs << "\n";
    }

    double totalLossPercent = 0.0;
    if (totalTx > 0)
        totalLossPercent =
            100.0 * (totalTx - totalRx) / totalTx;

    double aggregateThroughput =
        totalRxBytes * 8.0 /
        (measurementTime * 1000000.0);

    double averageDelayMs = 0.0;
    if (totalRx > 0)
        averageDelayMs = 1000.0 * totalDelay / totalRx;

    // Jain's fairness index: 1 means perfectly equal throughput.
    double sumRates = 0.0;
    double sumSquares = 0.0;

    for (double rate : flowThroughputs)
    {
        sumRates += rate;
        sumSquares += rate * rate;
    }

    double fairness = 0.0;
    if (sumSquares > 0)
    {
        fairness =
            (sumRates * sumRates) /
            (NUM_VEHICLES * sumSquares);
    }

    std::cout << "\nOVERALL RESULTS\n";
    std::cout << "Aggregate throughput: "
              << aggregateThroughput << " Mbps\n";
    std::cout << "Packet loss: "
              << totalLossPercent << " %\n";
    std::cout << "Average end-to-end delay: "
              << averageDelayMs << " ms\n";
    std::cout << "Jain's fairness index: "
              << fairness << "\n";

    csv.close();
    Simulator::Destroy();

    std::cout << "\nResults saved to vanet_" << safeName << ".csv\n";
    std::cout << "Congestion window traces: cwnd_0.dat ... cwnd_4.dat\n";
    std::cout << "PCAP traces: vanet-bottleneck-*.pcap\n";

    return 0;
}
