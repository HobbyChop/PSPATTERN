#include "MixerView.h"
#include "Application/AppWindow.h"
#include "ModalDialogs/DspBenchModal.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Services/Audio/AudioStats.h"
#include "Services/Audio/SendFx.h"
#include "UIController.h"
#include "VuMeterUtil.h"
#include <iostream>
#include <sstream>
#include <string>
#include "Application/Instruments/I_Instrument.h"
#include "Services/Audio/MasterEq.h"
#include "Application/Model/Project.h"
#include <math.h>

#define MIXER_STRIP_COL(i) (3 + (i) * 4)
#define MIXER_VAL_ROW 15
#define MIXER_MUTE_ROW 21
// The row cursor walks the six per-channel rows and then the four
// settings of the effects themselves. The globals ignore the column
// -- there is one delay and one reverb, which is the whole point of
// a send -- so they are drawn on their own line under the strips.
#define MIXER_CHAN_ROWS 6
#define MIXER_ROW_COUNT 10
#define MIXER_FX_ROW 23
/* Below the send line, the master EQ: ten bands on one line, walked
   left and right exactly as the sends are. It belongs to no channel,
   so like the sends it ignores the column. */
#define MIXER_EQ_BASE MIXER_ROW_COUNT
#define MIXER_ROW_TOTAL (MIXER_ROW_COUNT + MASTER_EQ_BANDS)
#define MIXER_EQ_ROW 25
// Where a channel counts as "a large share of the mix", as a fraction
// of full scale. -6dBFS lit six channels out of eight on the demo,
// which is TRUE -- six loud channels is exactly why that mix sums to
// twice full scale before the fader -- but a light that is on
// everywhere points at nothing. This picks out the two or three
// carrying the weight, which is what you actually want to reach for.
#define MIXER_HOT_LEVEL 0.65f

MixerView::MixerView(GUIWindow &w,ViewData *viewData):View(w,viewData) {
	clipboard_.active_=false ;
	clipboard_.data_=0 ;
	invertBatt_=false;
    soloChannel_ = -1;
    mixerRow_ = 1; // start on volume row
    previousViewType_ = VT_SONG; // Default to SongView
    for (int i = 0; i < 9; i++) {
        vuBarHeightsL_[i] = 0;
        vuBarHeightsR_[i] = 0;
        clipHold_[i] = 0;
        hotHold_[i] = 0;
    }
}

MixerView::~MixerView() {}

void MixerView::onStart() {
	Player *player=Player::GetInstance() ;
	unsigned char from=viewData_->songX_ ;
	unsigned char to=from ;
	//if (clipboard_.active_) {
	//	GUIRect r=getSelectionRect();
	//	from=r.Left() ;
	//	to=r.Right() ;
	//}
	player->OnStartButton(PM_SONG,from,false,to) ;
} ;

void MixerView::onStop() {
	Player *player=Player::GetInstance() ;
	unsigned char from=viewData_->songX_ ;
	unsigned char to=from ;
	player->OnStartButton(PM_SONG,from,true,to) ;
}

// Stay on the same column as SongView
void MixerView::OnFocus() { viewData_->mixerCol_ = viewData_->songX_; }

void MixerView::updateCursor(int dx,int dy) {

    // The four send settings -- delay division, feedback, reverb size,
    // damping -- are four separate entries in the cursor model because
    // that is how the channel grid above is indexed. On screen they sit
    // side by side on one line. So moving between them was up and down,
    // against everything the eye was telling you.
    //
    // Below they behave as what they look like: one row, walked with
    // left and right.
    bool onSends = (mixerRow_ >= MIXER_CHAN_ROWS &&
                    mixerRow_ < MIXER_EQ_BASE);
    bool onEq = (mixerRow_ >= MIXER_EQ_BASE);

    if (dy != 0) {
        if (viewData_->mixerCol_ == 8) {
            // the master strip is just its fader -- stay on it
            mixerRow_ = 1; isDirty_ = true; return;
        }
        if (onEq) {
            // the EQ is the bottom line: up returns to the sends, down
            // wraps round to the top of the channel grid
            mixerRow_ = (dy < 0) ? MIXER_CHAN_ROWS : 0;
        } else if (onSends) {
            // vertically the whole send line is a single row: up goes
            // back to the channel grid, down carries on to the EQ
            mixerRow_ = (dy < 0) ? (MIXER_CHAN_ROWS - 1) : MIXER_EQ_BASE;
        } else if (dy < 0) {
            // Row 0 is the bus, which is drawn, saved, and read by
            // PlayerMixer to route the channel -- it just had no way to
            // reach it, so every channel stayed on whatever bus the
            // file said forever.
            mixerRow_ = (mixerRow_ == 0) ? MIXER_CHAN_ROWS : mixerRow_ - 1;
        } else {
            mixerRow_ = (mixerRow_ == MIXER_CHAN_ROWS - 1)
                            ? MIXER_CHAN_ROWS       // into the sends, at dly
                            : mixerRow_ + 1;
        }
		isDirty_ = true;
        return;
    }

    if (dx != 0) {
        if (onEq) {
            int b = (mixerRow_ - MIXER_EQ_BASE) + dx;
            if (b < 0) b = 0;
            if (b > MASTER_EQ_BANDS - 1) b = MASTER_EQ_BANDS - 1;
            mixerRow_ = MIXER_EQ_BASE + b;
            isDirty_ = true;
            return;
        }
        if (onSends) {
            int f = (mixerRow_ - MIXER_CHAN_ROWS) + dx;
            if (f < 0) f = 0;
            if (f > (MIXER_ROW_COUNT - MIXER_CHAN_ROWS - 1))
                f = MIXER_ROW_COUNT - MIXER_CHAN_ROWS - 1;
            mixerRow_ = MIXER_CHAN_ROWS + f;
            isDirty_ = true;
            return;
        }
        // LEFT/RIGHT switches between channels (0-7 only, Master is
        // display-only)
        int x = viewData_->mixerCol_;
		x += dx;
		if (x < 0) x = 0;
		if (x > 8) x = 8;                 // 8 = master fader (was display-only)
		viewData_->mixerCol_ = x;
		if (x == 8) mixerRow_ = 1;        // land on the master fader
		isDirty_ = true;
	}
}

