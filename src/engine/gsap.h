// A faithful C++ port of the parts of GSAP 2.1.3 (TweenLite / TweenMax / TimelineLite / TimelineMax /
// BezierPlugin) that the original game uses. Rapid key presses make hops overlap, and then GSAP's
// overwrite "auto", lazy first renders and timeline shrinking decide what the chicken does, so the
// algorithms follow the JavaScript line by line (function names in comments). Verified against the
// original frame by frame with tools/webref/trace.mjs + apps/trace.cpp.
//
// Supported: to() tweens on Vec3 components with delay/ease/repeat/yoyo/onComplete, TimelineMax with
// sequential to() children and repeat, bezier {type:"cubic"}, pause(), killTweensOf().
// Numbers are `real` (real.h): double on PC/R36S, 16.16 on the SF2000.
#pragma once

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "math.h"

namespace gsap {

using cr::real;
using cr::Vec3;

constexpr real kTiny = cr::kRealTiny;

struct Ease {
    int type = 1;  // 1 easeOut, 2 easeIn, 3 easeInOut (Ease._type)
    int power = 1; // Power0..Power4 index (Ease._power)
    bool bounceOut = false;
    real getRatio(real p) const;

    static Ease make(int easeType, int easePower) { return Ease{easeType, easePower, false}; }
    static Ease bounce() { return Ease{0, 0, true}; }
};

// Power1.easeOut is TweenLite.defaultEase
inline Ease Power1Out() { return Ease::make(1, 1); }
inline Ease Power1In() { return Ease::make(2, 1); }
inline Ease Power1InOut() { return Ease::make(3, 1); }
inline Ease Power2In() { return Ease::make(2, 2); }
inline Ease Power2InOut() { return Ease::make(3, 2); }
inline Ease BounceOut() { return Ease::bounce(); }

struct Vars {
    real delay = 0;
    bool hasEase = false;
    Ease ease;
    int repeat = 0;
    bool yoyo = false;
    std::function<void()> onComplete;

    Vars &withEase(const Ease &e)
    {
        hasEase = true;
        ease = e;
        return *this;
    }
};

struct Prop {
    char name; // 'x', 'y' or 'z'
    real value;
};

class Engine;
class Timeline;
class Tween;
struct Bezier;

class Animation : public std::enable_shared_from_this<Animation> {
public:
    virtual ~Animation() = default;

    Engine *engine = nullptr;
    Vars vars;
    real _duration = 0, _totalDuration = 0, _delay = 0, _timeScale = 1;
    real _startTime = 0, _time = 0, _totalTime = 0, _rawPrevTime = -1, _pauseTime = 0;
    bool _active = false, _reversed = false, _dirty = false, _gc = false, _initted = false, _paused = false;
    Timeline *_timeline = nullptr; // last parent, kept after removal (JS _timeline)
    Timeline *timeline = nullptr;  // current parent (JS timeline)
    Animation *_next = nullptr, *_prev = nullptr;

    virtual void render(real time, bool suppressEvents, bool force) = 0;
    virtual real totalDuration();
    virtual real duration();
    virtual bool enabled(bool enable, bool ignoreTimeline); // _enabled
    virtual bool isTimeline() const { return false; }
    virtual bool hasPausedChild() const { return false; }

    bool isActive();
    void uncache(bool includeSelf);
    void paused(bool value);
    void pause() { paused(true); }
    real rawTime() const { return _totalTime; }
};

class Timeline : public Animation {
public:
    Animation *_first = nullptr, *_last = nullptr, *_recent = nullptr;
    bool isRoot = false;
    bool autoRemoveChildren = false, smoothChildTiming = false, _sortChildren = false;
    bool _calculatingDuration = false;
    // TimelineMax
    int _repeat = 0;
    real _repeatDelay = 0;
    int _cycle = 0;
    bool _yoyo = false, _locked = false;

    std::unordered_map<Animation *, std::shared_ptr<Animation>> owned;

    // TimelineLite.to(target, duration, vars)
    Timeline &to(Vec3 *target, std::initializer_list<Prop> props, real duration, const Vars &v = Vars());

