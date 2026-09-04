
#include "InstrumentBank.h"
#include "Application/Persistency/Checksum.h"

#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SynthInstrument.h"
#include "System/io/Status.h"
#include "Application/Utils/char.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistencyService.h"
#include "Filters.h"

char *InstrumentTypeData[IT_LAST]= {
	"Sample",
	"Midi",
	"Synth"
} ;


// Contain all instrument definition

InstrumentBank::InstrumentBank():Persistent("INSTRUMENTBANK") {

   	for (int i=0;i<MAX_SAMPLEINSTRUMENT_COUNT;i++) {
        SampleInstrument *s=new SampleInstrument() ;
        instrument_[i]=s ;
    }
	for (int i=0;i<MAX_MIDIINSTRUMENT_COUNT;i++) {
        MidiInstrument *s=new MidiInstrument() ;
        s->SetChannel(i) ;
        instrument_[MAX_SAMPLEINSTRUMENT_COUNT+i]=s ;
    }
	for (int i=0;i<MAX_SYNTHINSTRUMENT_COUNT;i++) {
        SynthInstrument *s=new SynthInstrument() ;
        instrument_[MAX_SAMPLEINSTRUMENT_COUNT+MAX_MIDIINSTRUMENT_COUNT+i]=s ;
    }
    Status::Set("All instrument loaded") ;
} ;

//
// Assigns default instruments value for new project
//

void InstrumentBank::AssignDefaults() {

	SamplePool *pool=SamplePool::GetInstance() ;
   	for (int i=0;i<MAX_SAMPLEINSTRUMENT_COUNT;i++) {
		SampleInstrument *s=(SampleInstrument*)instrument_[i] ;
		if (i<pool->GetNameListSize()) {
	        s->AssignSample(i) ;
		} else {
			s->AssignSample(-1) ;
		} 
    } ;
} ;

InstrumentBank::~InstrumentBank() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		delete instrument_[i] ;
	}	
} ;

I_Instrument *InstrumentBank::GetInstrument(int i) {
	// clamp: callers hand this raw song bytes (a phrase's instrument
	// column), and a foreign or hand-edited file can hold 0xA0..0xFE --
	// indexing past the array returned a garbage object whose observer
	// list was then walked
	if (i<0||i>=MAX_INSTRUMENT_COUNT) i=0 ;
	return instrument_[i] ;
} ;

unsigned int InstrumentBank::Checksum(unsigned int h) {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		I_Instrument *instr=instrument_[i] ;
		if (!instr) {
			h=checksumInt(h,-1) ;
			continue ;
		}
		// the type is saved as an attribute, so a type change on its
		// own has to move the sum
		h=checksumInt(h,(int)instr->GetType()) ;
		IteratorPtr<Variable> it(instr->GetIterator()) ;
		for (it->Begin();!it->IsDone();it->Next()) {
			Variable &v=it->CurrentItem() ;
			h=checksumString(h,v.GetString()) ;
		}
	}
	return h ;
} ;

void InstrumentBank::SaveContent(TiXmlNode *node) {
	char hex[3] ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {

		I_Instrument *instr=instrument_[i] ;
		if (!instr->IsEmpty()) {
			TiXmlElement data("INSTRUMENT") ;
			hex2char(i,hex) ;
			data.SetAttribute("ID",hex) ;
			data.SetAttribute("TYPE",InstrumentTypeData[instr->GetType()]) ;

			IteratorPtr<Variable> it(instr->GetIterator()) ;
			int count=0 ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Variable &v=it->CurrentItem() ;
				TiXmlElement param("PARAM") ;
				param.SetAttribute("NAME",v.GetName()) ;
				param.SetAttribute("VALUE",v.GetString()) ;
				data.InsertEndChild(param) ;
				count++ ;
			}
			if (count) node->InsertEndChild(data) ;
		}
	}
} ;

