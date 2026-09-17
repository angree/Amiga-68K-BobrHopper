// Port of GSAP 2.1.3 core behaviour. Comments name the JavaScript functions each block follows
// (node_modules/gsap/TweenLite.js, TimelineLite.js, TimelineMax.js, TweenMaxBase.js, BezierPlugin.js).
#include "gsap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gsap {

using cr::kRealHuge;

static real *component(Vec3 *v, char name)
{
    return name == 'x' ? &v->x : name == 'y' ? &v->y : &v->z;
}

// ------------------------------------------------------------------ Ease.getRatio / Bounce.easeOut

real Ease::getRatio(real p) const
{
    if (bounceOut) {
        if (p < real(1 / 2.75)) return real(7.5625) * p * p;
        if (p < real(2 / 2.75)) {
            p -= real(1.5 / 2.75);
            return real(7.5625) * p * p + real(0.75);
        }
        if (p < real(2.5 / 2.75)) {
            p -= real(2.25 / 2.75);
            return real(7.5625) * p * p + real(0.9375);
        }
        p -= real(2.625 / 2.75);
        return real(7.5625) * p * p + real(0.984375);
    }
    const int t = type, pw = power;
    real r = (t == 1) ? 1 - p : (t == 2) ? p : (p < real(0.5)) ? p * 2 : (1 - p) * 2;
    if (pw == 1) r *= r;
    else if (pw == 2) r *= r * r;
    else if (pw == 3) r *= r * r * r;
    else if (pw == 4) r *= r * r * r * r;
    return (t == 1) ? 1 - r : (t == 2) ? r : (p < real(0.5)) ? cr::rhalf(r) : 1 - cr::rhalf(r); // r / 2
}

// ------------------------------------------------------------------ Animation

real Animation::totalDuration()
{
    _dirty = false;
    return _totalDuration;
}

real Animation::duration()
{
    _dirty = false;
    return _duration;
}

bool Animation::isActive()
{
    Timeline *tl = _timeline;
    if (!tl) return true;
    if (_gc || _paused || !tl->isActive()) return false;
    real raw = tl->rawTime();
    const real total = totalDuration();
    // x / 1 == x: the division (a 64-bit library call on the SF2000) only for other time scales
    return raw >= _startTime && raw < _startTime + (_timeScale == 1 ? total : total / _timeScale) - kTiny;
}

bool Animation::enabled(bool enable, bool ignoreTimeline)
{
    _gc = !enable;
    _active = isActive();
    if (!ignoreTimeline) {
        if (enable && !timeline) {
            if (_timeline) _timeline->add(shared_from_this(), _startTime - _delay);
        } else if (!enable && timeline) {
            _timeline->remove(this, true);
        }
    }
    return false;
}

void Animation::uncache(bool includeSelf)
{
    Animation *t = includeSelf ? this : timeline;
    while (t) {
        t->_dirty = true;
        t = t->timeline;
    }
}

void Animation::paused(bool value)
{
    Timeline *tl = _timeline;
    if (value != _paused && tl) {
        real raw = tl->rawTime();
        real elapsed = raw - _pauseTime;
        if (!value && tl->smoothChildTiming) {
            _startTime += elapsed;
            uncache(false);
        }
        _pauseTime = value ? raw : real(0);
        _paused = value;
        _active = isActive();
    }
    if (_gc && !value) enabled(true, false);
}

// ------------------------------------------------------------------ Timeline

Timeline &Timeline::to(Vec3 *target, std::initializer_list<Prop> props, real dur, const Vars &v)
{
    std::shared_ptr<Tween> child = engine->to(target, props, dur, v);
    // TimelineLite.add -> _parseTimeOrLabel(undefined, 0, true, child): append at duration()
    real clippedDuration = (duration() > kRealHuge) ? _duration : _duration;
    add(child, clippedDuration + 0);
    return *this;
}

void Timeline::add(const std::shared_ptr<Animation> &childPtr, real position)
{
    Animation *child = childPtr.get();
    std::shared_ptr<Animation> keep = childPtr;

    // SimpleTimeline.add
    child->_startTime = position + child->_delay;
    if (child->timeline) child->timeline->remove(child, true);
    child->timeline = child->_timeline = this;
    if (child->_gc) child->enabled(true, true);
    Animation *prevTween = _last;
    if (_sortChildren) {
        real st = child->_startTime;
        while (prevTween && prevTween->_startTime > st) prevTween = prevTween->_prev;
    }
    if (prevTween) {
        child->_next = prevTween->_next;
        prevTween->_next = child;
    } else {
        child->_next = _first;
        _first = child;
    }
    if (child->_next) child->_next->_prev = child;
    else _last = child;
    child->_prev = prevTween;
    _recent = child;
    owned[child] = keep;
    if (_timeline) uncache(true);

    if (isRoot) return;

    // TimelineLite.add tail: re-enable an ended timeline that got longer
    if (_gc || _time == _duration)
        if (!_paused)
            if (_duration < duration()) {
                Timeline *tl = this;
                bool beforeRawTime = tl->rawTime() > child->_startTime;
                while (tl->_timeline) {
                    if (beforeRawTime && tl->_timeline->smoothChildTiming) {
                        // tl.totalTime(tl._totalTime, true) -- never reached: timelines are filled before they run
                    } else if (tl->_gc) {
                        tl->enabled(true, false);
                    }
                    tl = tl->_timeline;
                }
            }
}

