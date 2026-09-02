#include "ImportSampleDialog.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleInfo.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Player/Player.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include <stdio.h>
#include <string.h>

// the FILES panel: rows 3..22, seventeen cells of name
#define LIST_SIZE 20
#define NAME_CELLS 17

bool ImportSampleDialog::initStatic_=false ;
Path ImportSampleDialog::sampleLib_("") ;
Path ImportSampleDialog::currentPath_("") ;
SampleImportOptions ImportSampleDialog::options_ ;

static void ReplaceCallback(View &v,ModalView &dialog) {
	((ImportSampleDialog &)v).OnReplaceAnswer(dialog.GetReturnCode()==MBL_YES) ;
}

static bool isWavPath(Path &p) { return !p.IsDirectory()&&p.Matches("*.wav") ; }
static bool isSfPath(Path &p) { return !p.IsDirectory()&&p.Matches("*.sf2") ; }

ImportSampleDialog::ImportSampleDialog(View &view):ModalView(view) {
	currentSample_=0 ;
	topIndex_=0 ;
	toInstr_=0 ;
	previewPending_=false ;
	previewHeld_=false ;
	bHeld_=false ;
	bChorded_=false ;
	page_=P_LIST ;
	optRow_=0 ;
	infoPending_=false ;
	infoValid_=false ;
	shapePending_=false ;
	srcChannels_=0 ;
	srcRate_=0 ;
	srcBits_=0 ;
	srcFloat_=false ;
	srcFrames_=0 ;
	srcBytes_=0 ;
	importPhase_=0 ;
	if (!initStatic_) {
		const char *slpath=SamplePool::GetInstance()->GetSampleLib() ;
		sampleLib_=Path(slpath) ;
		currentPath_=Path(slpath) ;
		initStatic_=true ;
	}
} ;

ImportSampleDialog::~ImportSampleDialog() {
} ;

void ImportSampleDialog::DrawView() {

	SetFullScreen() ;

	// where we are, and what the machine has spent so far
	char title[40] ;
	if (page_==P_SETTINGS) {
		snprintf(title,sizeof(title),"IMPORT  %s",chosen_.GetName().c_str()) ;
	} else {
		std::string folder=currentPath_.GetName() ;
		snprintf(title,sizeof(title),"IMPORT  [%s]",folder.c_str()) ;
	}
	title[26]=0 ;   // leave the right side its room
	char right[16] ;
	{
		unsigned int used=System::GetInstance()->GetMemoryUsage() ;
		int tenths=(int)((unsigned long long)used*10/(1024*1024)) ;
		snprintf(right,sizeof(right),"used %d.%dM",tenths/10,tenths%10) ;
	}
	DrawTitleStrip(title,right) ;

	if (page_==P_SETTINGS) {
		drawSettings() ;
	} else {
		drawList() ;
		drawChoice() ;
	}

	// the status line, full width under the panels
	if (!status_.empty()) {
		GUITextProperties props ;
		char line[40] ;
		SetColor(CD_NORMAL) ;
		snprintf(line,39,"%s",status_.c_str()) ;
		DrawString(1,24,line,props) ;
	}

	DrawHintBar((page_==P_SETTINGS)
	            ?"TRI import  SEL hear  O+arw set  X back"
	            :"TRI choose  SEL hear  O folder  X back") ;
} ;

void ImportSampleDialog::drawList() {

	GUITextProperties props ;
	char buffer[256] ;
	char line[NAME_CELLS+1] ;

	DrawPanel(1,2,18,LIST_SIZE,"FILES") ;

	if (currentSample_<topIndex_) topIndex_=currentSample_ ;
	if (currentSample_>=topIndex_+LIST_SIZE) topIndex_=currentSample_-LIST_SIZE+1 ;

	IteratorPtr<Path> it(sampleList_.GetIterator()) ;
	int count=0 ;
	int y=3 ;
	for(it->Begin();!it->IsDone();it->Next()) {
		if ((count>=topIndex_)&&(count<topIndex_+LIST_SIZE)) {
			Path &current=it->CurrentItem() ;
			const std::string p=current.GetName() ;
			if (count==currentSample_) {
				SetColor(CD_HILITE2) ;
				props.invert_=true ;
			} else {
				SetColor(CD_NORMAL) ;
				props.invert_=current.Matches("*.sf2") ;
			}
			if (!current.IsDirectory()) {
				snprintf(buffer,sizeof(buffer),"%s",p.c_str()) ;
			} else {
				snprintf(buffer,sizeof(buffer),"[%s]",p.c_str()) ;
			}
			snprintf(line,sizeof(line),"%-17.17s",buffer) ;
			DrawString(2,y,line,props) ;
			y++ ;
		}
		count++ ;
	} ;
	props.invert_=false ;
	SetColor(CD_NORMAL) ;
} ;

