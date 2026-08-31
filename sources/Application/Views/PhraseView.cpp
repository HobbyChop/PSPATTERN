#include "PhraseView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Model/Scale.h"
#include "Application/Model/Table.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/CommandSelectorCommon.h"
#include "Application/Views/ModalDialogs/CommandSelectorModal.h"
#include "System/Console/Trace.h"
#include "UIController.h"
#include <stdlib.h>
#include <string.h>

short PhraseView::offsets_[2][4] = {-1, 1, 12, -12, -1, 1, 16, -16};

static void CommandSelectorCallback(View &v, ModalView &d) {
    ((PhraseView &)v).onCommandSelectorResult(d);
}

static void CommandSelectorPreviewCallback(View &v, ModalView &d) {
    ((PhraseView &)v).onCommandSelectorPreview(d);
}

PhraseView::PhraseView(GUIWindow &w, ViewData *viewData)
    : View(w, viewData), cmdEdit_("edit", FCC_EDIT, 0) {
    phrase_ = viewData_->song_->phrase_;
    lastPlayingPos_ = 0;
    GUIPoint pos(0, 10);
    cmdEditField_ =
        new UIBigHexVarField(pos, cmdEdit_, 4, "%4.4X", 0, 0xFFFF, 16, true);
    row_ = 0;
    viewData->phraseCurPos_ = 0;
    col_ = 0;
    lastNote_ = 60;
    lastInstr_ = 0;
    lastCmd_ = I_CMD_NONE;
    lastParam_ = 0;
    commandSelectorModalActive_ = false;

    clipboard_.active_ = false;
    clipboard_.width_ = 0;
    clipboard_.height_ = 0;

    for (int i = 0; i < 16; i++) {
        clipboard_.note_[i] = 0xFF;
        clipboard_.instr_[i] = 0;
    };
    View::EnableNotification();
}

PhraseView::~PhraseView() { delete cmdEditField_; };

void PhraseView::updateCursor(int dx, int dy) {

    col_ += dx;
    row_ += dy;
    if (col_ > 6)
        col_ = 6;
    if (col_ < 0)
        col_ = 0;
    if (row_ > 15) {
        // Try to see if the current chain has a phrase after this one

        if ((viewMode_ != VM_SELECTION) && (viewData_->chainRow_ < 15)) {
            viewData_->chainRow_++;
            unsigned char *p = viewData_->GetCurrentChainPointer();
            if (*p != 0xFF) {
                viewData_->currentPhrase_ = *p;
                row_ = 0;
            } else { // rollback
                viewData_->chainRow_--;
                row_ = 15;
            }
        } else {
            row_ = 15;
        }
    }
    if (row_ < 0) {

        // Try to see if the current chain has a phrase before this one

        if ((viewMode_ != VM_SELECTION) && (viewData_->chainRow_ > 0)) {
            viewData_->chainRow_--;
            unsigned char *p = viewData_->GetCurrentChainPointer();
            if (*p != 0xFF) {
                viewData_->currentPhrase_ = *p;
                row_ = 15;
            } else { // rollback
                viewData_->chainRow_++;
                row_ = 0;
            }
        } else {
            row_ = 0;
        }
    }
    GUIPoint anchor = gridAnchor();
    GUIPoint p(anchor);
    switch (col_) {
    case 4:
        p._x += 16;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(
            *(phrase_->param1_ + (16 * viewData_->currentPhrase_ + row_)));
        break;
    case 6:
        p._x += 26;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(
            *(phrase_->param2_ + (16 * viewData_->currentPhrase_ + row_)));
        break;
    };
    viewData_->phraseCurPos_ = row_;
    isDirty_ = true;
}

void PhraseView::stopAudition() {
    Player *player = Player::GetInstance();
    if (viewData_->playMode_ == PM_AUDITION)
        player->Stop();
}

bool PhraseView::isCommandColumn() const { return col_ == 3 || col_ == 5; }

FourCC *PhraseView::getCurrentCommandPointer() {
    return CommandSelectorCommon::getCommandPointerByCol(
        col_, 2, phrase_->cmd1_ + (16 * viewData_->currentPhrase_ + row_), 4,
        phrase_->cmd2_ + (16 * viewData_->currentPhrase_ + row_));
}


/* Which instrument type this channel is pointed at, as a CMD_ON_ mask.
   The picker uses it to show what a command can actually reach. */
static int channelApplyMask(ViewData *viewData,int instrument) {
    if ((!viewData)||(!viewData->project_)) return CMD_ON_ALL ;
    InstrumentBank *bank=viewData->project_->GetInstrumentBank() ;
    if (!bank) return CMD_ON_ALL ;
    if ((instrument<0)||(instrument>=MAX_INSTRUMENT_COUNT)) return CMD_ON_ALL ;
    I_Instrument *instr=bank->GetInstrument(instrument) ;
    if (!instr) return CMD_ON_ALL ;
    switch (instr->GetType()) {
        case IT_SAMPLE: return CMD_ON_SAMPLE ;
        case IT_SYNTH:  return CMD_ON_SYNTH ;
        case IT_MIDI:   return CMD_ON_MIDI ;
        default: break ;
    }
    return CMD_ON_ALL ;
}

void PhraseView::enterCommandSelector() {
    FourCC *cmdPtr = getCurrentCommandPointer();
    if (!cmdPtr) return;
    commandSelectorModalActive_ = true;
    // the instrument written on this row, or failing that whatever the
    // channel last played
    int instr=*(phrase_->instr_+(16*viewData_->currentPhrase_+row_)) ;
    if (instr==0xFF) instr=viewData_->currentInstrument_ ;
    DoModal(new CommandSelectorModal(*this, cmdPtr, CommandSelectorPreviewCallback,
                                     channelApplyMask(viewData_,instr)),
            CommandSelectorCallback);
}

void PhraseView::onCommandSelectorResult(ModalView &d) {
    commandSelectorModalActive_ = false;
    CommandSelectorModal &modal = (CommandSelectorModal &)d;
    if (modal.GetReturnCode() == 1) {
        FourCC *cmd = getCurrentCommandPointer();
        if (cmd) {
            lastCmd_ = *cmd;
        }
    }
    isDirty_ = true;
}

void PhraseView::onCommandSelectorPreview(ModalView &) {
    isDirty_ = true;
    Player *player = Player::GetInstance();
    // Don't audition when in playback, allow when browsing around
    if (!player->IsRunning() || viewData_->playMode_ == PM_AUDITION) {
        player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                              viewData_->chainRow_);
    }
}

