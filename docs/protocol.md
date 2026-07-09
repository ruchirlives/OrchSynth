# Orch Faust Synth OSC Protocol

Orch Faust Synth listens on UDP port `9020` by default.

## Supported Messages

### `/orch_faust/load_graph`
Loads a patch graph from JSON.
* **Argument:** `string` (JSON payload)

### `/orch_faust/compile`
Compiles the currently loaded graph into Faust DSP and loads it.
* **Argument:** none

### `/orch_faust/set_param`
Sets a parameter on a node dynamically.
* **Arguments:**
  * `string` (nodeId)
  * `string` (parameter name)
  * `float` (value)

### `/orch_faust/note_on`
Triggers a note on.
* **Arguments:**
  * `float` (pitch - MIDI note number)
  * `float` (velocity - normalized 0.0 to 1.0)

### `/orch_faust/note_off`
Releases a note.
* **Arguments:**
  * `float` (pitch - MIDI note number)

### `/orch_faust/all_notes_off`
Silences all active voices immediately.
* **Argument:** none

### `/orch_faust/status`
Queries the status of the synth (loaded, compiled, compilation error). Returns a status OSC message back.
* **Argument:** none