/* page one, the right column: what is under the cursor, and what
   TRIANGLE will do about it */
void ImportSampleDialog::drawChoice() {

	GUITextProperties props ;
	char line[41] ;
	Path *element=getImportElement() ;
	bool isWav=element&&isWavPath(*element) ;
	bool isSf=element&&isSfPath(*element) ;
	bool haveInfo=isWav&&infoValid_&&(infoName_==element->GetName()) ;

	DrawPanel(20,2,19,4,"FILE") ;
	SetColor(CD_NORMAL) ;
	if (!element) {
		DrawString(21,3,"nothing here",props) ;
	} else {
		snprintf(line,19,"%s",element->GetName().c_str()) ;
		DrawString(21,3,line,props) ;
		if (element->IsDirectory()) {
			SetColor(CD_ROW2) ;
			DrawString(21,4,"folder",props) ;
		} else if (isSf) {
			SetColor(CD_ROW2) ;
			DrawString(21,4,"soundfont",props) ;
			DrawString(21,5,"copied as it is",props) ;
		} else if (!haveInfo) {
			SetColor(CD_ROW2) ;
			DrawString(21,4,infoPending_?"reading":"can't read it",props) ;
		} else {
			char secs[12],bytes[8],ram[8] ;
			SampleInfo::FormatSeconds(srcRate_,srcFrames_,secs,sizeof(secs)) ;
			SampleInfo::FormatBytes((unsigned long)srcBytes_,bytes,sizeof(bytes)) ;
			SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(srcChannels_,srcFrames_),
			                        ram,sizeof(ram)) ;
			snprintf(line,19,"%s %d %d%s",SampleInfo::Channels(srcChannels_),srcRate_,
			         srcBits_,srcFloat_?"f":"b") ;
			DrawString(21,4,line,props) ;
			snprintf(line,19,"%s  file %s",secs,bytes) ;
			DrawString(21,5,line,props) ;
			snprintf(line,19,"in ram %s",ram) ;
			DrawString(21,6,line,props) ;
		}
	}

	DrawPanel(20,8,19,3,"NEXT") ;
	SetColor(CD_ROW2) ;
	if (!element) {
		DrawString(21,9,"nothing to do",props) ;
	} else if (element->IsDirectory()) {
		DrawString(21,9,"O opens it",props) ;
	} else if (isSf) {
		DrawString(21,9,"TRI imports the",props) ;
		DrawString(21,10,"soundfont",props) ;
	} else {
		DrawString(21,9,"TRI sets how it",props) ;
		DrawString(21,10,"is stored, then",props) ;
		DrawString(21,11,"imports it",props) ;
	}
	SetColor(CD_NORMAL) ;
} ;

/* page two: the chosen file, how the project will keep it, and what
   TRIANGLE will do */
