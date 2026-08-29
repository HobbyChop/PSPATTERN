#include "ProjectView.h"
#include "Services/Audio/Audio.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/ProjectDatas.h"
#include "Application/Model/Scale.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "Application/Views/ModalDialogs/NewProjectDialog.h"
#include "Application/Views/ModalDialogs/SelectProjectDialog.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIField.h"
#include "BaseClasses/UIIntVarField.h"
#include "Application/AppWindow.h"
#include "BaseClasses/UISliderField.h"
#include "BaseClasses/UIPillField.h"
#include "BaseClasses/UIStepperField.h"
#include "BaseClasses/UIStaticField.h"
#include "BaseClasses/UITempoField.h"
#include "Services/Midi/MidiService.h"
#include "System/System/System.h"
#include "Application/Version.h"

#define ACTION_PURGE            MAKE_FOURCC('P','U','R','G')
#define ACTION_SAVE             MAKE_FOURCC('S','A','V','E')
#define ACTION_SAVE_AS          MAKE_FOURCC('S','V','A','S')
#define ACTION_LOAD             MAKE_FOURCC('L','O','A','D')
#define ACTION_QUIT             MAKE_FOURCC('Q','U','I','T')
#define ACTION_PURGE_INSTRUMENT MAKE_FOURCC('P','R','G','I')
#define ACTION_TEMPO_CHANGED    MAKE_FOURCC('T','E','M','P')

static void SaveAsProjectCallback(View &v,ModalView &dialog) {

    FileSystemService FSS;
    NewProjectDialog &npd=(NewProjectDialog &)dialog;

    if (dialog.GetReturnCode()>0) {
		std::string str_dstprjdir;
		std::string str_dstsmpdir;

		Path root("root:");
		str_dstprjdir = root.GetName() + "/" + npd.GetName();
		str_dstsmpdir = str_dstprjdir + "/samples/";

		Path path_srcprjdir("project:");
		Path path_srcsmpdir("project:samples");
		Path path_dstprjdir = Path(str_dstprjdir);
		Path path_dstsmpdir = Path(str_dstsmpdir);

        Path path_srclgptdatsav = path_srcprjdir.GetPath() + "lgptsav_tmp.dat";
        Path path_dstlgptdatsav = path_dstprjdir.GetPath() + "/lgptsav.dat";

		// Every failure below used to be a Trace line and a silent
		// return: you pressed save-as, nothing visible happened, and
		// the half-written lgptsav_tmp.dat stayed in the old project.
		if (path_dstprjdir.Exists()) {
			Trace::Log("ProjectView", "Dst Dir '%s' Exist == true",
			path_dstprjdir.GetPath().c_str());
			FSS.Delete(path_srclgptdatsav);
			v.SetNotification("that name is taken");
			return;
		}

		if (FileSystem::GetInstance()->MakeDir(path_dstprjdir.GetPath().c_str()).Failed()) {
			Trace::Log("ProjectView", "Failed to create dir '%s'", path_dstprjdir.GetPath().c_str());
			FSS.Delete(path_srclgptdatsav);
			v.SetNotification("could not create project");
			return;
		};

		if (FileSystem::GetInstance()->MakeDir(path_dstsmpdir.GetPath().c_str()).Failed()) {
			Trace::Log("ProjectView", "Failed to create sample dir '%s'", path_dstprjdir.GetPath().c_str());
			FSS.Delete(path_srclgptdatsav);
			v.SetNotification("could not create samples dir");
			return;
		};

		if (FSS.Copy(path_srclgptdatsav, path_dstlgptdatsav) > -1) {
			FSS.Delete(path_srclgptdatsav);
		} else {
			Trace::Log("ProjectView", "Failed to copy save to '%s'",
			path_dstlgptdatsav.GetPath().c_str());
			FSS.Delete(path_srclgptdatsav);
			v.SetNotification("could not write project");
			return;
		}

		I_Dir *idir_srcsmpdir =
			FileSystem::GetInstance()->Open(path_srcsmpdir.GetPath().c_str());
		if (idir_srcsmpdir) {
			idir_srcsmpdir->GetContent("*");
			idir_srcsmpdir->Sort();
			IteratorPtr<Path>it(idir_srcsmpdir->GetIterator());
			bool allCopied=true;
			for (it->Begin();!it->IsDone();it->Next()) {
				Path &current=it->CurrentItem();
				if (current.IsFile()) {
					Path dstfile = Path((str_dstsmpdir+current.GetName()).c_str());
					Path srcfile = Path(current.GetPath());
					if (FSS.Copy(srcfile.GetPath(),dstfile.GetPath())<0) {
						Trace::Log("ProjectView", "Failed to copy sample '%s'",
						current.GetName().c_str());
						allCopied=false;
					}
				}
			}
			delete idir_srcsmpdir;
			// The project itself is written, so switch to it either way
			// -- but say so, because a copy that lost samples looks
			// exactly like one that didn't until you hit play.
			if (!allCopied) v.SetNotification("saved, some samples failed");
		}

		((ProjectView &)v).OnSaveAsProject((char*)str_dstprjdir.c_str());
    }
}