void InstrumentBank::RestoreContent(TiXmlElement *element) {

	TiXmlElement *current=element->FirstChildElement() ;

	PersistencyDocument *doc=(PersistencyDocument *)element->GetDocument() ;
  if (doc->version_ < 130)
  {
    if (Config::GetInstance()->GetValue("LEGACYDOWNSAMPLING") != NULL)
    {
      SampleInstrument::EnableDownsamplingLegacy();
    }
  }
	while (current) {

		// Check it is an instrument
		
		if (!strcmp(current->Value(),"INSTRUMENT")) {

			// Get the instrument ID
			
			const char* hexid=current->Attribute("ID") ;
			unsigned char b1=(c2h__(hexid[0]))<<4 ;
			unsigned char b2=c2h__(hexid[1]) ;
			unsigned char id=b1+b2 ;			

			InstrumentType it=IT_LAST ;
			const char* instype=current->Attribute("TYPE") ;
			if (instype) {
				for (int i=0;i<IT_LAST;i++) {
					if (!strcmp(instype,InstrumentTypeData[i])) {
						it=(InstrumentType)i ;
						break ;
					}
				}
			} else {
				it=(id<MAX_SAMPLEINSTRUMENT_COUNT)?IT_SAMPLE:IT_MIDI ;
			} ;
			if (id<MAX_INSTRUMENT_COUNT) {
        I_Instrument *instr=instrument_[id] ;
				if (instr->GetType()!=it) {
					// A TYPE this build doesn't know leaves 'it' at
					// IT_LAST, which matches no case -- the old code
					// deleted first and then stored the freed pointer
					// back into the bank for the rest of the session.
					// Build the replacement first; no replacement means
					// keep what's already there.
					I_Instrument *repl=0 ;
					switch (it) {
						case IT_SAMPLE:
							repl=new SampleInstrument() ;
							break ;
						case IT_MIDI:
							repl=new MidiInstrument() ;
							break ;
						case IT_SYNTH:
							repl=new SynthInstrument() ;
							break ;
						default:
							break ;
					}
					if (repl) {
						delete instr ;
						instr=repl ;
						instrument_[id]=instr ;
					} ;
				} ;

        TiXmlElement *param=current->FirstChildElement() ;
				while (param) {
					const char *name=param->Attribute("NAME") ;
					const char *value=param->Attribute("VALUE") ;

          // Convert old filter dist to newer filter mode

          if (!strcmp(name,"filter dist"))
          {
            name = "filter mode";
            if (!strcmp(value,"none"))
            {
              value = "orig";
            }
            else
            {
              value = "scrm";
            }
          }

          // legacy long enum spellings -> the short house names
          if (!strcmp(name,"interpol") && !strcmp(value,"linear")) {
            value = "lin";
          }
          if (!strcmp(name,"loopmode")) {
            if (!strcmp(value,"ping pong")) value = "ping";
            else if (!strcmp(value,"oscillator")) value = "osc";
            else if (!strcmp(value,"looper sync")) value = "sync";
          }
          if (!strcmp(name,"filter mode")) {
            if (!strcmp(value,"original")) value = "orig";
            else if (!strcmp(value,"scream")) value = "scrm";
          }

          // This fork renamed the synth's mod-envelope parameters and
          // shortened the LFO destinations. Without these, a project saved
          // by an earlier build loses its LFO and mod settings on load.
          if (!strcmp(name,"dcw"))         name = "mod";
          else if (!strcmp(name,"dcw attack"))  name = "mod attack";
          else if (!strcmp(name,"dcw decay"))   name = "mod decay";
          else if (!strcmp(name,"dcw sustain")) name = "mod sustain";
          if (!strcmp(name,"lfo dest")) {
            if (!strcmp(value,"pitch")) value = "pit";
            else if (!strcmp(value,"filter")) value = "flt";
          }

					IteratorPtr<Variable> it(instr->GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						Variable &v=it->CurrentItem() ;
						if (!strcmp(v.GetName(),name)) {
							v.SetString(value) ;
						} ;
					}
					param=param->NextSiblingElement() ;
				}
				if (doc->version_<38) {
					Variable *cvl=instr->FindVariable(SIP_CRUSHVOL) ;
					Variable *vol=instr->FindVariable(SIP_VOLUME);
					Variable *crs=instr->FindVariable(SIP_CRUSH) ;
					if ((vol)&&(cvl)&&(crs)) {
						if (crs->GetInt()!=16) {
							int temp=vol->GetInt() ;
							vol->SetInt(cvl->GetInt()) ;
							cvl->SetInt(temp) ;
						}
					} ;
				}
			}
		}
		current=current->NextSiblingElement() ;
	} ;
};

