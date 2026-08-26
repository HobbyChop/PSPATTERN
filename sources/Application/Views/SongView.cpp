#include "SongView.h"
#include "Application/AppWindow.h"
#include "Application/Commands/ApplicationCommandDispatcher.h"
#include "Application/Mixer/MixerService.h"
#include "Services/Audio/AudioStats.h"
#include "Application/Model/ProjectDatas.h"
#include "Application/Player/Player.h"
#include "Application/Utils/char.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include "UIController.h"
#include "VuMeterUtil.h"
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string>

// Low battery warning: enter below ON, leave above OFF, and blink
// twice a second on the wall clock rather than once per repaint.
#define BATT_WARN_ON  20
#define BATT_WARN_OFF 25
#define BATT_BLINK_MS 500


/****************
 Constructor
 ****************/

SongView::SongView(GUIWindow &w, ViewData *viewData, const char *song)
    : View(w, viewData) {

    updatingChain_ = false;
    lastChain_ = 0;
    songname_ = song;

    for (int i = 0; i < 8; i++) {
        this->lastPlayedPosition_[i] = 0;
        this->lastQueuedPosition_[i] = 0;
    }
    vuBarHeightsL_[0] = 0;
    vuBarHeightsR_[0] = 0;
    clipboard_.active_ = false;
    clipboard_.data_ = 0;
    invertBatt_ = false;
    battWarn_ = false;
    canDeepClone_ = false;
    jumpLength_ = 0x10; // B-jump 16 rows like LSDJ
}

/****************
 Destructor
 ****************/

SongView::~SongView() {
    if (clipboard_.data_ != 0)
        SYS_FREE((void *)clipboard_.data_);
};

/******************************************************
 updateChain:
        update current chain value by adding offset
        parameter
 ******************************************************/

void SongView::updateChain(int offset) {

    unsigned int chain = viewData_->UpdateSongChain(offset);
    updatingChain_ = true;
    lastChain_ = chain;
    updateX_ = viewData_->songX_;
    updateY_ = viewData_->songY_;
    isDirty_ = true;
    canDeepClone_ = false;
}

/******************************************************
 updateChain:
        set current chain value to value parameter
 ******************************************************/

void SongView::setChain(unsigned char value) {
    viewData_->SetSongChain(value);
    lastChain_ = value;
    isDirty_ = true;
}

/******************************************************
 updateSongOffset:
        Jump from the current position up or down
    by [offset] rows
 ******************************************************/

void SongView::updateSongOffset(int offset) {
    viewData_->UpdateSongOffset(offset);
    isDirty_ = true;
    canDeepClone_ = false;
}

/******************************************************
 updateCursor:
        modify location of cursor in view by
        adding dx & dy parameters
 ******************************************************/

void SongView::updateCursor(int dx, int dy) {
    viewData_->UpdateSongCursor(dx, dy);
    isDirty_ = true;
    canDeepClone_ = false;
}

/******************************************************
 cutPosition:
        copy current position content to clipboard &
        erase current position value
 ******************************************************/

void SongView::cutPosition() {

    // prepare selection data
    clipboard_.x_ = viewData_->songX_;
    clipboard_.y_ = viewData_->songY_;
    clipboard_.offset_ = viewData_->songOffset_;

    saveX_ = viewData_->songX_;
    saveY_ = viewData_->songY_;
    saveOffset_ = viewData_->songOffset_;

    // cut selection
    cutSelection();
};

/******************************************************
 pastePosition:
        set current position to last chain value if
        current step is empty
 ******************************************************/

void SongView::pasteLast() {

    // If we're on an empty spot, we past the last chain
    // otherwise we take the current chain as last

    unsigned char *c = viewData_->GetCurrentSongPointer();
    if (*c == 0xFF) {
        *c = lastChain_;
        viewData_->song_->chain_->SetUsed(*c);
        isDirty_ = true;
    } else {
        lastChain_ = *c;
    }
};

/******************************************************
 clonePosition:
        slim clone current position
 ******************************************************/

void SongView::clonePosition() {

    unsigned char *pos = viewData_->GetCurrentSongPointer();
    unsigned char current = *pos;
    if (current == 255)
        return;

    unsigned short next = viewData_->song_->chain_->GetNext();
    if (next == NO_MORE_CHAIN)
        return;

    unsigned char *src = viewData_->song_->chain_->data_ + 16 * current;
    unsigned char *dst = viewData_->song_->chain_->data_ + 16 * next;

    for (int i = 0; i < 16; i++) {
        *dst++ = *src++;
    };

    src = viewData_->song_->chain_->transpose_ + 16 * current;
    dst = viewData_->song_->chain_->transpose_ + 16 * next;

    for (int i = 0; i < 16; i++) {
        *dst++ = *src++;
    };
    setChain((unsigned char)next);
    isDirty_ = true;
};

/******************************************************
 deepClonePosition:
        deep clone chain and all phrases within
        made by koisignal (https://github.com/koi-ikeno)
 ******************************************************/