static void LoadCallback(View &v,ModalView &dialog) {
    MixerService::GetInstance()->SetRenderMode(0);
    if (dialog.GetReturnCode()==MBL_YES) {
		((ProjectView &)v).OnLoadProject() ;
	}
} ;

static void QuitCallback(View &v,ModalView &dialog) {
    MixerService::GetInstance()->SetRenderMode(0);
    if (dialog.GetReturnCode()==MBL_YES) {
		((ProjectView &)v).OnQuit() ;
	}
} ;

static void PurgeCallback(View &v,ModalView &dialog) {
	((ProjectView &)v).OnPurgeInstruments(dialog.GetReturnCode()==MBL_YES) ;
} ;

ProjectView::ProjectView(GUIWindow &w,ViewData *data):FieldView(w,data) {

    lastClock_ = 0;
    lastTick_ = 0;

	project_=data->project_ ;

	// two-column mockup layout: SONG/MIX/MIDI left, FILE right
	GUIPoint position(2,6) ;

	Variable *v=project_->FindVariable(VAR_TEMPO) ;
	UITempoField *f = new UITempoField(ACTION_TEMPO_CHANGED, position, *v,
	                                   "tempo  %d [%2.2x]  ", 60, 400, 1, 10);
	T_SimpleList<UIField>::Insert(f) ;
	f->AddObserver(*this) ;
	tempoField_=f ;

	position=GUIPoint(2,7) ;
	v = project_->FindVariable(VAR_TRANSPOSE);
	UIStepperField *st=new UIStepperField(position,*v,"transp ","%3.2d",-48,48) ;
	T_SimpleList<UIField>::Insert(st) ;

	v = project_->FindVariable(VAR_SCALE);
	if (v->GetInt() < 0) {
		v->SetInt(0);
	}
	position=GUIPoint(2,8) ;
	UIIntVarField *field =
	    // One space, not two, and clipped one cell wider: the name had
	    // 12 cells, and at 12 "Melodic minor" and "Neapolitan major"
	    // were indistinguishable from their siblings.
	    new UIIntVarField(position, *v, "scale %s", 0, scaleCount - 1, 1, 10,
	                      0, 19);
	T_SimpleList<UIField>::Insert(field);

	position=GUIPoint(2,9) ;
	v = project_->FindVariable(VAR_MASTERVOL);
	UISliderField *slider =
	    new UISliderField(position, *v, "master ", 10, 100, 1, 10, 7);
	T_SimpleList<UIField>::Insert(slider);

	position=GUIPoint(2,10) ;
	v = project_->FindVariable(VAR_LOOP);
	NAssert(v);
	field = new UIIntVarField(position, *v, "repeat %s", 0, 1, 1, 1);
	T_SimpleList<UIField>::Insert(field);

	position=GUIPoint(2,13) ;
	v = project_->FindVariable(VAR_PREGAIN);
	slider = new UISliderField(position, *v, "drive  ", 10, 200, 1, 10, 7);
	T_SimpleList<UIField>::Insert(slider);

	position=GUIPoint(2,14) ;
	v = project_->FindVariable(VAR_SOFTCLIP);
	field = new UIIntVarField(position, *v, "clip   %s", 0, 4, 1, 4);
	T_SimpleList<UIField>::Insert(field);

	position=GUIPoint(2,15) ;
	v = project_->FindVariable(VAR_SOFTCLIP_GAIN);
	UIPillField *pf=new UIPillField(position,*v,"gain   ",2) ;
	T_SimpleList<UIField>::Insert(pf) ;

	position=GUIPoint(2,21) ;
	v = project_->FindVariable(VAR_MIDISYNC);
	UIStepperField *sy=new UIStepperField(position,*v,"sync   ","%s",0,
	                                      MAX_MIDISYNC_MODE-1) ;
	T_SimpleList<UIField>::Insert(sy) ;

	position=GUIPoint(2,19) ;
	v = project_->FindVariable(VAR_MIDIDEVICE);
	NAssert(v);
	// max is the last valid INDEX, not the count -- stepping one past the
	// end selected "(null)", which silently killed MIDI
	field = new UIIntVarField(position, *v, "device %s", 0,
	                          MidiService::GetInstance()->Size() - 1, 1, 1);
	T_SimpleList<UIField>::Insert(field);

	// right column: FILE actions
	position=GUIPoint(23,6) ;
	UIActionField *a1 = new UIActionField("load song", ACTION_LOAD, position);
	a1->AddObserver(*this);
	T_SimpleList<UIField>::Insert(a1);

	position=GUIPoint(23,8) ;
	a1 = new UIActionField("save song", ACTION_SAVE, position);
	a1->AddObserver(*this);
	T_SimpleList<UIField>::Insert(a1);

	position=GUIPoint(23,10) ;
	a1 = new UIActionField("save song as", ACTION_SAVE_AS, position);
	a1->AddObserver(*this);
	T_SimpleList<UIField>::Insert(a1);

	position=GUIPoint(23,12) ;
	a1 = new UIActionField("compact seq", ACTION_PURGE, position);
	a1->AddObserver(*this);
	T_SimpleList<UIField>::Insert(a1);

	position=GUIPoint(23,14) ;
	a1 = new UIActionField("compact inst", ACTION_PURGE_INSTRUMENT,
	                       position);
	a1->AddObserver(*this);
	T_SimpleList<UIField>::Insert(a1);

	position=GUIPoint(23,16) ;
	v = project_->FindVariable(VAR_RENDER);
	NAssert(v);
	field = new UIIntVarField(position, *v, "render %s", 0,
	                          project_->MAX_RENDER_MODE - 1, 1, 2);
	T_SimpleList<UIField>::Insert(field);

	position=GUIPoint(23,19) ;
	a1 = new UIActionField("exit", ACTION_QUIT, position);
	a1->AddObserver(*this);
	T_SimpleList<UIField>::Insert(a1);

}

