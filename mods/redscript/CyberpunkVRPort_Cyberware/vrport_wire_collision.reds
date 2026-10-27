public native func VRCyberwareWirePlanes(side: Int32, epoch: Int32, planes: array<Vector4>) -> Void;
public native func VRCyberwareSelect(kind: Int32) -> Void;
public native func VRCyberwareCollisionInfo() -> Vector4;
@addField(PlayerPuppet) public let vrWireRayHits: Int32;
@addField(PlayerPuppet) public let vrWireRayTests: Int32;
@addField(PlayerPuppet) private let vrWireRayPreset: Int32;
@addField(PlayerPuppet) private let vrWirePlanesLeft: array<Vector4>;
@addField(PlayerPuppet) private let vrWirePlanesRight: array<Vector4>;

@addMethod(PlayerPuppet)
private func VrWireTrace(queries: ref<SpatialQueriesSystem>, start: Vector4, end: Vector4, out trace: TraceResult) -> Bool {
  let attempt: Int32;
  let index: Int32;
  let preset: CName;
  while attempt < 3 {
    index = (this.vrWireRayPreset + attempt) % 3;
    switch index {
      case 0: preset = n"Bullet"; break;
      case 1: preset = n"Camera"; break;
      default: preset = n"Sight Blocker";
    };
    this.vrWireRayTests += 1;
    if queries.SyncRaycastByQueryPreset(start, end, preset, trace, true, false) {
      this.vrWireRayPreset = index;
      return true;
    };
    attempt += 1;
  };
  return false;
}

@addMethod(PlayerPuppet)
public func VRCyberwareCollideWire(side: Int32, epoch: Int32, points: array<Vector4>, previous: array<Vector4>, dt: Float) -> Void {
  let queries = GameInstance.GetSpatialQueriesSystem(this.GetGame());
  let planes: array<Vector4>;
  let previousPlanes = side == 0 ? this.vrWirePlanesLeft : this.vrWirePlanesRight;
  let trace: TraceResult;
  let start: Vector4;
  let end: Vector4;
  let delta: Vector4;
  let point: Vector4;
  let normal: Vector4;
  let hit: Bool;
  let i: Int32;
  let contact: Vector4;
  let oldNormal: Vector4;
  let separation: Float;
  if ArraySize(points) != 46 || !IsDefined(queries) { return; };
  while i < ArraySize(points) {
    point = new Vector4(0.00, 0.00, 0.00, 0.00); normal = new Vector4(0.00, 0.00, 0.00, 0.00); hit = false;
    if i > 0 {
      start = points[0]; end = points[i];
      if ArraySize(previous) == ArraySize(points) {
        start = previous[i]; delta = points[i] - previous[i];
        end = points[i] + delta * MinF(0.04 / MaxF(dt, 0.001), 2.00);
        // Start outside the last contacted surface even if the rope constraint
        // solve moved the node a few millimetres behind it in the last frame.
        if ArraySize(previousPlanes) == 92 && previousPlanes[i*2].W > 0.50 {
          contact = previousPlanes[i*2]; oldNormal = previousPlanes[i*2 + 1];
          contact.W = 1.00;
          separation = Vector4.Dot(start - contact, oldNormal);
          if separation < 0.02 { start += oldNormal * (0.02 - separation); };
        };
      };
      if Vector4.Distance(start, end) > 0.002 {
        hit = this.VrWireTrace(queries, start, end, trace);
      };
      if !hit {
        start = points[i] + new Vector4(0.00, 0.00, 0.035, 0.00);
        end = points[i] - new Vector4(0.00, 0.00, 0.07, 0.00);
        hit = this.VrWireTrace(queries, start, end, trace);
      };
      if hit {
        this.vrWireRayHits += 1;
        point = new Vector4(trace.position.X, trace.position.Y, trace.position.Z, 1.00);
        normal = new Vector4(trace.normal.X, trace.normal.Y, trace.normal.Z, 0.00);
      };
    };
    ArrayPush(planes, point); ArrayPush(planes, normal);
    i += 1;
  };
  if side == 0 { this.vrWirePlanesLeft = planes; } else { this.vrWirePlanesRight = planes; };
  VRCyberwareWirePlanes(side, epoch, planes);
}