void SongView::deepClonePosition() {
    Phrase *ph = viewData_->song_->phrase_;
    Chain *ch = viewData_->song_->chain_;
    unsigned char *pos = viewData_->GetCurrentSongPointer();
    unsigned char curChainNum = *pos;

    if (curChainNum == CHAIN_COUNT) {
        View::SetNotification("no more chains!");
        return;
    }

    unsigned char *srcChain = ch->data_ + 16 * curChainNum;
    unsigned char *dstChain = ch->data_ + 16 * curChainNum;
    unsigned short srcPhrases[16];
    unsigned short dstPhrases[16];

    // Init outside valid range
    for (int i = 0; i < 16; i++) {
        srcPhrases[i] = NO_MORE_CHAIN;
        dstPhrases[i] = NO_MORE_CHAIN;
    }

    for (int i = 0; i < 16; i++) {
        unsigned short srcPhraseNum = *srcChain;

        // skip when "--"
        if (srcPhraseNum == CHAIN_COUNT) {
            srcChain++;
            dstChain++;
            continue;
        }

        unsigned short newPhraseNum = NO_MORE_CHAIN;

        for (int j = 0; j < 16; j++) {
            if (srcPhrases[j] == srcPhraseNum) {
                newPhraseNum = dstPhrases[j];
                break;
            }
        }

        if (newPhraseNum == NO_MORE_CHAIN) {
            newPhraseNum = ph->GetNext();
            if (newPhraseNum == NO_MORE_PHRASE) {
                View::SetNotification("no more phrases!");
                return;
            }
            for (int k = 0; k < 16; k++) {
                *(ph->note_ + 16 * newPhraseNum + k) =
                    *(ph->note_ + 16 * srcPhraseNum + k);
                *(ph->instr_ + 16 * newPhraseNum + k) =
                    *(ph->instr_ + 16 * srcPhraseNum + k);
                *(ph->cmd1_ + 16 * newPhraseNum + k) =
                    *(ph->cmd1_ + 16 * srcPhraseNum + k);
                *(ph->cmd2_ + 16 * newPhraseNum + k) =
                    *(ph->cmd2_ + 16 * srcPhraseNum + k);
                *(ph->param1_ + 16 * newPhraseNum + k) =
                    *(ph->param1_ + 16 * srcPhraseNum + k);
                *(ph->param2_ + 16 * newPhraseNum + k) =
                    *(ph->param2_ + 16 * srcPhraseNum + k);
            }
        }
        srcPhrases[i] = srcPhraseNum;
        dstPhrases[i] = newPhraseNum;
        *dstChain = newPhraseNum;
        srcChain++;
        dstChain++;
    }
    View::SetNotification("deep clone");

    setChain((unsigned char)curChainNum);
}

void SongView::extendSelection() {
    GUIRect rect = getSelectionRect();
    if (rect.Left() > 0 || rect.Right() < 7) {
        if (viewData_->songX_ < clipboard_.x_) {
            viewData_->songX_ = 0;
            clipboard_.x_ = 7;
        } else {
            viewData_->songX_ = 7;
            clipboard_.x_ = 0;
        }
        isDirty_ = true;
    } else {
        if (viewData_->songY_ < clipboard_.y_) {
            viewData_->songY_ = 0;
            clipboard_.y_ = 0x17;
        } else {
            clipboard_.y_ = 0;
            viewData_->songY_ = 0x17;
        }
        isDirty_ = true;
    }
}

/******************************************************
 OnFocus:
        called when current view is becoming active
 ******************************************************/

void SongView::OnFocus() { clipboard_.active_ = false; };

GUIRect SongView::getSelectionRect() {

    GUIRect selRect(clipboard_.x_, clipboard_.y_ + clipboard_.offset_,
                    viewData_->songX_,
                    viewData_->songY_ + viewData_->songOffset_);

    selRect.Normalize();
    return selRect;
}

/******************************************************
 fillClipboard:
        fill clipboard with current selection value
 ******************************************************/

void SongView::fillClipboardData() {

    // Clear current selection data

    if (!clipboard_.data_)
        SYS_FREE((void *)clipboard_.data_);

    // Prepare selection related information

    GUIRect selRect = getSelectionRect();

    // Set current selection  data

    clipboard_.width_ = selRect.Width() + 1;
    clipboard_.height_ = selRect.Height() + 1;

    clipboard_.data_ =
        (unsigned char *)SYS_MALLOC(clipboard_.width_ * clipboard_.height_);

    unsigned char *src = viewData_->song_->data_ + selRect.Left() +
                         SONG_CHANNEL_COUNT * selRect.Top();
    unsigned char *dst = clipboard_.data_;

    for (int j = 0; j < clipboard_.height_; j++) {
        for (int i = 0; i < clipboard_.width_; i++) {
            *dst++ = *src++;
        }
        src += (SONG_CHANNEL_COUNT - clipboard_.width_);
    }
};

/******************************************************
 copySelection:
        copy current selection to clipboard
 ******************************************************/

void SongView::copySelection() {

    fillClipboardData();
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    viewData_->songX_ = saveX_;
    viewData_->songY_ = saveY_;
    viewData_->songOffset_ = saveOffset_;
    View::SetNotification("copied selection");
}

/******************************************************
 cutSelection:
        cut current selection to clipboard
 ******************************************************/

