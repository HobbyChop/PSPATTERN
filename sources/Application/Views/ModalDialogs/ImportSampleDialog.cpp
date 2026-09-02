#include "ImportSampleDialog.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SampleInfo.h"
#include "Application/Instruments/WavFile.h"
#include "Application/Views/ModalDialogs/MessageBox.h"

#define LIST_SIZE 15
#define LIST_WIDTH 28

bool ImportSampleDialog::initStatic_=false ;
Path ImportSampleDialog::sampleLib_("") ;
Path ImportSampleDialog::currentPath_("") ;
SampleImportOptions ImportSampleDialog::options_ ;

ImportSampleDialog::ImportSampleDialog(View &view):ModalView(view) {
	previewPending_=false ;
	previewHeld_=false ;
	bHeld_=false ;
	bChorded_=false ;
	inOptions_=false ;
	optionsPending_=false ;
	optRow_=0 ;
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
	// the same window for both steps, so nothing jumps between them
	SetWindow(LIST_WIDTH, LIST_SIZE + 3);
	if (inOptions_) {
		drawOptions() ;
	} else {
		drawList() ;
	}
} ;

void ImportSampleDialog::drawList() {

    GUITextProperties props;

    // Where we are: the folder, on the top rule
    SetColor(CD_NORMAL);
    {
        char title[LIST_WIDTH] ;
        std::string folder=currentPath_.GetName() ;
        snprintf(title,sizeof(title),"[%s]",folder.c_str()) ;
        DrawString(1,0,title,props) ;
    }

    // Draw samples

    int x = 1;
    int y=1 ;

	if (currentSample_<topIndex_) {
		topIndex_=currentSample_ ;
	} ;
	if (currentSample_>=topIndex_+LIST_SIZE) {
		topIndex_=currentSample_ ;
	} ;

	IteratorPtr<Path> it(sampleList_.GetIterator()) ;
	int count=0 ;
	char buffer[256] ;
	for(it->Begin();!it->IsDone();it->Next()) {
		if ((count>=topIndex_)&&(count<topIndex_+LIST_SIZE)) {
			Path &current=it->CurrentItem() ;
			const std::string p=current.GetName() ;

			if (count==currentSample_) {
				SetColor(CD_HILITE2) ;
				props.invert_=true ;
			} else {
				SetColor(CD_NORMAL) ;
                props.invert_ = current.Matches("*.sf2");
            }
			if (!current.IsDirectory()) {
				strcpy(buffer,p.c_str()) ;
			} else {
				buffer[0]='[' ;
				strcpy(buffer+1,p.c_str()) ;
				strcat(buffer,"]") ;
			}
			buffer[LIST_WIDTH-1]=0 ;
			DrawString(x,y,buffer,props) ;
			y+=1 ;
		}
		count++ ;
	} ;

	/* one transient status line (imported X / can't play Y), and the
	   keys, always visible -- the old three-button row needed its own
	   cursor and was most of why importing felt like a puzzle */
	SetColor(CD_NORMAL) ;
	props.invert_=false ;
	if (!status_.empty()) {
		char line[LIST_WIDTH] ;
		snprintf(line,sizeof(line),"%s",status_.c_str()) ;
		DrawString(1,LIST_SIZE+1,line,props) ;
	}
	DrawString(1,LIST_SIZE+2,"O take  X back  SEL hear",props) ;
} ;

