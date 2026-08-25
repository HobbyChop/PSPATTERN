#include "View.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Utils/HelpLegend.h"
#include "System/Console/Trace.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "ModalView.h"

bool View::initPrivate_=false ;

int View::margin_=0 ;
int View::songRowCount_; //=21 sets screen height among other things
bool View::miniLayout_=false ;
int View::altRowNumber_ = 4;

View::View(GUIWindow &w,ViewData *viewData):
	w_(w),
	modalView_(0),
	modalViewCallback_(0),
	hasFocus_(false)
{
  if (!initPrivate_) 
  {
	   GUIRect rect=w.GetRect() ;
     miniLayout_=(rect.Width()<320);
	   View::margin_=0 ;
		songRowCount_ = miniLayout_ ? 16:22; // 22 is row display count among other things

		const char *altRowStr = Config::GetInstance()->GetValue("ALTROWNUMBER");
		if (altRowStr) {
			altRowNumber_ = atoi(altRowStr);
		}

     initPrivate_=true ;
  }
	mask_=0 ;
	viewMode_=VM_NORMAL ;
	locked_=false ;
	viewData_=viewData;
	NOTIFICATION_TIMEOUT = 1000;
	displayNotification_ = "";
    // Initialize VU meter inertia tracking
    for (int i = 0; i < 8; i++) {
        prevLeftVU_[i] = 0;
        prevRightVU_[i] = 0;
    }
} ;

GUIPoint View::GetAnchor() {
	int width=40 ;
	int height=30 ;
	return GUIPoint((width-SONG_CHANNEL_COUNT*3)/2+2,(height-View::songRowCount_)/2) ;
}

GUIPoint View::GetTitlePosition() {
#ifndef PLATFORM_CAANOO
	return GUIPoint(0,0) ;
#else
	return GUIPoint(0,1) ;
#endif
} ;

bool View::Lock() {
	if (locked_) return false ;
	locked_=true ;
	return true ;
} ;

void View::WaitForObject() {
	while (locked_) {} ;
}

void View::Unlock() {
	locked_=false ;
}

void View::drawNotes(int x0) {

    if (!miniLayout_) {

		GUIPoint anchor=GetAnchor() ;
		int initialX = (x0 >= 0) ? x0 : View::margin_+10 ;
		int initialY = anchor._y+22 ;
		GUIPoint pos(initialX,initialY) ;
		GUITextProperties props ;

        Player *player=Player::GetInstance() ;

		// live note readout under the channel columns: plain text, no
		// inverted blocks — silent when the player is stopped
		bool live = player->IsRunning() && viewData_->playMode_ != PM_AUDITION;
        for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
			if (live) {
				SetColor((i==viewData_->songX_) ? CD_HILITE2 : CD_NORMAL) ;
				DrawString(pos._x,pos._y,player->GetPlayedNote(i),props) ;
				SetColor(CD_ROW2) ;
				DrawString(pos._x,pos._y+1,player->GetPlayedOctive(i),props) ;
				DrawString(pos._x,pos._y+2,player->GetPlayedInstrument(i),props) ;
			} else {
				SetColor(CD_BACKGROUND) ;
				DrawString(pos._x,pos._y,"  ",props) ;
				DrawString(pos._x,pos._y+1,"  ",props) ;
				DrawString(pos._x,pos._y+2,"  ",props) ;
			}
			pos._x+= 3;
		}
		SetColor(CD_NORMAL) ;
     }
}

void View::DoModal(ModalView *view,ModalViewCallback cb) {
	modalView_=view ;
	modalView_->OnFocus() ;
	modalViewCallback_=cb ;
	isDirty_=true ;
} ;

void View::Redraw() {
	if (modalView_) {
		if (isDirty_) {
			DrawView() ;
		}
		modalView_->Redraw() ;
	} else {
		DrawView() ;
	}
	isDirty_=false ;
} ;

void View::SetDirty(bool isDirty) {
	isDirty_=true ;
} ;

void View::ProcessButton(unsigned short mask, bool pressed) {
	isDirty_=false ;
	if (modalView_) {
		modalView_->ProcessButton(mask,pressed);
		modalView_->isDirty_;
		if (modalView_->IsFinished()) {
			// process callback sending the modal dialog
			if (modalViewCallback_) {
				modalViewCallback_(*this,*modalView_) ;
			}
			SAFE_DELETE(modalView_) ;
			isDirty_=true ;
		}
	} else {
		ProcessButtonMask(mask,pressed);
	}
	if (isDirty_) ((AppWindow &)w_).SetDirty() ;
} ;

void View::Clear() {
	((AppWindow &)w_).Clear() ;
}

void View::SetColor(ColorDefinition cd) {
	((AppWindow &)w_).SetColor(cd) ;
} ;