void SongView::cutSelection() {

    // first copy the data to clipboard

    fillClipboardData();
    GUIRect selRect = getSelectionRect();

    // now move all rows up for cut

    unsigned char *dst = viewData_->song_->data_ + selRect.Left() +
                         SONG_CHANNEL_COUNT * (selRect.Top());
    unsigned char *src = dst + SONG_CHANNEL_COUNT * clipboard_.height_;

    int rowCount = SONG_ROW_COUNT - selRect.Bottom() - 1;

    for (int j = 0; j < rowCount; j++) {

        for (int i = 0; i < clipboard_.width_; i++) {
            *dst++ = *src++;
        }
        src += (SONG_CHANNEL_COUNT - clipboard_.width_);
        dst += (SONG_CHANNEL_COUNT - clipboard_.width_);
    }

    for (int j = 0; j > clipboard_.height_; j++) {
        for (int i = 0; i < clipboard_.width_; i++) {
            *dst++ = 0xFF;
        }
        dst += (SONG_CHANNEL_COUNT - clipboard_.width_);
    };

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    viewData_->songX_ = saveX_;
    viewData_->songY_ = saveY_;
    viewData_->songOffset_ = saveOffset_;

    isDirty_ = true;
}

/******************************************************
 pasteSelection:
        paste clipboard content to song
 ******************************************************/

void SongView::pasteClipboard() {

    if (!clipboard_.data_)
        return;

    // Check we're not out of scope

    int width = clipboard_.width_;
    int height = clipboard_.height_;

    if (viewData_->songX_ + width > SONG_CHANNEL_COUNT) {
        width = SONG_CHANNEL_COUNT - viewData_->songX_;
    }
    if (viewData_->songY_ + viewData_->songOffset_ + height > SONG_ROW_COUNT) {
        height = SONG_ROW_COUNT - viewData_->songY_ - viewData_->songOffset_;
    } else {

        // Move down from insert point

        unsigned char *dst = viewData_->song_->data_ + viewData_->songX_ +
                             (SONG_ROW_COUNT - 1) * SONG_CHANNEL_COUNT;
        unsigned char *src = dst - height * SONG_CHANNEL_COUNT;

        int rowCount =
            SONG_ROW_COUNT - (viewData_->songY_ + viewData_->songOffset_);

        for (int j = 0; j < rowCount; j++) {
            for (int i = 0; i < width; i++) {
                *dst++ = *src++;
            }
            dst -= (SONG_CHANNEL_COUNT + width);
            src -= (SONG_CHANNEL_COUNT + width);
        }
    }

    // Prepare copy pointer

    unsigned char *dst = viewData_->GetCurrentSongPointer();
    unsigned char *src = clipboard_.data_;

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            *dst++ = *src++;
        }
        dst += (SONG_CHANNEL_COUNT - width);
        src += (clipboard_.width_ - width);
    }

    updateCursor(0, height);
}

void SongView::unMuteAll() {

    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
};

void SongView::toggleMute() {

    UIController *controller = UIController::GetInstance();

    int from = viewData_->songX_;
    int to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    };
    controller->ToggleMute(from, to);
    viewMode_ = (viewMode_ != VM_MUTEON) ? VM_MUTEON : VM_NORMAL;
};

void SongView::switchSoloMode() {

    UIController *controller = UIController::GetInstance();
    int from = viewData_->songX_;
    int to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    };
    controller->SwitchSoloMode(from, to, (viewMode_ != VM_SOLOON));
    viewMode_ = (viewMode_ != VM_SOLOON) ? VM_SOLOON : VM_NORMAL;
    isDirty_ = true;
};

void SongView::onStart() {
    // Always play with zero offset in chains when in SongView
    viewData_->chainRow_ = 0;
    Player *player = Player::GetInstance();
    unsigned char from = viewData_->songX_;
    unsigned char to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    }
    // The render control on the project screen sets a project variable, but
    // nothing ever carried it to the mixer -- so "render Stereo" persisted
    // into the save file and START just played the song. Read it here, where
    // playback actually begins. (MSRM_PLAYBACK/STEREO/STEMS line up with the
    // project's Off/Stereo/Stems.)
    int renderMode = viewData_->project_->GetRenderMode();
    viewData_->renderMode_ = renderMode;
    if (renderMode > 0 && !player->IsRunning()) {
        MixerService::GetInstance()->SetRenderMode(renderMode);
        viewData_->isRendering_ = true;
        View::SetNotification("rendering to wav...");
    } else if (viewData_->isRendering_ && player->IsRunning()) {
        viewData_->isRendering_ = false;
        MixerService::GetInstance()->SetRenderMode(MSRM_PLAYBACK);
        View::SetNotification("render complete");
    }
    player->OnSongStartButton(from, to, false, false);
};

void SongView::startCurrentRow() {
    Player *player = Player::GetInstance();
    player->SetSequencerMode(SM_LIVE);
    player->OnSongStartButton(0, 7, false, false);
}

void SongView::startImmediate() {
    Player *player = Player::GetInstance();

    unsigned char from = viewData_->songX_;
    unsigned char to = from;
    player->OnSongStartButton(from, to, false, true);
}

