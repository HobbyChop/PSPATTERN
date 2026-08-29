#ifndef _CONFIG_VIEW_H_
#define _CONFIG_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "ViewData.h"

class Variable ;

// In-app editor for config.xml. Each row edits one option through a
// typed UI Variable whose GetString() IS the config value, so writing
// back is a plain SetString + Config::Save(). Changes are committed when
// you leave the screen: THEME re-applies live, the rest are baked in at
// boot and marked with a '*' (a reboot applies them).
class ConfigView: public FieldView {
public:
	ConfigView(GUIWindow &w,ViewData *data) ;
	virtual ~ConfigView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() {} ;
	// commit + save when the screen is left -- by chord, nav menu, or
	// anything else. The old design only committed inside this view's
	// own exit chords, which the nav menu retired.
	virtual void LooseFocus() ;

private:
	static const int MAX_SETTINGS=12 ;
	struct Setting {
		Variable *ui ;        // the typed field-backing variable we own
		const char *key ;     // the config.xml key it maps to
		bool reboot ;         // read once at boot -> needs a reboot
		bool theme ;          // THEME -> can be re-applied live
	} ;
	Setting settings_[MAX_SETTINGS] ;
	int count_ ;
	bool rebootPending_ ;
	int themeApplied_ ;   // last theme index applied live

	// build one setting row: a CHAR_LIST (enum) or an INT (number)
	void addList(int row,const char *label,const char *key,
	             const char *const *opts,int n,int defIdx,
	             bool reboot,bool theme) ;
	void addInt(int row,const char *label,const char *key,
	            int mn,int mx,int def,bool reboot) ;

	// write changed rows back to Config, apply the live ones, save
	void commit() ;
	void switchTo(ViewType vt) ;
} ;

#endif