void MixerView::toggleMute() {
    int col = viewData_->mixerCol_;
    if (col > 7) return;                 // master strip: nothing to mute
    UIController::GetInstance()->ToggleMute(col, col);
    isDirty_ = true;
};

void MixerView::toggleSolo() {
	int col=viewData_->mixerCol_ ;
	if (col > 7) return ;                // master strip: nothing to solo
	bool entering=(soloChannel_!=col) ;
	UIController::GetInstance()->SwitchSoloMode(col,col,entering) ;
    soloChannel_ = entering ? col : -1;
    isDirty_ = true;
}

void MixerView::unMuteAll() {
    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
    isDirty_ = true;
}

void MixerView::ProcessButtonMask(unsigned short mask, bool pressed) {

    if (clipboard_.active_) {
		viewMode_=VM_SELECTION ;
	} ;
	// Process selection related keys

    if (viewMode_ == VM_SELECTION) {
        if (clipboard_.active_==false) {
            clipboard_.active_=true ;
            clipboard_.x_ = viewData_->songX_;
            clipboard_.y_=viewData_->songY_ ;
            clipboard_.offset_=viewData_->songOffset_ ;
			saveX_=clipboard_.x_ ;
			saveY_=clipboard_.y_ ;
			saveOffset_=clipboard_.offset_ ;
        }
        processSelectionButtonMask(mask) ;
    } else {

        // Switch back to normal mode

        viewMode_=VM_NORMAL ;
        processNormalButtonMask(mask);
    }
};

/******************************************************
 processNormalButtonMask:
        process button mask in the case there is no
        selection active
 ******************************************************/

