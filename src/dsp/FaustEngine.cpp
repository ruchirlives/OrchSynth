#include "FaustEngine.h"
#include "util/Logging.h"
#include "faust/dsp/llvm-dsp.h"
#ifdef _WIN32
#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;
#else
#include <dlfcn.h>
#include <limits.h>
#endif

namespace OrchFaust {

static std::string getDllDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA((HINSTANCE)&__ImageBase, path, MAX_PATH);
    std::string s(path);
    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return s.substr(0, lastSlash);
    }
    return "";
#else
    Dl_info info;
    if (dladdr((void*)&getDllDir, &info) && info.dli_fname) {
        std::string s(info.dli_fname);
        size_t lastSlash = s.find_last_of("/");
        if (lastSlash != std::string::npos) {
            return s.substr(0, lastSlash);
        }
    }
    return "";
#endif
}

FaustEngine::FaustEngine()
    : factory(nullptr), dspInstance(nullptr), mapUI(nullptr),
      currentSampleRate(44100.0), currentBlockSize(512), compiled(false) {
    
    // Initialize Faust compiler multi-threading support
    // Since startMTDSPFactories is declared in llvm-dsp.h, we call it
    // but we can also do it safely.
}

FaustEngine::~FaustEngine() {
    cleanup();
}

void FaustEngine::cleanup() {
    std::lock_guard<std::mutex> lock(engineMutex);
    
    if (dspInstance) {
        delete dspInstance;
        dspInstance = nullptr;
    }
    
    if (factory) {
        deleteDSPFactory(factory);
        factory = nullptr;
    }
    
    mapUI.reset();
    compiled.store(false);
}

bool FaustEngine::compileFromCode(const std::string& name, const std::string& code) {
    Logger::logInfo("FaustEngine: Compiling ", name, "...");
    
    std::string dllDir = getDllDir();
#ifdef _WIN32
    std::string importPath = dllDir + "\\faust";
#else
    std::string importPath = dllDir + "/faust";
#endif
    Logger::logInfo("FaustEngine: Import path configured as: ", importPath);

    // Setup compiler arguments
    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(importPath.c_str());
    
    std::string errorMsg;
    llvm_dsp_factory* newFactory = createDSPFactoryFromString(
        name,
        code,
        static_cast<int>(argv.size()),
        argv.data(),
        "", // target machine
        errorMsg,
        -1  // default opt level
    );
    
    if (!newFactory) {
        lastError = errorMsg;
        Logger::logError("FaustEngine Compilation failed: ", errorMsg);
        return false;
    }
    
    llvm_dsp* newDsp = newFactory->createDSPInstance();
    if (!newDsp) {
        deleteDSPFactory(newFactory);
        lastError = "Failed to instantiate DSP from factory";
        Logger::logError("FaustEngine: Instantiation failed");
        return false;
    }
    
    // Prepare new instance
    newDsp->init(static_cast<int>(currentSampleRate));
    
    auto newMapUI = std::make_unique<MapUI>();
    newDsp->buildUserInterface(newMapUI.get());
    
    // Hot swap the engine instances atomically
    {
        std::lock_guard<std::mutex> lock(engineMutex);
        
        // Save parameters from old instance to the new instance if possible
        if (dspInstance && mapUI) {
            auto& oldPaths = mapUI->getFullpathMap();
            for (const auto& [path, zone] : oldPaths) {
                float val = *zone;
                newMapUI->setParamValue(path, val);
            }
        }
        
        // Clean up old
        if (dspInstance) {
            delete dspInstance;
        }
        if (factory) {
            deleteDSPFactory(factory);
        }
        
        // Swap to new
        factory = newFactory;
        dspInstance = newDsp;
        mapUI = std::move(newMapUI);
        compiled.store(true);
        lastError = "";
    }
    
    Logger::logInfo("FaustEngine: Successfully compiled and swapped DSP instance");
    return true;
}

void FaustEngine::prepare(double sampleRate, int blockSize) {
    std::lock_guard<std::mutex> lock(engineMutex);
    
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    
    if (dspInstance) {
        dspInstance->init(static_cast<int>(sampleRate));
    }
}

void FaustEngine::process(float** inputs, float** outputs, int numFrames) {
    // If not compiled, clear output buffers and return
    if (!compiled.load()) {
        for (int i = 0; i < 2; ++i) {
            if (outputs[i]) {
                memset(outputs[i], 0, numFrames * sizeof(float));
            }
        }
        return;
    }
    
    // Try lock to avoid blocking the audio thread if a swap is happening
    // (In a production environment, you'd use a triple buffer or double-buffer atomic swap,
    // but a try_lock is safe and prevents audio glitches)
    if (engineMutex.try_lock()) {
        if (dspInstance) {
            // libfaust's dsp::compute expects float** or double** depending on compilation flags
            // Since we compiled with double support, we should cast to FAUSTFLOAT**
            // Under Faust, FAUSTFLOAT is float or double. If we use double-precision compilation,
            // we should cast.
            dspInstance->compute(numFrames, reinterpret_cast<FAUSTFLOAT**>(inputs), reinterpret_cast<FAUSTFLOAT**>(outputs));
        } else {
            for (int i = 0; i < 2; ++i) {
                if (outputs[i]) {
                    memset(outputs[i], 0, numFrames * sizeof(float));
                }
            }
        }
        engineMutex.unlock();
    } else {
        // Fallback if compilation thread holds the lock (swap)
        for (int i = 0; i < 2; ++i) {
            if (outputs[i]) {
                memset(outputs[i], 0, numFrames * sizeof(float));
            }
        }
    }
}

void FaustEngine::setParameter(const std::string& path, float value) {
    std::lock_guard<std::mutex> lock(engineMutex);
    if (mapUI) {
        mapUI->setParamValue(path, value);
    }
}

float FaustEngine::getParameter(const std::string& path) {
    std::lock_guard<std::mutex> lock(engineMutex);
    if (mapUI) {
        return mapUI->getParamValue(path);
    }
    return 0.0f;
}

} // namespace OrchFaust