void ImportSampleDialog::drawOptions() {

	GUITextProperties props ;
	char line[LIST_WIDTH] ;

	// the file, on the top rule where the folder was
	SetColor(CD_NORMAL) ;
	std::string name=optPath_.GetName() ;
	snprintf(line,sizeof(line),"[%s]",name.c_str()) ;
	DrawString(1,0,line,props) ;

	// what it is
	char secs[12],bytes[8],ram[8] ;
	SampleInfo::FormatSeconds(srcRate_,srcFrames_,secs,sizeof(secs)) ;
	SampleInfo::FormatBytes((unsigned long)srcBytes_,bytes,sizeof(bytes)) ;
	SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(srcChannels_,srcFrames_),
	                        ram,sizeof(ram)) ;
	snprintf(line,sizeof(line),"%s %dHz %d%s",SampleInfo::Channels(srcChannels_),
	         srcRate_,srcBits_,srcFloat_?"f":"bit") ;
	DrawString(1,2,line,props) ;
	snprintf(line,sizeof(line),"%s  file %s  ram %s",secs,bytes,ram) ;
	DrawString(1,3,line,props) ;

	// how it will be stored
	int outCh=SampleConvert::OutChannels(srcChannels_,options_) ;
	int outRate=SampleConvert::OutRate(srcRate_,options_) ;
	long outFrames=SampleConvert::OutFrames(srcFrames_,options_) ;
	bool canFold=(srcChannels_>1) ;

	SetColor(CD_ROW2) ;
	props.invert_=false ;
	DrawString(1,5,"channels",props) ;
	DrawString(1,6,"rate",props) ;

	// the focused row reads like the list cursor
	SetColor((optRow_==0)?CD_HILITE2:CD_NORMAL) ;
	props.invert_=(optRow_==0) ;
	if (canFold) {
		snprintf(line,sizeof(line),"< %s >",SampleInfo::Channels(outCh)) ;
	} else {
		// a mono source has nothing to fold
		snprintf(line,sizeof(line),"  %s",SampleInfo::Channels(outCh)) ;
	}
	DrawString(11,5,line,props) ;

	SetColor((optRow_==1)?CD_HILITE2:CD_NORMAL) ;
	props.invert_=(optRow_==1) ;
	if (options_.rateDiv>1) {
		snprintf(line,sizeof(line),"< %dHz > /%d",outRate,options_.rateDiv) ;
	} else {
		snprintf(line,sizeof(line),"< %dHz >",outRate) ;
	}
	DrawString(11,6,line,props) ;

	SetColor(CD_NORMAL) ;
	props.invert_=false ;
	SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(outCh,outFrames),
	                        ram,sizeof(ram)) ;
	snprintf(line,sizeof(line),"in ram    %s",ram) ;
	DrawString(1,8,line,props) ;

	if (SamplePool::GetInstance()->GetIndexOf(name.c_str())>=0) {
		SetColor(CD_HILITE2) ;
		DrawString(1,10,"replaces the project copy",props) ;
	}

	SetColor(CD_NORMAL) ;
	if (!status_.empty()) {
		snprintf(line,sizeof(line),"%s",status_.c_str()) ;
		DrawString(1,LIST_SIZE+1,line,props) ;
	}
	DrawString(1,LIST_SIZE+2,"O import  X back  SEL hear",props) ;
} ;

void ImportSampleDialog::warpToNextSample(int direction) {

	currentSample_+=direction ;
	int size=sampleList_.Size() ;
	if (currentSample_ < 0) currentSample_ = 0;
	if (currentSample_ >= size) currentSample_ = size - 1;
	/* moving the cursor always STOPS the preview and never starts one.
	   Following the cursor while SELECT was held meant one press
	   played everything you browsed past -- you press to hear a file,
	   not to arm a mode. */
	previewHeld_=false ;
	previewPending_=false ;   // and drop anything queued but not yet started
	endPreview() ;
	isDirty_=true ;
}

void ImportSampleDialog::OnPlayerUpdate(PlayerEventType ,unsigned int currentTick) {
} ;

void ImportSampleDialog::OnFocus() {
	Path current(currentPath_) ;
	setCurrentFolder(&current) ;
	toInstr_=viewData_->currentInstrument_ ;
} ;

void ImportSampleDialog::preview(Path &element) {
	// queued: the press arrives inside the input path's mixer lock,
	// and starting a stream now opens a file -- ApplyDeferred runs it
	// outside every lock the render needs
	pendingPreview_=element ;
	previewPending_=true ;
}

void ImportSampleDialog::queueImport(Path &element) {
	pendingImport_=element ;
	importPhase_=1 ;
	status_="importing "+element.GetName() ;
	isDirty_=true ;
}

void ImportSampleDialog::ApplyDeferred() {
	if (previewPending_) {
		previewPending_=false ;
		if (!Player::GetInstance()->StartStreaming(pendingPreview_)) {
			status_="can't play that file" ;
			isDirty_=true ;
		}
	}
	if (optionsPending_) {
		optionsPending_=false ;
		openOptions(pendingOptions_) ;
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
		if (inOptions_) closeOptions() ;
	}
}

void ImportSampleDialog::endPreview() {
	Player::GetInstance()->StopStreaming() ;
}

void ImportSampleDialog::openOptions(Path &element) {
	// header only: Open reads the chunks, not the data
	WavFile *w=WavFile::Open(element.GetPath().c_str()) ;
	if (!w) {
		status_="can't read that file" ;
		isDirty_=true ;
		return ;
	}
	srcChannels_=w->GetChannelCount(-1) ;
	srcRate_=w->GetSampleRate(-1) ;
	srcBits_=w->GetBitDepth() ;
	srcFloat_=w->IsFloat() ;
	srcFrames_=w->GetSize(-1) ;
	srcBytes_=w->GetFileBytes() ;
	delete w ;
	optPath_=element ;
	optRow_=0 ;
	inOptions_=true ;
	status_="" ;
	isDirty_=true ;
}