void PhraseView::updateCursorValue(ViewUpdateDirection direction, int xOffset,
                                   int yOffset) {

    unsigned char *c = 0;
    unsigned char limit = 0;
    bool wrap = false;
    FourCC *cc;

    switch (col_ + xOffset) {
    case 0:
        c = phrase_->note_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        limit = 119;
        wrap = true;
        break;
    case 1:
        c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        limit = MAX_INSTRUMENT_COUNT - 1;
        wrap = true;
        break;
    case 2: {
        // Velocity reads 00..99. Up/down move by ten, left/right by
        // one, the same coarse/fine split the parameter columns use.
        c = phrase_->velocity_ +
            (16 * viewData_->currentPhrase_ + row_ + yOffset);
        bool coarse = (direction == VUD_UP || direction == VUD_DOWN);
        int dir = (direction == VUD_UP || direction == VUD_RIGHT) ? 1 : -1;
        if (*c == VELOCITY_EMPTY) {
            // First touch on an empty step writes the velocity it was
            // already sounding at, so the number that appears is the
            // one you were hearing rather than a jump to silence.
            *c = VELOCITY_FULL;
            if (dir > 0) {
                isDirty_ = true;
                return;
            }
        }
        int v = *c + dir * (coarse ? 10 : 1);
        if (v > VELOCITY_FULL)
            v = VELOCITY_FULL;
        if (v < 0) {
            // One step below 00 clears the step back to empty, which
            // is how you take a dynamic back off. A coarse step stops
            // at 00 instead: clearing should be a deliberate press,
            // not something that falls out of holding a direction.
            if (coarse) {
                v = 0;
            } else {
                *c = VELOCITY_EMPTY;
                isDirty_ = true;
                return;
            }
        }
        *c = (uchar)v;
        isDirty_ = true;
        return;
    }
    case 3:
        cc = phrase_->cmd1_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        switch (direction) {
        case VUD_RIGHT:
            *cc = CommandList::GetNext(*cc);
            break;
        case VUD_UP:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case VUD_LEFT:
            *cc = CommandList::GetPrev(*cc);
            break;
        case VUD_DOWN:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;
    case 4:
        switch (direction) {
        case VUD_RIGHT:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case VUD_UP:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case VUD_LEFT:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case VUD_DOWN:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(phrase_->param1_ + (16 * viewData_->currentPhrase_ + row_ +
                              yOffset)) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;
    case 5:
        cc = phrase_->cmd2_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        switch (direction) {
        case VUD_RIGHT:
            *cc = CommandList::GetNext(*cc);
            break;
        case VUD_UP:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case VUD_LEFT:
            *cc = CommandList::GetPrev(*cc);
            break;
        case VUD_DOWN:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;
    case 6:
        switch (direction) {
        case VUD_RIGHT:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case VUD_UP:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case VUD_LEFT:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case VUD_DOWN:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(phrase_->param2_ + (16 * viewData_->currentPhrase_ + row_ +
                              yOffset)) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;
    }
    if ((c) && (*c != 0xFF)) {
        int offset = offsets_[col_ + xOffset][direction];
        // if note column apply the set scale
        if (col_ + xOffset == 0) {
            // Add/remove from offset to match selected scale
            int scale = viewData_->project_->GetScale();
            while (!scaleSteps[scale][(*c + offset) % 12]) {
                offset > 0 ? offset++ : offset--;
            }
        }
        updateData(c, offset, limit, wrap);
        switch (col_ + xOffset) {
        case 0:
            lastNote_ = *c;
            break;
        case 1:
            lastInstr_ = *c;
            break;
        }
    }
    Player *player = Player::GetInstance();
    /* Auto-preview belongs to the columns that ARE the sound: note,
       instrument, velocity. Cycling an effect TYPE retriggered the
       row on every step -- six notes to browse six commands, none of
       them even carrying the effect being chosen, since FX are not
       applied to the preview. Tester call, and the right one. */
    int editedCol = col_ + xOffset;
    if (editedCol >= 0 && editedCol <= 2) {
        if (player->IsRunning()) {
            if ((viewData_->playMode_ == PM_AUDITION)) {
                player->Stop();
                player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                                      viewData_->chainRow_);
            }
        } else {
            player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                                  viewData_->chainRow_);
        }
    }
    isDirty_ = true;
}

// If we're on an empty spot, we past the last element
// otherwise we take the current phrase as last

void PhraseView::pasteLast() {

    uchar *c = 0;
    uint *i = 0;

    switch (col_) {
    case 0:
        c = phrase_->note_ + (16 * viewData_->currentPhrase_ + row_);
        if ((*c == 0xFF)) {
            *c = lastNote_;
            c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
            *c = lastInstr_;
            isDirty_ = true;
        } else {
            lastNote_ = *c;
            c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
            lastInstr_ = *c;
        }
        break;
    case 2:
        c = phrase_->velocity_ + (16 * viewData_->currentPhrase_ + row_);
        if (*c == VELOCITY_EMPTY) {
            *c = lastVelocity_;
            isDirty_ = true;
        } else {
            lastVelocity_ = *c;
        }
        break;
    case 1:
        c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
        if ((*c == 0xFF)) {
            *c = lastInstr_;
            isDirty_ = true;
        } else {
            lastInstr_ = *c;
        }
        break;
    case 3:
        i = phrase_->cmd1_ + (16 * viewData_->currentPhrase_ + row_);
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 4:
        /*			s=phrase_->param1_+(16*viewData_->currentPhrase_+row_) ;
                    if (*s==0) {
                        *s=lastParam_ ;
                        cmdEdit_.SetInt(lastParam_) ;
                        isDirty_=true ;
                    }
        �*/
        break;

    case 5:
        i = phrase_->cmd2_ + (16 * viewData_->currentPhrase_ + row_);
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 6:
        /*			s=phrase_->param2_+(16*viewData_->currentPhrase_+row_) ;
                    if (*s==0) {
                        *s=lastParam_ ;
                        isDirty_=true ;
                        cmdEdit_.SetInt(lastParam_) ;
                    }
        */
        break;
    }
}

void PhraseView::cutPosition() {

    clipboard_.active_ = true;
    clipboard_.row_ = row_;
    clipboard_.col_ = col_;
    saveRow_ = row_;
    saveCol_ = col_;

    if (col_ % 2 == 0)
        col_ += 1; // This way, A+B on note cuts
                   // the instruments too and parameters get cut with commands
    cutSelection();
};

void PhraseView::warpInChain(int offset) {

    int currentRow = viewData_->chainRow_;
    viewData_->chainRow_ += offset;
    if ((viewData_->chainRow_ < 16) && (viewData_->chainRow_ >= 0)) {
        unsigned char *p = viewData_->GetCurrentChainPointer();
        if (*p != 0xFF) {
            viewData_->currentPhrase_ = *p;
            switch (col_) {
            case 4:
                cmdEdit_.SetInt(*(phrase_->param1_ +
                                  (16 * viewData_->currentPhrase_ + row_)));
                break;
            case 6:
                cmdEdit_.SetInt(*(phrase_->param2_ +
                                  (16 * viewData_->currentPhrase_ + row_)));
                break;
            };
        } else { // rollback
            viewData_->chainRow_ = currentRow;
        }
    } else { // rollback
        viewData_->chainRow_ = currentRow;
    }
    isDirty_ = true;
}

void PhraseView::warpToNeighbour(int offset) {
    // save current data
    int saveX = viewData_->songX_;
    int saveOffset = viewData_->songOffset_;
    int newPos = saveX + offset;

    while ((newPos > -1) && (newPos < SONG_CHANNEL_COUNT)) {
        // Go to neighbout song channel
        viewData_->songX_ = newPos;
        unsigned char *c = viewData_->GetCurrentSongPointer();
        // is there a chain ?
        unsigned char oldChain = viewData_->currentChain_;
        if (*c != 0xFF) {
            // go to chain
            viewData_->currentChain_ = *c;
            // get phrase at location
            unsigned char *p = viewData_->GetCurrentChainPointer();
            // is there a phrase ?
            if (*p != 0xFF) {
                viewData_->currentPhrase_ = *p;
                updateCursor(0, 0);
                isDirty_ = true;
                return;
            } else {
                viewData_->currentPhrase_ = 0xFE;
                viewData_->currentChain_ = *c;
                updateCursor(0, 0);
                isDirty_ = true;
                return;
            }
        } else {
            // no chain, to neighbour song channel
            newPos += offset;
        }
    }
    // restore song
    viewData_->songX_ = saveX;
    viewData_->songOffset_ = saveOffset;
}

/******************************************************
 getSelectionRect:
        gets the normalized rectangle of the current
        selection. Valid only while selection is drawn
 ******************************************************/

GUIRect PhraseView::getSelectionRect() {
    GUIRect r(clipboard_.col_, clipboard_.row_, col_, row_);
    r.Normalize();
    return r;
};

/******************************************************
 fillClipboardData:

        copies the necessary information from the
        current selection to the clipboard for future
        paste. We're copying data all across the row
        because we"re too lazy to try to figure a better
        procedure
 ******************************************************/

void PhraseView::fillClipboardData() {

    // Get Current normalized selection rect

    GUIRect selRect = getSelectionRect();

    // Get size & store in clipboard

    clipboard_.width_ = selRect.Width() + 1;
    clipboard_.height_ = selRect.Height() + 1;
    clipboard_.row_ = selRect.Top();
    clipboard_.col_ = selRect.Left();

    // Copy the data

    uchar *src1 =
        viewData_->song_->phrase_->note_ + 16 * viewData_->currentPhrase_;
    uchar *dst1 = clipboard_.note_;
    uchar *src2 =
        viewData_->song_->phrase_->instr_ + 16 * viewData_->currentPhrase_;
    uchar *dst2 = clipboard_.instr_;
    uchar *srcV =
        viewData_->song_->phrase_->velocity_ + 16 * viewData_->currentPhrase_;
    uchar *dstV = clipboard_.velocity_;
    uint *src3 =
        viewData_->song_->phrase_->cmd1_ + 16 * viewData_->currentPhrase_;
    uint *dst3 = clipboard_.cmd1_;
    ushort *src4 =
        viewData_->song_->phrase_->param1_ + 16 * viewData_->currentPhrase_;
    ushort *dst4 = clipboard_.param1_;
    uint *src5 =
        viewData_->song_->phrase_->cmd2_ + 16 * viewData_->currentPhrase_;
    uint *dst5 = clipboard_.cmd2_;
    ushort *src6 =
        viewData_->song_->phrase_->param2_ + 16 * viewData_->currentPhrase_;
    ushort *dst6 = clipboard_.param2_;

    for (int i = 0; i < clipboard_.height_; i++) {
        dst1[i] = src1[clipboard_.row_ + i];
        dst2[i] = src2[clipboard_.row_ + i];
        dstV[i] = srcV[clipboard_.row_ + i];
        dst3[i] = src3[clipboard_.row_ + i];
        dst4[i] = src4[clipboard_.row_ + i];
        dst5[i] = src5[clipboard_.row_ + i];
        dst6[i] = src6[clipboard_.row_ + i];
    };
    updateCursor(0, 0);
};

void PhraseView::updateSelectionValue(ViewUpdateDirection direction) { // HERE

    saveRow_ = row_;
    saveCol_ = col_;

    GUIRect r = getSelectionRect();
    col_ = r.Left();
    row_ = r.Top();

    for (int i = 0; i <= r.Width(); i++) {
        for (int j = 0; j <= r.Height(); j++) {
            if (col_ + i < 2) {
                updateCursorValue(direction, i, j);
            }
        }
    }
    row_ = saveRow_;
    col_ = saveCol_;
}

void PhraseView::extendSelection() {
    GUIRect rect = getSelectionRect();
    if (rect.Left() > 0 || rect.Right() < 6) {
        if (col_ < clipboard_.col_) {
            col_ = 0;
            clipboard_.col_ = 6;
        } else {
            col_ = 6;
            clipboard_.col_ = 0;
        }
        isDirty_ = true;
    } else {
        if (row_ < clipboard_.row_) {
            row_ = 0;
            clipboard_.row_ = 15;
        } else {
            clipboard_.row_ = 0;
            row_ = 15;
        }
        isDirty_ = true;
    }
}

/******************************************************
 interpolateSelection:
        expands the lowest value of selection to the highest
 ******************************************************/
void PhraseView::interpolateSelection() {
    if (!clipboard_.active_) {
        return;
    }

    GUIRect rect = getSelectionRect();
    // Only interpolate if we're in note (0) or param (3, 5) columns
    int col = rect.Left();
    if (col != rect.Right() || (col != 0 && col != 3 && col != 5)) {
        return;
    }

    int startRow = rect.Top();
    int endRow = rect.Bottom();
    // Need at least 2 rows to interpolate
    if (endRow - startRow < 1) {
        return;
    }

    // Select the appropriate data array based on column
    if (col == 0) {
        // Note column
        uchar *noteData = phrase_->note_ + (16 * viewData_->currentPhrase_);

        uchar startNote = noteData[startRow];
        uchar endNote = noteData[endRow];

        if (startNote == 0xFF || endNote == 0xFF) {
            View::SetNotification("No note info");
            return;
        }

        int numSteps = endRow - startRow;
        int noteDiff = (int)endNote - (int)startNote;

        for (int step = 0; step <= numSteps; step++) {
            int row = startRow + step;
            int value = startNote + (noteDiff * step) / (numSteps);
            noteData[row] = (uchar)value;
        }
    } else {
        // Parameter columns (3 or 5)
        ushort *paramData = (col == 3) ? 
            phrase_->param1_ + (16 * viewData_->currentPhrase_) :
            phrase_->param2_ + (16 * viewData_->currentPhrase_);

        ushort startParam = paramData[startRow];
        ushort endParam = paramData[endRow];

        int numSteps = endRow - startRow;
        int paramDiff = (int)endParam - (int)startParam;

        for (int step = 0; step <= numSteps; step++) {
            int row = startRow + step;
            int value = startParam + (paramDiff * step) / (numSteps);
            paramData[row] = (ushort)value;
        }
    }
    isDirty_ = true;
}

/******************************************************
 copySelection:
        copies data in the current selection to the
        clipboard & end selection process
 ******************************************************/

void PhraseView::copySelection() {

    // Keep up with row,col of selection coz
    // fillClipboardData will trash it

    fillClipboardData();

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    row_ = saveRow_;
    col_ = saveCol_;

    View::SetNotification("Copied selection");
};

/******************************************************
 cut:  copies data in the current selection to the
       clipboard, clear selection content & end selection
       process
 ******************************************************/

void PhraseView::cutSelection() {

    // Keep up with row,col of selection coz
    // fillClipboardData will trash it

    fillClipboardData();

    // Loop over selection col, row & clear data inside it

    uchar *dst1 =
        viewData_->song_->phrase_->note_ + 16 * viewData_->currentPhrase_;
    uchar *dst2 =
        viewData_->song_->phrase_->instr_ + 16 * viewData_->currentPhrase_;
    uchar *dstV =
        viewData_->song_->phrase_->velocity_ + 16 * viewData_->currentPhrase_;
    uint *dst3 =
        viewData_->song_->phrase_->cmd1_ + 16 * viewData_->currentPhrase_;
    ushort *dst4 =
        viewData_->song_->phrase_->param1_ + 16 * viewData_->currentPhrase_;
    uint *dst5 =
        viewData_->song_->phrase_->cmd2_ + 16 * viewData_->currentPhrase_;
    ushort *dst6 =
        viewData_->song_->phrase_->param2_ + 16 * viewData_->currentPhrase_;

    for (int i = 0; i < clipboard_.width_; i++) {
        for (int j = 0; j < clipboard_.height_; j++) {
            switch (i + clipboard_.col_) {
            case 0:
                dst1[j + clipboard_.row_] = 0xFF;
                break;
            case 1:
                dst2[j + clipboard_.row_] = 0xFF;
                break;
            case 2:
                dstV[j + clipboard_.row_] = VELOCITY_EMPTY;
                break;
            case 3:
                dst3[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 4:
                dst4[j + clipboard_.row_] = 0x0000;
                break;
            case 5:
                dst5[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 6:
                dst6[j + clipboard_.row_] = 0x0000;
                break;
            }
        }
    }

    // Clear selection, end selection process & reposition cursor

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    row_ = saveRow_;
    col_ = saveCol_;
    updateCursor(0, 0);
    isDirty_ = true;
};

/******************************************************
 pasteClipboard:
        copies data in the clipboard to the current step
 ******************************************************/

void PhraseView::pasteClipboard() {

    // Get number of row to paste

    int height = clipboard_.height_;
    /*    if (row_+height>16) {
            height=16-row_ ;
        }
      */
    uchar *dst1 =
        viewData_->song_->phrase_->note_ + 16 * viewData_->currentPhrase_;
    uchar *src1 = clipboard_.note_;
    uchar *dst2 =
        viewData_->song_->phrase_->instr_ + 16 * viewData_->currentPhrase_;
    uchar *src2 = clipboard_.instr_;
    uchar *dstV =
        viewData_->song_->phrase_->velocity_ + 16 * viewData_->currentPhrase_;
    uchar *srcV = clipboard_.velocity_;
    uint *dst3 =
        viewData_->song_->phrase_->cmd1_ + 16 * viewData_->currentPhrase_;
    uint *src3 = clipboard_.cmd1_;
    ushort *dst4 =
        viewData_->song_->phrase_->param1_ + 16 * viewData_->currentPhrase_;
    ushort *src4 = clipboard_.param1_;
    uint *dst5 =
        viewData_->song_->phrase_->cmd2_ + 16 * viewData_->currentPhrase_;
    uint *src5 = clipboard_.cmd2_;
    ushort *dst6 =
        viewData_->song_->phrase_->param2_ + 16 * viewData_->currentPhrase_;
    ushort *src6 = clipboard_.param2_;

    uint *noCmd = (uint *)-1;
    ushort *noPrm = (ushort *)-1;
    // Indexed BY COLUMN, so inserting velocity at 2 pushes every
    // command and parameter one slot right. Getting this wrong pastes
    // a command into a parameter, silently.
    uint *srcCmd[7] = {noCmd, noCmd, noCmd, src3, noCmd, src5, noCmd};
    ushort *srcPrm[7] = {noPrm, noPrm, noPrm, noPrm, src4, noPrm, src6};
    uint *dstCmd[7] = {noCmd, noCmd, noCmd, dst3, noCmd, dst5, noCmd};
    ushort *dstPrm[7] = {noPrm, noPrm, noPrm, noPrm, dst4, noPrm, dst6};

    bool wasUpdated = false;

    for (int i = 0; i < clipboard_.width_; i++) {
        for (int j = 0; j < height; j++) {
            switch (i + clipboard_.col_) {
            case 0:
                dst1[(j + row_) % 16] = src1[j];
                wasUpdated = true;
                break;
            case 1:
                dst2[(j + row_) % 16] = src2[j];
                wasUpdated = true;
                break;
            case 2:
                dstV[(j + row_) % 16] = srcV[j];
                wasUpdated = true;
                break;
            case 3:
            case 5:
                if ((col_ + i) == 3 ||
                    (col_ + i) == 5) { // Don't allow commands in notes, etc
                    dstCmd[col_ + i][(row_ + j) % 16] =
                        srcCmd[clipboard_.col_ + i][j];
                    wasUpdated = true;
                }
                break;
            case 4:
            case 6:
                if ((col_ + i) == 4 || (col_ + i) == 6) {
                    dstPrm[col_ + i][(row_ + j) % 16] =
                        srcPrm[clipboard_.col_ + i][j];
                    wasUpdated = true;
                }
                break;
            }
        }
    }
    if (wasUpdated) {
        updateCursor(0x00, ((row_ + height) % 16 - row_));
        isDirty_ = true;
    }
};

void PhraseView::unMuteAll() {

    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
};

void PhraseView::toggleMute() {

    UIController *controller = UIController::GetInstance();
    controller->ToggleMute(viewData_->songX_, viewData_->songX_);
    viewMode_ = (viewMode_ != VM_MUTEON) ? VM_MUTEON : VM_NORMAL;
};

void PhraseView::switchSoloMode() {

    UIController *controller = UIController::GetInstance();
    controller->SwitchSoloMode(viewData_->songX_, viewData_->songX_,
                               (viewMode_ == VM_NORMAL));
    viewMode_ = (viewMode_ != VM_SOLOON) ? VM_SOLOON : VM_NORMAL;
    isDirty_ = true;
};

void PhraseView::OnFocus() {
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    updateCursor(0, 0);
};

void PhraseView::ProcessButtonMask(unsigned short mask, bool pressed) {

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

            // If note or I, we request a new instr

            if (col_ < 2) {
                // note/instrument columns no longer arm VM_NEW at
                // all; nothing to do if we somehow arrive here
                mask &= (0xFFFF - EPBM_A);
            } else {
                if ((col_ == 4) &&
                    (*(phrase_->cmd1_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    unsigned short next = th->GetNext();
                    if (next != NO_MORE_TABLE) {
                        ushort *c = phrase_->param1_ +
                                    (16 * viewData_->currentPhrase_ + row_);
                        *c = next;
                        isDirty_ = true;
                        mask &= (0xFFFF - EPBM_A);
                        cmdEdit_.SetInt(next);
                    }
                }
                if ((col_ == 6) &&
                    (*(phrase_->cmd2_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    unsigned short next = th->GetNext();
                    if (next != NO_MORE_TABLE) {
                        ushort *c = phrase_->param2_ +
                                    (16 * viewData_->currentPhrase_ + row_);
                        *c = next;
                        isDirty_ = true;
                        mask &= (0xFFFF - EPBM_A);
                        cmdEdit_.SetInt(next);
                    }
                }
            };
        }
    }

    if (viewMode_ == VM_CLONE) {
        if ((mask & EPBM_A) && (mask & EPBM_R)) {
            if (col_ < 2) {
                InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
                unsigned char *c =
                    phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
                if (*c != 0xFF) {
                    unsigned short next = bank->Clone(*c);
                    if (next != NO_MORE_INSTRUMENT) {
                        *c = (unsigned char)next;
                        lastInstr_ = next;
                        isDirty_ = true;
                    } else {
                        View::SetNotification("No more instruments");
                    }
                }
            } else {
                if ((col_ == 4) &&
                    (*(phrase_->cmd1_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    int current = *(phrase_->param1_ +
                                    (16 * viewData_->currentPhrase_ + row_));
                    if (current != -1) {
                        unsigned short next = th->Clone(current);
                        if (next != NO_MORE_TABLE) {
                            ushort *c = phrase_->param1_ +
                                        (16 * viewData_->currentPhrase_ + row_);
                            *c = next;
                            isDirty_ = true;
                            cmdEdit_.SetInt(next);
                        } else {
                            View::SetNotification("No more tables");
                        }
                    }
                }
                if ((col_ == 6) &&
                    (*(phrase_->cmd2_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    unsigned short next =
                        th->Clone(*(phrase_->param2_ +
                                    (16 * viewData_->currentPhrase_ + row_)));
                    if (next != NO_MORE_TABLE) {
                        ushort *c = phrase_->param2_ +
                                    (16 * viewData_->currentPhrase_ + row_);
                        *c = next;
                        isDirty_ = true;
                        cmdEdit_.SetInt(next);
                    } else {
                        View::SetNotification("No more tables");
                    }
                }
            };
            mask &= (0xFFFF - (EPBM_A | EPBM_R));
        } else {
            viewMode_ = VM_SELECTION;
        }
    };

    if (viewMode_ == VM_SELECTION) {
        if (!clipboard_.active_) {
            clipboard_.active_ = true;
            clipboard_.col_ = col_;
            clipboard_.row_ = row_;
            saveCol_ = col_;
            saveRow_ = row_;
        }
        processSelectionButtonMask(mask);
    } else {
        viewMode_ = VM_NORMAL;
        processNormalButtonMask(mask);
    };
}

void PhraseView::processNormalButtonMask(unsigned short mask) {
    // Stop audition when pressing any button except A
    if (!(mask & EPBM_A)) {
        stopAudition();
    }
    // B Modifier

    Player *player = Player::GetInstance();

    if (mask & EPBM_B) {
        if (mask & EPBM_LEFT)
            warpToNeighbour(-1);
        if (mask & EPBM_RIGHT)
            warpToNeighbour(1);
        if (mask & EPBM_UP)
            warpInChain(-1);
        if (mask & EPBM_DOWN)
            warpInChain(1);
        if (mask & EPBM_A) {
            cutPosition();
        }
        if (mask & EPBM_R) {
            viewMode_ = VM_CLONE;
        };
        if (mask & EPBM_L)
            toggleMute();
    } else {

        // A Modifer

        if (mask & EPBM_A) {
            if (col_ == 0) { // Preview when pressing A: the note column
                // is the sound; the instrument column is plumbing, and
                // playing the row on every O there was asked to stop
                Player *player = Player::GetInstance();
                if (!player->IsRunning()) {
                    player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                                          viewData_->chainRow_);
                }
            }

            if (mask & EPBM_DOWN) {
                if (isCommandColumn())
                    enterCommandSelector();
                else
                    updateCursorValue(VUD_DOWN);
            }
            if (mask & EPBM_UP) {
                if (isCommandColumn())
                    enterCommandSelector();
                else
                    updateCursorValue(VUD_UP);
            }
            if (mask & EPBM_LEFT)
                updateCursorValue(VUD_LEFT);
            if (mask & EPBM_RIGHT)
                updateCursorValue(VUD_RIGHT);
            if (mask & EPBM_R)
                pasteClipboard();
            if (mask & EPBM_L)
                switchSoloMode();
            if (mask == EPBM_A) {
                /* O on the instrument column is just SET: empty cell
                   takes the last instrument, full cell becomes the
                   last. The second-press new-instrument gesture is
                   gone from here -- new instruments are made on the
                   instrument screen. The arming stays only for the
                   TBL command params, where second-O allocating a
                   fresh table is still the quickest way to one. */
                pasteLast();
                if ((col_ == 4) || (col_ == 6))
                    viewMode_ = VM_NEW;
            }

        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_CHAIN;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_RIGHT) {
                    unsigned char *c = phrase_->instr_ +
                                       (16 * viewData_->currentPhrase_ + row_);
                    if (*c != 0xFF) {
                        viewData_->currentInstrument_ = *c;
                    } else {
                        int nearest = findClosestInstrumentFor(row_);
                        if (nearest >= 0) {
                            viewData_->currentInstrument_ = nearest;
                        } else viewData_->currentInstrument_= lastInstr_;
                    }
                    if (viewData_->currentInstrument_ != 0xFF) {
                        ViewType vt = VT_INSTRUMENT;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        SetChanged();
                        NotifyObservers(&ve);
                    }
                }
                if (mask & EPBM_DOWN) {

                    // Go to table view

                    ViewType vt = VT_TABLE;

                    FourCC *cmd = phrase_->cmd1_ +
                                  (16 * viewData_->currentPhrase_ + row_);
                    ushort *param = phrase_->param1_ +
                                    (16 * viewData_->currentPhrase_ + row_);

                    if (*cmd != I_CMD_TABL) {
                        cmd = phrase_->cmd2_ +
                              (16 * viewData_->currentPhrase_ + row_);
                        param = phrase_->param2_ +
                                (16 * viewData_->currentPhrase_ + row_);
                    }
                    if (*cmd == I_CMD_TABL) {
                        viewData_->currentTable_ = (*param) & 0x7F;
                    }
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_UP) {

                    // Go to groove view

                    ViewType vt = VT_GROOVE;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
                if (mask & EPBM_L)
                    unMuteAll();

            } else {
                // L Modifier
                if (mask & EPBM_L) {

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
                        player->OnStartButton(PM_PHRASE, viewData_->songX_,
                                              false, viewData_->chainRow_);
                    }
                }
            }
        }
    }
};

/*
 * For currently selected row, find nearest instrument from the top
 */
int PhraseView::findClosestInstrumentFor(int row) {
    unsigned char *instr = phrase_->instr_ + (16 * viewData_->currentPhrase_);
    if (instr[row] != 0xFF) return instr[row];
    for (int d = 1; d < 16; ++d) {
        int up = row - d;
        int down = row + d;
        if (up >= 0 && instr[up] != 0xFF) return instr[up];
        if (down < 16 && instr[down] != 0xFF) return instr[down];
    }
    return -1; // none found in phrase
}

void PhraseView::processSelectionButtonMask(unsigned short mask) {

    Player *player = Player::GetInstance();

    // B modifier

    if (mask & EPBM_B) {
        if (mask & EPBM_R) {
            extendSelection();
        } else if (mask & EPBM_L) {
            interpolateSelection();
        } else {
            copySelection();
        }
    } else {

        // A Modifer

        if (mask & EPBM_A) {

            if (mask & EPBM_DOWN)
                updateSelectionValue(VUD_DOWN);
            if (mask & EPBM_UP)
                updateSelectionValue(VUD_UP);
            if (mask & EPBM_LEFT)
                updateSelectionValue(VUD_LEFT);
            if (mask & EPBM_RIGHT)
                updateSelectionValue(VUD_RIGHT);

            if (mask & EPBM_R)
                cutSelection();
            if (mask & EPBM_L)
                switchSoloMode();
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_CHAIN;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_RIGHT) {
                    unsigned char *c = phrase_->instr_ +
                                       (16 * viewData_->currentPhrase_ + row_);
                    if (*c != 0xFF) {
                        viewData_->currentInstrument_ = *c;
                    } else {
                        int nearest = findClosestInstrumentFor(row_);
                        if (nearest >= 0) {
                            viewData_->currentInstrument_ = nearest;
                        } else viewData_->currentInstrument_= lastInstr_;
                    }
                    ViewType vt = VT_INSTRUMENT;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
                if (mask & EPBM_L)
                    unMuteAll();

            } else {
                // L Modifier
                if (mask & EPBM_L) {

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
                        player->OnStartButton(PM_PHRASE, viewData_->songX_,
                                              false, viewData_->chainRow_);
                    }
                }
            }
        }
    }
};

void PhraseView::setTextProps(GUITextProperties &props, int row, int col,
                              bool restore) {

    bool invert = false;

    if (clipboard_.active_) {
        GUIRect selRect = getSelectionRect();
        if ((row >= selRect.Left()) && (row <= selRect.Right()) &&
            (col >= selRect.Top()) && (col <= selRect.Bottom())) {
            invert = true;
        }
    } else {
        if ((col_ == row) && (row_ == col)) {
            invert = true;
        }
    }

    if (invert) {
        if (restore) {
            SetColor(CD_NORMAL);
            props.invert_ = false;
        } else {
            SetColor(CD_HILITE2);
            props.invert_ = true;
        }
    }
};

/* One phrase: notes, instruments and both command/parameter pairs. */
#define PHRASE_UNDO_BYTES (16*(1+1+sizeof(FourCC)+sizeof(ushort)+sizeof(FourCC)+sizeof(ushort)))
int PhraseView::UndoSize() { return PHRASE_UNDO_BYTES ; }
int PhraseView::UndoContext() { return viewData_->currentPhrase_ ; }
void PhraseView::UndoCapture(unsigned char *dst) {
	int base=16*viewData_->currentPhrase_ ;
	Phrase *p=viewData_->song_->phrase_ ;
	unsigned char *d=dst ;
	memcpy(d,p->note_+base,16) ;                     d+=16 ;
	memcpy(d,p->instr_+base,16) ;                    d+=16 ;
	memcpy(d,p->cmd1_+base,16*sizeof(FourCC)) ;      d+=16*sizeof(FourCC) ;
	memcpy(d,p->param1_+base,16*sizeof(ushort)) ;    d+=16*sizeof(ushort) ;
	memcpy(d,p->cmd2_+base,16*sizeof(FourCC)) ;      d+=16*sizeof(FourCC) ;
	memcpy(d,p->param2_+base,16*sizeof(ushort)) ;
}
void PhraseView::UndoRestore(int context,const unsigned char *src) {
	viewData_->currentPhrase_=context ;
	int base=16*context ;
	Phrase *p=viewData_->song_->phrase_ ;
	const unsigned char *d=src ;
	memcpy(p->note_+base,d,16) ;                     d+=16 ;
	memcpy(p->instr_+base,d,16) ;                    d+=16 ;
	memcpy(p->cmd1_+base,d,16*sizeof(FourCC)) ;      d+=16*sizeof(FourCC) ;
	memcpy(p->param1_+base,d,16*sizeof(ushort)) ;    d+=16*sizeof(ushort) ;
	memcpy(p->cmd2_+base,d,16*sizeof(FourCC)) ;      d+=16*sizeof(FourCC) ;
	memcpy(p->param2_+base,d,16*sizeof(ushort)) ;
}

void PhraseView::DrawView() {

    Clear();
    View::EnableNotification();

    GUITextProperties props;
    GUIPoint pos;
    char title[24];
    sprintf(title, "PHRASE %2.2X", (int)viewData_->currentPhrase_);
    DrawTitleStrip(title, "");
    {
        AppWindow &app = (AppWindow &)w_;
        GUIPoint anchor = gridAnchor();
        // Row numbers sit at anchor-3 and the last parameter column
        // ends at anchor+29, so the frame has to span 32 cells plus a
        // little air. At -14/220 it cut through the second digit of
        // every row number on one side and the last digit of every
        // parameter on the other.
        app.OpFrame(anchor._x * 8 - 30, anchor._y * 8 - 6, 274,
                    16 * 8 + 12, CD_ROW);
    }

    drawTriggerTrail();

    // Compute song grid location

    GUIPoint anchor = gridAnchor();

    // Display row numbers

    char buffer[6];
    pos = anchor;
    pos._x -= 3;
    for (int j = 0; j < 16; j++) {
        ((j / altRowNumber_) % 2) ? SetColor(CD_ROW) : SetColor(CD_ROW2);
        hex2char(j, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        pos._y++;
    }

    SetColor(CD_NORMAL);

    pos = anchor;

    // Display notes

    unsigned char *data = phrase_->note_ + (16 * viewData_->currentPhrase_);

    buffer[4] = 0;
    for (int j = 0; j < 16; j++) {
        unsigned char d = *data++;
        setTextProps(props, 0, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        if (d == 0xFF) {
            DrawString(pos._x, pos._y, "----", props);
        } else {
            note2char(d, buffer);
            DrawString(pos._x, pos._y, buffer, props);
        }
        setTextProps(props, 0, j, true);
        pos._y++;
    }

    // Draw instruments

    pos = anchor;
    pos._x += 4;

    data = phrase_->instr_ + (16 * viewData_->currentPhrase_);
    buffer[0] = 'I';
    buffer[3] = 0;

    for (int j = 0; j < 16; j++) {
        unsigned char d = *data++;
        setTextProps(props, 1, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        if (d == 0xFF) {
            SetColor(CD_NORMAL);
            DrawString(pos._x, pos._y, "I", props);
            DrawString(pos._x + 1, pos._y, "--", props);
        } else {
            hex2char(d, buffer + 1);
            DrawString(pos._x, pos._y, buffer, props);
            if (j == row_ && (col_ == 0 || col_ == 1)) {
                SetColor(CD_NORMAL);
                sprintf(buffer, "I%2.2x: ", d);
                std::string instrLine = buffer;
                setTextProps(props, 1, j, true);
                GUIPoint location = GetTitlePosition();
                location._x += 12;
                InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
                I_Instrument *instr = bank->GetInstrument(d);
                instrLine += instr->GetName();
                DrawString(location._x, location._y, instrLine.c_str(), props);
            }
        }
        setTextProps(props, 1, j, true);
        pos._y++;
    }

    // Draw velocity

    // Two digits reading 00..99, and "--" where nothing is written.
    // An empty step is not "zero" -- zero is silence, empty is "play
    // it as loud as the instrument is set", which is what every
    // phrase written before this column existed means.
    pos = anchor;
    pos._x += 8;

    data = phrase_->velocity_ + (16 * viewData_->currentPhrase_);
    buffer[2] = 0;

    for (int j = 0; j < 16; j++) {
        unsigned char d = *data++;
        setTextProps(props, 2, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        if (d == VELOCITY_EMPTY) {
            DrawString(pos._x, pos._y, "--", props);
        } else {
            if (d > VELOCITY_FULL)
                d = VELOCITY_FULL;
            buffer[0] = '0' + (d / 10);
            buffer[1] = '0' + (d % 10);
            DrawString(pos._x, pos._y, buffer, props);
        }
        setTextProps(props, 2, j, true);
        pos._y++;
    }

    // Draw command 1

    pos = anchor;
    pos._x += 11;

    FourCC *f = phrase_->cmd1_ + (16 * viewData_->currentPhrase_);

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        setTextProps(props, 3, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 3, j, true);
        pos._y++;
        // ...or on its parameter. Standing on the parameter is
        // exactly when you need to be told what it means, and
        // the help used to disappear there.
        if (j == row_ && (col_ == 3 || col_ == 4)) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params 1

    pos = anchor;
    pos._x += 16;

    ushort *param = phrase_->param1_ + (16 * viewData_->currentPhrase_);
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 4, j, false);
        /*		if (p==0xFFFF) {
                    DrawString(pos._x,pos._y,"----",props) ;
                } else {
        */ (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                   : SetColor(CD_NORMAL);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        /*		}
         */
        setTextProps(props, 4, j, true);
        pos._y++;
    }

    // Draw commands 2

    pos = anchor;
    pos._x += 21;

    f = phrase_->cmd2_ + (16 * viewData_->currentPhrase_);

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        setTextProps(props, 5, j, false);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 5, j, true);
        pos._y++;
        if (j == row_ && (col_ == 5 || col_ == 6)) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params

    pos = anchor;
    pos._x += 26;

    param = phrase_->param2_ + (16 * viewData_->currentPhrase_);
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 6, j, false);
        /*		if (p==0xFFFF) {
                    DrawString(pos._x,pos._y,"----",props) ;
                } else {
        */ (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                   : SetColor(CD_NORMAL);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        /*		}
         */
        setTextProps(props, 6, j, true);
        pos._y++;
    }

    // (map removed — R+arrows nav lives in the hint bar)
    drawNotes();

    Player *player = Player::GetInstance();
    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    };

    if ((viewMode_ != VM_SELECTION) && ((col_ == 4) || (col_ == 6))) {
        cmdEditField_->SetFocus();
        cmdEditField_->Draw(w_);
    };
    DrawHintBar("O set  O+</> edit  R+v table  R+< chain");
};

void PhraseView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {

    GUITextProperties props;
    drawNotes();
    drawTriggerTrail();

    GUIPoint anchor = gridAnchor();
    GUIPoint pos = anchor;
    pos._x -= 1;

    SetColor(CD_NORMAL);

    pos._y = anchor._y + lastPlayingPos_;
    DrawString(pos._x, pos._y, " ", props);

    Player *player = Player::GetInstance();

    if (eventType != PET_STOP) {

        // Clear current position if needed

        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (player->IsChannelPlaying(i)) {
                if (viewData_->currentPlayPhrase_[i] ==
                        viewData_->currentPhrase_ &&
                    viewData_->playMode_ != PM_AUDITION) {
                    pos._y = anchor._y + viewData_->phrasePlayPos_[i];
                    if (!player->IsChannelMuted(i)) {
                        SetColor(CD_PLAY);
                        DrawString(pos._x, pos._y, ">", props);
                    } else {
                        SetColor(CD_MUTE);
                        DrawString(pos._x, pos._y, "-", props);
                    }
                    SetColor(CD_NORMAL);
                    lastPlayingPos_ = viewData_->phrasePlayPos_[i];
                    break;
                }
            }
        }

        // clear any live indicator
        pos._y = anchor._y;

        // Loop on all channels to see if one has queued current chain
        if (player->GetSequencerMode() == SM_LIVE) {

            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                // is anything queued ?
                if (player->GetQueueingMode(i) != QM_NONE) {
                    // find the chain queued in channel
                    unsigned char songPos = player->GetQueuePosition(i);
                    unsigned char *chain =
                        viewData_->song_->data_ + i + 8 * songPos;
                    if (*chain == viewData_->currentChain_) {
                        char *indicator = player->GetLiveIndicator(i);
                        DrawString(pos._x, pos._y, indicator, props);
                        break;
                    }
                }
            }
        }
    }

    pos = anchor;
    pos._x += 200;

    /*	if (player->Clipped()) {
               w_.DrawString("clip",pos,props);
        } else {
               w_.DrawString("----",pos,props);
        }
    */
};

void PhraseView::printHelpLegend(FourCC command, GUITextProperties props) {
    // the selector popup prints its own legend; two copies of it is
    // just noise
    if (commandSelectorModalActive_) {
        return;
    }
    DrawCommandHelp(command);
    DrawHintBar("O set  O+</> edit  R+v table  R+< chain");
}

/* A trail behind the playhead, on the rows that actually fired.
 *
 * The playhead marker says where the player is. It does not say what
 * just happened, so a phrase full of notes and a phrase full of empty
 * rows look identical while they play, and you have to read the grid
 * to see the rhythm you are listening to.
 *
 * This puts a short bar beside each row that has a note and has just
 * been passed, brightest on the row sounding now and fading over the
 * three behind it. The rhythm becomes visible: gaps stay dark, runs
 * light up in sequence.
 *
 * Only rows with a note glow. A trail that followed the playhead
 * regardless would be a longer playhead, which is not worth any
 * pixels.
 *
 * Drawn rather than written, because a character cell holds a palette
 * index and not a colour -- there is no way to write text a third as
 * bright. OpGlow exists for this.
 *
 * The op for every row is registered every time, including the ones
 * that are dark: intensity 0 paints the background, so a row leaving
 * the trail erases itself without anything having to remember that it
 * used to be lit. setOp replaces by position, so this stays at
 * sixteen ops however long it runs.
 */
void PhraseView::drawTriggerTrail() {

    AppWindow &app = (AppWindow &)w_;
    GUIPoint anchor = gridAnchor();

    Player *player = Player::GetInstance();
    int playRow = -1;
    if (player->IsRunning() && viewData_->playMode_ != PM_AUDITION) {
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (player->IsChannelPlaying(i) &&
                !player->IsChannelMuted(i) &&
                viewData_->currentPlayPhrase_[i] ==
                    viewData_->currentPhrase_) {
                playRow = viewData_->phrasePlayPos_[i];
                break;
            }
        }
    }

    unsigned char *note =
        phrase_->note_ + (16 * viewData_->currentPhrase_);

    for (int j = 0; j < 16; j++) {

        int intensity = 0;
        if (playRow >= 0 && note[j] != 0xFF) {
            // how many steps ago this row went by, wrapping at the
            // top of the phrase
            int age = playRow - j;
            if (age < 0)
                age += 16;
            if (age < 4)
                intensity = 255 >> age;      // 255, 127, 63, 31
        }
        app.OpGlow(anchor._x * 8 - 8, (anchor._y + j) * 8 + 2, 4, 4,
                   intensity);
    }
}

// nav-menu context prep: entering the instrument screen lands on the
// instrument under the cursor (or the nearest one), as the old drill did
void PhraseView::OnNavTo(ViewType to) {
    if (to == VT_TABLE || to == VT_TABLE2) {
        /* If the row under the cursor carries a TBL command, jumping
           to the table screen means THAT table. Either command column
           counts; the first one wins. Otherwise the instrument's own
           table is the next best answer, and unset leaves the screen
           where it was. */
        int base = 16 * viewData_->currentPhrase_ + row_;
        uint c1 = *(phrase_->cmd1_ + base);
        ushort p1 = *(phrase_->param1_ + base);
        uint c2 = *(phrase_->cmd2_ + base);
        ushort p2 = *(phrase_->param2_ + base);
        int t = -1;
        if (c1 == I_CMD_TABL) t = p1 & 0x7F;
        else if (c2 == I_CMD_TABL) t = p2 & 0x7F;
        else {
            /* same resolution playback uses: the instrument on this
               row, else the nearest one above -- phrases name the
               instrument once at the top and leave the column empty
               below, and rows 1..15 deserve the same landing as row 0 */
            unsigned char *ip = phrase_->instr_ + base;
            int which = (*ip != 0xFF) ? *ip : findClosestInstrumentFor(row_);
            if (which >= 0) {
                I_Instrument *instr =
                    viewData_->project_->GetInstrumentBank()->GetInstrument(which);
                if (instr) t = instr->GetTable();
            }
        }
        if (t >= 0 && t < TABLE_COUNT) viewData_->currentTable_ = t;
    }
    if (to == VT_INSTRUMENT) {
        unsigned char *c = phrase_->instr_ +
                           (16 * viewData_->currentPhrase_ + row_);
        if (*c != 0xFF) {
            viewData_->currentInstrument_ = *c;
        } else {
            int nearest = findClosestInstrumentFor(row_);
            if (nearest >= 0) {
                viewData_->currentInstrument_ = nearest;
            } else viewData_->currentInstrument_ = lastInstr_;
        }
    }
}
