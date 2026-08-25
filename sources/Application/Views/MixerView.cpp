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
    if (dy != 0) {
        // UP/DOWN cycles rows 0..3. Row 0 is the bus, which is drawn,
        // saved, and read by PlayerMixer to route the channel -- it
        // just had no way to reach it, so every channel stayed on
        // whatever bus the file said forever.
        if (dy < 0) {
            mixerRow_ = (mixerRow_ == 0) ? MIXER_ROW_COUNT - 1 : mixerRow_ - 1;
        } else {
            mixerRow_ = (mixerRow_ == MIXER_ROW_COUNT - 1) ? 0 : mixerRow_ + 1;
        }
		isDirty_ = true;
    }
    if (dx != 0) {
        // LEFT/RIGHT switches between channels (0-7 only, Master is
        // display-only)
        int x = viewData_->mixerCol_;
		x += dx;
		if (x < 0) x = 0;
		if (x > 7) x = 7;
		viewData_->mixerCol_ = x;
		isDirty_ = true;
	}
}

void MixerView::toggleMute() {
    int col = viewData_->mixerCol_;
    UIController::GetInstance()->ToggleMute(col, col);
    isDirty_ = true;
};

void MixerView::toggleSolo() {
	int col=viewData_->mixerCol_ ;
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

    // R Modifier
    if (mask & EPBM_R) {
        if (mask & EPBM_B) {
            toggleMute();
        } else if (mask & EPBM_A) {
            toggleSolo();
        } else if (mask & EPBM_L) {
            unMuteAll();
        } else if (mask & EPBM_UP) {
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
        } else if (mask & EPBM_DOWN) {
            // The mixer is where the dsp figure lives, so this is
            // where the thing that explains it lives too.
            DoModal(new DspBenchModal(*this)) ;
        } else if (mask & EPBM_START) {
            onStop();
        }
    } else if (mask & EPBM_B) {
        if (mask & EPBM_A) {
            // B + A = cut: reset volume to full
            Mixer::GetInstance()->SetChannelVolume(viewData_->mixerCol_, 0xFF);
            isDirty_ = true;
        }
    } else if (mask & EPBM_A) {
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
    DrawHintBar("R+X mute  R+O solo  R+L all  R+v cpu");

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

        if (i == 8) {
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

    // Read peak levels for all 9 channels
    float peakLevelsL[9];
    float peakLevelsR[9];

    if (!running) {
        for (int i = 0; i < 9; i++) {
            peakLevelsL[i] = peakLevelsR[i] = 0.0f;
        }
    } else {
        MixerService *ms = MixerService::GetInstance();

        // Channels 0-7: Read pre-volume peaks
        for (int i = 0; i < 8; i++) {
            MixBus *bus = ms->GetMixBus(i);
            if (bus) {
                uint32_t level = bus->GetPreMasterVolumePeakLevel();
                peakLevelsL[i] = (float)((level >> 16) & 0xFFFF) / 32767.0f;
                peakLevelsR[i] = (float)(level & 0xFFFF) / 32767.0f;
            } else {
                peakLevelsL[i] = peakLevelsR[i] = 0.0f;
            }
        }

        // Channel 8 (Master): Read post-volume peaks
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
        int held = VuElapsedMs();
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
    int vuMs = VuElapsedMs();
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
    int dsp = running ? AudioStats::GetDspPercent() : 0;
    if (dsp > 99) dsp = 99;
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
        // How much the buses add up to before the master fader, and
        // what the fader is set to. Over 1.0 is normal and is what
        // the fader is for; it is only a problem when the fader
        // cannot pull it back, and now you can see which it is.
        unsigned int sum = running
            ? MixerService::GetInstance()->GetPreFaderSum() : 0;
        int master = viewData_->project_->GetMasterVolume();
        sprintf(buf, "sum%d.%02dx m%3d dsp%2d%%", sum >> 8,
                ((sum & 0xFF) * 100) >> 8, master, dsp > 99 ? 99 : dsp);
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
