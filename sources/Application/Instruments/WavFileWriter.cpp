#include "WavFileWriter.h"
#include <string.h>
#include <stdlib.h>
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioStats.h"
#include "System/Console/Trace.h"
#include "System/Process/Process.h"
#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif

#ifdef PLATFORM_PSP
#include <pspthreadman.h>
extern int g_pspMainThreadPriority ;
#endif

// 256 KB: a second and a half of 16-bit stereo at 44.1k
#define WAV_RING_SHORTS (128 * 1024)

class WavWriteThread : public SysThread {
public:
    WavWriteThread(WavFileWriter *w) : w_(w) {}
    virtual bool Execute() {
#ifdef PLATFORM_PSP
        // The UI's priority: below the DAC feed, round-robin with the
        // render and the screen, starved by neither. See the same
        // move in SDLAudioDriverThread.
        sceKernelChangeThreadPriority(sceKernelGetThreadId(),
                                      g_pspMainThreadPriority);
#endif
        w_->threadMain();
        return true;
    }
private:
    WavFileWriter *w_;
};

#ifdef PLATFORM_PSP
#include <pspkernel.h>
static unsigned int writeMicros() { return sceKernelGetSystemTimeLow(); }
#else
#include <time.h>
static unsigned int writeMicros() {
    return (unsigned int)((unsigned long long)clock() * 1000000u /
                          CLOCKS_PER_SEC);
}
#endif

// 32 KB. Against writes that arrive 64 times less often than they did
// and are the size a flash card actually wants.
#define WAV_PENDING_SHORTS (16 * 1024)

WavFileWriter::WavFileWriter(const char *path)
    : file_(0), buffer_(0), bufferSize_(0), dataBytes_(0), channels_(2),
      pending_(0), pendingUsed_(0), ring_(0), ringSize_(0), ringRead_(0),
      ringWrite_(0), wake_(0), thread_(0), finishing_(false), done_(false) {
    open(path, 2, Audio::GetInstance()->GetSampleRate());
    // no thread (no memory for the ring) is the old in-thread path:
    // still correct, just back to stalling the render on the card
    if (file_) startThread();
};

WavFileWriter::WavFileWriter(const char *path, int channels, int rate)
    : file_(0), buffer_(0), bufferSize_(0), dataBytes_(0),
      channels_(channels), pending_(0), pendingUsed_(0), ring_(0),
      ringSize_(0), ringRead_(0), ringWrite_(0), wake_(0), thread_(0),
      finishing_(false), done_(false) {
    open(path, channels, rate);
};

WavFileWriter::~WavFileWriter() { Close(); }


void WavFileWriter::open(const char *path, int channels, int rate) {
    pending_ = (short *)malloc(WAV_PENDING_SHORTS * sizeof(short));
    Path filePath(path);
    file_ = FileSystem::GetInstance()->Open(filePath.GetPath().c_str(), "wb");
    if (!file_)
        return;

    /* The 44 byte header, built in memory and written once.
     *
     * It used to be twelve separate Write calls -- four bytes, two
     * bytes, four bytes -- and on a Memory Stick a write is a card
     * transaction, not a buffered nothing. Twelve of them land at
     * the instant the transport starts, which is why starting a
     * render paused the song for a moment before it moved.
     *
     * The rest of the writing was already fixed: samples go
     * through a 32KB buffer so the card sees them in lumps it
     * wants. This is the same argument applied to the one write
     * that was left doing it the old way.
     */
    unsigned char h[44];
    unsigned char *p = h;

#define PUT32(v)                                                           \
    {                                                                      \
        unsigned int _v = (v);                                             \
        *p++ = (unsigned char)(_v);                                        \
        *p++ = (unsigned char)(_v >> 8);                                   \
        *p++ = (unsigned char)(_v >> 16);                                  \
        *p++ = (unsigned char)(_v >> 24);                                  \
    }
#define PUT16(v)                                                           \
    {                                                                      \
        unsigned short _v = (unsigned short)(v);                           \
        *p++ = (unsigned char)(_v);                                        \
        *p++ = (unsigned char)(_v >> 8);                                   \
    }
#define PUTTAG(a, b, c, d)                                                 \
    {                                                                      \
        *p++ = a;                                                          \
        *p++ = b;                                                          \
        *p++ = c;                                                          \
        *p++ = d;                                                          \
    }

    PUTTAG('R', 'I', 'F', 'F')
    PUT32(0) // filled in by Close
    PUTTAG('W', 'A', 'V', 'E')
    PUTTAG('f', 'm', 't', ' ')
    PUT32(16)                  // fmt chunk size
    PUT16(1)                   // PCM
    PUT16(channels)
    PUT32(rate)
    PUT32(rate * channels * 2) // byte rate
    PUT16(channels * 2)        // block align
    PUT16(16)                  // bits per sample
    PUTTAG('d', 'a', 't', 'a')
    PUT32(0) // filled in by Close

#undef PUT32
#undef PUT16
#undef PUTTAG

    file_->Write(h, 1, 44);
};

