# DSP Notes

## HackRF int8 Normalization

HackRF returns signed `int8` IQ samples in `[-128, 127]`.

- `sample / 128.0f` maps to `[-1.0, 0.9921875]`
- `sample / 127.0f` maps to `[-1.007874, 1.0]`

Both are commonly used. In this project we keep `/128.0f` because:

- it guarantees no positive overflow above `+1.0`
- it keeps DSP blocks stable and avoids accidental clipping in later stages
- asymmetry is small and does not materially degrade FM demod quality

If strict symmetry is needed, `/127.0f` can be used with an explicit clamp.

## Spectrum Scale

Current spectrum is normalized to dBFS (FFT size + Hann coherent gain compensation):

`dBFS = 20 * log10(|FFT| / (N * 0.5))`

This makes visual levels more stable when changing `fftSize`.

## Detection and RBW

Detection tolerance should scale with RBW:

- `RBW = sample_rate / fft_size`
- merge tolerance is adaptively expanded from RBW to avoid over-fragmenting wide bins

Sub-bin peak interpolation (parabolic) is used to reduce one-bin frequency bias.