void Timeline::remove(Animation *tween, bool skipDisable)
{
    // SimpleTimeline._remove
    if (tween->timeline == this) {
        if (!skipDisable) tween->enabled(false, true);
        if (tween->_prev) tween->_prev->_next = tween->_next;
        else if (_first == tween) _first = tween->_next;
        if (tween->_next) tween->_next->_prev = tween->_prev;
        else if (_last == tween) _last = tween->_prev;
        tween->_next = tween->_prev = nullptr;
        tween->timeline = nullptr;
        if (tween == _recent) _recent = _last;
        if (_timeline) uncache(true);
        auto it = owned.find(tween);
        if (it != owned.end()) {
            engine->graveyard.push_back(it->second);
            owned.erase(it);
        }
    }
    if (isRoot) return;
    // TimelineLite._remove
    if (!_last) {
        _time = _totalTime = _duration = _totalDuration = 0;
    } else if (_time > duration()) {
        _time = _duration;
        _totalTime = _totalDuration;
    }
}

real Timeline::totalDuration()
{
    if (isRoot) return Animation::totalDuration();
    if (_dirty) {
        // TimelineLite.totalDuration
        real max = 0;
        Animation *tween = _last;
        real prevStart = kRealHuge;
        while (tween) {
            Animation *prev = tween->_prev;
            if (tween->_dirty) tween->totalDuration();
            if (tween->_startTime > prevStart && _sortChildren && !tween->_paused && !_calculatingDuration) {
                _calculatingDuration = true;
                std::shared_ptr<Animation> keep = owned[tween];
                add(keep, tween->_startTime - tween->_delay);
                _calculatingDuration = false;
            } else {
                prevStart = tween->_startTime;
            }
            real end = tween->_startTime +
                   (tween->_timeScale == 1 ? tween->_totalDuration : tween->_totalDuration / tween->_timeScale);
            if (end > max) max = end;
            tween = prev;
        }
        _duration = _totalDuration = max;
        _dirty = false;
        // TimelineMax.totalDuration
        _totalDuration = (_repeat == -1) ? kRealHuge : _duration * (_repeat + 1) + (_repeatDelay * _repeat);
    }
    return _totalDuration;
}

real Timeline::duration()
{
    if (isRoot) return Animation::duration();
    if (_dirty) totalDuration();
    return _duration;
}

bool Timeline::enabled(bool enable, bool ignoreTimeline)
{
    if (enable == _gc)
        for (Animation *tween = _first; tween; tween = tween->_next) tween->enabled(enable, true);
    return Animation::enabled(enable, ignoreTimeline);
}

bool Timeline::hasPausedChild() const
{
    for (Animation *tween = _first; tween; tween = tween->_next)
        if (tween->_paused || (tween->isTimeline() && tween->hasPausedChild())) return true;
    return false;
}

void Timeline::render(real time, bool suppressEvents, bool force)
{
    if (isRoot) renderSimple(time, suppressEvents, force);
    else renderMax(time, suppressEvents, force);
}

void Timeline::renderSimple(real time, bool suppressEvents, bool force)
{
    Animation *tween = _first;
    _totalTime = _time = _rawPrevTime = time;
    while (tween) {
        Animation *next = tween->_next;
        if (tween->_active || (time >= tween->_startTime && !tween->_paused && !tween->_gc)) {
            if (!tween->_reversed) tween->render((time - tween->_startTime) * tween->_timeScale, suppressEvents, force);
        }
        tween = next;
    }
}

