/*******************************************************************************
  app_tracker.c -- SORT-lite tracker implementation (see app_tracker.h).

  Box coordinates flow through as-is in model-input pixel space (0..159 x
  0..127). IoU is dimensionless so the choice of unit doesn't matter; the host
  is responsible for the inverse letterbox before rendering.
*******************************************************************************/

#include "app_tracker.h"
#include "app_cam.h"   /* LETTERBOX_ROW_PAD for model->camera y conversion */

#include <stddef.h>
#include <string.h>

static track_t s_tracks[TRK_MAX_TRACKS];
static int     s_next_id = 1;     /* 0 reserved for "empty slot" on the wire */
static int     s_active_count = 0;

/* Tripwire counters. Increment-only since boot; the host exposes a manual
 * reset elsewhere if needed. uint32_t at 7 FPS, 1 cross/frame would wrap in
 * ~19 years -- in practice unbounded. */
static uint32_t s_count_in  = 0;
static uint32_t s_count_out = 0;

/* Box center along the tripwire's measurement axis, in camera-space
 * (160x120). Model boxes are y-padded by LETTERBOX_ROW_PAD; we strip
 * that for a horizontal line. */
static inline int box_axis_camera(const det_box *b)
{
    int cx = (int)((b->x0 + b->x1) * 0.5f);
    int cy = (int)((b->y0 + b->y1) * 0.5f) - (int)LETTERBOX_ROW_PAD;
#if TRK_TRIPWIRE_VERTICAL
    return cx;
#else
    return cy;
#endif
}

/* Strict side (no deadband). Used to seed prev_side at spawn so the very
 * first real crossing still counts. */
static inline int8_t strict_side(int axis)
{
    return (axis < TRK_TRIPWIRE_POS) ? (int8_t)-1 : (int8_t)+1;
}

/* Deadband-hold side. Inside [POS-DB, POS+DB] we return prev so a person
 * lingering on the line never flips state, which would otherwise let
 * detector jitter accumulate phantom crossings. */
static inline int8_t side_with_hold(int axis, int8_t prev)
{
    if (axis < TRK_TRIPWIRE_POS - TRK_TRIPWIRE_DEADBAND) return -1;
    if (axis > TRK_TRIPWIRE_POS + TRK_TRIPWIRE_DEADBAND) return +1;
    return prev;
}

static inline float iou(const det_box *a, const det_box *b)
{
    float xx1 = a->x0 > b->x0 ? a->x0 : b->x0;
    float yy1 = a->y0 > b->y0 ? a->y0 : b->y0;
    float xx2 = a->x1 < b->x1 ? a->x1 : b->x1;
    float yy2 = a->y1 < b->y1 ? a->y1 : b->y1;
    float bw  = xx2 - xx1; if (bw < 0.f) bw = 0.f;
    float bh  = yy2 - yy1; if (bh < 0.f) bh = 0.f;
    float inter = bw * bh;
    float aa = (a->x1 - a->x0) * (a->y1 - a->y0);
    float bb = (b->x1 - b->x0) * (b->y1 - b->y0);
    float un = aa + bb - inter;
    return un > 0.f ? inter / un : 0.f;
}

void APP_TRK_Reset(void)
{
    memset(s_tracks, 0, sizeof(s_tracks));
    s_next_id = 1;
    s_active_count = 0;
    s_count_in  = 0;
    s_count_out = 0;
}

void APP_TRK_Update(const det_box *det, int n)
{
    /* Cap n to whatever postprocessing could emit (MAX_BOX_PER_CLASS = 10).
     * Stack arrays sized accordingly. */
    uint8_t det_used[16] = {0};
    uint8_t trk_used[TRK_MAX_TRACKS] = {0};

    if (n > 16) n = 16;

    /* 1. Greedy IoU matching: each pass pick the highest-IoU unused (track,det)
     *    pair above the gate. O(T*D*min(T,D)) <= 8*16*8 = ~1k ops, negligible. */
    for (;;) {
        float best = TRK_IOU_GATE;
        int   bt = -1, bd = -1;
        for (int t = 0; t < TRK_MAX_TRACKS; ++t) {
            if (!s_tracks[t].active || trk_used[t]) continue;
            for (int d = 0; d < n; ++d) {
                if (det_used[d]) continue;
                float v = iou(&s_tracks[t].box, &det[d]);
                if (v > best) { best = v; bt = t; bd = d; }
            }
        }
        if (bt < 0) break;
        s_tracks[bt].box        = det[bd];     /* hard update, no smoothing */
        s_tracks[bt].miss_count = 0;

        /* Tripwire check: the box just moved, so re-evaluate which side of
         * the line this track is on. A sign flip vs prev_side = one crossing.
         * Only ±1 -> ∓1 counts; transitions involving 0 (unseeded) don't,
         * but spawn() always seeds prev_side strictly so 0 shouldn't occur
         * here in normal flow. */
        int     new_axis = box_axis_camera(&s_tracks[bt].box);
        int8_t  new_side = side_with_hold(new_axis, s_tracks[bt].prev_side);
        if (s_tracks[bt].prev_side == -1 && new_side == +1) ++s_count_in;
        else if (s_tracks[bt].prev_side == +1 && new_side == -1) ++s_count_out;
        s_tracks[bt].prev_side = new_side;

        trk_used[bt] = 1;
        det_used[bd] = 1;
    }

    /* 2. Age unmatched active tracks, kill at the threshold. */
    for (int t = 0; t < TRK_MAX_TRACKS; ++t) {
        if (!s_tracks[t].active || trk_used[t]) continue;
        if (++s_tracks[t].miss_count > TRK_MAX_MISSES) {
            s_tracks[t].active = 0;
        }
    }

    /* 3. Spawn new tracks for unmatched detections (drop if slots are full). */
    for (int d = 0; d < n; ++d) {
        if (det_used[d]) continue;
        for (int t = 0; t < TRK_MAX_TRACKS; ++t) {
            if (!s_tracks[t].active) {
                s_tracks[t].id         = s_next_id++;
                s_tracks[t].active     = 1;
                s_tracks[t].miss_count = 0;
                s_tracks[t].box        = det[d];
                /* Seed strictly (ignore deadband) so a track that happens
                 * to spawn on the line still has a definite side -- the
                 * next genuine crossing will then count. */
                s_tracks[t].prev_side  = strict_side(box_axis_camera(&det[d]));
                break;
            }
        }
    }

    s_active_count = 0;
    for (int t = 0; t < TRK_MAX_TRACKS; ++t)
        if (s_tracks[t].active) ++s_active_count;
}

const track_t *APP_TRK_GetTracks(int *active_count_out)
{
    if (active_count_out) *active_count_out = s_active_count;
    return s_tracks;
}

void APP_TRK_GetCounts(uint32_t *count_in, uint32_t *count_out)
{
    if (count_in)  *count_in  = s_count_in;
    if (count_out) *count_out = s_count_out;
}
