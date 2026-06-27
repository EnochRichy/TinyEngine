/*******************************************************************************
  app_tracker.c -- SORT-lite tracker implementation (see app_tracker.h).

  Box coordinates flow through as-is in model-input pixel space (0..159 x
  0..127). IoU is dimensionless so the choice of unit doesn't matter; the host
  is responsible for the inverse letterbox before rendering.
*******************************************************************************/

#include "app_tracker.h"
#include "app_cam.h"   /* CROP_*_OFFSET for model->camera coord conversion */

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
 * (160x120). The 128x96 model is a center-crop of the 160x120 camera frame,
 * so model coords are offset by CROP_*_OFFSET from camera coords; we add
 * those back to convert. (For comparison, the 160x128 letterbox config
 * subtracts LETTERBOX_ROW_PAD instead.) */
static inline int box_axis_camera(const det_box *b)
{
    int cx = (int)((b->x0 + b->x1) * 0.5f) + (int)CROP_COL_OFFSET;
    int cy = (int)((b->y0 + b->y1) * 0.5f) + (int)CROP_ROW_OFFSET;
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

/* Grow a box by fraction k on each side around its center (k=0.10 -> +10%).
 * Used to widen a track's search region as miss_count rises so a person who
 * keeps walking during a 1-2 frame detector gap still associates on resume. */
static inline det_box inflate_box(const det_box *b, float k)
{
    float cx = (b->x0 + b->x1) * 0.5f;
    float cy = (b->y0 + b->y1) * 0.5f;
    float hw = (b->x1 - b->x0) * 0.5f * (1.0f + k);
    float hh = (b->y1 - b->y0) * 0.5f * (1.0f + k);
    det_box r;
    r.x0 = cx - hw; r.y0 = cy - hh;
    r.x1 = cx + hw; r.y1 = cy + hh;
    r.score = b->score;
    return r;
}

static inline float center_dist_sq(const det_box *a, const det_box *b)
{
    float dx = ((a->x0 + a->x1) - (b->x0 + b->x1)) * 0.5f;
    float dy = ((a->y0 + a->y1) - (b->y0 + b->y1)) * 0.5f;
    return dx * dx + dy * dy;
}

/* Mean of the four edge lengths -- crude but rotation-free size proxy used
 * to scale the distance gate so a head-sized box and a torso-sized box get
 * proportional tolerance. */
static inline float box_size_avg(const det_box *a, const det_box *b)
{
    return 0.25f * ((a->x1 - a->x0) + (a->y1 - a->y0)
                  + (b->x1 - b->x0) + (b->y1 - b->y0));
}

/* Adopt det as track t's new observation, age-reset, and re-evaluate the
 * tripwire. Shared by both the IoU and center-distance association passes
 * so they behave identically once a (track, det) pair has been picked. */
static void apply_match(int t, const det_box *det_box_p)
{
    s_tracks[t].box        = *det_box_p;
    s_tracks[t].miss_count = 0;

    int     new_axis = box_axis_camera(&s_tracks[t].box);
    int8_t  new_side = side_with_hold(new_axis, s_tracks[t].prev_side);
    if (s_tracks[t].prev_side == -1 && new_side == +1) ++s_count_in;
    else if (s_tracks[t].prev_side == +1 && new_side == -1) ++s_count_out;
    s_tracks[t].prev_side = new_side;
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
     *    pair above the gate. The track's box is inflated proportional to its
     *    miss_count so a track that just survived a 1-2 frame blink still
     *    matches a person who continued moving during the gap.
     *    O(T*D*min(T,D)) <= 8*16*8 = ~1k ops, negligible. */
    for (;;) {
        float best = TRK_IOU_GATE;
        int   bt = -1, bd = -1;
        for (int t = 0; t < TRK_MAX_TRACKS; ++t) {
            if (!s_tracks[t].active || trk_used[t]) continue;
            float   k  = TRK_MISS_INFLATE_PER * (float)s_tracks[t].miss_count;
            det_box sb = (k > 0.0f) ? inflate_box(&s_tracks[t].box, k)
                                    : s_tracks[t].box;
            for (int d = 0; d < n; ++d) {
                if (det_used[d]) continue;
                float v = iou(&sb, &det[d]);
                if (v > best) { best = v; bt = t; bd = d; }
            }
        }
        if (bt < 0) break;
        apply_match(bt, &det[bd]);             /* hard update, no smoothing */
        trk_used[bt] = 1;
        det_used[bd] = 1;
    }

    /* 2. Center-distance fallback: any track still unmatched after the IoU
     *    pass can claim a leftover detection whose center is within
     *    TRK_DIST_GATE_FRAC * avg_box_size * (1 + 0.15*miss_count). This is
     *    the main defense against the detector blinking on/off near the
     *    tripwire -- IoU collapses to 0 across the gap, but the centers
     *    are still obviously the same target. Greedy by closest distance. */
    for (;;) {
        float best_slack = 0.0f;   /* thr^2 - d^2; larger = farther under gate */
        int   bt = -1, bd = -1;
        for (int t = 0; t < TRK_MAX_TRACKS; ++t) {
            if (!s_tracks[t].active || trk_used[t]) continue;
            float miss_scale = 1.0f + 0.15f * (float)s_tracks[t].miss_count;
            for (int d = 0; d < n; ++d) {
                if (det_used[d]) continue;
                float size = box_size_avg(&s_tracks[t].box, &det[d]);
                float thr  = TRK_DIST_GATE_FRAC * size * miss_scale;
                float thr2 = thr * thr;
                float d2   = center_dist_sq(&s_tracks[t].box, &det[d]);
                if (d2 < thr2) {
                    float slack = thr2 - d2;
                    if (slack > best_slack) { best_slack = slack; bt = t; bd = d; }
                }
            }
        }
        if (bt < 0) break;
        apply_match(bt, &det[bd]);
        trk_used[bt] = 1;
        det_used[bd] = 1;
    }

    /* 3. Age unmatched active tracks, kill at the threshold. */
    for (int t = 0; t < TRK_MAX_TRACKS; ++t) {
        if (!s_tracks[t].active || trk_used[t]) continue;
        if (++s_tracks[t].miss_count > TRK_MAX_MISSES) {
            s_tracks[t].active = 0;
        }
    }

    /* 4. Spawn new tracks for unmatched detections (drop if slots are full). */
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