ProjectView::~ProjectView() {
}

void ProjectView::ProcessButtonMask(unsigned short mask,bool pressed) {

    if (!pressed)
        return;

    FieldView::ProcessButtonMask(mask);

    if (mask & EPBM_R) {
        if (mask&EPBM_DOWN) {
			ViewType vt=VT_SONG;
			ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
			SetChanged();
            NotifyObservers(&ve);
        }
        if (mask&EPBM_RIGHT) {
            ViewType vt=VT_CONFIG;
            ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
            SetChanged();
            NotifyObservers(&ve);
        }
    } else {
        if (mask&EPBM_START) {
            Player *player = Player::GetInstance();

            int renderMode = viewData_->renderMode_;
			if (renderMode > 0 && !player->IsRunning()) {
				viewData_->isRendering_ = true;
				View::SetNotification("Rendering started!");
			} else if (viewData_->isRendering_ && player->IsRunning()) {
				viewData_->isRendering_ = false;
				View::SetNotification("Rendering done!");
			}

			player->OnStartButton(PM_SONG,viewData_->songX_,false,viewData_->songX_) ;
		}
    };
} ;

void ProjectView::DrawView() {

	Clear() ;
	View::EnableNotification() ;

	DrawTitleStrip("PROJECT",PSPATTERN_VERSION_STRING) ;

	// 20 is as wide as these go before the frame meets FILE's at
	// cell 22; the scale name needs every cell it can get.
	// Five rows, not four: tempo, transpose, scale, master, repeat.
	// repeat was added to the box and the box was not grown with it,
	// so it was drawn on the bottom border rather than inside it.
	DrawPanel(1,5,20,5,"SONG") ;
	DrawPanel(1,12,20,3,"MIX") ;
	DrawPanel(1,18,20,3,"MIDI") ;

	// the rate the audio device actually opened at. Everything is
	// timed from it, so if a song plays at the wrong speed this is
	// the first thing to look at.
	{
		GUITextProperties props ;
		char rate[24] ;
		int hz=Audio::GetInstance()->GetSampleRate() ;
		sprintf(rate,"%d.%dkHz",hz/1000,(hz%1000)/100) ;
		SetColor(CD_ROW2) ;
		DrawString(2,20,"rate",props) ;
		SetColor((hz==44100)?CD_NORMAL:CD_HILITE2) ;
		DrawString(9,20,rate,props) ;
		SetColor(CD_NORMAL) ;
	}
	// 12, not 11: the action rows are boxed too, and the last box's
	// bottom rule and the panel's landed on adjacent pixel rows -- one
	// spare row keeps them from reading as a doubled rail
	DrawPanel(22,5,17,12,"FILE") ;

	// framed action rows
	AppWindow &app=(AppWindow &)w_ ;
	int rows[]={6,8,10,12,14,16} ;
	for (int i=0;i<6;i++) {
		app.OpFrame(23*8-4,rows[i]*8-2,15*8+8,12,CD_ROW) ;
	}
	app.OpFrame(23*8-4,19*8-2,6*8+8,12,CD_ROW) ;

	// maker's mark next to exit
	{
		GUITextProperties props ;
		SetColor(CD_ROW2) ;
		// col 31 put the last "p" hard against the right bezel
		DrawString(30,19,"hobbychop",props) ;
		SetColor(CD_NORMAL) ;
	}

	FieldView::Redraw() ;
	DrawHintBar("O select  R+v song") ;
} ;

