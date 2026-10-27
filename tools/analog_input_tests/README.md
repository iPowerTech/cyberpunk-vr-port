# Analog stick input

Build with `cmake -S tools/analog_input_tests -B build/analog-input-tests`, then
`cmake --build build/analog-input-tests --config Release` and
`ctest --test-dir build/analog-input-tests -C Release --output-on-failure`.

The production range mapping is adapted from the user's TE6-testing-highlight
checkout. Five groups exercise centre/outer boundaries, signed half travel,
24,012 monotonic/symmetric samples across deadzone and saturation settings,
raw full-travel gesture thresholds, invalid inputs and fixed/vehicle mode gates.
These are input-mapping checks; they do not measure the game's movement speed.

Two additional groups cover numeric-keypad ownership timeouts, the proportional
cursor curve and continuous panel movement at 30–144 FPS, including gaze loss,
reacquisition, edge reversal and invalid input. `keypad_scope.lua` is a lexical
mock fixture: replace its `-- KEYPAD_MODULE` marker with the production
`modules/keypad_input.lua` before executing it through CET or a Lua interpreter.
