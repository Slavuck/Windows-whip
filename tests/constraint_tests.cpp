#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

namespace whip {
// This access is limited to the algebraic constraint tests. No application,
// window, input hook, or integration step is involved.
struct PhysicsTestAccess {
    using State=std::array<Vec2,Whip::solverSegments+1>;
    static void seed(Whip& whip,const State& position,const State& velocity) {
        whip.nodes_=position;whip.velocity_=velocity;
        whip.hand_=position[0];whip.handVelocity_=velocity[0];
    }
    static void project(Whip& whip) {whip.solveVelocities();}
};
}

namespace {
using namespace whip;
using State=PhysicsTestAccess::State;
int checks{};
void require(bool condition,const char* message) {
    ++checks;if(!condition) throw std::runtime_error(message);
}
double energy(const State& velocity,Vec2 frame={}) {
    const auto mass=Whip::particleMasses();double result=0;
    for(std::size_t i=1;i<velocity.size();++i) {
        const auto relative=velocity[i]-frame;
        result+=0.5*mass[i]*dot(relative,relative);
    }
    return result;
}
double maximumSpeed(const State& velocity) {
    double result=0;for(const auto v:velocity) result=std::max(result,length(v));return result;
}
State chain(int shape) {
    State position{};
    for(std::size_t i=0;i<Whip::solverSegments;++i) {
        double angle=1.5707963267948966;
        if(shape>0) angle+=1.1*std::sin(i*0.31+shape*0.17)+0.3*std::sin(i*0.73+shape);
        const double rest=i==0?Whip::handleLength:Whip::tailLinkLength;
        position[i+1]=position[i]+Vec2{std::cos(angle)*rest,std::sin(angle)*rest};
    }
    return position;
}
void checkProjection(const State& position,const State& input) {
    Whip whip;PhysicsTestAccess::seed(whip,position,input);
    const double before=energy(input),scale=std::max(1.0,before);
    PhysicsTestAccess::project(whip);const auto projected=whip.velocities();
    require(length(projected[0]-input[0])<1e-12,"projection keeps the kinematic root fixed");
    require(energy(projected)<=before+scale*1e-10,"a stationary-root constraint projection cannot create kinetic energy");
    require(whip.constraintVelocityError()<=2e-8*std::max(1.0,maximumSpeed(input)),"projected velocities satisfy the current bar Jacobian");
    State removed{};for(std::size_t i=0;i<input.size();++i) removed[i]=input[i]-projected[i];
    require(std::abs(before-energy(projected)-energy(removed))<=scale*1e-8,"constraint correction is mass-orthogonal to the tangent velocity");
    PhysicsTestAccess::project(whip);const auto twice=whip.velocities();
    double change=0;for(std::size_t i=0;i<twice.size();++i) change=std::max(change,length(twice[i]-projected[i]));
    require(change<=2e-8*std::max(1.0,maximumSpeed(input)),"the velocity projection is idempotent");

    // A common frame velocity changes neither the relative constraint nor
    // the correction; work at a moving boundary is not mistaken for a bug.
    const Vec2 frame{137,-83};State moving=input;
    for(auto& velocity:moving) velocity+=frame;
    PhysicsTestAccess::seed(whip,position,moving);PhysicsTestAccess::project(whip);
    const auto shifted=whip.velocities();double frameError=0;
    for(std::size_t i=0;i<shifted.size();++i) frameError=std::max(frameError,length(shifted[i]-frame-projected[i]));
    require(frameError<=2e-8*std::max(1.0,maximumSpeed(input)),"projection is invariant under a uniform change of velocity frame");
}
}
int main() {
    try {
        // Regression for the old compression-triggered lateral kick. Its
        // artificial kick aligned with an existing transverse velocity and
        // increased KE, although the root was stationary and no force acted.
        State adversarial{};
        for(std::size_t node=1;node<adversarial.size();++node) {
            const auto bar=node-1;const double sign=((bar*2654435761u)>>7&1u)?1.0:-1.0;
            adversarial[node]={-1000*sign,-1};
        }
        checkProjection(chain(0),adversarial);
        std::mt19937 random(0x1937);std::uniform_real_distribution<double> speed(-1200,1200);
        for(int shape=1;shape<=32;++shape) {
            const auto position=chain(shape);State velocity{};
            for(std::size_t i=1;i<velocity.size();++i) velocity[i]={speed(random),speed(random)};
            checkProjection(position,velocity);
            State stationary{};Whip whip;PhysicsTestAccess::seed(whip,position,stationary);PhysicsTestAccess::project(whip);
            require(maximumSpeed(whip.velocities())<1e-12,"zero velocity cannot excite a deformed chain");
        }
        std::cout<<checks<<" passive constraint projection checks passed. No desktop input.\n";
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
