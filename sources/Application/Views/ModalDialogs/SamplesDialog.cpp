#include "SamplesDialog.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleInfo.h"
#include "Application/Instruments/SoundFontManager.h"
#include "Application/Player/Player.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "System/Console/Trace.h"
#include <stdio.h>
#include <string.h>

// rows 4..22 inside the FILES panel; 3 is the column header
#define LIST_ROWS 19

static void RemoveCallback(View &v,ModalView &dialog) {
	((SamplesDialog &)v).OnRemoveAnswer(dialog.GetReturnCode()==MBL_YES) ;
}

SamplesDialog::SamplesDialog(View &view):ModalView(view) {
	cursor_=0 ;
	top_=0 ;
	totalRam_=0 ;
	previewHeld_=false ;
	previewPending_=false ;
	removePending_=false ;
	removeIndex_=-1 ;
	bHeld_=false ;
	bChorded_=false ;
}

SamplesDialog::~SamplesDialog() {
}

void SamplesDialog::OnFocus() {
	rebuild() ;
}

/* the file name out of a path the manager kept */
static std::string leafOf(const char *path) {
	if (!path) return "" ;
	std::string s(path) ;
	std::string::size_type p=s.find_last_of("/\\:") ;
	return (p==std::string::npos)?s:s.substr(p+1) ;
}

void SamplesDialog::rebuild() {
	rows_.clear() ;
	totalRam_=0 ;

	SamplePool *pool=SamplePool::GetInstance() ;
	SoundFontManager *sfm=SoundFontManager::GetInstance() ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;

	// who is on what: the same walk the project's compaction makes
	int users[MAX_PIG_SAMPLES] ;
	std::string usedBy[MAX_PIG_SAMPLES] ;
	for (int i=0;i<MAX_PIG_SAMPLES;i++) users[i]=0 ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		I_Instrument *in=bank->GetInstrument(i) ;
		if (!in||in->GetType()!=IT_SAMPLE) continue ;
		int idx=((SampleInstrument *)in)->GetSampleIndex() ;
		if (idx<0||idx>=MAX_PIG_SAMPLES) continue ;
		users[idx]++ ;
		if (users[idx]<=6) {
			char slot[4] ;
			snprintf(slot,sizeof(slot),"%02X",i) ;
			if (!usedBy[idx].empty()) usedBy[idx]+=" " ;
			usedBy[idx]+=slot ;
		} else if (users[idx]==7) {
			usedBy[idx]+=" .." ;
		}
	}

	int n=pool->GetNameListSize() ;
	char **names=pool->GetNameList() ;
	for (int i=0;i<n;i++) {
		SoundSource *src=pool->GetSource(i) ;
		if (!src||src->IsBaked()) continue ;   // the kit is not a file
		if (src->IsMulti()) {
			// one row per soundfont, however many presets it became
			int b=pool->GetBankOf(i) ;
			int found=-1 ;
			for (size_t k=0;k<rows_.size();k++) {
				if (rows_[k].kind==K_BANK&&rows_[k].bankId==b) { found=(int)k ; break ; }
			}
			if (found<0) {
				Row r ;
				r.kind=K_BANK ;
				r.name=leafOf(sfm->GetBankPath(b)) ;
				if (r.name.empty()) r.name=names[i]?names[i]:"soundfont" ;
				r.poolIndex=i ;
				r.bankId=b ;
				r.presets=0 ;
				r.ramBytes=sfm->GetBankBytes(b) ;
				r.channels=src->GetChannelCount(60) ;
				r.rate=src->GetSampleRate(60) ;
				r.frames=src->GetSize(60) ;
				r.users=0 ;
				rows_.push_back(r) ;
				found=(int)rows_.size()-1 ;
				totalRam_+=r.ramBytes ;
			}
			Row &row=rows_[found] ;
			row.presets++ ;
			row.users+=users[i] ;
			if (!usedBy[i].empty()) {
				if (!row.usedBy.empty()) row.usedBy+=" " ;
				row.usedBy+=usedBy[i] ;
			}
		} else {
			Row r ;
			r.kind=K_WAV ;
			r.name=names[i]?names[i]:"" ;
			r.poolIndex=i ;
			r.bankId=-1 ;
			r.presets=0 ;
			r.channels=src->GetChannelCount(-1) ;
			r.rate=src->GetSampleRate(-1) ;
			r.frames=src->GetSize(-1) ;
			r.ramBytes=SampleInfo::RamBytes(r.channels,r.frames) ;
			r.usedBy=usedBy[i] ;
			r.users=users[i] ;
			rows_.push_back(r) ;
			totalRam_+=r.ramBytes ;
		}
	}

	/* Files in the folder the pool never took: an unreadable wav, a
	   soundfont past the bank limit. Invisible everywhere else, and
	   still on the card taking room. */
	Path dir("samples:") ;
	I_Dir *d=FileSystem::GetInstance()->Open(dir.GetPath().c_str()) ;
	if (d) {
		d->GetContent((char *)"*") ;
		d->Sort() ;
		IteratorPtr<Path> it(d->GetIterator()) ;
		for (it->Begin();!it->IsDone();it->Next()) {
			Path &p=it->CurrentItem() ;
			if (p.IsDirectory()) continue ;
			if (!(p.Matches("*.wav")||p.Matches("*.sf2"))) continue ;
			std::string name=p.GetName() ;
			bool known=false ;
			for (size_t k=0;k<rows_.size();k++) {
				if (rows_[k].name==name) { known=true ; break ; }
			}
			if (known) continue ;
			Row r ;
			r.kind=K_STRAY ;
			r.name=name ;
			r.poolIndex=-1 ;
			r.bankId=-1 ;
			r.presets=0 ;
			r.ramBytes=0 ;
			r.channels=0 ;
			r.rate=0 ;
			r.frames=0 ;
			r.users=0 ;
			rows_.push_back(r) ;
		}
		delete d ;
	}

	if (cursor_>=(int)rows_.size()) cursor_=(int)rows_.size()-1 ;
	if (cursor_<0) cursor_=0 ;
	if (top_>cursor_) top_=cursor_ ;
	isDirty_=true ;
}

