#pragma once
// Independent prescribed trajectory: measurements here include the moving
// interval, not just the settled endpoint used by the original turn fixtures.
void DynamicsMetrics() {
    for(float hz:{45.f,90.f,144.f})for(float speed:{30.f,90.f,180.f,360.f}) {
        Run r(hz);r.Warm(poses[1]);float peak=0,total=0,minRate=1e9f,maxRate=0,previous=0;int n=0;
        for(int i=1;i<=int(hz*4);++i) {
            const float goal=speed*deg*i/hz;const auto value=r.Step(poses[1],goal,goal);
            if(i>int(hz*.5f)) {
                const float error=Error(value.yaw,goal);peak=std::max(peak,error);total+=error;++n;
                const float rate=std::remainder(value.yaw-previous,2*pi)*hz/deg;
                minRate=std::min(minRate,rate);maxRate=std::max(maxRate,rate);
            }
            previous=value.yaw;
        }
        std::cout<<"{\"hz\":"<<hz<<",\"speed\":"<<speed<<",\"mean_lag_deg\":"<<total/n
                 <<",\"peak_lag_deg\":"<<peak<<",\"min_rate\":"<<minRate<<",\"max_rate\":"<<maxRate<<"}\n";
    }
}

void PredictionSteady() {
    float worst=0;int count=0;
    for(const auto& pose:poses)for(float hz:{30.f,45.f,72.f,90.f,120.f,144.f})
    for(float speed:{-360.f,-180.f,-30.f,30.f,180.f,360.f}) {
        Run r(hz);r.Warm(pose);float previous=0;
        for(int i=1;i<=int(hz*6);++i) {
            const float goal=speed*deg*i/hz;const auto sample=r.Step(pose,goal,goal);
            if(i>hz*.5f) {
                const float error=Error(sample.yaw,goal);worst=std::max(worst,error);
                Check(error<.02f,"constant rotation lagged at a reference boundary: "+std::string(pose.name));
                const float velocity=std::remainder(sample.yaw-previous,2*pi)*hz/deg;
                Check(std::abs(velocity-speed)<.08f,"constant rotation developed stop/catch-up steps");
                const auto extrapolated=r.tracker.Predict(r.time+uint64_t(500000/hz));
                const float horizon=std::min(.5f/hz,.015f);
                Check(Error(extrapolated.yaw,goal+std::clamp(speed*deg*horizon,-3*deg,3*deg))<.02f,
                      "prediction does not bridge the observed sample interval");
            }
            previous=sample.yaw;
        }
        ++count;
    }
    std::cout<<count<<" multi-revolution steady trajectories; peak lag "<<worst<<" deg\n";
}

void PredictionStops() {
    float peakOver=0,peakIdle=0;int count=0;
    for(const auto& pose:poses)for(float hz:{30.f,45.f,90.f,144.f})
    for(float speed:{-360.f,-90.f,90.f,360.f}) {
        Run r(hz);r.Warm(pose);float goal=0;
        for(int cycle=0;cycle<4;++cycle) {
            const float v=cycle%2 ? -speed:speed;
            for(int i=0;i<int(hz*.55f);++i){goal+=v*deg/hz;r.Step(pose,goal,goal);}
            // Stop coincides with the current sample: no measured overshoot may
            // survive it. A future read before a new sample has a separate 3deg cap.
            for(int i=0;i<int(hz*.45f);++i) {
                const auto measured=r.Step(pose,goal,goal);
                const auto predicted=r.tracker.Predict(r.time+uint64_t(500000/hz));
                const float over=std::remainder(predicted.yaw-goal,2*pi)*(v>0 ? 1:-1)/deg;
                peakOver=std::max(peakOver,over);
                Check(over<.15f,"predictor overshot a measured stop");
                if(i>int(hz*.3f)) {peakIdle=std::max(peakIdle,Error(measured.yaw,goal));Check(Error(measured.yaw,goal)<.1f,"stop did not settle");}
            }
        }
        ++count;
    }
    std::cout<<count<<" repeated reversals/stops; overshoot "<<peakOver<<" deg, settled "<<peakIdle<<" deg\n";
}

