#include "ImportSampleDialog.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Views/ModalDialogs/MessageBox.h"

#define LIST_SIZE 15
#define LIST_WIDTH 28

bool ImportSampleDialog::initStatic_=false ;
Path ImportSampleDialog::sampleLib_("") ;
Path ImportSampleDialog::currentPath_("") ;

ImportSampleDialog::ImportSampleDialog(View &view):ModalView(view) {
	previewPending_=false ;
	previewHeld_=false ;
	bHeld_=false ;
	bChorded_=false ;
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

    SetWindow(LIST_WIDTH, LIST_SIZE + 3);

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

void ImportSampleDialog::ApplyDeferred() {
	if (!previewPending_) return ;
	previewPending_=false ;
	if (!Player::GetInstance()->StartStreaming(pendingPreview_)) {
		status_="can't play that file" ;
		isDirty_=true ;
	}
}

void ImportSampleDialog::endPreview() {
	Player::GetInstance()->StopStreaming() ;
}

void ImportSampleDialog::import(Path &element) {

	SamplePool *pool=SamplePool::GetInstance() ;
	int sampleID=pool->ImportSample(element) ;
	if (sampleID>=0) {
		I_Instrument *instr=viewData_->project_->GetInstrumentBank()->GetInstrument(toInstr_) ;
		if (instr->GetType()==IT_SAMPLE) {
			SampleInstrument *sinstr=(SampleInstrument *)instr ;
			sinstr->AssignSample(sampleID) ;
			toInstr_=viewData_->project_->GetInstrumentBank()->GetNext() ;
		};
		status_="imported "+element.GetName() ;
	} else {
        const char *err_str = (sampleID == -SLOAD_ERR_MAX_SAMPLES)
                                  ? "Maximum number of samples exceeded"
                              : (sampleID == -SLOAD_ERR_MAX_SOUNDFONTS)
                                  ? "Maximum number of SoundFonts exceeded"
                              : (sampleID == -SLOAD_ERR_INVALID_DIR)
                                  ? "Invalid directory"
                                  : "Unable to open file";
        Trace::Error(err_str);
        MessageBox *mb = new MessageBox(*this, err_str);
        View::DoModal(mb);
	};
	isDirty_=true ;
} ;

void ImportSampleDialog::ProcessButtonMask(unsigned short mask,bool pressed) {

	/* The grammar every other screen already taught:
	     up/down walk, B+up/down page
	     SELECT held = hear the file (walk while holding to hear each)
	     A = take what is under the cursor: enter a folder, import a file
	     B = step out of the folder; at the library root, close
	   The old three-button row with its own left/right cursor is gone --
	   pressing A meant listen OR import OR exit depending on invisible
	   state, which is exactly why previews felt hit and miss. */

	if (!pressed) {
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
			import(*element) ;
		}
		return ;
	}

	if (mask==EPBM_UP) warpToNextSample(-1) ;
	if (mask==EPBM_DOWN) warpToNextSample(1) ;
} ;

bool ImportSampleDialog::isSampleLibRoot()
{
    // return sampleLib_.GetPath().find(currentPath_.GetPath()) != std::string::npos; // Causes issues in Win, Miyoo
	return currentPath_.GetPath()==sampleLib_.GetPath();
};

Path* ImportSampleDialog::getImportElement() {
	IteratorPtr<Path> it(sampleList_.GetIterator());
	int count = 0;
	Path *element = 0;
	for(it->Begin(); !it->IsDone(); it->Next()) {
		if (count++ == currentSample_) {
			return &it->CurrentItem();
		}
	}
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
