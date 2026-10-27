-- Select the optical path independently of ADS and of either eye's FOV.
local M = { cameraThreshold = 4.0, imageCoverageDegrees = 80.0 }

function M.select(hasSight, isCollimator, isSniper, magnification)
    if not hasSight then return 'none', 1.0 end
    if isCollimator then return 'collimator', 1.0 end
    if type(magnification) ~= 'number' or magnification ~= magnification
        or magnification == math.huge or magnification < 1.0 then
        return 'none', 1.0
    end
    if isSniper or magnification >= M.cameraThreshold then
        return 'camera', magnification
    end
    return 'shader', magnification
end

function M.cameraFov(magnification)
    local halfCoverage = math.tan(math.rad(M.imageCoverageDegrees * 0.5))
    return math.deg(2.0 * math.atan(halfCoverage / magnification))
end

function M.lowAppearance(magnification)
    -- Authored material variants have 0.01x precision; selected only on an
    -- equipment/stat change, with no mutation of shared native materials.
    return string.format('z%03d', math.max(100, math.min(399, math.floor(magnification * 100 + 0.5))))
end

return M