void SamplesDialog::DrawView() {

	SetFullScreen() ;

	GUITextProperties props ;
	char line[41] ;

	char right[24],ram[8] ;
	SampleInfo::FormatBytes((unsigned long)totalRam_,ram,sizeof(ram)) ;
	snprintf(right,sizeof(right),"%d files  %s",(int)rows_.size(),ram) ;
	DrawTitleStrip("SAMPLES  in this project",right) ;

	DrawPanel(1,2,38,20,"FILES") ;
	SetColor(CD_ROW2) ;
	snprintf(line,sizeof(line),"%-20s %5s  %s","name","ram","used by") ;
	DrawString(2,3,line,props) ;

	if (rows_.empty()) {
		SetColor(CD_NORMAL) ;
		DrawString(2,5,"no sample files in this project",props) ;
	}
	if (cursor_<top_) top_=cursor_ ;
	if (cursor_>=top_+LIST_ROWS) top_=cursor_-LIST_ROWS+1 ;
	for (int i=0;i<LIST_ROWS;i++) {
		int k=top_+i ;
		if (k>=(int)rows_.size()) break ;
		const Row &r=rows_[k] ;
		char size[8] ;
		if (r.kind==K_STRAY) {
			snprintf(size,sizeof(size),"--") ;
		} else {
			SampleInfo::FormatBytes((unsigned long)r.ramBytes,size,sizeof(size)) ;
		}
		const char *used=(r.kind==K_STRAY)?"not loaded":
		                 (r.users?r.usedBy.c_str():"--") ;
		snprintf(line,sizeof(line),"%-20.20s %5s  %-10.10s",r.name.c_str(),size,used) ;
		if (k==cursor_) {
			SetColor(CD_HILITE2) ;
			props.invert_=true ;
		} else {
			SetColor(CD_NORMAL) ;
			props.invert_=false ;
		}
		DrawString(2,4+i,line,props) ;
	}
	props.invert_=false ;

	// what the highlighted one is, and who is on it
	DrawPanel(1,24,38,3,"DETAIL") ;
	SetColor(CD_NORMAL) ;
	if (!rows_.empty()) {
		const Row &r=rows_[cursor_] ;
		if (r.kind==K_WAV) {
			SampleInfo::Describe(r.channels,r.rate,r.frames,line,37) ;
		} else if (r.kind==K_BANK) {
			char b[8] ;
			SampleInfo::FormatBytes((unsigned long)r.ramBytes,b,sizeof(b)) ;
			snprintf(line,37,"soundfont, %d preset%s, %s",r.presets,
			         (r.presets==1)?"":"s",b) ;
		} else {
			snprintf(line,37,"on the card, but the pool refused it") ;
		}
		DrawString(2,25,line,props) ;
		if (r.kind==K_STRAY) {
			snprintf(line,37,"nothing plays it; safe to remove") ;
		} else if (r.users==0) {
			snprintf(line,37,"no instrument uses it") ;
		} else {
			snprintf(line,37,"used by %s",r.usedBy.c_str()) ;
		}
		SetColor(r.users?CD_HILITE2:CD_ROW2) ;
		DrawString(2,26,line,props) ;
	}
	if (!status_.empty()) {
		SetColor(CD_NORMAL) ;
		snprintf(line,37,"%s",status_.c_str()) ;
		DrawString(2,27,line,props) ;
	}
	SetColor(CD_NORMAL) ;
	DrawHintBar("O remove  SEL hear  X back  X+u/d page") ;
}

