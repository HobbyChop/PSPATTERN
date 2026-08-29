
#include "Observable.h"

#include "T_SimpleList.h"

Observable::Observable() {
	_hasChanged=false ;
}

Observable::~Observable() {
}

void Observable::AddObserver(I_Observer &o) {
	_list.push_back(&o) ;
}

void Observable::RemoveObserver(I_Observer &o) {
	std::vector<I_Observer *>::iterator it=_list.begin() ;
	while (it!=_list.end()) {
		if (*it==&o) {
			_list.erase(it) ;
			break ;
		}
		it++ ;
	}
}

void Observable::RemoveAllObservers() {
	std::vector<I_Observer *>::iterator it=_list.begin() ;
	while (it!=_list.end()) {
		it=_list.erase(it) ;
	}
}

void Observable::NotifyObservers(I_ObservableData *d) {
	if (_hasChanged) {
		// Walk a SNAPSHOT: an observer's Update may add or remove
		// observers on this very object (a view re-observing after a
		// change), and erase/push_back invalidate a live iterator --
		// the next step then reads freed memory and calls through a
		// garbage vtable. The copy makes re-entry safe; an observer
		// removed mid-notify may still get this one last Update.
		std::vector<I_Observer *> snapshot=_list ;
		std::vector<I_Observer *>::iterator it=snapshot.begin() ;
		while (it!=snapshot.end()) {
			I_Observer *o=*it++ ;
			o->Update(*this,d) ;
		}
		ClearChanged() ;
	}







}

void Observable::SetChanged() {
	_hasChanged=true ;
}

bool Observable::HasChanged() {
	return _hasChanged ;
}