void PredictionLifecycle() {
    Run r(90);r.Warm(poses[1]);
    for(int i=0;i<90;++i)r.Step(poses[1],i*deg,i*deg);
    const auto a=r.tracker.Predict(r.time+50000),b=r.tracker.Predict(r.time+100000);
    Check(a.valid && b.valid && Error(a.yaw,b.yaw)<.0001f,"stale prediction integrated after its horizon");
    Check(Error(a.yaw,r.last.yaw)<=3.001f,"prediction exceeded angular cap");
    Check(!r.tracker.Predict(r.time+150001).valid,"stale tracking remained eligible");
    Check(!r.tracker.Predict(r.time-1).valid,"future tracking clock accepted");
    auto frame=Frame(poses[1],90*deg,90*deg,++r.seq,r.time+=11111);frame.tracked[1]=false;
    const auto lost=r.tracker.Update(frame);Check(!lost.valid,"missing controller accepted");
    Check(!r.tracker.Predict(r.time).valid,"tracking loss left a live extrapolator");
    frame.tracked[1]=true;frame.origin=2;frame.sequence++;frame.stampUs+=11111;
    Check(Error(r.tracker.Update(frame).yaw,0)<.001f,"origin change extrapolated previous turn");
    std::cout<<"PASS bounded age, missing controller, clocks and recenter\n";
}

void PredictionNoisyRebase() {
    std::mt19937 rng(912711);float falseTurn=0;int count=0;
    for(const auto& initial:poses)for(float hz:{45.f,90.f,144.f})for(float sign:{-1.f,1.f}) {
        Run r(hz);r.Warm(initial);float goal=0;
        for(int i=0;i<int(hz*1.2f);++i) {goal=sign*60*deg*i/hz;r.Step(initial,goal,goal);}
        // Gestures and loss shortly after crossing the rebase angle must not
        // inherit a permission to extrapolate the previous body motion.
        const float expected=goal;
        for(int i=0;i<int(hz*4);++i) {
            const float t=i/hz;Pose gesture=initial;
            gesture.rotation[0]=MultiplyQuat(Yaw(sign*50*deg*std::sin(t*2)),initial.rotation[0]);
            gesture.rotation[1]=MultiplyQuat(Yaw(sign*50*deg*std::sin(t*2)),initial.rotation[1]);
            auto f=Frame(gesture,goal,goal+sign*50*deg*std::sin(t*2),++r.seq,r.time+=uint64_t(1000000/hz));
            Jitter(f,rng,3);r.last=r.tracker.Update(f);
            const float error=Error(r.tracker.Predict(r.time+uint64_t(400000/hz)).yaw,expected);
            falseTurn=std::max(falseTurn,error);
            Check(error<3.0f,"post-rebase wrist gesture generated a predicted body turn: "+std::string(initial.name)+
                " hz="+std::to_string(hz)+" sign="+std::to_string(sign)+" t="+std::to_string(t)+" error="+std::to_string(error));
        }
        ++count;
    }
    std::cout<<count<<" post-rebase independent noisy gestures; peak false yaw "<<falseTurn<<" deg\n";
}

void PredictionCadence() {
    float worst=0;int count=0,failures=0;
    for(const auto& pose:poses)for(float inputHz:{45.f,72.f,90.f,120.f,144.f})
    for(float gameHz:{30.f,60.f,90.f,144.f})for(float direction:{-1.f,1.f}) {
        TrackedBodyYaw tracker;uint64_t seq=0,next=1000000;
        // Separate native and tracking clocks; the consumer sees held samples.
        float previous=0;bool havePrevious=false;
        for(int tick=0;tick<int(gameHz*7);++tick) {
            const uint64_t now=1000000+uint64_t(tick*1e6/gameHz);
            auto trajectory=[&](float t) {
                if(t<.5f)return 0.f;
                if(t<2.5f)return direction*(t-.5f)*90*deg;
                if(t<3.f)return direction*180*deg;
                if(t<5.f)return direction*(180-(t-3)*90)*deg;
                return 0.f;
            };
            while(next<=now) {
                const float t=float(next-1000000)*1e-6f,goal=trajectory(t);
                tracker.Update(Frame(pose,goal,goal,++seq,next));
                next+=uint64_t(1e6/inputHz);
            }
            const auto estimate=tracker.Predict(now);
            if(!estimate.valid)continue;
            const float t=float(now-1000000)*1e-6f,goal=trajectory(t);
            const float error=Error(estimate.yaw,goal);
            if((t>1 && t<2.45f)||(t>3.5f && t<4.95f)) {
                worst=std::max(worst,error);
                if(error>=2.2f) {
                    ++failures;
                    if(failures<8)std::cout<<"cadence miss "<<pose.name<<" input="<<inputHz<<" game="<<gameHz<<" t="<<t<<" err="<<error<<"\n";
                }
            }
            if(t>5.4f)Check(error<.1f,"different rates left an offset after return");
            if(havePrevious)Check(Error(estimate.yaw,previous)<720/gameHz+3.1f,"cadence caused an unbounded turn");
            previous=estimate.yaw;havePrevious=true;
        }
        ++count;
    }
    std::cout<<count<<" asynchronous XR/native cadence trajectories; peak steady error "<<worst<<" deg\n";
    Check(!failures,"different native/XR rates lost steady turn");
}

