#include "SamplePool.h"
#include "DrumKit.h"
#include <string.h>
#include <stdlib.h>
#include "System/Console/Trace.h"
#include "Application/Persistency/PersistencyService.h" 
#include "System/io/Status.h"
#include <string>
#include "SoundFontSample.h"
#include "SoundFontPreset.h"
#include "SoundFontManager.h"
#include "Application/Model/Config.h"
#include "SampleConvert.h"

#define SAMPLE_LIB "root:samplelib" 

SamplePool::SamplePool() {
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		names_[i]=NULL ;
		wav_[i]=NULL ;
	} ;
	count_=0 ;
	drumsBaked_=false ;
} ;

SamplePool::~SamplePool() {
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		SAFE_DELETE(wav_[i]) ;
		SAFE_FREE(names_[i]) ;
	} ;
} ;

const char *SamplePool::GetSampleLib() {
	Config *config=Config::GetInstance() ;
	const char *lib=config->GetValue("SAMPLELIB") ;
	return lib?lib:SAMPLE_LIB ;
} 

/* Synthesised at boot rather than read from anywhere. See DrumKit.h
   for why, and for why they are sources rather than a synth engine. */
SamplePool::ProgressFn SamplePool::progress_ = 0;

void SamplePool::SetProgressCallback(ProgressFn fn) { progress_ = fn; }

void SamplePool::report(const char *what, int done, int total) {
	if (progress_) progress_(what, done, total);
}

void SamplePool::bakeDrums() {
	for (int i=0;i<DRUMKIT_TOTAL;i++) {
		if (count_>=MAX_PIG_SAMPLES) return ;
		report(DrumKit::Name(i),i,DRUMKIT_TOTAL) ;
		BakedSource *src=DrumKit::Bake(i) ;
		if (!src) {
			// out of memory part way through a kit: keep what baked
			Trace::Error("[DRUMKIT] could not bake %s",DrumKit::Name(i)) ;
			return ;
		}
		wav_[count_]=src ;
		const char *name=DrumKit::Name(i) ;
		names_[count_]=(char *)SYS_MALLOC(strlen(name)+1) ;
		strcpy(names_[count_],name) ;
		count_++ ;
	}
}

void SamplePool::Reset() {
	/* Keep the baked kit; drop everything that came from the project.

	   The kit is synthesised, deterministic and identical every time,
	   so re-making it on every project close and open was pure work:
	   twenty four drums torn down and built again to arrive at exactly
	   the bytes that were just freed. It also churned about a megabyte
	   of the heap per song load, on a machine with 32.

	   Only entries at or above DRUMKIT_TOTAL came from this project's
	   samples folder, because bakeDrums always fills from zero. */
	int keep=drumsBaked_?DRUMKIT_TOTAL:0 ;
	if (keep>count_) keep=count_ ;   // never trust kit accounting blindly
	for (int i=keep;i<MAX_PIG_SAMPLES;i++) {
		SAFE_DELETE(wav_[i]) ;
		SAFE_FREE(names_[i]) ;
	} ;
	count_=keep ;
	SoundFontManager::GetInstance()->Reset() ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_INVALID_DIR, SLOAD_ERR_MAX_SAMPLES,
   SLOAD_ERR_MAX_SOUNDFONTS, SLOAD_ERR_MAX_SAMPLES | SLOAD_ERR_MAX_SOUNDFONTS}.
