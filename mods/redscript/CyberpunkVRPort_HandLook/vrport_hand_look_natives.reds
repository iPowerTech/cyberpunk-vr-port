// CyberpunkVRPort -- the probe native, and it lives here for one reason only.
//
// THIS FILE INTENTIONALLY HAS NO `module` LINE. A native declared inside a module is looked up by its
// MODULE-QUALIFIED name, while the plugin registers a plain global -- so declaring it next to the code
// that calls it makes redscript refuse the whole file. The same split is already documented in
// vrport_device_natives.reds and in vrport_no_anims.reds; this is that fix copied rather than
// rediscovered. A global func stays callable from inside a module, so nothing else has to move.
//
// WHAT IT IS FOR. Adding to one of four counters the camera panel prints, so an experiment written in
// redscript can say whether it ran at all. "It looked the same" is not an answer when a wrap that
// never fired and a wrap that fired and changed nothing produce the same picture.

native func SetVRProbe(idx: Int32, value: Float) -> Int32;
