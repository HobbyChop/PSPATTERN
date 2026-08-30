#include "ConfigView.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "Application/Version.h"
#include "BaseClasses/UIPillField.h"
#include "BaseClasses/UIStepperField.h"
#include "Foundation/Variables/Variable.h"
#include "System/Console/Trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Option lists. Each string IS the literal config.xml value, so a row's
// GetString() can be written straight back with no mapping.
static const char *THEME_OPTS[] = {"SUNSET","ICEBOX","EMBER","GRAPHITE"} ;
static const char *FONT_OPTS[]  = {"CUSTOM","0","1","2"} ;
static const char *BUF_OPTS[]   = {"64","128","256","512","1024"} ;
static const char *YES_NO[]     = {"YES","NO"} ;   // default-on toggles
static const char *NO_YES[]     = {"NO","YES"} ;   // default-off toggles

static int optIndex(const char *val,const char *const *opts,int n,int def) {
	if (val)
		for (int i=0;i<n;i++)
			if (!strcasecmp(val,opts[i])) return i ;
	return def ;
}

ConfigView::ConfigView(GUIWindow &w,ViewData *data):FieldView(w,data) {
	count_=0 ;
	rebootPending_=false ;

	// DISPLAY
	addList(6, "theme  ", "THEME",        THEME_OPTS,4, 0, false, true) ;
	addList(7, "font  *", "FONTTYPE",     FONT_OPTS, 4, 0, true,  false) ;
	addInt (8, "arows *", "ALTROWNUMBER", 1,16, 4,        true) ;

	// AUDIO
	addList(11,"buffer*", "AUDIOBUFFERSIZE", BUF_OPTS,5, 1, true, false) ;
	addInt (12,"prebuf*", "AUDIOPREBUFFER",  2,12, 5,         true) ;
	addList(13,"me fx *", "ME_OFFLOAD",       YES_NO,2, 0, true, false) ;

	// ENGINES
	addList(16,"fm     ", "FM_ENGINE",        YES_NO,2, 0, false, false) ;

	// SYNC
	// trim for MIDI clock follow, milliseconds, positive = play
	// earlier. The audio pipeline is measured and compensated; this
	// covers what cannot be measured from inside (the leader's own
	// output latency, the adapter hop). Applies at the next play start.
	addInt (18,"synco  ", "MIDISYNCOFFSET", -50,100, 50, false) ;

	// BEHAVIOUR
	// autosave: YES by default; NO for a live set, where a Memory
	// Stick write (frozen input, chewed tails) must never happen.
	// Applies immediately -- the autosave tick reads it every pass.
	addList(17,"asave  ", "AUTOSAVE",         YES_NO,2, 0, false, false) ;

	themeApplied_=settings_[0].ui->GetInt() ;   // the theme row is first
}

ConfigView::~ConfigView() {
	// the fields reference these but never touch them during teardown
	for (int i=0;i<count_;i++)
		delete settings_[i].ui ;
}

void ConfigView::addList(int row,const char *label,const char *key,
                         const char *const *opts,int n,int defIdx,
                         bool reboot,bool theme) {
	if (count_>=MAX_SETTINGS) return ;
	Config *cfg=Config::GetInstance() ;
	// seed a key the shipped/older config.xml never had, so it can be
	// edited now and appended on save
	if (!cfg->FindVariable(key))
		cfg->Insert(new Variable(key,(FourCC)0,opts[defIdx])) ;
	int idx=optIndex(cfg->GetValue(key),opts,n,defIdx) ;
	Variable *v=new Variable(label,(FourCC)0,opts,n,idx) ;
	GUIPoint pos(2,row) ;
	if (n<=2) {
		UIPillField *f=new UIPillField(pos,*v,label,n) ;
		T_SimpleList<UIField>::Insert(f) ;
	} else {
		UIStepperField *f=new UIStepperField(pos,*v,label,"%s",0,n-1) ;
		T_SimpleList<UIField>::Insert(f) ;
	}
	settings_[count_].ui=v ; settings_[count_].key=key ;
	settings_[count_].reboot=reboot ; settings_[count_].theme=theme ;
	count_++ ;
}