void PredictionAcceleration() {
    float peak=0;int count=0;
    for(const auto& pose:poses)for(float hz:{30.f,45.f,90.f,144.f})
    for(int scenario=0;scenario<4;++scenario)for(float sign:{-1.f,1.f}) {
        Run r(hz);r.Warm(pose);float previous=0,goal=0;
        for(int i=0;i<int(hz*6);++i) {
            const float t=i/hz;
            if(scenario==0)goal=sign*90*deg*std::sin(t*1.8f);
            if(scenario==1)goal=sign*180*deg*std::sin(t*.9f);
            if(scenario==2)goal=sign*(t<1 ? 180*t*t:t<3 ? 180+360*(t-1):900)*deg;
            if(scenario==3)goal=sign*15*deg*std::sin(t*8);
            const auto out=r.Step(pose,goal,goal);
            const auto predicted=r.tracker.Predict(r.time+uint64_t(300000/hz));
            const float error=Error(out.yaw,goal);peak=std::max(peak,error);
            Check(std::isfinite(predicted.yaw) && out.valid,"acceleration invalidated coherent tracking");
            Check(Error(out.yaw,previous)<=720/hz+.01f,"acceleration exceeded native rate bound");
            previous=out.yaw;
        }
        for(int i=0;i<int(hz*.6f);++i)r.Step(pose,goal,goal);
        Check(Error(r.last.yaw,goal)<.1f,"accelerating turn did not settle at the measured endpoint");
        ++count;
    }
    std::cout<<count<<" accelerated, sinusoidal, rapid reversal trajectories; moving peak "<<peak<<" deg\n";
}

void PredictionNoiseMetrics() {
    for(float hz:{45.f,90.f,144.f})for(float speed:{30.f,90.f,360.f}) {
        std::mt19937 rng(48117);Run r(hz);r.Warm(poses[1]);float previous=0,total=0,peak=0,minRate=1e9f,maxRate=0;int n=0,backwards=0;
        for(int i=0;i<int(hz*8);++i) {
            const float goal=speed*deg*i/hz;auto f=Frame(poses[1],goal,goal,++r.seq,r.time+=uint64_t(1000000/hz));
            Jitter(f,rng,3);const auto out=r.tracker.Update(f);
            if(i>hz*.5f){const float rate=std::remainder(out.yaw-previous,2*pi)*hz/deg;minRate=std::min(minRate,rate);maxRate=std::max(maxRate,rate);backwards+=rate<-.1f;
                const float err=Error(out.yaw,goal);total+=err;peak=std::max(peak,err);++n;}
            previous=out.yaw;
        }
        std::cout<<"noise hz="<<hz<<" speed="<<speed<<" lag mean="<<total/n<<" peak="<<peak<<" rates="<<minRate<<","<<maxRate<<" backwards="<<backwards<<"/"<<n<<"\n";
    }
}

void PredictionHeldSamples() {
    float peak=0;int cases=0;
    for(const auto& pose:poses)for(int repeat:{2,3,4})for(float hz:{60.f,90.f,144.f}) {
        Run r(hz);r.Warm(pose);float measured=0;
        for(int i=0;i<int(hz*4);++i) {
            if(i%repeat==0)measured=90*deg*i/hz;
            const auto out=r.Step(pose,measured,measured);
            if(i>hz*.5f) {
                const float err=Error(out.yaw,measured);peak=std::max(peak,err);
                Check(err<3.1f,"held pose publications repeatedly disabled prediction");
                Check(std::remainder(out.yaw-measured,2*pi)<.001f,"held target was exceeded at its sample time");
            }
        }
        ++cases;
    }
    std::cout<<cases<<" repeated-pose publication trajectories; peak measured-target lag "<<peak<<" deg\n";
}