void View::DrawTitleStrip(const char *name, const char *right) {
    AppWindow &app = (AppWindow &)w_;
    app.OpRect(0, 0, 0, 320, 11, AppWindow::OC_STRIP);
    app.OpRect(0, 0, 11, 320, 1, CD_HILITE1);
    GUITextProperties props;
    SetColor(CD_NORMAL);
    DrawString(1, 0, name, props);
    if (right) {
        int x = 39 - (int)strlen(right);
        SetColor(CD_HILITE2);
        DrawString(x, 0, right, props);
    }
    SetColor(CD_NORMAL);
}

void View::DrawHintBar(const char *hints) {
    AppWindow &app = (AppWindow &)w_;
    app.OpRect(0, 0, 232, 320, 8, AppWindow::OC_STRIP);
    // A live notification owns this row -- it is drawn by
    // EnableNotification, which some views call before this and some
    // after. Either way the hints stay out of its way rather than the
    // two of them overwriting each other.
    if (notificationLive()) return;
    GUITextProperties props;
    SetColor(CD_ROW2);
    DrawString(1, 29, hints, props);
    SetColor(CD_NORMAL);
}

void View::DrawPanel(int cx, int cy, int cw, int ch, const char *label) {
    AppWindow &app = (AppWindow &)w_;
    int x = cx * 8 - 3;
    int y = cy * 8 + 4;
    int w = cw * 8 + 6;
    // The bottom rule has to land on the blank row BELOW the content,
    // not on the content's own last pixel row -- at (ch+1)*8-5 it drew
    // a strikethrough across the panel's last line of text. App pixels
    // scale to the PSP's 9-tall cells as psp = app*9/8, so a bottom
    // edge at (cy+ch)*8+8 lands exactly on the first pixel row of the
    // next cell. That means every panel needs the row under it free,
    // which is why the layouts below leave one.
    int h = (ch + 1) * 8 - 3;
    int gapX = 0, gapW = 0;
    if (label && label[0]) {
        gapX = (cx + 1) * 8 - 3;
        gapW = (int)strlen(label) * 8 + 5;
    }
    app.OpFrame(x, y, w, h, CD_ROW, gapX, gapW);
    if (label && label[0]) {
        GUITextProperties props;
        SetColor(CD_HILITE1);
        DrawString(cx + 1, cy, label, props);
        SetColor(CD_NORMAL);
    }
}

void View::DrawCommandHelp(FourCC command) {

	// 4 rows of body: 3 lines of help plus the breathing room
	// the frame needs, or the last line sits on the border
	DrawPanel(1,COMMAND_HELP_ROW,38,4,"COMMAND") ;

	GUITextProperties props ;
	char blank[39] ;
	memset(blank,' ',36) ;
	blank[36]=0 ;
	for (int i=0;i<3;i++) {
		SetColor(CD_BACKGROUND) ;
		DrawString(2,COMMAND_HELP_ROW+1+i,blank,props) ;
	}

	if (command==I_CMD_NONE) {
		// an empty slot: say how to fill it rather than showing an
		// empty box
		SetColor(CD_ROW2) ;
		DrawString(2,COMMAND_HELP_ROW+1,"no command on this step",props) ;
		DrawString(2,COMMAND_HELP_ROW+2,"hold O + up/down to pick one",props) ;
		SetColor(CD_NORMAL) ;
		return ;
	}

	std::string *lines=getHelpLegend(command) ;
	for (int i=0;i<3;i++) {
		SetColor((i==0)?CD_HILITE2:CD_NORMAL) ;
		DrawString(2,COMMAND_HELP_ROW+1+i,lines[i].c_str(),props) ;
	}
	SetColor(CD_NORMAL) ;
} ;

void View::ClearRect(int x,int y,int w,int h) {
	GUIRect rect(x,y,(x+w),(y+h)) ;
	w_.ClearRect(rect) ;
} ;

void View::DrawString(int x,int y,const char *txt,GUITextProperties &props) {
	GUIPoint pos(x,y) ;
	w_.DrawString(txt,pos,props) ;
} ;

/*
	Displays the saved notification for 1 second
*/
bool View::notificationLive() {
	return (SDL_GetTicks() - notificationTime_) <= NOTIFICATION_TIMEOUT ;
}

void View::EnableNotification() {
	if (notificationLive()) {
		SetColor(CD_NORMAL);
		GUITextProperties props;
        int xOffset = 4;
        // support single-line or two-line notifications separated by '\n'
        size_t nl = displayNotification_.find('\n');
		if (nl == std::string::npos) {
			DrawString(xOffset, notiDistY_, displayNotification_.c_str(), props);
		} else {
			std::string line1 = displayNotification_.substr(0, nl);
			std::string line2 = displayNotification_.substr(nl + 1);
			DrawString(xOffset, notiDistY_, line1.c_str(), props);
			DrawString(xOffset, notiDistY_ + 1, line2.c_str(), props);
		}
    } else {
		displayNotification_ = "";
	}
}

/*
    Set displayed notification
    Saves the current time
    Optionally set display y offset if not in a project (default == 2)
    Allows negative offsets, use with care!
*/
void View::SetNotification(const char *notification, int offset) {
    notificationTime_ = SDL_GetTicks();
    displayNotification_ = notification;
    notiDistY_ = offset;
    isDirty_ = true;
}

