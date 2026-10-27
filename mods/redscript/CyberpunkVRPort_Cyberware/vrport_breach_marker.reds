public native func VRWorldMarkerAnchor(widget: ref<inkWidget>, position: Vector4) -> Void;
@addField(gameaiCyberwareBreachGameController) public let vrBreachCanvas: wref<inkWidget>;
@addField(gameaiCyberwareBreachGameController) public let vrBreachAnchor: Vector4;
@addField(gameaiCyberwareBreachGameController) public let vrBreachVisible: Bool;

@addMethod(gameaiCyberwareBreachGameController)
public func VRBreachGeometry() -> array<Vector4> {
  let result: array<Vector4>;
  let scale: Vector2;
  let size: Vector2;
  if IsDefined(this.vrBreachCanvas) {
    scale = this.vrBreachCanvas.GetScale(); size = this.vrBreachCanvas.GetSize();
    ArrayPush(result, new Vector4(scale.X, scale.Y, size.X, size.Y));
    ArrayPush(result, this.vrBreachAnchor);
  };
  return result;
}

@wrapMethod(gameaiCyberwareBreachGameController)
protected cb func OnBreachDataChanged(value: Variant) -> Bool {
  let result = wrappedMethod(value);
  let parameters = FromVariant<ref<BreachUIParameters>>(value);
  let player = GetPlayer(GetGameInstance());
  let breach: wref<BreachComponent>;
  let finder: ref<BreachFinderComponent>;
  let world: Vector4;
  let canvas = inkWidgetRef.Get(this.m_breachCanvasWRef);
  this.vrBreachVisible = IsDefined(parameters) && parameters.tracking && parameters.visible;
  if !IsDefined(canvas) { return result; };
  if IsDefined(parameters) && parameters.tracking && IsDefined(player) {
    finder = player.GetBreachFinderComponent();
    if IsDefined(finder) { breach = finder.GetTrackedBreachComponent(); };
    if IsDefined(breach) { world = breach.GetPosition(); world.W = 1.00; };
  };
  // Scale the complete indicator about its authored pivot, including text.
  // Set an absolute scale: repeated updates must never halve it again.
  canvas.SetScale(new Vector2(0.50, 0.50));
  this.vrBreachCanvas = canvas;
  this.vrBreachAnchor = world;
  VRWorldMarkerAnchor(this.GetRootWidget(), world);
  return result;
}