void MixerView::processNormalButtonMask(unsigned int mask) {

    // L modifier: mute, solo and unmute-all.
    //
    // These were on R, which is also the key that leaves this screen:
    // R with a direction goes to the chain or the table, R and down
    // opens the load dialog, R and start stops the transport. Holding
    // it to mute means holding the key that navigates away, and one
    // stray direction takes you off the mixer or puts a modal over it.
    //
    // L does nothing else here. It is the selection and clone modifier
    // on the grid screens, but the mixer's selection handler has empty
    // branches for it, so the key was free on this screen and only on
    // this screen.
    if (mask & EPBM_L) {
        if (mask & EPBM_B) {
            toggleMute();
        } else if (mask & EPBM_A) {
            toggleSolo();
        } else if (mask & EPBM_UP) {
            unMuteAll();
        } else if (mask & EPBM_DOWN) {
            // The mixer is where the dsp figure lives, so this is
            // where the thing that explains it lives too.
            DoModal(new DspBenchModal(*this)) ;
        }

    // R Modifier: leaving the screen, and the transport
    } else if (mask & EPBM_R) {
        if (mask & EPBM_UP) {
            // R + UP = the screen ABOVE this one on the map, which is
            // the chain. It used to return to whatever view you came
            // from, so the map said mixer sat under chain and the
            // only way to get there was the long way round through
            // the phrase. The map is the promise; this keeps it.
            ViewType vt = VT_CHAIN;
            viewData_->songX_ = viewData_->mixerCol_;
            unsigned char *data = viewData_->GetCurrentSongPointer();
            if (*data != 0xFF) {
                viewData_->currentChain_ = *data;
            }
            ViewEvent ve(VET_SWITCH_VIEW, &vt);
            SetChanged();
            NotifyObservers(&ve);
        } else if (mask & EPBM_LEFT) {
            // R + LEFT = the FX rack, the cell left of the mixer on the map
            ViewType vt = VT_FX;
            ViewEvent ve(VET_SWITCH_VIEW, &vt);
            SetChanged();
            NotifyObservers(&ve);
        } else if (mask & EPBM_RIGHT) {
            ViewType vt = VT_TABLE;
            ViewEvent ve(VET_SWITCH_VIEW, &vt);
            // Go back to the same column as in MixerView
            viewData_->songX_ = viewData_->mixerCol_;
            unsigned char *data = viewData_->GetCurrentSongPointer();
            if (*data != 0xFF) { // Set chain
                viewData_->currentChain_ = *data;
            }
            data = viewData_->GetCurrentChainPointer();
            if (*data != 0xFF) { // Set phrase
                viewData_->currentPhrase_ = *data;
            }
            SetChanged();
            NotifyObservers(&ve);
        } else if (mask & EPBM_START) {
            onStop();
        }
    } else if (mask & EPBM_B) {
        if (mask & EPBM_A) {
            /* X + O resets whatever the cursor is on.

               It only ever reset the channel volume before, wherever
               you were standing -- so on the EQ line it quietly
               changed a fader on some channel instead. */
            if (mixerRow_ >= MIXER_EQ_BASE) {
                int b = mixerRow_ - MIXER_EQ_BASE;
                viewData_->project_->SetEqBand(b, MASTER_EQ_FLAT);
                isDirty_ = true;
                char notifBuf[48];
                sprintf(notifBuf, "        EQ %4s: flat",
                        MasterEq::BandName(b));
                SetNotification(notifBuf);
            } else if (mixerRow_ < MIXER_CHAN_ROWS) {
                if (viewData_->mixerCol_ == 8) {
                    // reset the master fader to its default
                    viewData_->project_->FindVariable(VAR_MASTERVOL)->SetInt(75);
                } else {
                    // reset volume to full
                    Mixer::GetInstance()->SetChannelVolume(viewData_->mixerCol_,
                                                           0xFF);
                }
                isDirty_ = true;
            }
        }
    } else if (mask & EPBM_A) {
        if (viewData_->mixerCol_ == 8) {
            // master strip: A+dir moves the master fader (10..100)
            Variable *mv = viewData_->project_->FindVariable(VAR_MASTERVOL);
            int v = mv->GetInt();
            if (mask & EPBM_UP)    v += 4;
            if (mask & EPBM_DOWN)  v -= 4;
            if (mask & EPBM_RIGHT) v += 1;
            if (mask & EPBM_LEFT)  v -= 1;
            if (v < 10)  v = 10;
            if (v > 100) v = 100;
            mv->SetInt(v);
            isDirty_ = true;
            return;
        }
        if (mixerRow_ == 3) {
            // On LPF row: A+Up/Down = coarse ×2/÷2, A+Left/Right = fine
            // ±10Hz A alone (no direction) = toggle off/1000Hz
            Mixer *m = Mixer::GetInstance();
            int col = viewData_->mixerCol_;
            unsigned short freq = m->GetChannelLPF(col);
            unsigned short newFreq = freq;
            if (mask & EPBM_UP) {
                if (freq == 0)
                    newFreq = 20;
                else {
                    unsigned short step = freq / 10 < 1 ? 1 : freq / 10;
                    newFreq =
                        (unsigned short)(freq + step > 20000 ? 0
                                                                : freq + step);
                }
            } else if (mask & EPBM_DOWN) {
                if (freq == 0)
                    newFreq = 20000;
                else {
                    unsigned short step = freq / 10 < 1 ? 1 : freq / 10;
                    newFreq = (freq <= step)
                                    ? 0
                                    : (unsigned short)(freq - step < 20
                                                            ? 0
                                                            : freq - step);
                }
            } else if (mask & EPBM_RIGHT) {
                if (freq == 0)
                    newFreq = 20;
                else
                    newFreq =
                        (unsigned short)(freq + 10 > 20000 ? 0 : freq + 10);
            } else if (mask & EPBM_LEFT) {
                if (freq == 0)
                    newFreq = 20000;
                else
                    newFreq =
                        (freq <= 10)
                            ? 0
                            : (unsigned short)(freq - 10 < 20 ? 0
                                                                : freq - 10);
            }
            if (newFreq != freq) {
                m->SetChannelLPF(col, newFreq);
                isDirty_ = true;
            }
            // show notification
            if (newFreq == 0) {
                SetNotification("       Low Pass Filter: OFF");
            } else {
                char notifBuf[40];
                sprintf(notifBuf, "       Low Pass Filter: %dHz",
                        (int)newFreq);
                SetNotification(notifBuf);
            }
        } else if (mixerRow_ == 2) {
            // On HPF row: A+Right cycles forward, A+Left cycles backward
            if (mask & (EPBM_RIGHT | EPBM_LEFT)) {
                Mixer *m = Mixer::GetInstance();
                int col = viewData_->mixerCol_;
                int mode = m->GetChannelHPF(col);
                if (mask & EPBM_RIGHT) {
                    mode = (mode + 1) % 3;
                } else {
                    mode = (mode + 2) % 3;
                }
                m->SetChannelHPF(col, mode);
                isDirty_ = true;
                const char *modeStr = (mode == 0)   ? "OFF"
                                        : (mode == 1) ? "20Hz"
                                                    : "90Hz";
                std::string notif =
                    std::string("      High Pass Filter: ") + modeStr;
                SetNotification(notif.c_str());
            }
        } else if (mixerRow_ == 0) {
            // On bus row: A+Left/Right walks the bus this channel
            // feeds. Bus 8 is the file streamer's, so channels get
            // 0..7.
            if (mask & (EPBM_RIGHT | EPBM_LEFT)) {
                Mixer *m = Mixer::GetInstance();
                int col = viewData_->mixerCol_;
                int bus = m->GetBus(col);
                if (mask & EPBM_RIGHT) {
                    bus = (bus + 1) % STREAM_MIX_BUS;
                } else {
                    bus = (bus + STREAM_MIX_BUS - 1) % STREAM_MIX_BUS;
                }
                // PlayerMixer::Update pulls every mixer setting on each
                // audio tick, so the model is all that needs writing --
                // same as the volume and filter rows below.
                m->SetBus(col, bus);
                isDirty_ = true;
                char notifBuf[40];
                sprintf(notifBuf, "               Bus: %d", bus);
                SetNotification(notifBuf);
            }
        } else if (mixerRow_ >= MIXER_EQ_BASE) {
            /* The master EQ. Up and down move a band by 8 of its 127
               steps, left and right by one -- the same shape the send
               settings use, so the two lines behave alike.

               64 is flat. The band gain is v*v*8, so the control is
               fine near the middle where a decibel matters and coarse
               at the ends where it does not. */
            int b = mixerRow_ - MIXER_EQ_BASE;
            int step = 0;
            if (mask & EPBM_UP) step = 8;
            if (mask & EPBM_DOWN) step = -8;
            if (mask & EPBM_RIGHT) step = 1;
            if (mask & EPBM_LEFT) step = -1;
            if (step != 0) {
                Project *proj = viewData_->project_;
                int next = proj->GetEqBand(b) + step;
                if (next < 0) next = 0;
                if (next > MASTER_EQ_MAX) next = MASTER_EQ_MAX;
                proj->SetEqBand(b, next);
                isDirty_ = true;
                char notifBuf[48];
                /* Decibels, which is what an EQ speaks.

                   This used to read out a percentage of the CONTROL
                   value, which is a number about the knob rather than
                   about the sound: one step below flat printed "-1%"
                   when it is a quarter of a decibel, and the top of
                   the range printed "+98%" for +11.9dB. It also could
                   not say "flat" -- it said "+0%", which reads like
                   another number rather than the thing you were
                   looking for. */
                if (next == 0) {
                    sprintf(notifBuf, "        EQ %4s: off",
                            MasterEq::BandName(b));
                } else if (next == MASTER_EQ_FLAT) {
                    sprintf(notifBuf, "        EQ %4s: flat",
                            MasterEq::BandName(b));
                } else {
                    double db = 20.0 * log10(((double)next * next * 8.0) /
                                             32768.0);
                    sprintf(notifBuf, "        EQ %4s: %+.1f dB",
                            MasterEq::BandName(b), db);
                }
                SetNotification(notifBuf);
            }
        } else if (mixerRow_ >= MIXER_CHAN_ROWS) {
            // The effects themselves. The delay time is a musical
            // division rather than a number of milliseconds, so it
            // stays in time when the tempo changes -- an echo that
            // drifts off the grid is worse than no echo.
            Mixer *m = Mixer::GetInstance();
            int f = mixerRow_ - MIXER_CHAN_ROWS;
            int step = 0;
            if (mask & EPBM_UP) step = 16;
            if (mask & EPBM_DOWN) step = -16;
            if (mask & EPBM_RIGHT) step = 1;
            if (mask & EPBM_LEFT) step = -1;
            if (step != 0) {
                char notifBuf[48];
                if (f == 0) {
                    int d = m->GetDelayDivision() + (step > 0 ? 1 : -1);
                    if (d < 0) d = SendFx::DIV_COUNT - 1;
                    if (d >= SendFx::DIV_COUNT) d = 0;
                    m->SetDelayDivision(d);
                    sprintf(notifBuf, "        Delay Time: %s",
                            SendFx::DivisionName(d));
                } else {
                    int cur = (f == 1) ? m->GetDelayFeedback()
                            : (f == 2) ? m->GetReverbSize()
                                       : m->GetReverbDamp();
                    int next = cur + step;
                    if (next < 0) next = 0;
                    if (next > 255) next = 255;
                    if (f == 1) {
                        m->SetDelayFeedback(next);
                        sprintf(notifBuf, "    Delay Feedback: %d", next);
                    } else if (f == 2) {
                        m->SetReverbSize(next);
                        sprintf(notifBuf, "       Reverb Size: %d", next);
                    } else {
                        m->SetReverbDamp(next);
                        sprintf(notifBuf, "    Reverb Damping: %d", next);
                    }
                }
                isDirty_ = true;
                SetNotification(notifBuf);
            }
        } else if (mixerRow_ == 4 || mixerRow_ == 5) {
            // The send rows. Same law as the volume row -- UP/DOWN
            // coarse, LEFT/RIGHT fine -- because they are the same
            // kind of control and muscle memory should carry over.
            Mixer *m = Mixer::GetInstance();
            int col = viewData_->mixerCol_;
            bool isDelay = (mixerRow_ == 4);
            int cur = isDelay ? m->GetChannelDelaySend(col)
                              : m->GetChannelReverbSend(col);
            int next = cur;
            if (mask & EPBM_UP) next = cur + 16;
            if (mask & EPBM_DOWN) next = cur - 16;
            if (mask & EPBM_RIGHT) next = cur + 1;
            if (mask & EPBM_LEFT) next = cur - 1;
            if (next < 0) next = 0;
            if (next > 255) next = 255;
            if (next != cur) {
                if (isDelay) m->SetChannelDelaySend(col, next);
                else m->SetChannelReverbSend(col, next);
                isDirty_ = true;
                char notifBuf[40];
                sprintf(notifBuf, "        %s Send: %d",
                        isDelay ? "Delay " : "Reverb", next);
                SetNotification(notifBuf);
            }
        } else if (mixerRow_ == 1) {
            // On volume row: A adjusts volume
            Mixer *mixer = Mixer::GetInstance();
            int col = viewData_->mixerCol_;
            int currentVol = mixer->GetChannelVolume(col);
            int newVol = currentVol;

            // Coarse adjustment (UP/DOWN)
            if (mask & EPBM_UP) {
                newVol = currentVol + 16;
            }
            if (mask & EPBM_DOWN) {
                newVol = currentVol - 16;
            }

            // Fine adjustment (RIGHT/LEFT)
            if (mask & EPBM_RIGHT) {
                newVol = currentVol + 1;
            }
            if (mask & EPBM_LEFT) {
                newVol = currentVol - 1;
            }

            // Clamp to valid range (0-255)
            if (newVol < 0)
                newVol = 0;
            if (newVol > 255)
                newVol = 255;

            if (newVol != currentVol) {
                mixer->SetChannelVolume(col, newVol);
                isDirty_ = true;
            }
        }
    } else {
        // Normal cursor movement (no modifier)
		// This works everywhere, including Master channel
		if (mask & EPBM_UP)
			updateCursor(0, -1);
		if (mask & EPBM_DOWN)
			updateCursor(0, 1);
		if (mask & EPBM_LEFT)
			updateCursor(-1, 0);
		if (mask & EPBM_RIGHT)
			updateCursor(1, 0);

		if (mask & EPBM_START) {
			onStart();
		}
    }
}

