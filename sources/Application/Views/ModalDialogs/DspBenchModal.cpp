#include "DspBenchModal.h"
#include "Application/AppWindow.h"
#include "Application/Player/Player.h"
#include <stdio.h>
#include <string.h>

#define BENCH_W 34
// six voice rows, a blank, a send header, three send rows, then the
// two lines of legend -- plus the title, the budget line and the
// column header above them
#define BENCH_H 18

DspBenchModal::DspBenchModal(View &view) : ModalView(view) {
	memset(&result_,0,sizeof(result_)) ;
	started_=false ;
}

DspBenchModal::~DspBenchModal() {}

void DspBenchModal::OnFocus() {}

void DspBenchModal::AnimationUpdate() {}

void DspBenchModal::DrawView() {

	SetWindow(BENCH_W,BENCH_H) ;
	GUITextProperties props ;
	char line[64] ;

	SetColor(CD_HILITE1) ;
	DrawString(0,0,"CPU LOAD",props) ;
	SetColor(CD_NORMAL) ;

	if (!started_) {
		SetColor(CD_ROW2) ;
		DrawString(0,2,"what a voice of each engine,",props) ;
		DrawString(0,3,"and the send bus, cost HERE.",props) ;
		DrawString(0,5,"takes a couple of seconds and",props) ;
		DrawString(0,6,"the audio will stutter while",props) ;
		DrawString(0,7,"it runs.",props) ;
		SetColor(CD_HILITE2) ;
		DrawString(0,9,"O  measure       X  close",props) ;
		SetColor(CD_NORMAL) ;
		return ;
	}

	// header: the budget everything below is a percentage of
	if (result_.cpuMhz_>0) {
		sprintf(line,"%d frames @ %dHz   %dMHz",result_.blockFrames_,
		        result_.sampleRate_,result_.cpuMhz_) ;
	} else {
		sprintf(line,"%d frames @ %dHz",result_.blockFrames_,
		        result_.sampleRate_) ;
	}
	SetColor(CD_ROW2) ;
	DrawString(0,1,line,props) ;

	SetColor(CD_HILITE1) ;
	DrawString(0,3,"engine",props) ;
	for (int st=0;st<DSPB_STEPS;st++) {
		sprintf(line,"%2dv",DspBench::VoicesAt(st)) ;
		DrawString(10+st*5,3,line,props) ;
	}
	SetColor(CD_NORMAL) ;

	// The send rows get their own header because their step axis is
	// not the same axis: channels sending, not voices sounding.
	for (int e=0;e<DSPB_ENGINES;e++) {

		int y=4+e ;
		if (e>=DSPB_VOICE_ROWS) {
			y++ ;                       // the sub-header's line
			if (e==DSPB_VOICE_ROWS) {
				SetColor(CD_HILITE1) ;
				DrawString(0,y-1,"send bus",props) ;
				for (int st=0;st<DSPB_STEPS;st++) {
					sprintf(line,"%2ds",DspBench::VoicesAt(st)) ;
					DrawString(10+st*5,y-1,line,props) ;
				}
			}
		}

		SetColor(CD_ROW2) ;
		DrawString(0,y,DspBench::EngineName(e),props) ;
		for (int st=0;st<DSPB_STEPS;st++) {
			int p=result_.load_[e][st] ;      // permille of one block
			// over budget is the number that matters, so it gets the
			// colour that means "look at this"
			SetColor((p>=1000)?CD_HILITE2:((p>=750)?CD_MUTE:CD_NORMAL)) ;
			if (p>=1000) {
				sprintf(line,"%3d",p/10) ;
			} else {
				sprintf(line,"%2d%%",p/10) ;
			}
			DrawString(10+st*5,y,line,props) ;
		}
	}
	SetColor(CD_ROW2) ;
	DrawString(0,4+DSPB_ENGINES+2,"% of one block's realtime",props) ;
	DrawString(0,4+DSPB_ENGINES+3,"v voices sounding, s sending",props) ;
	SetColor(CD_NORMAL) ;
}

void DspBenchModal::ProcessButtonMask(unsigned short mask,bool pressed) {

	if (!pressed) return ;

	if (mask&EPBM_B) {
		EndModal(0) ;
		return ;
	}
	if (mask&EPBM_A) {
		if (!started_) {
			// Stop the transport first: the bench wants the machine to
			// itself, and a running song would both distort the number
			// and be distorted by it.
			Player *player=Player::GetInstance() ;
			if (player->IsRunning()) player->Stop() ;
			started_=true ;
			DspBench::Run(result_) ;
			isDirty_=true ;
		} else {
			EndModal(0) ;
		}
	}
}
