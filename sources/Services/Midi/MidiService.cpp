#include "MidiService.h"
#include "Application/Model/Config.h"
#include "Application/Player/Player.h"
#include "Application/Player/SyncMaster.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/Audio.h"
#include "System/Console/Trace.h"
#include "System/Timer/Timer.h"

#ifdef SendMessage
#undef SendMessage
#endif

MidiService::MidiService()
    : T_SimpleList<MidiOutDevice>(true), inList_(true), device_(0),
      sendSync_(true) {
    rtHead_ = rtTail_ = 0;
    rtThread_ = 0;
    sendoMs_ = 0;
    sendSyncNow_ = false;
    for (int i = 0; i < MIDI_MAX_BUFFERS; i++) {
        queues_[i] = new T_SimpleList<MidiMessage>(true);
    }
    const char *delay = Config::GetInstance()->GetValue("MIDIDELAY");
    midiDelay_ = delay ? atoi(delay) : 1;

    const char *sendSync = Config::GetInstance()->GetValue("MIDISENDSYNC");
    if (sendSync) {
        sendSync_ = (strcmp(sendSync, "YES") == 0);
    }
};

MidiService::~MidiService() { Close(); };

bool MidiService::Init() {
    Empty();
    inList_.Empty();
    buildDriverList();
    // Add a merger for the input
    merger_ = new MidiInMerger();
    IteratorPtr<MidiInDevice> it(inList_.GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        MidiInDevice &current = it->CurrentItem();
        merger_->Insert(current);
    }

    return true;
};

void MidiService::Close() { Stop(); };

I_Iterator<MidiInDevice> *MidiService::GetInIterator() {
    return inList_.GetIterator();
};

void MidiService::SelectDevice(const std::string &name) { deviceName_ = name; };

/* A host-OS MIDI stack hands out ports that exist; there is no cable to
   ask about. So "a device is selected" is the whole of what this side
   can honestly report. The PSP override knows more and says more. */
MidiLinkState MidiService::GetLinkState() {
    return deviceName_.size() ? MLS_READY : MLS_NODRIVER;
}

bool MidiService::Start() {
    currentPlayQueue_ = 0;
    currentOutQueue_ = 0;
    return true;
};

void MidiService::Stop() { stopDevice(); };

/* The queue lists are touched from two threads: the main thread fills
   them as the pattern plays, the audio driver thread empties and sends
   them. Locking used to be compiled in only on X64 (_FEAT_MIDI_LOCK),
   and the TryLock that stood in for it returns true without taking
   anything on SDL1 -- so on PSP an Empty() could delete list nodes
   while an Insert() walked them. SysMutex.o is in COMMONFILES, every
   platform has it, so the lock is no longer optional. */
void MidiService::QueueMessage(MidiMessage &m) {
    if (device_) {
        SysMutexLocker locker(queueMutex_);
        T_SimpleList<MidiMessage> *queue = queues_[currentPlayQueue_];
        MidiMessage *ms = new MidiMessage(m.status_, m.data1_, m.data2_);
        queue->Insert(ms);
    }
};

void MidiService::Trigger() {
    AdvancePlayQueue();
    if (device_ && sendSyncNow_) {
        SyncMaster *sm = SyncMaster::GetInstance();
        if (sm->MidiSlice()) {
            PostRealtime(0xF8);
        }
    }
}

/* ---- leader-mode clock scheduler ---------------------------------- */

#ifdef PLATFORM_PSP
#include <pspthreadman.h>

class MidiClockSender : public SysThread {
  public:
    MidiClockSender(MidiService &s) : svc_(s) {}
    virtual bool Execute() {
        /* above the interface threads, below the audio: a late clock
           byte is jitter the follower hears */
        sceKernelChangeThreadPriority(0, 0x13);
        while (!shouldTerminate()) {
            if (!svc_.ClockSenderStep()) sceKernelDelayThread(500);
        }
        return true;
    }
  private:
    MidiService &svc_;
};

unsigned int MidiService::clockLeadUs() {
    /* the OUT mirror of the follow mode's measured lead: a tick decided
       now is HEARD after the queued audio (prebuffer plus the hardware
       fragment) plus the slice being rendered around it */
    unsigned int us = 0;
    Audio *audio = Audio::GetInstance();
    if (audio) {
        int rate = audio->GetSampleRate();
        int frames = audio->GetAudioBufferSize() *
                     (audio->GetAudioPreBufferCount() + 1);
        if (rate > 0) us = (unsigned int)((long long)frames * 1000000 / rate);
    }
    SyncMaster *sm = SyncMaster::GetInstance();
    int bpm = sm ? sm->GetTempo() : 0;
    if (bpm > 0) us += (unsigned int)(2500000 / bpm);   // one tick
    int total = (int)us + sendoMs_ * 1000;
    if (total < 0) total = 0;
    return (unsigned int)total;
}