void ImportSampleDialog::drawSettings() {

	GUITextProperties props ;
	char line[41] ;
	bool isSf=isSfPath(chosen_) ;
	bool haveInfo=!isSf&&infoValid_&&(infoName_==chosen_.GetName()) ;

	// ---- FILE: what it is ----
	DrawPanel(1,2,38,3,"FILE") ;
	SetColor(CD_NORMAL) ;
	snprintf(line,37,"%s",chosen_.GetName().c_str()) ;
	DrawString(2,3,line,props) ;
	if (isSf) {
		SetColor(CD_ROW2) ;
		DrawString(2,4,"soundfont: every preset, copied as it is",props) ;
	} else if (!haveInfo) {
		SetColor(CD_ROW2) ;
		DrawString(2,4,infoPending_?"reading the header":"can't read it",props) ;
	} else {
		char secs[12],bytes[8],ram[8] ;
		SampleInfo::FormatSeconds(srcRate_,srcFrames_,secs,sizeof(secs)) ;
		SampleInfo::FormatBytes((unsigned long)srcBytes_,bytes,sizeof(bytes)) ;
		SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(srcChannels_,srcFrames_),
		                        ram,sizeof(ram)) ;
		snprintf(line,37,"%s %dHz %d%s  %s",SampleInfo::Channels(srcChannels_),srcRate_,
		         srcBits_,srcFloat_?"f":"bit",secs) ;
		DrawString(2,4,line,props) ;
		snprintf(line,37,"file %s   in ram %s",bytes,ram) ;
		DrawString(2,5,line,props) ;
	}

	// ---- STORE: how the project will keep it ----
	DrawPanel(1,7,38,5,"STORE") ;
	int outCh=haveInfo?SampleConvert::OutChannels(srcChannels_,options_):(options_.mono?1:2) ;
	int outRate=haveInfo?SampleConvert::OutRate(srcRate_,options_):0 ;
	long outFrames=haveInfo?SampleConvert::OutFrames(srcFrames_,options_):0 ;
	bool canFold=!haveInfo||(srcChannels_>1) ;

	if (isSf) {
		SetColor(CD_ROW2) ;
		DrawString(2,8,"nothing to set for a soundfont",props) ;
	} else {
		SetColor(CD_ROW2) ;
		props.invert_=false ;
		DrawString(2,8,"chan",props) ;
		DrawString(2,9,"rate",props) ;

		// the focused value reads like a list cursor
		SetColor((optRow_==0)?CD_HILITE2:CD_NORMAL) ;
		props.invert_=(optRow_==0) ;
		if (canFold) {
			snprintf(line,sizeof(line)," %s ",options_.mono?"mono":"stereo") ;
		} else {
			snprintf(line,sizeof(line)," mono ") ;   // a mono source has nothing to fold
		}
		DrawString(10,8,line,props) ;

		SetColor((optRow_==1)?CD_HILITE2:CD_NORMAL) ;
		props.invert_=(optRow_==1) ;
		if (haveInfo) {
			snprintf(line,sizeof(line)," %dHz ",outRate) ;
		} else {
			snprintf(line,sizeof(line)," %s ",(options_.rateDiv==1)?"as is":
			                                  (options_.rateDiv==2)?"half":"quarter") ;
		}
		DrawString(10,9,line,props) ;
		props.invert_=false ;
		SetColor(CD_ROW2) ;
		if (options_.rateDiv>1) {
			snprintf(line,sizeof(line),"/%d",options_.rateDiv) ;
			DrawString(20,9,line,props) ;
		}
		DrawString(24,8,"left/right set",props) ;
		DrawString(24,9,"up/down row",props) ;

		SetColor(CD_NORMAL) ;
		if (haveInfo) {
			char ram[8] ;
			SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(outCh,outFrames),
			                        ram,sizeof(ram)) ;
			snprintf(line,37,"in ram  %s",ram) ;
			DrawString(2,10,line,props) ;
		}
	}
	if (!isSf&&SamplePool::GetInstance()->GetIndexOf(chosen_.GetName().c_str())>=0) {
		SetColor(CD_HILITE2) ;
		DrawString(2,11,"replaces the copy this project holds",props) ;
	}

	// ---- IMPORT: what TRIANGLE will do ----
	DrawPanel(1,13,38,3,"IMPORT") ;
	if (isSf) {
		SetColor(CD_NORMAL) ;
		DrawString(2,14,"TRI imports the soundfont, every preset",props) ;
	} else if (haveInfo) {
		bool converts=(options_.rateDiv>1)||(options_.mono&&srcChannels_>1)||
		              (srcBits_!=16)||srcFloat_||(srcChannels_>2) ;
		char ram[8] ;
		SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(outCh,outFrames),
		                        ram,sizeof(ram)) ;
		SetColor(CD_ROW2) ;
		DrawString(2,14,"TRI imports as",props) ;
		SetColor(converts?CD_HILITE2:CD_NORMAL) ;
		snprintf(line,37,"%s %dHz  %s in ram",SampleInfo::Channels(outCh),outRate,ram) ;
		DrawString(17,14,line,props) ;
	} else {
		SetColor(CD_ROW2) ;
		DrawString(2,14,"TRI imports it once the header is read",props) ;
	}
	SetColor(CD_ROW2) ;
	DrawString(2,15,"SEL hears it as it will be stored",props) ;
	DrawString(2,16,"X goes back to the list, nothing taken",props) ;
	SetColor(CD_NORMAL) ;
} ;

