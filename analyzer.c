/* paperwidget spectrum analyzer — PulseAudio + FFTW
 * Captures audio from the default PulseAudio sink's monitor (desktop output),
 * performs 2048-point FFT, outputs 8 frequency band values as
 * newline-delimited JSON to stdout.
 *
 * Usage: ./analyzer
 *   Automatically finds the default sink's monitor source.
 *
 * Exit codes: 0 = normal (user quit), 1 = no device, 2 = audio error.
 * All errors go to stderr; only band data goes to stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <fftw3.h>

#define SAMPLE_RATE 48000
#define FFT_SIZE    2048
#define RING_SIZE   (FFT_SIZE * 2)
#define NUM_BANDS   8

static volatile int running = 1;

static float ring[RING_SIZE];
static int ring_w = 0, ring_count = 0;

static float window[FFT_SIZE];
static fftwf_complex fft_in[FFT_SIZE];
static fftwf_complex fft_out[FFT_SIZE];
static fftwf_plan fft_plan;

static double prev_band[NUM_BANDS];

static pa_simple *pa = NULL;

/* 8 bands: sub-bass → air, each perceptually distinct */
static const double band_edges[NUM_BANDS + 1] = {
    20, 100, 300, 600, 1200, 3000, 6000, 12000, 20000
};

static void on_signal(int sig) { (void)sig; running = 0; }

static void init_fft(void) {
    for (int i = 0; i < FFT_SIZE; i++)
        window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));
    fft_plan = fftwf_plan_dft_1d(FFT_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_MEASURE);
}

/* ---- PulseAudio context: find default sink's monitor source ---- */

static char found_monitor[512] = {0};
static char default_sink[512] = {0};

static void server_info_cb(pa_context *c, const pa_server_info *info, void *ud) {
    (void)c; (void)ud;
    if (info && info->default_sink_name)
        snprintf(default_sink, sizeof(default_sink), "%s", info->default_sink_name);
}

static void sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *ud) {
    (void)c; (void)ud;
    if (!eol && info && info->monitor_source_name)
        snprintf(found_monitor, sizeof(found_monitor), "%s", info->monitor_source_name);
}

static int wait_op(pa_mainloop *loop, pa_context *ctx, pa_operation *op) {
    if (!op) return 1;
    int fail = 0;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        if (!running || pa_context_get_state(ctx) != PA_CONTEXT_READY ||
            pa_mainloop_iterate(loop, 1, NULL) < 0) {
            pa_operation_cancel(op); fail = 1; break;
        }
    }
    if (pa_operation_get_state(op) != PA_OPERATION_DONE) fail = 1;
    pa_operation_unref(op);
    return fail;
}

static int find_monitor_source(void) {
    int result = 1;
    pa_mainloop *loop = pa_mainloop_new();
    if (!loop) return 1;
    pa_context *ctx = pa_context_new(pa_mainloop_get_api(loop), "paperwidget-find-monitor");
    if (!ctx) goto cleanup;
    if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) goto cleanup;

    while (pa_context_get_state(ctx) != PA_CONTEXT_READY) {
        if (!running || !PA_CONTEXT_IS_GOOD(pa_context_get_state(ctx)) ||
            pa_mainloop_iterate(loop, 1, NULL) < 0) goto cleanup;
    }

    if (wait_op(loop, ctx, pa_context_get_server_info(ctx, server_info_cb, NULL)) ||
        !default_sink[0]) goto cleanup;

    if (wait_op(loop, ctx,
            pa_context_get_sink_info_by_name(ctx, default_sink, sink_info_cb, NULL)))
        goto cleanup;

    if (!found_monitor[0]) {
        snprintf(found_monitor, sizeof(found_monitor), "%s.monitor", default_sink);
        fprintf(stderr, "Trying default sink monitor: %s\n", found_monitor);
    } else {
        fprintf(stderr, "Found monitor source: %s\n", found_monitor);
    }
    result = 0;

