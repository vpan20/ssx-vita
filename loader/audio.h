#ifndef __AUDIO_H__
#define __AUDIO_H__

#include "so_util.h"

// Wire the game's audio out to the Vita.
//
// The engine's audio thread (EA::Audio::Core::Dac::EAAudioCoreThreadFunc) mixes a
// buffer and hands finished, interleaved S16 stereo PCM to
//   AndroidEAAudioCoreJni::SubmitAudio(int count, short *pcm)
//     (mangled: _ZN2EA5Audio4Core21AndroidEAAudioCoreJni11SubmitAudioEiPs)
// which on Android copies it into a Java short[] and calls AudioTrack.write().
//
// We so_hook SubmitAudio and redirect that PCM straight into a SceAudioOut port,
// so no Java/AudioTrack/JNI array plumbing is needed. `count` is the number of
// interleaved int16 samples (both channels), i.e. frames = count / AUDIO_CHANNELS.
//
// Call audio_bridge_install() once, AFTER the game .so is loaded/relocated but
// BEFORE the engine's audio thread starts (i.e. before AndroidEAAudioCore_Init).
void audio_bridge_install(so_module *game);

#endif
