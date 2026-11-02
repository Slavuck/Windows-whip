#include "core.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>
#include <stdexcept>
using namespace whip;
namespace whip {
// Contract fixtures isolate the contact gate from the integrator. Full synthetic
// strokes below exercise the same gate using the unmodified physical tip.
struct PhysicsTestAccess {
    static Whip contactState(Vec2 tip,Vec2 relativeVelocity,Vec2 rootVelocity={}) {
        Whip whip;whip.reset({0,0});
        for(std::size_t i=0;i<=Whip::segments;++i) whip.position_[i]=tip*(static_cast<double>(i)/Whip::segments);
        whip.tipVelocity_=relativeVelocity+rootVelocity;whip.handVelocity_=rootVelocity;
        return whip;
    }
};
}
struct Result {
    int swings{},stops{},hits{};
    double reach{},delay{},projection{},tipSpeed{},pointError{};
    bool eligible=true;
};
Result stroke(double travel,double duration,double angle,Mode mode,double fps) {
    Whip whip;whip.reset({0,0});for(double t=0;t<1;t+=Whip::step) whip.advance(Whip::step,{0,0},0);
    GestureDetector gesture;gesture.reset(0,{0,0});ContactValidator contact;Result result;double attachment=0;
    const Vec2 direction{std::cos(angle),std::sin(angle)};
    for(int i=1;i<=static_cast<int>(fps*2);++i) {
        const double dt=1/fps,t=i*dt,u=std::min(t/duration,1.0),s=u*u*(3-2*u);const Vec2 cursor=direction*(travel*s);
        if(auto event=gesture.sample(t,cursor,false)) {if(event->kind==GestureEvent::Kind::Swing) {++result.swings;contact.cancel();}else {++result.stops;contact.arm(*event,mode);}}
        const double target=mode==Mode::Exact?(contact.pending()?1:std::clamp(gesture.speed()/GestureDetector::peakSpeed,0.0,1.0)):0;
        attachment+=(target-attachment)*(1-std::exp(-dt*(contact.pending()?90:14)));if(mode==Mode::Exact && contact.pending()) attachment=1;
        whip.advance(dt,mode==Mode::Exact && contact.pending()?contact.target():cursor,attachment);
        if(auto hit=contact.update(t,whip,cursor,false)) {
            ++result.hits;const auto offset=whip.tip()-whip.points().front(),relative=whip.tipVelocity()-whip.handleVelocity();
            result.reach=length(offset);result.delay=hit->time-hit->stopTime;result.projection=dot(offset,direction);result.tipSpeed=hit->speed;
            result.pointError=std::max(result.pointError,length(hit->point-(mode==Mode::Exact?cursor:whip.tip())));
            result.eligible=result.eligible && result.delay>=0 && result.delay<=ContactValidator::contactWindow
                && result.reach>=Whip::totalLength*ContactValidator::minReachRatio && result.reach<=Whip::totalLength*ContactValidator::maxReachRatio
                && result.projection>=Whip::totalLength*ContactValidator::minProjectionRatio
                && dot(relative,direction)>=ContactValidator::minForwardSpeed && dot(relative,offset)>0;
        }
    }
    return result;
}
static int checks=0;
static void require(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
int main(int argc,char**) {
    try {
        if(argc>1) {
            std::cout<<"mode,travel,duration,angle,fps,swings,stops,hits,reach,delay_ms,projection,speed\n";
            for(auto mode:{Mode::Sadomaso,Mode::Exact}) for(double travel:{320.0,480.0,640.0}) for(double angle:{0.0,-0.7853981633974483}) for(double fps:{60.0,90.0,120.0,144.0,240.0}) {
                const auto r=stroke(travel,0.12,angle,mode,fps);
                std::cout<<(mode==Mode::Exact?"Exact":"Sadomaso")<<','<<travel<<",0.12,"<<angle<<','<<fps<<','<<r.swings<<','<<r.stops<<','<<r.hits<<','<<r.reach<<','<<r.delay*1000<<','<<r.projection<<','<<r.tipSpeed<<'\n';
            }
            return 0;
        }
        // Starting from a hanging lash, these finite-acceleration strokes have
        // enough travel to roll the loop out. Recognition alone is not contact.
        for(auto mode:{Mode::Sadomaso,Mode::Exact}) for(double travel:{480.0,640.0}) for(double angle:{0.0,-0.7853981633974483}) for(double fps:{60.0,90.0,120.0,144.0,240.0}) {
            const auto r=stroke(travel,0.12,angle,mode,fps);
            require(r.swings==1 && r.stops==1 && r.hits==1,"a strong directed stroke produces exactly one physical contact");
            require(r.eligible,"contact requires natural extension, forward/outward tip velocity and the current stop window");
            require(r.pointError<1e-6,"Sadomaso clicks the visible tip and Exact clicks the cursor's stop point");
        }
        for(auto mode:{Mode::Sadomaso,Mode::Exact}) for(double angle:{0.0,-1.5707963267948966}) for(double fps:{60.0,90.0,120.0,144.0,240.0}) {
            const auto r=stroke(320,0.12,angle,mode,fps);
            require(r.swings==1 && r.stops==1,"a recognised stroke still produces exactly one swing and stop");
            require(r.hits<=1 && r.eligible && r.pointError<1e-6,"a shorter stroke can click only if its physical tip reaches contact");
        }
        for(auto mode:{Mode::Sadomaso,Mode::Exact}) {
            require(stroke(160,0.2,0,mode,144).hits==0,"weak stroke cannot click");
            require(stroke(256,0.12,0,mode,144).swings==0,"medium stroke below the raised sustained-speed threshold cannot arm");
            const auto fast=stroke(560,0.06,0,mode,144);
            require(fast.hits<=1,"a fast crossing stroke never repeats");
            require(fast.eligible && fast.pointError<1e-6,"a crossing stroke clicks only after the tip physically unfolds ahead");
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
        Whip resting;resting.reset({0,0});for(double t=0;t<1;t+=Whip::step) resting.advance(Whip::step,{0,0},0);
        {
            Whip trailing=resting;trailing.advance(Whip::step,{160,0},0);
            ContactValidator gate;gate.arm({GestureEvent::Kind::Stop,{160,0},1,4000,99,{1,0}},Mode::Sadomaso);
            require(!gate.update(1.001,trailing,{160,0},false),"a fast moving tip behind its handle cannot click");
        }
        const GestureEvent stop{GestureEvent::Kind::Stop,{0,0},1,3000,100,{1,0}};
        ContactValidator contact;contact.arm(stop,Mode::Sadomaso);
        require(!contact.update(1.01,resting,{0,0},false),"a qualified gesture cannot click a stationary/trailing tip");
        require(!contact.update(stop.time+ContactValidator::contactWindow+0.001,resting,{0,0},false) && !contact.pending(),"expired contact is discarded");
        contact.arm(stop,Mode::Sadomaso);require(!contact.pending(),"the same gesture cannot be rearmed after cancellation");
        auto next=stop;next.id=101;contact.arm(next,Mode::Sadomaso);contact.update(1.01,resting,{0,0},true);require(!contact.pending(),"held button cancels contact");
        next.id=102;contact.arm(next,Mode::Sadomaso);contact.update(1.01,resting,{9,0},false);require(!contact.pending(),"renewed cursor motion cancels contact");
        {
            const auto eligible=PhysicsTestAccess::contactState({640,0},{3000,0});
            for(auto mode:{Mode::Sadomaso,Mode::Exact}) {
                ContactValidator gate;auto event=stop;event.point={23,37};gate.arm(event,mode);
                const auto hit=gate.update(event.time+ContactValidator::contactWindow-0.001,eligible,event.point,false);
                require(hit.has_value(),"an eligible physical contact is accepted near the end of the full contact window");
                require(length(hit->point-(mode==Mode::Exact?event.point:eligible.tip()))<1e-9,"mode selects the stop point or the actual visible tip");
                require(!gate.update(event.time+ContactValidator::contactWindow-0.0005,eligible,event.point,false),"an accepted gesture cannot click twice");
                gate.arm(event,mode);require(!gate.pending(),"an accepted gesture cannot be rearmed");
            }
            for(const auto& invalid:{
                PhysicsTestAccess::contactState({600,0},{3000,0}), // folded/short
                PhysicsTestAccess::contactState({820,0},{3000,0}), // overextended
                PhysicsTestAccess::contactState({200,700},{3000,0}), // behind the required forward projection
                PhysicsTestAccess::contactState({640,0},{2199,6000}), // high total speed, insufficient forward speed
                PhysicsTestAccess::contactState({300,650},{2300,-3000}) // forward yet moving radially inward
            }) {
                ContactValidator gate;gate.arm(stop,Mode::Sadomaso);
                require(!gate.update(1.01,invalid,{0,0},false),"each extension, projection and velocity gate independently rejects a false contact");
                require(gate.pending(),"a rejected pose leaves time for a later genuine contact");
            }
            ContactValidator gate;gate.arm(stop,Mode::Sadomaso);
            require(!gate.update(stop.time+ContactValidator::contactWindow+0.001,eligible,{0,0},false) && !gate.pending(),"even an eligible pose cannot click after expiry");
            const auto movingRoot=PhysicsTestAccess::contactState({640,0},{1000,0},{3000,0});
            gate.arm(next,Mode::Sadomaso);
            require(!gate.update(1.01,movingRoot,{0,0},false),"contact speed is relative to the handle, not world-space tip speed");
        }
        std::cout<<checks<<" gesture/contact regression checks passed. No desktop input injected.\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