void SamplesDialog::move(int dir) {
	int size=(int)rows_.size() ;
	cursor_+=dir ;
	if (cursor_>=size) cursor_=size-1 ;
	if (cursor_<0) cursor_=0 ;
	// moving the cursor stops a preview and never starts one
	previewHeld_=false ;
	previewPending_=false ;
	endPreview() ;
	status_="" ;
	isDirty_=true ;
}

void SamplesDialog::ProcessButtonMask(unsigned short mask,bool pressed) {

	/* up/down walk, X+up/down page
	   SELECT held = hear the file under the cursor
	   O = remove it, after a yes/no that names who is on it
	   X = back to the project screen */

	if (!pressed) {
		if (previewHeld_&&!(mask&EPBM_SELECT)) {
			previewHeld_=false ;
			endPreview() ;
		}
		if (bHeld_&&!(mask&EPBM_B)) {
			bHeld_=false ;
			if (!bChorded_) {
				endPreview() ;
				EndModal(0) ;
			}
		}
		return ;
	}

	if (mask&EPBM_SELECT) {
		if (!rows_.empty()) {
			previewHeld_=true ;
			previewPending_=true ;   // started from ApplyDeferred, outside the lock
			isDirty_=true ;
		}
		return ;
	}

	// any press that is not SELECT ends a preview: releases can be missed
	if (previewHeld_) { previewHeld_=false ; endPreview() ; }

	if (mask&EPBM_B) {
		if (mask==EPBM_B) { bHeld_=true ; bChorded_=false ; }
		else bChorded_=true ;
		if (mask&EPBM_UP) move(-LIST_ROWS) ;
		if (mask&EPBM_DOWN) move(LIST_ROWS) ;
		return ;
	}

	if (mask&EPBM_A) {
		askRemove() ;
		return ;
	}

	if (mask==EPBM_UP) move(-1) ;
	if (mask==EPBM_DOWN) move(1) ;
}

void SamplesDialog::askRemove() {
	if (rows_.empty()) return ;
	/* a live voice holds a pointer into the buffer about to go, and
	   the instrument swap refuses for the same reason */
	if (Player::GetInstance()->IsRunning()) {
		status_="stop the song first" ;
		isDirty_=true ;
		return ;
	}
	const Row &r=rows_[cursor_] ;
	char name[16] ;
	snprintf(name,sizeof(name),"%s",r.name.c_str()) ;
	char msg[40] ;
	if (r.users>0) {
		snprintf(msg,sizeof(msg),"Remove %s (used by %d)?",name,r.users) ;
	} else {
		snprintf(msg,sizeof(msg),"Remove %s?",name) ;
	}
	removeIndex_=cursor_ ;
	// No is the default selection, so a stray O on the box cancels
	MessageBox *mb=new MessageBox(*this,msg,MBBF_YES|MBBF_NO) ;
	DoModal(mb,RemoveCallback) ;
}