void SongView::onStop() {
    // Always play with zero offset in chains when in SongView
    viewData_->chainRow_ = 0;
    Player *player = Player::GetInstance();
    unsigned char from = viewData_->songX_;
    unsigned char to = from;
    if (clipboard_.active_) {
        GUIRect r = getSelectionRect();
        from = r.Left();
        to = r.Right();
    }

    player->OnSongStartButton(from, to, true, false);
};

void SongView::jumpToNextSection(int direction) {

    int current = viewData_->songY_ + viewData_->songOffset_;
    bool foundGap = false;
    for (int i = 0; i < SONG_ROW_COUNT; i++) {
        unsigned char *start = viewData_->song_->data_ + viewData_->songX_ +
                               SONG_CHANNEL_COUNT * current;
        if (foundGap && (*start != 0xFF)) {
            break;
        } else {
            if (*start == 0xFF) {
                foundGap = true;
            }
        }
        current += direction;
        if (current < 0) {
            current += SONG_ROW_COUNT;
        }
        if (current >= SONG_ROW_COUNT) {
            current -= SONG_ROW_COUNT;
        }
    }
    // If we go backwards, we stil have to go to the beginning of the block

    if (direction < 0) {
        while (current > 0) {
            unsigned char *start = viewData_->song_->data_ + viewData_->songX_ +
                                   SONG_CHANNEL_COUNT * current;
            if (*start == 0xFF) {
                current++;
                break;
            };
            current--;
        };
    }

    // Update viewdata position from current

    if ((current - viewData_->songOffset_ > 0x17) ||
        (current - viewData_->songOffset_ < 0)) {
        viewData_->songOffset_ = current - 4;
        if (viewData_->songOffset_ < 0) {
            viewData_->songOffset_ = 0;
        }
    }
    viewData_->songY_ = current - viewData_->songOffset_;
    isDirty_ = true;
}

/******************************************************
 ProcessButtonMask:
        process button mask even coming from the main
        application window
 ******************************************************/

void SongView::ProcessButtonMask(unsigned short mask, bool pressed) {

    if (!pressed) {
        if (viewMode_ == VM_MUTEON) {
            if (mask & EPBM_L) {
                toggleMute();
            }
        };
        if (viewMode_ == VM_SOLOON) {
            if (mask & EPBM_L) {
                switchSoloMode();
            }
        };
        return;
    };

    if (viewMode_ == VM_NEW) {
        if (mask == EPBM_A) {
            unsigned short next = viewData_->song_->chain_->GetNext();
            if (next != NO_MORE_CHAIN) {
                setChain((unsigned char)next);
                isDirty_ = true;
            }
            mask &= (0xFFFF - EPBM_A);
        }
    }

    if (viewMode_ == VM_CLONE) {
        if ((mask & EPBM_A) && (mask & EPBM_R)) {
            clonePosition();
            mask &= (0xFFFF - (EPBM_A | EPBM_R));
            canDeepClone_ = true;
        } else {
            viewMode_ = VM_SELECTION;
        }
    };

    if (canDeepClone_ && (mask & EPBM_A) && (mask & EPBM_R)) {
        deepClonePosition();
        mask &= (0xFFFF - (EPBM_A | EPBM_L));
        canDeepClone_ = false;
    }
    if (clipboard_.active_) {
        viewMode_ = VM_SELECTION;
    };
    // Process selection related keys

    if (viewMode_ == VM_SELECTION) {
        if (clipboard_.active_ == false) {
            clipboard_.active_ = true;
            clipboard_.x_ = viewData_->songX_;
            clipboard_.y_ = viewData_->songY_;
            clipboard_.offset_ = viewData_->songOffset_;
            saveX_ = clipboard_.x_;
            saveY_ = clipboard_.y_;
            saveOffset_ = clipboard_.offset_;
        }
        processSelectionButtonMask(mask);
    } else {

        // Switch back to normal mode

        viewMode_ = VM_NORMAL;
        processNormalButtonMask(mask);
    }
}

/******************************************************
 processNormalButtonMask:
        process button mask in the case there is no
        selection active
 ******************************************************/

