#include "claude_protocol.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace claude_protocol;
int main() {
    State state{};
    assert(!start(nullptr,"a","Bash","test",nullptr,0,100));
    assert(!start(&state,"","Bash","test",nullptr,0,100));
    assert(start(&state,"a","Bash","test",nullptr,100,200));
    const auto first=state.generation;
    assert(!start(&state,"b","Bash","test",nullptr,100,200));
    assert(key(&state,true,true,101)==Event::None);
    assert(key(&state,false,false,102)==Event::None);
    assert(key(&state,true,false,103)==Event::AllowRequested);
    assert(key(&state,false,true,104)==Event::None);
    assert(!complete(&state,"wrong",Decision::Allow));
    assert(complete(&state,"a",Decision::Allow));
    assert(!complete(&state,"a",Decision::Allow));
    clear(&state);
    assert(start(&state,"b","Edit","test","cwd",200,100));
    assert(state.generation!=first);
    assert(key(&state,false,true,201)==Event::DenyRequested);
    assert(tick(&state,202,true)==Event::Completed);
    clear(&state);
    assert(start(&state,"c","Bash","test",nullptr,0xfffffff0U,32));
    assert(tick(&state,0,true)==Event::None);
    assert(key(&state,true,false,16)==Event::Expired);
    assert(state.decision==Decision::Deny);
    clear(&state);
    assert(start(&state,"d","Bash","test",nullptr,0,10));
    assert(tick(&state,1,false)==Event::Disconnected);
    assert(state.decision==Decision::Deny && !state.active);
    clear(&state);
    state.generation=0xffffffffU;
    assert(start(&state,"e","Bash","test",nullptr,0,10));
    assert(state.generation==1);
    assert(tick(&state,10,true)==Event::Expired);
    assert(!state.active && state.decision==Decision::Deny);
    std::cout<<"Claude permission state tests: PASS\n";
}
