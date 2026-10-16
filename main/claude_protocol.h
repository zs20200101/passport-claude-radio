#pragma once

#include <cstddef>
#include <cstdint>

namespace claude_protocol {

constexpr std::size_t kMaxRequestBytes = 1024;
constexpr std::size_t kMaxToolNameBytes = 32;
constexpr std::size_t kMaxSummaryBytes = 256;
constexpr uint32_t kDefaultDecisionTimeoutMs = 120000;

enum class Decision : uint8_t { Pending = 0, Allow = 1, Deny = 2 };

enum class Event : uint8_t {
    None = 0,
    RequestShown,
    AllowRequested,
    DenyRequested,
    Completed,
    Expired,
    Disconnected,
};

struct Request {
    char id[40];
    char tool[33];
    char summary[257];
    char cwd[161];
    uint32_t generation;
    uint32_t created_ms;
    uint32_t deadline_ms;
};

struct State {
    Request request;
    Decision decision;
    uint32_t generation;
    bool active;
};

void clear(State *state);
bool start(State *state, const char *id, const char *tool, const char *summary,
           const char *cwd, uint32_t now_ms, uint32_t timeout_ms);
Event key(State *state, bool up, bool down, uint32_t now_ms);
Event tick(State *state, uint32_t now_ms, bool connected);
bool complete(State *state, const char *id, Decision decision);
const char *decision_text(Decision decision);

}  // namespace claude_protocol
