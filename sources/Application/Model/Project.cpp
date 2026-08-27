#include "Project.h"
#include "Application/Persistency/Checksum.h"

#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/SyncMaster.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Groove.h"
#include "Scale.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/io/Status.h"
#include "Table.h"

#include "ProjectDatas.h"
#include <math.h>
#include "Services/Audio/MasterEq.h"

Project::Project()
:Persistent("PROJECT")
,midiDeviceList_(0),
tempoNudge_(0)
{
    WatchedVariable *tempo = new WatchedVariable("tempo", VAR_TEMPO, 138);
    this->Insert(tempo);
    /* 75, not 100, and the number is not arbitrary.

       The fader's taper is (v/100)^4, so 75 is 0.75^4 = 0.3164, which
       is -10.00 dB exactly. That is the headroom this tracker needs
       and did not have: measured, one voice at default settings peaks
       at -9.1 dBFS, so TWO channels fit and THREE already exceed full
       scale. Eight ask the master for +8.4 dB with a fifth of their
       samples past the clamp. The loudest baked drum is -3.31 dBFS, so
       two drums on one step made +1.6 dB. Nothing anywhere in the
       chain attenuated on purpose -- every default was exactly unity,
       and the first thing a new user met was the ceiling.

       At -10 dB the eight-channel case lands at -1.6 dBFS and two
       drums at -7.3. It is not enough for eight simultaneous full
       scale drums, which is the right place to stop: past that the
       user should be turning something down, and now they have room
       to hear that they need to.

       Why the fader rather than a hidden shift in the summing loop:
       this number is visible, adjustable, and above all it MIGRATES
       FOR FREE. SaveContent writes every variable by name, so every
       project already on a memory stick carries its own master value
       and keeps it. Only new projects start here. A shift in the mix
       path would have re-levelled every song ever saved with nothing
       stored that the loader could divide back out.

       The same reasoning is why the baked drum kits are NOT being
       renormalised, though they are the loudest thing in the tree: a
       kit is generated at boot, so there is no stored value to undo,
       and quieting it would silently change every song that used it
       with no way back. */
    Variable *masterVolume = new Variable("master", VAR_MASTERVOL, 75, 100);
    this->Insert(masterVolume) ;
    Variable *pregain =
        new Variable("pregain", VAR_PREGAIN, 100, 200);
    this->Insert(pregain);
    /* Ten master EQ bands. 64 is flat, which is where they all start,
       so a project that never touches the EQ costs nothing: the whole
       stage is skipped while every band is flat. */
    static const FourCC eqIds[MASTER_EQ_BANDS] = {
        VAR_EQ0, VAR_EQ1, VAR_EQ2, VAR_EQ3, VAR_EQ4,
        VAR_EQ5, VAR_EQ6, VAR_EQ7, VAR_EQ8, VAR_EQ9 };
    static const char *eqNames[MASTER_EQ_BANDS] = {
        "eq0","eq1","eq2","eq3","eq4","eq5","eq6","eq7","eq8","eq9" };
    for (int b = 0; b < MASTER_EQ_BANDS; b++) {
        this->Insert(new Variable(eqNames[b], eqIds[b],
                                  MASTER_EQ_FLAT, MASTER_EQ_MAX));
    }
    Variable *softclip =
        new Variable("softclip", VAR_SOFTCLIP, softclipStates, 5, 0);
    this->Insert(softclip);
    Variable *softclipGain = new Variable("softclipGain", VAR_SOFTCLIP_GAIN,
                                          softclipGainStates, 2, 0);
    this->Insert(softclipGain);
	Variable *wrap=new Variable("wrap", VAR_WRAP, false);
	this->Insert(wrap);
	// Default to playing once. A song that loops forever the moment you
	// press play is a nuisance to work on and a surprise to anyone
	// rendering one; loop is a choice, not a default.
	Variable *loop=new Variable("loop", VAR_LOOP, loopModes, 2, 1);
	this->Insert(loop);
	Variable *transpose=new Variable("transpose", VAR_TRANSPOSE, 0);
	this->Insert(transpose);
    Variable *scale =
        new Variable("scale", VAR_SCALE, scaleNames, scaleCount, 0);
    this->Insert(scale);
    scale->SetInt(0);
    Variable *renderMode =
        new Variable("renderMode", VAR_RENDER, renderModes, MAX_RENDER_MODE, 0);
    this->Insert(renderMode);
    Variable *midiSync = new Variable("midiSync", VAR_MIDISYNC, midiSyncModes,
                                      MAX_MIDISYNC_MODE, 0);
    this->Insert(midiSync);

// Reload the midi device list

	buildMidiDeviceList() ;

	WatchedVariable *midi=new WatchedVariable("midi",VAR_MIDIDEVICE,midiDeviceList_,midiDeviceListSize_) ;
	this->Insert(midi) ;
	midi->AddObserver(*this) ;
	// The observer only fires on CHANGE, and the variable was just built at
	// its default index -- so on a brand new project nothing ever selected
	// the device and every MIDI message was dropped until the user saved and
	// reloaded. Apply the initial value now.
	if (midiDeviceListSize_>0) {
		MidiService::GetInstance()->SelectDevice(std::string(midi->GetString())) ;
	}


	song_=new Song() ;
	instrumentBank_=new InstrumentBank() ;

	// look if we can find a sav file

	// Makes sure the tables exists for restoring

	TableHolder::GetInstance() ;

	Groove::GetInstance()->Clear() ;

	tempoTapCount_=0 ;

	Status::Set("About to load project") ;

} ;

