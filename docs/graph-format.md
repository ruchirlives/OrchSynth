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

### Advanced / Physical Modeling
* `karplus_string`: Karplus-Strong string generator. Parameters: `freq`, `damping`.
* `body_resonator`: Wood/body plate resonator. Parameters: `size`, `brightness`.
* `reverb`: Stereo reverberator. Parameters: `size`, `damp`.
