#include "OscServer.h"
#include "util/Logging.h"
#include "osc/OscOutboundPacketStream.h"
#include <iostream>
#include <cstring>
#include <vector>

namespace OrchFaust {

OscServer::OscServer(OscCommandQueue& queue)
    : commandQueue(queue), running(false), shouldStop(false), portNum(9020),
      replyHost("127.0.0.1"), replyPort(0) {}

OscServer::~OscServer() {
    stop();
}

bool OscServer::start(int port) {
    if (running.load()) {
        return true;
    }
    
    portNum = port;
    shouldStop.store(false);
    
    try {
        serverThread = std::thread(&OscServer::run, this);
    } catch (const std::exception& e) {
        Logger::logError("Failed to start OSC server thread: ", e.what());
        return false;
    }
    
    // Wait for the thread to start running
    int waitCount = 0;
    while (!running.load() && waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;
    }
    
    return running.load();
}

void OscServer::stop() {
    shouldStop.store(true);
    
    if (socket) {
        socket->AsynchronousBreak();
    }
    
    if (serverThread.joinable()) {
        serverThread.join();
    }
    
    running.store(false);
    Logger::logInfo("OSC Server stopped.");
}

void OscServer::run() {
    int currentPort = portNum;
    bool bound = false;
    
    while (!bound && currentPort <= 9029) {
        try {
            Logger::logInfo("OSC Server trying port ", currentPort, "...");
            socket = std::make_unique<UdpListeningReceiveSocket>(
                IpEndpointName(IpEndpointName::ANY_ADDRESS, currentPort),
                this
            );
            portNum = currentPort; // update to the successfully bound port
            bound = true;
        } catch (const std::exception& e) {
            Logger::logWarning("OSC Server port ", currentPort, " binding failed: ", e.what());
            currentPort++;
        }
    }
    
    if (bound) {
        Logger::logInfo("OSC Server successfully bound to port ", portNum);
        running.store(true);
        socket->Run();
    } else {
        Logger::logError("OSC Server failed to bind to any port in the range 9020-9029.");
    }
    
    running.store(false);
}

void OscServer::setReplyTarget(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(replyMutex);
    replyHost = host.empty() ? "127.0.0.1" : host;
    replyPort = port;
}

void OscServer::sendEvent(const std::string& type,
                          const std::string& value1,
                          const std::string& value2,
                          float numeric1,
                          float numeric2) {
    std::string host;
    int targetPort = 0;
    {
        std::lock_guard<std::mutex> lock(replyMutex);
        host = replyHost;
        targetPort = replyPort;
    }

    if (targetPort <= 0) {
        return;
    }

    try {
        UdpTransmitSocket transmitSocket(IpEndpointName(host.c_str(), targetPort));
        char buffer[2048];
        osc::OutboundPacketStream p(buffer, sizeof(buffer));
        p << osc::BeginMessage("/orch_faust/event")
          << portNum
          << type.c_str()
          << value1.c_str()
          << value2.c_str()
          << numeric1
          << numeric2
          << osc::EndMessage;

        transmitSocket.Send(p.Data(), p.Size());
    } catch (const std::exception& e) {
        Logger::logError("OSC: Failed to send editor event: ", e.what());
    }
}

void OscServer::sendGraphState(const std::string& graphJson) {
    std::string host;
    int targetPort = 0;
    {
        std::lock_guard<std::mutex> lock(replyMutex);
        host = replyHost;
        targetPort = replyPort;
    }
    if (targetPort <= 0) return;

    try {
        UdpTransmitSocket transmitSocket(IpEndpointName(host.c_str(), targetPort));
        std::vector<char> buffer(graphJson.size() + 512);
        osc::OutboundPacketStream packet(buffer.data(), static_cast<int>(buffer.size()));
        packet << osc::BeginMessage("/orch_faust/event")
               << portNum << "graph_state" << graphJson.c_str() << "" << 0.0f << 0.0f
               << osc::EndMessage;
        transmitSocket.Send(packet.Data(), packet.Size());
    } catch (const std::exception& e) {
        Logger::logError("OSC: Failed to send graph state: ", e.what());
    }
}

void OscServer::sendDebugValue(const std::string& nodeId, float value) {
    sendEvent("debug_value", nodeId, "", value, 0.0f);
}

void OscServer::ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) {
    std::string address = m.AddressPattern();
    
    try {
        if (address == "/orch_faust/load_graph") {
            osc::ReceivedMessage::const_iterator arg = m.ArgumentsBegin();
            if (arg != m.ArgumentsEnd() && arg->IsString()) {
                std::string jsonStr = arg->AsString();
                OscCommand cmd{CommandType::LoadGraph, jsonStr, "", 0.0f, 0.0f};
                commandQueue.push(cmd);
                Logger::logInfo("OSC: Enqueued load_graph command");
                sendEvent("graph_loaded", "", "", static_cast<float>(jsonStr.size()), 0.0f);
            }
        }
        else if (address == "/orch_faust/compile") {
            OscCommand cmd{CommandType::Compile, "", "", 0.0f, 0.0f};
            commandQueue.push(cmd);
            Logger::logInfo("OSC: Enqueued compile command");
            sendEvent("compile_requested");
        }
        else if (address == "/orch_faust/set_param") {
            auto arg = m.ArgumentsBegin();
            if (arg != m.ArgumentsEnd() && arg->IsString()) {
                std::string nodeId = arg->AsString();
                arg++;
                if (arg != m.ArgumentsEnd() && arg->IsString()) {
                    std::string param = arg->AsString();
                    arg++;
                    if (arg != m.ArgumentsEnd()) {
                        float val = 0.0f;
                        if (arg->IsFloat()) val = arg->AsFloat();
                        else if (arg->IsDouble()) val = (float)arg->AsDouble();
                        else if (arg->IsInt32()) val = (float)arg->AsInt32();
                        
                        OscCommand cmd{CommandType::SetParam, nodeId, param, val, 0.0f};
                        commandQueue.push(cmd);
                        sendEvent("param_changed", nodeId, param, val, 0.0f);
                    }
                }
            }
        }
        else if (address == "/orch_faust/note_on") {
            auto arg = m.ArgumentsBegin();
            float pitch = 0.0f;
            float velocity = 1.0f;
            
            if (arg != m.ArgumentsEnd()) {
                if (arg->IsFloat()) pitch = arg->AsFloat();
                else if (arg->IsDouble()) pitch = (float)arg->AsDouble();
                else if (arg->IsInt32()) pitch = (float)arg->AsInt32();
                arg++;
            }
            if (arg != m.ArgumentsEnd()) {
                if (arg->IsFloat()) velocity = arg->AsFloat();
                else if (arg->IsDouble()) velocity = (float)arg->AsDouble();
                else if (arg->IsInt32()) velocity = (float)arg->AsInt32();
            }
            
            OscCommand cmd{CommandType::NoteOn, "", "", pitch, velocity};
            commandQueue.push(cmd);
            sendEvent("note_on", "", "", pitch, velocity);
        }
        else if (address == "/orch_faust/note_off") {
            auto arg = m.ArgumentsBegin();
            float pitch = 0.0f;
            if (arg != m.ArgumentsEnd()) {
                if (arg->IsFloat()) pitch = arg->AsFloat();
                else if (arg->IsDouble()) pitch = (float)arg->AsDouble();
                else if (arg->IsInt32()) pitch = (float)arg->AsInt32();
            }
            
            OscCommand cmd{CommandType::NoteOff, "", "", pitch, 0.0f};
            commandQueue.push(cmd);
            sendEvent("note_off", "", "", pitch, 0.0f);
        }
        else if (address == "/orch_faust/all_notes_off") {
            OscCommand cmd{CommandType::AllNotesOff, "", "", 0.0f, 0.0f};
            commandQueue.push(cmd);
            Logger::logInfo("OSC: Enqueued all_notes_off command");
            sendEvent("all_notes_off");
        }
        else if (address == "/orch_faust/status") {
            OscCommand cmd{CommandType::Status, "", "", 0.0f, 0.0f};
            commandQueue.push(cmd);
        }
        else if (address == "/orch_faust/request_graph") {
            commandQueue.push(OscCommand{CommandType::RequestGraph, "", "", 0.0f, 0.0f});
        }
        else if (address == "/orch_faust/request_debug_value") {
            auto arg = m.ArgumentsBegin();
            if (arg != m.ArgumentsEnd() && arg->IsString()) {
                commandQueue.push(OscCommand{CommandType::RequestDebugValue, arg->AsString(), "", 0.0f, 0.0f});
            }
        }
        else if (address == "/orch_faust/ping") {
            Logger::logInfo("OSC: Received ping from ", remoteEndpoint.address, ":", remoteEndpoint.port);
            char ip[IpEndpointName::ADDRESS_STRING_LENGTH];
            remoteEndpoint.AddressAsString(ip);
            
            try {
                UdpTransmitSocket transmitSocket(IpEndpointName(ip, remoteEndpoint.port));
                
                char buffer[1024];
                osc::OutboundPacketStream p(buffer, sizeof(buffer));
                p << osc::BeginMessage("/orch_faust/pong")
                  << "Orch Synth"
                  << portNum
                  << osc::EndMessage;
                  
                transmitSocket.Send(p.Data(), p.Size());
                Logger::logInfo("OSC: Sent pong reply to ", ip, ":", remoteEndpoint.port);
            } catch (const std::exception& e) {
                Logger::logError("OSC: Failed to send pong reply: ", e.what());
            }
        }
        else if (address == "/orch_faust/set_reply_target") {
            auto arg = m.ArgumentsBegin();
            std::string host = "127.0.0.1";
            int targetPort = 0;

            if (arg != m.ArgumentsEnd() && arg->IsString()) {
                host = arg->AsString();
                arg++;
            }
            if (arg != m.ArgumentsEnd()) {
                if (arg->IsInt32()) targetPort = arg->AsInt32();
                else if (arg->IsFloat()) targetPort = static_cast<int>(arg->AsFloat());
                else if (arg->IsDouble()) targetPort = static_cast<int>(arg->AsDouble());
            }

            setReplyTarget(host, targetPort);
            Logger::logInfo("OSC: Editor reply target set to ", host, ":", targetPort);
            sendEvent("reply_target_set", host, "", static_cast<float>(targetPort), 0.0f);
        }
        else {
            Logger::logWarning("OSC Unknown address pattern: ", address);
        }
    } catch (const std::exception& e) {
        Logger::logError("OSC Message Parse error: ", e.what());
    }
}

} // namespace OrchFaust
