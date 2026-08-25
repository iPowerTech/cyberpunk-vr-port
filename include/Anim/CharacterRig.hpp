#pragma once

// The solve for the player's own bones, and the shared-block reader that feeds it. Declared here so
// the pose-apply detour in src/Hooks/AnimPose.cpp can call them without either file including the
// other's implementation.
//
// Signatures are copied from the definitions, never retyped.

#include "Anim/VrikState.hpp"

#include <cstdint>
#include <cstddef>
#include <atomic>

// ONE RENDERED FRAME, COUNTED INSIDE THIS DLL. Bumped once per Present (src/Runtimes/
// OpenXRPresent.cpp), read by the solve as the key its cache is valid for.
//
// It replaces g_pSharedHands[99], which was pushed from CET Lua's onUpdate through the shared block.
// That made the arms' freshness depend on a Lua callback: a fresh solve happened only when Lua got
// round to advancing the counter, and every pass in between replayed the previous pose. Two thread
// hops and a script scheduler stood between a hand moving and the arm following it, which is what
// "VRIK looks laggy" was.
//
// WHY A FRAME AND NOT THE XR SAMPLE. The engine applies the player pose 4-5 times per rendered frame,
// and every pass must leave the buffer identical or mid-frame consumers (shadow, reflection, the
// render snapshot) catch two different arm positions inside one image -- measured, and it looked like
// a second arm on snap turns. Keying on the XR publish (72/s against ~52 frames) would reintroduce
// exactly that. One epoch per frame keeps the passes coherent while taking the script out of the path.
extern std::atomic<uint32_t> g_VrikFrameEpoch;

// Peak second difference per hand, per stage (0 ctrl as the solve reads it, 1 anchor, 2 target,
// 3 the same controller sampled uniformly on the XR thread), in mm. See the definition in
// src/Anim/CharacterRig.cpp -- stage 3 against stage 0 is what separates aliasing from noise.
// Peak mm the engine had moved our bones before we wrote them back -- the width of the
// window in which the skeleton is not ours. See src/Hooks/AnimPose.cpp.
extern int   g_VrikOverwritePassHist[8];
extern int   g_VrikBatchGapNew;
extern int   g_VrikBatchGapSame;
extern float g_VrikBatchGapNewMin;
extern float g_VrikBatchGapSameMax;
extern float g_VrikEngineOverwriteMm;
extern int   g_VrikEngineOverwriteBone;
extern int   g_VrikEngineOverwriteHits;
extern int   g_VrikEngineOverwritePasses;
extern float g_VrikShakePeakMm[2][4];   // any motion
extern float g_VrikShakeSlowMm[2][4];   // slow motion only -- the number that answers the report
extern int   g_VrikShakeSlowN[2][4];
void VRIK_NoteShake(int hand, int stage, const float* pos);

extern bool  g_viewPktValid;
extern float g_fkRot[VRIK_MAX_BONES][4];
extern float g_solveCacheSnapCtr;
extern uint32_t g_solveCacheTick;
extern float g_solveCacheVal[96][7];
extern float g_solveCacheYaw;
extern float g_viewPkt[17];
extern float s_vrSharedSquatDrop;
extern int   g_solveCacheIdx[96];
extern int   g_solveCacheN;
extern uint32_t g_handsStableSeq;
// outEntityQuat receives the exact world->model basis source used for outPos.  Keeping it with the
// result prevents callers from converting another world-space offset with a newer entity yaw.
// outPairedRot receives the camera rotation from that same camera/entity publication: the stable
// base frame expected by the existing controller-packet composition.  outRot may be replaced by
// the fresh XR sample for the head bone and must not then be used with controller-packet data.
bool VRIK_ComputeCamModel(float* outPos, float* outRot, float* outEntityQuat = nullptr,
                          float* outPairedRot = nullptr);
bool VRIK_ResolveViewPos(float out[3]);
float VRIK_Dot3(const float* a, const float* b);
float VRIK_Norm3(float* v);
int VRIK_FKCount();
void VRIK_ApplyHandStop(int side, float* target, float* handRot);
void VRIK_BodyAxesFromCamYaw(const float* camModelRot, float* bodyRight, float* bodyUp, float* bodyFwd);
void VRIK_ComputeFK(uint8_t* boneBuf, int count);
void VRIK_Cross3(const float* a, const float* b, float* o);
void VRIK_DampenTorsoWeaponPose(uint8_t* boneBuf);
void VRIK_LatchViewPacket();
void VRIK_PinGirdleTranslations(uint8_t* boneBuf);
void VRIK_PlaceBodyUnderHMD(uint8_t* boneBuf, const float* camModelPos, const float* camModelRot, int headIdx, const float* bodyFwd);
void VRIK_QuatFromTo(const float* a, const float* b, float* o);
void VRIK_QuatScale(const float* q, float t, float* o);
void VRIK_RemapAxis(int preset, const float* v, float* o);
void VRIK_ScaleArmBonesFromRest(uint8_t* boneBuf, uintptr_t trackBuf, int boneCount, int foreIdx, int handIdx, float userArmLen);
// The inverse, for an arm handed to the driving animation: the rig's own rest translations back, the
// captured shoulder protraction rest included. See the definition for why the engine will not do it.
void VRIK_RestoreArmRestTrans(uint8_t* boneBuf, uintptr_t trackBuf, int boneCount, int upperIdx, int foreIdx, int handIdx, bool isLeft);
void VRIK_SolveArm(uint8_t* boneBuf, int upperIdx, int foreIdx, int handIdx, const float* targetModel, const float* handModelRot, const float* bodyRight, const float* bodyUp, const float* bodyFwd, float poleAngleRad, float swingGain, bool isLeft, bool storeDbg);
void VRIK_TwistAbout(const float* q, const float* a, float* outT);
void VRIK_WriteLocalPos(uint8_t* boneBuf, int idx, const float* parentModelPos, const float* parentModelRot, const float* modelPos);
void VRIK_WriteLocalRot(uint8_t* boneBuf, int idx, const float* parentModelRot, const float* modelRot);
void VRIK_WriteHand(uint8_t* boneBuf, int bIdx, const float* headPos, const float* headQuat, bool headOk, const float* vrPos, const float* vrQuat, bool writeRot);

// Copied from the definition. The generator's pattern let a greedy character class swallow the
// name itself, so this one came back "not found" three passes in a row -- a reminder that a
// generator is only worth what its failures cost to notice.
void VRIK_BuildHandTarget(const float* shoulderModelPos,
                          const float* calibratedShoulderBody,
                          const float* hmdRel,
                          const float* vrPos, const float* vrQuat,
                          const float* wristCorr,
                          float scale,
                          const float* off,
                          float* outTarget, float* outHandRot);
