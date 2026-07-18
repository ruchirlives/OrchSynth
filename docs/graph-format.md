# Orch Faust Synth Graph Format

Patches are defined as a Directed Acyclic Graph (DAG) of DSP nodes and connections.

## Schema
See [orchfaust.graph.schema.json](../schemas/orchfaust.graph.schema.json).

## Node Types

### Sources
* `sine`: Sine oscillator. Parameters: `freq`.
* `saw`: Sawtooth oscillator. Parameters: `freq`.
* `square`: Square oscillator. Parameters: `freq`.
* `noise`: White noise generator. No parameters.

### Filters & Processors
* `lowpass`: 2nd-order lowpass filter. Parameters: `cutoff`.
* `highpass`: 2nd-order highpass filter. Parameters: `cutoff`.
* `gain`: Multiplies the input signal. Parameters: `gain`.
* `delay`: Delay line. Parameters: `delay` (in seconds).

### Envelope & Modulators
* `adsr`: Envelope generator. Parameters: `attack`, `decay`, `sustain`, `release`.
* `lfo`: Low frequency oscillator. Parameters: `freq`.
* `mixer`: Sums multiple inputs. No parameters.

### Performance Inputs
Performance inputs expose the highest-resolution VST3 value supplied by the host. MIDI 1
and MIDI 2 sources therefore use the same graph nodes; MIDI 1 values simply have fewer
distinct source steps.

* `velocity`: Note-on velocity, normalized to 0..1.
* `release_velocity`: Note-off velocity, normalized to 0..1.
* `aftertouch`: Per-note pressure when available, otherwise channel pressure.
* `note_pressure`: Per-note pressure only.
* `channel_pressure`: Channel-wide pressure only.
* `pitch_bend`: Channel pitch bend multiplied by the node's `range` in semitones.
* `note_pitch`: Per-note tuning/pitch expression in semitones.
* `channel_pitch_bend`: Channel pitch bend multiplied by `range` in semitones.
* `timbre`: Per-note brightness/timbre, normalized to 0..1.
* `expression`: Per-note expression, normalized to 0..1.
* `note_number`: The MIDI key number normalized to `0..1` (`note / 127`) for graph modulation.
* `note_frequency`: Final voice frequency after note tuning and pitch expression.
* `gate`: The current voice gate.
* `cc`: Channel controller selected by the `cc` parameter, normalized to 0..1.
* `debug`: Transparent runtime signal probe; its input is passed through unchanged and displayed in the editor.

### Advanced / Physical Modeling
* `karplus_string`: Karplus-Strong string generator. Parameters: `freq`, `damping`.
* `body_resonator`: Wood/body plate resonator. Parameters: `size`, `brightness`.
* `reverb`: Stereo reverberator. Parameters: `size`, `damp`.
