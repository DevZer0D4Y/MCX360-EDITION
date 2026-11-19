// Title Update 9 core: host replacements for guest functions.
//
// These are compiled into the same library as the recompiled code. They have
// to be: the generated functions are hidden weak symbols, and a strong
// definition only replaces them inside the same binary. The addresses are
// TU9's; every other title update moves these functions, so each core carries
// its own copy of this file (see cores/README.md).

#include <chrono>
#include <thread>

#include "mc360_init.h"

// The Xbox 360 SDK's PIX named-event call is compiled into the retail game. It
// formats the event text on every call, then drops it unless a PIX capture has
// registered a callback, which never happens here. The chunk builder calls it
// constantly (~10% of that thread), so skip the formatting when nothing would
// receive it.
extern "C" REX_FUNC(sub_8240FF90) {
  const uint32_t pix_callbacks = REX_LOAD_U32(0x82000A04);
  if (!pix_callbacks || !REX_LOAD_U32(pix_callbacks)) {
    return;
  }
  __imp__sub_8240FF90(ctx, base);
}

// Direct3D's spin-wait step, which every loop that waits for the GPU (fences,
// ring buffer space) calls between checks. On the console it paused the
// hardware thread with db16cyc delays, which do nothing when recompiled, so
// the main thread spun a performance core at full power for up to half of
// every frame - heat that made the phone throttle within minutes. Sleep
// briefly before each check instead; the GPU's progress is still noticed
// within a fraction of a millisecond.
extern "C" REX_FUNC(sub_824308D8) {
  std::this_thread::sleep_for(std::chrono::microseconds(50));
  __imp__sub_824308D8(ctx, base);
}
