#include "claude_protocol.h"

#include <cstring>

namespace claude_protocol {
namespace {

template <std::size_t N>
void copy_text(char (&destination)[N], const char *source) {
    if (!source) source = "";
    std::strncpy(destination, source, N - 1);
    destination[N - 1] = '\0';
}

bool reached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

void clear(State *state) {
    if (!state) return;
    const uint32_t generation = state->generation;
    std::memset(state, 0, sizeof(*state));
    state->generation = generation;
    state->decision = Decision::Pending;
}

bool start(State *state, const char *id, const char *tool, const char *summary,
           const char *cwd, uint32_t now_ms, uint32_t timeout_ms) {
    if (!state || !id || !id[0] || state->active) return false;
    clear(state);
    copy_text(state->request.id, id);
    copy_text(state->request.tool, tool);
    copy_text(state->request.summary, summary);
    copy_text(state->request.cwd, cwd);
    if (++state->generation == 0) ++state->generation;
    state->request.generation = state->generation;
    state->request.created_ms = now_ms;
    state->request.deadline_ms = now_ms + timeout_ms;
    state->active = true;
    state->decision = Decision::Pending;
    return true;
}

Event key(State *state, bool up, bool down, uint32_t now_ms) {
    if (!state || !state->active || state->decision != Decision::Pending) {
        return Event::None;
    }
    if (reached(now_ms, state->request.deadline_ms)) {
        state->decision = Decision::Deny;
        return Event::Expired;
    }
    if (up == down) return Event::None;
    state->decision = up ? Decision::Allow : Decision::Deny;
    return up ? Event::AllowRequested : Event::DenyRequested;
}

Event tick(State *state, uint32_t now_ms, bool connected) {
    if (!state || !state->active) return Event::None;
    if (!connected) {
        state->decision = Decision::Deny;
        state->active = false;
        return Event::Disconnected;
    }
    if (state->decision == Decision::Pending &&
        reached(now_ms, state->request.deadline_ms)) {
        state->decision = Decision::Deny;
        state->active = false;
        return Event::Expired;
    }
    if (state->decision != Decision::Pending) {
        state->active = false;
        return Event::Completed;
    }
    return Event::None;
}

bool complete(State *state, const char *id, Decision decision) {
    if (!state || !state->active || !id ||
        std::strncmp(id, state->request.id, sizeof(state->request.id)) != 0 ||
        decision == Decision::Pending) {
        return false;
    }
    state->decision = decision;
    state->active = false;
    return true;
}

const char *decision_text(Decision decision) {
    switch (decision) {
        case Decision::Allow: return "allow";
        case Decision::Deny: return "deny";
        default: return "pending";
    }
}

}  // namespace claude_protocol