void WavFileWriter::AddBuffer(fixed *bufferIn, int size) {

    if (!file_)
        return;

    // allocate a short buffer for transfer

    if (size > bufferSize_) {
        SAFE_FREE(buffer_);
        buffer_ = (short *)malloc(size * 2 * sizeof(short));
        bufferSize_ = size;
    };

    if (!buffer_)
        return;

    short *s = buffer_;
    fixed *p = bufferIn;

    fixed v;
    fixed f_32767 = i2fp(32767);
    fixed f_m32768 = i2fp(-32768);

    for (int i = 0; i < size * 2; i++) {
        // Left
        v = *p++;
        if (v > f_32767) {
            v = f_32767;
        } else if (v < f_m32768) {
            v = f_m32768;
        }
        *s++ = short(fp2i(v));
    };
    queue(buffer_, size * 2);
    dataBytes_ += (unsigned int)size * 4;
};

void WavFileWriter::AddShorts(const short *frames, int frameCount) {
    if (!file_ || frameCount <= 0)
        return;
    queue(frames, frameCount * channels_);
    dataBytes_ += (unsigned int)frameCount * channels_ * 2;
};

// Into the pending buffer, and out to the card only when there is a
// worthwhile amount of it. Falls back to writing straight through if
// the buffer could not be allocated, which is the old behaviour and
// still correct, just slower.
void WavFileWriter::queue(const short *src, int n) {
    if (thread_) {
        ringPut(src, n);
        return;
    }
    if (!pending_) {
        file_->Write(src, 2, n);
        return;
    }
    while (n > 0) {
        int room = WAV_PENDING_SHORTS - pendingUsed_;
        int take = (n < room) ? n : room;
        memcpy(pending_ + pendingUsed_, src, take * sizeof(short));
        pendingUsed_ += take;
        src += take;
        n -= take;
        if (pendingUsed_ == WAV_PENDING_SHORTS) {
            flush();
        }
    }
};

/* Timed, and the time handed to the DSP meter to take back off its
   total.

   This runs inside the audio block, because the tap that feeds it
   sits in the mixer's render. A 32KB write to a Memory Stick takes
   tens of milliseconds against a block budget of about six, so
   without this the block it lands on reads several hundred per cent
   and the peak hold latches it -- while nothing is actually wrong,
   the prebuffer covers it and the song plays straight through. A
   meter labelled dsp that spends a render pinned at 300 because of a
   card write is a meter people learn to ignore. */
void WavFileWriter::flush() {
    if (!file_ || !pending_ || pendingUsed_ == 0)
        return;
    unsigned int t0 = writeMicros();
    file_->Write(pending_, 2, pendingUsed_);
    AudioStats::ExcludeMicros(writeMicros() - t0);
    pendingUsed_ = 0;
};

