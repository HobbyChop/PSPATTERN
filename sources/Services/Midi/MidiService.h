
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
    bool sendSync_;
    SysMutex queueMutex_;
};
#endif
