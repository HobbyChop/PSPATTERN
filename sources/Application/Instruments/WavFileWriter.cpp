#include "WavFileWriter.h"
#include <string.h>
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"

// 32 KB a file. Eight stems is a quarter of a megabyte of buffer,
// which the PSP can spare, against writes that arrive 64 times less
// often and are the size a flash card actually wants.
#define WAV_PENDING_SHORTS (16 * 1024)

WavFileWriter::WavFileWriter(const char *path)
    : file_(0), buffer_(0), bufferSize_(0), sampleCount_(0),
      pending_(0), pendingUsed_(0) {
    pending_ = (short *)malloc(WAV_PENDING_SHORTS * sizeof(short));
    Path filePath(path);
    file_ = FileSystem::GetInstance()->Open(filePath.GetPath().c_str(), "wb");
    if (file_) {

        // RIFF chunk

        unsigned int chunk;
        chunk = Swap32(0x46464952);
        file_->Write(&chunk, 1, 4);
        unsigned int size;
        size = 0; // to be filled later
        file_->Write(&size, 1, 4);

        // WAVE chunk

        chunk = Swap32(0x45564157);
        file_->Write(&chunk, 1, 4);
        chunk = Swap32(0x20746D66);
        file_->Write(&chunk, 1, 4);
        size = Swap32(16);
        file_->Write(&size, 1, 4);

        unsigned short ushort;
        ushort = Swap16(1); // compression
        file_->Write(&ushort, 1, 2);
        ushort = Swap16(2); // nChannels
        file_->Write(&ushort, 1, 2);
        unsigned int sampleRate = Swap32(Audio::GetInstance()->GetSampleRate());
        file_->Write(&sampleRate, 1, 4);

        unsigned int byteRate =
            Swap32(4 * Audio::GetInstance()->GetSampleRate());
        file_->Write(&byteRate, 1, 4);

        ushort = Swap16(4); //  blockalign
        file_->Write(&ushort, 1, 2);

        ushort = Swap16(16); // bitPerSample
        file_->Write(&ushort, 1, 2);

        // data subchunk

        chunk = Swap32(0x61746164);
        file_->Write(&chunk, 1, 4);

        // This wrote `chunk` again, so every rendered wav carried the
        // "data" tag twice and no data size at all -- the field Close
        // comes back to fill in was never there.
        size = 0; // to be updated later
        file_->Write(&size, 1, 4);
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
