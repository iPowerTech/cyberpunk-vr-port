#pragma once
#include <cmath>

namespace cvr::camera {
struct AnchorVector {
    float x{}, y{}, z{};
};

inline AnchorVector ActiveVehicleCameraOffset(AnchorVector configured,bool passengerCombat) {
    return passengerCombat ? AnchorVector{} : configured;
}

inline AnchorVector RotateAnchorYaw(AnchorVector v, float yaw) {
    const float c=std::cos(yaw), s=std::sin(yaw);
    return {c*v.x-s*v.y, s*v.x+c*v.y, v.z};
}

// headResidual and trackingOffset use the recenter/play-space axes; modelOffset
// is published by the skeleton in model axes. They are not the same after the
// native physical body follower contributes yaw to the entity.
inline AnchorVector ComposeAnchorTranslation(AnchorVector headResidual,
                                            AnchorVector trackingOffset,
                                            AnchorVector modelOffset,
                                            float trackingYaw, float modelYaw) {
    const auto room=RotateAnchorYaw({headResidual.x+trackingOffset.x,
                                    headResidual.y+trackingOffset.y,
                                    headResidual.z+trackingOffset.z},trackingYaw);
    const auto body=RotateAnchorYaw(modelOffset,modelYaw);
    return {room.x+body.x,room.y+body.y,room.z+body.z};
}

struct AnchorRecipe {
    AnchorVector trackingOffset{}, modelOffset{};
    float scale{1.0f}, trackingYaw{}, modelYaw{};
    float centreYaw{};
    // On foot the HMD owns the optical centre. Calibration establishes the
    // tracking origin; body yaw changes the avatar's placement about that point.
    bool bodyOrbit{};
};

inline AnchorVector ComposeViewTranslation(AnchorVector residual,const AnchorRecipe& recipe,
                                           float trackingYaw,float bodyYaw) {
    return ComposeAnchorTranslation(residual,recipe.trackingOffset,recipe.modelOffset,
                                    trackingYaw,recipe.bodyOrbit ? recipe.centreYaw:bodyYaw);
}

inline AnchorVector BodyOrbitModel(const AnchorRecipe& recipe,float bodyYaw,float /*trackingYaw*/) {
    if(!recipe.bodyOrbit)return {};
    const auto centre=RotateAnchorYaw(recipe.modelOffset,recipe.centreYaw-bodyYaw);
    return {centre.x-recipe.modelOffset.x,centre.y-recipe.modelOffset.y,0};
}

inline AnchorRecipe MakeAnchorRecipe(AnchorVector manualOffset, AnchorVector vehicleOffset,
                                     AnchorVector bakedOffset, bool inVehicle,
                                     float scale, float trackingYaw, float modelYaw) {
    AnchorRecipe result{};
    result.scale=scale;
    result.trackingYaw=trackingYaw;
    result.modelYaw=modelYaw;
    result.centreYaw=modelYaw;
    result.modelOffset=bakedOffset;
    if (inVehicle) {
        // Seat adjustments retain the vehicle view basis. On foot, the same
        // sliders calibrate camera-to-body alignment, just like the bakes.
        result.trackingOffset={manualOffset.x+vehicleOffset.x,
                               manualOffset.y+vehicleOffset.y,
                               manualOffset.z+vehicleOffset.z};
    } else {
        result.modelOffset={bakedOffset.x+manualOffset.x,
                            bakedOffset.y+manualOffset.y,
                            bakedOffset.z+manualOffset.z};
    }
    return result;
}

inline AnchorVector AbsoluteHorizontalCameraBake(AnchorVector activeBake,
                                                 AnchorVector bodyTarget,
                                                 AnchorVector renderedCamera) {
    // The observation already contains activeBake; replacing it with just the
    // measured difference would undo the previous calibration on the next click.
    return {activeBake.x+bodyTarget.x-renderedCamera.x,
            activeBake.y+bodyTarget.y-renderedCamera.y,0.0f};
}
}
