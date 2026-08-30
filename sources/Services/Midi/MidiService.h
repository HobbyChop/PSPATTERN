
#ifndef _MIDI_SERVICE_H_
#define _MIDI_SERVICE_H_

#include "Foundation/Observable.h"
#include "Foundation/T_Factory.h"
#include "MidiInDevice.h"
#include "MidiInMerger.h"
#include "MidiOutDevice.h"
#include "System/Process/SysMutex.h"
#include "System/Timer/Timer.h"
#include <string>

#define MIDI_MAX_BUFFERS 20

/* What the MIDI hardware is actually doing.

   The song screen used to read the configured device NAME out of the
   config file and print that, which said "PSPMIDI" whether or not
   anything was plugged in -- a settings echo dressed as a status
   readout, which is worse than no readout at all.

   The two failures are worth telling apart, because they are the first
   question in every support case: a driver that never loaded is a file
   missing beside the EBOOT or a plugin conflict, while a driver that
   loaded and sees nothing is a cable. */
enum MidiLinkState {
    MLS_NODRIVER,   // no MIDI driver at all: nothing can be sent, ever
    MLS_WAITING,    // driver is there, no adapter answering
    MLS_READY,      // an adapter is connected
};

#include "System/Process/Process.h"

class MidiService : public T_Factory<MidiService>,
                    public T_SimpleList<MidiOutDevice>,
                    public I_Observer {

  public:
    MidiService();
    virtual ~MidiService();

    bool Init();
    void Close();
    bool Start();
    void Stop();

    void SelectDevice(const std::string &name);

    I_Iterator<MidiInDevice> *GetInIterator();
    // Everything arriving on any input, merged. Observers get the raw
    // MidiMessage, which is how note input reaches the player.
    MidiInMerger *GetMerger() { return merger_ ; } ;

    //! player notification

    void OnPlayerStart();
    void OnPlayerStop();

    //! Queues a MidiMessage to the current time chunk

    void QueueMessage(MidiMessage &);

    //! Time chunk trigger

    void Trigger();
    void AdvancePlayQueue();
    //! Flush current queue to the output

    void Flush();

  protected:
    T_SimpleList<MidiInDevice> inList_;

    virtual void Update(Observable &o, I_ObservableData *d);
    void onAudioTick();

    //! start the selected midi device

    void startDevice();

    //! stop the selected midi device

    void stopDevice();

    //! build the list of available drivers

    virtual void buildDriverList() = 0;

  public:
    /* Platforms that can tell say so. The default is the honest answer
       for a build whose MIDI is whatever the host OS exposes: if a
       device is selected it is as connected as this side can know. */
    virtual MidiLinkState GetLinkState();

  private:
    void flushOutQueue();

  private:
    std::string deviceName_;
    MidiOutDevice *device_;

    T_SimpleList<MidiMessage> *queues_[MIDI_MAX_BUFFERS];
    int currentPlayQueue_;
    int currentOutQueue_;

    MidiInMerger *merger_;
    int midiDelay_;
    int tickToFlush_;
    bool sendSync_;      // legacy MIDISENDSYNC hard-mute (config file)
    bool sendSyncNow_;   // this run: leader mode AND not hard-muted
    SysMutex queueMutex_;

    /* LEADER-MODE CLOCK SCHEDULER (PSP).

       The note queue is flushed on audio-fragment boundaries, which is
       fine for notes but quantises the outgoing 0xF8 stream to fragment
       size AND sends it at tick-DECISION time -- so downstream gear ran
       one audio pipeline EARLY against what this machine was heard
       playing, with fragment-sized jitter on top.

       Realtime bytes (clock, start, stop) go through this instead: a
       ring of (status, due-microseconds) drained by a high-priority
       thread that sleeps until each byte is due and writes it straight
       to the device. Due = decision time + the same measured audible
       latency the follow mode compensates (pipe + slice in flight) +
       the sendo trim -- so the wire carries the clock of what is HEARD,
       to sub-millisecond, at any tempo and any buffer setting. */
  public:
    void PostRealtime(unsigned char status);
    bool ClockSenderStep();           // one wait/send cycle (thread shim)
  private:
    void postDirect(unsigned char status);   // fallback: old queue path
    unsigned int clockLeadUs();
    struct RtEvent { unsigned char status_; unsigned int dueUs_; };
    static const int RT_RING = 64;
    RtEvent rtRing_[RT_RING];
    volatile int rtHead_, rtTail_;
    SysThread *rtThread_;
    int sendoMs_;                     // trim, read at each start
};
#endif