void SamplesDialog::OnRemoveAnswer(bool yes) {
	if (yes) removePending_=true ;
	isDirty_=true ;
}

void SamplesDialog::ApplyDeferred() {
	if (previewPending_) {
		previewPending_=false ;
		preview() ;
	}
	if (removePending_) {
		removePending_=false ;
		doRemove() ;
	}
}

void SamplesDialog::preview() {
	if (rows_.empty()) return ;
	const Row &r=rows_[cursor_] ;
	Player *player=Player::GetInstance() ;
	bool ok=false ;
	if (r.kind==K_STRAY) {
		// not in the pool: the file itself, if it will play at all
		std::string p="samples:"+r.name ;
		Path path(p.c_str()) ;
		ok=player->StartStreaming(path) ;
	} else {
		SoundSource *src=SamplePool::GetInstance()->GetSource(r.poolIndex) ;
		if (src) {
			int note=src->IsMulti()?60:-1 ;
			ok=player->StartStreamingBuffer((const short *)src->GetSampleBuffer(note),
			                                src->GetSize(note),src->GetChannelCount(note),
			                                src->GetSampleRate(note)) ;
		}
	}
	if (!ok) {
		status_="can't play that one" ;
		isDirty_=true ;
	}
}

void SamplesDialog::endPreview() {
	Player::GetInstance()->StopStreaming() ;
}

void SamplesDialog::doRemove() {
	if (removeIndex_<0||removeIndex_>=(int)rows_.size()) return ;
	if (Player::GetInstance()->IsRunning()) {
		status_="stop the song first" ;
		isDirty_=true ;
		return ;
	}
	Row r=rows_[removeIndex_] ;   // a copy: rebuild drops the vector
	removeIndex_=-1 ;

	// the preview may be reading the very buffer about to go
	Player::GetInstance()->StopStreamingNow() ;

	SamplePool *pool=SamplePool::GetInstance() ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	if (r.kind==K_STRAY) {
		std::string p="samples:"+r.name ;
		Path path(p.c_str()) ;
		FileSystem::GetInstance()->Delete(path.GetPath().c_str()) ;
	} else {
		/* Whoever is on it steps off first, under the mixer lock so no
		   render is mid-block. Any voice still sounding it is cut -- a
		   release tail keeps a pointer into the buffer, sequencer
		   running or not -- then the instrument is set to no sample,
		   which drops its cached source and makes its Start return
		   false until something is assigned again. Only then does the
		   slot go, and nothing can reach the freed memory. */
		MixerService *ms=MixerService::GetInstance() ;
		ms->Lock() ;
		for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
			I_Instrument *in=bank->GetInstrument(i) ;
			if (!in||in->GetType()!=IT_SAMPLE) continue ;
			SampleInstrument *si=(SampleInstrument *)in ;
			int idx=si->GetSampleIndex() ;
			bool on=(r.kind==K_WAV)?(idx==r.poolIndex)
			                       :(idx>=0&&pool->GetBankOf(idx)==r.bankId) ;
			if (!on) continue ;
			Player::GetInstance()->CutInstrument(in) ;
			si->AssignSample(NO_SAMPLE) ;
		}
		ms->Unlock() ;
		if (r.kind==K_WAV) {
			pool->PurgeSample(r.poolIndex) ;
		} else {
			pool->RemoveBank(r.bankId) ;
		}
	}
	// the delete is queued in the card driver; land it before anything reads
	FileSystem::GetInstance()->Sync() ;
	Trace::Log("SAMPLES","removed %s",r.name.c_str()) ;
	status_="removed "+r.name ;
	rebuild() ;
}
