#include "WavFileWriter.h"
#include <string.h>
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"

// 32 KB. Against writes that arrive 64 times less often than they did
// and are the size a flash card actually wants.
#define WAV_PENDING_SHORTS (16 * 1024)

WavFileWriter::WavFileWriter(const char *path)
    : file_(0), buffer_(0), bufferSize_(0), sampleCount_(0),
      pending_(0), pendingUsed_(0) {
    pending_ = (short *)malloc(WAV_PENDING_SHORTS * sizeof(short));
    Path filePath(path);
    file_ = FileSystem::GetInstance()->Open(filePath.GetPath().c_str(), "wb");
    if (file_) {

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
        const unsigned int rate = Audio::GetInstance()->GetSampleRate();
        unsigned char h[44];
        unsigned char *p = h;

        #define PUT32(v) { unsigned int _v=(v);                            *p++=(unsigned char)(_v);                            *p++=(unsigned char)(_v>>8);                            *p++=(unsigned char)(_v>>16);                            *p++=(unsigned char)(_v>>24); }
        #define PUT16(v) { unsigned short _v=(unsigned short)(v);                            *p++=(unsigned char)(_v);                            *p++=(unsigned char)(_v>>8); }
        #define PUTTAG(a,b,c,d) { *p++=a; *p++=b; *p++=c; *p++=d; }

        PUTTAG('R','I','F','F')
        PUT32(0)                 // filled in by Close
        PUTTAG('W','A','V','E')
        PUTTAG('f','m','t',' ')
        PUT32(16)                // fmt chunk size
        PUT16(1)                 // PCM
        PUT16(2)                 // stereo
        PUT32(rate)
        PUT32(rate * 4)          // byte rate
        PUT16(4)                 // block align
        PUT16(16)                // bits per sample
        PUTTAG('d','a','t','a')
        PUT32(0)                 // filled in by Close

        #undef PUT32
        #undef PUT16
        #undef PUTTAG

        file_->Write(h, 1, 44);
    };
};

WavFileWriter::~WavFileWriter() { Close(); }

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
    // Into the pending buffer, and out to the card only when there is
    // a worthwhile amount of it. Falls back to writing straight
    // through if the buffer could not be allocated, which is the old
    // behaviour and still correct, just slower.
    if (!pending_) {
        file_->Write(buffer_, 2, size * 2);
    } else {
        int n = size * 2;
        const short *src = buffer_;
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
    }
    sampleCount_ += size;
};

void WavFileWriter::flush() {
    if (!file_ || !pending_ || pendingUsed_ == 0)
        return;
    file_->Write(pending_, 2, pendingUsed_);
    pendingUsed_ = 0;
};

void WavFileWriter::Close() {

    if (!file_)
        return;

    // Whatever is still held has to reach the card before Tell is
    // asked where the end is, or the header records a length that is
    // short by up to the buffer size.
    flush();

    size_t len = file_->Tell();
    len = Swap32(len - 8);
    file_->Seek(4, SEEK_SET);
    file_->Write(&len, 4, 1);

    file_->Seek(40, SEEK_SET);
    sampleCount_ = Swap32(sampleCount_ * 4);
    file_->Write(&sampleCount_, 4, 1);

    file_->Seek(0, SEEK_END);

    file_->Close();
    SAFE_DELETE(file_);
    SAFE_FREE(buffer_);
    SAFE_FREE(pending_);
};