Project::~Project() {
	delete song_ ;
	delete instrumentBank_ ;
} ;

int Project::GetScale() {
    Variable *v = FindVariable(VAR_SCALE);
    NAssert(v);
    return v->GetInt();
}

int Project::GetTempo() {
	Variable *v=FindVariable(VAR_TEMPO) ;
	NAssert(v) ;
	int tempo = v->GetInt()+tempoNudge_ ;
	return tempo ;
} ;

void Project::SetTempo(int tempo) {
	Variable *v=FindVariable(VAR_TEMPO) ;
	NAssert(v) ;
	if (tempo<30) tempo=30 ;
	if (tempo>400) tempo=400 ;
	tempoNudge_=0 ;
	v->SetInt(tempo) ;
} ;

int Project::GetMasterVolume() {
    Variable *v = FindVariable(VAR_MASTERVOL);
    NAssert(v);
	return v->GetInt();
} ;

int Project::GetSoftclip() {
    Variable *v = FindVariable(VAR_SOFTCLIP);
    NAssert(v);
	return v->GetInt();
}

int Project::GetSoftclipGain() {
    Variable *v = FindVariable(VAR_SOFTCLIP_GAIN);
    NAssert(v);
	return v->GetBool();
}

int Project::GetPregain() {
    Variable *v = FindVariable(VAR_PREGAIN);
    NAssert(v);
	return v->GetInt();
}

static FourCC eqVarId(int band) {
    static const FourCC ids[MASTER_EQ_BANDS] = {
        VAR_EQ0, VAR_EQ1, VAR_EQ2, VAR_EQ3, VAR_EQ4,
        VAR_EQ5, VAR_EQ6, VAR_EQ7, VAR_EQ8, VAR_EQ9 };
    if (band < 0 || band >= MASTER_EQ_BANDS) return VAR_EQ0;
    return ids[band];
}

int Project::GetEqBand(int band) {
    Variable *v = FindVariable(eqVarId(band));
    NAssert(v);
    return v->GetInt();
}

void Project::SetEqBand(int band, int value) {
    Variable *v = FindVariable(eqVarId(band));
    NAssert(v);
    if (value < 0) value = 0;
    if (value > MASTER_EQ_MAX) value = MASTER_EQ_MAX;
    v->SetInt(value);
}

int Project::GetRenderMode() {
    Variable *v = FindVariable(VAR_RENDER);
    NAssert(v);
	return v->GetInt();
}

int Project::GetMidiSync() {
    Variable *v = FindVariable(VAR_MIDISYNC);
    NAssert(v);
	return v->GetInt();
}

void Project::NudgeTempo(int value) {
	if((GetTempo() + tempoNudge_) > 0)
		tempoNudge_ += value;
} ;

void Project::Trigger() {
	if (tempoNudge_!=0) {
		if (tempoNudge_>0) {
			tempoNudge_-- ;
		} else {
			tempoNudge_++ ;
		};
	}
} ;

