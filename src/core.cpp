#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace whip {
double length(Vec2 v) { return std::hypot(v.x,v.y); }
double dot(Vec2 a,Vec2 b) { return a.x*b.x+a.y*b.y; }
Vec2 mix(Vec2 a,Vec2 b,double t) { return a*(1-t)+b*t; }
bool Rect::contains(Vec2 p) const { return p.x>=x && p.y>=y && p.x<x+width && p.y<y+height; }
NormalizedPoint normalizePoint(Vec2 p,Rect r) {
    if(r.width<=0 || r.height<=0) return {};
    const auto axis=[](double v,int start,int size){ return static_cast<int>(std::clamp(std::floor((v-start+0.5)*65536.0/size),0.0,65535.0)); };
    return {axis(p.x,r.x,r.width),axis(p.y,r.y,r.height)};
}
void GestureDetector::reset(double t,Vec2 p) {
    previous_=strokeStart_=p;strokeDelta_={};previousTime_=t;speed_=travel_=peak_=fastDuration_=acceleration_=previousSpeed_=0;
    lastFast_=t;quietSince_=-1;cooldownUntil_=t;initialized_=true;armed_=false;
    history_.clear();history_.push_back({t,p});
}
std::optional<GestureEvent> GestureDetector::sample(double t,Vec2 p,bool held) {
    if(!initialized_ || t<=previousTime_ || t-previousTime_>0.040 || !std::isfinite(p.x) || !std::isfinite(p.y)) {reset(t,p);return {};}
    const double dt=t-previousTime_;const auto delta=p-previous_;const double distance=length(delta),raw=distance/dt;
    previous_=p;previousTime_=t;speed_+=(raw-speed_)*(1-std::exp(-dt/0.010));
    history_.push_back({t,p});const double cutoff=t-0.100;
    while(history_.size()>1 && history_[1].time<=cutoff) history_.pop_front();
    auto old=history_.front().point;
    if(history_.size()>1 && history_.front().time<cutoff) old=mix(old,history_[1].point,(cutoff-history_.front().time)/(history_[1].time-history_.front().time));
    const double sustained=length(p-old)/0.100;
    if(held) {reset(t,p);cooldownUntil_=t+0.20;return {};}
    if(t<cooldownUntil_) {previousSpeed_=raw;return {};}
    if(raw>fastSpeed) {
        if(travel_==0) {strokeStart_=p-delta;strokeStarted_=t-dt;strokeDelta_={};}
        if(length(strokeDelta_)>1 && dot(delta,strokeDelta_)<0) {armed_=false;travel_=fastDuration_=peak_=acceleration_=0;strokeDelta_={};strokeStart_=p-delta;strokeStarted_=t-dt;}
        travel_+=distance;strokeDelta_+=delta;fastDuration_+=dt;peak_=std::max(peak_,raw);lastFast_=t;quietSince_=-1;
        acceleration_=std::max(acceleration_,(raw-previousSpeed_)/dt);
    } else if(!armed_ && t-lastFast_>0.022) {travel_=fastDuration_=peak_=acceleration_=0;strokeDelta_={};}
    if(raw<100) {if(quietSince_<0) quietSince_=t-dt;}else quietSince_=-1;
    const double coherence=travel_>0?length(strokeDelta_)/travel_:0;
    previousSpeed_=raw;
    const double average=length(p-strokeStart_)/std::max(t-strokeStarted_,0.001);
    if(!armed_ && t-strokeStarted_>0.18) {travel_=fastDuration_=peak_=acceleration_=0;strokeDelta_={};}
    if(!armed_ && fastDuration_>=0.028 && travel_>=100 && peak_>=peakSpeed && acceleration_>=20000 && coherence>=0.90 && average>=2250 && sustained>=sustainedSpeed && t-lastFast_<0.012) {
        armed_=true;return GestureEvent{GestureEvent::Kind::Swing,p,t,peak_,++serial_,strokeDelta_/length(strokeDelta_)};
    }
    if(armed_ && raw<100 && quietSince_>=0 && t-quietSince_>=0.018 && t-lastFast_<=0.090 && quietSince_-lastFast_<=0.045) {
        const auto direction=strokeDelta_/std::max(length(strokeDelta_),1.0);const double power=peak_;
        armed_=false;travel_=fastDuration_=peak_=acceleration_=0;strokeDelta_={};cooldownUntil_=t+0.28;
        return GestureEvent{GestureEvent::Kind::Stop,p,t,power,serial_,direction};
    }
    if(armed_ && t-lastFast_>0.090) {armed_=false;travel_=fastDuration_=peak_=acceleration_=0;strokeDelta_={};}
    return {};
}
namespace {
constexpr double physicsPi=3.14159265358979323846;
constexpr std::size_t N=Whip::solverSegments;   // bars; nodes are 0..N
constexpr std::size_t M=N-1;                    // thong bars (bar 0 is the rigid handle)

// --- Hand and wrist ---------------------------------------------------------------------
// The hand is a critically damped follower of the mouse. It removes pixel quantisation and gives
// the hand a finite acceleration; the drawn handle is still translated to sit on the cursor.
constexpr double handFrequency=220.0;           // rad/s
// A passive wrist spring rests downward. Only the moving hand, gravity and stored
// elastic energy can drive the lash; cursor speed never rotates a powered target.
constexpr double wristFrequency=8.0,wristDamping=1.0;
constexpr double handleMass=260.0;              // equivalent point mass of the handle at its tip

// --- Whip body ---------------------------------------------------------------------------
// Linear density relative to the belly: a thick plaited belly tapering to a thin fall and cracker.
constexpr double tipDensity=0.04,taperPower=2.4;
constexpr double tangentialDrag=0.06;           // along-the-rope drag relative to across-the-rope drag
constexpr double bellyDampingRatio=0.45,tipDampingRatio=1.10;
constexpr double crackSpeed=9000;               // px/s, drag doubles here: the crack of a real whip radiates a large part of its energy
constexpr double speedLimit=90000;              // px/s, numerical safety net only

double taper(double s) {return tipDensity+(1-tipDensity)*std::pow(1-s,taperPower);}
double cross(Vec2 a,Vec2 b) {return a.x*b.y-a.y*b.x;}
Vec2 perp(Vec2 v) {return {-v.y,v.x};}
Vec2 unit(Vec2 v) {const double l=length(v);return l>1e-9?v/l:Vec2{0,1};}

struct Layout {
    std::array<double,N+1> mass{},inverse{},drag{},bend{};
    Layout() {
        for(std::size_t j=1;j<=M;++j) {
            const double m=taper((static_cast<double>(j)-0.5)/M)*Whip::tailLinkLength;
            mass[j]+=0.5*m;mass[j+1]+=0.5*m;
        }
        mass[1]+=handleMass;
        for(std::size_t i=1;i<=N;++i) {
            const double density=taper(static_cast<double>(i-1)/M);
            inverse[i]=1/mass[i];
            drag[i]=i==1?0.3:1/std::sqrt(density); // thinner rope: more drag per unit mass
            const double u=std::clamp((i-1)*Whip::tailLinkLength/Whip::handleLength,0.0,1.0);
            const double neck=0.25+0.75*u*u*(3-2*u);
            bend[i]=density*density*neck; // a soft neck, then EI proportional to radius^4
        }
    }
};
const Layout& layout() {static const Layout value;return value;}

// Solves the symmetric tridiagonal system of a bar chain (A P = rhs) with the Thomas algorithm.
// A(i,i)=w[i]+w[i+1], A(i,i+1)=-(n[i].n[i+1]) w[i+1]. Node 0 is kinematic (w=0).
void solveChain(const std::array<Vec2,N>& dir,const std::array<double,N>& rhs,const std::array<double,N+1>& w,std::array<double,N>& result) {
    std::array<double,N> cp{},dp{};
    double previousOff=0;
    for(std::size_t i=0;i<N;++i) {
        const double diag=w[i]+w[i+1]+1e-12;
        const double m=diag-(i?previousOff*cp[i-1]:0);
        const double off=i+1<N?-dot(dir[i],dir[i+1])*w[i+1]:0;
        cp[i]=off/m;dp[i]=(rhs[i]-(i?previousOff*dp[i-1]:0))/m;previousOff=off;
    }
    result[N-1]=dp[N-1];
    for(std::size_t i=N-1;i-->0;) result[i]=dp[i]-cp[i]*result[i+1];
}
double restLength(std::size_t bar) {return bar==0?Whip::handleLength:Whip::tailLinkLength;}
}
WhipTuning tuning(WhipStyle style) {
    //                          gravity damping  1/EI        width outline drag
    if(style==WhipStyle::Heavy) return {4200,0.9,1/9.0e8,8.0,2.08,1.0e-4};
    if(style==WhipStyle::Spring) return {3200,1.6,1/6.0e8,5.6,2.72,1.8e-4};
    return {2800,1.4,1/3.5e8,4.4,1.76,1.5e-4};
}
void Whip::reset(Vec2 cursor) {
    for(std::size_t i=0;i<=solverSegments;++i) {nodes_[i]=cursor+Vec2{0,i?handleLength+(i-1)*tailLinkLength:0};velocity_[i]={};}
    hand_=control_=cursor;handVelocity_=tipVelocity_={};controlAttachment_=0;accumulator_=tipSpeed_=0;
    updateCurve(cursor,0);
}
void Whip::advance(double elapsed,Vec2 cursor,double attachment) {
    elapsed=std::clamp(elapsed,0.0,0.035);if(elapsed<=0) return;
    const auto from=control_;const double before=accumulator_;accumulator_+=elapsed;
    double time=step-before;
    while(accumulator_>=step) {
        // The mouse moves linearly across the frame; every sub-step sees its own point on that line.
        integrate(mix(from,cursor,std::clamp(time/elapsed,0.0,1.0)));
        accumulator_-=step;time+=step;
    }
    control_=cursor;controlAttachment_=std::clamp(attachment,0.0,1.0);
    updateCurve(cursor,controlAttachment_);
}
void Whip::solveVelocities() {
    const auto& inverse=layout().inverse;
    std::array<Vec2,N> dir{};std::array<double,N> rhs{},impulse{};
    for(std::size_t i=0;i<N;++i) {
        dir[i]=unit(nodes_[i+1]-nodes_[i]);rhs[i]=-dot(dir[i],velocity_[i+1]-velocity_[i]);
    }
    solveChain(dir,rhs,inverse,impulse);
    for(std::size_t i=0;i<N;++i) {
        velocity_[i+1]+=dir[i]*(impulse[i]*inverse[i+1]);velocity_[i]-=dir[i]*(impulse[i]*inverse[i]);
    }
}
void Whip::solvePositions(int iterations) {
    // Newton iterations on the bar lengths. A fast tip can travel more than one bar per sub-step, so
    // after the usual few passes we keep iterating (bounded) until the lengths are accurate.
    const auto& inverse=layout().inverse;
    for(int pass=0;pass<iterations+12;++pass) {
        std::array<Vec2,N> dir{};std::array<double,N> rhs{},lambda{};double worst=0;
        for(std::size_t i=0;i<N;++i) {
            const Vec2 d=nodes_[i+1]-nodes_[i];dir[i]=unit(d);rhs[i]=-(length(d)-restLength(i));
            worst=std::max(worst,std::abs(rhs[i])/restLength(i));
        }
        if(pass>=iterations && worst<1e-6) break;
        solveChain(dir,rhs,inverse,lambda);
        for(std::size_t i=0;i<N;++i) {
            const Vec2 up=dir[i]*(lambda[i]*inverse[i+1]),down=dir[i]*(lambda[i]*inverse[i]);
            nodes_[i+1]+=up;nodes_[i]-=down;velocity_[i+1]+=up/step;velocity_[i]-=down/step;
        }
    }
}
void Whip::integrate(Vec2 cursor) {
    const auto& lay=layout();const auto cfg=tuning(style_);constexpr double dt=step;
    // 1. The hand follows the mouse and carries node 0.
    // Exact critically damped update for this substep's held target. It remains
    // stable without introducing derivative noise from the cursor grid.
    const Vec2 offset=hand_-cursor,decayTerm=handVelocity_+offset*handFrequency;
    const double decay=std::exp(-handFrequency*dt);
    hand_=cursor+(offset+decayTerm*dt)*decay;
    handVelocity_=(handVelocity_-decayTerm*(handFrequency*dt))*decay;
    nodes_[0]=hand_;velocity_[0]=handVelocity_;
    // 2. A fixed-rest-angle wrist can yield to the rope and dissipate motion.
    // Its equilibrium never chases velocity or recentres on a timer.
    for(std::size_t i=1;i<=N;++i) velocity_[i].y+=cfg.gravity*dt;
    {
        const Vec2 r=nodes_[1]-hand_;const double radius2=std::max(dot(r,r),1e-9);
        const double error=std::remainder(physicsPi/2-std::atan2(r.y,r.x),2*physicsPi),rate=cross(r,velocity_[1]-handVelocity_)/radius2;
        const double w=lay.inverse[1]/radius2,k=wristFrequency*wristFrequency/w,c=2*wristDamping*wristFrequency/w;
        const double impulse=dt*(k*error-(c+dt*k)*rate)/(1+dt*(c+dt*k)*w);
        velocity_[1]+=perp(r)*(impulse*lay.inverse[1]/radius2);
    }
    // 3. Bending: a torsional spring with internal friction at every joint of the lash.
    const double stiffness=1/cfg.bendCompliance;
    for(std::size_t i=1;i<N;++i) {
        const Vec2 d1=nodes_[i]-nodes_[i-1],d2=nodes_[i+1]-nodes_[i];
        const double l1=std::max(length(d1),1e-6),l2=std::max(length(d2),1e-6);
        const double angle=std::atan2(cross(d1,d2),dot(d1,d2));
        const Vec2 ga=perp(d1)/(l1*l1),gc=perp(d2)/(l2*l2),gb=(ga+gc)*-1.0;
        const double rate=dot(ga,velocity_[i-1])+dot(gb,velocity_[i])+dot(gc,velocity_[i+1]);
        const double k=stiffness*lay.bend[i]/(0.5*(l1+l2));
        const double w=lay.inverse[i-1]*dot(ga,ga)+lay.inverse[i]*dot(gb,gb)+lay.inverse[i+1]*dot(gc,gc);
        const double s=static_cast<double>(i-1)/M,zeta=bellyDampingRatio+(tipDampingRatio-bellyDampingRatio)*s*s;
        const double c=2*zeta*std::sqrt(k/std::max(w,1e-12));
        // Backward-Euler angular spring/dashpot with effective rotational
        // inertia 1/w; damping stays controlled even at the light tip.
        const double impulse=-dt*(k*angle+(c+dt*k)*rate)/(1+dt*(c+dt*k)*w);
        velocity_[i-1]+=ga*(impulse*lay.inverse[i-1]);velocity_[i]+=gb*(impulse*lay.inverse[i]);velocity_[i+1]+=gc*(impulse*lay.inverse[i+1]);
    }
    // 4. Air: quadratic drag, much stronger across the rope than along it (implicit, unconditionally stable).
    for(std::size_t i=1;i<=N;++i) {
        const Vec2 tangent=unit(nodes_[std::min(i+1,N)]-nodes_[i-1]);
        const Vec2 along=tangent*dot(velocity_[i],tangent),across=velocity_[i]-along;
        const double q=cfg.drag*lay.drag[i];
        const double across2=length(across)/crackSpeed;
        const double fn=1/(1+(cfg.damping+q*length(across)*(1+across2*across2))*dt),ft=1/(1+(cfg.damping+tangentialDrag*q*length(along))*dt);
        velocity_[i]=across*fn+along*ft;
    }
    // 5. Constraints (SHAKE/RATTLE): exact inextensibility through direct tridiagonal solves.
    solveVelocities();
    for(std::size_t i=1;i<=N;++i) nodes_[i]+=velocity_[i]*dt;
    solvePositions(3);
    for(std::size_t i=1;i<=N;++i) {
        const double v=length(velocity_[i]);if(v>speedLimit) velocity_[i]=velocity_[i]*(speedLimit/v);
    }
    solveVelocities(); // RATTLE: restore tangent velocities at the corrected positions.
    if(!std::isfinite(nodes_[N].x+nodes_[N].y+velocity_[N].x+velocity_[N].y+nodes_[1].x+nodes_[1].y)) {reset(cursor);return;}
    tipVelocity_=velocity_[N];tipSpeed_=length(tipVelocity_);
}
void Whip::updateCurve(Vec2 cursor,double attachment) {
    // Tangents: the handle direction continues through its end node; elsewhere central differences.
    std::array<Vec2,solverSegments+1> tangent{};
    tangent[0]=tangent[1]=unit(nodes_[1]-nodes_[0]);
    for(std::size_t i=2;i<solverSegments;++i) tangent[i]=unit(nodes_[i+1]-nodes_[i-1]);
    tangent[solverSegments]=unit(nodes_[solverSegments]-nodes_[solverSegments-1]);
    for(std::size_t i=0;i<=segments;++i) {
        const double arc=static_cast<double>(i)*totalLength/segments;
        if(arc<=handleLength) {position_[i]=mix(nodes_[0],nodes_[1],arc/handleLength);continue;}
        const double u=(arc-handleLength)/tailLinkLength;
        const auto k=std::min(static_cast<std::size_t>(u),solverSegments-2)+1;const double t=u-static_cast<double>(k-1);
        const Vec2 a=nodes_[k],b=nodes_[k+1];const double chord=length(b-a),t2=t*t,t3=t2*t;
        position_[i]=a*(2*t3-3*t2+1)+tangent[k]*(chord*(t3-2*t2+t))+b*(-2*t3+3*t2)+tangent[k+1]*(chord*(t3-t2));
    }
    // The curve is built around the (smoothed) hand. Exact changes the held point by a rigid translation of
    // the curve so that the held point sits on the cursor; rest lengths and relative velocities are
    // identical in both modes. At low speed the small difference between mouse and hand (pixel
    // quantisation) is faded out towards the tip, so the lash does not shimmer with the cursor grid.
    const double coordinate=attachment*segments;const auto a=std::min(static_cast<std::size_t>(coordinate),segments-1);
    const Vec2 shift=hand_-mix(position_[a],position_[a+1],coordinate-a),lag=cursor-hand_;
    const double rigid=std::clamp(attachment/0.25,0.0,1.0),blend=rigid*rigid*(3-2*rigid);
    for(std::size_t i=0;i<=segments;++i) position_[i]+=shift+lag*(blend+(1-blend)*(1-static_cast<double>(i)/segments));
}
double Whip::motion() const {
    double speed=0;for(const auto& v:velocity_) speed+=length(v);
    return speed/(solverSegments+1);
}
double Whip::stretchError() const {
    double worst=0;
    for(std::size_t i=0;i<solverSegments;++i) worst=std::max(worst,std::abs(length(nodes_[i+1]-nodes_[i])-restLength(i))/restLength(i));
    return worst;
}
const std::array<double,Whip::solverSegments+1>& Whip::particleMasses() {return layout().mass;}
double Whip::constraintVelocityError() const {
    double worst=0;for(std::size_t i=0;i<N;++i) worst=std::max(worst,std::abs(dot(unit(nodes_[i+1]-nodes_[i]),velocity_[i+1]-velocity_[i])));
    return worst;
}
void ContactValidator::arm(const GestureEvent& stop,Mode mode) {
    cancel();
    if(stop.kind!=GestureEvent::Kind::Stop || stop.id<=consumed_ || stop.speed<GestureDetector::peakSpeed || !std::isfinite(stop.speed) || std::abs(length(stop.direction)-1)>0.01 || !std::isfinite(stop.time) || !std::isfinite(stop.point.x) || !std::isfinite(stop.point.y)) return;
    stop_=stop;mode_=mode;
}
std::optional<ImpactEvent> ContactValidator::update(double time,const Whip& whip,Vec2 cursor,bool held) {
    if(!stop_) return {};
    if(held || time<stop_->time || time-stop_->time>contactWindow || length(cursor-stop_->point)>8) {cancel();return {};}
    const auto offset=whip.tip()-whip.points().front(),velocity=whip.tipVelocity()-whip.handleVelocity();
    const double reach=length(offset),forward=dot(velocity,stop_->direction);
    // This is a physical contact gate, not a target radius or a tip-position
    // correction. A folded, trailing, stationary or inward-moving tip cannot click.
    if(reach<Whip::totalLength*minReachRatio || reach>Whip::totalLength*maxReachRatio || dot(offset,stop_->direction)<Whip::totalLength*minProjectionRatio || forward<minForwardSpeed || dot(velocity,offset)<=0) return {};
    const auto stop=*stop_;cancel();
    return ImpactEvent{stop.id,time,stop.time,length(velocity),mode_==Mode::Exact?stop.point:whip.tip()};
}
std::optional<PcmWave> parseWave(std::span<const std::uint8_t> bytes) {
    if(bytes.size()<12 || std::memcmp(bytes.data(),"RIFF",4)!=0 || std::memcmp(bytes.data()+8,"WAVE",4)!=0) return {};
    const auto u16=[&](std::size_t p){return static_cast<std::uint16_t>(bytes[p]|(bytes[p+1]<<8));};
    const auto u32=[&](std::size_t p){return static_cast<std::uint32_t>(bytes[p])|(static_cast<std::uint32_t>(bytes[p+1])<<8)|(static_cast<std::uint32_t>(bytes[p+2])<<16)|(static_cast<std::uint32_t>(bytes[p+3])<<24);};
    const std::size_t end=static_cast<std::size_t>(u32(4))+8;
    if(end>bytes.size() || end<12) return {};
    PcmWave wave;bool format=false;
    for(std::size_t p=12;p+8<=end;) {
        const std::size_t size=u32(p+4);const auto body=p+8;if(size>end-body) return {};
        if(std::memcmp(bytes.data()+p,"fmt ",4)==0) {
            if(size<16 || u16(body)!=1) return {};
            wave.channels=u16(body+2);wave.rate=u32(body+4);wave.bits=u16(body+14);format=true;
            if(wave.channels<1 || wave.channels>2 || wave.bits!=16 || wave.rate<8000 || wave.rate>192000 || u16(body+12)!=wave.channels*2 || u32(body+8)!=wave.rate*wave.channels*2) return {};
        } else if(std::memcmp(bytes.data()+p,"data",4)==0) wave.data.assign(bytes.begin()+static_cast<std::ptrdiff_t>(body),bytes.begin()+static_cast<std::ptrdiff_t>(body+size));
        p=body+size+(size&1);
    }
    if(!format || wave.data.empty() || wave.data.size()%(wave.channels*2)!=0) return {};
    return wave;
}
void CoilAnimation::begin(const Points& visible,Vec2 handle,bool coil,double time) {
    source_=visible;start_=time;duration_=coil?0.64:0.46;initialized_=true;coiling_=coil;
    origin_=cursor_=anchor_=handle;anchorVelocity_={};motion_=accumulator_=0;angle_=spin_=0;
    constexpr double pi=3.14159265358979323846;
    const Vec2 center=handle+Vec2{41.6,54.4};
    for(std::size_t i=0;i<=Whip::segments;++i) {
        if(i<=3) coil_[i]=handle+Vec2{41.6*i/3,0};
        else {
            const double s=static_cast<double>(i-3)/(Whip::segments-3),angle=-pi/2+s*pi*5.3,radius=54.4-23.2*s;
            coil_[i]=center+Vec2{std::cos(angle)*radius,std::sin(angle)*radius};
        }
    }
}
void CoilAnimation::follow(Vec2 cursor,double elapsed) {
    if(!initialized_ || !coiling_) return;
    // The ring hangs from the end of the handle like a rigid pendulum. The pivot is the hand, so the
    // ring feels its acceleration as an extra sideways "gravity".
    constexpr double dt=1.0/240,frequency=110,reach=84,gravity=3000,damping=1.6;
    cursor_=cursor;accumulator_+=std::clamp(elapsed,0.0,0.035);
    while(accumulator_>=dt) {
        accumulator_-=dt;
        const Vec2 pull=(cursor-anchor_)*(frequency*frequency)-anchorVelocity_*(2*frequency);
        anchorVelocity_+=pull*dt;anchor_+=anchorVelocity_*dt;
        const double sine=std::sin(angle_),cosine=std::cos(angle_);
        spin_+=(-(gravity*sine+pull.x*cosine-pull.y*sine)/reach-damping*spin_)*dt;angle_+=spin_*dt;
        angle_=std::clamp(angle_,-2.8,2.8);
        if(std::abs(angle_)<0.02 && std::abs(spin_)<0.06) angle_=spin_=0; // settles instead of ringing forever
    }
    motion_=std::abs(spin_)*reach+length(anchorVelocity_);
}
CoilAnimation::Points CoilAnimation::sample(const Points& live,double time) const {
    if(!initialized_) return live;
    const double t=std::clamp((time-start_)/duration_,0.0,1.0),ease=t*t*t*(10+t*(-15+6*t));
    Points target=coiling_?coil_:live;Points result;
    if(coiling_) {
        const Vec2 shift=cursor_-origin_,restPivot=origin_+Vec2{41.6,0},pivot=restPivot+shift;
        const double sine=std::sin(angle_),cosine=std::cos(angle_);
        for(std::size_t i=0;i<target.size();++i) {
            if(i<=3) {target[i]+=shift;continue;} // the handle stays in the hand
            const Vec2 r=coil_[i]-restPivot;
            target[i]=pivot+Vec2{r.x*cosine+r.y*sine,-r.x*sine+r.y*cosine};
        }
    }
    for(std::size_t i=0;i<result.size();++i) {
        result[i]=mix(source_[i],target[i],ease);
        if(coiling_ && t>0 && t<1) {
            const double s=static_cast<double>(i)/Whip::segments,arc=std::sin(3.141592653589793*t)*std::sin(s*6.283185307+3*t)*12*s;
            result[i]+=Vec2{arc,-arc*0.45};
        }
    }
    return result;
}
}
