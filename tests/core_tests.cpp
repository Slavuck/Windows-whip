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
        const double x=i<=10?i*32.0:320.0;
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
    check(Whip::solverSegments==49,"one rigid handle and 48 inextensible tail bars");
    // Specify durations rather than tying settling to an old solver's step count.
    for(double t=0;t<2.5;t+=Whip::step) rope.advance(Whip::step,{std::sin(t*4)*100,std::cos(t*4)*30},0);
    for(double t=0;t<6;t+=Whip::step) rope.advance(Whip::step,{100,100},0);
    check(length(rope.points().front()-Vec2{100,100})<0.001,"Sadomaso fixes the handle");
    for(auto point:rope.nodes()) check(std::isfinite(point.x)&&std::isfinite(point.y),"finite physical bar positions");
    for(auto velocity:rope.velocities()) check(std::isfinite(velocity.x)&&std::isfinite(velocity.y),"finite physical bar velocities");
    check(rope.stretchError()<1e-5,"all physical bars retain their rest lengths");
    check(rope.constraintVelocityError()<1e-5,"settled velocities are tangent to the bar constraints");
    const auto first=rope.points()[5]-rope.points()[0];
    for(std::size_t i=1;i<5;++i) {const auto delta=rope.points()[i]-rope.points()[0];check(std::abs(first.x*delta.y-first.y*delta.x)<1e-5,"short handle remains straight and rigid");}
    check(Whip::totalLength==768 && std::abs(Whip::handleLength-67.584)<1e-6,"fixed lash length with an additional 12 percent shorter handle");
    check(rope.tip().y>rope.points()[0].y+560,"gravity pulls the settled lash downward");
    for(double t=0;t<1;t+=Whip::step) rope.advance(Whip::step,{100,100},std::min(1.0,t/0.5));
    check(length(rope.tip()-Vec2{100,100})<0.001,"Exact fixes the tip");
    CoilAnimation coil;const auto live=rope.points();coil.begin(live,{100,100},true,10);
    const auto start=coil.sample(live,10),middle=coil.sample(live,10.32),end=coil.sample(live,11);
    check(length(start[20]-live[20])<1e-6,"coiling starts at the visible pose without a jump");
    check(coil.active(10.3) && !coil.active(11) && coil.coiled(),"coiling has a bounded transition and a stable paused state");
    for(auto point:middle) check(std::isfinite(point.x)&&std::isfinite(point.y),"finite coiling geometry");
    double minX=end[0].x,maxX=minX,minY=end[0].y,maxY=minY;
    for(auto point:end) {minX=std::min(minX,point.x);maxX=std::max(maxX,point.x);minY=std::min(minY,point.y);maxY=std::max(maxY,point.y);}
    check(maxX-minX<150 && maxY-minY<150,"paused whip forms a compact circular coil");
    const auto parked=coil.sample(live,12);check(length(parked[40]-end[40])<1e-6,"rest pose is stable without input");
    const Vec2 mouse{220,180};for(int i=0;i<90;++i) coil.follow(mouse,1.0/144);
    const auto carried=coil.sample(live,12);
    check(length(carried[0]-mouse)<1e-6,"paused grip follows the cursor");
    check(length(carried[3]-(mouse+Vec2{41.6,0}))<1e-6,"second paused pin follows the cursor");
    check(length((carried[40]-end[40])-(mouse-Vec2{100,100}))>0.5,"free coil sags and moves independently of its pins");
    for(auto point:carried) check(std::isfinite(point.x)&&std::isfinite(point.y),"finite carried coil");
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
