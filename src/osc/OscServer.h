#pragma once

#include <thread>
#include <atomic>
#include <memory>
#include "osc/OscPacketListener.h"
#include "ip/UdpSocket.h"
#include "OscCommandQueue.h"

namespace OrchFaust {

class OscServer : public osc::OscPacketListener {
public:
    OscServer(OscCommandQueue& queue);
    ~OscServer();

    bool start(int port = 9020);
    void stop();
    bool isRunning() const { return running.load(); }

protected:
    void ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) override;

private:
    void run();

    OscCommandQueue& commandQueue;
    std::atomic<bool> running;
    std::atomic<bool> shouldStop;
    int portNum;
    std::thread serverThread;
    std::unique_ptr<UdpListeningReceiveSocket> socket;
};

} // namespace OrchFaust
