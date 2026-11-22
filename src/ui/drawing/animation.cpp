#include "clashyuka/ui/drawing/animation.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <wx/app.h>

#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {
namespace {

double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double BezierComponent(double t, double p1, double p2) {
    const double u = 1.0 - t;
    return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t;
}

double BezierDerivative(double t, double p1, double p2) {
    const double u = 1.0 - t;
    return 3.0 * u * u * p1 + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (1.0 - p2);
}

// Solves x(t) = progress for a cubic bezier and returns y(t).
double CubicBezier(double progress, double x1, double y1, double x2, double y2) {
    const double x = Clamp01(progress);
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }

    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double currentX = BezierComponent(t, x1, x2) - x;
        if (std::fabs(currentX) < 1e-6) {
            return BezierComponent(t, y1, y2);
        }
        const double derivative = BezierDerivative(t, x1, x2);
        if (std::fabs(derivative) < 1e-6) {
            break;
        }
        t = std::clamp(t - currentX / derivative, 0.0, 1.0);
    }

    double low = 0.0;
    double high = 1.0;
    t = x;
    for (int i = 0; i < 20; ++i) {
        const double currentX = BezierComponent(t, x1, x2);
        if (std::fabs(currentX - x) < 1e-6) {
            break;
        }
        if (currentX < x) {
            low = t;
        } else {
            high = t;
        }
        t = (low + high) / 2.0;
    }
    return BezierComponent(t, y1, y2);
}

}  // namespace

double ApplyEasing(Easing easing, double t) {
    const double value = Clamp01(t);
    switch (easing) {
        case Easing::Linear:
            return value;
        case Easing::EaseOutCubic: {
            const double inverted = 1.0 - value;
            return 1.0 - inverted * inverted * inverted;
        }
        case Easing::EaseInOutCubic:
            if (value < 0.5) {
                return 4.0 * value * value * value;
            }
            return 1.0 - std::pow(-2.0 * value + 2.0, 3.0) / 2.0;
        case Easing::Standard:
            return CubicBezier(value, 0.2, 0.0, 0.0, 1.0);
        case Easing::EmphasizedDecelerate:
            return CubicBezier(value, 0.05, 0.7, 0.1, 1.0);
        case Easing::EmphasizedAccelerate:
            return CubicBezier(value, 0.3, 0.0, 0.8, 0.15);
    }
    return value;
}

double AnimationSpec::ProgressAt(double elapsedMs) const {
    if (durationMs <= 0) {
        return 1.0;
    }
    return Clamp01(elapsedMs / static_cast<double>(durationMs));
}

double AnimationSpec::ValueAt(double elapsedMs) const {
    const double progress = ApplyEasing(easing, ProgressAt(elapsedMs));
    return from + (to - from) * progress;
}

AnimationHandle::AnimationHandle(AnimationManager* manager, std::uint64_t id, bool finished)
    : manager_(manager), id_(id), finished_(finished) {}

AnimationHandle::~AnimationHandle() {
    Cancel();
}

void AnimationHandle::Cancel() {
    if (finished_) {
        return;
    }
    finished_ = true;
    if (manager_ != nullptr) {
        manager_->Cancel(id_);
    }
}

bool AnimationHandle::IsRunning() const {
    return !finished_ && manager_ != nullptr && manager_->IsActive(id_);
}

AnimationManager& AnimationManager::Instance() {
    static AnimationManager manager;
    return manager;
}

AnimationManager::AnimationManager() {
    Bind(wxEVT_TIMER, &AnimationManager::OnTick, this);
}

std::shared_ptr<AnimationHandle> AnimationManager::Animate(
    double from, double to, int durationMs, Easing easing,
    std::function<void(double)> update) {
    if (!ThemeManager::Instance().MotionEnabled() || !update) {
        if (update) {
            update(to);
        }
        return std::shared_ptr<AnimationHandle>(new AnimationHandle(this, 0, true));
    }

    Entry entry;
    entry.id = nextId_++;
    entry.spec = AnimationSpec{from, to, std::max(1, durationMs), easing};
    entry.loop = false;
    entry.start = std::chrono::steady_clock::now();
    entry.update = std::move(update);
    entries_.push_back(std::move(entry));
    EnsureTimer();

    return std::shared_ptr<AnimationHandle>(new AnimationHandle(this, entries_.back().id, false));
}

