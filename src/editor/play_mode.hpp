#pragma once
#include "core/ecs.hpp"

// Scene ownership stays in the same World so editor/runtime bindings stay valid.
class PlayMode {
public:
    enum class State { Edit, Playing, Paused };
    State state() const { return state_; }
    bool playing() const { return state_ == State::Playing; }
    bool active() const { return state_ != State::Edit; }
    bool play(World& world) {
        if (active()) return false;
        saved_ = world.snapshot();
        state_ = State::Playing;
        return true;
    }
    void pause() {
        if (active()) state_ = playing() ? State::Paused : State::Playing;
    }
    bool stop(World& world) {
        if (!active()) return false;
        world.restore(*saved_);
        saved_.reset();
        state_ = State::Edit;
        return true;
    }
private:
    State state_ = State::Edit;
    std::unique_ptr<World> saved_;
};
