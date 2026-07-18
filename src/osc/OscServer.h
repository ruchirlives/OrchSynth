#pragma once

#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
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
    int getPortNum() const { return portNum; }
    void sendGraphState(const std::string& graphJson);
    void sendDebugValue(const std::string& nodeId, float value);

protected:
    void ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) override;

private:
    void run();
    void setReplyTarget(const std::string& host, int port);
    void sendEvent(const std::string& type,
                   const std::string& value1 = "",
                   const std::string& value2 = "",
                   float numeric1 = 0.0f,
                   float numeric2 = 0.0f);

    OscCommandQueue& commandQueue;
    std::atomic<bool> running;
    std::atomic<bool> shouldStop;
    int portNum;
    std::mutex replyMutex;
    std::string replyHost;
    int replyPort;
    std::thread serverThread;
    std::unique_ptr<UdpListeningReceiveSocket> socket;
};

} // namespace OrchFaust