void SongView::processNormalButtonMask(unsigned int mask) {

    // B Modifier

    if (mask & EPBM_B) {

        if (mask & EPBM_DOWN)
            updateSongOffset(SongView::jumpLength_);
        if (mask & EPBM_UP)
            updateSongOffset(-SongView::jumpLength_);
        if (mask & (EPBM_RIGHT | EPBM_LEFT)) {
            Player *player = Player::GetInstance();
            switch (player->GetSequencerMode()) {
            case SM_SONG:
                player->SetSequencerMode(SM_LIVE);
                break;
            case SM_LIVE:
                player->SetSequencerMode(SM_SONG);
                break;
            }
            isDirty_ = true;
        }
        if ((mask & EPBM_A) && (!(mask & EPBM_L)))
            cutPosition();
        if (mask & EPBM_R) {

            viewMode_ = VM_CLONE;
        };
        if (mask & EPBM_L) {
            toggleMute();
        };
        if (mask & EPBM_START) {
            startImmediate();
        }
    } else {

        // A modifier

        if (mask & EPBM_A) {

            if (mask & EPBM_DOWN)
                updateChain(-0x10);
            if (mask & EPBM_UP)
                updateChain(0x10);
            if (mask & EPBM_LEFT)
                updateChain(-0x01);
            if (mask & EPBM_RIGHT)
                updateChain(0x01);
            if (mask & EPBM_R && !canDeepClone_) {
                pasteClipboard();
            }
            if (mask == EPBM_A) {

                pasteLast();
                viewMode_ = VM_NEW;
            }
            if (mask & EPBM_L) {
                switchSoloMode();
            };
        } else {

            // R Modifier

            if (mask & EPBM_R) {

                if (mask & EPBM_L) {
                    unMuteAll();
                }

                if (mask & EPBM_RIGHT) {
                    unsigned char *data = viewData_->GetCurrentSongPointer();
                    if (*data != 0xFF) {
                        ViewType vt = VT_CHAIN;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        viewData_->currentChain_ = *data;
                        SetChanged();
                        NotifyObservers(&ve);
                    }
                }

                if (mask & EPBM_UP) {
                    ViewType vt = VT_PROJECT;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {
                    ViewType vt = VT_MIXER;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_START) {
                    onStop();
                }

            } else {

                // L Modifier

                if (mask & EPBM_L) {
                    if (mask & EPBM_DOWN)
                        jumpToNextSection(1);
                    if (mask & EPBM_UP)
                        jumpToNextSection(-1);
                    if (mask & EPBM_START)
                        startCurrentRow();
                    if (mask & EPBM_LEFT)
                        nudgeTempo(-1);
                    if (mask & EPBM_RIGHT)
                        nudgeTempo(1);
                } else {

                    // No modifier

                    if (mask & EPBM_DOWN)
                        updateCursor(0, 1);
                    if (mask & EPBM_UP)
                        updateCursor(0, -1);
                    if (mask & EPBM_LEFT)
                        updateCursor(-1, 0);
                    if (mask & EPBM_RIGHT)
                        updateCursor(1, 0);

                    if (mask & EPBM_START) {
                        onStart();
                    }
                }
            }
        }
    }

    if ((!(mask & EPBM_A)) && updatingChain_) {
        unsigned char *c = viewData_->song_->data_ + updateX_ +
                           8 * (viewData_->songOffset_ + updateY_);
        viewData_->song_->chain_->SetUsed(*c);
        updatingChain_ = false;
    }
};

/******************************************************
 processSelectionButtonMask:
        process button mask in the case there is a
        selection active
 ******************************************************/

void SongView::processSelectionButtonMask(unsigned int mask) {

    // B Modifier

    if (mask & EPBM_B) {
        if (mask & EPBM_L) {
            toggleMute();
        };
        if (mask & EPBM_R) {
            extendSelection();
        };
        if (mask == EPBM_B) {
            copySelection();
        }

    } else {

        // A modifier

        if (mask & EPBM_A) {
            if (mask & EPBM_R) {
                cutSelection();
            }
            if (mask & EPBM_L) {
                switchSoloMode();
            };
        } else {

            // R Modifier

            if (mask & EPBM_R) {

                if (mask & EPBM_L) {
                    unMuteAll();
                }

                if (mask & EPBM_RIGHT) {
                    unsigned char *data = viewData_->GetCurrentSongPointer();
                    if (*data != 0xFF) {
                        ViewType vt = VT_CHAIN;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        viewData_->currentChain_ = *data;
                        SetChanged();
                        NotifyObservers(&ve);
                    }
                }

                if (mask & EPBM_UP) {
                    ViewType vt = VT_PROJECT;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {
                    ViewType vt = VT_MIXER;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_START) {
                    onStop();
                }

            } else {

                // No modifier

                if (mask & EPBM_DOWN)
                    updateCursor(0, 1);
                if (mask & EPBM_UP)
                    updateCursor(0, -1);
                if (mask & EPBM_LEFT)
                    updateCursor(-1, 0);
                if (mask & EPBM_RIGHT)
                    updateCursor(1, 0);
                if (mask & EPBM_START) {
                    onStart();
                }
            }
        }
    }
}

/******************************************************
 DrawVuBars:
        Draw stereo L/R VU meters to the right of tracks
        Called from both AnimationUpdate and DrawView to ensure
        bars are always visible and never flicker
 ******************************************************/

void SongView::DrawVuBars() {
    Player *player = Player::GetInstance();

    GUIPoint anchor = GetAnchor();
    GUIPoint vuPos = anchor;
    vuPos._x += 25;
    vuPos._y += View::songRowCount_ - 1;
    
    GUITextProperties vuProps;
    vuProps.invert_ = true;

    float peakLevelsL[1];
    float peakLevelsR[1];

    // Read master peak level (post-volume) to match MixerView's Master meter
    if (!player->IsRunning()) {
        peakLevelsL[0] = peakLevelsR[0] = 0.0f;
    } else {
        MixerService *ms = MixerService::GetInstance();
        uint32_t masterLevel = ms->GetMasterPeakLevel();
        peakLevelsL[0] = (float)((masterLevel >> 16) & 0xFFFF) / 32767.0f;
        peakLevelsR[0] = (float)(masterLevel & 0xFFFF) / 32767.0f;
    }

    // Update bar height for master using utility
    int displayHeightsL[1];
    int displayHeightsR[1];
    int vuMs = VuElapsedMs();
    UpdateVuBarHeights(vuBarHeightsL_, displayHeightsL, peakLevelsL, 1,
                       vuMs);
    UpdateVuBarHeights(vuBarHeightsR_, displayHeightsR, peakLevelsR, 1,
                       vuMs);

    // Draw vertical VU bars for L and R channels (two columns side by side)
    for (int row = 0; row < VU_METER_HEIGHT; row++) {
        SetColor(GetVuBarColor(row));

        // Draw left channel at x
        GUIPoint posL = vuPos;
        posL._y -= row;
        DrawVuBarRow(this, posL, row, displayHeightsL[0], vuProps,
                     GetVuBarColor(row));

        // Draw right channel at x+1 (one character to the right)
        GUIPoint posR = vuPos;
        posR._x += 1;
        posR._y -= row;
        DrawVuBarRow(this, posR, row, displayHeightsR[0], vuProps,
                     GetVuBarColor(row));
    }

    SetColor(CD_NORMAL);
}

/******************************************************
 DrawSidePanel:
        The right-hand panel stack of the song screen —
        scope / mix info / live state — plus the live
        figures on the title strip. Redrawn every
        animation tick.
 ******************************************************/

// the song grid hugs the left edge; the panel column owns cols 27-39
#define SONG_GRID_X 3
#define PANEL_X 27
#define PANEL_TXT (PANEL_X + 1)

void SongView::DrawSidePanel() {

    GUITextProperties props;
    AppWindow &app = (AppWindow &)w_;
    Player *player = Player::GetInstance();
    bool running = player->IsRunning();
    char buf[48];

    // --- scope: live master-mix waveform ---------------------------
    DrawPanel(PANEL_X, 2, 12, 6, "scope");
    static short scopeTick = 0;
    if (running) scopeTick++;
    app.OpScope(PANEL_X * 8 + 5, 24, 92, 44, running ? scopeTick : 0);

    // --- mix: dsp load, tempo, project, midi, battery --------------
    DrawPanel(PANEL_X, 10, 12, 5, "mix");
    // A dropout is one block missing its deadline. The figure on the
    // left of this line is an average smoothed over eight blocks, so a
    // spike to three hundred per cent moves it a few points and is
    // gone before anybody looks: a machine can glitch steadily while
    // this reads twenty. It also used to clamp at 99, so an overrun
    // could not be shown even when it was being averaged in.
    //
    // When a block has recently overrun, show the worst one instead,
    // with an exclamation mark rather than a per cent sign, in the
    // mute colour so it reads as a warning at a glance.
    int dsp = running ? AudioStats::GetDspPercent() : 0;
    int peak = running ? AudioStats::GetDspPeak() : 0;
    bool over = (peak > 100);
    int shown = over ? peak : dsp;
    if (shown > 999) shown = 999;
    SetColor(CD_ROW2);
    DrawString(PANEL_TXT, 11, "dsp", props);
    app.OpBar(PANEL_TXT * 8 + 26, 11 * 8 + 1, 34,
              (dsp > 100 ? 100 : dsp) * 34 / 100, false);
    if (over) {
        sprintf(buf, "%3d!", shown);
        SetColor(CD_MUTE);
    } else {
        sprintf(buf, "%2d%%", shown);
        SetColor(CD_HILITE2);
    }
    DrawString(36, 11, buf, props);

    int bpm = viewData_->project_->GetTempo();
    SetColor(CD_ROW2);
    DrawString(PANEL_TXT, 12, "bpm", props);
    sprintf(buf, "%3d", bpm);
    SetColor(CD_HILITE2);
    DrawString(36, 12, buf, props);

    SetColor(CD_ROW2);
    DrawString(PANEL_TXT, 13, "prj", props);
    char name[8];
    strncpy(name, songname_.substr(0, 5) == "lgpt_"
                      ? songname_.c_str() + 5
                      : songname_.c_str(),
            7);
    name[7] = 0;
    SetColor(CD_NORMAL);
    sprintf(buf, "%7s", name);
    DrawString(32, 13, buf, props);

    SetColor(CD_ROW2);
    DrawString(PANEL_TXT, 14, "mid", props);
    const char *ctrl = Config::GetInstance()->GetValue("MIDICTRLDEVICE");
    sprintf(buf, "%7s", ctrl ? ctrl : "none");
    SetColor(CD_HILITE1);
    DrawString(32, 14, buf, props);

    System *sys = System::GetInstance();
    int batt = sys->GetBatteryLevel();
    if (batt >= 0) {
        if (batt > 999) batt = 999;
        SetColor(CD_ROW2);
        DrawString(PANEL_TXT, 15, "bat", props);
        // The low battery warning blinks on the wall clock, not once
        // per draw. This panel is one of the animated ones, so it
        // repaints every UI frame -- inverting on each repaint made a
        // low battery strobe at frame rate instead of blinking.
        //
        // The threshold gets hysteresis for the same reason: a battery
        // sitting on the line reads 19 and 21 on alternate polls, and
        // with a single threshold that alone flipped the colour
        // between the warning and the normal one every few frames.
        if (batt < BATT_WARN_ON) {
            battWarn_ = true;
        } else if (batt > BATT_WARN_OFF) {
            battWarn_ = false;
        }
        invertBatt_ =
            battWarn_ && (((sys->GetClock() / BATT_BLINK_MS) & 1) != 0);
        props.invert_ = invertBatt_;
        sprintf(buf, "%3d%%", batt);
        SetColor(battWarn_ ? CD_HILITE2 : CD_NORMAL);
        DrawString(35, 15, buf, props);
        props.invert_ = false;
    }

    // --- live: queue + mute state per channel ----------------------
    DrawPanel(PANEL_X, 17, 12, 2, "live");
    SetColor(CD_ROW2);
    DrawString(PANEL_TXT, 18, "cue", props);
    DrawString(PANEL_TXT, 19, "mut", props);
    bool liveMode = (player->GetSequencerMode() == SM_LIVE);
    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        char c[2] = {'-', 0};
        if (liveMode && running && player->GetQueueingMode(i) != QM_NONE) {
            c[0] = player->GetLiveIndicator(i)[0];
            SetColor(CD_PLAY);
        } else {
            SetColor(CD_ROW);
        }
        DrawString(31 + i, 18, c, props);
        if (player->IsChannelMuted(i)) {
            c[0] = 'm';
            SetColor(CD_MUTE);
        } else {
            c[0] = '-';
            SetColor(CD_ROW);
        }
        DrawString(31 + i, 19, c, props);
    }

    // --- title-strip live figures: dsp / clip, tempo, play time ----
    int time = running ? int(player->GetPlayTime()) : 0;
    if (time < 0) time = 0;
    int mi = (time / 60) % 100;
    int se = time % 60;
    if (bpm < 0) bpm = 0;
    if (bpm > 999) bpm = 999;
    if (player->Clipped()) {
        sprintf(buf, "CLIP -- %3dbpm %02d:%02d", bpm, mi, se);
    } else {
        sprintf(buf, "dsp%3d%% %3dbpm %02d:%02d", dsp, bpm, mi, se);
    }
    SetColor(CD_HILITE2);
    DrawString(19, 0, buf, props);
    SetColor(CD_NORMAL);
}

/******************************************************
 AnimationUpdate:
        Update animation and draw VU meters
 ******************************************************/

void SongView::AnimationUpdate() {
    if (HasModal())
        return; // never repaint the panels over a dialog
    DrawSidePanel();
}

/******************************************************
 Redraw:
        redraw completely the song view
 ******************************************************/

/* The whole song grid. Clone and deep clone also allocate new chains
   and phrases, but restoring the grid entry is what makes the edit go
   away -- the orphaned chain is simply unused again. */
int SongView::UndoSize() { return SONG_ROW_COUNT*SONG_CHANNEL_COUNT ; }
int SongView::UndoContext() { return 0 ; }
void SongView::UndoCapture(unsigned char *dst) {
	memcpy(dst,viewData_->song_->data_,SONG_ROW_COUNT*SONG_CHANNEL_COUNT) ;
}
void SongView::UndoRestore(int context,const unsigned char *src) {
	memcpy(viewData_->song_->data_,src,SONG_ROW_COUNT*SONG_CHANNEL_COUNT) ;
}

void SongView::DrawView() {

    Clear();
    View::EnableNotification();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    // Prepare selection related information

    GUIRect selRect;
    if (clipboard_.active_) {
        selRect = GUIRect(clipboard_.x_, clipboard_.y_ + clipboard_.offset_,
                          viewData_->songX_,
                          viewData_->songY_ + viewData_->songOffset_);

        selRect.Normalize();
    }

    // Draw title

    SetColor(CD_NORMAL);

    Player *player = Player::GetInstance();

    std::ostringstream os;

    os << ((player->GetSequencerMode() == SM_SONG) ? "SONG " : "LIVE ");

    if (songname_.substr(0, 5) == "lgpt_") {
        os << songname_.substr(5);
    } else {
        os << songname_;
    }

    std::string buffer(os.str());

    DrawTitleStrip(buffer.c_str(), "");

    // the sequencer grid panel: frame, column headers, cursor band.
    // The song grid hugs the left edge (row numbers cols 0-1, grid
    // cols 3-25) so the side panel column owns cols 27-39.
    {
        AppWindow &app = (AppWindow &)w_;
        GUIPoint ga = GetAnchor();
        ga._x = SONG_GRID_X;
        // Height stops one pixel short of row 26, where drawNotes
        // puts the live note readout -- at the old +10 the bottom
        // rule cut through those letters. It now reads as the rule
        // between the grid and the readout, which is what it is.
        app.OpFrame(0, ga._y * 8 - 6, 212, View::songRowCount_ * 8 + 5,
                    CD_ROW);
        // current-row band under the grid text
        app.OpRect(0, 1, (ga._y + viewData_->songY_) * 8 - 1, 209, 9,
                   AppWindow::OC_PANEL2);
        // channel headers, clear of the frame's top edge
        SetColor(CD_ROW);
        GUITextProperties hprops;
        for (int i = 0; i < 8; i++) {
            char h[2] = {(char)('1' + i), 0};
            DrawString(ga._x + i * 3, ga._y - 2, h, hprops);
        }
        SetColor(CD_NORMAL);
    }

    // Compute song grid location

    GUIPoint anchor = GetAnchor();
    anchor._x = SONG_GRID_X;

    // Display row numbers

    char row[3];
    pos = anchor;
    pos._x -= 3;
    for (int j = 0; j < View::songRowCount_; j++) {
        char p = j + viewData_->songOffset_;
        ((p / altRowNumber_) % 2) ? SetColor(CD_ROW) : SetColor(CD_ROW2);
        hex2char(p, row);
        DrawString(pos._x, pos._y, row, props);
        pos._y += 1;
    }

    SetColor(CD_NORMAL);

    pos = anchor;
    unsigned char *data =
        viewData_->song_->data_ + (SONG_CHANNEL_COUNT * viewData_->songOffset_);
    short dx = 3;
    short dy = 1;
    for (int j = 0; j < View::songRowCount_; j++) {

        pos._x = anchor._x;

        for (int i = 0; i < 8; i++) {

            bool invert = false;

            // see if we need to invert current step
            // if there's a selection or we are at cursor position

            if (clipboard_.active_) {
                if ((i >= selRect.Left()) && (i <= selRect.Right()) &&
                    (j + viewData_->songOffset_ >= selRect.Top()) &&
                    (j + viewData_->songOffset_ <= selRect.Bottom())) {
                    invert = true;
                }
            } else {
                if (i == viewData_->songX_ && j == viewData_->songY_) {
                    invert = true;
                }
            }

            // draw current step

            unsigned char d = *data++;

            if (d == 0xFE) {
                SetColor(CD_SONGVIEWFE);
            } else if (d == 0x00) {
                SetColor(CD_SONGVIEW00);
            } else {
                SetColor(CD_NORMAL);
            }

            if (invert) {
                SetColor(CD_HILITE2);
                props.invert_ = true;
            }

            if (d == 0xFF) {
                DrawString(pos._x, pos._y, "--", props);
            } else {
                hex2char(d, row);
                DrawString(pos._x, pos._y, row, props);
            }

            // Put back drawing state

            if (invert) {
                SetColor(CD_NORMAL);
                props.invert_ = false;
            }

            // Next step

            pos._x += dx;
        }
        pos._y += dy;
    }
    SetColor(CD_NORMAL);

    drawNotes(SONG_GRID_X);

    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    };

    DrawSidePanel();
    DrawHintBar("O set  O+</> edit  R+> chain  L+X mute");
};