void ImportSampleDialog::warpToNextSample(int direction) {

	currentSample_+=direction ;
	int size=sampleList_.Size() ;
	if (currentSample_ >= size) currentSample_ = size - 1;
	if (currentSample_ < 0) currentSample_ = 0;
	/* moving the cursor always STOPS the preview and never starts one.
	   Following the cursor while SELECT was held meant one press
	   played everything you browsed past -- you press to hear a file,
	   not to arm a mode. */
	previewHeld_=false ;
	previewPending_=false ;   // and drop anything queued but not yet started
	endPreview() ;
	// the FILE panel follows the cursor; the header read waits for
	// ApplyDeferred like every other file open
	infoValid_=false ;
	infoPending_=true ;
	status_="" ;
	isDirty_=true ;
}

void ImportSampleDialog::OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) {
} ;

void ImportSampleDialog::OnFocus() {
	Path current(currentPath_) ;
	setCurrentFolder(&current) ;
	toInstr_=viewData_->currentInstrument_ ;
	page_=P_LIST ;
	infoValid_=false ;
	infoPending_=true ;
} ;

void ImportSampleDialog::preview(Path &element) {
	// queued: the press arrives inside the input path's mixer lock,
	// and starting a stream now opens a file -- ApplyDeferred runs it
	// outside every lock the render needs
	pendingPreview_=element ;
	previewPending_=true ;
}

void ImportSampleDialog::endPreview() {
	Player::GetInstance()->StopStreaming() ;
}

void ImportSampleDialog::readInfo() {
	Path *element=(page_==P_SETTINGS)?&chosen_:getImportElement() ;
	infoValid_=false ;
	if (element&&isWavPath(*element)) {
		// header only: Open reads the chunks, not the data
		WavFile *w=WavFile::Open(element->GetPath().c_str()) ;
		if (w) {
			srcChannels_=w->GetChannelCount(-1) ;
			srcRate_=w->GetSampleRate(-1) ;
			srcBits_=w->GetBitDepth() ;
			srcFloat_=w->IsFloat() ;
			srcFrames_=w->GetSize(-1) ;
			srcBytes_=w->GetFileBytes() ;
			infoName_=element->GetName() ;
			infoValid_=true ;
			delete w ;
		}
	}
	isDirty_=true ;
}

void ImportSampleDialog::stepOption(int dir) {
	if (optRow_==0) {
		if (!infoValid_||srcChannels_>1) options_.mono=!options_.mono ;
	} else {
		int d=options_.rateDiv ;
		if (dir>0) d=(d<4)?d*2:1 ;
		else d=(d>1)?d/2:4 ;
		options_.rateDiv=d ;
	}
	// a running preview follows the setting -- from ApplyDeferred, since
	// the shape change takes the mixer lock and this press holds it
	shapePending_=true ;
	isDirty_=true ;
}

/* TRIANGLE on the list: a file becomes the subject of the settings
   page. Folders are O's business. */
void ImportSampleDialog::choose() {
	Path *element=getImportElement() ;
	if (!element||element->IsDirectory()) {
		status_="O opens a folder; TRI chooses a file" ;
		isDirty_=true ;
		return ;
	}
	chosen_=*element ;
	page_=P_SETTINGS ;
	optRow_=0 ;
	status_="" ;
	if (!(infoValid_&&infoName_==chosen_.GetName())) {
		infoValid_=false ;
		infoPending_=true ;
	}
	isDirty_=true ;
}

void ImportSampleDialog::queueImport(Path &element) {
	pendingImport_=element ;
	importPhase_=1 ;
	status_="importing "+element.GetName() ;
	isDirty_=true ;
}