void ImportSampleDialog::closeOptions() {
	inOptions_=false ;
	isDirty_=true ;
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
		if (src&&!src->IsMulti()) {
			char ram[8],line[LIST_WIDTH] ;
			int ch=src->GetChannelCount(-1) ;
			SampleInfo::FormatBytes((unsigned long)SampleInfo::RamBytes(ch,src->GetSize(-1)),
			                        ram,sizeof(ram)) ;
			snprintf(line,sizeof(line),"imported %s %dk %s",SampleInfo::Channels(ch),
			         src->GetSampleRate(-1)/1000,ram) ;
			status_=line ;
		} else {
			status_="imported "+name ;
		}
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

	/* The grammar every other screen already taught:
	     up/down walk, B+up/down page
	     SELECT held = hear the file (walk while holding to hear each)
	     A = take what is under the cursor: enter a folder, or choose
	         a file -- a wav opens the options step, a soundfont
	         imports at once
	     B = step out of the folder; at the library root, close
	   The old three-button row with its own left/right cursor is gone --
	   pressing A meant listen OR import OR exit depending on invisible
	   state, which is exactly why previews felt hit and miss. */

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
				if (isSampleLibRoot()) {
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

	if (inOptions_) {
		processOptionButtons(mask) ;
	} else {
		processListButtons(mask) ;
	}
} ;

void ImportSampleDialog::processListButtons(unsigned short mask) {

	if (mask&EPBM_SELECT) {
		Path *element=getImportElement() ;
		if (element&&!element->IsDirectory()&&!element->Matches("*.sf2")) {
			previewHeld_=true ;
			preview(*element) ;
			/* WITHOUT THIS the press queued a preview and nothing
			   redrew, so ApplyDeferred never ran -- and the NEXT
			   d-pad press, which does redraw, started the file the
			   select had asked for. Press did nothing, moving played
			   the one before: the funk the tester described. */
			isDirty_=true ;
		}
		return ;
	}

	/* any press that is not SELECT ends a preview: releases can be
	   missed (a full event queue once did exactly that), and a preview
	   that outlives the button is the complaint we are fixing */
	if (previewHeld_) { previewHeld_=false ; endPreview() ; }

	if (mask&EPBM_B) {
		if (mask==EPBM_B) { bHeld_=true ; bChorded_=false ; }
		else bChorded_=true ;
		if (mask&EPBM_UP) warpToNextSample(-LIST_SIZE) ;
		if (mask&EPBM_DOWN) warpToNextSample(LIST_SIZE) ;
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
			endPreview() ;
			if (element->Matches("*.sf2")) {
				// soundfonts carry their own shape: copied as they are
				queueImport(*element) ;
			} else {
				pendingOptions_=*element ;
				optionsPending_=true ;
				isDirty_=true ;
			}
		}
		return ;
	}

	if (mask==EPBM_UP) warpToNextSample(-1) ;
	if (mask==EPBM_DOWN) warpToNextSample(1) ;
} ;

void ImportSampleDialog::processOptionButtons(unsigned short mask) {

	/* up/down walk the two rows, left/right step the value, SELECT
	   still plays the original, O imports, X goes back to the list */

	if (mask&EPBM_SELECT) {
		previewHeld_=true ;
		preview(optPath_) ;
		isDirty_=true ;
		return ;
	}

	if (previewHeld_) { previewHeld_=false ; endPreview() ; }

	if (mask&EPBM_B) {
		/* the release that follows would step out of the folder;
		   mark it chorded so it does nothing */
		bHeld_=true ;
		bChorded_=true ;
		closeOptions() ;
		return ;
	}

	if (mask&EPBM_A) {
		endPreview() ;
		queueImport(optPath_) ;
		return ;
	}

	if (mask==EPBM_UP||mask==EPBM_DOWN) {
		optRow_=1-optRow_ ;
		isDirty_=true ;
		return ;
	}

	if (mask==EPBM_LEFT||mask==EPBM_RIGHT) {
		int dir=(mask==EPBM_RIGHT)?1:-1 ;
		if (optRow_==0) {
			if (srcChannels_>1) options_.mono=!options_.mono ;
		} else {
			int d=options_.rateDiv ;
			if (dir>0) d=(d<4)?d*2:1 ;
			else d=(d>1)?d/2:4 ;
			options_.rateDiv=d ;
		}
		isDirty_=true ;
	}
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
			dir->GetContent("*") ;
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
		}
	}
} ;