void Timeline::renderMax(real time, bool suppressEvents, bool force)
{
    if (_gc) enabled(true, false);
    real prevTime = _time;
    real totalDur = (!_dirty) ? _totalDuration : totalDuration();
    real dur = _duration, prevTotalTime = _totalTime, prevStart = _startTime, prevTimeScale = _timeScale;
    real prevRawPrevTime = _rawPrevTime;
    bool prevPaused = _paused;
    int prevCycle = _cycle;
    bool isComplete = false, internalForce = false;
    enum { None, Complete, ReverseComplete } callback = None;
    if (prevTime != _time) time += _time - prevTime;

    if (time >= totalDur - kTiny && time >= 0) {
        if (!_locked) {
            _totalTime = totalDur;
            _cycle = _repeat;
        }
        if (!_reversed && !hasPausedChild()) {
            isComplete = true;
            callback = Complete;
            internalForce = _timeline->autoRemoveChildren;
            if (_duration == 0)
                if ((time <= 0 && time >= -kTiny) || prevRawPrevTime < 0 || prevRawPrevTime == kTiny)
                    if (prevRawPrevTime != time && _first) {
                        internalForce = true;
                        if (prevRawPrevTime > kTiny) callback = ReverseComplete;
                    }
        }
        _rawPrevTime = (_duration != 0 || !suppressEvents || time != 0 || _rawPrevTime == time) ? time : kTiny;
        if (_yoyo && (_cycle & 1)) {
            _time = time = 0;
        } else {
            _time = dur;
            time = dur + real(0.0001);
        }
    } else if (time < kTiny) {
        if (!_locked) {
            _totalTime = 0;
            _cycle = 0;
        }
        _time = 0;
        if (time > -kTiny) time = 0;
        if (prevTime != 0 ||
            (dur == 0 && prevRawPrevTime != kTiny && (prevRawPrevTime > 0 || (time < 0 && prevRawPrevTime >= 0)) && !_locked)) {
            callback = ReverseComplete;
            isComplete = _reversed;
        }
        if (time < 0) {
            _active = false;
            if (_timeline->autoRemoveChildren && _reversed) {
                internalForce = isComplete = true;
                callback = ReverseComplete;
            } else if (prevRawPrevTime >= 0 && _first) {
                internalForce = true;
            }
            _rawPrevTime = time;
        } else {
            _rawPrevTime = (dur != 0 || !suppressEvents || time != 0 || _rawPrevTime == time) ? time : kTiny;
            if (time == 0 && isComplete) {
                for (Animation *tween = _first; tween && tween->_startTime == 0; tween = tween->_next)
                    if (tween->_duration == 0) isComplete = false;
            }
            time = 0;
            if (!_initted) internalForce = true;
        }
    } else {
        if (dur == 0 && prevRawPrevTime < 0) internalForce = true;
        _time = _rawPrevTime = time;
        if (!_locked) {
            _totalTime = time;
            if (_repeat != 0) {
                real cycleDuration = dur + _repeatDelay;
                const real cycles = _totalTime / cycleDuration; // one division for both uses
                _cycle = int(cycles);
                if (_cycle && real(_cycle) == cycles && prevTotalTime <= time) _cycle--;
                _time = _totalTime - (_cycle * cycleDuration);
                if (_yoyo && (_cycle & 1)) _time = dur - _time;
                if (_time > dur) {
                    _time = dur;
                    time = dur + real(0.0001);
                } else if (_time < 0) {
                    _time = time = 0;
                } else {
                    time = _time;
                }
            }
        }
    }

    if (_cycle != prevCycle && !_locked) {
        bool backwards = (_yoyo && (prevCycle & 1) != 0);
        bool wrap = (backwards == (_yoyo && (_cycle & 1) != 0));
        real recTotalTime = _totalTime, recRawPrevTime = _rawPrevTime, recTime = _time;
        int recCycle = _cycle;

        _totalTime = prevCycle * dur;
        if (_cycle < prevCycle) backwards = !backwards;
        else _totalTime += dur;
        _time = prevTime;
        _rawPrevTime = (dur == 0) ? prevRawPrevTime - real(0.0001) : prevRawPrevTime;
        _cycle = prevCycle;
        _locked = true;
        prevTime = backwards ? real(0) : dur;
        render(prevTime, suppressEvents, dur == 0);
        if (prevTime != _time) return;
        if (wrap) {
            _cycle = prevCycle;
            _locked = true;
            prevTime = backwards ? dur + real(0.0001) : real(-0.0001);
            render(prevTime, true, false);
        }
        _locked = false;
        if (_paused && !prevPaused) return;
        _time = recTime;
        _totalTime = recTotalTime;
        _cycle = recCycle;
        _rawPrevTime = recRawPrevTime;
    }

    if ((_time == prevTime || !_first) && !force && !internalForce) return;
    if (!_initted) _initted = true;

    if (!_active)
        if (!_paused && _totalTime != prevTotalTime && time > 0) _active = true;

    real curTime = _time;
    if (curTime >= prevTime) {
        Animation *tween = _first;
        while (tween) {
            Animation *next = tween->_next;
            if (curTime != _time || (_paused && !prevPaused)) break;
            if (tween->_active || (tween->_startTime <= _time && !tween->_paused && !tween->_gc)) {
                if (!tween->_reversed) tween->render((time - tween->_startTime) * tween->_timeScale, suppressEvents, force);
            }
            tween = next;
        }
    } else {
        Animation *tween = _last;
        while (tween) {
            Animation *next = tween->_prev;
            if (curTime != _time || (_paused && !prevPaused)) break;
            if (tween->_active || (tween->_startTime <= prevTime && !tween->_paused && !tween->_gc)) {
                if (!tween->_reversed) tween->render((time - tween->_startTime) * tween->_timeScale, suppressEvents, force);
            }
            tween = next;
        }
    }

    if (callback != None)
        if (!_locked)
            if (!_gc)
                if (prevStart == _startTime || prevTimeScale != _timeScale)
                    if (_time == 0 || totalDur >= totalDuration()) {
                        if (isComplete) {
                            if (!engine->lazyTweens.empty()) engine->lazyRender();
                            if (_timeline->autoRemoveChildren) enabled(false, false);
                            _active = false;
                        }
                        if (!suppressEvents && callback == Complete && vars.onComplete) vars.onComplete();
                    }
}

