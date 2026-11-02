#include "core.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>
#include <stdexcept>
using namespace whip;
struct Result {int swings{},stops{},hits{};double reach{},delay{},projection{},tipSpeed{},pointError{};};
Result stroke(double travel,double duration,double angle,Mode mode,double fps) {
    Whip whip;whip.reset({0,0});for(int i=0;i<480;++i) whip.advance(Whip::step,{0,0},0);
    GestureDetector gesture;gesture.reset(0,{0,0});ContactValidator contact;Result result;double attachment=0;
    const Vec2 direction{std::cos(angle),std::sin(angle)};
    for(int i=1;i<=static_cast<int>(fps*2);++i) {
        const double dt=1/fps,t=i*dt,u=std::min(t/duration,1.0),s=u*u*(3-2*u);const Vec2 cursor=direction*(travel*s);
        if(auto event=gesture.sample(t,cursor,false)) {if(event->kind==GestureEvent::Kind::Swing) {++result.swings;contact.cancel();}else {++result.stops;contact.arm(*event,mode);}}
        const double target=mode==Mode::Exact?(contact.pending()?1:std::clamp(gesture.speed()/GestureDetector::peakSpeed,0.0,1.0)):0;
        attachment+=(target-attachment)*(1-std::exp(-dt*(contact.pending()?90:14)));if(mode==Mode::Exact && contact.pending()) attachment=1;
        whip.advance(dt,mode==Mode::Exact && contact.pending()?contact.target():cursor,attachment);
        if(auto hit=contact.update(t,whip,cursor,false)) {++result.hits;const auto offset=whip.tip()-whip.points().front();result.reach=length(offset);result.delay=(hit->time-hit->stopTime)*1000;result.projection=dot(offset,direction);result.tipSpeed=hit->speed;result.pointError=length(hit->point-whip.tip());}
    }
    return result;
}
static int checks=0;
static void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
int main(int argc,char**) {
    try {
        if(argc>1) {
            std::cout<<"mode,travel,duration,angle,fps,swings,stops,hits,reach,delay,projection,speed\n";
            for(auto mode:{Mode::Sadomaso,Mode::Exact}) for(double angle:{0.0,-1.5707963267948966}) for(double fps:{60.0,90.0,120.0,144.0,240.0}) {
                const auto r=stroke(320,0.12,angle,mode,fps);
                std::cout<<(mode==Mode::Exact?"Exact":"Sadomaso")<<",320,0.12,"<<angle<<','<<fps<<','<<r.swings<<','<<r.stops<<','<<r.hits<<','<<r.reach<<','<<r.delay<<','<<r.projection<<','<<r.tipSpeed<<'\n';
            }
            return 0;
        }
        for(auto mode:{Mode::Sadomaso,Mode::Exact}) for(double angle:{0.0,-1.5707963267948966}) for(double fps:{60.0,90.0,120.0,144.0,240.0}) {
            const auto r=stroke(320,0.12,angle,mode,fps);
            require(r.swings==1 && r.stops==1 && r.hits==1,"a strong directed stroke produces exactly one physical contact");
            require(r.delay>=0 && r.delay<=80,"contact occurs within 80 ms of confirmed stop");
            require(r.reach>=Whip::totalLength*0.84 && r.reach<=Whip::totalLength*1.06,"contact comes from a naturally extended lash");
            require(r.projection>Whip::totalLength*0.35 && r.tipSpeed>=1800,"free tip physically flies ahead of handle");
            require(r.pointError<1e-6,"click point matches the visible physical tip in both modes");
        }
        for(auto mode:{Mode::Sadomaso,Mode::Exact}) {
            require(stroke(160,0.2,0,mode,144).hits==0,"weak stroke cannot click");
            require(stroke(256,0.12,0,mode,144).swings==0,"medium stroke below the raised sustained-speed threshold cannot arm");
            const auto fast=stroke(560,0.06,0,mode,144);
            require(fast.hits<=1,"a fast crossing stroke never repeats");
            if(fast.hits) require(fast.reach>=Whip::totalLength*0.84 && fast.projection>Whip::totalLength*0.35,"a crossing stroke clicks only after the tip physically unfolds ahead");
        }
        // Delayed/coalesced pointer delivery can create high instantaneous
        // speeds even for slow hands. Require the real stroke average too.
        for(double speed:{40.0,200.0,500.0,900.0,1200.0}) for(double batch:{0.016,0.032,0.064,0.096}) {
            GestureDetector gesture;gesture.reset(0,{0,0});int events=0;double time=0;
            for(int i=1;i<4000;++i) {time+=i%5==0?0.003:0.009;const double delivery=std::floor(time/batch)*batch;if(gesture.sample(time,{delivery*speed,std::sin(time)*0.1},false)) ++events;}
            require(events==0,"slow motion with bunched delivery never arms");
        }
        {
            GestureDetector gesture;gesture.reset(0,{0,0});int events=0;
            for(int i=1;i<300;++i) if(gesture.sample(i*0.007,{i%2?3.0:-3.0,0},false)) ++events;
            require(events==0,"jitter and reversals never arm");
            gesture.reset(0,{0,0});for(int i=1;i<300;++i) if(gesture.sample(i*0.007,{1600,0},false)) ++events;
            require(events==0,"one cursor teleport cannot become a stroke");
        }
        Whip resting;resting.reset({0,0});for(int i=0;i<500;++i) resting.advance(Whip::step,{0,0},0);
        {
            Whip trailing=resting;trailing.advance(Whip::step,{160,0},0);
            ContactValidator gate;gate.arm({GestureEvent::Kind::Stop,{160,0},1,4000,99,{1,0}},Mode::Sadomaso);
            require(!gate.update(1.001,trailing,{160,0},false),"a fast moving tip behind its handle cannot click");
        }
        const GestureEvent stop{GestureEvent::Kind::Stop,{0,0},1,3000,100,{1,0}};
        ContactValidator contact;contact.arm(stop,Mode::Sadomaso);
        require(!contact.update(1.01,resting,{0,0},false),"a qualified gesture cannot click a stationary/trailing tip");
        require(!contact.update(1.081,resting,{0,0},false) && !contact.pending(),"expired contact is discarded");
        contact.arm(stop,Mode::Sadomaso);require(!contact.pending(),"the same gesture cannot be rearmed after cancellation");
        auto next=stop;next.id=101;contact.arm(next,Mode::Sadomaso);contact.update(1.01,resting,{0,0},true);require(!contact.pending(),"held button cancels contact");
        next.id=102;contact.arm(next,Mode::Sadomaso);contact.update(1.01,resting,{9,0},false);require(!contact.pending(),"renewed cursor motion cancels contact");
        std::cout<<checks<<" gesture/contact regression checks passed. No desktop input injected.\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
