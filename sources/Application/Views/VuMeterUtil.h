#ifndef _VU_METER_UTIL_H_
#define _VU_METER_UTIL_H_

#include "Application/Mixer/MixerService.h"
#include "BaseClasses/View.h"
#include "System/System/System.h"
#include <math.h>

/**
 * VuMeterUtil: Common utility functions for VU meter rendering.
 * Shared between MixerView and SongView for consistent slew rate decay.
 */

// How far a meter falls per second when the signal drops away, as a
// fraction of the remaining distance. 0.85 per FRAME was the old law,
// and it was wrong in a way that is easy to miss: the fall took a
// fixed number of REPAINTS, so the meter emptied in a fifth of a
// second at 40fps and took two full seconds if the frame rate sagged.
// A meter that changes its ballistics with the load is not a meter.
// Tied to the clock instead, it falls the same way at any frame rate.
// The fraction of the remaining distance still left after one full
// second. The first attempt at this wrote the law the wrong way round
// -- it computed a keep factor of 0.9999 per frame instead of 0.85,
// so the bars rose to the first transient and then stayed there
// forever. A meter that does not come back down is not a meter.
const float VU_KEEP_PER_SEC = 0.0015f;
// ln(VU_KEEP_PER_SEC), so the per-frame factor is one expf and not a
// logf as well
#define VU_LN_KEEP (-6.5023f)

/**
 * VuElapsedMs:
 *   Wall time since the last meter update. One shared clock: only one
 *   view is on screen at a time, so only one of them is stepping the
 *   meters. Coming back to a screen after a while yields a large delta
 *   that UpdateVuBarHeights clamps, which just drops the bars to where
 *   the signal is now -- correct, and what you want to see anyway.
 */
inline int VuElapsedMs() {
    static unsigned long last = 0;
    unsigned long now = System::GetInstance()->GetClock();
    unsigned long d = (last == 0) ? 16 : (now - last);
    last = now;
    return (int)d;
}

/* The bottom of the meter, in dB below full scale.

   The bars were LINEAR in amplitude, which is a scale on which almost
   nothing useful happens: half of the 88 pixels are spent on the top
   6 dB, and everything from a quiet part to a loud one crowds into the
   bottom third. It was survivable only while the tracker had no
   headroom and everything ran at the ceiling. Once the master default
   moved to -10 dB and the sampler stopped being 6 dB hot, a synth at
   full blast drew a tenth of the bar and the meter stopped saying
   anything at all.

   Every real meter is logarithmic for this reason. 48 dB across 88
   pixels is 1.83 px per dB: a 1 dB move is visible, a 10 dB move is a
   fifth of the bar, and the range still reaches quiet enough to show a
   tail decaying.

   Note this also changes the FALL. The decay below runs on pixel
   heights, so on a dB scale it becomes a constant dB-per-second
   fallback -- which is how meters have always behaved, and closer to
   right than the constant fraction-of-amplitude it was before. */
#define VU_FLOOR_DB (-48.0f)

inline int VuLevelToPixels(float peak, int fullScale) {
    if (peak <= 0.0f) return 0;
    // 20*log10 in one logf; log10(x) = logf(x) * 0.4342945f
    float db = 20.0f * 0.4342945f * logf(peak);
    if (db >= 0.0f) return fullScale;
    if (db <= VU_FLOOR_DB) return 0;
    int px = (int)(fullScale * (1.0f - db / VU_FLOOR_DB));
    if (px > fullScale) px = fullScale;
    if (px < 0) px = 0;
    return px;
}

/**
 * UpdateVuBarHeights:
 *   Apply slew rate decay to VU bar heights.
 *   Implements fast attack (instant rise) and slow decay (smooth fall).
 *   elapsedMs is the wall time since the previous update.
 */