// ------------------------------------------------------------------ BezierPlugin (type "cubic")

void Bezier::init(Vec3 *t, const Vec3 values[4])
{
    target = t;
    props = {'x', 'y', 'z'}; // for (p in values[0])
    for (int i = int(props.size()) - 1; i >= 0; i--) overwriteProps.push_back(props[size_t(i)]);

    // _parseBezierData(values, "cubic"): one Segment per property; obj keys are created z, y, x
    const char order[3] = {'z', 'y', 'x'};
    for (char p : order) {
        real a = *component(const_cast<Vec3 *>(&values[0]), p);
        real b = *component(const_cast<Vec3 *>(&values[1]), p);
        real c = *component(const_cast<Vec3 *>(&values[2]), p);
        real d = *component(const_cast<Vec3 *>(&values[3]), p);
        if (c == d) c = d - (d - b) / 1000000;
        if (a == b) b = a + (c - a) / 1000000;
        beziers[p] = Segment{a, b, c, d, d - a, c - a, b - a};
    }

    // _parseLengthData(beziers, 6), accumulating the properties in object key order (z, y, x)
    const int resolution = 6;
    std::vector<real> steps(resolution, real(0));
    std::vector<bool> stepSet(resolution, false);
    for (char p : order) {
        const Segment &bez = beziers[p];
        real s = bez.a, da = bez.d - s, ca = bez.c - s, ba = bez.b - s;
        real d = 0, d1 = 0;
        const real inc = real(1) / real(resolution);
        for (int i = 1; i <= resolution; i++) {
            real pp = inc * i, inv = 1 - pp;
            real next = (pp * pp * da + 3 * inv * (pp * ca + inv * ba)) * pp;
            d = d1 - next;
            d1 = next;
            int index = i - 1;
            steps[size_t(index)] = (stepSet[size_t(index)] ? steps[size_t(index)] : real(0)) + d * d;
            stepSet[size_t(index)] = true;
        }
    }
    real dsum = 0, total = 0;
    std::vector<real> curLS(resolution, real(0));
    for (int i = 0; i < resolution; i++) {
        dsum += cr::rsqrt(steps[size_t(i)]);
        int index = i % resolution;
        curLS[size_t(index)] = dsum;
        if (index == resolution - 1) {
            total += dsum;
            segments.push_back(curLS);
            lengths.push_back(total);
            dsum = 0;
        }
    }
    length = total;
    l1 = s1 = 0;
    li = si = 0;
    l2 = lengths[0];
    curSeg = 0;
    s2 = segments[0][0];
    prec = real(1) / real(int(segments[0].size()));
}

void Bezier::setRatio(real v)
{
    const std::vector<real> &seg = segments[size_t(curSeg)];
    real v1 = v * length;
    int i = li;
    int curIndex = i;
    v1 -= l1;
    i = si;
    int last = int(seg.size()) - 1;
    if (v1 > s2 && i < last) {
        while (i < last && (s2 = seg[size_t(++i)]) <= v1) {
        }
        s1 = seg[size_t(i - 1)];
        si = i;
    } else if (v1 < s1 && i > 0) {
        while (i > 0 && (s1 = seg[size_t(--i)]) >= v1) {
        }
        if (i == 0 && v1 < s1) s1 = 0;
        else i++;
        s2 = seg[size_t(i)];
        si = i;
    }
    real t = (v == 1) ? real(1) : ((real(i) + (v1 - s1) / (s2 - s1)) * prec);
    if (cr::risnan(t)) t = 0;
    real inv = 1 - t;
    (void)curIndex;
    for (int k = int(props.size()) - 1; k >= 0; k--) {
        char p = props[size_t(k)];
        const Segment &b = beziers[p];
        real val = (t * t * b.da + 3 * inv * (t * b.ca + inv * b.ba)) * t + b.a;
        *component(target, p) = val;
    }
}

