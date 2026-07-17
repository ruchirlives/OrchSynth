# Convolution Node Implementation Plan

## Scope

Add a `convolution` node that loads a WAV impulse response and applies it as a
native post-mix stereo effect. The existing patch compiler emits one complete
Faust DSP program, so this first version intentionally does not support an
arbitrary in-graph convolution insert. The node is represented in the graph
and UI, but its signal input/output ports are disabled because it processes the
final rendered output.

## Design

1. Vendor the permissively licensed FFTConvolver (MIT) and dr_wav (MIT-0 or
   public domain) sources, with their notices in `THIRD_PARTY_LICENSES.txt`.
2. Add a native `ConvolutionProcessor` with independent left/right partitioned
   convolvers, no allocation or file I/O in `process`, wet/dry mix, output gain,
   and a bounded IR duration.
3. Extend graph nodes with string parameters and preserve `ir_path` through
   graph parsing, editor serialization, presets, and MCP normalization.
4. On graph load, configure the native post-mix effect from the single enabled
   convolution node. Invalid paths or WAV files leave the effect bypassed and
   report an error without breaking the Faust graph.
5. Add the editor node with path entry, Browse control, wet, and gain controls.
6. Test WAV decoding, identity convolution, dry/wet behavior, graph parsing,
   UI production build, VST build, and the Steinberg validator.

## Body Convolution Flow Node

`body_convolution` is a distinct, insertable signal-flow node for instrument
bodies. It loads a mono, normalized WAV IR (up to 4096 samples after resampling)
when the graph is compiled and emits a Faust `fi.convN` FIR directly in the
generated graph. This provides per-voice body resonance in the expected place
in the flow without making a long room IR part of every voice. The existing
`convolution` node remains the shared post-mix, partitioned FFT room effect.

## Follow-up

Support for convolution at an arbitrary graph location requires splitting the
current single Faust program into native/Faust processing segments. That is a
separate graph-runtime change and is deliberately excluded from this release.
