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
    if(raw>1600) {
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
    if(!armed_ && fastDuration_>=0.028 && travel_>=100 && peak_>=2400 && acceleration_>=16000 && coherence>=0.90 && average>=1800 && sustained>=2000 && t-lastFast_<0.012) {
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
WhipTuning tuning(WhipStyle style) {
    if(style==WhipStyle::Heavy) return {3200,2.3,0.0010,8.0,2.08};
    if(style==WhipStyle::Spring) return {2200,1.5,0.000025,5.6,2.72};
    return {2600,1.9,0.0014,4.4,1.76};
}
void Whip::reset(Vec2 cursor) {
    for(std::size_t i=0;i<=solverSegments;++i) nodes_[i]=oldNodes_[i]=cursor+Vec2{0,i?handleLength+(i-1)*tailLinkLength:0};
    control_=stepControl_=cursor;controlAttachment_=0;tipVelocity_=handleVelocity_={};handleAngle_=targetAngle_=1.5707963267948966;handleSpin_=quietTime_=0;
    updateCurve(cursor);previous_=position_;accumulator_=tipSpeed_=0;
}
void Whip::advance(double elapsed,Vec2 cursor,double attachment) {
    elapsed=std::clamp(elapsed,0.0,0.035);if(elapsed<=0) return;
    const auto from=control_;const double oldAttachment=controlAttachment_,before=accumulator_;accumulator_+=elapsed;
    double time=step-before;
    while(accumulator_>=step) {
        const double part=std::clamp(time/elapsed,0.0,1.0);
        integrate(mix(from,cursor,part),oldAttachment+(std::clamp(attachment,0.0,1.0)-oldAttachment)*part);
        accumulator_-=step;time+=step;
    }
    control_=cursor;controlAttachment_=std::clamp(attachment,0.0,1.0);
    updateCurve(cursor,controlAttachment_);
}
void Whip::integrate(Vec2 cursor,double attachment) {
    const Vec2 oldTip=position_.back(),oldHandle=position_.front(),velocityInput=(cursor-stepControl_)/step;stepControl_=cursor;
    const double inputSpeed=length(velocityInput);
    if(inputSpeed>350) {
        const double desired=std::atan2(velocityInput.y,velocityInput.x);
        if(inputSpeed>=1400) targetAngle_=desired;
        else targetAngle_+=std::remainder(desired-targetAngle_,6.283185307179586)*std::pow(inputSpeed/2000,2)*(1-std::exp(-10*step));
        quietTime_=0;
    }else quietTime_+=step;
    if(quietTime_>0.8) targetAngle_=1.5707963267948966;
    const double error=std::remainder(targetAngle_-handleAngle_,6.283185307179586);
    handleSpin_+=(error*650-handleSpin_*32)*step;handleAngle_+=handleSpin_*step;
    const Vec2 gripDirection{std::cos(handleAngle_),std::sin(handleAngle_)};
    const auto restLength=[](std::size_t i){return i==0?handleLength:tailLinkLength;};
    const auto settings=tuning(style_);previous_=position_;
    std::array<double,solverSegments+1> inv{};
    for(std::size_t i=0;i<=solverSegments;++i) {
        const double s=static_cast<double>(i)/solverSegments;inv[i]=std::pow(18.0,s);
        auto velocity=(nodes_[i]-oldNodes_[i])*std::exp(-settings.damping*step);
        const double v=length(velocity),limit=(7000+25000*s*s)*step;if(v>limit) velocity=velocity*(limit/v);
        oldNodes_[i]=nodes_[i];nodes_[i]+=velocity+Vec2{0,settings.gravity*step*step};
    }
    std::array<double,solverSegments> stretch{};
    std::array<double,solverSegments-1> bend{};
    const auto pin=[&]{nodes_[0]=cursor;nodes_[1]=cursor+gripDirection*handleLength;};
    const auto constrain=[&](std::size_t a,std::size_t b,double rest,double compliance,double& lambda){
        const Vec2 delta=nodes_[b]-nodes_[a];const double distance=length(delta);
        if(distance<1e-7) return;
        const double alpha=compliance/(step*step);
        const double dl=(-(distance-rest)-alpha*lambda)/(inv[a]+inv[b]+alpha);lambda+=dl;
        nodes_[a]-=delta*(inv[a]*dl/distance);nodes_[b]+=delta*(inv[b]*dl/distance);
    };
    const auto separate=[&](std::size_t i,std::size_t j) {
        const Vec2 u=nodes_[i+1]-nodes_[i],v=nodes_[j+1]-nodes_[j],w=nodes_[i]-nodes_[j];
        const double a=dot(u,u),b=dot(u,v),c=dot(v,v),d=dot(u,w),e=dot(v,w),denom=a*c-b*b;
        double s=denom>1e-6?std::clamp((b*e-c*d)/denom,0.0,1.0):0;
        const double t=std::clamp((b*s+e)/std::max(c,1e-6),0.0,1.0);s=std::clamp((b*t-d)/std::max(a,1e-6),0.0,1.0);
        Vec2 delta=mix(nodes_[i],nodes_[i+1],s)-mix(nodes_[j],nodes_[j+1],t);const double distance=length(delta);
        if(distance>=16) return;
        if(distance<1e-5) {delta={-u.y,u.x};const auto before=mix(oldNodes_[i],oldNodes_[i+1],s)-mix(oldNodes_[j],oldNodes_[j+1],t);if(dot(delta,before)<0) delta=delta*-1;delta=delta/std::max(length(delta),1e-6);}else delta=delta/distance;
        const double wa=1-s,wb=s,wc=1-t,wd=t,weight=inv[i]*wa*wa+inv[i+1]*wb*wb+inv[j]*wc*wc+inv[j+1]*wd*wd;
        const Vec2 correction=delta*((16-distance)/std::max(weight,1e-6));
        nodes_[i]+=correction*(inv[i]*wa);nodes_[i+1]+=correction*(inv[i+1]*wb);nodes_[j]-=correction*(inv[j]*wc);nodes_[j+1]-=correction*(inv[j+1]*wd);
    };
    for(int iteration=0;iteration<20;++iteration) {
        pin();
        for(std::size_t i=0;i<solverSegments;++i) constrain(i,i+1,restLength(i),0,stretch[i]);
        for(std::size_t i=0;i+2<=solverSegments;++i) constrain(i,i+2,(restLength(i)+restLength(i+1))*0.985,settings.bendCompliance*std::pow(3.0,static_cast<double>(i)),bend[i]);
        for(std::size_t i=0;i<solverSegments;++i) for(std::size_t j=i+2;j<solverSegments;++j) separate(i,j);
        pin();
    }
    // Finish with inextensible links. The first full link is a rigid handle.
    for(int pass=0;pass<5;++pass) {for(std::size_t i=0;i<solverSegments;++i) constrain(i,i+1,restLength(i),0,stretch[i]);pin();}
    updateCurve(cursor,attachment);
    tipVelocity_=(position_.back()-oldTip)/step;handleVelocity_=(position_.front()-oldHandle)/step;tipSpeed_=length(tipVelocity_);
}
void Whip::updateCurve(Vec2 cursor,double attachment) {
    for(std::size_t i=0;i<=segments;++i) {
        const double arc=static_cast<double>(i)*totalLength/segments;
        const double coordinate=arc<=handleLength?arc/handleLength:1+(arc-handleLength)/tailLinkLength;
        const auto k=std::min(static_cast<std::size_t>(coordinate),solverSegments-1);const double t=coordinate-k;
        if(k==0) position_[i]=mix(nodes_[0],nodes_[1],t);
        else {
            const auto a=nodes_[k-1],b=nodes_[k],c=nodes_[k+1],d=nodes_[std::min(k+2,solverSegments)];
            // The handle tangent remains collinear through the seamless join.
            const auto tangent=k==1?b-a:(c-a)*0.5,endTangent=k+1==solverSegments?c-b:(d-b)*0.5;
            const double t2=t*t,t3=t2*t;position_[i]=b*(2*t3-3*t2+1)+tangent*(t3-2*t2+t)+c*(-2*t3+3*t2)+endTangent*(t3-t2);
        }
    }
    // Exact changes the held point by a rigid translation of the curve.
    // Rest lengths and relative velocities are identical in both modes.
    const double coordinate=attachment*segments;const auto a=std::min(static_cast<std::size_t>(coordinate),segments-1);
    const auto offset=cursor-mix(position_[a],position_[a+1],coordinate-a);
    for(auto& point:position_) point+=offset;
}
double Whip::motion() const {
    double energy=0;for(std::size_t i=0;i<=segments;++i) energy+=length(position_[i]-previous_[i])/step;
    return energy/(segments+1);
}
void ContactValidator::arm(const GestureEvent& stop,Mode mode) {
    cancel();
    if(stop.kind!=GestureEvent::Kind::Stop || stop.id<=consumed_ || stop.speed<2400 || !std::isfinite(stop.speed) || std::abs(length(stop.direction)-1)>0.01 || !std::isfinite(stop.time) || !std::isfinite(stop.point.x) || !std::isfinite(stop.point.y)) return;
    stop_=stop;mode_=mode;
}
std::optional<ImpactEvent> ContactValidator::update(double time,const Whip& whip,Vec2 cursor,bool held) {
    if(!stop_) return {};
    if(held || time<stop_->time || time-stop_->time>0.080 || length(cursor-stop_->point)>8) {cancel();return {};}
    const auto offset=whip.tip()-whip.points().front(),velocity=whip.tipVelocity()-whip.handleVelocity();
    const double reach=length(offset),forward=dot(velocity,stop_->direction);
    // This is a physical contact gate, not a target radius or a tip-position
    // correction. A folded, trailing, stationary or inward-moving tip cannot click.
    if(reach<Whip::totalLength*0.84 || reach>Whip::totalLength*1.06 || dot(offset,stop_->direction)<Whip::totalLength*0.35 || forward<1800 || dot(velocity,offset)<=0) return {};
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
    origin_=cursor_=handle;motion_=accumulator_=0;
    constexpr double pi=3.14159265358979323846;
    const Vec2 center=handle+Vec2{41.6,54.4};
    for(std::size_t i=0;i<=Whip::segments;++i) {
        if(i<=3) coil_[i]=handle+Vec2{41.6*i/3,0};
        else {
            const double s=static_cast<double>(i-3)/(Whip::segments-3),angle=-pi/2+s*pi*5.3,radius=54.4-23.2*s;
            coil_[i]=center+Vec2{std::cos(angle)*radius,std::sin(angle)*radius};
        }
    }
    constexpr std::array<std::size_t,8> indices{0,3,13,23,33,43,53,64};
    for(std::size_t i=0;i<indices.size();++i) {rest_[i]=coil_[indices[i]]-handle;bodies_[i]=previousBodies_[i]=coil_[indices[i]];}
}
void CoilAnimation::follow(Vec2 cursor,double elapsed) {
    if(!initialized_ || !coiling_) return;
    cursor_=cursor;accumulator_+=std::clamp(elapsed,0.0,0.035);constexpr double dt=1.0/240;
    while(accumulator_>=dt) {
        accumulator_-=dt;motion_=0;
        for(std::size_t i=0;i<bodies_.size();++i) {
            const auto before=bodies_[i],target=cursor+rest_[i];
            if(i<2) {bodies_[i]=previousBodies_[i]=target;continue;}
            auto velocity=(bodies_[i]-previousBodies_[i])*std::exp(-7.0*dt);previousBodies_[i]=before;
            bodies_[i]+=velocity+((target-bodies_[i])*85+Vec2{0,500})*(dt*dt);
            const auto offset=bodies_[i]-target;const double distance=length(offset);if(distance>75) bodies_[i]=target+offset*(75/distance);
            motion_=std::max(motion_,length(bodies_[i]-before)/dt);
        }
        // Preserve the rolled shape while the free masses trail the two pins.
        for(int pass=0;pass<4;++pass) for(std::size_t i=0;i+1<bodies_.size();++i) {
            const auto delta=bodies_[i+1]-bodies_[i];const double distance=length(delta),rest=length(rest_[i+1]-rest_[i]);if(distance<1e-6) continue;
            const double a=i<2?0.0:1.0,b=i+1<2?0.0:1.0;if(a+b==0) continue;
            const auto correction=delta*((distance-rest)/(distance*(a+b))*0.35);bodies_[i]+=correction*a;bodies_[i+1]-=correction*b;
        }
    }
}
CoilAnimation::Points CoilAnimation::sample(const Points& live,double time) const {
    if(!initialized_) return live;
    const double t=std::clamp((time-start_)/duration_,0.0,1.0),ease=t*t*t*(10+t*(-15+6*t));
    Points target=coiling_?coil_:live;Points result;
    if(coiling_) {
        constexpr std::array<std::size_t,8> indices{0,3,13,23,33,43,53,64};
        for(std::size_t i=0;i<target.size();++i) {
            std::size_t k=0;while(k+2<indices.size() && i>indices[k+1]) ++k;
            const double part=static_cast<double>(i-indices[k])/(indices[k+1]-indices[k]);
            const auto a=bodies_[k]-(origin_+rest_[k]),b=bodies_[k+1]-(origin_+rest_[k+1]);
            target[i]+=mix(a,b,part);
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
