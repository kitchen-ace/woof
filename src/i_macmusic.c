//
// Copyright (C) 2006-2020 by The Odamex Team.
// Copyright(C) 2023 Roman Fomin
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//

/*
native_midi_macosx: Native Midi support on Mac OS X for the SDL_mixer library
Copyright (C) 2009 Ryan C. Gordon <icculus@icculus.org>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include "doomtype.h"
#include "i_printf.h"
#include "i_sound.h"
#include "memio.h"
#include "mus2mid.h"
#include "m_array.h"

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AvailabilityMacros.h>

static MusicPlayer player;
static MusicSequence sequence;
static AudioUnit unit;
static AUGraph graph;
static AUNode synth;
static AUNode output;

static MusicTimeStamp endtime;

static boolean music_initialized;

static boolean is_playing, is_looping;

static boolean I_MAC_InitMusic(int device)
{
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050
    ComponentDescription d;
#else
    AudioComponentDescription d;
#endif

    NewAUGraph(&graph);

    d.componentType = kAudioUnitType_MusicDevice;
    d.componentSubType = kAudioUnitSubType_DLSSynth;
    d.componentManufacturer = kAudioUnitManufacturer_Apple;
    d.componentFlags = 0;
    d.componentFlagsMask = 0;
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050
    AUGraphNewNode(graph, &d, 0, NULL, &synth);
#else
    AUGraphAddNode(graph, &d, &synth);
#endif

    d.componentType = kAudioUnitType_Output;
    d.componentSubType = kAudioUnitSubType_DefaultOutput;
    d.componentManufacturer = kAudioUnitManufacturer_Apple;
    d.componentFlags = 0;
    d.componentFlagsMask = 0;
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050
    AUGraphNewNode(graph, &d, 0, NULL, &output);
#else
    AUGraphAddNode(graph, &d, &output);
#endif

    if (AUGraphConnectNodeInput(graph, synth, 0, output, 0) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_InitMusic: AUGraphConnectNodeInput failed.");
        return false;
    }

    if (AUGraphOpen(graph) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_InitMusic: AUGraphOpen failed.");
        return false;
    }

    if (AUGraphInitialize(graph) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_InitMusic: AUGraphInitialize failed.");
        return false;
    }

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050 // this is deprecated, but works back to 10.0
    if (AUGraphGetNodeInfo(graph, output, NULL, NULL, NULL, &unit) != noErr)
#else // not deprecated, but requires 10.5 or later
    if (AUGraphNodeInfo(graph, output, NULL, &unit) != noErr)
#endif
    {
        I_Printf(VB_ERROR, "I_MAC_InitMusic: AUGraphGetNodeInfo failed.");
        return false;
    }

    if (NewMusicPlayer(&player) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_InitMusic: Music player creation failed using AudioToolbox.");
        return false;
    }

    I_Printf(VB_INFO, "I_MAC_InitMusic: Music playback enabled using AudioToolbox.");
    music_initialized = true;

    return true;
}

static void I_MAC_SetMusicVolume(int volume)
{
    if (!music_initialized)
        return;

    if (AudioUnitSetParameter(unit,
                              kAudioUnitParameterUnit_LinearGain,
                              kAudioUnitScope_Output,
                              0, (float) volume / 15, 0) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_SetMusicVolume: AudioUnitSetParameter failed.");
    }
}

static void I_MAC_PauseSong(void *handle)
{
    if (!music_initialized)
        return;

    MusicPlayerStop(player);
}

static void I_MAC_ResumeSong(void *handle)
{
    if (!music_initialized)
        return;

    MusicPlayerStart(player);
}

static void I_MAC_PlaySong(void *handle, boolean looping)
{
    UInt32 i, ntracks;

    if (!music_initialized)
        return;

    if (MusicSequenceSetAUGraph(sequence, graph) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicSequenceSetAUGraph failed.");
        return;
    }

    if (MusicPlayerSetSequence(player, sequence) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicPlayerSetSequence failed.");
        return;
    }

    if (MusicPlayerPreroll(player) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicPlayerPreroll failed.");
        return;
    }

    if (MusicSequenceGetTrackCount(sequence, &ntracks) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicSequenceGetTrackCount failed.");
        return;
    }

    endtime = 0;

    for (i = 0; i < ntracks; i++)
    {
        MusicTimeStamp time;
        MusicTrack track;
        UInt32 size = sizeof(time);

        if (MusicSequenceGetIndTrack(sequence, i, &track) != noErr)
        {
            I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicSequenceGetIndTrack failed.");
            return;
        }

        if (MusicTrackGetProperty(track, kSequenceTrackProperty_TrackLength,
                                  &time, &size) != noErr)
        {
            I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicTrackGetProperty failed.");
            return;
        }

        if (time > endtime)
        {
            endtime = time;
        }
    }

    if (MusicPlayerSetTime(player, 0) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicPlayerSetTime failed.");
        return;
    }

    if (MusicPlayerStart(player) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_PlaySong: MusicPlayerStart failed.");
        return;
    }

    is_playing = true;
    is_looping = looping;
}

static void I_MAC_StopSong(void *handle)
{
    if (!music_initialized)
        return;

    MusicPlayerStop(player);

    // needed to prevent error and memory leak when disposing sequence
    MusicPlayerSetSequence(player, NULL);

    is_playing = false;
}

static void *I_MAC_RegisterSong(void *data, int len)
{
    CFDataRef data_ref = NULL;

    if (!music_initialized)
        return NULL;

    if (NewMusicSequence(&sequence) != noErr)
    {
        I_Printf(VB_ERROR, "I_MAC_RegisterSong: Unable to create AudioUnit sequence.");
        return NULL;
    }

    if (IsMid(data, len))
    {
        data_ref = CFDataCreate(NULL, (const UInt8 *)data, len);
    }
    else
    {
        // Assume a MUS file and try to convert
        MEMFILE *instream;
        MEMFILE *outstream;
        void *outbuf;
        size_t outbuf_len;

        instream = mem_fopen_read(data, len);
        outstream = mem_fopen_write();

        if (mus2mid(instream, outstream) == 0)
        {
            mem_get_buf(outstream, &outbuf, &outbuf_len);
            data_ref = CFDataCreate(NULL, (const UInt8 *)outbuf, outbuf_len);
        }

        mem_fclose(instream);
        mem_fclose(outstream);
    }

    if (data_ref == NULL)
    {
        I_Printf(VB_ERROR, "I_MAC_RegisterSong: Failed to load MID.");
        DisposeMusicSequence(sequence);
        return NULL;
    }

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1050
  // MusicSequenceLoadSMFData() (avail. in 10.2, no 64 bit) is equivalent to
  // calling MusicSequenceLoadSMFDataWithFlags() with a flags value of 0
  // (avail. in 10.3, avail. 64 bit). So, we use MusicSequenceLoadSMFData() for
  // powerpc versions but the *WithFlags() on intel which require 10.4 anyway.
  #if defined(__ppc__) || defined(__POWERPC__)
    if (MusicSequenceLoadSMFData(sequence, data_ref) != noErr)
  #else
    if (MusicSequenceLoadSMFDataWithFlags(sequence, data_ref, 0) != noErr)
  #endif
#else // MusicSequenceFileLoadData() requires 10.5 or later.
    if (MusicSequenceFileLoadData(sequence, data_ref, 0, 0) != noErr)
#endif
    {
        DisposeMusicSequence(sequence);
        CFRelease(data_ref);
        return NULL;
    }

    CFRelease(data_ref);

    return (void *)1;
}

static void I_MAC_UnRegisterSong(void *handle)
{
    if (!music_initialized)
        return;

    DisposeMusicSequence(sequence);
}

static void I_MAC_ShutdownMusic(void)
{
    if (!music_initialized)
        return;

    I_MAC_StopSong(NULL);
    I_MAC_UnRegisterSong(NULL);

    DisposeMusicPlayer(player);
    DisposeAUGraph(graph);

    music_initialized = false;
}

static const char **I_MAC_DeviceList(int *current_device)
{
    const char **devices = NULL;
    *current_device = 0;
    array_push(devices, "Native");
    return devices;
}

static void I_MAC_UpdateMusic(void)
{
    MusicTimeStamp time;

    if (!music_initialized || !is_playing)
        return;

    MusicPlayerGetTime(player, &time);

    if (time < endtime)
    {
        return;
    }
    else if (is_looping)
    {
        MusicPlayerSetTime(player, 0);
    }
}

music_module_t music_mac_module =
{
    I_MAC_InitMusic,
    I_MAC_ShutdownMusic,
    I_MAC_SetMusicVolume,
    I_MAC_PauseSong,
    I_MAC_ResumeSong,
    I_MAC_RegisterSong,
    I_MAC_PlaySong,
    I_MAC_UpdateMusic,
    I_MAC_StopSong,
    I_MAC_UnRegisterSong,
    I_MAC_DeviceList,
};