/* TRIANGLE on the settings page: the import, or the question first
   when the project already holds the name */
void ImportSampleDialog::askImport() {
	if (isWavPath(chosen_)&&
	    SamplePool::GetInstance()->GetIndexOf(chosen_.GetName().c_str())>=0) {
		// No is the answer a stray press gets
		char name[16] ;
		snprintf(name,sizeof(name),"%s",chosen_.GetName().c_str()) ;
		char msg[40] ;
		snprintf(msg,sizeof(msg),"Replace %s?",name) ;
		MessageBox *mb=new MessageBox(*this,msg,MBBF_YES|MBBF_NO) ;
		DoModal(mb,ReplaceCallback) ;
		return ;
	}
	endPreview() ;
	queueImport(chosen_) ;
}

void ImportSampleDialog::OnReplaceAnswer(bool yes) {
	if (yes) {
		endPreview() ;
		queueImport(chosen_) ;
	}
	isDirty_=true ;
}

void ImportSampleDialog::ApplyDeferred() {
	Player *player=Player::GetInstance() ;
	if (previewPending_) {
		previewPending_=false ;
		if (!player->StartStreaming(pendingPreview_)) {
			status_="can't play that file" ;
			isDirty_=true ;
		} else {
			// heard as it will be stored
			player->SetStreamingShape(options_.mono,options_.rateDiv) ;
		}
	}
	if (shapePending_) {
		shapePending_=false ;
		if (player->IsStreaming()) {
			player->SetStreamingShape(options_.mono,options_.rateDiv) ;
		}
	}
	if (infoPending_) {
		infoPending_=false ;
		readInfo() ;
	}
	if (importPhase_==1) {
		/* announced on this redraw, run on the next: Redraw calls in
		   here BEFORE it paints, so doing the work now would leave the
		   status line saying nothing for the seconds it takes. The
		   next redraw is the release of the button that asked. */
		importPhase_=2 ;
		isDirty_=true ;
		return ;
	}
	if (importPhase_==2) {
		importPhase_=0 ;
		import(pendingImport_,options_) ;
		// back to the list, with the result on the status line
		page_=P_LIST ;
		infoValid_=false ;
		infoPending_=true ;
	}
}

void ImportSampleDialog::import(Path &element,const SampleImportOptions &opt) {

	SamplePool *pool=SamplePool::GetInstance() ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	std::string name=element.GetName() ;

	/* Importing a name this project already holds REPLACES it. The
	   pool drops the old entry and shifts everything above it down,
	   and an instrument still pointing at the slot would land on a
	   neighbour -- so everyone on it steps off first and back on to
	   the new entry after. Only wavs: soundfont presets are listed by
	   preset name, never by file. */
	int old=element.Matches("*.wav")?pool->GetIndexOf(name.c_str()):-1 ;
	bool onOld[MAX_INSTRUMENT_COUNT] ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		onOld[i]=false ;
		if (old<0) continue ;
		I_Instrument *in=bank->GetInstrument(i) ;
		if (in&&in->GetType()==IT_SAMPLE&&
		    ((SampleInstrument *)in)->GetSampleIndex()==old) {
			onOld[i]=true ;
			((SampleInstrument *)in)->AssignSample(NO_SAMPLE) ;
		}
	}

	int sampleID=pool->ImportSample(element,opt) ;
	if (sampleID>=0) {
		for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
			if (onOld[i]) {
				((SampleInstrument *)bank->GetInstrument(i))->AssignSample(sampleID) ;
			}
		}
		I_Instrument *instr=bank->GetInstrument(toInstr_) ;
		if (instr->GetType()==IT_SAMPLE) {
			SampleInstrument *sinstr=(SampleInstrument *)instr ;
			sinstr->AssignSample(sampleID) ;
			toInstr_=bank->GetNext() ;
		};
		// say what the project now holds, not what was asked for
		SoundSource *src=pool->GetSource(sampleID) ;
		char line[40] ;
		char shortName[15] ;
		snprintf(shortName,sizeof(shortName),"%s",name.c_str()) ;
		if (src&&!src->IsMulti()) {
			char ram[8] ;
			int ch=src->GetChannelCount(-1) ;
			SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(ch,src->GetSize(-1)),
			                        ram,sizeof(ram)) ;
			snprintf(line,sizeof(line),"%s: %s %dHz %s",shortName,SampleInfo::Channels(ch),
			         src->GetSampleRate(-1),ram) ;
		} else {
			snprintf(line,sizeof(line),"imported %s",shortName) ;
		}
		status_=line ;
	} else {
		/* the old entry survives a failed write -- the pool only drops
		   it once the new file is in place -- so put everyone back */
		if (old>=0&&pool->GetIndexOf(name.c_str())==old) {
			for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
				if (onOld[i]) {
					((SampleInstrument *)bank->GetInstrument(i))->AssignSample(old) ;
				}
			}
		}
        const char *err_str = (sampleID == -SLOAD_ERR_MAX_SAMPLES)
                                  ? "Maximum number of samples exceeded"
                              : (sampleID == -SLOAD_ERR_MAX_SOUNDFONTS)
                                  ? "Maximum number of SoundFonts exceeded"
                              : (sampleID == -SLOAD_ERR_INVALID_DIR)
                                  ? "Invalid directory"
                              : (sampleID == -SLOAD_ERR_OUTPUT_FILE)
                                  ? "Can't write to the project"
                                  : "Unable to open file";
        Trace::Error(err_str);
        status_=err_str ;
        MessageBox *mb = new MessageBox(*this, err_str);
        View::DoModal(mb);
	};
	isDirty_=true ;
} ;