std::shared_ptr<AnimationHandle> AnimationManager::AnimateLoop(
    double from, double to, int durationMs, Easing easing,
    std::function<void(double)> update) {
    if (!ThemeManager::Instance().MotionEnabled() || !update) {
        if (update) {
            update(from);
        }
        return std::shared_ptr<AnimationHandle>(new AnimationHandle(this, 0, true));
    }

    Entry entry;
    entry.id = nextId_++;
    entry.spec = AnimationSpec{from, to, std::max(1, durationMs), easing};
    entry.loop = true;
    entry.start = std::chrono::steady_clock::now();
    entry.update = std::move(update);
    entries_.push_back(std::move(entry));
    EnsureTimer();

    return std::shared_ptr<AnimationHandle>(new AnimationHandle(this, entries_.back().id, false));
}

void AnimationManager::Cancel(std::uint64_t id) {
    if (inTick_) {
        canceledDuringTick_.push_back(id);
        return;
    }
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [id](const Entry& entry) { return entry.id == id; }),
                   entries_.end());
    if (entries_.empty() && timer_) {
        timer_->Stop();
    }
}

bool AnimationManager::IsActive(std::uint64_t id) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [id](const Entry& entry) { return entry.id == id; });
}

void AnimationManager::StopAll() {
    entries_.clear();
    if (timer_) {
        timer_->Stop();
    }
    if (inTick_) {
        // Invalidate the snapshot currently being processed.
        ++generation_;
    }
}

void AnimationManager::EnsureTimer() {
    // Test binaries and early-startup code may run without a wxApp; entries are
    // still tracked so they can be inspected and cancelled.
    if (wxTheApp == nullptr) {
        return;
    }
    if (!timer_) {
        timer_ = std::make_unique<wxTimer>();
        timer_->SetOwner(this);
    }
    if (!timer_->IsRunning()) {
        timer_->Start(16);
    }
}

void AnimationManager::OnTick(wxTimerEvent&) {
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t generation = generation_;

    // Move entries out first: callbacks may cancel, create or stop animations.
    std::vector<Entry> snapshot = std::move(entries_);
    entries_.clear();
    canceledDuringTick_.clear();
    inTick_ = true;

    std::vector<Entry> survivors;
    survivors.reserve(snapshot.size());

    for (auto& entry : snapshot) {
        if (generation != generation_) {
            break;
        }
        if (std::find(canceledDuringTick_.begin(), canceledDuringTick_.end(), entry.id) !=
            canceledDuringTick_.end()) {
            continue;
        }

        const double elapsed = std::chrono::duration<double, std::milli>(now - entry.start).count();
        try {
            if (entry.loop) {
                const double duration = static_cast<double>(entry.spec.durationMs);
                entry.update(entry.spec.ValueAt(std::fmod(elapsed, duration)));
            } else {
                entry.update(entry.spec.ValueAt(elapsed));
            }
        } catch (...) {
            // A drawing callback must never take down the animation clock.
            continue;
        }

        if (entry.loop || elapsed < entry.spec.durationMs) {
            survivors.push_back(std::move(entry));
        }
    }

    inTick_ = false;
    if (generation != generation_) {
        canceledDuringTick_.clear();
        return;
    }

    // Entries created during the callbacks stay; removed ones are filtered.
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [this](const Entry& entry) {
                                      return std::find(canceledDuringTick_.begin(),
                                                       canceledDuringTick_.end(),
                                                       entry.id) != canceledDuringTick_.end();
                                  }),
                   entries_.end());
    entries_.insert(entries_.end(), std::make_move_iterator(survivors.begin()),
                    std::make_move_iterator(survivors.end()));
    canceledDuringTick_.clear();

    if (entries_.empty() && timer_) {
        timer_->Stop();
    }
}

}  // namespace clashyuka::ui
