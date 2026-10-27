#pragma once
#include "Overlay/VrInteraction.hpp"
#include <imgui.h>
namespace cvr::vrui {
// This project's ImGui1.90 DX12 backend ignores FramebufferScale. Transform
// vertices and scissor rectangles for the desktop pass, then restore XR data.
class DesktopDraw {
    ImDrawData& data;DesktopRect rect;ImVec2 pos,size,scale;
    void Transform(float s,float x,float y){
        for(int i=0;i<data.CmdListsCount;++i){auto* list=data.CmdLists[i];
            for(auto& v:list->VtxBuffer){v.pos.x=v.pos.x*s+x;v.pos.y=v.pos.y*s+y;}
            for(auto& c:list->CmdBuffer){c.ClipRect={c.ClipRect.x*s+x,c.ClipRect.y*s+y,c.ClipRect.z*s+x,c.ClipRect.w*s+y};}
        }
    }
public:
    DesktopDraw(ImDrawData& d,float w,float h,DesktopRect placement):data(d),rect(placement),pos(d.DisplayPos),size(d.DisplaySize),scale(d.FramebufferScale){
        Transform(rect.scale,rect.x,rect.y);data.DisplayPos={0,0};data.DisplaySize={w,h};data.FramebufferScale={1,1};
    }
    DesktopDraw(ImDrawData& d,float w,float h):DesktopDraw(d,w,h,DesktopPlacement(w,h)){}
    ~DesktopDraw(){Transform(1/rect.scale,-rect.x/rect.scale,-rect.y/rect.scale);data.DisplayPos=pos;data.DisplaySize=size;data.FramebufferScale=scale;}
};
}
