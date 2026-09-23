#pragma once

namespace batida
{

// Stereo-linked feed-forward compressor with a soft knee. Amount (0..1) lowers
// the threshold (0 → -36 dB) and raises the ratio (1:1 → 10:1) together. The
// detector listens to the chain's own signal or to an external key (the
// sidechain input).
//
// Make-up gain is adaptive: it compares the loudness going in and coming out
// over ~1.5 s and restores it, so the amount changes density rather than level,
// without pumping. With an external key (ducking) there is no make-up.
class Compressor
{
public:
    void prepare (double sampleRate);
    void reset();
    void setTimes (float attackMs, float releaseMs);

    // Returns the gain to apply for this sample, given the detector level.
    // Make-up gain suits compressing the signal itself; for an external key
    // (ducking) it is left out.
    float gainFor (float detectorLevel, float amount);

    // Slow loudness match for one stereo sample: pass the input and the
    // compressed signal energy (L² + R²); returns the make-up gain.
    float makeupFor (float inputEnergy, float compressedEnergy);

    float getGainReductionDb() const { return reductionDb; }

    // Static curve (no envelope, no make-up), for tests and the UI.
    static float staticGainDb (float inputDb, float amount);

private:
    double sampleRate = 48000.0;
    float attackCoef = 0.0f, releaseCoef = 0.0f;
    float envelope = 0.0f;
    float reductionDb = 0.0f;
    float makeupCoef = 0.0f, meanIn = 0.0f, meanOut = 0.0f;
};

} // namespace batida
