#include "OscServer.h"
#include "util/Logging.h"
#include <iostream>

namespace OrchFaust {

OscServer::OscServer(OscCommandQueue& queue)
    : commandQueue(queue), running(false), shouldStop(false), portNum(9020) {}

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
    Logger::logInfo("OSC Server starting on port ", portNum, "...");
    try {
        socket = std::make_unique<UdpListeningReceiveSocket>(
            IpEndpointName(IpEndpointName::ANY_ADDRESS, portNum),
            this
        );
        
        running.store(true);
        socket->Run();
    } catch (const std::exception& e) {
        Logger::logWarning("OSC Server socket error: ", e.what());
    }
    
    running.store(false);
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
            }
        }
        else if (address == "/orch_faust/compile") {
            OscCommand cmd{CommandType::Compile, "", "", 0.0f, 0.0f};
            commandQueue.push(cmd);
            Logger::logInfo("OSC: Enqueued compile command");
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
        }
        else if (address == "/orch_faust/all_notes_off") {
            OscCommand cmd{CommandType::AllNotesOff, "", "", 0.0f, 0.0f};
            commandQueue.push(cmd);
            Logger::logInfo("OSC: Enqueued all_notes_off command");
        }
        else if (address == "/orch_faust/status") {
            OscCommand cmd{CommandType::Status, "", "", 0.0f, 0.0f};
            commandQueue.push(cmd);
        }
        else {
            Logger::logWarning("OSC Unknown address pattern: ", address);
        }
    } catch (const std::exception& e) {
        Logger::logError("OSC Message Parse error: ", e.what());
    }
}

} // namespace OrchFaust
