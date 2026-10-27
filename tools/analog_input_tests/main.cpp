#include "Hooks/AnalogStick.hpp"
#include "Hooks/KeypadInput.hpp"
#include "Hooks/WeaponTrigger.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)try {
    using namespace cvr::input;
    Check(argc==2,"case required");const std::string name=argv[1];
    if(name=="weapon_trigger") {
        WeaponTrigger t;
        Check(!t.Update(true,0),"rest fired");
        for(float v:{.2f,.5f,.849f})Check(!t.Update(true,v),"early break");
        Check(t.Update(true,.85f),"break missed");
        for(int i=0;i<100;++i)Check(t.Update(true,1),"automatic hold interrupted");
        for(float v:{.84f,.5f,.201f,.9f})Check(t.Update(true,v),"hold reset early");
        Check(!t.Update(true,.20f),"reset missed");
        for(int i=0;i<100;++i){Check(t.Update(true,.85f),"rapid re-press missed");Check(!t.Update(true,.2f),"rapid reset missed");}
        Check(!t.Update(false,1),"inactive fired");
        Check(!t.Update(true,1) && !t.Update(true,.3f),"reentry with held trigger fired");
        Check(!t.Update(true,.2f) && t.Update(true,.85f),"reentry did not rearm");
        Check(!t.Update(true,std::numeric_limits<float>::quiet_NaN()),"invalid input fired");
        Check(!t.Update(true,1),"tracking recovery auto-fired");
    } else if(name=="keypad_cursor") {
        const KeypadPoint size{600,1066}, centre{300,533};
        for(int fps:{30,45,60,90,120,144}) {
            KeypadCursor cursor;
            Check(!cursor.Update(false,{}, {},size,.02f),"panel without a real hit was claimed");
            Check(cursor.Update(true,centre,{},size,0),"initial head hit failed");
            for(int i=0;i<fps;++i)cursor.Update(false,{}, {.1f,0},size,1.f/fps);
            Check(std::abs(cursor.position.x-390)<.02f,"stick speed depends on FPS or gaze");
            const auto held=cursor.position;
            cursor.Update(false,{}, {},size,.03f);
            Check(cursor.position.x==held.x,"cursor moves after releasing the stick");
            cursor.Update(true,{50,50},{},size,.02f);
            Check(cursor.position.x==held.x,"returning gaze teleports the held cursor");
            cursor.Update(true,{55,57},{},size,.02f);
            Check(std::abs(cursor.position.x-held.x-5)<.001f && std::abs(cursor.position.y-held.y-7)<.001f,"head movement stopped after stick use");
        }
        for(float axis:{-.5f,.5f}) {
            KeypadCursor cursor;cursor.Update(true,centre,{},size,0);
            for(int i=0;i<1000;++i)cursor.Update(false,{}, {axis,axis},size,.02f);
            Check(cursor.position.x>=1 && cursor.position.x<=599 && cursor.position.y>=1 && cursor.position.y<=1065,"cursor escaped panel");
            const auto edge=cursor.position;
            cursor.Update(false,{}, {-axis,-axis},size,.02f);
            Check(std::abs(cursor.position.x-edge.x)>8.9f,"edge overtravel delays reversal");
        }
        KeypadCursor cursor;cursor.Update(true,centre,{},size,0);
        cursor.Update(false,{}, {.5f,0},size,10);
        Check(std::abs(cursor.position.x-322.5f)<.001f,"hitch jumped across keypad");
        const auto before=cursor.position;
        const float nan=std::numeric_limits<float>::quiet_NaN();
        cursor.Update(true,{nan,nan},{nan,nan},size,nan);
        Check(cursor.position.x==before.x && cursor.position.y==before.y,"invalid pose/input moved cursor");
        Check(!cursor.Update(true,centre,{}, {0,1066},.01f) && !cursor.initialized,"invalid/resized panel retains cursor");
        cursor.Update(true,{75,80},{},size,0);
        Check(cursor.position.x==75 && cursor.position.y==80,"next device inherits old cursor");
    } else if(name=="keypad") {
        float previous=0;
        for(int i=0;i<=1000;++i) {
            const float raw=i/1000.f,value=KeypadCursorAxis(raw,.15f);
            Check(value>=previous && value<=.5f,"keypad cursor is not bounded and proportional");
            Check(KeypadCursorAxis(-raw,.15f)==-value,"keypad cursor direction is asymmetric");
            if(raw<=.15f)Check(value==0,"keypad centre drifts");
            previous=value;
        }
        Check(KeypadCursorAxis(.575f,.15f)>.249f && KeypadCursorAxis(.575f,.15f)<.251f,"partial keypad push became full movement");
        Check(KeypadCursorAxis(std::numeric_limits<float>::quiet_NaN(),.15f)==0,"invalid keypad input moves cursor");
        Check(!KeypadInputFresh(0,1000) && KeypadInputFresh(1000,1350) && !KeypadInputFresh(1000,1351),"keypad session does not expire");
        Check(!KeypadInputFresh(1000,999),"clock rollback keeps keypad input claimed");
    } else if(name=="curve") {
        for(float sign:{-1.f,1.f}) {
            Check(AnalogAxis(sign*.14f,.15f,.9f)==0,"centre drift escapes deadzone");
            Check(AnalogAxis(sign*.15f,.15f,.9f)==0,"deadzone boundary moves");
            Check(std::abs(AnalogAxis(sign*.525f,.15f,.9f)-sign*.5f)<1e-6f,"half usable travel must give half input");
            Check(AnalogAxis(sign*.9f,.15f,.9f)==sign,"outer threshold does not give full input");
            Check(AnalogAxis(sign*1.1f,.15f,.9f)==sign,"overshoot exceeds full input");
        }
    } else if(name=="ranges") {
        for(float dead:{0.f,.05f,.15f,.30f})for(float full:{.8f,.9f,1.f}) {
            float previous=-1;
            for(int i=-1000;i<=1000;++i) {
                const float raw=i*.001f,out=AnalogAxis(raw,dead,full);
                Check(std::isfinite(out) && std::abs(out)<=1,"analog output outside range");
                Check(out>=previous,"stick speed is not monotonic");previous=out;
                Check(std::abs(out+AnalogAxis(-raw,dead,full))<1e-6f,"asymmetric forward/back or strafe response");
            }
        }
    } else if(name=="gestures") {
        for(float dead:{0.f,.15f,.30f})for(float full:{.8f,.9f,1.f}) {
            Check(AnalogAxis(full,dead,full)==1,"full travel mapping lost");
            Check(!AtFullTravel(full-.001f,true,full),"partial travel activates sprint/dash");
            Check(AtFullTravel(full,true,full),"full raw threshold missed");
            Check(AtFullTravel(-full,true,full,true),"crouch threshold differs from forward");
            Check(!AtFullTravel(0,true,full),"consumed scanner axis activates sprint");
        }
        Check(!AtFullTravel(.9f,false,.8f) && AtFullTravel(.901f,false,1),"fixed-mode threshold changed");
    } else if(name=="invalid") {
        const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
        for(float value:{nan,inf,-inf}) {
            Check(AnalogAxis(value,.15f,.9f)==0,"invalid axis is not neutral");
            Check(!AtFullTravel(value,true,.9f),"invalid axis activates a gesture");
            Check(StickDeadzone(value)==.15f && StickFullInput(value)==.9f,"invalid settings lack safe defaults");
        }
        Check(StickDeadzone(-.1f)==.15f && StickDeadzone(.31f)==.15f,"deadzone bounds not validated");
        Check(StickFullInput(.79f)==.9f && StickFullInput(1.01f)==.9f,"full threshold bounds not validated");
    } else if(name=="default") {
        Check(!UseAnalogMovement(0,false),"fixed default replaced with analog");
        Check(UseAnalogMovement(1,false),"analog switch has no effect on foot");
        Check(!UseAnalogMovement(1,true),"analog tuning changes driving");
        Check(!UseAnalogMovement(-1,false) && !UseAnalogMovement(2,false),"unknown movement mode accepted");
    } else throw std::runtime_error("unknown case");
    std::cout<<"PASS "<<name<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