void ProjectView::Update(Observable &,I_ObservableData *data) {

	if (!hasFocus_) {
		return ;
	}

# ifdef _64BIT
	int fourcc=*((int*)data);
#else
    int fourcc = (unsigned int)data;
#endif

    UIField *focus = GetFocus();
    if (fourcc!= ACTION_TEMPO_CHANGED) {
		focus->ClearFocus() ;
		focus->Draw(w_) ;
		w_.Flush() ;
		focus->SetFocus() ;
	} else {
		focus=tempoField_ ;
	}
    Player *player = Player::GetInstance();
    switch (fourcc) {
		case ACTION_PURGE:
			project_->Purge() ;
			break ;
		case ACTION_PURGE_INSTRUMENT:
		{
            MessageBox *mb = new MessageBox(*this, "Purge unused samples?",
                                            MBBF_YES | MBBF_NO);
            DoModal(mb,PurgeCallback) ;
			break ;
		}
        case ACTION_SAVE: {
            MixerService::GetInstance()->SetRenderMode(0);
            PersistencyService *service = PersistencyService::GetInstance();
            // saving used to be completely silent -- a full or removed
            // Memory Stick looked exactly like success
            if (service->Save()) {
                View::SetNotification("song saved");
                // the recovery file is only interesting while there is
                // something unsaved to recover
                ViewEvent ve(VET_PROJECT_SAVED);
                SetChanged();
                NotifyObservers(&ve);
            } else {
                View::SetNotification("SAVE FAILED - check the memory stick");
            }
            break;
        }
        case ACTION_SAVE_AS: {
            PersistencyService *service = PersistencyService::GetInstance();
            service->Save("project:lgptsav_tmp.dat");
            NewProjectDialog *mb = new NewProjectDialog(*this, "root:");
            DoModal(mb, SaveAsProjectCallback);
            break;
        }
        case ACTION_LOAD: {
            MessageBox *mb = new MessageBox(
                *this, "Load song and lose changes ?", MBBF_YES | MBBF_NO);
            DoModal(mb, LoadCallback);
            break;
        }
        case ACTION_QUIT: {
            MessageBox *mb = new MessageBox(*this, "Quit and lose faith ?",
                                            MBBF_YES | MBBF_NO);
            DoModal(mb, QuitCallback);
            break;
        }
        case ACTION_TEMPO_CHANGED:
			break ;
		default:
			NInvalid ;
			break ;
	} ;
    focus->Draw(w_) ;
	isDirty_=true ;
} ;

void ProjectView::OnPurgeInstruments(bool removeFromDisk) {
	project_->PurgeInstruments(removeFromDisk) ;
} ;

void ProjectView::OnLoadProject() {
	ViewEvent ve(VET_QUIT_PROJECT) ;
	SetChanged();
	NotifyObservers(&ve) ;
} ;

void ProjectView::OnSaveAsProject(char * data) {
        ViewEvent ve(VET_SAVEAS_PROJECT,data) ;
	SetChanged();
	NotifyObservers(&ve) ;
} ;

void ProjectView::OnQuit() {
	ViewEvent ve(VET_QUIT_APP) ;
	SetChanged();
	NotifyObservers(&ve) ;
} ;
