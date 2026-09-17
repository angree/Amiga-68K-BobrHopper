#include "script.h"

#include <cstdlib>
#include <sstream>

#include "engine/log.h"

namespace cr {

void InputScript::parse(const std::string &text)
{
    ops_.clear();
    pos_ = 0;
    wait_ = 0;
    pendingRelease_ = false;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        Op op{Op::Wait};
        if (tok.compare(0, 5, "shot:") == 0) {
            op.kind = Op::Shot;
            op.name = tok.substr(5);
        } else if (tok[0] == 'w') {
            op.kind = Op::Wait;
            op.value = std::atoi(tok.c_str() + 1);
        } else if (tok == "s") {
            op.kind = Op::Start;
        } else if (tok == "a") {
            op.kind = Op::Restart;
        } else if (tok == "u" || tok == "d" || tok == "l" || tok == "r") {
            op.kind = Op::Hop;
            op.dir = tok == "u" ? Swipe::Up : tok == "d" ? Swipe::Down : tok == "l" ? Swipe::Left : Swipe::Right;
        } else {
            logf("script: unknown token %s", tok.c_str());
            continue;
        }
        ops_.push_back(op);
    }
}

std::vector<std::string> InputScript::apply(Game &game)
{
    std::vector<std::string> shots;
    if (pendingRelease_) {
        game.moveWithDirection(pendingDir_);
        pendingRelease_ = false;
        return shots;
    }
    // shot requests do not consume a frame
    while (pos_ < ops_.size() && wait_ == 0 && ops_[pos_].kind == Op::Shot) shots.push_back(ops_[pos_++].name);
    if (pos_ >= ops_.size()) return shots;
    if (wait_ > 0) {
        wait_--;
        return shots;
    }
    const Op &op = ops_[pos_++];
    switch (op.kind) {
    case Op::Wait: wait_ = op.value - 1; break;
    case Op::Start: game.startPlaying(); break;
    case Op::Hop:
        game.beginMoveWithDirection();
        pendingRelease_ = true;
        pendingDir_ = op.dir;
        break;
    case Op::Restart:
        if (game.state() == GameState::GameOver) game.restart();
        break;
    case Op::Shot: break;
    }
    return shots;
}

} // namespace cr
