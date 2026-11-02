// Portable numerical replay: no Windows headers, overlays, hooks, audio or clicks.
// Build with: g++ -std=c++20 -O2 -Isrc src/core.cpp tests/physics_tests.cpp -o physics_tests
#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using whip::Vec2;
using whip::Whip;
constexpr double pi=3.14159265358979323846;
double squared(Vec2 v) {return whip::dot(v,v);}
double smooth(double t) {t=std::clamp(t,0.0,1.0);return t*t*(3-2*t);}
struct Scenario {
    std::string name;
    double duration=4,stop=0;
    Vec2 point(double t) const {
        if(name=="stationary") return {};
        if(name=="slow") return {180*smooth((t-0.5)/1.8),0};
        if(name=="jitter") return {0.35*std::sin(2*pi*30*t),0.2*std::sin(2*pi*19*t)};
        if(name=="small_pulse") return {smooth((t-0.5)/0.025)-smooth((t-0.525)/0.025),0};
        if(name=="constant_drift") return {std::clamp(t-0.5,0.0,1.6)*450,0};
        if(name=="reference_flick") return {260*std::clamp((t-0.60)/0.10,0.0,1.0),0};
        // A real finite-acceleration stroke; no teleport or artificial tip impulse.
        return {320*smooth((t-0.5)/0.12),0};
    }
};
struct Metrics {
    std::string name;
    double fps{},maxStretch{},peakTip{},peakHand{},postStopPeak{},lateTipRms{},lateHighFrequencyRms{},lateAccelerationRms{},lateNodeRms{},lateAngularRms{},forwardReach{},bodyAboveHand{},curvatureTurns{},finalTipX{},finalTipY{};
    bool finite=true;
    double fixedTailJointRateRms{},fixedTailJointHighFrequencyRms{};
    std::vector<Vec2> tipFrames;
};
struct JointMetrics {double rateRms{},highFrequencyRms{};};
double jointAngle(const std::array<Vec2,Whip::solverSegments+1>& nodes,std::size_t joint) {
    const Vec2 a=nodes[joint]-nodes[joint-1],b=nodes[joint+1]-nodes[joint];
    return std::atan2(a.x*b.y-a.y*b.x,whip::dot(a,b));
}
JointMetrics fixedJointReplay(const Scenario& scenario) {
    // Deliberately independent of display fps: sample local bending at each
    // actual integration tick, rather than aliasing a 960 Hz tail onto 144 Hz.
    Whip rope;rope.reset({0,0});
    constexpr std::size_t count=std::min<std::size_t>(8,Whip::solverSegments-1),first=Whip::solverSegments-count;
    std::array<double,count> previous{},lowPass{};
    for(std::size_t j=0;j<count;++j) previous[j]=jointAngle(rope.nodes(),first+j);
    double rateSquare=0,highSquare=0,late=0;
    const double dt=Whip::step,alpha=1-std::exp(-2*pi*12*dt);
    for(std::size_t tick=1;tick<=static_cast<std::size_t>(std::ceil(scenario.duration/dt));++tick) {
        const double time=tick*dt;rope.advance(dt,scenario.point(time),0);
        double frameRate=0,frameHigh=0;
        for(std::size_t j=0;j<count;++j) {
            const double angle=jointAngle(rope.nodes(),first+j),rate=std::remainder(angle-previous[j],2*pi)/dt;
            previous[j]=angle;lowPass[j]+=(rate-lowPass[j])*alpha;
            frameRate+=rate*rate;frameHigh+=(rate-lowPass[j])*(rate-lowPass[j]);
        }
        if(time>=scenario.stop+0.75) {late+=dt;rateSquare+=frameRate/count*dt;highSquare+=frameHigh/count*dt;}
    }
    return {std::sqrt(rateSquare/std::max(late,1e-9)),std::sqrt(highSquare/std::max(late,1e-9))};
}
Metrics replay(const Scenario& scenario,double fps,const std::filesystem::path& traceDirectory={},double perturbation=0,bool variable=false) {
    Whip rope;rope.reset({0,0});Metrics m;m.name=scenario.name+(variable?"_variable":"");m.fps=fps;
    std::ofstream trace,poses,curves;
    if(!traceDirectory.empty()) {
        std::filesystem::create_directories(traceDirectory);
        trace.open(traceDirectory/(m.name+"_"+std::to_string(static_cast<int>(fps))+".csv"));
        trace<<"t,cursor_x,cursor_y,hand_x,hand_y,neck_x,neck_y,mid_x,mid_y,tip_x,tip_y,tip_vx,tip_vy,stretch\n"<<std::setprecision(12);
        if(scenario.name=="reference_flick") {
            const auto stem=m.name+"_"+std::to_string(static_cast<int>(fps));
            poses.open(traceDirectory/(stem+"_poses.csv"));poses<<"t,node,x,y\n"<<std::setprecision(12);
            curves.open(traceDirectory/(stem+"_curves.csv"));curves<<"t,point,x,y\n"<<std::setprecision(12);
        }
    }
    auto previousNodes=rope.nodes();Vec2 previousVelocity{},lowPass{};double previousAngle=0;
    double totalLate=0,tipSquare=0,highSquare=0,accelerationSquare=0,nodeSquare=0,angleSquare=0;
    double time=0;unsigned frame=0;
    while(time<scenario.duration-1e-10) {
        const double multipliers[]{0.65,1.25,0.80,1.30};
        double dt=std::min((variable?multipliers[frame%4]:1.0)/fps,scenario.duration-time);
        if(scenario.name=="reference_flick") {
            // Preserve the exact start/stop and sample clocks supplied with
            // compare.png instead of snapping poses to the next display frame.
            for(double checkpoint:{0.60,0.66,0.70,0.72,0.78,0.84,0.90,1.00,1.20,1.55}) if(checkpoint>time+1e-10) {dt=std::min(dt,checkpoint-time);break;}
        }
        time+=dt;++frame;
        Vec2 mouse=scenario.point(time);
        if(perturbation) mouse.y+=perturbation*std::sin(2*pi*7*time)*std::min(1.0,time/0.1);
        rope.advance(dt,mouse,0);
        const auto& nodes=rope.nodes();const auto velocity=rope.tipVelocity();
        const double tipSpeed=whip::length(velocity),handSpeed=whip::length(rope.handleVelocity());
        m.maxStretch=std::max(m.maxStretch,rope.stretchError());m.peakTip=std::max(m.peakTip,tipSpeed);m.peakHand=std::max(m.peakHand,handSpeed);
        if(time>=scenario.stop && time<scenario.stop+0.75) {
            m.postStopPeak=std::max(m.postStopPeak,tipSpeed);m.forwardReach=std::max(m.forwardReach,nodes.back().x-nodes.front().x);
            double turn=0;
            for(std::size_t i=1;i<nodes.size();++i) {
                m.bodyAboveHand=std::max(m.bodyAboveHand,nodes.front().y-nodes[i].y);
                if(i+1<nodes.size()) {const auto a=nodes[i]-nodes[i-1],b=nodes[i+1]-nodes[i];turn+=std::abs(std::atan2(a.x*b.y-a.y*b.x,whip::dot(a,b)));}
            }
            m.curvatureTurns=std::max(m.curvatureTurns,turn/(2*pi));
        }
        lowPass+=(velocity-lowPass)*(1-std::exp(-2*pi*12*dt));
        const auto acceleration=(velocity-previousVelocity)/dt;
        const auto end=nodes.back()-nodes[nodes.size()-2];const double angle=std::atan2(end.y,end.x);
        const double angularRate=std::remainder(angle-previousAngle,2*pi)/dt;
        double frameNodes=0;
        for(std::size_t i=0;i<nodes.size();++i) {
            m.finite=m.finite && std::isfinite(nodes[i].x) && std::isfinite(nodes[i].y);
            frameNodes+=squared((nodes[i]-previousNodes[i])/dt);
        }
        m.finite=m.finite && std::isfinite(tipSpeed) && std::isfinite(m.maxStretch);
        if(time>=scenario.stop+0.75) {
            totalLate+=dt;tipSquare+=squared(velocity)*dt;highSquare+=squared(velocity-lowPass)*dt;
            accelerationSquare+=squared(acceleration)*dt;nodeSquare+=frameNodes/nodes.size()*dt;angleSquare+=angularRate*angularRate*dt;
        }
        m.tipFrames.push_back(nodes.back());
        if(poses) for(double sample:{0.66,0.72,0.78,0.84,0.90,1.00,1.20,1.55}) if(std::abs(time-sample)<1e-10) {
            for(std::size_t i=0;i<nodes.size();++i) poses<<sample<<','<<i<<','<<nodes[i].x<<','<<nodes[i].y<<'\n';
            const auto& points=rope.points();for(std::size_t i=0;i<points.size();++i) curves<<sample<<','<<i<<','<<points[i].x<<','<<points[i].y<<'\n';
        }
        if(trace) {
            const auto neck=nodes[std::min<std::size_t>(nodes.size()-1,1+static_cast<std::size_t>(Whip::handleLength/Whip::tailLinkLength))],middle=nodes[nodes.size()/2];
            trace<<time<<','<<mouse.x<<','<<mouse.y<<','<<nodes.front().x<<','<<nodes.front().y<<','<<neck.x<<','<<neck.y<<','<<middle.x<<','<<middle.y<<','<<nodes.back().x<<','<<nodes.back().y<<','<<velocity.x<<','<<velocity.y<<','<<rope.stretchError()<<'\n';
        }
        previousNodes=nodes;previousVelocity=velocity;previousAngle=angle;
    }
    const auto rms=[&](double value){return std::sqrt(value/std::max(totalLate,1e-9));};
    m.lateTipRms=rms(tipSquare);m.lateHighFrequencyRms=rms(highSquare);m.lateAccelerationRms=rms(accelerationSquare);m.lateNodeRms=rms(nodeSquare);m.lateAngularRms=rms(angleSquare);
    m.finalTipX=rope.nodes().back().x;m.finalTipY=rope.nodes().back().y;return m;
}
void row(std::ostream& out,const Metrics& m) {
    out<<m.name<<','<<m.fps<<','<<(m.finite?1:0)<<','<<m.maxStretch<<','<<m.peakTip<<','<<m.peakHand<<','<<m.postStopPeak<<','<<m.lateTipRms<<','<<m.lateHighFrequencyRms<<','<<m.lateAccelerationRms<<','<<m.lateNodeRms<<','<<m.lateAngularRms<<','<<m.forwardReach<<','<<m.bodyAboveHand<<','<<m.curvatureTurns<<','<<m.finalTipX<<','<<m.finalTipY<<','<<m.fixedTailJointRateRms<<','<<m.fixedTailJointHighFrequencyRms<<','<<1/Whip::step<<'\n';
}
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    try {
        std::filesystem::path output,traceDirectory;bool check=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--check") check=true;
            else if(arg=="--output" && i+1<argc) output=argv[++i];
            else if(arg=="--trace-dir" && i+1<argc) traceDirectory=argv[++i];
            else throw std::runtime_error("Usage: physics_tests [--check] [--output metrics.csv] [--trace-dir directory]");
        }
        std::vector<Metrics> metrics;
        const Scenario scenarios[]={{"stationary",4,0},{"slow",5,2.3},{"constant_drift",5,2.1},{"jitter",3,0},{"small_pulse",4,0.55},{"flick",4,0.62},{"reference_flick",4,0.70}};
        for(const auto& scenario:scenarios) metrics.push_back(replay(scenario,144,traceDirectory));
        const Scenario flick{"flick",4,0.62};
        for(double fps:{60.0,90.0,240.0}) metrics.push_back(replay(flick,fps,traceDirectory));
        metrics.push_back(replay(flick,144,traceDirectory,0,true));
        std::map<std::string,JointMetrics> jointResults;
        for(const auto& scenario:scenarios) jointResults.emplace(scenario.name,fixedJointReplay(scenario));
        for(auto& metric:metrics) {
            const auto& angular=jointResults.at(metric.name=="flick_variable"?"flick":metric.name);
            metric.fixedTailJointRateRms=angular.rateRms;metric.fixedTailJointHighFrequencyRms=angular.highFrequencyRms;
        }
        const auto repeated=replay(flick,144),perturbed=replay(flick,144,{},0.01);
        double repeatError=0,perturbedError=0;
        const auto& reference=metrics[5].tipFrames;
        for(std::size_t i=0;i<reference.size();++i) {
            repeatError=std::max(repeatError,whip::length(reference[i]-repeated.tipFrames[i]));
            // Sensitivity in the first 250 ms after the stroke, before long-time nonlinear divergence.
            const double t=(i+1)/144.0;if(t>=0.5 && t<=0.87) perturbedError=std::max(perturbedError,whip::length(reference[i]-perturbed.tipFrames[i]));
        }
        std::ofstream file;if(!output.empty()) {if(!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());file.open(output);if(!file) throw std::runtime_error("Cannot write metrics output");}
        auto& out=output.empty()?std::cout:file;out<<std::setprecision(12);
        out<<"scenario,fps,finite,max_relative_stretch,peak_tip_speed,peak_hand_speed,post_stop_peak_tip_speed,late_tip_rms,late_tip_high_frequency_rms,late_tip_acceleration_rms,late_node_speed_rms,late_tip_angular_rate_rms,post_stop_forward_reach,post_stop_body_above_hand,post_stop_max_curvature_turns,final_tip_x,final_tip_y,fixed_late_tail_joint_rate_rms,fixed_late_tail_joint_high_frequency_rms,fixed_joint_sample_hz\n";
        for(const auto& m:metrics) row(out,m);
        std::cerr<<"Deterministic replay max error: "<<repeatError<<" px; 0.01 px input perturbation, short-time tip max difference: "<<perturbedError<<" px.\n";
        if(check) {
            for(const auto& m:metrics) {require(m.finite,"simulation produced non-finite state");require(m.maxStretch<0.0001,"bar length error exceeded 0.01 percent");require(std::isfinite(m.fixedTailJointRateRms) && std::isfinite(m.fixedTailJointHighFrequencyRms),"fixed-rate angular diagnostics became non-finite");}
            require(metrics[0].peakTip<1,"stationary pointer spontaneously accelerated the lash");
            require(metrics[0].fixedTailJointRateRms<1e-6,"stationary pointer spontaneously excited tail bending");
            require(repeatError<1e-7,"identical input history did not replay deterministically");
            require(perturbedError<Whip::totalLength*0.1,"tiny input perturbation caused a discontinuous short-time tip jump");
            require(metrics[5].postStopPeak>metrics[5].peakHand,"a strong stroke lost physical tip-speed amplification");
            require(metrics[5].forwardReach>Whip::totalLength*0.15 && metrics[5].bodyAboveHand>Whip::handleLength*0.25,"a strong stroke no longer rolls above the hand and throws the tip forward");
            std::cerr<<"Headless numerical invariants passed. Metrics quantify behavior; they do not judge animation aesthetics.\n";
        }
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
