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
    previous_=p;previousTime_=t;speed_=travel_=peak_=0;lastFast_=t;quietSince_=-1;cooldownUntil_=t;initialized_=true;armed_=false;
}
std::optional<GestureEvent> GestureDetector::sample(double t,Vec2 p,bool held) {
    if(!initialized_ || t<=previousTime_ || t-previousTime_>0.2) {reset(t,p);return {};}
    const double dt=t-previousTime_,distance=length(p-previous_);
    speed_=distance/dt;previous_=p;previousTime_=t;
    if(held) {reset(t,p);cooldownUntil_=t+0.15;return {};}
    if(t<cooldownUntil_) return {};
    if(speed_>180) {travel_+=distance;quietSince_=-1;} else if(quietSince_<0) quietSince_=t;
    if(speed_>750) {lastFast_=t;peak_=std::max(peak_,speed_);}
    if(!armed_ && travel_>=55 && peak_>=900 && t-lastFast_<0.10) {
        armed_=true; return GestureEvent{GestureEvent::Kind::Swing,p,t,peak_,++serial_};
    }
    if(armed_ && speed_<110 && quietSince_>=0 && t-quietSince_>=0.018 && t-lastFast_<0.16) {
        armed_=false;travel_=0;const double power=peak_;peak_=0;cooldownUntil_=t+0.16;
        return GestureEvent{GestureEvent::Kind::Stop,p,t,power,serial_};
    }
    if(t-lastFast_>0.22 || (quietSince_>=0 && t-quietSince_>0.12)) {armed_=false;travel_=peak_=0;}
    return {};
}
void Whip::reset(Vec2 cursor) {
    for(std::size_t i=0;i<=segments;++i) {
        const double s=static_cast<double>(i)/segments;
        position_[i]=cursor+Vec2{std::sin(s*4.0)*96.0,segments*segmentLength*s};previous_[i]=position_[i];
    }
    accumulator_=tipSpeed_=0;
}
void Whip::advance(double elapsed,Vec2 cursor,double attachment) {
    accumulator_+=std::clamp(elapsed,0.0,0.035);
    while(accumulator_>=step) { integrate(cursor,std::clamp(attachment,0.0,1.0));accumulator_-=step; }
}
void Whip::integrate(Vec2 cursor,double attachment) {
    const Vec2 oldTip=position_.back();
    std::array<double,segments+1> inv{};
    for(std::size_t i=0;i<=segments;++i) {
        const double s=static_cast<double>(i)/segments;inv[i]=1.0+12*s*s;
        auto velocity=(position_[i]-previous_[i])*std::exp(-3.0*step);
        const double v=length(velocity); if(v>35) velocity=velocity*(35/v);
        previous_[i]=position_[i];position_[i]+=velocity+Vec2{0,190*step*step};
    }
    std::array<double,segments> stretch{};
    std::array<double,segments-1> bend{};
    const double coordinate=attachment*segments;
    const std::size_t anchor=std::min(static_cast<std::size_t>(coordinate),segments-1);
    const double fraction=coordinate-static_cast<double>(anchor);
    const auto pin=[&]{
        const double a=1-fraction,b=fraction;
        const Vec2 error=mix(position_[anchor],position_[anchor+1],fraction)-cursor;
        const double denominator=inv[anchor]*a*a+inv[anchor+1]*b*b;
        position_[anchor]-=error*(inv[anchor]*a/denominator);
        position_[anchor+1]-=error*(inv[anchor+1]*b/denominator);
    };
    const auto constrain=[&](std::size_t a,std::size_t b,double rest,double compliance,double& lambda){
        const Vec2 delta=position_[b]-position_[a];const double distance=length(delta);
        if(distance<1e-7) return;
        const double alpha=compliance/(step*step);
        const double dl=(-(distance-rest)-alpha*lambda)/(inv[a]+inv[b]+alpha);lambda+=dl;
        position_[a]-=delta*(inv[a]*dl/distance);position_[b]+=delta*(inv[b]*dl/distance);
    };
    for(int iteration=0;iteration<12;++iteration) {
        pin();
        for(std::size_t i=0;i<segments;++i) constrain(i,i+1,segmentLength,1e-9,stretch[i]);
        for(std::size_t i=0;i+2<=segments;++i) constrain(i,i+2,segmentLength*1.996,0.000003*(1+static_cast<double>(i)/8),bend[i]);
        pin();
    }
    tipSpeed_=length(position_.back()-oldTip)/step;
}
double Whip::motion() const {
    double energy=0;for(std::size_t i=0;i<=segments;++i) energy+=length(position_[i]-previous_[i])/step;
    return energy/(segments+1);
}
double Whip::forecastPeak(Vec2 cursor,double attachment) const {
    auto forecast=*this;double best=0,bestTime=0.05;
    for(int i=1;i<=34;++i) {
        forecast.advance(step,cursor,attachment);
        if(i>=12 && forecast.tipSpeed()>best) {best=forecast.tipSpeed();bestTime=i*step;}
    }
    return std::clamp(bestTime,0.032,0.068);
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
    constexpr double pi=3.14159265358979323846;
    const Vec2 center=handle+Vec2{52,68};
    for(std::size_t i=0;i<=Whip::segments;++i) {
        if(i<=3) coil_[i]=handle+Vec2{52.0*i/3,0};
        else {
            const double s=static_cast<double>(i-3)/(Whip::segments-3),angle=-pi/2+s*pi*5.3,radius=68-29*s;
            coil_[i]=center+Vec2{std::cos(angle)*radius,std::sin(angle)*radius};
        }
    }
}
CoilAnimation::Points CoilAnimation::sample(const Points& live,double time) const {
    if(!initialized_) return live;
    const double t=std::clamp((time-start_)/duration_,0.0,1.0),ease=t*t*t*(10+t*(-15+6*t));
    const auto& target=coiling_?coil_:live;Points result;
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