/******************************************************
 OnPlayerUpdate:
        Called when positions in player change. Should
        provide visual feedback of currently played
        position
 ******************************************************/

void SongView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {

    Player *player = Player::GetInstance();


    GUIPoint anchor = GetAnchor();
    anchor._x = SONG_GRID_X;
    GUIPoint pos = anchor;
    pos._x -= 1;

    GUITextProperties props;
    SetColor(CD_CURSOR);

    // Loop on all channels

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {

        // Clear all current positions

        int y = lastPlayedPosition_[i] - viewData_->songOffset_;
        if (y >= 0 && y < View::songRowCount_ &&
            viewData_->playMode_ != PM_AUDITION) {
            pos._y = anchor._y + y;
            DrawString(pos._x, pos._y, " ", props);
        }

        // Clear all last queued positions

        y = lastQueuedPosition_[i] - viewData_->songOffset_;
        if (y >= 0 && y < View::songRowCount_) {
            pos._y = anchor._y + y;
            DrawString(pos._x, pos._y, " ", props);
        }

        // For each playing position, draw current location

        if (player->IsChannelPlaying(i)) {
            if (eventType != PET_STOP) {
                if (viewData_->currentPlayChain_[i] != 0xFF) {
                    int y = viewData_->songPlayPos_[i] - viewData_->songOffset_;
                    if (y >= 0 && y < View::songRowCount_) {
                        pos._y = anchor._y + y;
                        if (!player->IsChannelMuted(i)) {
                            SetColor(CD_PLAY);
                            DrawString(pos._x, pos._y, ">", props);
                        } else {
                            SetColor(CD_MUTE);
                            DrawString(pos._x, pos._y, "-", props);
                        }
                        SetColor(CD_CURSOR);
                        lastPlayedPosition_[i] = viewData_->songPlayPos_[i];
                    }
                }
            }
        }

        // If in live mode, update queued position

        if (player->GetSequencerMode() == SM_LIVE) {
            if (player->GetQueueingMode(i) != QM_NONE) {

                if (eventType != PET_STOP) {
                    int y =
                        player->GetQueuePosition(i) - viewData_->songOffset_;
                    if (y >= 0 && y < View::songRowCount_) {
                        pos._y = anchor._y + y;
                        char *indicator = player->GetLiveIndicator(i);
                        DrawString(pos._x, pos._y, indicator, props);
                        lastQueuedPosition_[i] = player->GetQueuePosition(i);
                    }
                }
            };
        }
        pos._x += 3;
    }

    SetColor(CD_NORMAL);

    // clip / dsp / battery / play time live on the side panel + strip

    drawNotes(SONG_GRID_X);
};

void SongView::nudgeTempo(int direction) {
    ApplicationCommandDispatcher *dispatcher =
        ApplicationCommandDispatcher::GetInstance();
    switch (direction) {
    case -1:
        dispatcher->OnNudgeDown();
        break;
    case 1:
        dispatcher->OnNudgeUp();
        break;
    }
}
