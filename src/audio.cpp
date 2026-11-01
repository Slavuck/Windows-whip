#include "audio.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cmath>

namespace whip {
Audio::Audio() {
    for(int i=0;i<3;++i) {
        auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101+i),RT_RCDATA);if(!resource) continue;
        const auto data=static_cast<const std::uint8_t*>(LockResource(LoadResource(nullptr,resource)));
        if(data) if(auto wave=parseWave({data,SizeofResource(nullptr,resource)})) samples_[static_cast<std::size_t>(i)]=std::move(*wave);
    }
    wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    thread_=std::thread([this]{run();});
}
Audio::~Audio() {quit_=true;SetEvent(wake_);if(thread_.joinable()) thread_.join();CloseHandle(wake_);}
void Audio::post(Command command) {
    {std::lock_guard lock(mutex_);if(commands_.size()<128) commands_.push_back(command);}SetEvent(wake_);
}
void Audio::play(bool hit,std::uint64_t id,double speed) {post({hit?Kind::Hit:Kind::Swing,id,speed});}
void Audio::cancel(std::uint64_t id) {post({Kind::Cancel,id,0});}
void Audio::stopAll() {post({Kind::Stop,0,0});}
void Audio::recover() {recovery_=true;SetEvent(wake_);}
void STDMETHODCALLTYPE Audio::OnCriticalError(HRESULT) {recovery_=true;available_=false;SetEvent(wake_);}
bool Audio::initialize() {
    if(samples_[0].data.empty()) return false;
    if(FAILED(XAudio2Create(engine_.ReleaseAndGetAddressOf(),0,XAUDIO2_DEFAULT_PROCESSOR))) return false;
    engine_->RegisterForCallbacks(this);
    if(FAILED(engine_->CreateMasteringVoice(&master_,XAUDIO2_DEFAULT_CHANNELS,48000,0,nullptr,nullptr,AudioCategory_GameEffects))) {release();return false;}
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=96000;
    for(std::size_t i=0;i<voices_.size();++i) {
        voices_[i].hit=i<16;
        if(FAILED(engine_->CreateSourceVoice(&voices_[i].value,&format,0,1.1f))) {release();return false;}
    }
    running_=true;available_=true;latency_=0.020;return true;
}
void Audio::release() {
    available_=false;
    if(engine_) engine_->UnregisterForCallbacks(this);
    for(auto& voice:voices_) {if(voice.value) voice.value->DestroyVoice();voice.value=nullptr;voice.gesture=0;}
    if(master_) {master_->DestroyVoice();master_=nullptr;}
    engine_.Reset();running_=false;
}
void Audio::process(const Command& command) {
    if(command.kind==Kind::Stop || command.kind==Kind::Cancel) {
        for(auto& voice:voices_) if(voice.value && (command.kind==Kind::Stop || voice.gesture==command.gesture)) {voice.value->Stop();voice.value->FlushSourceBuffers();voice.gesture=0;}
        return;
    }
    if(!available_ || !engine_) return;
    if(!running_) {if(FAILED(engine_->StartEngine())) {recovery_=true;return;}running_=true;}
    Voice* selected=nullptr;
    for(auto& voice:voices_) if(voice.hit==(command.kind==Kind::Hit)) {XAUDIO2_VOICE_STATE state{};voice.value->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);if(!state.BuffersQueued) {selected=&voice;break;}}
    if(!selected) return;
    int sample=0;
    if(command.kind==Kind::Swing) {sample=lastSwing_==1?2:1;lastSwing_=sample;}
    auto& pcm=samples_[static_cast<std::size_t>(sample)];if(pcm.data.empty()) return;
    random_=1664525*random_+1013904223;
    const float pitch=0.97f+0.06f*static_cast<float>((random_>>8)&0xffff)/65535.0f;
    const float power=static_cast<float>(std::clamp(command.speed/2400.0,0.35,1.0));
    selected->value->SetFrequencyRatio(pitch);
    selected->value->SetVolume((command.kind==Kind::Hit?0.66f:0.38f)*std::sqrt(power));
    XAUDIO2_BUFFER buffer{};buffer.AudioBytes=static_cast<UINT32>(pcm.data.size());buffer.pAudioData=pcm.data.data();buffer.Flags=XAUDIO2_END_OF_STREAM;
    selected->gesture=command.gesture;
    if(SUCCEEDED(selected->value->SubmitSourceBuffer(&buffer))) selected->value->Start();
}
void Audio::run() {
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    double retry=0,lastPlay=clockSeconds(),lastMeasure=0;
    while(!quit_) {
        const double now=clockSeconds();
        if(recovery_.exchange(false)) {release();retry=0;std::lock_guard lock(mutex_);commands_.clear();}
        if(!engine_ && now>=retry && !samples_[0].data.empty()) {initialize();retry=now+2;}
        std::deque<Command> batch;
        {std::lock_guard lock(mutex_);batch.swap(commands_);}
        for(const auto& command:batch) {process(command);if(command.kind==Kind::Hit || command.kind==Kind::Swing) lastPlay=now;}
        if(engine_ && running_ && now-lastMeasure>0.5) {
            XAUDIO2_PERFORMANCE_DATA performance{};engine_->GetPerformanceData(&performance);
            const double estimate=static_cast<double>(performance.CurrentLatencyInSamples)/48000.0+0.010;
            latency_=std::clamp(estimate,0.010,0.050);lastMeasure=now;
        }
        if(engine_ && running_ && now-lastPlay>2) {engine_->StopEngine();running_=false;}
        WaitForSingleObject(wake_,100);
    }
    release();if(SUCCEEDED(com)) CoUninitialize();
}
}