int Project::GetTranspose() {
	Variable *v=FindVariable(VAR_TRANSPOSE) ;
	NAssert(v) ;
	int result=v->GetInt() ;
	if (result>0x80) {
		result-=128 ;
	}
	return result ;
} ;

const char *Project::loopModes[2] = { "loop", "once" } ;

bool Project::Loop() {
	Variable *v=FindVariable(VAR_LOOP) ;
	if (!v) return true ;
	return v->GetInt()==0 ;
} ;

bool Project::Wrap() {
	Variable *v=FindVariable(VAR_WRAP) ;
	NAssert(v) ;
	return v->GetBool() ;
} ;

InstrumentBank* Project::GetInstrumentBank() {
	return instrumentBank_ ;
} ;

//bool Project::MidiEnabled() {
//	Variable *v=FindVariable(VAR_MIDIENABLE) ;
//	NAssert(v) ;
//	return v->GetBool() ;
//}

void Project::Update(Observable &o,I_ObservableData *d) {
	WatchedVariable &v=(WatchedVariable &)o ;
	switch(v.GetID()) {
		case VAR_MIDIDEVICE:
			MidiService::GetInstance()->SelectDevice(std::string(v.GetString())) ;
 /*           bool enabled=v.GetBool() ;
            Midi *midi=Midi::GetInstance() ;
            if (enabled) {
                midi->Init() ;
            } else {
                midi->Stop() ;
                midi->Close() ;
            }
 */           break ;
    }
}

void Project::Purge() {

	song_->chain_->ClearAllocation() ;
	song_->phrase_->ClearAllocation() ;

		
	unsigned char *data=song_->data_ ;
	for (int i=0;i<256*SONG_CHANNEL_COUNT;i++) {
		if (*data!=0xFF) {
			song_->chain_->SetUsed(*data) ;
		} 
		data++ ;
	}
                
    data=song_->chain_->data_ ;        
    unsigned char *data2=song_->chain_->transpose_ ;
    
	for (int i=0;i<CHAIN_COUNT;i++) {

		if (song_->chain_->IsUsed(i)) {
			for (int j=0;j<16;j++) {
				if (*data!=0xFF) {
					song_->phrase_->SetUsed(*data) ;
				}
				data++ ;
				data2++ ;
			}
		} else {

			for (int j=0;j<16;j++) {
				*data++=0xFF ;
				*data2++=0x00 ;
			} 
		}
    }

    data=song_->phrase_->note_ ;
    data2=song_->phrase_->instr_ ;

	FourCC *cmd1=song_->phrase_->cmd1_ ;
	ushort *param1=song_->phrase_->param1_ ;
	FourCC *cmd2=song_->phrase_->cmd2_ ;
	ushort *param2=song_->phrase_->param2_ ;    

	for (int i=0;i<PHRASE_COUNT;i++) {
		for (int j=0;j<16;j++) {
			if (!song_->phrase_->IsUsed(i)) {
				*data=0xFF ;
				*data2=0xFF ;
				*cmd1='----' ;
				*param1=0 ;
				*cmd2='----' ;
				*param2=0 ;
			}
            data++ ;
            data2++ ;
			cmd1++ ;
			param1++ ;
			cmd2++ ;
			param2++ ;
        } ;	
    }	
} ;

 
void Project::PurgeInstruments(bool removeFromDisk) {

    bool used[MAX_INSTRUMENT_COUNT] = {false};

    unsigned char *data=song_->phrase_->instr_ ;

	for (int i=0;i<PHRASE_COUNT;i++) {
		for (int j=0;j<16;j++) {
			if (*data!=0xFF) {
				NAssert(*data<MAX_INSTRUMENT_COUNT) ;
				used[*data]=true ;
			}
			data++ ;
		}
	}

	InstrumentBank *bank=GetInstrumentBank() ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		if (!used[i]) {
			I_Instrument *instrument=bank->GetInstrument(i) ;
			instrument->Purge() ;
		}
	}

  // now see if any samples isn't used and get rid if them if needed

	if (removeFromDisk) {

		// clear used flag

        bool used[MAX_PIG_SAMPLES] = {false};

        // flag all samples actually used

        for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
            I_Instrument *instrument=bank->GetInstrument(i) ;
			if (instrument->GetType()==IT_SAMPLE) {
				SampleInstrument *si=(SampleInstrument *)instrument ;
				int index=si->GetSampleIndex() ;
                if (index >= 0)
                    used[index] = true;
            };
        }

        // Now effectively purge all unused sample from disk

        int purged = 0;
        SamplePool *sp = SamplePool::GetInstance();
        for (int i = 0; i < MAX_PIG_SAMPLES; i++) {
            if ((!used[i])&&(sp->GetSource(i-purged))) {
                sp->PurgeSample(i - purged);
                Trace::Debug("Purged sample [%d]", i - purged);
        		purged++;
				}
        }
    }
}