void ConfigView::addInt(int row,const char *label,const char *key,
                        int mn,int mx,int def,bool reboot) {
	if (count_>=MAX_SETTINGS) return ;
	Config *cfg=Config::GetInstance() ;
	if (!cfg->FindVariable(key)) {
		char b[16] ; sprintf(b,"%d",def) ;
		cfg->Insert(new Variable(key,(FourCC)0,b)) ;
	}
	const char *cur=cfg->GetValue(key) ;
	int val=cur?atoi(cur):def ;
	if (val<mn) val=mn ;
	if (val>mx) val=mx ;
	Variable *v=new Variable(label,(FourCC)0,val,mx) ;
	GUIPoint pos(2,row) ;
	UIStepperField *f=new UIStepperField(pos,*v,label,"%d",mn,mx) ;
	T_SimpleList<UIField>::Insert(f) ;
	settings_[count_].ui=v ; settings_[count_].key=key ;
	settings_[count_].reboot=reboot ; settings_[count_].theme=false ;
	count_++ ;
}

void ConfigView::commit() {
	Config *cfg=Config::GetInstance() ;
	bool changed=false,rebootNeeded=false,themeChanged=false ;
	for (int i=0;i<count_;i++) {
		Variable *cv=cfg->FindVariable(settings_[i].key) ;
		if (!cv) continue ;
		const char *nv=settings_[i].ui->GetString() ;
		if (strcmp(cv->GetString(),nv)!=0) {
			cv->SetString(nv) ;
			changed=true ;
			if (settings_[i].reboot) rebootNeeded=true ;
			if (settings_[i].theme)  themeChanged=true ;
		}
	}
	if (!changed) return ;
	if (themeChanged)
		((AppWindow &)w_).ApplyTheme() ;      // live palette + repaint
	// values are already live in Config's memory; the STICK write is
	// deferred to the pre-redraw hook, outside the input-path mixer
	// lock this method runs under (20-150ms of render starvation
	// otherwise, audible if the song was playing)
	s_diskPending_=true ;
	if (rebootNeeded) {
		rebootPending_=true ;
		View::SetNotification("saved - reboot to apply") ;
	} else {
		View::SetNotification("settings saved") ;
	}
}

bool ConfigView::s_diskPending_=false ;

void ConfigView::CommitPendingToDisk() {
	if (!s_diskPending_) return ;
	s_diskPending_=false ;
	if (!Config::GetInstance()->Save()) {
		Trace::Error("config save failed - check the stick") ;
	}
}

void ConfigView::LooseFocus() {
	commit() ;
	View::LooseFocus() ;
}

void ConfigView::switchTo(ViewType vt) {
	commit() ;                                 // persist before we leave
	ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
	SetChanged() ;
	NotifyObservers(&ve) ;
}

void ConfigView::ProcessButtonMask(unsigned short mask,bool pressed) {
	if (!pressed)
		return ;

	FieldView::ProcessButtonMask(mask) ;

	// live theme preview: stepping the theme repaints the palette
	// immediately -- no save yet, that happens on leave
	for (int i=0;i<count_;i++) {
		if (settings_[i].theme) {
			int v=settings_[i].ui->GetInt() ;
			if (v!=themeApplied_) {
				themeApplied_=v ;
				Config *cfg=Config::GetInstance() ;
				Variable *cv=cfg->FindVariable(settings_[i].key) ;
				if (cv) cv->SetString(settings_[i].ui->GetString()) ;
				((AppWindow &)w_).ApplyTheme() ;
			}
			break ;
		}
	}

	if (mask & EPBM_R) {
		if (mask&EPBM_LEFT)  switchTo(VT_PROJECT) ;
		if (mask&EPBM_RIGHT) switchTo(VT_GROOVE) ;
		if (mask&EPBM_DOWN)  switchTo(VT_CHAIN) ;
	}
}

void ConfigView::DrawView() {
	Clear() ;
	View::EnableNotification() ;

	DrawTitleStrip("SETTINGS",PSPATTERN_VERSION_STRING) ;

	DrawPanel(1,5,20,4,"DISPLAY") ;    // theme, font, alt-rows
	DrawPanel(1,10,20,4,"AUDIO") ;     // buffer, prebuffer, me offload
	DrawPanel(1,15,20,3,"ENGINES") ;   // fm

	FieldView::Redraw() ;

	GUITextProperties props ;
	SetColor(CD_ROW2) ;
	DrawString(2,20,"* takes effect after a reboot",props) ;
	if (rebootPending_) {
		SetColor(CD_HILITE2) ;
		DrawString(2,21,"reboot pending",props) ;
	}
	SetColor(CD_NORMAL) ;

	DrawHintBar("O change  hold R map") ;
}
