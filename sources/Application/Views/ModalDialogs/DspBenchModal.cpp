#include "DspBenchModal.h"
#include "Application/AppWindow.h"
#include "Application/Player/Player.h"
#include <stdio.h>
#include <string.h>
#ifdef PSP_DSP_PROFILE
#include "Application/Instruments/SynthInstrument.h"
#endif

#ifdef PSP_ME_OFFLOAD
extern "C" unsigned int PSPME_Ready(void) ;
extern "C" unsigned int PSPME_Heartbeat(void) ;
extern "C" unsigned int PSPME_Jobs(void) ;
extern "C" unsigned int PSPME_Calls(void) ;
#ifndef ME_FM_PROBE
#define ME_FM_PROBE 0
#endif
#if ME_FM_PROBE
extern "C" unsigned int PSPME_FmCycles(void) ;
#endif
#endif

#define BENCH_W 34
// six voice rows, a blank, a send header, three send rows, then the
// two lines of legend -- plus the title, the budget line and the
// column header above them
#ifdef PSP_ME_OFFLOAD
#define BENCH_H 24
#else
#define BENCH_H 20
#endif

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
#ifdef PSP_DSP_PROFILE
		// Tier-0 live renderFm profile (accumulated over whatever has
		// played since boot). flt/unf = avg us per FM block with/without
		// the two-pass SVF; their difference is the filter-stage cost, and
		// "filtered %%" says whether the single-pass filter opt is worth it.
		{
			unsigned long long uf,uu ; unsigned int bf,bu ;
			SynthInstrument::GetFmProfile(uf,uu,bf,bu) ;
			unsigned int tot=bf+bu ;
			SetColor(CD_ROW2) ;
			sprintf(line,"FM us/blk flt%u unf%u",
			        bf?(unsigned int)(uf/bf):0,bu?(unsigned int)(uu/bu):0) ;
			DrawString(0,11,line,props) ;
			sprintf(line,"blks%u  filtered %u%%",tot,tot?(bf*100u/tot):0) ;
			DrawString(0,12,line,props) ;
			SetColor(CD_NORMAL) ;
		}
#endif
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
#ifdef PSP_ME_OFFLOAD
	// Second-core state: rdy = survived init, hb climbs = loop alive,
	// job climbs = reverb blocks actually processed there.
	SetColor(CD_HILITE2) ;
	sprintf(line,"ME rdy%u hb%u",PSPME_Ready(),PSPME_Heartbeat()) ;
	DrawString(0,4+DSPB_ENGINES+5,line,props) ;
	sprintf(line,"call%u job%u",PSPME_Calls(),PSPME_Jobs()) ;
	DrawString(0,4+DSPB_ENGINES+6,line,props) ;
#if ME_FM_PROBE
	{ unsigned int cyc=PSPME_FmCycles(); unsigned int us=cyc/333u;
	  sprintf(line,"FM8v %uus /5805 %s",us,(us&&us<5805)?"FITS":"OVER") ;
	  DrawString(0,4+DSPB_ENGINES+7,line,props) ; }
#endif
	SetColor(CD_NORMAL) ;
#endif
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