void ImportSampleDialog::ProcessButtonMask(unsigned short mask,bool pressed) {

	/* Two pages, one grammar:
	     list:     up/down walk, left/right jump a page, O enters a
	               folder, TRIANGLE chooses a file, X steps out of the
	               folder and closes at the root
	     settings: up/down pick the row, left/right (O held or not)
	               change the value, TRIANGLE imports, X goes back to
	               the list
	     SELECT held = hear the file, as it will be stored, on either */

	if (!pressed) {
		/* the import runs on the redraw after the one that announced
		   it, and a release only redraws if something says so */
		if (importPhase_==2) isDirty_=true ;
		if (previewHeld_&&!(mask&EPBM_SELECT)) {
			previewHeld_=false ;
			endPreview() ;
		}
		if (bHeld_&&!(mask&EPBM_B)) {
			bHeld_=false ;
			if (!bChorded_) {
				if (page_==P_SETTINGS) {
					page_=P_LIST ;
					status_="" ;
					infoValid_=false ;
					infoPending_=true ;
					isDirty_=true ;
				} else if (isSampleLibRoot()) {
					endPreview() ;
					EndModal(0) ;
				} else {
					Path parent=currentPath_.GetParent() ;
					setCurrentFolder(&parent) ;
					status_="" ;
					isDirty_=true ;
				}
			}
		}
		return ;
	}

	if (page_==P_SETTINGS) {
		processSettingsButtons(mask) ;
	} else {
		processListButtons(mask) ;
	}
} ;

void ImportSampleDialog::processListButtons(unsigned short mask) {

	if (mask&EPBM_TRIANGLE) {
		endPreview() ;
		previewHeld_=false ;
		choose() ;
		return ;
	}

	if (mask&EPBM_SELECT) {
		Path *element=getImportElement() ;
		if (element&&isWavPath(*element)) {
			previewHeld_=true ;
			preview(*element) ;
			/* WITHOUT THIS the press queued a preview and nothing
			   redrew, so ApplyDeferred never ran -- and the NEXT
			   d-pad press, which does redraw, started the file the
			   select had asked for. */
			isDirty_=true ;
		}
		return ;
	}

	/* any press that is not SELECT ends a preview: releases can be
	   missed (a full event queue once did exactly that), and a preview
	   that outlives the button is the complaint we are fixing */
	if (previewHeld_) { previewHeld_=false ; endPreview() ; }

	if (mask&EPBM_B) {
		// a bare press arms the release; anything chorded disarms it
		if (mask==EPBM_B) { bHeld_=true ; bChorded_=false ; }
		else bChorded_=true ;
		return ;
	}

	if (mask&EPBM_A) {
		Path *element=getImportElement() ;
		if (!element) return ;
		if (element->IsDirectory()) {
			if (element->GetName()=="..") {
				if (!isSampleLibRoot()) {
					Path parent=element->GetParent().GetParent() ;
					setCurrentFolder(&parent) ;
				}
			} else {
				setCurrentFolder(element) ;
			}
			status_="" ;
			isDirty_=true ;
		} else {
			// O never takes a file; the key that does is named
			status_="TRI chooses a file" ;
			isDirty_=true ;
		}
		return ;
	}

	if (mask==EPBM_UP) warpToNextSample(-1) ;
	if (mask==EPBM_DOWN) warpToNextSample(1) ;
	if (mask==EPBM_LEFT) warpToNextSample(-LIST_SIZE) ;
	if (mask==EPBM_RIGHT) warpToNextSample(LIST_SIZE) ;
} ;

