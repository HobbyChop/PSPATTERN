#include "FieldView.h"
#include "System/Console/Trace.h"

FieldView::FieldView(GUIWindow &w,ViewData *data):View(w,data),T_SimpleList<UIField>(true) {
	focus_=0 ;	
} ;

void FieldView::SetFocus(UIField *field) {

	if (focus_) {
		focus_->ClearFocus() ;
	}
	focus_=field ;

//  Empty field view, we don't have anything to do

	if (focus_==0) return ;

	focus_->SetFocus() ;

} ;

void FieldView::ClearFocus() {
	if (focus_) {
		focus_->ClearFocus() ;
	} ;
	focus_=0 ;
} ;

UIField *FieldView::GetFocus() {
    return focus_ ;
} ;

void FieldView::Redraw() {

	if (focus_==0) {
		SetFocus(T_SimpleList<UIField>::GetFirst()) ;
	}

	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;

	for (it->Begin();!it->IsDone();it->Next()) {
		UIField &current=it->CurrentItem() ;
		current.Draw(w_) ;
	} ;
};

/* How far apart two fields can sit horizontally and still count as the
   same column. Up and down used to pick purely on y, ignoring x
   entirely, which walks diagonally across a two-column screen: on the
   project page, pressing down on "load song" landed on "transp" in the
   other column, and the file actions under it could not be reached at
   all. Left and right already switch columns by matching rows. */
#define FIELD_SAME_COLUMN 8