*/
unsigned int SamplePool::Load() {

    unsigned int result = SLOAD_OK;

	count_=0 ;

    /* The baked kit goes in first: it is the only thing a project can
       rely on being there. A song that uses it opens on a machine with
       nothing on the memory stick, which is what makes it possible to
       ship a demo at all.

       Baked once, then kept. Reset leaves the kit in place, so on
       every load after the first there is nothing to do but say where
       the samples start. If a previous bake ran out of memory part way
       through, drumsBaked_ stays false and the next load tries again. */
    if (drumsBaked_) {
        count_=DRUMKIT_TOTAL ;
    } else {
        report("drum kit",0,DRUMKIT_TOTAL);
        bakeDrums();
        drumsBaked_=(count_>=DRUMKIT_TOTAL) ;
        report("drum kit",DRUMKIT_TOTAL,DRUMKIT_TOTAL);
    }

    /* the pool says what it holds, once, where a log can catch it --
       the KIT/WAV split depends on this order and guessing at it from
       symptoms has cost days */
    Trace::Log("POOL","baked end at %d of %d",GetBakedEnd(),count_) ;
    for (int di=0;di<count_&&di<32;di++) {
        Trace::Log("POOL","%02d %s baked=%d",di,
                   names_[di]?names_[di]:"(null)",
                   wav_[di]?(int)wav_[di]->IsBaked():-1) ;
    }

    Path sampleDir("samples:");

    I_Dir *dir = FileSystem::GetInstance()->Open(sampleDir.GetPath().c_str());
    if (!dir) {
        // no samples folder is not an error any more -- the kit is
        // still a usable instrument set on its own
        return result;
    }

    // Then any wav files

    dir->GetContent("*.wav");
    IteratorPtr<Path> it(dir->GetIterator()) ;

	int wavCount=0 ;
	for(it->Begin();!it->IsDone();it->Next()) wavCount++ ;
	int wavIdx=0 ;
	for(it->Begin();!it->IsDone();it->Next()) {
		Path &path=it->CurrentItem() ;
		report(path.GetName().c_str(),wavIdx++,wavCount) ;
        Trace::Log("Load", "%s", path.GetCanonicalPath().c_str());
        // A wav this build can't read -- 24 bit, an odd sample rate --
        // used to simply not appear in the pool, with nothing said. The
        // instrument pointing at it then plays silence.
        if (loadSample(path.GetPath().c_str())!=SLOAD_OK) {
            result |= SLOAD_ERR_INPUT_FILE;
        }
		if (count_==MAX_PIG_SAMPLES) {
            result |= SLOAD_ERR_MAX_SAMPLES;
            Trace::Error("Warning maximum sample count reached");
            break;
		} ;

	} ;

	// now, let's look at soundfonts

	dir->GetContent("*.sf2") ;
	IteratorPtr<Path> it2(dir->GetIterator()) ;
    int sf_idx = 0;

    int sfCount=0 ;
    for (it2->Begin(); !it2->IsDone(); it2->Next()) sfCount++ ;
    for (it2->Begin(); !it2->IsDone(); it2->Next(), sf_idx++) {
        Path &path=it2->CurrentItem() ;
        report(path.GetName().c_str(),sf_idx,sfCount) ;
        Trace::Log("Load", "%s", path.GetCanonicalPath().c_str());
        if (loadSoundFont(path.GetPath().c_str())!=SLOAD_OK) {
            result |= SLOAD_ERR_INPUT_FILE;
        }
		if (sf_idx == MAX_SOUNDFONTS) {
            result |= SLOAD_ERR_MAX_SOUNDFONTS;
            Trace::Error("Warning maximum soundfont count reached");
            break;
		} ;
    };

    delete dir;

    // now sort the samples
    Sort();

    return result;
};

void SamplePool::Sort() {
    int rest=count_;
	while(rest>0) {
        int index = 0;
        for (int i=1;i<rest;i++) {
			if (strcmp(names_[i],names_[index])>0) {
                index = i;
            }
        }
        SoundSource *tWav = wav_[index];
		char *tName = names_[index];
		wav_[index] = wav_[rest-1];
		names_[index] = names_[rest-1];
		wav_[rest-1] = tWav;
        names_[rest - 1] = tName;
        rest--;
	}
}

int SamplePool::getIndexOf(const char *name) {
    for (int i=0;i<count_;i++) {
		if (strcmp(names_[i], name)==0) {
			return i;
		}
	}
	return -1;
}

int SamplePool::GetIndexOf(const char *name) {
	return getIndexOf(name) ;
}

int SamplePool::GetBakedEnd() {
	int i=0 ;
	while (i<count_&&wav_[i]&&wav_[i]->IsBaked()) i++ ;
	return i ;
}

SoundSource *SamplePool::GetSource(int i) {
	// indices come from instrument parameters, which come from the file
	if ((i<0)||(i>=MAX_PIG_SAMPLES)) return 0 ;
	return wav_[i] ;
} ;

char **SamplePool::GetNameList() {
	return names_ ;
} ;

int SamplePool::GetNameListSize() {
	return count_ ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_MAX_SAMPLES, SLOAD_ERR_INPUT_FILE}.