void Project::RestoreContent(TiXmlElement *element) {

	// Get version attribute

	PersistencyDocument *doc=(PersistencyDocument *)element->GetDocument() ;
	doc->version_=32 ;

	const char *aVersion=element->Attribute("VERSION") ;
	if (aVersion) {
		doc->version_=int(atof(aVersion)*100) ;
	};

	// Get table ratio
	
	int tableRatio ;
	if (!element->Attribute("TABLERATIO",&tableRatio)) {
		tableRatio=(doc->version_<=32)?2:1 ;
	}
	SyncMaster::GetInstance()->SetTableRatio(tableRatio) ;

	// Now loop on all variables

	TiXmlElement *current=element->FirstChildElement() ;
	while (current) {
		const char *name=current->Attribute("NAME") ;
		const char *value=current->Attribute("VALUE") ;

		// CHAR_LIST variables are saved by their displayed name, so
		// anything renamed for layout has to be mapped back or the
		// setting is silently lost on load.
		if ((value)&&(!strcmp(value,"[unity]"))) value="unity" ;
		if ((value)&&(!strcmp(value,"[boost]"))) value="boost" ;
		if ((value)&&(!strcmp(value,"Melodic minor (asc)"))) value="Melodic asc" ;
		if ((value)&&(!strcmp(value,"None (Chromatic)"))) value="Chromatic" ;

		Variable *v=FindVariable(name) ;
		if (v) {
			v->SetString(value) ;
		} ;
		current=current->NextSiblingElement() ;
	} ;
};


unsigned int Project::Checksum(unsigned int h) {
	// Everything the project screen edits is a Variable, and
	// SaveContent writes exactly this list.
	IteratorPtr<Variable> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &v=it->CurrentItem() ;
		h=checksumString(h,v.GetString()) ;
	}
	return h ;
} ;

void Project::SaveContent(TiXmlNode *node) {

	// store project version

	TiXmlElement *element=(TiXmlElement *)node ;
	element->SetAttribute("VERSION",PROJECT_NUMBER) ;

	// store table ratio if not one

	int tableRatio=SyncMaster::GetInstance()->GetTableRatio() ;
	if (tableRatio!=1) {
		element->SetAttribute("TABLERATIO",tableRatio) ;
	}
	// save all of the project's parameters
	
	IteratorPtr<Variable> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		TiXmlElement param("PARAMETER") ;
		Variable v=it->CurrentItem() ;
		param.SetAttribute("NAME",v.GetName()) ;
		param.SetAttribute("VALUE",v.GetString()) ;
		node->InsertEndChild(param) ;
	}
} ;