void ImportSampleDialog::processSettingsButtons(unsigned short mask) {

	if (mask&EPBM_TRIANGLE) {
		askImport() ;
		return ;
	}

	if (mask&EPBM_SELECT) {
		if (isWavPath(chosen_)) {
			previewHeld_=true ;
			preview(chosen_) ;
			isDirty_=true ;
		}
		return ;
	}

	/* no preview cut here: changing a setting while the file plays is
	   the point -- the sound follows the setting */

	if (mask&EPBM_B) {
		if (mask==EPBM_B) { bHeld_=true ; bChorded_=false ; }
		else bChorded_=true ;
		return ;
	}

	if (isSfPath(chosen_)) return ;   // nothing to set

	// the rows are toggles, so O held or not, an arrow does the same
	if (mask&(EPBM_UP|EPBM_DOWN)) {
		if (mask&EPBM_A) {
			stepOption((mask&EPBM_UP)?1:-1) ;
		} else {
			optRow_=1-optRow_ ;
			isDirty_=true ;
		}
		return ;
	}
	if (mask&EPBM_LEFT) { stepOption(-1) ; return ; }
	if (mask&EPBM_RIGHT) { stepOption(1) ; return ; }
} ;

bool ImportSampleDialog::isSampleLibRoot()
{
    // return sampleLib_.GetPath().find(currentPath_.GetPath()) != std::string::npos; // Causes issues in Win, Miyoo
	return currentPath_.GetPath()==sampleLib_.GetPath();
};

Path* ImportSampleDialog::getImportElement() {
	IteratorPtr<Path> it(sampleList_.GetIterator());
	int count = 0;
	for(it->Begin(); !it->IsDone(); it->Next()) {
		if (count++ == currentSample_) {
			return &it->CurrentItem();
		}
	}
	return 0 ;
}

void ImportSampleDialog::setCurrentFolder(Path *path) {

	Path formerPath(currentPath_) ;

	topIndex_=0 ;
	currentSample_=0 ;

	currentPath_=Path(*path) ;
	sampleList_.Empty() ;
	if (path) {
		int count=0 ;
		I_Dir *dir=FileSystem::GetInstance()->Open(path->GetPath().c_str()) ;
		if (dir) {
			dir->GetContent((char *)"*") ;
			dir->Sort() ;
			IteratorPtr<Path>it(dir->GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Path &current=it->CurrentItem() ;
		 		if (current.IsDirectory()) {
					if (current.GetName().substr(0,1)!="." || current.GetName()=="..") {
						Path *sample=new Path(current) ;
						sampleList_.Insert(sample) ;
						if (!formerPath.Compare(current)) {
							currentSample_=count ;
						}
						count++ ;
					}
				}
			}
			for (it->Begin();!it->IsDone();it->Next()) {
                Path &current = it->CurrentItem();
                if (!current.IsDirectory()) {
                                  if ((current.Matches("*.wav") || current.Matches("*.sf2")) && current.GetName()[0]!='.') {
						Path *sample=new Path(current) ;
						sampleList_.Insert(sample) ;
					}
				};
			}
			delete dir ;
		}
	}
	// the FILE panel follows
	infoValid_=false ;
	infoPending_=true ;
} ;