bool Bezier::kill(const KillMap &lookup)
{
    // BezierPlugin._kill: properties present in the lookup stop being written
    for (auto it = beziers.begin(); it != beziers.end();) {
        if (lookup.count(it->first)) {
            char p = it->first;
            props.erase(std::remove(props.begin(), props.end(), p), props.end());
            it = beziers.erase(it);
        } else {
            ++it;
        }
    }
    // TweenPlugin._kill: overwriteProps with a non-null lookup value are removed
    for (int i = int(overwriteProps.size()) - 1; i >= 0; i--) {
        auto f = lookup.find(overwriteProps[size_t(i)]);
        if (f != lookup.end() && f->second) overwriteProps.erase(overwriteProps.begin() + i);
    }
    return false;
}

// ------------------------------------------------------------------ Tween (TweenLite + TweenMax)

real Tween::totalDuration()
{
    if (_dirty) {
        _totalDuration = (_repeat == -1) ? kRealHuge : _duration * (_repeat + 1) + (_repeatDelay * _repeat);
        _dirty = false;
    }
    return _totalDuration;
}

bool Tween::enabled(bool enable, bool ignoreTimeline)
{
    if (enable && _gc) engine->registerTween(std::static_pointer_cast<Tween>(shared_from_this()), true);
    Animation::enabled(enable, ignoreTimeline);
    return false;
}

void Tween::unlink(PropTween *pt)
{
    if (pt->prev) pt->prev->next = pt->next;
    else if (pt == _firstPT) _firstPT = pt->next;
    if (pt->next) pt->next->prev = pt->prev;
    pt->next = pt->prev = nullptr;
}

bool Tween::kill(const KillMap *vars, KillSource source, Tween *overwriting)
{
    // TweenLite._kill
    if (!vars) {
        lazyPending = false;
        return enabled(false, false);
    }
    bool simultaneous = overwriting && _time != 0 && overwriting->_startTime == _startTime &&
                        _timeline == overwriting->_timeline;
    PropTween *firstPT = _firstPT;
    bool changed = false;
    hasOverwrittenProps = true; // this._overwrittenProps = this._overwrittenProps || {}
    bool record = source == KillSource::OtherLookup && !overwrittenAll;
    for (const auto &kv : *vars) {
        char p = kv.first;
        auto it = propLookup.find(p);
        PropTween *pt = it != propLookup.end() ? it->second : nullptr;
        if (pt) {
            if (simultaneous) {
                if (pt->plugin) pt->plugin->setRatio(pt->s);
                else *pt->target = pt->s;
                changed = true;
            }
            if (pt->plugin && pt->plugin->kill(*vars)) changed = true;
            if (!pt->plugin || pt->plugin->overwriteProps.empty()) unlink(pt);
            propLookup.erase(it);
        }
        if (record) overwrittenProps.insert(p);
    }
    if (!_firstPT && _initted && firstPT) enabled(false, false);
    return changed;
}

bool Tween::initProps()
{
    // TweenLite._initProps
    if (engine->lazyLookup.count(target)) engine->lazyRender();

    if (hasBezier) {
        pluginStore.emplace_back(new Bezier());
        Bezier *b = pluginStore.back().get();
        b->init(target, bezierValues);
        ptStore.emplace_back(new PropTween());
        PropTween *pt = ptStore.back().get();
        pt->plugin = b;
        pt->name = 'b';
        pt->s = 0;
        pt->c = 1;
        pt->next = _firstPT;
        if (pt->next) pt->next->prev = pt;
        _firstPT = pt;
        for (char p : b->overwriteProps) propLookup[p] = pt;
    }
    for (const Prop &pr : props) {
        real *f = component(target, pr.name);
        real s = *f;
        real c = pr.value - s;
        if (cr::risnan(c)) c = 0;
        if (c != 0) {
            ptStore.emplace_back(new PropTween());
            PropTween *pt = ptStore.back().get();
            pt->target = f;
            pt->name = pr.name;
            pt->s = s;
            pt->c = c;
            pt->next = _firstPT;
            if (pt->next) pt->next->prev = pt;
            _firstPT = pt;
            propLookup[pr.name] = pt;
        } else {
            propLookup[pr.name] = nullptr;
        }
    }

    if (hasOverwrittenProps) {
        KillMap op;
        for (char p : overwrittenProps) op[p] = true;
        if (kill(&op, KillSource::OwnOverwritten, nullptr)) return initProps();
    }
    if (_overwrite > 1 && _firstPT && engine->lookup[target].size() > 1) {
        KillMap mine;
        for (const auto &kv : propLookup) mine[kv.first] = kv.second != nullptr;
        if (engine->applyOverwrite(this, mine)) {
            kill(&mine, KillSource::OwnLookup, nullptr);
            return initProps();
        }
    }
    if (_firstPT && _duration != 0) engine->lazyLookup.insert(target);
    return false;
}