    void add(const std::shared_ptr<Animation> &child, real position); // SimpleTimeline.add (+TimelineLite.add)
    void remove(Animation *tween, bool skipDisable);                  // _remove
    void render(real time, bool suppressEvents, bool force) override;
    real totalDuration() override;
    real duration() override;
    bool enabled(bool enable, bool ignoreTimeline) override;
    bool isTimeline() const override { return true; }
    bool hasPausedChild() const override;

private:
    void renderSimple(real time, bool suppressEvents, bool force); // SimpleTimeline.render (root)
    void renderMax(real time, bool suppressEvents, bool force);    // TimelineMax.render
};

struct PropTween {
    real *target = nullptr; // plain property
    Bezier *plugin = nullptr;
    char name = 0;
    real s = 0, c = 0;
    PropTween *next = nullptr, *prev = nullptr;
};

// key -> "value != null" of a JS lookup object
using KillMap = std::map<char, bool>;

struct Bezier {
    struct Segment {
        real a, b, c, d, da, ca, ba;
    };
    Vec3 *target = nullptr;
    std::vector<char> props, overwriteProps;
    std::map<char, Segment> beziers;
    real length = 0;
    std::vector<real> lengths;
    std::vector<std::vector<real>> segments;
    real l1 = 0, l2 = 0, s1 = 0, s2 = 0, prec = 0;
    int li = 0, si = 0, curSeg = 0;

    void init(Vec3 *t, const Vec3 values[4]);
    void setRatio(real v);
    bool kill(const KillMap &lookup);
};

class Tween : public Animation {
public:
    Vec3 *target = nullptr;
    std::vector<Prop> props;
    bool hasBezier = false;
    Vec3 bezierValues[4];

    int _overwrite = 2; // "auto"
    int _repeat = 0;
    real _repeatDelay = 0;
    int _cycle = 0;
    bool _yoyo = false;
    real ratio = 0;
    Ease _ease;

    std::vector<std::unique_ptr<PropTween>> ptStore;
    std::vector<std::unique_ptr<Bezier>> pluginStore;
    PropTween *_firstPT = nullptr;
    std::map<char, PropTween *> propLookup; // present key with null value == JS undefined entry
    bool hasOverwrittenProps = false, overwrittenAll = false;
    std::set<char> overwrittenProps;

    bool lazyPending = false; // _lazy !== false
    real lazyTime = 0;
    bool lazySuppress = false;

    void render(real time, bool suppressEvents, bool force) override; // TweenMax.render
    real totalDuration() override;                                     // TweenMax.totalDuration
    real duration() override { return _duration; }
    bool enabled(bool enable, bool ignoreTimeline) override;

    enum class KillSource { OtherLookup, OwnOverwritten, OwnLookup };
    bool kill(const KillMap *vars, KillSource source, Tween *overwriting); // _kill (vars == nullptr: kill all)

private:
    friend class Engine;
    void init();       // _init
    bool initProps();  // _initProps
    void unlink(PropTween *pt);
};

class Engine {
public:
    Engine();

    // one requestAnimationFrame of 1000/60 ms: Ticker._tick + Animation._updateRoot
    void tick();

    std::shared_ptr<Tween> to(Vec3 *target, std::initializer_list<Prop> props, real duration, const Vars &v = Vars());
    std::shared_ptr<Tween> bezierTo(Vec3 *target, const Vec3 values[4], real duration, const Vars &v = Vars());
    std::shared_ptr<Timeline> timeline(const Vars &v = Vars());
    void killTweensOf(Vec3 *target);
    // drops every animation (a new game scene); the ticker keeps its time
    void clear();

    real time() const { return time_; }
    int liveAnimations() const;
    // debugging aid for trace comparisons: one line per root animation and its children
    std::string describe();

    // internals shared by the classes above
    Timeline root;
    std::vector<std::shared_ptr<Tween>> lazyTweens;
    std::set<Vec3 *> lazyLookup;
    std::unordered_map<Vec3 *, std::vector<std::shared_ptr<Tween>>> lookup;
    std::vector<std::shared_ptr<Animation>> graveyard;

    void lazyRender();
    bool applyOverwrite(Tween *tween, const KillMap &props);
    real checkOverlap(Animation *tween, real reference, bool zeroDur);
    void registerTween(const std::shared_ptr<Tween> &t, bool scrub);

private:
    std::shared_ptr<Tween> make(Vec3 *target, real duration, const Vars &v);
    void updateRoot();
#ifdef CR_FIXED
    // 16.16 cannot hold the ticker's millisecond epoch: game time is ticks / 60 s (see Engine::tick)
    uint32_t ticks_ = 0;
    real time_ = 0;
#else
    double epoch_ = 1700000000000.0, now_ = 0, lastUpdate_ = 0, startTime_ = 0, time_ = 0, nextTime_ = 0, gap_ = 0;
#endif
    int frame_ = 0, nextGCFrame_ = 30;
};

} // namespace gsap