inline void UpdateVuBarHeights(int *vuBarHeights, int *displayHeights,
                               float *peakLevels, int numChannels,
                               int elapsedMs,
                               int fullScale = VU_METER_HEIGHT) {
    // keep = VU_KEEP_PER_SEC ^ (elapsed seconds). Called twice a
    // frame, not once a channel, so an expf here costs nothing.
    if (elapsedMs < 1) elapsedMs = 1;
    if (elapsedMs > 250) elapsedMs = 250;
    float keep = expf(VU_LN_KEEP * (elapsedMs * 0.001f));
    if (keep < 0.0f) keep = 0.0f;
    if (keep > 1.0f) keep = 1.0f;

    for (int i = 0; i < numChannels; i++) {
        // Eight steps is enough for a bar drawn out of characters
        // and far too few for one drawn out of 88 pixels: at eight,
        // a peak of 0.99 and a peak of 0.87 are the same bar, and
        // everything between "loud" and "clipping" is one step.
        int newBarHeight = VuLevelToPixels(peakLevels[i], fullScale);

        if (newBarHeight > vuBarHeights[i]) {
            vuBarHeights[i] = newBarHeight;  // Instant rise to peak
        } else {
            float v = vuBarHeights[i] * keep + newBarHeight * (1.0f - keep);
            // integer truncation alone can strand a bar one pixel up
            // forever, because 1*keep truncates back to 1
            int next = (int)v;
            if (next >= vuBarHeights[i] && vuBarHeights[i] > newBarHeight)
                next = vuBarHeights[i] - 1;
            vuBarHeights[i] = next;
        }

        displayHeights[i] = vuBarHeights[i];
    }
}

/**
 * ReadMixBusPeakLevels:
 *   Read stereo L/R peak levels from all 8 mix buses.
 *   Returns 0.0 for all channels when isPlaying is false.
 */
inline void ReadMixBusPeakLevels(bool isPlaying, float *peakLevelsL,
                                  float *peakLevelsR) {
    if (!isPlaying) {
        for (int i = 0; i < 8; i++) {
            peakLevelsL[i] = peakLevelsR[i] = 0.0f;
        }
        return;
    }
    MixerService *ms = MixerService::GetInstance();
    for (int i = 0; i < 8; i++) {
        MixBus *bus = ms->GetMixBus(i);
        if (bus) {
            uint32_t level = bus->GetPeakLevel();
            peakLevelsL[i] = (float)((level >> 16) & 0xFFFF) / 32767.0f;
            peakLevelsR[i] = (float)(level & 0xFFFF) / 32767.0f;
        } else {
            peakLevelsL[i] = peakLevelsR[i] = 0.0f;
        }
    }
}

/**
 * GetVuPeakLevelsStereo:
 *   Apply slew rate decay to stereo (L/R) VU bar heights.
 */
inline void GetVuPeakLevelsStereo(int *vuBarHeightsL, int *vuBarHeightsR,
                                   int *displayHeightsL, int *displayHeightsR,
                                   float *peakLevelsL, float *peakLevelsR,
                                   int elapsedMs) {
    UpdateVuBarHeights(vuBarHeightsL, displayHeightsL, peakLevelsL, 8,
                       elapsedMs);
    UpdateVuBarHeights(vuBarHeightsR, displayHeightsR, peakLevelsR, 8,
                       elapsedMs);
}

/**
 * GetVuBarColor:
 *   Return the color for a given VU bar row (0 = bottom, top = clip).
 */
inline ColorDefinition GetVuBarColor(int row) {
    if (row >= VU_METER_CLIP_LEVEL) {
        return CD_MAJORBEAT;  // Clip level = red
    } else if (row >= VU_METER_WARN_LEVEL) {
        return CD_HILITE2;    // Warning level = orange/yellow
    } else {
        return CD_CONSOLE;    // Normal = cyan
    }
}

/**
 * DrawVuBarRow:
 *   Draw a single row of a VU meter bar.
 *   Uses '=' for filled cells and '-' for empty cells.
 *   Restores rowColor after drawing an empty cell so the caller's color
 *   state is unaffected.
 */
inline void DrawVuBarRow(View *view, GUIPoint pos, int row, int barHeight,
                         GUITextProperties vuProps, ColorDefinition rowColor) {
    if (row < barHeight) {
        view->DrawString(pos._x, pos._y, "=", vuProps);
    } else {
        vuProps.invert_ = false;
        view->SetColor(CD_NORMAL);
        view->DrawString(pos._x, pos._y, "-", vuProps);
        view->SetColor(rowColor);
    }
}

#endif

