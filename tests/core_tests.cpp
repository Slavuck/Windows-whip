#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>

static int checks=0;
static void check(bool value,const char* message) {++checks;if(!value) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
int main() {
    using namespace whip;
    for(const auto desktop:{Rect{-1920,-1080,5760,3240},Rect{0,0,3840,2160}}) {
        for(int y=desktop.y;y<desktop.y+desktop.height;y+=79) for(int x=desktop.x;x<desktop.x+desktop.width;x+=83) {
            const auto point=normalizePoint({static_cast<double>(x),static_cast<double>(y)},desktop);
            const int decodedX=desktop.x+static_cast<int>(static_cast<long long>(point.x)*desktop.width/65536);
            const int decodedY=desktop.y+static_cast<int>(static_cast<long long>(point.y)*desktop.height/65536);
            check(x==decodedX && y==decodedY,"virtual desktop pixel center round trip");
        }
        const auto edge=normalizePoint({static_cast<double>(desktop.x+desktop.width-1),static_cast<double>(desktop.y+desktop.height-1)},desktop);
        check(edge.x<=65535 && edge.y<=65535,"last desktop pixel");
    }
    GestureDetector gesture;gesture.reset(0,{0,0});int swings=0,stops=0;
    for(int i=1;i<=100;++i) {
        const double x=i<=10?i*18.0:180.0;
        if(auto event=gesture.sample(i*0.008,{x,0},false)) {if(event->kind==GestureEvent::Kind::Swing) ++swings;else ++stops;}
    }
    check(swings==1 && stops==1,"one fast stroke produces one swing and stop");
    gesture.reset(0,{0,0});int unexpected=0;
    for(int i=1;i<=1000;++i) if(gesture.sample(i*0.008,{i*0.2,std::sin(i*0.3)*0.2},false)) ++unexpected;
    check(unexpected==0,"slow movement and jitter never arm");
    gesture.reset(0,{0,0});
    for(int i=1;i<=100;++i) if(gesture.sample(i*0.008,{i<10?i*18.0:180,0},true)) ++unexpected;
    check(unexpected==0,"dragging never arms");
    Whip rope;rope.reset({0,0});
    for(int i=0;i<1200;++i) rope.advance(Whip::step,{std::sin(i*0.01)*100,std::cos(i*0.01)*30},0);
    for(int i=0;i<1200;++i) rope.advance(Whip::step,{100,100},0);
    check(length(rope.points().front()-Vec2{100,100})<0.001,"Sadomaso fixes the handle");
    for(auto point:rope.points()) check(std::isfinite(point.x)&&std::isfinite(point.y),"finite XPBD state");
    double maximum=0;for(std::size_t i=0;i<Whip::segments;++i) maximum=std::max(maximum,length(rope.points()[i+1]-rope.points()[i]));
    check(maximum<Whip::segmentLength*1.2,"rope returns to its rest lengths");
    for(int i=0;i<700;++i) rope.advance(Whip::step,{100,100},std::min(1.0,i/400.0));
    check(length(rope.tip()-Vec2{100,100})<0.001,"Exact fixes the tip");
    const auto original=rope.points();const double forecast=rope.forecastPeak({100,100},1);
    check(forecast>=0.032 && forecast<=0.068,"forecast leaves room inside the 80 ms deadline");
    check(rope.points()[10].x==original[10].x,"forecast does not mutate live physics");
    CoilAnimation coil;const auto live=rope.points();coil.begin(live,{100,100},true,10);
    const auto start=coil.sample(live,10),middle=coil.sample(live,10.32),end=coil.sample(live,11);
    check(length(start[20]-live[20])<1e-6,"coiling starts at the visible pose without a jump");
    check(coil.active(10.3) && !coil.active(11) && coil.coiled(),"coiling has a bounded transition and a stable paused state");
    for(auto point:middle) check(std::isfinite(point.x)&&std::isfinite(point.y),"finite coiling geometry");
    double minX=end[0].x,maxX=minX,minY=end[0].y,maxY=minY;
    for(auto point:end) {minX=std::min(minX,point.x);maxX=std::max(maxX,point.x);minY=std::min(minY,point.y);maxY=std::max(maxY,point.y);}
    check(maxX-minX<150 && maxY-minY<150,"paused whip forms a compact circular coil");
    const auto parked=coil.sample(live,12);check(length(parked[40]-end[40])<1e-6,"paused coil is static");
    coil.begin(end,{100,100},false,12);
    check(length(coil.sample(live,12)[40]-end[40])<1e-6,"resume begins from the parked coil");
    check(length(coil.sample(live,13)[40]-live[40])<1e-6 && !coil.coiled(),"resume rejoins live physics");
    std::vector<std::uint8_t> wav(48,0);
    const auto put=[&](int offset,unsigned value,int bytes){for(int i=0;i<bytes;++i) wav[static_cast<std::size_t>(offset+i)]=static_cast<std::uint8_t>(value>>(8*i));};
    std::memcpy(wav.data(),"RIFF",4);put(4,40,4);std::memcpy(wav.data()+8,"WAVEfmt ",8);put(16,16,4);put(20,1,2);put(22,1,2);put(24,48000,4);put(28,96000,4);put(32,2,2);put(34,16,2);std::memcpy(wav.data()+36,"data",4);put(40,4,4);
    const auto parsed=parseWave(wav);check(parsed && parsed->data.size()==4 && parsed->rate==48000,"PCM WAV parser");
    auto broken=wav;broken[40]=255;check(!parseWave(broken),"truncated data rejected");
    broken=wav;broken[20]=3;check(!parseWave(broken),"unsupported format rejected");
    std::mt19937 random(34);
    for(int i=0;i<4000;++i) {auto fuzz=wav;fuzz[static_cast<std::size_t>(random()%fuzz.size())]=static_cast<std::uint8_t>(random());const auto result=parseWave(fuzz);if(result) check(result->bits==16 && !result->data.empty(),"WAV mutation invariant");}
    std::cout<<checks<<" checks passed\n";
}
