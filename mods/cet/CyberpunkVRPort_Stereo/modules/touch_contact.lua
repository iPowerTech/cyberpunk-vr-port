-- Metres from the actual display surface, not trigger/confirm button state.
local C = { press=.004, release=.025, through=-.025, maxGap=.10 }
C.dropPoint={press=.10,release=.13}

function C.reset(s)
    s.armed=false;s.down=false;s.previous=nil;s.target=nil
end

function C.step(s, sample, target, dt, profile)
    if not sample or sample.w<.5 or dt<=0 or dt>C.maxGap or sample.z<C.through then
        C.reset(s);return false
    end
    local d=sample.z
    local press=profile and profile.press or C.press
    local release=profile and profile.release or C.release
    if d>=release then s.armed=true;s.down=false;s.target=nil end
    local click=s.armed and not s.down and s.previous and s.previous>press and d<=press
    s.previous=d
    if click then
        -- Even empty/disabled glass consumes this approach. Sliding a finger
        -- already inside the screen onto another button is not a new press.
        s.armed=false;s.down=true;s.target=target
        return target~=nil
    end
    return false
end
return C
