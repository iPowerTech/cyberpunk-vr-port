#include "Render/NativeUploadReservation.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace upload=cvr::stereo::upload_reservation;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static upload::ExternalInstallPlan Plan(){
    upload::ExternalInstallPlan result{};
    result.version=upload::ExternalPlanVersion;result.nonce=1;result.processId=123;result.processCreationTime=456;
    result.gameBase=0x140000000ull;result.allocatorAddress=result.gameBase+upload::AllocatorRva;result.siteAddress=result.gameBase+upload::WitnessRva;
    result.relayAddress=0x140880000ull;result.modulePinAddress=0x180000000ull;result.thunkAddress=0x180004000ull;
    result.allocatorBytes=177;result.siteBytes=7;result.relayBytes=14;result.thunkBytes=1024;result.originalProtection=0x20;
    result.guardFnv=upload::AllocatorFnv;result.original=upload::OriginalWitnessBytes;
    constexpr std::string_view hex=
        "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 40 "
        "49 8b f9 4c 8b ea 48 8b d9 4d 85 c9 74 04 41 c6 01 00 "
        "48 8b 69 08 49 8d 40 ff 48 89 84 24 80 00 00 00 4c 8b e0 "
        "49 f7 d4 4c 8b 0b 4a 8d 0c 08 49 23 cc 4c 8b 43 10 "
        "33 d2 48 8b c1 49 f7 f0 4a 8d 04 2a 49 3b c0 77 4b "
        "4a 8d 14 29 48 8b c2 48 2b c5 49 3b c0 0f 87 c4 17 78 01 "
        "49 8b c1 f0 48 0f b1 13 75 24 33 d2 48 8b c1 48 f7 73 10 "
        "48 8b c2 48 8b 9c 24 90 00 00 00 48 83 c4 40 41 5f 41 5e "
        "41 5d 41 5c 5f 5e 5d c3 48 8b 84 24 80 00 00 00 eb 96 "
        "49 8b c0 48 2b c2 48 03 c8 eb aa";
    const auto nibble=[](char c)->uint8_t{if(c>='0' && c<='9')return uint8_t(c-'0');if(c>='a' && c<='f')return uint8_t(c-'a'+10);throw std::runtime_error("Bad fixture hex");};
    Require(hex.size()==result.guard.size()*3-1,"Original body fixture extent differs");
    for(size_t i=0;i<result.guard.size();++i)result.guard[i]=uint8_t((nibble(hex[i*3])<<4)|nibble(hex[i*3+1]));
    const auto displacement=int32_t(result.relayAddress-(result.siteAddress+5));
    result.patch={0xE8,0,0,0,0,0x75,0x1F};std::memcpy(result.patch.data()+1,&displacement,4);
    result.relayCode={0xFF,0x25};std::memcpy(result.relayCode.data()+6,&result.thunkAddress,8);
    result.checksum=upload::ExternalPlanChecksum(result);return result;
}
static upload::ExternalInstallReceipt Receipt(const upload::ExternalInstallPlan& plan){
    upload::ExternalInstallReceipt result{};result.version=upload::ExternalReceiptVersion;result.nonce=plan.nonce;result.planChecksum=plan.checksum;
    result.processId=plan.processId;result.processCreationTime=plan.processCreationTime;result.outcome=uint64_t(upload::ExternalInstallOutcome::CommittedDetached);
    result.debugEventCode=1;result.debugEventThread=789;result.originalProtection=result.finalProtection=plan.originalProtection;
    result.bytesWritten=7;result.patchVerified=result.cacheFlushed=result.detachSucceeded=1;result.exceptionCode=0x80000003;result.firstChance=1;
    result.checksum=upload::ExternalReceiptChecksum(result);return result;
}
int main()try{
    const auto plan=Plan();Require(upload::ValidExternalPlan(plan),"Exact prepared CALL/JNE plan rejected");
    const auto receipt=Receipt(plan);Require(upload::ValidExternalReceipt(plan,receipt),"Exact detached breakpoint receipt rejected");
    Require(!upload::ValidExternalPlan({}) && !upload::ValidExternalReceipt(plan,{}),"Default ABI buffers acquired installation proof");
    auto body=plan.guard;Require(!upload::MatchesExternalPatchedBody(plan,body),"Original body was mistaken for installed patch");
    std::copy(plan.patch.begin(),plan.patch.end(),body.begin()+upload::WitnessRva-upload::AllocatorRva);
    Require(upload::MatchesExternalPatchedBody(plan,body),"Exact installed full body rejected");
    body[0]^=1;Require(!upload::MatchesExternalPatchedBody(plan,body),"Foreign prologue passed full body check");
    body=plan.guard;std::copy(plan.patch.begin(),plan.patch.end(),body.begin()+upload::WitnessRva-upload::AllocatorRva);
    body[upload::WitnessRva-upload::AllocatorRva+2]^=1;Require(!upload::MatchesExternalPatchedBody(plan,body),"Torn patch passed full body check");
    for(const auto field:{&upload::ExternalInstallPlan::gameBase,&upload::ExternalInstallPlan::allocatorAddress,&upload::ExternalInstallPlan::siteAddress,
        &upload::ExternalInstallPlan::relayAddress,&upload::ExternalInstallPlan::thunkAddress,&upload::ExternalInstallPlan::allocatorBytes,
        &upload::ExternalInstallPlan::siteBytes,&upload::ExternalInstallPlan::relayBytes,&upload::ExternalInstallPlan::thunkBytes,&upload::ExternalInstallPlan::originalProtection}){
        auto bad=plan;bad.*field=0;bad.checksum=upload::ExternalPlanChecksum(bad);Require(!upload::ValidExternalPlan(bad),"Invalid prepared address/extent admitted");}
    auto bad=plan;bad.guard[0]^=1;bad.checksum=upload::ExternalPlanChecksum(bad);Require(!upload::ValidExternalPlan(bad),"Foreign177 guard admitted");
    bad=plan;bad.patch[6]^=1;bad.checksum=upload::ExternalPlanChecksum(bad);Require(!upload::ValidExternalPlan(bad),"Wrong native retry target admitted");
    bad=plan;bad.relayCode[6]^=1;bad.checksum=upload::ExternalPlanChecksum(bad);Require(!upload::ValidExternalPlan(bad),"Foreign thunk target admitted");
    bad=plan;bad.reserved[0]=1;bad.checksum=upload::ExternalPlanChecksum(bad);Require(!upload::ValidExternalPlan(bad),"Unknown prepared metadata admitted");
    bad=plan;bad.reservedBytes[0]=1;bad.checksum=upload::ExternalPlanChecksum(bad);Require(!upload::ValidExternalPlan(bad),"Unknown prepared byte metadata admitted");
    for(const auto field:{&upload::ExternalInstallReceipt::version,&upload::ExternalInstallReceipt::nonce,&upload::ExternalInstallReceipt::planChecksum,
        &upload::ExternalInstallReceipt::processId,&upload::ExternalInstallReceipt::processCreationTime,&upload::ExternalInstallReceipt::debugEventThread,
        &upload::ExternalInstallReceipt::bytesWritten,&upload::ExternalInstallReceipt::patchVerified,&upload::ExternalInstallReceipt::cacheFlushed,
        &upload::ExternalInstallReceipt::detachSucceeded,&upload::ExternalInstallReceipt::exceptionCode,&upload::ExternalInstallReceipt::firstChance}){
        auto rejected=receipt;rejected.*field=0;rejected.checksum=upload::ExternalReceiptChecksum(rejected);
        Require(!upload::ValidExternalReceipt(plan,rejected),"Missing detached receipt proof admitted");}
    for(const auto outcome:{upload::ExternalInstallOutcome::CommittedHeldEvent,upload::ExternalInstallOutcome::RolledBackDetached,upload::ExternalInstallOutcome::FailedNoWriteDetached}){
        auto rejected=receipt;rejected.outcome=uint64_t(outcome);rejected.checksum=upload::ExternalReceiptChecksum(rejected);
        Require(!upload::ValidExternalReceipt(plan,rejected),"Noncommitted/detached outcome admitted");}
    auto rejected=receipt;rejected.debugEventCode=3;rejected.checksum=upload::ExternalReceiptChecksum(rejected);
    Require(!upload::ValidExternalReceipt(plan,rejected),"CREATE_PROCESS event was mistaken for attach breakpoint");
    rejected=receipt;rejected.win32Error=5;rejected.checksum=upload::ExternalReceiptChecksum(rejected);
    Require(!upload::ValidExternalReceipt(plan,rejected),"Failed external transaction admitted");
    rejected=receipt;rejected.checksum^=1;Require(!upload::ValidExternalReceipt(plan,rejected),"Torn external receipt admitted");
    std::puts("upload_install_prepare_test: OK");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"upload_install_prepare_test: %s\n",error.what());return 1;}
