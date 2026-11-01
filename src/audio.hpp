#pragma once
#include "core.hpp"
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace whip {
class Audio final:public IXAudio2EngineCallback {
public:
    Audio();
    ~Audio();
    void play(bool hit,std::uint64_t gesture,double speed);
    void cancel(std::uint64_t gesture);
    void stopAll();
    void recover();
    double latency() const {return latency_.load();}
    bool available() const {return available_.load();}
    struct Stats {std::array<unsigned,3> loaded{},submitted{},observed{};};
    Stats stats() const;
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override;
private:
    enum class Kind {Hit,Swing,Cancel,Stop};
    struct Command {Kind kind;std::uint64_t gesture;double speed;};
    struct Voice {IXAudio2SourceVoice* value{};std::uint64_t gesture{},startSample{};int sample=-1;bool hit{};};
    void post(Command command);
    void run();
    bool initialize();
    void release();
    void process(const Command& command);
    std::array<PcmWave,3> samples_{};
    std::array<Voice,24> voices_{};
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_{};
    HANDLE wake_{};
    std::thread thread_;
    std::mutex mutex_;
    std::deque<Command> commands_;
    std::atomic<bool> quit_{},recovery_{},available_{};
    std::atomic<double> latency_{0.020};
    unsigned random_=0x51324;
    int lastSwing_=2;
    std::array<std::atomic<unsigned>,3> submitted_{},observed_{};
    bool running_{};
};
}
