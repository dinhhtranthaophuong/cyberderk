#pragma once
#include <cstdint>
namespace KeypadMatrix { void init(); uint32_t poll(uint32_t nowMs); bool isIdle(); }