void Tween::init()
{
    // TweenLite._init
    bool op = hasOverwrittenProps;
    _ease = vars.hasEase ? vars.ease : Power1Out();
    _firstPT = nullptr;
    initProps();
    if (op && !_firstPT) enabled(false, false);
    _initted = true;
}

void Tween::render(real time, bool suppressEvents, bool force)
{
    // TweenMax.render
    real totalDur = (!_dirty) ? _totalDuration : totalDuration();
    real prevTime = _time, prevTotalTime = _totalTime, duration = _duration, prevRawPrevTime = _rawPrevTime;
    int prevCycle = _cycle;
    bool isComplete = false;
    enum { None, Complete, ReverseComplete } callback = None;
    // before _init() the prototype ease (TweenLite.defaultEase) is in effect
    const Ease ease = _initted ? _ease : Power1Out();

    if (time >= totalDur - kTiny && time >= 0) {
        _totalTime = totalDur;
        _cycle = _repeat;
        if (_yoyo && (_cycle & 1) != 0) {
            _time = 0;
            ratio = 0;
        } else {
            _time = duration;
            ratio = 1;
        }
        if (!_reversed) {
            isComplete = true;
            callback = Complete;
            force = force || _timeline->autoRemoveChildren;
        }
    } else if (time < kTiny) {
        _totalTime = _time = 0;
        _cycle = 0;
        ratio = 0;
        if (prevTotalTime != 0 || (duration == 0 && prevRawPrevTime > 0)) {
            callback = ReverseComplete;
            isComplete = _reversed;
        }
        if (time > -kTiny) time = 0;
        else if (time < 0) _active = false;
        if (!_initted) force = true;
    } else {
        _totalTime = _time = time;
        if (_repeat != 0) {
            real cycleDuration = duration + _repeatDelay;
            const real cycles = _totalTime / cycleDuration; // one division for both uses
            _cycle = int(cycles);
            if (_cycle != 0 && real(_cycle) == cycles && prevTotalTime <= time) _cycle--;
            _time = _totalTime - (_cycle * cycleDuration);
            if (_yoyo && (_cycle & 1) != 0) _time = duration - _time;
            if (_time > duration) _time = duration;
            else if (_time < 0) _time = 0;
        }
        ratio = ease.getRatio(_time / duration);
    }

    if (prevTime == _time && !force && prevCycle == _cycle) return;
    if (!_initted) {
        init();
        if (!_initted || _gc) return;
        if (!force && _firstPT && _duration != 0) {
            _time = prevTime;
            _totalTime = prevTotalTime;
            _rawPrevTime = prevRawPrevTime;
            _cycle = prevCycle;
            engine->lazyTweens.push_back(std::static_pointer_cast<Tween>(shared_from_this()));
            lazyPending = true;
            lazyTime = time;
            lazySuppress = suppressEvents;
            return;
        }
        if (_time != 0 && !isComplete) ratio = _ease.getRatio(_time / duration);
    }
    if (lazyPending) lazyPending = false;

    if (!_active)
        if (!_paused && _time != prevTime && time >= 0) _active = true;

    for (PropTween *pt = _firstPT; pt; pt = pt->next) {
        real v = pt->c * ratio + pt->s;
        if (pt->plugin) pt->plugin->setRatio(v);
        else *pt->target = v;
    }

    if (callback != None)
        if (!_gc || force) {
            if (isComplete) {
                if (_timeline->autoRemoveChildren) enabled(false, false);
                _active = false;
            }
            if (!suppressEvents && callback == Complete && vars.onComplete) {
                auto fn = vars.onComplete; // the callback may kill this tween
                fn();
            }
        }
}

// ------------------------------------------------------------------ Engine (ticker, root, statics)

Engine::Engine()
{
    root.engine = this;
    root.isRoot = true;
    root.autoRemoveChildren = root.smoothChildTiming = true;
    root._active = true;
#ifdef CR_FIXED
    // wake() -> _tick(2): one frame, zero elapsed time
    time_ = 0;
    frame_ = 1;
    root._startTime = time_;
#else
    // _lastUpdate = _getTime() at module load, then new Ticker(): _startTime = _getTime(), fps(undefined)
    lastUpdate_ = epoch_ + now_;
    startTime_ = epoch_ + now_;
    time_ = 0;
    frame_ = 0;
    gap_ = 1.0 / 60;
    nextTime_ = time_ + gap_;
    // wake() -> _tick(2); the "tick" listener (_updateRoot) is not registered yet
    double elapsed = (epoch_ + now_) - lastUpdate_;
    if (elapsed > 500) startTime_ += elapsed - 33;
    lastUpdate_ += elapsed;
    time_ = (lastUpdate_ - startTime_) / 1000;
    double overlap = time_ - nextTime_;
    frame_++;
    nextTime_ += overlap + (overlap >= gap_ ? 0.004 : gap_ - overlap);
    root._startTime = time_;
#endif
}

