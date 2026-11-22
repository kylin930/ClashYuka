#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <wx/event.h>
#include <wx/timer.h>

namespace clashyuka::ui {

enum class Easing {
    Linear,
    EaseOutCubic,
    EaseInOutCubic,
    Standard,
    EmphasizedDecelerate,
    EmphasizedAccelerate,
};

double ApplyEasing(Easing easing, double t);

struct AnimationSpec {
    double from = 0.0;
    double to = 1.0;
    int durationMs = 200;
    Easing easing = Easing::EaseOutCubic;

    double ProgressAt(double elapsedMs) const;
    double ValueAt(double elapsedMs) const;
};

class AnimationManager;

class AnimationHandle {
public:
    ~AnimationHandle();
    void Cancel();
    bool IsRunning() const;

private:
    friend class AnimationManager;
    AnimationHandle(AnimationManager* manager, std::uint64_t id, bool finished);

    AnimationManager* manager_;
    std::uint64_t id_;
    bool finished_;
};

// A single wxTimer drives every animation, so components never create their own
// timers. Reduced motion / disabled animations apply the final value instantly.
class AnimationManager : public wxEvtHandler {
public:
    static AnimationManager& Instance();

    std::shared_ptr<AnimationHandle> Animate(double from, double to, int durationMs,
                                             Easing easing,
                                             std::function<void(double)> update);
    std::shared_ptr<AnimationHandle> AnimateLoop(double from, double to, int durationMs,
                                                 Easing easing,
                                                 std::function<void(double)> update);

    void Cancel(std::uint64_t id);
    bool IsActive(std::uint64_t id) const;
    std::size_t ActiveCount() const { return entries_.size(); }
    void StopAll();

private:
    AnimationManager();
    ~AnimationManager() override = default;

    struct Entry {
        std::uint64_t id = 0;
        AnimationSpec spec;
        bool loop = false;
        std::chrono::steady_clock::time_point start;
        std::function<void(double)> update;
    };

    void OnTick(wxTimerEvent& event);
    void EnsureTimer();

    std::vector<Entry> entries_;
    // Created on first use: wxTimer requires a fully initialized wxApp.
    std::unique_ptr<wxTimer> timer_;
    std::uint64_t nextId_ = 1;
    std::uint64_t generation_ = 0;
    bool inTick_ = false;
    std::vector<std::uint64_t> canceledDuringTick_;
};

}  // namespace clashyuka::ui
