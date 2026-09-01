#include "SDLAudioDriver.h"
#include "Services/Audio/AudioStats.h"
#include <string.h>
#ifdef __PSP__
#include <pspthreadman.h>
#endif
#include "Services/Midi/MidiService.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"

void sdl_callback(void *userdata, Uint8 *stream, int len) {
    SDLAudioDriver *sound = (SDLAudioDriver *)userdata;
    sound->OnChunkDone(stream, len);
};

SDLAudioDriverThread::SDLAudioDriverThread(SDLAudioDriver *driver) {
    semaphore_ = SysSemaphore::Create(0, 4);
    driver_ = driver;
};

bool SDLAudioDriverThread::Execute() {
#ifdef __PSP__
    // Drop to the main thread's priority so the render round-robins
    // with the UI instead of starving it during a heavy block. The
    // audio buffer's lead absorbs the interleaving; the SDL output
    // callback that feeds the DAC is a separate, higher thread and is
    // untouched. See PSPmain.cpp for the whole reasoning.
    {
        extern int g_pspMainThreadPriority;
        sceKernelChangeThreadPriority(sceKernelGetThreadId(),
                                      g_pspMainThreadPriority);
    }
#endif
    while (!shouldTerminate()) {
        semaphore_->Wait();
        if (shouldTerminate()) break;   // woken to leave, not to render
        driver_->OnNewBufferNeeded();
    };
    /* The semaphore is NOT deleted here. It used to be, and the object
       that owns it was deleted by the stopping thread a few
       milliseconds later regardless of whether this one had got here
       -- two threads racing to free the same things. The destructor
       owns it now, and the destructor runs only after the stop has
       waited for this function to return. */
    return true;
};

void SDLAudioDriverThread::Notify() {
    if (semaphore_) {
        semaphore_->Post();
    }
};

void SDLAudioDriverThread::RequestTermination() {
    SysThread::RequestTermination();
    // post to be sure we're not locked
    if (semaphore_) semaphore_->Post();

    /* AND ACTUALLY WAIT.

       This slept ten milliseconds and called it done. A render block
       can take longer than that on its own, and after a resume the
       thread is slower still to be scheduled -- so the caller deleted
       this object while the thread was inside Execute(), still
       touching the semaphore and the driver. That is the freeze when
       loading a project after standby: a use-after-free of a live
       thread, on a path every project load takes.

       Wait for the thread to say it has finished, up to two seconds.
       Bounded, because refusing to come back is not better than the
       bug -- but two seconds is thousands of times the honest worst
       case, so it will not be reached. */
    for (int i = 0; i < 200 && !IsFinished(); i++) {
        SDL_Delay(10);
    }
}

SDLAudioDriverThread::~SDLAudioDriverThread() {
    // whatever the wait concluded, only this thread's owner frees it
    SysSemaphore *s = semaphore_;
    semaphore_ = 0;
    if (s) delete s;
}

//-------------------------------------------------------------------------------------------------

SDLAudioDriver::SDLAudioDriver(AudioSettings &settings)
    : AudioDriver(settings), unalignedMain_(0), miniBlank_(0) {
    isPlaying_ = false;
    thread_ = 0;
}

SDLAudioDriver::~SDLAudioDriver() {}

int SDLAudioDriver::actualSampleRate_ = 0;

struct SDL_AudioSpec input;
struct SDL_AudioSpec returned;

/* THE DEVICE IS OPENED ONCE AND KEPT.

   Every project load used to close the audio device and open a new
   one: SDL_CloseAudio waits for SDL's own audio thread to come out of
   the driver, and on this machine after a standby that thread is
   inside a blocking output call against a channel the sleep took
   away. It never comes out, so the close never returns -- the freeze
   on answering YES to "lose changes", before the picker even draws,
   reliably after a resume and rarely otherwise.

   Nothing needs the churn: the only settings that shape the device
   are the buffer size and prebuffer count, and both are marked on the
   config screen as taking effect after a reboot. So the device opens
   with the first project and stays open for the run; project loads
   stop and start the render thread around it, which is all they ever
   wanted to do. */
static bool sdlAudioDeviceOpen_ = false;

