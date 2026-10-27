#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
struct Registers {std::array<uint64_t,8> gpr;std::array<uint8_t,96> xmm;};
extern "C" {
void CvrStoryAttentionDetour();
void NativeStoryReplay();
void ProbeStory(void(*)(),Registers*);
uint32_t StoryAbiError{},StoryWrites{};
}
int main() {
    Registers direct{},hooked{};
    ProbeStory(&NativeStoryReplay,&direct);
    ProbeStory(&CvrStoryAttentionDetour,&hooked);
    if(StoryAbiError || StoryWrites!=1 || std::memcmp(&direct,&hooked,sizeof(direct))) {
        std::cerr<<"FAIL callback arguments, alignment, transform, instruction replay or registers/flags\n";return 1;
    }
    std::cout<<"PASS live RBX/RBP, shadow space, transform write, displaced instructions, all volatile registers/flags\n";
}