// The header's two lengths, then the file. Whichever thread finishes
// the file does this; nothing touches file_ afterwards.
void WavFileWriter::finalize() {

    if (!file_)
        return;

    // Whatever is still held has to reach the card before Tell is
    // asked where the end is, or the header records a length that is
    // short by up to the buffer size.
    flush();

    unsigned int len = (unsigned int)file_->Tell();
    len = Swap32(len - 8);
    file_->Seek(4, SEEK_SET);
    file_->Write(&len, 4, 1);

    unsigned int data = Swap32(dataBytes_);
    file_->Seek(40, SEEK_SET);
    file_->Write(&data, 4, 1);

    file_->Seek(0, SEEK_END);

    file_->Close();
    SAFE_DELETE(file_);
};

void WavFileWriter::Close() {

    if (thread_) {
        if (!done_) {
            Finish();
            while (!done_) SDL_Delay(1);
        }
        while (!thread_->IsFinished()) SDL_Delay(1);
        SAFE_DELETE(thread_);
        SAFE_DELETE(wake_);
        SAFE_FREE(ring_);
        ringSize_ = 0;
    } else {
        finalize();
        done_ = true;
    }
    SAFE_FREE(buffer_);
    SAFE_FREE(pending_);
};

void WavFileWriter::Finish() {
    if (!thread_) {
        finalize();
        done_ = true;
        return;
    }
    if (finishing_) return;
    finishing_ = true;
    wake_->Post();
};

bool WavFileWriter::startThread() {
    ring_ = (short *)malloc(WAV_RING_SHORTS * sizeof(short));
    if (!ring_) return false;
    ringSize_ = WAV_RING_SHORTS;
    ringRead_ = ringWrite_ = 0;
    wake_ = SysSemaphore::Create(0, 256);
    if (!wake_) {
        SAFE_FREE(ring_);
        ringSize_ = 0;
        return false;
    }
    thread_ = new WavWriteThread(this);
    thread_->Start();
    return true;
};

// The render's side of the ring. Copies in, and wakes the thread
// once a card-sized lump is waiting.
void WavFileWriter::ringPut(const short *src, int n) {
    while (n > 0) {
        int used = ringWrite_ - ringRead_;
        if (used < 0) used += ringSize_;
        int room = ringSize_ - 1 - used;
        if (room <= 0) {
            // The card has fallen a second and a half behind. Waiting
            // is the old behaviour -- the render stalls -- and keeps
            // the file whole, which is what a render is for.
            wake_->Post();
            SDL_Delay(1);
            continue;
        }
        int take = (n < room) ? n : room;
        int w = ringWrite_;
        int first = ringSize_ - w;
        if (first > take) first = take;
        memcpy(ring_ + w, src, first * sizeof(short));
        if (take > first)
            memcpy(ring_, src + first, (take - first) * sizeof(short));
        w += take;
        if (w >= ringSize_) w -= ringSize_;
        ringWrite_ = w;
        src += take;
        n -= take;
        used += take;
        if (used >= WAV_PENDING_SHORTS) wake_->Post();
    }
};

// The thread's side: to the card in lumps while a lump is there, or
// down to nothing when the file is finishing.
void WavFileWriter::drain(bool all) {
    for (;;) {
        int used = ringWrite_ - ringRead_;
        if (used < 0) used += ringSize_;
        if (used <= 0) return;
        if (used < WAV_PENDING_SHORTS && !all) return;
        int take = (used < WAV_PENDING_SHORTS) ? used : WAV_PENDING_SHORTS;
        int r = ringRead_;
        int first = ringSize_ - r;
        if (first > take) first = take;
        file_->Write(ring_ + r, 2, first);
        r += first;
        if (r >= ringSize_) r -= ringSize_;
        ringRead_ = r;
    }
};

void WavFileWriter::threadMain() {
    for (;;) {
        wake_->Wait();
        drain(finishing_);
        if (finishing_) {
            drain(true);
            finalize();
            done_ = true;
            return;
        }
    }
};