*/
int SamplePool::loadSample(const char *path) {

    if (count_==MAX_PIG_SAMPLES) return SLOAD_ERR_MAX_SAMPLES ;

Path sPath(path) ;
Status::Set("Loading %s", sPath.GetName().c_str());
Trace::Log("loadSample", "%s", path);

Path wavPath(path);
WavFile *wave = WavFile::Open(path);
if (wave) {
    wav_[count_] = wave;
    const std::string name = wavPath.GetName();
    names_[count_] = (char *)SYS_MALLOC(name.length() + 1);
    strcpy(names_[count_], name.c_str());
    count_++;
    if (!wave->GetBuffer(0, wave->GetSize(-1))) {
        /* opened fine, would not fit or would not read: a half-loaded
           sample left registered here was a null buffer handed to the
           sampler mid-song later. Back it out completely. */
        Trace::Error("Failed to buffer %s", wavPath.GetName().c_str());
        count_--;
        SYS_FREE(names_[count_]);
        names_[count_]=0;
        wav_[count_]=0;
        wave->Close();
        delete wave;
        return SLOAD_ERR_INPUT_FILE;
    }
    wave->Close();
    return SLOAD_OK;
} else {
    Trace::Error("Failed to load samples %s", wavPath.GetName().c_str());
    return SLOAD_ERR_INPUT_FILE;
}
}

/* 64KB heap chunks, not a 1000-byte stack buffer: a megabyte sample
   was two thousand read/write round trips to the Memory Stick (and
   1000 is not even sector-aligned). Falls back to smaller chunks if
   the heap is tight during an import. */
#define IMPORT_CHUNK_SIZE 65536

/* Byte-for-byte copy, for a file already in the shape the pool
   wants. Returns SLOAD_OK or a positive SLOAD_ERR code; a failed
   copy leaves nothing behind at dst. */
static int copyFile(Path &src,Path &dst) {
    FileSystem *fs=FileSystem::GetInstance() ;
    I_File *fin=fs->Open(src.GetPath().c_str(),"r") ;
    if (!fin) {
        Trace::Error("Failed to open input file %s",
                     src.GetCanonicalPath().c_str());
        return SLOAD_ERR_INPUT_FILE;
    };
    fin->Seek(0, SEEK_END);
    long size=fin->Tell() ;
    fin->Seek(0,SEEK_SET) ;

    I_File *fout=fs->Open(dst.GetPath().c_str(),"w") ;
    if (!fout) {
        fin->Close() ;
        delete (fin);
        return SLOAD_ERR_OUTPUT_FILE ;
    } ;

    int chunk=IMPORT_CHUNK_SIZE ;
    char *buffer=(char *)malloc(chunk) ;
    while (!buffer && chunk>4096) { chunk>>=1 ; buffer=(char *)malloc(chunk) ; }
    if (!buffer) {
        fin->Close() ; fout->Close() ;
        delete (fin) ; delete (fout) ;
        fs->Delete(dst.GetPath().c_str()) ;
        return SLOAD_ERR_OUTPUT_FILE ;
    }
    while (size>0) {
        int count=(size>chunk)?chunk:size ;
        fin->Read(buffer,1,count) ;
        fout->Write(buffer,1,count) ;
        size-=count ;
    } ;
    free(buffer) ;

    fin->Close() ;
    fout->Close() ;
    delete(fin) ;
    delete(fout) ;
    return SLOAD_OK ;
}