/******************************************************
 processSelectionButtonMask:
        process button mask in the case there is a
        selection active
 ******************************************************/

void MixerView::processSelectionButtonMask(unsigned int mask) {

	// B Modifier

    if (mask & EPBM_B) {

    } else {

        // A modifier

        if (mask & EPBM_A) {

        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask&EPBM_START) {
				    onStop() ;
                }
            } else {

                // No modifier
                if (mask & EPBM_START) {
                    onStart();
                }
            }
        }
    }
}

// strip geometry: legend cols 0-2, nine 4-col strips from col 3
// (frames y 16-200, meters rows 3-13, value cells rows 15-18, box 20)

void MixerView::DrawView() {

    Clear();
    DrawTitleStrip("MIXER","");
    DrawHintBar("L+X mute  L+O solo  L+^ all  L+v cpu");

    GUITextProperties props;
    Player *player = Player::GetInstance();
    Mixer *mixer = Mixer::GetInstance();
    AppWindow &app = (AppWindow &)w_;

    // legend for the value rows, left of the strips
    static const char *rowLabels[MIXER_CHAN_ROWS] =
        {"bus", "vol", "hpf", "lpf", "dly", "rev"};
    for (int r = 0; r < MIXER_CHAN_ROWS; r++) {
        SetColor(mixerRow_ == r ? CD_HILITE2 : CD_ROW2);
        DrawString(0, MIXER_VAL_ROW + r, rowLabels[r], props);
    }
    SetColor(CD_NORMAL);

    // nine channel strips: header, stereo meters (drawn by
    // DrawVuBars), value cells, mute/solo box. Master is display-only.
    for (int i = 0; i < 9; i++) {
        int c = MIXER_STRIP_COL(i);
        int x0 = c * 8 + 2;
        // the box stops above the effects line, which belongs to no
        // single channel and should not look like it does
        app.OpFrame(x0, 13, 28, 165, CD_ROW);

        // header
        char head[2] = {(char)(i < 8 ? '1' + i : 'M'), 0};
        if (i == 8) {
            SetColor(CD_HILITE1);
        } else {
            SetColor(i == viewData_->mixerCol_ ? CD_HILITE1 : CD_ROW2);
        }
        DrawString(c + 1, 2, head, props);

        /* What KIND of instrument this strip is carrying.

           A mixer that shows eight identical numbered strips makes you
           remember which is the drums and which is the bass. One
           character beside the number says it: y for a synth, s for a
           sampler, m for MIDI, and a dash for a channel that has not
           played anything yet.

           Lowercase on purpose -- the channel number is the strip's
           name and should stay the loud thing; this sits beside it as
           a note, not a second heading. It follows the LAST instrument
           played rather than the sounding one, so it does not blink
           off in the gaps between notes. */
        if (i < 8) {
            InstrumentType it = player->GetChannelInstrumentType(i);
            char tc[2] = {'-', 0};
            switch (it) {
            case IT_SYNTH:  tc[0] = 'y'; break;
            case IT_SAMPLE: tc[0] = 's'; break;
            case IT_MIDI:   tc[0] = 'm'; break;
            default:        tc[0] = '-'; break;
            }
            SetColor(tc[0] == '-' ? CD_ROW : CD_ROW2);
            DrawString(c + 2, 2, tc, props);
            SetColor(CD_NORMAL);
        }

        if (i == 8) {
            // the master fader, on the volume row of the master strip
            char mv[8];
            sprintf(mv, "%d", viewData_->project_->GetMasterVolume());
            bool cursor = (viewData_->mixerCol_ == 8 && mixerRow_ == 1);
            props.invert_ = cursor;
            SetColor(cursor ? CD_HILITE2 : CD_NORMAL);
            DrawString(c + 1, MIXER_VAL_ROW + 1, mv, props);
            props.invert_ = false;
            SetColor(CD_ROW2);
            DrawString(c + 1, MIXER_MUTE_ROW, "st", props);
            continue;
        }

        // value cells: bus / vol / hpf / lpf
        char cell[4];
        for (int r = 0; r < MIXER_CHAN_ROWS; r++) {
            switch (r) {
            case 4:
                hex2char(mixer->GetChannelDelaySend(i), cell);
                break;
            case 5:
                hex2char(mixer->GetChannelReverbSend(i), cell);
                break;
            case 0:
                hex2char(mixer->GetBus(i), cell);
                break;
            case 1:
                hex2char(mixer->GetChannelVolume(i), cell);
                break;
            case 2: {
                int hpf = mixer->GetChannelHPF(i);
                cell[0] = (hpf == 0) ? '-' : (hpf == 1 ? '2' : '9');
                cell[1] = (hpf == 0) ? '-' : '0';
                cell[2] = 0;
                break;
            }
            case 3:
            default: {
                unsigned short lpf = mixer->GetChannelLPF(i);
                if (lpf == 0) {
                    cell[0] = '-'; cell[1] = '-';
                } else if (lpf < 100) {
                    cell[0] = '0' + (lpf / 10);
                    cell[1] = '0' + (lpf % 10);
                } else if (lpf < 1000) {
                    cell[0] = '0' + (lpf / 100);
                    cell[1] = 'h';
                } else {
                    cell[0] = '0' + (lpf / 1000);
                    cell[1] = 'k';
                }
                cell[2] = 0;
                break;
            }
            }
            bool cursor = (i == viewData_->mixerCol_ && mixerRow_ == r);
            props.invert_ = cursor;
            SetColor(cursor ? CD_HILITE2 : CD_NORMAL);
            DrawString(c + 1, MIXER_VAL_ROW + r, cell, props);
            props.invert_ = false;
        }

        // mute / solo box
        if (soloChannel_ == i) {
            props.invert_ = true;
            SetColor(CD_HILITE2);
            DrawString(c + 1, MIXER_MUTE_ROW, "S ", props);
            props.invert_ = false;
        } else if (player->IsChannelMuted(i)) {
            props.invert_ = true;
            SetColor(CD_CURSOR);
            DrawString(c + 1, MIXER_MUTE_ROW, "M ", props);
            props.invert_ = false;
        } else {
            SetColor(CD_ROW);
            DrawString(c + 1, MIXER_MUTE_ROW, "--", props);
        }
    }
    SetColor(CD_NORMAL);

    // the two effects themselves, on one line under the strips
    {
        static const int fxX[4] = {6, 14, 22, 29};
        char v[8];
        SetColor(CD_ROW2);
        DrawString(2, MIXER_FX_ROW, "dly", props);
        DrawString(11, MIXER_FX_ROW, "fb", props);
        DrawString(18, MIXER_FX_ROW, "rev", props);
        DrawString(25, MIXER_FX_ROW, "dmp", props);
        for (int f = 0; f < 4; f++) {
            bool cursor = (mixerRow_ == MIXER_CHAN_ROWS + f);
            switch (f) {
            case 0:
                strcpy(v, SendFx::DivisionName(mixer->GetDelayDivision()));
                break;
            case 1: hex2char(mixer->GetDelayFeedback(), v); break;
            case 2: hex2char(mixer->GetReverbSize(), v); break;
            default: hex2char(mixer->GetReverbDamp(), v); break;
            }
            props.invert_ = cursor;
            SetColor(cursor ? CD_HILITE2 : CD_NORMAL);
            DrawString(fxX[f], MIXER_FX_ROW, v, props);
            props.invert_ = false;
        }
        SetColor(CD_NORMAL);
    }

    /* The master EQ, ten bands on one line under the sends.

       Two columns a band: the band's gain in decibels, which is what
       an EQ actually speaks, colour-coded by direction so the shape
       still reads at a glance. */
    {
        Project *proj = viewData_->project_;
        SetColor(CD_ROW2);
        DrawString(0, MIXER_EQ_ROW, "eq", props);
        for (int b = 0; b < MASTER_EQ_BANDS; b++) {
            int x = 4 + b * 3;
            int v = proj->GetEqBand(b);
            // Real dB per band, which is what an EQ speaks -- right
            // aligned in two columns, cuts carry a minus, flat reads 0,
            // a killed band reads "of". Colour still carries the
            // direction at a glance: dim for flat, warm for boost, cool
            // for cut.
            char g[8];
            if (v == 0)                   { g[0]='o'; g[1]='f'; g[2]=0; }
            else if (v == MASTER_EQ_FLAT) { g[0]=' '; g[1]='0'; g[2]=0; }
            else {
                double db = 20.0 * log10((double)v * v * 8.0 / 32768.0);
                int idb = (int)(db >= 0 ? db + 0.5 : db - 0.5);
                if (idb > 99) idb = 99;
                if (idb < -99) idb = -99;
                sprintf(g, "%2d", idb);
            }
            bool cursor = (mixerRow_ == MIXER_EQ_BASE + b);
            props.invert_ = cursor;
            SetColor(cursor ? CD_HILITE2
                            : (v == MASTER_EQ_FLAT ? CD_ROW
                            : (v > MASTER_EQ_FLAT ? CD_HILITE1 : CD_HILITE2)));
            DrawString(x, MIXER_EQ_ROW, g, props);
            props.invert_ = false;
        }
        SetColor(CD_NORMAL);
    }

    // (no note readout here - the meters show channel activity)
    EnableNotification();

    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    }

    // Draw VU bars at the end so they appear on top of everything
    // They will always show the empty dashes and smoothly decay when paused
    DrawVuBars();
};