void MidiService::PostRealtime(unsigned char status) {
    if (!device_) return;
    if (status == 0xFA) {
        /* start: (re)read the trim, and start the sender if needed */
        const char *t = Config::GetInstance()->GetValue("MIDISENDOFFSET");
        sendoMs_ = t ? atoi(t) : 0;
        if (!rtThread_) {
            rtThread_ = new MidiClockSender(*this);
            rtThread_->Start();
        }
    }
    if (!rtThread_) { postDirect(status); return; }
    int next = (rtHead_ + 1) % RT_RING;
    if (next == rtTail_) return;   // full: drop rather than block audio
    rtRing_[rtHead_].status_ = status;
    rtRing_[rtHead_].dueUs_ = sceKernelGetSystemTimeLow() + clockLeadUs();
    rtHead_ = next;
}

/* One scheduler cycle. Returns false when there was nothing to do
   (the thread then idles briefly). */
bool MidiService::ClockSenderStep() {
    if (rtTail_ == rtHead_) return false;
    RtEvent ev = rtRing_[rtTail_];
    unsigned int now = sceKernelGetSystemTimeLow();
    int wait = (int)(ev.dueUs_ - now);
    if (wait > 400) {
        sceKernelDelayThread(wait > 4000 ? 2000 : (wait - 200));
        return true;   // re-check: more may have queued meanwhile
    }
    while ((int)(ev.dueUs_ - sceKernelGetSystemTimeLow()) > 2) {}
    {
        /* the fragment-flush path and this thread share the device
           (and the prx TX ring is single-producer) */
        SysMutexLocker locker(queueMutex_);
        if (device_) {
            MidiMessage m;
            m.status_ = ev.status_;
            device_->SendMessage(m);
        }
    }
    rtTail_ = (rtTail_ + 1) % RT_RING;
    return true;
}

#else  /* non-PSP builds keep the fragment-quantised path */

unsigned int MidiService::clockLeadUs() { return 0; }
bool MidiService::ClockSenderStep() { return false; }
void MidiService::PostRealtime(unsigned char status) { postDirect(status); }

#endif

void MidiService::postDirect(unsigned char status) {
    MidiMessage msg;
    msg.status_ = status;
    QueueMessage(msg);
}

void MidiService::AdvancePlayQueue() {
    SysMutexLocker locker(queueMutex_);
    int next = (currentPlayQueue_ + 1) % MIDI_MAX_BUFFERS;
    queues_[next]->Empty();
    currentPlayQueue_ = next;
}

void MidiService::Update(Observable &o, I_ObservableData *d) {
    AudioDriver::Event *event = (AudioDriver::Event *)d;
    if (event->type_ == AudioDriver::Event::ADET_DRIVERTICK) {
        onAudioTick();
    }
};

void MidiService::onAudioTick() {
    if (tickToFlush_ > 0) {
        if (--tickToFlush_ == 0) {
            flushOutQueue();
        }
    }
}

void MidiService::Flush() {
    tickToFlush_ = midiDelay_;
    if (tickToFlush_ == 0) {
        flushOutQueue();
    }
};

void MidiService::flushOutQueue() {
    SysMutexLocker locker(queueMutex_);
    int next = (currentOutQueue_ + 1) % MIDI_MAX_BUFFERS;

    T_SimpleList<MidiMessage> *flushQueue = queues_[next];

    if (device_) {
        device_->SendQueue(*flushQueue);
    }

    flushQueue->Empty();
    currentOutQueue_ = next; // Advance only after safe flush
}

/*
 * starts midi device
 */
void MidiService::startDevice() {
    IteratorPtr<MidiOutDevice> it(GetIterator());

    for (it->Begin(); !it->IsDone(); it->Next()) {
        MidiOutDevice &current = it->CurrentItem();
        if (!strcmp(deviceName_.c_str(), current.GetName())) {
            if (current.Init()) {
                if (current.Start()) {
                    Trace::Log("MidiService", "midi device %s started",
                               deviceName_.c_str());
                    device_ = &current;
                } else {
                    Trace::Log("MidiService", "midi device %s failed to start",
                               deviceName_.c_str());
                    current.Close();
                }
            }
            break;
        }
    }
};

/*
 * closes midi device
 */
void MidiService::stopDevice() {
    if (device_) {
        device_->Stop();
#ifdef PLATFORM_PSP
        // the clock sender writes to the device from its own thread;
        // ask it out before the device goes (pump precedent: request,
        // not joined -- it exits within one short wait)
        if (rtThread_) {
            rtThread_->RequestTermination();
            rtThread_ = 0;
        }
#endif
        device_->Close();
    }
    device_ = 0;
};

/*
 * starts midi device when playback starts
 */
void MidiService::OnPlayerStart() {
    if (deviceName_.size() != 0) {
        stopDevice();
        startDevice();
        deviceName_ = "";
    } else {
        startDevice();
    }

    /* the rig decides: clock goes out only when this machine is the
       LEADER (config screen, SYNC panel). MIDISENDSYNC=NO from the
       old config format still hard-mutes it. */
    sendSyncNow_ = sendSync_ && (Player::GetSyncMode() == Player::SYNC_LEADER);
    if (sendSyncNow_) {
        PostRealtime(0xFA);
    }
};

/*
 * queues midi stop message when player stops
 */
void MidiService::OnPlayerStop() {
    if (sendSyncNow_) {
        PostRealtime(0xFC);
    }
};
