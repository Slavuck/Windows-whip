#pragma once
#include <array>
#include <cstdint>
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
struct GestureEvent { enum class Kind { Swing, Stop }; Kind kind; Vec2 point; double time{},speed{}; std::uint64_t id{}; };
class GestureDetector {
public:
    std::optional<GestureEvent> sample(double time, Vec2 point, bool buttonHeld);
    void reset(double time, Vec2 point);
    double speed() const { return speed_; }
    bool armed() const { return armed_; }
private:
    Vec2 previous_{};
    double previousTime_{},speed_{},travel_{},peak_{},lastFast_{},quietSince_{-1},cooldownUntil_{};
    bool initialized_{},armed_{};
    std::uint64_t serial_{};
};
struct ImpactEvent { std::uint64_t gestureId{}; double time{},stopTime{},speed{}; Vec2 point{}; };
class Whip {
public:
    static constexpr std::size_t segments=64;
    static constexpr double segmentLength=15.0;
    static constexpr double step=1.0/480.0;
    void reset(Vec2 cursor);
    void advance(double elapsed, Vec2 cursor, double attachment);
    const std::array<Vec2,segments+1>& points() const { return position_; }
    Vec2 tip() const { return position_.back(); }
    double tipSpeed() const { return tipSpeed_; }
    double motion() const;
    double forecastPeak(Vec2 cursor, double attachment) const;
private:
    void integrate(Vec2 cursor, double attachment);
    std::array<Vec2,segments+1> position_{},previous_{};
    double accumulator_{},tipSpeed_{};
};
struct PcmWave { std::uint32_t rate{}; std::uint16_t channels{},bits{}; std::vector<std::uint8_t> data; };
std::optional<PcmWave> parseWave(std::span<const std::uint8_t> bytes);
}
