#include "Overlay/VrWidgets.hpp"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Rect {ImVec2 a,b;};
int main()try {
    ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.DisplaySize={1100,900};io.DeltaTime=1.f/60;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    auto& style=ImGui::GetStyle();style.WindowPadding={24,20};style.FramePadding={12,10};style.ItemSpacing={14,12};style.ItemInnerSpacing={12,10};
    unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    float value=.50f;int integer=3,choice=0;bool disabled=false;
    Rect slider{},list{};int changes=0;bool sliderActive=false;
    auto frame=[&] {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,850});
        ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar);
        ImGui::BeginDisabled(disabled);
        changes+=overlay::widgets::SliderFloat("A slider label that wraps in a narrow view",&value,0,1,"%.2f");
        sliderActive=ImGui::IsItemActive();
        slider={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        changes+=overlay::widgets::SliderInt("Integer setting",&integer,0,10,"%d");
        const char* items[]={"First","Second","Third"};
        changes+=overlay::widgets::Combo("List setting",&choice,items,3);
        list={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        ImGui::EndDisabled();ImGui::End();ImGui::Render();
    };
    auto point=[](Rect r,float fraction){return ImVec2{r.a.x+(r.b.x-r.a.x)*fraction,(r.a.y+r.b.y)*.5f};};
    auto click=[&](ImVec2 p,bool ctrl=false) {
        io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,true);frame();
        io.AddMouseButtonEvent(0,false);frame();frame();
    };
    auto key=[&](ImGuiKey k) {io.AddKeyEvent(k,true);frame();io.AddKeyEvent(k,false);frame();};
    frame();frame();
    click(point(slider,.98f));Check(std::abs(value-.51f)<1e-6f,"slider arrow did not change by displayed precision");
    click(point(slider,.43f));Check(std::abs(value-.50f)<1e-6f,"slider decrement arrow failed");
    const auto held=point(slider,.75f);io.AddMousePosEvent(held.x,held.y);io.AddMouseButtonEvent(0,true);frame();
    for(int i=0;i<180;++i){frame();Check(sliderActive,"stationary hold lost grouped slider activity");}
    io.AddMouseButtonEvent(0,false);frame();Check(!sliderActive,"released slider remained active");
    value=1;frame();click(point(slider,.98f));Check(value==1,"slider arrow exceeded maximum");
    value=.5f;frame();
    const auto drag=point(slider,.88f);io.AddMousePosEvent(drag.x,drag.y);io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(slider.b.x+300,drag.y);frame();io.AddMouseButtonEvent(0,false);frame();
    Check(value==1,"dragged slider failed to clamp at maximum");
    click(point(list,.98f));Check(choice==1,"list arrow did not change selection");
    click(point(list,.75f));Check(GImGui->OpenPopupStack.Size>0,"list value did not open its dropdown");
    key(ImGuiKey_DownArrow);key(ImGuiKey_Enter);frame();
    Check(choice==2 && GImGui->OpenPopupStack.Size==0,"dropdown keyboard selection failed");
    click(point(slider,.70f),true);Check(GImGui->TempInputId!=0,"Ctrl-click lost native numeric entry");
    Check(sliderActive,"numeric entry did not expose slider activity");
    io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddInputCharactersUTF8("0.37");frame();key(ImGuiKey_Enter);frame();
    Check(std::abs(value-.37f)<1e-6f,"numeric entry did not apply to themed slider");
    disabled=true;frame();const int before=changes;
    click(point(slider,.98f));click(point(list,.43f));
    Check(value==.37f && choice==2 && changes==before,"disabled controls still changed settings");
    ImGui::DestroyContext();std::cout<<"PASS themed arrows, slider drag/clamp, popup, keyboard numeric entry and disabled controls\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