void Engine::tick()
{
#ifdef CR_FIXED
    // Ticker._tick with a fixed 1000/60 ms per frame (never above its 500 ms lag threshold): time = ticks / 60 s,
    // computed from the count so rounding never accumulates (65536 / 60 = 1092 + 16/60)
    ticks_++;
    time_ = real::fromRaw(int32_t(ticks_ * 1092u + (ticks_ * 16u + 30u) / 60u));
    frame_++;
#else
    now_ += 1000.0 / 60;
    // Ticker._tick
    double elapsed = (epoch_ + now_) - lastUpdate_;
    if (elapsed > 500) startTime_ += elapsed - 33;
    lastUpdate_ += elapsed;
    time_ = (lastUpdate_ - startTime_) / 1000;
    double overlap = time_ - nextTime_;
    frame_++;
    nextTime_ += overlap + (overlap >= gap_ ? 0.004 : gap_ - overlap);
#endif
    updateRoot();
}

void Engine::updateRoot()
{
    // Animation._updateRoot
    if (!lazyTweens.empty()) lazyRender();
    root.render((time_ - root._startTime) * root._timeScale, false, false);
    if (!lazyTweens.empty()) lazyRender();
    if (frame_ >= nextGCFrame_) {
        nextGCFrame_ = frame_ + 120;
        for (auto it = lookup.begin(); it != lookup.end();) {
            auto &a = it->second;
            a.erase(std::remove_if(a.begin(), a.end(), [](const std::shared_ptr<Tween> &t) { return t->_gc; }), a.end());
            if (a.empty()) it = lookup.erase(it);
            else ++it;
        }
    }
    graveyard.clear();
}

void Engine::lazyRender()
{
    // _lazyRender: tweens queued while flushing are dropped by `_lazyTweens.length = 0`
    size_t l = lazyTweens.size();
    lazyLookup.clear();
    for (size_t i = 0; i < l; i++) {
        std::shared_ptr<Tween> tween = lazyTweens[i];
        if (tween && tween->lazyPending) {
            tween->render(tween->lazyTime, tween->lazySuppress, true);
            tween->lazyPending = false;
        }
    }
    lazyTweens.clear();
}

void Engine::registerTween(const std::shared_ptr<Tween> &t, bool scrub)
{
    auto &a = lookup[t->target];
    if (scrub) a.erase(std::remove(a.begin(), a.end(), t), a.end());
    a.push_back(t);
}

real Engine::checkOverlap(Animation *tween, real reference, bool zeroDur)
{
    // _checkOverlap
    Timeline *tl = tween->_timeline;
    real ts = tl->_timeScale;
    real t = tween->_startTime;
    while (tl->_timeline) {
        t += tl->_startTime;
        ts *= tl->_timeScale;
        if (tl->_paused) return -100;
        tl = tl->_timeline;
    }
    t /= ts;
    if (t > reference) return t - reference;
    if ((zeroDur && t == reference) || (!tween->_initted && t - reference < 2 * kTiny)) return kTiny;
    t += tween->totalDuration() / tween->_timeScale / ts;
    return (t > reference + kTiny) ? real(0) : t - reference - kTiny;
}

bool Engine::applyOverwrite(Tween *tween, const KillMap &props)
{
    // _applyOverwrite, mode 2 ("auto")
    std::vector<std::shared_ptr<Tween>> siblings = lookup[tween->target];
    real startTime = tween->_startTime + kTiny;
    std::vector<Tween *> overlaps;
    bool zeroDur = tween->_duration == 0;
    real globalStart = 0;
    for (size_t i = siblings.size(); i-- > 0;) {
        Tween *cur = siblings[i].get();
        if (cur == tween || cur->_gc || cur->_paused) continue;
        if (cur->_timeline != tween->_timeline) {
            if (globalStart == 0) globalStart = checkOverlap(tween, 0, zeroDur);
            if (checkOverlap(cur, globalStart, zeroDur) == 0) overlaps.push_back(cur);
        } else if (cur->_startTime <= startTime) {
            if (cur->_startTime + cur->totalDuration() / cur->_timeScale > startTime)
                if (!((zeroDur || !cur->_initted) && startTime - cur->_startTime <= kTiny * 2)) overlaps.push_back(cur);
        }
    }
    bool changed = false;
    for (size_t i = overlaps.size(); i-- > 0;) {
        Tween *cur = overlaps[i];
        PropTween *l = cur->_firstPT;
        if (cur->kill(&props, Tween::KillSource::OtherLookup, tween)) changed = true;
        if (!cur->_firstPT && cur->_initted && l)
            if (cur->enabled(false, false)) changed = true;
    }
    return changed;
}

