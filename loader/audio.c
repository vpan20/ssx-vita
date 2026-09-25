// Audio bridge: game -> SceAudioOut.  See audio.h for the mechanism.
//
// Design: a single-producer/single-consumer ring buffer of int16 samples.
//   Producer = SubmitAudio_hook, called on the engine's audio thread.
//   Consumer = a dedicated Vita thread that drains fixed GRAIN-sized chunks
//              into a SceAudioOut port (sceAudioOutOutput blocks, which paces us).
// The ring decouples the engine's arbitrary submit sizes from the Vita's fixed
// output grain. On overrun the newest submit is dropped (brief glitch) rather
// than blocking the engine thread; on underrun we output silence.

#include <vitasdk.h>
#include <string.h>
#include <stdio.h>
#include "audio.h"
#include "so_util.h"

extern void debugPrintf(const char *fmt, ...);

// ---- Format. The loader inits EAAudioCore at 44100 / 16-bit / stereo; match it.
// If the game turns out to submit mono or a different rate, change these (and see
// the note in CALIBRATION.md about reading them back from AndroidEAAudioCoreJni::Init).
#define AUDIO_FREQ      44100
#define AUDIO_CHANNELS  2
#define AUDIO_GRAIN     1024                      // frames per SceAudioOut block (64..2048, mult of 64)
#define CHUNK_SHORTS    (AUDIO_GRAIN * AUDIO_CHANNELS)

// ---- SPSC ring. Size is a power of two so indices mask cleanly. ~0.75s @44.1k stereo.
#define RING_SHORTS     (1u << 16)                // 65536 shorts
#define RING_MASK       (RING_SHORTS - 1u)

static int16_t   s_ring[RING_SHORTS];
static volatile uint32_t s_w;                     // producer cursor (monotonic, wraps at 2^32)
static volatile uint32_t s_r;                     // consumer cursor
static int       s_started = 0;
static uint32_t  s_drops   = 0;

// Producer: copy `n` shorts into the ring. Drop the whole submit if it won't fit.
static void ring_write(const int16_t *src, uint32_t n) {
  uint32_t w = __atomic_load_n(&s_w, __ATOMIC_RELAXED);
  uint32_t r = __atomic_load_n(&s_r, __ATOMIC_ACQUIRE);
  uint32_t free_shorts = RING_SHORTS - (w - r);
  if (n > free_shorts) { if ((s_drops++ % 64) == 0) debugPrintf("audio: ring full, dropping %u shorts (drop#%u)\n", n, s_drops); return; }
  uint32_t idx = w & RING_MASK;
  uint32_t first = RING_SHORTS - idx; if (first > n) first = n;
  memcpy(&s_ring[idx], src, first * sizeof(int16_t));
  if (n > first) memcpy(&s_ring[0], src + first, (n - first) * sizeof(int16_t));
  __atomic_store_n(&s_w, w + n, __ATOMIC_RELEASE);
}

// Consumer: pull exactly CHUNK_SHORTS, padding with silence on underrun.
static void ring_read_chunk(int16_t *dst) {
  uint32_t r = __atomic_load_n(&s_r, __ATOMIC_RELAXED);
  uint32_t w = __atomic_load_n(&s_w, __ATOMIC_ACQUIRE);
  uint32_t avail = w - r;
  uint32_t take = avail < CHUNK_SHORTS ? avail : CHUNK_SHORTS;
  uint32_t idx = r & RING_MASK;
  uint32_t first = RING_SHORTS - idx; if (first > take) first = take;
  memcpy(dst, &s_ring[idx], first * sizeof(int16_t));
  if (take > first) memcpy(dst + first, &s_ring[0], (take - first) * sizeof(int16_t));
  if (take < CHUNK_SHORTS) memset(dst + take, 0, (CHUNK_SHORTS - take) * sizeof(int16_t));
  __atomic_store_n(&s_r, r + take, __ATOMIC_RELEASE);
}

static int audio_thread(SceSize args, void *argp) {
  (void)args; (void)argp;
  int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRAIN, AUDIO_FREQ,
                                 AUDIO_CHANNELS == 2 ? SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO);
  if (port < 0) { debugPrintf("audio: sceAudioOutOpenPort failed 0x%08X\n", port); return -1; }
  int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
  sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
  debugPrintf("audio: port %d open (%d Hz, %d ch, grain %d)\n", port, AUDIO_FREQ, AUDIO_CHANNELS, AUDIO_GRAIN);

  static int16_t chunk[CHUNK_SHORTS];
  for (;;) {
    ring_read_chunk(chunk);
    sceAudioOutOutput(port, chunk);   // blocks ~one grain; this is our clock
  }
  return 0;
}

// ---- The hook. Called as SubmitAudio(int count, short *pcm): r0=count, r1=pcm
// (the function reads state from file-scope globals, not from an implicit `this`).
// count = number of interleaved int16 samples. We return count to mimic
// AudioTrack.write()'s "samples written" contract; the engine only checks >= 0.
static int SubmitAudio_hook(int count, short *pcm) {
  if (pcm && count > 0) ring_write((const int16_t *)pcm, (uint32_t)count);
  return count;
}

void audio_bridge_install(so_module *game) {
  if (s_started) return;

  uintptr_t submit = so_symbol(game, "_ZN2EA5Audio4Core21AndroidEAAudioCoreJni11SubmitAudioEiPs");
  if (!submit) submit = game->text_base + 0x4f0528;   // local symbol (not in dynsym): use its offset from the symtab
  if (!submit) { debugPrintf("audio: SubmitAudio symbol not found — audio disabled\n"); return; }

  SceUID th = sceKernelCreateThread("ssx_audio", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
  if (th < 0) { debugPrintf("audio: create thread failed 0x%08X\n", th); return; }
  sceKernelStartThread(th, 0, NULL);

  hook_addr(submit, (uintptr_t)&SubmitAudio_hook);   // ARM function; hook_addr picks arm/thumb by addr bit0
  s_started = 1;
  debugPrintf("audio: hooked SubmitAudio @ 0x%08X, output thread started\n", (unsigned)submit);
}
