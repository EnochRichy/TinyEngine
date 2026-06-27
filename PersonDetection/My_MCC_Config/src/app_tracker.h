/*******************************************************************************
  app_tracker.h -- SORT-lite multi-object tracker for YOLO detections

  Greedy IoU association across frames. Each call to APP_TRK_Update() consumes
  the current frame's list of det_box detections (from yoloOutput.c's
  postprocessing) and updates a fixed slot table of persistent tracks.

  Tracks carry monotonic integer IDs starting at 1, so 0 means "no id" on the
  wire. After TRK_MAX_MISSES consecutive unmatched frames a track is killed
  and its slot freed.

  No Kalman filter, no velocity prior: at ~3 FPS the linear-motion prediction
  helps little, and a person moving 1 m/s already covers >50% of a head-sized
  box per frame -- pure IoU on the last observation is the most reliable
  signal.
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

/* Tripwire line-crossing counter config. Coordinates are camera-space
 * (160x120). Change here and rebuild firmware + restart dashboard.
 *   VERTICAL = 0  -> horizontal line at y = POS, IN = top-to-bottom
 *   VERTICAL = 1  -> vertical line   at x = POS, IN = left-to-right
 * DEADBAND is the hysteresis half-width: a track sitting inside
 * [POS-DEADBAND, POS+DEADBAND] holds its previous side, so a person
 * jittering on the line cannot rack up phantom counts. */
#define TRK_TRIPWIRE_VERTICAL   1
#define TRK_TRIPWIRE_POS        90
#define TRK_TRIPWIRE_DEADBAND   3

typedef struct {
    int      id;
    int8_t   active;
    uint8_t  miss_count;
    int8_t   prev_side;     /* -1 = before line, +1 = after, 0 = unseeded */
    det_box  box;
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
