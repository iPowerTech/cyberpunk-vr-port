#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace cvr::cyberware {
struct Vec3 {
    float x{},y{},z{};
    Vec3 operator+(Vec3 b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec3 operator-(Vec3 b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec3 operator*(float s)const{return {x*s,y*s,z*s};}
};
inline float Length(Vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
inline bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline Vec3 Unit(Vec3 v){const float n=Length(v);return n>1e-6f?v*(1/n):Vec3{0,1,0};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Plane {Vec3 position{},normal{};bool valid{};};

// A free-ended line, anchored at the emitter. World-space inertia, gravity and
// inextensible segments are shared by the rendered rig and combat geometry.
// The owner calls Step once per tracking/animation epoch, never per graph pass.
struct Wire {
    static constexpr int Count=46;
    static constexpr float Reach=5.f;
    std::array<Vec3,Count> point{},velocity{};
    std::array<Plane,Count> planes{};
    Vec3 lastAnchor{},lastDirection{},motion{};
    float deployed{},swingTension{};
    bool valid{};
    void Reset(){*this={};}
    // carry: how far the body itself moved since the last step. The whole line moves with it first,
    // so running, walking and falling are not motion of the line (2026-10-17, user: the wire flew
    // backwards while running). The velocity damping below works in world space: a line left out of
    // the carry trails its pin by about 0.115 s of the body's speed, 0.7 m at a 6 m/s sprint.
    void Step(Vec3 anchor,Vec3 direction,bool grip,float dt,float length,Vec3 carry={}) {
        if(!Finite(anchor)||!Finite(direction)||!std::isfinite(dt)||!std::isfinite(length)) {Reset();return;}
        length=std::clamp(length,.2f,8.f);
        direction=Unit(direction);
        if(valid && Finite(carry)) {
            for(auto& p:point)p=p+carry;
            lastAnchor=lastAnchor+carry;
        }
        if(!valid || Length(anchor-lastAnchor)>1.f || dt>.15f) {
            for(int i=0;i<Count;++i){point[i]=anchor+direction*(deployed*length*i/(Count-1));velocity[i]={};}
            lastAnchor=anchor;
            lastDirection=direction;motion={};swingTension=0;
            valid=true;
        }
        const Vec3 anchorBefore=valid?lastAnchor:anchor;
        const float sampleDt=std::max(dt,.001f);
        // Motion at the line's middle captures both arm sweeps and wrist
        // flicks. Work in 3D so the same trailing bow applies in any plane.
        const Vec3 sampleMotion=(anchor-lastAnchor+(direction-lastDirection)*(length*.5f))*(1/sampleDt);
        const float motionBlend=1-std::exp(-std::min(dt,.05f)/.04f);
        motion=motion+(sampleMotion-motion)*motionBlend;
        const float tensionTarget=std::clamp((Length(motion)-.6f)/2.4f,0.f,1.f);
        const float tensionBlend=1-std::exp(-std::min(dt,.05f)/(tensionTarget>swingTension?.04f:.18f));
        swingTension+=(tensionTarget-swingTension)*tensionBlend;
        const Vec3 lateral=motion-direction*Dot(motion,direction);
        const float lateralSpeed=Length(lateral);
        const Vec3 bow=lateralSpeed>1e-5f ? lateral*(-std::min(length*.12f,lateralSpeed*.10f)*swingTension/lateralSpeed):Vec3{};
        lastAnchor=anchor;
        lastDirection=direction;
        dt=std::clamp(dt,0.f,.05f);
        const int steps=std::max(1,int(std::ceil(dt*240.f)));const float h=dt/steps;
        if(h<1e-6f)return;
        for(int step=0;step<steps;++step) {
            const Vec3 pin=anchorBefore+(anchor-anchorBefore)*(float(step+1)/steps);
            const float previousDeployed=deployed;
            deployed=std::clamp(deployed+(grip?5.f:-8.f)*h,0.f,1.f);
            if(deployed<=.0001f) {point.fill(anchor);velocity.fill({});continue;}
            if(previousDeployed<=.0001f) {
                for(int i=0;i<Count;++i)point[i]=anchor+direction*(length*deployed*i/(Count-1));
                velocity.fill({});
            } else if(deployed!=previousDeployed) {
                const float ratio=deployed/previousDeployed;
                for(int i=1;i<Count;++i)point[i]=anchor+(point[i]-anchor)*ratio;
            }
            const float segment=length*deployed/(Count-1);
            const auto before=point;
            point[0]=pin;
            for(int i=1;i<Count;++i) {
                // A light filament must retain a flick's momentum. The old
                // floor multiplier removed 90% of it in just 0.18 seconds.
                velocity[i]=velocity[i]*std::exp(-.35f*h);
                if(grip && deployed>previousDeployed)
                    velocity[i]=velocity[i]+direction*(12.f*h*float(i)/(Count-1));
                // A supported filament, not a hanging rope: shape memory keeps
                // the proximal section ahead of the wrist, fading to a free
                // flexible tip. Constraints/collisions still own its real shape.
                const float along=float(i)/(Count-1);
                const float support=std::clamp((.85f-along)/.20f,0.f,1.f);
                const float stiffness=300.f*(support*support+(1-support*support)*swingTension);
                // A smooth, slightly bowed arc trails the swing. Its end
                // tangent is straight; tension supports the otherwise hanging
                // tip only during motion, then releases it gradually.
                const float curve=16.f*along*along*(1-along)*(1-along);
                const Vec3 rest=pin+direction*(segment*i)+bow*(curve*deployed);
                velocity[i]=velocity[i]+(rest-point[i])*(stiffness*h);
                velocity[i]=velocity[i]*std::exp(-2.f*std::sqrt(stiffness)*h);
                velocity[i].z-=9.81f*h;
                point[i]=point[i]+velocity[i]*h;
            }
            // Symmetric passes avoid a permanent stretch gradient at the hand.
            for(int iteration=0;iteration<24;++iteration) {
                for(int j=1;j<Count;++j) {
                    const int i=(iteration&1)?Count-j:j;
                    Vec3 delta=point[i]-point[i-1];const float n=Length(delta);
                    delta=n>1e-6f?delta*(1/n):direction;
                    const Vec3 correction=delta*(n-segment);
                    if(i==1)point[i]=point[i]-correction;
                    else {point[i-1]=point[i-1]+correction*.5f;point[i]=point[i]-correction*.5f;}
                }
                // Long-range, tension-only constraints carry a hand impulse
                // through the chain without making the slack line a rigid rod.
                for(int i=2;i<Count;++i) {
                    Vec3 delta=point[i]-pin;const float n=Length(delta),limit=segment*i;
                    if(n>limit)point[i]=pin+delta*(limit/n);
                }
                point[0]=pin;
                // The first short segment exits along the wrist. Rotating the
                // emitter can flick the line; the remaining 44 joints are free.
                point[1]=pin+direction*segment;
                for(int i=1;i<Count;++i)if(planes[i].valid) {
                    const Vec3 delta=point[i]-planes[i].position;
                    const float distance=Dot(delta,planes[i].normal);
                    const Vec3 tangent=delta-planes[i].normal*distance;
                    if(distance<.006f && Length(tangent)<.65f)
                        point[i]=point[i]+planes[i].normal*(.006f-distance);
                }
            }
            for(int i=1;i<Count;++i) {
                velocity[i]=(point[i]-before[i])*(1/h);
                const float speed=Length(velocity[i]);if(speed>35.f)velocity[i]=velocity[i]*(35.f/speed);
                if(planes[i].valid && Dot(point[i]-planes[i].position,planes[i].normal)<.012f) {
                    const float into=Dot(velocity[i],planes[i].normal);
                    if(into<0)velocity[i]=velocity[i]-planes[i].normal*into;
                    const Vec3 tangent=velocity[i]-planes[i].normal*Dot(velocity[i],planes[i].normal);
                    const float tangentSpeed=Length(tangent);
                    if(tangentSpeed>1e-6f)velocity[i]=velocity[i]-tangent*(std::min(tangentSpeed,.04f*9.81f*h)/tangentSpeed);
                }
            }
        }
    }
};
}