void MixerView::OnPlayerUpdate(PlayerEventType, unsigned int tick) {
    // live figures are on the title strip (DrawVuBars); the mute
    // boxes track player state on the next full draw
    // (no note readout here)
};

/******************************************************
 DrawVuBars:
        Draw VU meters below the mixer controls
        Called from both AnimationUpdate and DrawView to ensure
        bars are always visible and never flicker
 ******************************************************/

void MixerView::DrawVuBars() {
    Player *player = Player::GetInstance();
    AppWindow &app = (AppWindow &)w_;
    GUITextProperties props;
    bool running = player->IsRunning();

    /* Read the elapsed time ONCE.

       VuElapsedMs resets its own 'last' on every call, so asking it
       twice in one pass gives the second caller about 0ms. That was
       happening here: the hold timers took one reading and the decay
       took another, so the decay always saw ~1ms, and at 1ms the
       integer truncation in UpdateVuBarHeights makes the fall exactly
       one pixel per repaint -- ballistics tied to frame rate, which is
       the thing VuMeterUtil says was removed. */
    int vuMs = VuElapsedMs();

    // Read peak levels for all 9 channels
    float peakLevelsL[9];
    float peakLevelsR[9];

    if (!running) {
        for (int i = 0; i < 9; i++) {
            peakLevelsL[i] = peakLevelsR[i] = 0.0f;
        }
    } else {
        MixerService *ms = MixerService::GetInstance();

        /* Channels 0-7: the bus peak AFTER its own gain.

           This used to read the pre-gain peak, which meant no control
           the user can reach moved a channel bar: pregain is the bus's
           own volume and is applied after that tap, so turning it up
           made the master bar rise while the strip that caused it sat
           still. A meter you cannot move with the fader above it is
           the thing that reads as "no mixer behaves like this".

           The master fader is deliberately NOT folded in here. On a
           desk the master fader moves the master meter and leaves the
           channel meters alone, because a channel meter answers "what
           is this strip sending", not "what is left of it". */
        for (int i = 0; i < 8; i++) {
            MixBus *bus = ms->GetMixBus(i);
            if (bus) {
                uint32_t level = bus->GetPeakLevel();
                peakLevelsL[i] = (float)((level >> 16) & 0xFFFF) / 32767.0f;
                peakLevelsR[i] = (float)(level & 0xFFFF) / 32767.0f;
            } else {
                peakLevelsL[i] = peakLevelsR[i] = 0.0f;
            }
        }

        // Channel 8 (Master): the finished output, after the sum of
        // every bus plus the effects return, and after both clippers.
        // It is a SUM, so it routinely reads higher than any single
        // strip -- eight strips at 0.35 make 2.8 before the clipper.
        uint32_t masterLevel = ms->GetMasterPeakLevel();
        peakLevelsL[8] = (float)((masterLevel >> 16) & 0xFFFF) / 32767.0f;
        peakLevelsR[8] = (float)(masterLevel & 0xFFFF) / 32767.0f;
    }

    // ---- hot and clip lights -----------------------------------
    // A channel lights for either of two reasons, and both are worth
    // seeing: its own bus actually hit the clipper, or it is sitting
    // at the ceiling. The second is the one that matters here --
    // a channel at full scale is not distorting by itself, but it is
    // what makes the MASTER clip when everything lands on the same
    // beat, and until now there was no way to see which channel to
    // reach for. The master showing CLIP while every channel looked
    // fine was not a meter fault; the channels simply had no light.
    {
        int held = vuMs;
        MixerService *ms = MixerService::GetInstance();
        for (int i = 0; i < 9; i++) {
            bool hot = false;
            if (running) {
                if (i < 8) {
                    MixBus *bus = ms->GetMixBus(i);
                    if (bus && bus->TakeClipLatch()) hot = true;
                } else if (ms->TakeMasterClipLatch()) {
                    hot = true;
                }
                if (peakLevelsL[i] >= 0.99f || peakLevelsR[i] >= 0.99f)
                    hot = true;
            }
            if (hot) clipHold_[i] = 1200;
            else if (clipHold_[i] > 0) {
                clipHold_[i] -= held;
                if (clipHold_[i] < 0) clipHold_[i] = 0;
            }

            // HOT: this channel on its own is using half the output
            // range. Nothing is wrong with one of them; the point is
            // that two peaking together fill the master, so when the
            // mix sounds pushed these are the faders to reach for.
            // Judged against the ceiling, not against the other
            // channels, so it means the same thing in a sparse mix as
            // in a busy one.
            bool warm = running &&
                        (peakLevelsL[i] >= MIXER_HOT_LEVEL ||
                         peakLevelsR[i] >= MIXER_HOT_LEVEL);
            if (warm) hotHold_[i] = 700;
            else if (hotHold_[i] > 0) {
                hotHold_[i] -= held;
                if (hotHold_[i] < 0) hotHold_[i] = 0;
            }
        }
    }

    // slew-rate decay, then draw as pixel meters inside the strips
    int displayHeightsL[9];
    int displayHeightsR[9];
    // 88 steps, one per pixel of the meter, instead of 8. At eight
    // there was a single step between "loud" and "at the ceiling",
    // so the last thing a meter should be good at -- showing you how
    // close you are -- was the thing it could not show.
    // (vuMs is read once at the top of this function -- see there.)
    UpdateVuBarHeights(vuBarHeightsL_, displayHeightsL, peakLevelsL, 9,
                       vuMs, 88);
    UpdateVuBarHeights(vuBarHeightsR_, displayHeightsR, peakLevelsR, 9,
                       vuMs, 88);

    for (int i = 0; i < 9; i++) {
        int x0 = MIXER_STRIP_COL(i) * 8 + 2;
        int cap = (clipHold_[i] > 0) ? 2 : ((hotHold_[i] > 0) ? 1 : 0);
        app.OpVBar(x0 + 5, 26, 8, 88, displayHeightsL[i], cap);
        app.OpVBar(x0 + 15, 26, 8, 88, displayHeightsR[i], cap);
    }

    // live figures on the title strip
    char buf[48];
    int bpm = viewData_->project_->GetTempo();
    int time = running ? int(player->GetPlayTime()) : 0;
    if (time < 0) time = 0;
    int mi = (time / 60) % 100;
    int se = time % 60;
    if (bpm < 0) bpm = 0;
    if (bpm > 999) bpm = 999;
    if (player->Clipped()) {
        sprintf(buf, "CLIP -- %3dbpm %02d:%02d", bpm, mi, se);
    } else {
        // ui = repaints per second. It should sit at UIFRAMERATE
        // (60 by default); if it does not, the panel work is costing
        // more than the frame budget on this hardware.
        /* sat = the share of the last block the master sum spent
           pinned at the rail. 0 means the mix fits. Anything climbing
           means the buses are adding up to more than the accumulator
           can hold and the sound is being squared off before the
           fader or the clipper ever see it -- which is exactly the
           state that makes a mix sound crushed while every channel
           strip still looks reasonable. */
        unsigned int sat = running
            ? MixerService::GetInstance()->GetSaturationPercent() : 0;
        int master = viewData_->project_->GetMasterVolume();
        // dsp moved to the status bar; sat and the master fader are
        // the two figures only this screen can explain
        sprintf(buf, "sat%2d%% m%3d       ", sat > 99 ? 99 : sat, master);
    }
    SetColor(CD_HILITE2);
    DrawString(19, 0, buf, props);
    SetColor(CD_NORMAL);
}

void MixerView::AnimationUpdate() {
    if (HasModal())
        return; // never repaint the meters over a dialog
    DrawVuBars();
}

// nav-menu context prep: the mixer hands its column to whatever screen
// is entered, and seeds chain/phrase for the drill targets
void MixerView::OnNavTo(ViewType to) {
    if (to == VT_CHAIN || to == VT_TABLE || to == VT_PHRASE) {
        viewData_->songX_ = viewData_->mixerCol_ > 7 ? 7 : viewData_->mixerCol_;
        unsigned char *data = viewData_->GetCurrentSongPointer();
        if (*data != 0xFF) {
            viewData_->currentChain_ = *data;
        }
        if (to != VT_CHAIN) {
            data = viewData_->GetCurrentChainPointer();
            if (*data != 0xFF) {
                viewData_->currentPhrase_ = *data;
            }
        }
    }
}