/*
  Copies the file into the project and loads it. A wav is written in
  the shape the pool keeps -- 16-bit PCM, the channels and rate asked
  for -- rather than copied as it came; a 16-bit file with nothing to
  change is copied byte for byte. Soundfonts carry their own shape and
  are always copied.

  A wav whose name the pool already holds REPLACES it: the old entry
  is unloaded and the new one lands at the end. The unload shifts every
  index above it, so callers move any instrument off the old index
  before this and back on to the returned one after.

  Returns a nonnegative int or an element of
  {-SLOAD_ERR_INVALID_DIR, -SLOAD_ERR_INPUT_FILE, -SLOAD_ERR_OUTPUT_FILE,
   -SLOAD_ERR_MAX_SAMPLES, -SLOAD_ERR_MAX_SOUNDFONTS}.
*/
int SamplePool::ImportSample(Path &path,const SampleImportOptions &opt) {

    if (count_ == MAX_PIG_SAMPLES)
        return -SLOAD_ERR_MAX_SAMPLES;

    FileSystem *fs=FileSystem::GetInstance() ;
    bool isWav=path.Matches("*.wav") ;

    // construct target path

    std::string dpath = "samples:";
    dpath+=path.GetName() ;
    Path dstPath(dpath.c_str()) ;

    /* Written beside its final name and moved into place afterwards,
       so a write that fails part way -- a full card -- cannot take
       the existing copy in the project with it. Load() only lists
       *.wav and *.sf2, so a temp left by a crash is never picked up. */
    std::string tpath=dpath+".imp" ;
    Path tmpPath(tpath.c_str()) ;

    int status=SLOAD_OK ;
    if (isWav) {
        WavFile *src=WavFile::Open(path.GetPath().c_str()) ;
        if (!src) {
            Trace::Error("Failed to read %s",path.GetCanonicalPath().c_str()) ;
            return -SLOAD_ERR_INPUT_FILE ;
        }
        bool convert=SampleConvert::NeedsConversion(src,opt) ;
        delete src ;
        if (convert) {
            Trace::Log("IMPORT","%s: %s, rate/%d",path.GetName().c_str(),
                       opt.mono?"mono":"channels kept",opt.rateDiv) ;
            SampleConvertResult r=SampleConvert::Convert(path.GetPath().c_str(),
                                                          tmpPath.GetPath().c_str(),opt) ;
            if (r==SCR_CANT_READ) status=SLOAD_ERR_INPUT_FILE ;
            else if (r!=SCR_OK) status=SLOAD_ERR_OUTPUT_FILE ;
        } else {
            status=copyFile(path,tmpPath) ;
        }
    } else {
        status=copyFile(path,tmpPath) ;
    }
    if (status!=SLOAD_OK) {
        // never leave half a file where anyone could find it
        fs->Delete(tmpPath.GetPath().c_str()) ;
        return -status ;
    }

    fs->Delete(dstPath.GetPath().c_str()) ;
    if (!fs->Rename(tmpPath.GetPath().c_str(),dstPath.GetPath().c_str())) {
        // no rename on this adapter: copy across and drop the temp
        status=copyFile(tmpPath,dstPath) ;
        fs->Delete(tmpPath.GetPath().c_str()) ;
        if (status!=SLOAD_OK) return -status ;
    }

    // the old entry under this name goes before the new one is loaded
    int existing=isWav?getIndexOf(path.GetName().c_str()):-1 ;
    if (existing>=0) {
        Trace::Log("IMPORT","%s replaces pool slot %d",path.GetName().c_str(),existing) ;
        unload(existing) ;
    }

    // now load the sample
    status = isWav ? loadSample(dstPath.GetPath().c_str())
                   : loadSoundFont(dstPath.GetPath().c_str());

    SetChanged();
    SamplePoolEvent ev ;
    ev.index_=count_-1 ;
    ev.type_=SPET_INSERT ;
    NotifyObservers(&ev);
    return !status ?(count_-1):(-status) ;
};


bool SamplePool::IsImported(std::string name) {
    std::string dpath="samples:";
    dpath += name;
    Path dstPath(dpath.c_str());
    Path checkPath(dstPath.GetPath());
    return checkPath.Exists();
}

/*
    Unsorted reassign for now
    Returns the index of the sample in the pool
    count_-1 position if new
    previous position if already imported
*/
int SamplePool::Reassign(std::string name, bool imported) {
    if (count_ == MAX_PIG_SAMPLES)
        return -1;
    int insertedIndex = getIndexOf(name.c_str());
    if (imported)
        unload(insertedIndex);

    std::string aliasPath = "samples:";
    aliasPath += name;
    Path dstPath(aliasPath.c_str());

    if (loadSample(dstPath.GetCanonicalPath().c_str())) {
        SetChanged();
        SamplePoolEvent ev;
        ev.index_ = getIndexOf(name.c_str());;
        ev.type_=SPET_INSERT;
        NotifyObservers(&ev);
        return ev.index_;
    }
    return -1;
}

