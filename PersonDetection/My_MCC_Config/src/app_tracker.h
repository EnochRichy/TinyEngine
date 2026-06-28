/*******************************************************************************
  app_tracker.h -- SORT-lite multi-object tracker for YOLO detections

  Greedy IoU association across frames. Each call to APP_TRK_Update() consumes
  the current frame's list of det_box detections (from yoloOutput.c's
  postprocessing) and updates a fixed slot table of persistent tracks.

  Tracks carry monotonic integer IDs starting at 1, so 0 means "no id" on the
  wire. After TRK_MAX_MISSES consecutive unmatched frames a track is killed
  and its slot freed.

  Light constant-velocity prediction: each track stores its previous box,
  and at the top of APP_TRK_Update the matching pass uses (box + (box -
  prev_box)) as the search region for tracks that were matched on the
  previous frame. This rescues a fast walker across a 1-frame detector
  blink that the static miss-inflation box wouldn't catch.
*******************************************************************************/

#ifndef _APP_TRACKER_H
#define _APP_TRACKER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "TinyEngine/include/yoloOutput.h"   /* det_box */

#define TRK_MAX_TRACKS  8
#define TRK_MAX_MISSES  5        /* ~0.7 s at 7 FPS -- survives a brief detector blink */
#define TRK_IOU_GATE    0.30f

/* Association tolerance. After the IoU pass fails to claim a detection, a
 * second pass matches by center distance: a detection whose center lies
 * within TRK_DIST_GATE_FRAC * avg(box_size) * (1 + 0.15 * miss_count) of
 * an unmatched track's last box is treated as the same person. This rescues
 * a track when the detector blinks for one or two frames and the person
 * keeps moving -- IoU collapses below the gate but the centers are still
 * obviously the same target. Set TRK_DIST_GATE_FRAC <= 0 to disable.
 *
 * TRK_MISS_INFLATE_PER also expands the track's search box for the IoU
 * pass by this fraction per missed frame (e.g. 0.10 -> +10% per miss on
 * each side), so a fast-moving person who keeps going during a detector
 * gap still associates on resume. */
#define TRK_DIST_GATE_FRAC      0.60f
#define TRK_MISS_INFLATE_PER    0.10f

/* Two-stage detection threshold. The detector emits everything above the
 * (lower) DET_VALID_THRESHOLD floor in app_ml.cpp, but only detections at
 * or above TRK_SPAWN_SCORE are allowed to *create* a new track here. Low-
 * confidence detections can still update existing tracks, which keeps a
 * confirmed person alive through a 1-2 frame detector dip without letting
 * background blobs at the same confidence spawn phantom tracks. */
#define TRK_SPAWN_SCORE         0.50f

/* Hit-streak / track confirmation. A track is "confirmed" only after
 * TRK_CONFIRM_HITS consecutive (or near-consecutive) matched frames.
 * Tripwire counting is gated on confirmation, so a one-frame ghost cannot
 * fire a phantom in/out event. At 7 FPS, 3 hits = ~430 ms minimum
 * presence -- well below any genuine human crossing time. */
#define TRK_CONFIRM_HITS        3

/* Box-position EMA smoothing. On a normal match (not just-recovered from
 * a miss), the new box is blended with the previous box as
 *   box = TRK_BOX_EMA_ALPHA * det + (1 - alpha) * box
 * reducing the visible ~1-3 px anchor-decode jitter on the dashboard and
 * making the tripwire deadband more effective. After a miss the old box
 * is stale, so the fresh detection is used unmodified. */
#define TRK_BOX_EMA_ALPHA       0.5f

/* Minimum axis displacement from spawn (camera-space pixels) before a
 * track is allowed to register tripwire counts. Once any frame's axis
 * differs from the track's spawn axis by >= this many pixels, the track
 * is "mobile" and stays mobile until it dies -- subsequent crossings
 * count normally. A track that lingers near the line and barely flips
 * sides never satisfies the gate. Camera-axis range is 0..159 horizontal
 * or 0..119 vertical, so 8 px is ~5% of the wider axis -- well above
 * tripwire-deadband jitter (3 px) and detector jitter (~3 px), but small
 * enough that any deliberate crossing easily clears it. */
#define TRK_MIN_DISPLACEMENT    8

/* Tripwire line-crossing counter config. Coordinates are camera-space
 * (160x120). Change here and rebuild firmware + restart dashboard.
 *   VERTICAL = 0  -> horizontal line at y = POS, IN = top-to-bottom
 *   VERTICAL = 1  -> vertical line   at x = POS, IN = left-to-right
 * DEADBAND is the hysteresis half-width: a track sitting inside
 * [POS-DEADBAND, POS+DEADBAND] holds its previous side, so a person
 * jittering on the line cannot rack up phantom counts. */
#define TRK_TRIPWIRE_VERTICAL   1
#define TRK_TRIPWIRE_POS        80
#define TRK_TRIPWIRE_DEADBAND   3

typedef struct {
    int      id;
    int8_t   active;
    uint8_t  miss_count;
    uint8_t  hits;          /* matched-frame count, saturates at 255.
                             * Confirmed iff hits >= TRK_CONFIRM_HITS. */
    int8_t   prev_side;     /* -1 = before line, +1 = after, 0 = unseeded */
    int8_t   moved_enough;  /* 1 once axis has differed from spawn_axis by
                             * >= TRK_MIN_DISPLACEMENT; sticky for the rest
                             * of the track's life. Gates count emission. */
    int16_t  spawn_axis;    /* camera-space axis at spawn, for the gate */
    det_box  box;           /* last (smoothed) observation in model-pixel space */
    det_box  prev_box;      /* observation one frame before `box`; used to
                             * estimate per-frame velocity for prediction.
                             * Equal to `box` immediately after a spawn or
                             * a coasting-recovery (velocity unknown / stale). */
} track_t;

/* Drop all active tracks and reset the id counter. Not called automatically;
 * static state is zero-initialized at program start. */
void APP_TRK_Reset(void);

/* Match detections to existing tracks (greedy IoU >= TRK_IOU_GATE), age
 * unmatched tracks (kill at TRK_MAX_MISSES), and spawn new tracks for any
 * unmatched detections (silently dropped if slot table is full). */
void APP_TRK_Update(const det_box *detections, int n);

/* Returns the underlying track table. Caller filters by .active. */
const track_t *APP_TRK_GetTracks(int *active_count_out);

/* Cumulative tripwire crossing counts since boot. IN = -1 -> +1 transition,
 * OUT = +1 -> -1 transition. Either pointer may be NULL. */
void APP_TRK_GetCounts(uint32_t *count_in, uint32_t *count_out);

#ifdef __cplusplus
}
#endif

#endif /* _APP_TRACKER_H */
