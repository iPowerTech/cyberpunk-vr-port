#pragma once
#include <string>
namespace cvr::reflex {
// -1 follows the game; 0 off; 1 low latency; 2 low latency + boost.
int GetMode();
void SetMode(int mode);
int GetAppliedMode(); // -2 until a recognized options block has been submitted.
// Opt-in diagnostic: request on the script thread, collect on the renderer's
// serialized SetOptions path, read the result later. No sampling thread/waits.
std::string TimingReport(bool request);
}