void SamplePool::PurgeSample(int i) {

	/* The caller walks pool positions while this function shifts them
	   under it, so a stale index reaching here is a file deleted by
	   the wrong name, or a null read as a string. Neither is worth
	   risking to save a comparison. */
	if ((i<0)||(i>=count_)||(!names_[i])) {
		Trace::Error("purge asked for slot %d of %d",i,count_) ;
		return ;
	}

	// construct the path of the sample to delete

	std::string wavPath="samples:" ;
	wavPath+=names_[i] ;
	Path path(wavPath.c_str()) ;
	//delete wav
	SAFE_DELETE(wav_[i]) ;
	// delete name entry -- names_ comes from SYS_MALLOC (see Load), so
	// it has to go back through free; Reset does this correctly and
	// this path was calling delete on it
	SAFE_FREE(names_[i]) ;

	// delete file
	FileSystem::GetInstance()->Delete(path.GetPath().c_str()) ;

	// shift all entries from deleted to end
	for (int j=i;j<count_-1;j++) {
		wav_[j]=wav_[j+1] ;
		names_[j]=names_[j+1] ;
	} ;
	// decrease sample count
	count_-- ;
	wav_[count_]=0 ;
	names_[count_]=0 ;

	// now notify observers
	SetChanged() ;
	SamplePoolEvent ev ;
	ev.index_=i ;
	ev.type_=SPET_DELETE ;
	NotifyObservers(&ev) ;
} ;

void SamplePool::unload(int i) {

	// getIndexOf returns -1 when the name isn't in the pool, and
	// Reassign passes that straight through
	if ((i<0)||(i>=count_)) return ;

    // construct the path of the sample to delete

	std::string wavPath="samples:" ;
	wavPath+=names_[i] ;
	Path path(wavPath.c_str()) ;

	// The slot is about to be overwritten by the shift below, so free
	// the name -- observers re-read the list through GetNameList after
	// the notification, so no one is holding this string.
	//
	// wav_[i] is deliberately NOT deleted, and does leak. Unlike
	// PurgeSample, which only ever runs on samples no instrument
	// references, this runs on a sample that is in use by definition --
	// and SampleInstrument caches the SoundSource pointer in source_,
	// refreshed only by Init/updateInstrumentData. Deleting here would
	// leave any other instrument on the same sample pointing at freed
	// memory from the audio thread. A bounded leak beats that until
	// instruments re-resolve on pool changes.
	SAFE_FREE(names_[i]) ;

	// shift all entries from deleted to end
	for (int j=i;j<count_-1;j++) {
		wav_[j]=wav_[j+1] ;
		names_[j]=names_[j+1] ;
	} ;
	// decrease sample count
	count_-- ;
	wav_[count_]=0 ;
	names_[count_]=0 ;

	// now notify observers
	SetChanged() ;
	SamplePoolEvent ev ;
	ev.index_=i ;
	ev.type_=SPET_DELETE ;
	NotifyObservers(&ev) ;
}

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_MAX_SOUNDFONTS, SLOAD_ERR_INPUT_FILE}.
*/
int SamplePool::loadSoundFont(const char *path) {

    Path sPath(path);
    Status::Set("Loading %s", sPath.GetName().c_str());
    Trace::Log("loadSoundFont", "%s", path);

    sfBankID id = SoundFontManager::GetInstance()->LoadBank(path);
    if (id==-SF_BANK_TABLE_FULL) {
		return SLOAD_ERR_MAX_SOUNDFONTS ;
    } else if (id < 0) {
        return SLOAD_ERR_INPUT_FILE;
    }

    // Grab the sample offset

    long offset = sfGetSMPLOffset(id);

    // Add all presets of the sf

    WORD presetCount = 0;
    SFPRESETHDRPTR pHeaders=sfGetPresetHdrs(id,&presetCount); 

	for (int i=0;i<presetCount;i++) {
		if (count_<MAX_PIG_SAMPLES) {
			sfPresetHdr current=pHeaders[i] ;
			wav_[count_]=new SoundFontPreset(id,i) ;
			const char *name=pHeaders[i].achPresetName ;
            Trace::Log("loadSoundFont", "%s", name);
            names_[count_] = (char *)SYS_MALLOC(strlen(name) + 1);
            strcpy(names_[count_], name);
            count_++;
		}
	}
    /*
        // Get Sample information

        WORD headerCount=0 ;
        SFSAMPLEHDRPTR  &headers=sfGetSampHdrs(id,&headerCount );

        // Loop on every sample, add them

        for (int i=0;i<headerCount;i++) {
            if (count_<MAX_PIG_SAMPLES) {
                sfSampleHdr &current=headers[i] ;
                wav_[count_]=new SoundFontSample(current) ;
                const char *name=headers[i].achSampleName ;
                names_[count_]=(char*)SYS_MALLOC(strlen(name)+1) ;
                strcpy(names_[count_],name) ;
                count_++ ;
            }
        }
    */
    return SLOAD_OK;
} ;