void FieldView::ProcessButtonMask(unsigned short mask) {

	if (focus_==0) {
		focus_=T_SimpleList<UIField>::GetFirst() ;
	//  Empty field view, we don't have anything to do
		if (focus_==0) return ;
 		focus_->SetFocus() ;
	}

	
	if (mask&EPBM_A) {  // A or A+ARROW is sent to the field
		if (mask&EPBM_DOWN) {
			focus_->ProcessArrow(EPBM_DOWN) ;
			isDirty_=true ;
		}
		if (mask&EPBM_UP){
			focus_->ProcessArrow(EPBM_UP)  ;
			isDirty_=true ;
		}

		if (mask&EPBM_LEFT) {
			focus_->ProcessArrow(EPBM_LEFT) ;
			isDirty_=true ;
		}

		if (mask&EPBM_RIGHT){
			focus_->ProcessArrow(EPBM_RIGHT)  ;
			isDirty_=true ;
		}

		if (mask==EPBM_A) {
			focus_->OnClick() ;
		};

	} else {
		if (mask&EPBM_B) {  // B or B+ARROW is sent to the field

			if (mask==EPBM_B) {
				focus_->OnBClick() ;
				isDirty_=true ;
			};

			if (mask&EPBM_DOWN) {
				focus_->ProcessBArrow(EPBM_DOWN) ;
				isDirty_=true ;
			}
			if (mask&EPBM_UP){
				focus_->ProcessBArrow(EPBM_UP)  ;
				isDirty_=true ;
			}

			if (mask&EPBM_LEFT) {
				focus_->ProcessBArrow(EPBM_LEFT) ;
				isDirty_=true ;
			}

			if (mask&EPBM_RIGHT){
				focus_->ProcessBArrow(EPBM_RIGHT)  ;
				isDirty_=true ;
			}

		} else { // Nor B or A is pressed

			if (!(mask&(EPBM_A|EPBM_B|EPBM_L|EPBM_R|EPBM_SELECT|EPBM_START))) {

				if (mask&EPBM_DOWN) {
					// Same column first, then wrap within it, and only
					// cross columns when this one has nothing.
					UIField *next=0 ;      // below, same column
					UIField *first=0 ;     // topmost, same column (wrap)
					UIField *nextAny=0 ;   // below, any column
					UIField *firstAny=0 ;  // topmost anywhere (wrap)

					int fx=focus_->GetPosition()._x ;
					int fy=focus_->GetPosition()._y ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (current.IsStatic()) continue ;
						int cx=current.GetPosition()._x ;
						int cy=current.GetPosition()._y ;
						int dx=(cx>fx)?(cx-fx):(fx-cx) ;
						bool sameCol=(dx<=FIELD_SAME_COLUMN) ;

						if ((!firstAny)||(cy<firstAny->GetPosition()._y)) firstAny=&current ;
						if (sameCol&&((!first)||(cy<first->GetPosition()._y))) first=&current ;
						if (cy>fy) {
							if ((!nextAny)||(cy<nextAny->GetPosition()._y)) nextAny=&current ;
							if (sameCol&&((!next)||(cy<next->GetPosition()._y))) next=&current ;
						}
					}

					UIField *target=next?next:(first?first:(nextAny?nextAny:firstAny)) ;
					if (target) {
						focus_->ClearFocus() ;
						focus_=target ;
						focus_->SetFocus() ;
						isDirty_=true ;
					}
				}


				if (mask&EPBM_UP){

					UIField *prev=0 ;      // above, same column
					UIField *last=0 ;      // bottom-most, same column (wrap)
					UIField *prevAny=0 ;
					UIField *lastAny=0 ;

					int fx=focus_->GetPosition()._x ;
					int fy=focus_->GetPosition()._y ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (current.IsStatic()) continue ;
						int cx=current.GetPosition()._x ;
						int cy=current.GetPosition()._y ;
						int dx=(cx>fx)?(cx-fx):(fx-cx) ;
						bool sameCol=(dx<=FIELD_SAME_COLUMN) ;

						if ((!lastAny)||(cy>lastAny->GetPosition()._y)) lastAny=&current ;
						if (sameCol&&((!last)||(cy>last->GetPosition()._y))) last=&current ;
						if (cy<fy) {
							if ((!prevAny)||(cy>prevAny->GetPosition()._y)) prevAny=&current ;
							if (sameCol&&((!prev)||(cy>prev->GetPosition()._y))) prev=&current ;
						}
					}

					UIField *target=prev?prev:(last?last:(prevAny?prevAny:lastAny)) ;
					if (target) {
						focus_->ClearFocus() ;
						focus_=target ;
						focus_->SetFocus() ;
						isDirty_=true ;
					}
				}

				if (mask&EPBM_RIGHT) {
					// Nearest field to the right, by row distance. This
					// used to demand an exact row match, so on the
					// project screen only the two rows that happened to
					// line up with a FILE action could cross columns at
					// all -- from "master" or "transp" right did nothing.
					UIField *next=0 ;
					int bestDy=0 ;
					int fx=focus_->GetPosition()._x ;
					int fy=focus_->GetPosition()._y ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (current.IsStatic()) continue ;
						int cx=current.GetPosition()._x ;
						int cy=current.GetPosition()._y ;
						if (cx<=fx) continue ;
						int dy=(cy>fy)?(cy-fy):(fy-cy) ;
						if ((!next)||(dy<bestDy)||
						    ((dy==bestDy)&&(cx<next->GetPosition()._x))) {
							next=&current ;
							bestDy=dy ;
						}
					}

					if (next) {
						focus_->ClearFocus() ;
						focus_=next ;
						focus_->SetFocus() ;
						isDirty_=true ;
					}
				}

				if (mask&EPBM_LEFT){

					UIField *prev=0 ;
					int bestDy=0 ;
					int fx=focus_->GetPosition()._x ;
					int fy=focus_->GetPosition()._y ;

					IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						UIField &current=it->CurrentItem() ;
						if (current.IsStatic()) continue ;
						int cx=current.GetPosition()._x ;
						int cy=current.GetPosition()._y ;
						if (cx>=fx) continue ;
						int dy=(cy>fy)?(cy-fy):(fy-cy) ;
						if ((!prev)||(dy<bestDy)||
						    ((dy==bestDy)&&(cx>prev->GetPosition()._x))) {
							prev=&current ;
							bestDy=dy ;
						}
					}

					if (prev) {
						focus_->ClearFocus() ;
						focus_=prev ;
						focus_->SetFocus() ;
						isDirty_=true ;
					}
				}
			}
		}
	}
}

int FieldView::GetFocusIndex() {

	int focusIndex=0 ;
	IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		if (&(it->CurrentItem())==focus_) {
			break ;
		} ;
		focusIndex++ ;
	} ;
	return focusIndex ;
}
