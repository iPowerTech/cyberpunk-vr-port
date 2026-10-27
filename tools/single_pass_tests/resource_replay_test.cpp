#include "Render/NativeResourceReplay.hpp"
#include <cstdio>
#include <stdexcept>
#include <type_traits>

namespace replay=cvr::stereo::resource_replay;
namespace {
constexpr replay::Identity Identity{101,3,4,0x10000,0x20000,0x30000};
constexpr replay::NodeBinding Node{77,0,219,1};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
replay::RawDeclaration Row(uint32_t kind,uint32_t slot=Node.slot){
    replay::RawDeclaration row{};
    for(size_t i=0;i<row.size();++i)row[i]=uint8_t(i+0x30);
    row[0]=uint8_t(kind);row[1]=uint8_t(slot);row[4]=0x12;row[5]=0x34;row[6]=0x56;row[7]=0x78;return row;
}
struct Fixture {
    std::vector<replay::NodeBinding> nodes{Node};
    std::array<uint32_t,replay::Slots> counts{};
    std::vector<replay::RawDeclaration> rows;
    explicit Fixture(uint32_t kind=1){counts[Node.slot]=1;rows.push_back(Row(kind));}
    bool Capture(replay::Receipt& receipt,replay::Kind13Policy policy=replay::Kind13Policy::RecordRequestDifference)const{
        return receipt.Capture(Identity,Identity,nodes,counts,counts,rows,rows,policy);
    }
};
bool Before(replay::Receipt& receipt,const replay::RawDeclaration& command,replay::Token& token,uint32_t cursor=0){
    return receipt.Before(Identity,Node,2,cursor,command,token);
}
void AllKindsAndSemanticMasks(){
    constexpr uint32_t expected[]{0,40,16,0,4,0,0,4,4,1,0,4,4,1};
    for(uint32_t kind=0;kind<14;++kind){
        Require(replay::PayloadBytes(kind)==expected[kind] && replay::CommandBytes(kind)==8+expected[kind],"Wrong native copy grammar");
        Fixture fixture(kind);const auto captured=fixture.rows;replay::Receipt receipt;
        Require(fixture.Capture(receipt) && receipt.Captured() && !receipt.Complete(),"Valid typed row capture failed");
        auto command=fixture.rows[0];command[2]^=0xff;command[3]^=0xff;
        for(size_t i=8+expected[kind];i<command.size();++i)command[i]^=0xff;
        const auto beforeCommand=command;replay::Token token;
        Require(receipt.Before(Identity,Node,2,0,{command.data(),replay::CommandBytes(kind)},token),"Padding/inactive payload was compared or small readable extent rejected");
        Require(receipt.Pending(Node.slot) && !receipt.Complete() && receipt.Cursor(Node.slot)==0,"Before increment claimed completed native work");
        Require(receipt.After(token,Identity,Node,1,kind==13?fixture.rows[0][8]:uint8_t(0xee)) && receipt.Complete(),"Valid native cursor/result was rejected");
        Require(receipt.CompletedCalls()==1 && !receipt.Pending(Node.slot) && fixture.rows==captured && command==beforeCommand,
            "Pure receipt changed source data or lost final completion");
    }
    Require(replay::PayloadBytes(14)==UINT32_MAX && !replay::CommandBytes(14),"Unknown native kind has an invented grammar");
    for(uint32_t kind=0;kind<14;++kind)for(uint32_t byte=0;byte<8+expected[kind];++byte){
        if(byte==2 || byte==3 || (kind==13 && byte==8) || (kind==2 && byte>=12 && byte<16))continue;
        Fixture fixture(kind);replay::Receipt receipt;Require(fixture.Capture(receipt),"Changed field capture failed");
        auto command=fixture.rows[0];command[byte]^=1;replay::Token token;
        Require(!Before(receipt,command,token) && receipt.Failed() && !token.nonce && !receipt.Complete(),"Changed kind/key/slot/copied payload accepted");
    }
}
void Kind13DecisionAndRequest(){
    Fixture fixture(13);fixture.rows[0][8]=0xf5;
    replay::Receipt permissive;Require(fixture.Capture(permissive),"Kind13 default capture failed");
    auto command=fixture.rows[0];command[8]=0x21;replay::Token token;
    Require(Before(permissive,command,token) && token.requestDifference && token.compiledDecision==0xf5 && token.requestedDecision==0x21,
        "Kind13 request was silently equated with compiled decision");
    Require(permissive.After(token,Identity,Node,1,0xf5) && permissive.Complete() && permissive.RequestDifferences()==1,
        "Actual recorded native decision did not complete");
    replay::Receipt strict;Require(fixture.Capture(strict,replay::Kind13Policy::RequireSameRequest),"Strict kind13 capture failed");
    Require(!Before(strict,command,token) && strict.Rejection()==replay::Failure::PayloadMismatch,"Strict request-value policy ignored a change");
    replay::Receipt wrongReturn;Require(fixture.Capture(wrongReturn) && Before(wrongReturn,command,token),"Wrong return fixture failed");
    Require(!wrongReturn.After(token,Identity,Node,1,0x21) && wrongReturn.Rejection()==replay::Failure::Result && !wrongReturn.Complete(),
        "Requested value replaced native recorded-decision result");
}
void BufferUnionRequestMask(){
    for(uint8_t type:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(255)})for(uint32_t byte=8;byte<48;++byte){
        Fixture fixture(1);fixture.rows[0][44]=type;replay::Receipt receipt;
        Require(fixture.Capture(receipt),"Descriptor union capture failed");
        auto command=fixture.rows[0];command[byte]^=1;replay::Token token;
        const bool allowed=(type==1 && byte>=40 && byte<44) || (type==0 && byte>40 && byte<44) ||
            ((type==0 || type==1) && byte>=46);
        Require(Before(receipt,command,token)==allowed,"Descriptor mask escaped audited union bytes or affected another type");
        if(allowed)Require(receipt.After(token,Identity,Node,1,0) && receipt.Complete(),"Inactive BUFFER union request failed cursor closure");
        else Require(receipt.Rejection()==replay::Failure::PayloadMismatch && !receipt.Complete(),"Active descriptor field mismatch not sticky");
    }
    Fixture fixture(1);fixture.rows[0][44]=1;fixture.rows[0][45]=7;
    fixture.rows[0][40]=0xc9;fixture.rows[0][41]=0x22;fixture.rows[0][42]=0xf3;fixture.rows[0][43]=0x0f;
    auto command=fixture.rows[0];command[40]=9;command[41]=1;command[42]=command[43]=0;
    replay::Receipt observed;replay::Token token;
    Require(fixture.Capture(observed) && Before(observed,command,token) && observed.After(token,Identity,Node,1,0) && observed.Complete(),
        "Observed339 BUFFER descriptor request mismatch was not interpreted correctly");
    Require(observed.Declarations()==fixture.rows,"Request mask normalized stored D0 bytes");
    for(uint32_t byte:{40u,41u,42u,43u,46u,47u}){
        auto changed=fixture.rows;changed[0][byte]^=1;replay::Receipt torn;
        Require(!torn.Capture(Identity,Identity,fixture.nodes,fixture.counts,fixture.counts,fixture.rows,changed) &&
            torn.Rejection()==replay::Failure::Changed,"Request mask weakened strict D0 capture stability");
    }
    fixture.rows[0][44]=0;
    for(uint32_t bit=0;bit<8;++bit){
        replay::Receipt texture;Require(fixture.Capture(texture),"TEXTURE flag capture failed");
        command=fixture.rows[0];command[40]^=uint8_t(1u<<bit);
        Require(Before(texture,command,token)==(bit>=3),"TEXTURE active bits0..2 mask changed");
        if(bit>=3)Require(texture.After(token,Identity,Node,1,0) && texture.Complete(),"Inactive TEXTURE flags failed closure");
    }
    fixture.rows[0][40]=0x39;fixture.rows[0][41]=0xf9;fixture.rows[0][42]=0x8f;fixture.rows[0][43]=0x98;
    command=fixture.rows[0];command[40]=0xb9;command[41]=0xf8;command[42]=0x3f;
    replay::Receipt textureObserved;
    Require(fixture.Capture(textureObserved) && Before(textureObserved,command,token) &&
        textureObserved.After(token,Identity,Node,1,0) && textureObserved.Complete(),"Observed340 inactive TEXTURE flag storage failed");
    auto changed=fixture.rows;changed[0][40]^=0x80;replay::Receipt textureTorn;
    Require(!textureTorn.Capture(Identity,Identity,fixture.nodes,fixture.counts,fixture.counts,fixture.rows,changed) &&
        textureTorn.Rejection()==replay::Failure::Changed,"TEXTURE request mask weakened strict D0 stability");
    fixture.rows[0][44]=1;fixture.rows[0][45]=5;fixture.rows[0][46]=0x5c;fixture.rows[0][47]=0xcb;
    command=fixture.rows[0];command[46]=command[47]=0;replay::Receipt bufferTail;
    Require(fixture.Capture(bufferTail) && Before(bufferTail,command,token) && bufferTail.After(token,Identity,Node,1,0) && bufferTail.Complete(),
        "Observed341 BUFFER trailing descriptor padding failed");
}
void Kind2AlignmentPadding(){
    Fixture fixture(2);auto command=fixture.rows[0];
    for(uint32_t byte=12;byte<16;++byte)command[byte]^=0xff;
    replay::Receipt receipt;replay::Token token;
    Require(fixture.Capture(receipt) && Before(receipt,command,token) && receipt.After(token,Identity,Node,1,0) && receipt.Complete(),
        "Kind2 native source-key/callback alignment padding was treated as semantic payload");
    Require(receipt.Declarations()==fixture.rows,"Kind2 request mask changed stored declarations");
    auto torn=fixture.rows;torn[0][12]^=1;replay::Receipt strict;
    Require(!strict.Capture(Identity,Identity,fixture.nodes,fixture.counts,fixture.counts,fixture.rows,torn) &&
        strict.Rejection()==replay::Failure::Changed,"Kind2 padding mask weakened D0 capture stability");
}
void SlotsAndCursorClosure(){
    Fixture fixture;fixture.nodes={{11,0,0,0},{22,1,2,1},{33,2,219,1}};
    fixture.counts={};fixture.counts[0]=2;fixture.counts[2]=1;fixture.counts[219]=1;
    fixture.rows={Row(1,0),Row(9,0),Row(4,2),Row(13,219)};
    replay::Receipt receipt;Require(fixture.Capture(receipt),"Multiple slot capture failed");
    replay::Token first,last;
    Require(receipt.Before(Identity,fixture.nodes[0],2,0,fixture.rows[0],first) &&
        receipt.Before(Identity,fixture.nodes[2],2,0,fixture.rows[3],last),"Different slots could not have simultaneous original calls");
    Require(receipt.After(last,Identity,fixture.nodes[2],1,fixture.rows[3][8]) && receipt.After(first,Identity,fixture.nodes[0],1,0),
        "Independent slot return order was constrained");
    Require(!receipt.Complete(),"Partial per-slot closure became whole grammar completion");
    Require(receipt.Before(Identity,fixture.nodes[1],2,0,fixture.rows[2],first) && receipt.After(first,Identity,fixture.nodes[1],1,0),"Middle slot failed");
    Require(!receipt.Complete(),"One partially consumed slot was ignored");
    Require(receipt.Before(Identity,fixture.nodes[0],2,1,fixture.rows[1],first) && receipt.After(first,Identity,fixture.nodes[0],2,0) &&
        receipt.Complete() && receipt.CompletedCalls()==4,"Exact all-slot final cursor closure failed");
    replay::Receipt unordered;std::reverse(fixture.nodes.begin(),fixture.nodes.end());Require(fixture.Capture(unordered),"Ordinal-labelled unordered node bindings rejected");
    Fixture empty;empty.counts={};empty.rows.clear();replay::Receipt noCalls;
    Require(empty.Capture(noCalls) && noCalls.Complete(),"Legitimate empty grammar was fabricated as nonempty work");
}
void CaptureArgumentsAndTears(){
    for(unsigned failure=0;failure<21;++failure){Fixture fixture;replay::Receipt receipt;auto before=Identity,after=Identity;
        auto countsAfter=fixture.counts;auto rowsAfter=fixture.rows;auto policy=replay::Kind13Policy::RecordRequestDifference;
        switch(failure){
        case 0:before.consumerId=after.consumerId=0;break;case 1:before.epoch=after.epoch=0;break;
        case 2:before.epoch=after.epoch=uint64_t(UINT32_MAX)+1;break;case 3:before.generation=after.generation=0;break;
        case 4:before.pool=after.pool=0;break;case 5:before.pool=after.pool=0x10001;break;
        case 6:before.executionGraph=after.executionGraph=before.sourceGraph;break;case 7:++after.generation;break;
        case 8:fixture.nodes.clear();break;case 9:fixture.nodes[0].slot=replay::Slots;break;
        case 10:fixture.nodes[0].viewIndex=2;break;case 11:fixture.nodes[0].ordinal=1;break;
        case 12:fixture.nodes[0].handle=65536;break;case 13:++countsAfter[Node.slot];break;
        case 14:fixture.counts[Node.slot]=countsAfter[Node.slot]=501;break;case 15:fixture.rows.clear();rowsAfter.clear();break;
        case 16:rowsAfter[0][47]^=1;break;case 17:fixture.rows[0][0]=rowsAfter[0][0]=14;break;
        case 18:fixture.rows[0][1]=rowsAfter[0][1]=0;break;case 19:policy=replay::Kind13Policy(3);break;
        case 20:fixture.counts[1]=countsAfter[1]=1;fixture.rows.insert(fixture.rows.begin(),Row(1,1));rowsAfter=fixture.rows;break;
        }
        Require(!receipt.Capture(before,after,fixture.nodes,fixture.counts,countsAfter,fixture.rows,rowsAfter,policy) &&
            receipt.Failed() && !receipt.Captured() && receipt.Declarations().empty() && !receipt.Complete(),"Malformed/torn capture published owned rows");
    }
    Fixture fixture;replay::Receipt shortCounts;
    Require(!shortCounts.Capture(Identity,Identity,fixture.nodes,{fixture.counts.data(),219},fixture.counts,fixture.rows,fixture.rows),"Short count ABI accepted");
    for(unsigned duplicate=0;duplicate<3;++duplicate){Fixture f;f.nodes.push_back({78,1,218,0});
        if(duplicate==0)f.nodes[1].handle=f.nodes[0].handle;if(duplicate==1)f.nodes[1].ordinal=f.nodes[0].ordinal;if(duplicate==2)f.nodes[1].slot=f.nodes[0].slot;
        replay::Receipt receipt;Require(!f.Capture(receipt),"Duplicate physical node/ordinal/slot accepted");
    }
    replay::Receipt recapture;Require(fixture.Capture(recapture),"Recapture fixture failed");
    Require(!fixture.Capture(recapture) && recapture.Failed() && !recapture.Complete(),"Existing receipt rearmed itself");
}
void InvocationAndReturnFailures(){
    for(unsigned failure=0;failure<13;++failure){Fixture fixture;replay::Receipt receipt;Require(fixture.Capture(receipt),"Invocation fixture capture failed");
        auto identity=Identity;auto node=Node;auto command=fixture.rows[0];uint32_t cursor=0,mode=2;size_t bytes=48;
        switch(failure){
        case 0:++identity.consumerId;break;case 1:++identity.epoch;break;case 2:++identity.generation;break;
        case 3:identity.pool+=8;break;case 4:identity.sourceGraph+=8;break;case 5:identity.executionGraph+=8;break;
        case 6:++node.handle;break;case 7:++node.viewIndex;break;case 8:mode=1;break;
        case 9:cursor=1;break;case 10:bytes=7;break;case 11:bytes=47;break;case 12:command[0]=255;break;
        }
        replay::Token token;Require(!receipt.Before(identity,node,mode,cursor,{command.data(),bytes},token) && receipt.Failed() && !token.nonce &&
            !receipt.Complete(),"Foreign/torn/native premature cursor invocation accepted");
    }
    for(unsigned failure=0;failure<4;++failure){Fixture fixture;replay::Receipt receipt;replay::Token token;
        Require(fixture.Capture(receipt) && Before(receipt,fixture.rows[0],token),"Return fixture failed");
        auto identity=Identity;auto node=Node;uint32_t cursor=1;
        if(failure==0)++identity.epoch;if(failure==1)++node.handle;if(failure==2)cursor=0;if(failure==3)cursor=2;
        Require(!receipt.After(token,identity,node,cursor,0) && receipt.Failed() && !receipt.Pending(Node.slot) && !receipt.Complete(),
            "Foreign return or wrong native counter increment completed");
    }
    Fixture fixture;replay::Receipt busy;replay::Token first,second;
    Require(fixture.Capture(busy) && Before(busy,fixture.rows[0],first),"Busy fixture failed");
    Require(!Before(busy,fixture.rows[0],second) && busy.Rejection()==replay::Failure::Busy &&
        !busy.After(first,Identity,Node,1,0) && !busy.Pending(Node.slot),"Overlapping same-slot calls or failed cleanup recovered");
    replay::Receipt duplicate;Require(fixture.Capture(duplicate) && Before(duplicate,fixture.rows[0],first) &&
        duplicate.After(first,Identity,Node,1,0) && duplicate.Complete(),"Duplicate fixture failed");
    Require(!duplicate.After(first,Identity,Node,1,0) && !duplicate.Complete(),"Duplicate original return remained complete");
    replay::Receipt forged;Require(fixture.Capture(forged) && Before(forged,fixture.rows[0],first),"Forged token fixture failed");
    second=first;++second.nonce;Require(!forged.After(second,Identity,Node,1,0) && !forged.Complete(),"Forged token completed original invocation");
    replay::Receipt fresh;Require(fixture.Capture(fresh) && !fresh.After(first,Identity,Node,1,0),"Prior receipt token completed another receipt");
}
void CaptureCapacity(){
    Fixture fixture;fixture.nodes.clear();fixture.rows.clear();fixture.counts.fill(replay::RowsPerSlot);
    for(uint32_t slot=0;slot<replay::Slots;++slot){
        fixture.nodes.push_back({1000+slot,slot,slot,slot&1});
        for(uint32_t index=0;index<replay::RowsPerSlot;++index)fixture.rows.push_back(Row(index%14,slot));
    }
    replay::Receipt maximum;Require(fixture.Capture(maximum) && maximum.Declarations().size()==replay::MaxRows,"Audited maximum full declaration inventory rejected");
    const auto node=fixture.nodes.back();replay::Token token;
    Require(!maximum.Before(Identity,node,2,500,fixture.rows.back(),token),"Native cursor outside declared slot capacity accepted");
    replay::Receipt tooMany;fixture.nodes.push_back({9999,220,0,0});Require(!fixture.Capture(tooMany),"Node inventory overflow accepted");
}
}

int main()try{
    static_assert(!std::is_copy_constructible_v<replay::Receipt> && !std::is_move_constructible_v<replay::Receipt>);
    AllKindsAndSemanticMasks();Kind13DecisionAndRequest();BufferUnionRequestMask();Kind2AlignmentPadding();SlotsAndCursorClosure();CaptureArgumentsAndTears();InvocationAndReturnFailures();CaptureCapacity();
    std::puts("PASS native resource replay: exact0..13 copy masks, full fresh slot inventory, bound consumer/graph/node, typed payload/cursor closure and separate kind13 request/compiled decisions; no native calls or alias/lifetime authority");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