void Project::LoadFirstGen(const char *root) {


	char filename[1024] ;
	sprintf(filename,"%s/lgptsav.dat",root) ;

	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(filename,"r") ;

	if (file) {
		// Read file
		int tempo ;
		SyncMaster::GetInstance()->SetTableRatio(2) ;
		file->Read(&tempo,sizeof(int),1) ;
		Variable *v=FindVariable(VAR_TEMPO) ;
		v->SetInt(tempo);
		file->Read(song_->data_,sizeof(char),SONG_CHANNEL_COUNT*256) ;
		file->Read(song_->chain_->data_,sizeof(char),CHAIN_COUNT*16) ;
		file->Read(song_->chain_->transpose_,sizeof(char),CHAIN_COUNT*16) ;
		file->Read(song_->phrase_->note_,sizeof(char),PHRASE_COUNT*16) ;
		file->Read(song_->phrase_->instr_,sizeof(char),PHRASE_COUNT*16) ;

		// read instrument data
		int buffer[MAX_INSTRUMENT_COUNT*3] ; // Three parameters per instruments
		int byteRead=file->Read(buffer,sizeof(int),MAX_INSTRUMENT_COUNT*3) ;
		if (byteRead>0) {
			int *current=buffer ;
			InstrumentBank *bank=this->instrumentBank_ ;
			for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
				I_Instrument *instr=bank->GetInstrument(i) ;
				int count=0 ;
				IteratorPtr<Variable> it(instr->GetIterator()) ;
				for (it->Begin();count<3;it->Next()) {
					Variable &ip=it->CurrentItem() ;
					ip.SetInt(*current++) ;
					count++ ;
				}
			}
		}
		file->Close() ;
		delete file ;

		// Restore chain & phrase allocation table
		
		unsigned char *data=song_->data_ ;
		for (int i=0;i<256*SONG_CHANNEL_COUNT;i++) {
			if (*data!=0xFF) {
				song_->chain_->SetUsed(*data) ;
			} 
		    data++ ;
		}
                
        data=song_->chain_->data_ ;        

		for (int i=0;i<CHAIN_COUNT;i++) {
            for (int j=0;j<16;j++) {
                if (*data!=0xFF) {
                    song_->chain_->SetUsed(i) ;
                    song_->phrase_->SetUsed(*data) ;
                }
                data++ ;
            } ;	
        }

        data=song_->phrase_->note_ ;
             
		for (int i=0;i<PHRASE_COUNT;i++) {
            for (int j=0;j<16;j++) {
                if (*data!=0xFF) {
                    song_->phrase_->SetUsed(i) ;
                }
                data++ ;
            } ;	
        }	
        
        // TEMP: clear out Phrase 0xFE
        
		data=song_->phrase_->note_+0xFE*16 ;
		for (int i=0;i<16;i++) {
			*data++=0xFF ;
		} ;
        // Trace output unused instruments to the console
        
        bool used[MAX_INSTRUMENT_COUNT] ;
        for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
            used[i]=false ;
        } ;	
        
        data=song_->phrase_->instr_ ;
        for (int i=0;i<PHRASE_COUNT;i++) {
            for (int j=0;j<16;j++) {
                if (*data!=0xFF) {
                    used[*data]=true ;
                }
                data++ ;
            } ;
        } ;
        
        //for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
        //    if ((!used[i])&&(song_->instrument_[i]->IsInitialized())) {
        //        Trace::Dump("Instrument %x not used",i) ;
        //    }
        //} ;	
	}
}

void Project::buildMidiDeviceList() {
	if (midiDeviceList_) {
		for (int i=0;i<midiDeviceListSize_;i++) {
			SAFE_FREE(midiDeviceList_[i]) ;
		}
		SAFE_FREE(midiDeviceList_) ;
	} 
	midiDeviceListSize_=MidiService::GetInstance()->Size() ;
	midiDeviceList_=(char **)SYS_MALLOC(midiDeviceListSize_*sizeof(char *)) ;
	IteratorPtr<MidiOutDevice> it(MidiService::GetInstance()->GetIterator()) ;
	it->Begin() ;
	for (int i=0;i<midiDeviceListSize_;i++) {
		std::string deviceName=it->CurrentItem().GetName() ;
		midiDeviceList_[i]=(char *)malloc(sizeof(char *)*deviceName.size()+1);
		strcpy(midiDeviceList_[i],deviceName.c_str()) ;
		it->Next() ;
	} ;
} ;

void Project::OnTempoTap() {

	unsigned long now=System::GetInstance()->GetClock() ;

  if (tempoTapCount_!=0) {
		// count last tick tempo and see if in range
		unsigned millisec=now-lastTap_[tempoTapCount_-1] ;
		int t=int(60000/(float)millisec) ;
		if (t>30) {
			if (tempoTapCount_==MAX_TAP) {
				for (int i=0;i<int(tempoTapCount_)-1;i++)  {
					lastTap_[i]=lastTap_[i+1] ;
				}				
			} else {
				tempoTapCount_++ ;
			}
			int tempo=int(60000*(tempoTapCount_-1)/(float)(now-lastTap_[0])) ;
			Variable *v=FindVariable(VAR_TEMPO) ;
			v->SetInt(tempo) ;
		} else {
			tempoTapCount_=1 ;
		}
	} else {
		tempoTapCount_=1 ;
	}
	lastTap_[tempoTapCount_-1]=now ;
}