void InstrumentBank::SetType(int i,InstrumentType it) {
	I_Instrument *instr=instrument_[i] ;
	if (instr->GetType()==it) return ;
	// stash the leaving type's parameters (the RestoreContent replay
	// pattern), so coming back restores everything
	InstrumentType oldType=instr->GetType() ;
	if (oldType>=0&&oldType<IT_LAST) {
		std::vector<std::pair<std::string,std::string> > &st=stash_[i][oldType] ;
		st.clear() ;
		IteratorPtr<Variable> itv(instr->GetIterator()) ;
		for (itv->Begin();!itv->IsDone();itv->Next()) {
			Variable &v=itv->CurrentItem() ;
			st.push_back(std::make_pair(std::string(v.GetName()),
			                            std::string(v.GetString()))) ;
		}
	}
	delete instr ;
	switch (it) {
		case IT_MIDI: {
			MidiInstrument *m=new MidiInstrument() ;
			m->SetChannel(i&0x0F) ;
			instr=m ;
			break ;
		}
		case IT_SYNTH:
			instr=new SynthInstrument() ;
			break ;
		case IT_SAMPLE:
		default: {
			SampleInstrument *s=new SampleInstrument() ;
			s->AssignSample(-1) ;
			instr=s ;
			break ;
		}
	}
	instrument_[i]=instr ;
	instr->Init() ;
	// returning to a type this slot has seen: replay its settings
	if (it>=0&&it<IT_LAST) {
		std::vector<std::pair<std::string,std::string> > &rs=stash_[i][it] ;
		for (size_t k=0;k<rs.size();k++) {
			Variable *v=instr->FindVariable(rs[k].first.c_str()) ;
			if (v) v->SetString(rs[k].second.c_str()) ;
		}
	}
} ;

void InstrumentBank::Init() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		instrument_[i]->Init() ;
	}
}

unsigned short InstrumentBank::GetNext() {
	for (int i=0;i<MAX_SAMPLEINSTRUMENT_COUNT;i++) {
		SampleInstrument *si=(SampleInstrument *)instrument_[i] ;
		Variable *sample=si->FindVariable(SIP_SAMPLE) ;
		if (sample) {
			if (sample->GetInt()==-1) {
				return i ;
			}
		}
	}
	return NO_MORE_INSTRUMENT ;
} ;

unsigned short InstrumentBank::Clone(unsigned short i) {
	// can't clone midi/synth instruments: clones land in sample slots

	if (instrument_[i]->GetType()!=IT_SAMPLE) {
		return NO_MORE_INSTRUMENT ;
	}

	unsigned short next=GetNext() ;
	if (next==NO_MORE_INSTRUMENT) 
  {
		return NO_MORE_INSTRUMENT ;
	}

	I_Instrument *src=instrument_[i] ;
	I_Instrument *dst=instrument_[next] ;

  if (src == dst)
  {
		return NO_MORE_INSTRUMENT ;
	}

	delete dst ;
  
	if (src->GetType()==IT_SAMPLE) {
		dst=new SampleInstrument() ;
	} else {
		dst=new MidiInstrument() ;
	}
	instrument_[next]=dst ;
	IteratorPtr<Variable> it(src->GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &srcV=it->CurrentItem() ;
		Variable *dstV=dst->FindVariable(srcV.GetID()) ;
		if (dstV) {
			dstV->CopyFrom(srcV) ;
		}
	}
	return next ;

}

unsigned short InstrumentBank::CloneNext(unsigned short i,bool retype) {
	if (i>=MAX_INSTRUMENT_COUNT) return NO_MORE_INSTRUMENT ;
	I_Instrument *src=instrument_[i] ;
	if (!src) return NO_MORE_INSTRUMENT ;
	InstrumentType type=src->GetType() ;
	int next=-1 ;
	// the next empty slot of the same type, walking on from this one
	for (int k=1;k<MAX_INSTRUMENT_COUNT;k++) {
		int j=(i+k)%MAX_INSTRUMENT_COUNT ;
		if (instrument_[j]->IsEmpty()&&(instrument_[j]->GetType()==type)) { next=j ; break ; }
	}
	if ((next<0)&&retype) {
		for (int k=1;k<MAX_INSTRUMENT_COUNT;k++) {
			int j=(i+k)%MAX_INSTRUMENT_COUNT ;
			if (instrument_[j]->IsEmpty()) { next=j ; break ; }
		}
		if (next>=0) SetType(next,type) ;
	}
	if (next<0) return NO_MORE_INSTRUMENT ;
	I_Instrument *dst=instrument_[next] ;
	IteratorPtr<Variable> it(src->GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &srcV=it->CurrentItem() ;
		Variable *dstV=dst->FindVariable(srcV.GetID()) ;
		if (dstV) dstV->CopyFrom(srcV) ;
	}
	return (unsigned short)next ;
}

void InstrumentBank::OnStart() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		instrument_[i]->OnStart() ;
	}
	init_filters() ;
} ;
