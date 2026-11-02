#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace whip {
struct Vec2 {
    double x{}, y{};
    Vec2 operator+(Vec2 b) const { return {x+b.x,y+b.y}; }
    Vec2 operator-(Vec2 b) const { return {x-b.x,y-b.y}; }
    Vec2 operator*(double s) const { return {x*s,y*s}; }
    Vec2 operator/(double s) const { return {x/s,y/s}; }
    Vec2& operator+=(Vec2 b) { x+=b.x; y+=b.y; return *this; }
    Vec2& operator-=(Vec2 b) { x-=b.x; y-=b.y; return *this; }
};
double length(Vec2 v);
double dot(Vec2 a, Vec2 b);
Vec2 mix(Vec2 a, Vec2 b, double t);
enum class Mode { Sadomaso, Exact };
enum class Language { English, Russian };
struct Rect { int x{},y{},width{},height{}; bool contains(Vec2 p) const; };
struct NormalizedPoint { int x{},y{}; };
NormalizedPoint normalizePoint(Vec2 p, Rect desktop);
struct GestureEvent { enum class Kind { Swing, Stop }; Kind kind; Vec2 point; double time{},speed{}; std::uint64_t id{}; Vec2 direction{}; };
class GestureDetector {
public:
    static constexpr double fastSpeed=2000,peakSpeed=3000,sustainedSpeed=2500;
    std::optional<GestureEvent> sample(double time, Vec2 point, bool buttonHeld);
    void reset(double time, Vec2 point);
    double speed() const { return speed_; }
    bool armed() const { return armed_; }
private:
    Vec2 previous_{},strokeStart_{},strokeDelta_{};
    double previousTime_{},speed_{},travel_{},peak_{},lastFast_{},quietSince_{-1},cooldownUntil_{},fastDuration_{},acceleration_{},previousSpeed_{},strokeStarted_{};
    bool initialized_{},armed_{};
    std::uint64_t serial_{};
    struct MotionPoint {double time;Vec2 point;};
    std::deque<MotionPoint> history_;
};
struct ImpactEvent { std::uint64_t gestureId{}; double time{},stopTime{},speed{}; Vec2 point{}; };
enum class WhipStyle { Contour, Heavy, Spring };
// gravity        px/s^2
// damping        linear air damping, 1/s
// bendCompliance 1 / (bending stiffness of the belly), i.e. EI_belly = 1/bendCompliance [mass*px^3/s^2]
// bodyWidth,outline  drawing only (used by lash_visual.hpp)
// drag           quadratic drag coefficient across the belly, 1/px (thinner parts get proportionally more)
struct WhipTuning { double gravity,damping,bendCompliance,bodyWidth,outline,drag; };
WhipTuning tuning(WhipStyle style);
class Whip {
public:
    // A real chain of rigid bars (one handle bar + 48 thong bars) is integrated with
    // exact inextensibility. The render curve is a smooth Hermite spline through the nodes.
    static constexpr std::size_t solverSegments=49;
    static constexpr std::size_t segments=64;
    static constexpr double segmentLength=12.0;
    static constexpr double totalLength=segments*segmentLength;
    static constexpr double handleLength=76.8*0.88;
    static constexpr double handleWidthRatio=1.35*1.15;
    static constexpr double tailLinkLength=(totalLength-handleLength)/(solverSegments-1);
    static constexpr double step=1.0/960.0;
    void reset(Vec2 cursor);
    void advance(double elapsed, Vec2 cursor, double attachment);
    const std::array<Vec2,segments+1>& points() const { return position_; }
    const std::array<Vec2,solverSegments+1>& nodes() const { return nodes_; }
    Vec2 tip() const { return position_.back(); }
    double tipSpeed() const { return tipSpeed_; }
    Vec2 tipVelocity() const {return tipVelocity_;}
    Vec2 handleVelocity() const {return handVelocity_;}
    double motion() const;
    // Largest relative deviation of any bar from its rest length (diagnostics).
    double stretchError() const;
    void style(WhipStyle value) {style_=value;}
private:
    void integrate(Vec2 cursor);
    void updateCurve(Vec2 cursor,double attachment=0);
    void solveVelocities();
    void solvePositions(int iterations);
    std::array<Vec2,segments+1> position_{};
    std::array<Vec2,solverSegments+1> nodes_{},velocity_{};
    Vec2 control_{},hand_{},handVelocity_{},tipVelocity_{};
    double accumulator_{},tipSpeed_{},controlAttachment_{},targetAngle_{1.5707963267948966},quietTime_{};
    WhipStyle style_=WhipStyle::Spring;
};
class ContactValidator {
public:
    void arm(const GestureEvent& stop,Mode mode);
    void cancel() {if(stop_ && stop_->id>consumed_) consumed_=stop_->id;stop_.reset();}
    bool pending() const {return stop_.has_value();}
    Vec2 target() const {return stop_?stop_->point:Vec2{};}
    std::optional<ImpactEvent> update(double time,const Whip& whip,Vec2 cursor,bool held);
private:
    std::optional<GestureEvent> stop_;
    Mode mode_=Mode::Sadomaso;
    std::uint64_t consumed_{};
};
struct PcmWave { std::uint32_t rate{}; std::uint16_t channels{},bits{}; std::vector<std::uint8_t> data; };
std::optional<PcmWave> parseWave(std::span<const std::uint8_t> bytes);
class CoilAnimation {
public:
    using Points=std::array<Vec2,Whip::segments+1>;
    void begin(const Points& visible,Vec2 handle,bool coil,double time);
    void follow(Vec2 cursor,double elapsed);
    Points sample(const Points& live,double time) const;
    bool active(double time) const {return initialized_ && time<start_+duration_;}
    bool coiled() const {return initialized_ && coiling_;}
    double motion() const {return motion_;}
private:
    // The rolled whip is a rigid ring hanging from the end of the handle like a pendulum.
    Points source_{},coil_{};
    Vec2 origin_{},cursor_{},anchor_{},anchorVelocity_{};
    double motion_{},accumulator_{};
    double start_{},duration_{};
    double angle_{},spin_{};
    bool initialized_{},coiling_{};
};
}