cleanup:
    if (result) fprintf(stderr, "Cannot resolve default output monitor\n");
    if (ctx) { pa_context_disconnect(ctx); pa_context_unref(ctx); }
    pa_mainloop_free(loop);
    return result;
}

/* ---- End PA context ---- */

static int init_pa(const char *source_name) {
    pa_sample_spec ss = { .format = PA_SAMPLE_FLOAT32LE, .rate = SAMPLE_RATE, .channels = 1 };
    pa_buffer_attr ba = {
        .fragsize = FFT_SIZE * sizeof(float),
        .maxlength = RING_SIZE * sizeof(float),
        .tlength = (uint32_t)-1, .prebuf = (uint32_t)-1, .minreq = (uint32_t)-1
    };
    int error;
    pa = pa_simple_new(NULL, "paperwidget-spectrum", PA_STREAM_RECORD,
                       source_name, "PaperWidget Spectrum", &ss, NULL, &ba, &error);
    if (!pa) { fprintf(stderr, "pa_simple_new failed: %s\n", pa_strerror(error)); return 1; }
    fprintf(stderr, "PulseAudio connected (source: %s)\n",
            source_name ? source_name : "default");
    return 0;
}

static void process_frame(void) {
    if (ring_count < FFT_SIZE) return;

    int pos = (ring_w - FFT_SIZE + RING_SIZE) % RING_SIZE;
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_in[i][0] = ring[(pos + i) % RING_SIZE] * window[i];
        fft_in[i][1] = 0.0f;
    }
    fftwf_execute(fft_plan);

    /* Accumulate power into bands */
    double band_sum[NUM_BANDS] = {0};
    int band_count[NUM_BANDS] = {0};

    for (int i = 1; i < FFT_SIZE / 2; i++) {
        double freq = (double)i * SAMPLE_RATE / FFT_SIZE;
        double power = fft_in[i][0] * fft_in[i][0] + fft_in[i][1] * fft_in[i][1];
        for (int b = 0; b < NUM_BANDS; b++) {
            if (freq >= band_edges[b] && freq < band_edges[b + 1]) {
                band_sum[b] += power;
                band_count[b]++;
                break;
            }
        }
    }

    /* Compute RMS per band, apply fixed gain + power-law compression.
     * NO per-frame max normalization — each band is independent. */
    double gain = 100.0;
    for (int b = 0; b < NUM_BANDS; b++) {
        double raw = 0;
        if (band_count[b] > 0)
            raw = sqrt(band_sum[b] / band_count[b]);
        double val = pow(raw * gain, 0.4);
        if (val > 1.0) val = 1.0;
        if (val < 0.0) val = 0.0;

        /* EMA smoothing: fast attack, slow release */
        double c = (val > prev_band[b]) ? 0.45 : 0.15;
        prev_band[b] += c * (val - prev_band[b]);
    }

    printf("{\"bands\":[");
    for (int b = 0; b < NUM_BANDS; b++) {
        printf("%.4f", prev_band[b]);
        if (b < NUM_BANDS - 1) printf(",");
    }
    printf("]}\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    init_fft();
    memset(prev_band, 0, sizeof(prev_band));

    if (find_monitor_source() != 0) { fftwf_destroy_plan(fft_plan); return 1; }

    if (init_pa(found_monitor) != 0) { fftwf_destroy_plan(fft_plan); return 1; }

    fprintf(stderr, "Analyzer started\n");

    float buf[FFT_SIZE];
    int error;
    while (running) {
        if (pa_simple_read(pa, buf, sizeof(buf), &error) < 0) {
            fprintf(stderr, "pa_simple_read failed: %s\n", pa_strerror(error));
            break;
        }
        for (int i = 0; i < FFT_SIZE; i++) {
            ring[ring_w] = buf[i];
            ring_w = (ring_w + 1) % RING_SIZE;
            if (ring_count < RING_SIZE) ring_count++;
        }
        if (ring_count >= FFT_SIZE) process_frame();
    }

    pa_simple_free(pa);
    fftwf_destroy_plan(fft_plan);
    return 0;
}