bool SDLAudioDriver::InitDriver() {

    if (sdlAudioDeviceOpen_) {
        /* The DEVICE is ours already -- but the buffers are not. Every
           close frees them, so they have to be built again here or the
           render thread starts against a null mix buffer and the load
           stops dead on "starting up", which is what a second project
           did. fragSize_ is still the size this device was opened
           with; the driver object lives for the whole run. */
        unalignedMain_ = (char *)SYS_MALLOC(fragSize_ + SOUND_BUFFER_MAX);
#ifdef _64BIT
        mainBuffer_ = (char *)unalignedMain_;
#else
        mainBuffer_ = (char *)((((int)unalignedMain_) + 1) & (0xFFFFFFFC));
#endif
        miniBlank_ = (char *)malloc(fragSize_);
        if (!unalignedMain_ || !miniBlank_) {
            Trace::Error("audio buffers could not be reallocated");
            return false;
        }
        SYS_MEMSET(miniBlank_, 0, fragSize_);
        return true;
    }

    // set sound
    input.freq = 44100;
    input.format = AUDIO_S16SYS;
    input.channels = 2;
    input.callback = sdl_callback;
    input.samples = settings_.bufferSize_;
    input.userdata = this;

    if (SDL_OpenAudio(&input, &returned) < 0) {
        Trace::Error("Couldn't open sdl audio: %s\n", SDL_GetError());
        return false;
    }
    char bufferName[256];
    SDL_AudioDriverName(bufferName, 256);

    sdlAudioDeviceOpen_ = true;
    fragSize_ = returned.size;
    // the device does not have to honour our request: everything
    // downstream (tempo, envelope times, LFO rates) is computed from
    // the rate, so record what we actually got instead of assuming
    actualSampleRate_ = returned.freq;
    // Allocates a rotating sound buffer
    unalignedMain_ = (char *)SYS_MALLOC(fragSize_ + SOUND_BUFFER_MAX);
    // Make sure the buffer is aligned
#ifdef _64BIT
    mainBuffer_ = (char *)unalignedMain_;
#else
    mainBuffer_ = (char *)((((int)unalignedMain_) + 1) & (0xFFFFFFFC));
#endif

    Trace::Log("AUDIO", "%s successfully opened with %d samples", bufferName,
               fragSize_ / 4);

    // Create mini blank buffer in case of underruns

    miniBlank_ = (char *)malloc(fragSize_);
    SYS_MEMSET(miniBlank_, 0, fragSize_);

    return true;
};

void SDLAudioDriver::CloseDriver() {

    if (miniBlank_) {
        SYS_FREE(miniBlank_);
        miniBlank_ = 0;
    }

    if (unalignedMain_) {
        SYS_FREE(unalignedMain_);
        unalignedMain_ = 0;
    };
    /* SDL_CloseAudio() is NOT called here -- see InitDriver. The
       device belongs to the run, not to the project. It is paused by
       the stop above, which is enough: a paused device asks for
       nothing, and the callback that would have raced this teardown
       cannot fire. */
};

bool SDLAudioDriver::StartDriver() {

    thread_ = new SDLAudioDriverThread(this);
    thread_->Start();

    short blank[4000];
    SYS_MEMSET(blank, 0, 4000);
    bufferPos_ = 0;
    bufferSize_ = 0;

    for (int i = 0; i < settings_.preBufferCount_; i++) {
        AddBuffer((short *)miniBlank_, fragSize_ / 4);
        MidiService::GetInstance()->AdvancePlayQueue();
    }
    if (settings_.preBufferCount_ == 0) {
        thread_->Notify();
    }

    SDL_PauseAudio(0);
    startTime_ = SDL_GetTicks();

    return 1;
};

void SDLAudioDriver::StopDriver() {
    Trace::Log("AUDIO", "stop: begin");
    if (thread_) {
        thread_->RequestTermination();
        SysThread *thread = thread_;
        thread_ = 0;
        Trace::Log("AUDIO", "stop: render thread joined");
        SDL_PauseAudio(1);
        Trace::Log("AUDIO", "stop: device paused");
        delete thread;
    };
    Trace::Log("AUDIO", "stop: done");
};

double SDLAudioDriver::GetStreamTime() {
    return (SDL_GetTicks() - startTime_) / 1000.0;
}

void SDLAudioDriver::OnChunkDone(Uint8 *stream, int len) {

    // Look if we have enough data in main buffer

    while (bufferSize_ - bufferPos_ < len) {

        // First move remaining bytes at the front.
        //
        // memmove, not memcpy: source and destination are the same
        // buffer and they overlap whenever bufferPos_ is less than
        // what is left. Overlapping memcpy is undefined, and the
        // definition it happens to get is the C library's, which is
        // not the same library on the machine this is developed on as
        // on the machine it runs on.
        memmove(mainBuffer_, mainBuffer_ + bufferPos_,
                bufferSize_ - bufferPos_);

        // then get next queued buffer and copy data from it

        if (pool_[poolPlayPosition_].buffer_ == 0) {
            // starved: the queue is empty and the card gets silence
            AudioStats::AddUnderrun();
            SYS_MEMCPY(mainBuffer_ + bufferSize_ - bufferPos_, miniBlank_, len);
            bufferSize_ = bufferSize_ - bufferPos_ + len;

            bufferPos_ = 0;
        } else {

            memcpy(mainBuffer_ + bufferSize_ - bufferPos_,
                   pool_[poolPlayPosition_].buffer_,
                   pool_[poolPlayPosition_].size_);

            MidiService::GetInstance()->Flush();
            // Adapt buffer variables

            bufferSize_ =
                bufferSize_ - bufferPos_ + pool_[poolPlayPosition_].size_;
            bufferPos_ = 0;

            // Hand the block back to the pool rather than freeing it,
            // so the audio thread does no heap work per block.
            ReleaseBuffer(poolPlayPosition_);

            poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
            if (thread_)
                thread_->Notify();
        }
    }
    // Now dump audio to the device

    SYS_MEMCPY(stream, (short *)(mainBuffer_ + bufferPos_), len);
    onAudioBufferTick();
    bufferPos_ += len;
}

int SDLAudioDriver::GetPlayedBufferPercentage() {
    //	return
    //100-(bufferSize_-bufferPos_-fragSize_)*100/(bufferSize_-fragSize_) ;
    return 0;
};