std::shared_ptr<Tween> Engine::make(Vec3 *target, real dur, const Vars &v)
{
    // Animation constructor
    auto t = std::make_shared<Tween>();
    t->engine = this;
    t->vars = v;
    t->_duration = t->_totalDuration = dur;
    t->_delay = v.delay;
    t->_timeScale = 1;
    t->_active = false;
    t->_reversed = false;
    root.add(t, root._time);
    // TweenLite constructor
    t->target = target;
    t->_overwrite = 2;
    registerTween(t, false);
    // TweenMax constructor
    t->_cycle = 0;
    t->_yoyo = v.yoyo;
    t->_repeat = v.repeat;
    t->_repeatDelay = 0;
    if (t->_repeat) t->uncache(true);
    return t;
}

std::shared_ptr<Tween> Engine::to(Vec3 *target, std::initializer_list<Prop> props, real dur, const Vars &v)
{
    std::shared_ptr<Tween> t = make(target, dur, v);
    t->props.assign(props.begin(), props.end());
    return t;
}

std::shared_ptr<Tween> Engine::bezierTo(Vec3 *target, const Vec3 values[4], real dur, const Vars &v)
{
    std::shared_ptr<Tween> t = make(target, dur, v);
    t->hasBezier = true;
    for (int i = 0; i < 4; i++) t->bezierValues[i] = values[i];
    return t;
}

std::shared_ptr<Timeline> Engine::timeline(const Vars &v)
{
    auto tl = std::make_shared<Timeline>();
    tl->engine = this;
    tl->vars = v;
    tl->_duration = tl->_totalDuration = 0;
    tl->_delay = v.delay;
    root.add(tl, root._time);
    // TimelineLite constructor
    tl->autoRemoveChildren = false;
    tl->smoothChildTiming = false;
    tl->_sortChildren = true;
    // TimelineMax constructor
    tl->_repeat = v.repeat;
    tl->_repeatDelay = 0;
    tl->_cycle = 0;
    tl->_yoyo = v.yoyo;
    tl->_dirty = true;
    return tl;
}

void Engine::killTweensOf(Vec3 *target)
{
    auto it = lookup.find(target);
    if (it == lookup.end()) return;
    std::vector<std::shared_ptr<Tween>> a = it->second;
    for (auto &t : a)
        if (!t->_gc) t->kill(nullptr, Tween::KillSource::OtherLookup, nullptr);
}

void Engine::clear()
{
    for (auto &kv : root.owned) {
        kv.second->_gc = true;
        graveyard.push_back(kv.second);
    }
    root.owned.clear();
    root._first = root._last = root._recent = nullptr;
    lazyTweens.clear();
    lazyLookup.clear();
    lookup.clear();
}

#ifdef CR_FIXED
// raw 16.16 values: no double at run time on the SF2000
std::string Engine::describe()
{
    std::string out;
    char buf[256];
    snprintf(buf, sizeof buf, "root time %d/65536\n", int(root._time.v));
    out += buf;
    for (Animation *a = root._first; a; a = a->_next) {
        snprintf(buf, sizeof buf, "  %s start=%d time=%d dur=%d gc=%d paused=%d active=%d\n",
                 a->isTimeline() ? "TL" : "TW", int(a->_startTime.v), int(a->_time.v), int(a->_duration.v), a->_gc,
                 a->_paused, a->_active);
        out += buf;
    }
    return out;
}
#else
std::string Engine::describe()
{
    std::string out;
    char buf[256];
    snprintf(buf, sizeof buf, "root time %.17g\n", root._time);
    out += buf;
    for (Animation *a = root._first; a; a = a->_next) {
        snprintf(buf, sizeof buf, "  %s start=%.17g time=%.17g dur=%.4g gc=%d paused=%d active=%d\n",
                      a->isTimeline() ? "TL" : "TW", a->_startTime, a->_time, a->_duration, a->_gc, a->_paused,
                      a->_active);
        out += buf;
        if (a->isTimeline())
            for (Animation *c = static_cast<Timeline *>(a)->_first; c; c = c->_next) {
                Tween *t = static_cast<Tween *>(c);
                std::string props;
                for (PropTween *pt = t->_firstPT; pt; pt = pt->next) props += pt->name;
                snprintf(buf, sizeof buf, "    child start=%.4g time=%.17g init=%d gc=%d props=%s lazy=%d\n",
                              c->_startTime, c->_time, c->_initted, c->_gc, props.c_str(), t->lazyPending);
                out += buf;
            }
    }
    return out;
}
#endif

int Engine::liveAnimations() const
{
    int n = 0;
    for (Animation *a = root._first; a; a = a->_next) n++;
    return n;
}

} // namespace gsap
