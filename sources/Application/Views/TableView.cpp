#include "TableView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/CommandSelectorCommon.h"
#include "Application/Views/ModalDialogs/CommandSelectorModal.h"

#define FCC_EDIT MAKE_FOURCC('T', 'B', 'E', 'D')

static void CommandSelectorCallback(View &v, ModalView &d) {
    ((TableView &)v).onCommandSelectorResult(d);
}

static void CommandSelectorPreviewCallback(View &v, ModalView &d) {
    ((TableView &)v).onCommandSelectorPreview(d);
}

TableView::TableView(GUIWindow &w, ViewData *viewData)
    : View(w, viewData), cmdEdit_("edit", FCC_EDIT, 0) {
    row_ = 0;
    col_ = 0;
    GUIPoint pos(0, 10);
    cmdEditField_ =
        new UIBigHexVarField(pos, cmdEdit_, 4, "%4.4X", 0, 0xFFFF, 16, true);

    lastVol_ = 0;
    lastTick_ = 0;
    lastTsp_ = 0;
    lastCmd_ = I_CMD_NONE;
    lastParam_ = 0;

    clipboard_.active_ = false;
    clipboard_.width_ = 0;
    clipboard_.height_ = 0;
}

TableView::~TableView() {}

void TableView::OnFocus() {
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    lastPosition_[0] = lastPosition_[1] = lastPosition_[2] = 0;
    updateCursor(0, 0);
};

void TableView::cutPosition() {

    clipboard_.active_ = true;
    clipboard_.row_ = row_;
    clipboard_.col_ = col_;
    saveRow_ = row_;
    saveCol_ = col_;

    if ((col_ == 0) || (col_ == 2) || (col_ == 4))
        col_ += 1; // This way, A+B on note cuts
                   // the instruments too and parameters get cut with commands
    cutSelection();
};

GUIRect TableView::getSelectionRect() {
    GUIRect r(clipboard_.col_, clipboard_.row_, col_, row_);
    r.Normalize();
    return r;
};

void TableView::fillClipboardData() {

    // Get Current normalized selection rect

    GUIRect selRect = getSelectionRect();

    // Get size & store in clipboard

    clipboard_.width_ = selRect.Width() + 1;
    clipboard_.height_ = selRect.Height() + 1;
    clipboard_.row_ = selRect.Top();
    clipboard_.col_ = selRect.Left();

    // Copy the data

    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    uint *src1 = table.cmd1_;
    uint *dst1 = clipboard_.cmd1_;
    ushort *src2 = table.param1_;
    ushort *dst2 = clipboard_.param1_;
    uint *src3 = table.cmd2_;
    uint *dst3 = clipboard_.cmd2_;
    ushort *src4 = table.param2_;
    ushort *dst4 = clipboard_.param2_;
    uint *src5 = table.cmd3_;
    uint *dst5 = clipboard_.cmd3_;
    ushort *src6 = table.param3_;
    ushort *dst6 = clipboard_.param3_;

    for (int i = 0; i < clipboard_.height_; i++) {
        dst1[i] = src1[clipboard_.row_ + i];
        dst2[i] = src2[clipboard_.row_ + i];
        dst3[i] = src3[clipboard_.row_ + i];
        dst4[i] = src4[clipboard_.row_ + i];
        dst5[i] = src5[clipboard_.row_ + i];
        dst6[i] = src6[clipboard_.row_ + i];
    };
    updateCursor(0, 0);
};

void TableView::extendSelection() {
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
void TableView::interpolateSelection() {
    if (!clipboard_.active_) {
        return;
    }

    GUIRect rect = getSelectionRect();

    // Only interpolate if we're in param columns (1, 3, 5)
    int col = rect.Left();
    if (col != rect.Right() || (col != 1 && col != 3 && col != 5)) {
        return;
    }

    int startRow = rect.Top();
    int endRow = rect.Bottom();

    // Need at least 2 rows to interpolate
    if (endRow - startRow < 1) {
        return;
    }

    Table &table = TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    ushort *paramData;
    if (col == 1) {
        paramData = table.param1_;
    } else if (col == 3) {
        paramData = table.param2_;
    } else {
        paramData = table.param3_;
    }

    ushort startParam = paramData[startRow];
    ushort endParam = paramData[endRow];

    int numSteps = endRow - startRow;
    int paramDiff = (int)endParam - (int)startParam;

    for (int step = 0; step <= numSteps; step++) {
        int row = startRow + step;
        int value = startParam + (paramDiff * step) / (numSteps);
        paramData[row] = (ushort)value;
    }

    isDirty_ = true;
}

void TableView::copySelection() {

    // Keep up with row,col of selection coz
    // fillClipboardData will trash it

    fillClipboardData();

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    row_ = saveRow_;
    col_ = saveCol_;

    isDirty_ = true;
};

void TableView::cutSelection() {

    // Keep up with row,col of selection coz
    // fillClipboardData will trash it

    fillClipboardData();

    // Loop over selection col, row & clear data inside it

    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);
    uint *dst1 = table.cmd1_;
    ushort *dst2 = table.param1_;
    uint *dst3 = table.cmd2_;
    ushort *dst4 = table.param2_;
    uint *dst5 = table.cmd3_;
    ushort *dst6 = table.param3_;

    for (int i = 0; i < clipboard_.width_; i++) {
        for (int j = 0; j < clipboard_.height_; j++) {
            switch (i + clipboard_.col_) {
            case 0:
                dst1[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 1:
                dst2[j + clipboard_.row_] = 0x0000;
                break;
            case 2:
                dst3[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 3:
                dst4[j + clipboard_.row_] = 0x0000;
                break;
            case 4:
                dst5[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 5:
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

void TableView::pasteClipboard() {

    // Get number of row to paste

    int height = clipboard_.height_;
    /*    if (row_+height>16) {
            height=16-row_ ;
        }
      */
    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    uint *dst1 = table.cmd1_;
    uint *src1 = clipboard_.cmd1_;
    ushort *dst2 = table.param1_;
    ushort *src2 = clipboard_.param1_;
    uint *dst3 = table.cmd2_;
    uint *src3 = clipboard_.cmd2_;
    ushort *dst4 = table.param2_;
    ushort *src4 = clipboard_.param2_;
    uint *dst5 = table.cmd3_;
    uint *src5 = clipboard_.cmd3_;
    ushort *dst6 = table.param3_;
    ushort *src6 = clipboard_.param3_;

    uint *noCmd = (uint *)-1;
    ushort *noPrm = (ushort *)-1;
    uint *srcCmd[5] = {src1, noCmd, src3, noCmd, src5};
    ushort *srcPrm[6] = {noPrm, src2, noPrm, src4, noPrm, src6};
    uint *dstCmd[5] = {dst1, noCmd, dst3, noCmd, dst5};
    ushort *dstPrm[6] = {noPrm, dst2, noPrm, dst4, noPrm, dst6};

    bool wasUpdated = false;

    for (int i = 0; i < clipboard_.width_; i++) {
        for (int j = 0; j < height; j++) {
            switch (i + clipboard_.col_) {
            case 0:
            case 2:
            case 4:
                if ((col_ + i) == 0 || (col_ + i) == 2 ||
                    (col_ + i) == 4) { // Don't allow commands in params, etc
                    dstCmd[col_ + i][(row_ + j) % 16] =
                        srcCmd[clipboard_.col_ + i][j];
                    wasUpdated = true;
                }
                break;
            case 1:
            case 3:
            case 5:
                if ((col_ + i) == 1 || (col_ + i) == 3 || (col_ + i) == 5) {
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

void TableView::updateCursor(int dx, int dy) {

    col_ += dx;
    row_ += dy;
    // 6 is the transpose column: on the right, so that everything
    // keyed off "0, 2 and 4 are command columns" keeps working
    if (col_ > 6)
        col_ = 6;
    if (col_ < 0)
        col_ = 0;
    if (row_ > 15)
        row_ = 15;
    if (row_ < 0)
        row_ = 0;

    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    GUIPoint anchor = GetAnchor();
    GUIPoint p(anchor);
    switch (col_) {
    case 1:
        p._x += 5;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(*(table.param1_ + row_));
        break;
    case 3:
        p._x += 15;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(*(table.param2_ + row_));
        break;
    case 5:
        p._x += 25;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(*(table.param3_ + row_));
        break;
    };
    (void)table;

    isDirty_ = true;
};

void TableView::warpToNeighbour(int dir) {

    int current = viewData_->currentTable_ + dir;

    if (current >= TABLE_COUNT) {
        current -= TABLE_COUNT;
    }
    if (current < 0) {
        current += TABLE_COUNT;
    }
    viewData_->currentTable_ = current;
    updateCursor(0, 0);
    isDirty_ = true;
}

void TableView::updateCursorValue(int offset) {

    unsigned char *c = 0;
    unsigned char limit = 0;
    bool wrap = false;
    FourCC *cc;

    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    switch (col_) {
    case 0:
        cc = table.cmd1_ + row_;
        switch (offset) {
        case 0x01:
            *cc = CommandList::GetNext(*cc);
            break;
        case 0x10:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case -0x01:
            *cc = CommandList::GetPrev(*cc);
            break;
        case -0x10:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;

    case 1:
        switch (offset) {
        case 0x01:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case 0x10:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case -0x01:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case -0x10:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(table.param1_ + row_) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;

    case 2:
        cc = table.cmd2_ + row_;
        switch (offset) {
        case 0x01:
            *cc = CommandList::GetNext(*cc);
            break;
        case 0x10:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case -0x01:
            *cc = CommandList::GetPrev(*cc);
            break;
        case -0x10:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;
    case 3:
        switch (offset) {
        case 0x01:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case 0x10:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case -0x01:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case -0x10:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(table.param2_ + row_) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;
    case 4:
        cc = table.cmd3_ + row_;
        switch (offset) {
        case 0x01:
            *cc = CommandList::GetNext(*cc);
            break;
        case 0x10:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case -0x01:
            *cc = CommandList::GetPrev(*cc);
            break;
        case -0x10:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;
    case 5:
        switch (offset) {
        case 0x01:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case 0x10:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case -0x01:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case -0x10:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(table.param3_ + row_) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;

    case 6: {
        /* Semitones, signed, clamped rather than wrapped. A transpose
           that rolls from +63 round to -64 because a thumb was held a
           moment too long is not a feature. */
        int t = table.transpose_[row_];
        if (offset == 0x01) t += 1;
        else if (offset == -0x01) t -= 1;
        else if (offset == 0x10) t += 12;
        else if (offset == -0x10) t -= 12;
        if (t > 63) t = 63;
        if (t < -64) t = -64;
        table.transpose_[row_] = (signed char)t;
        break;
    }
    }
    if (c) {
        updateData(c, offset, limit, wrap);
        switch (col_) {
        case 0:
            lastVol_ = *c;
            break;
        case 1:
            lastTick_ = *c;
            break;
        case 2:
            lastTsp_ = *c;
            break;
        }
    }
    isDirty_ = true;
}

bool TableView::isCommandColumn() const {
    return CommandSelectorCommon::isCommandColumn(col_, 0, 2, 4);
}

FourCC *TableView::getCurrentCommandPointer() {
    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);
    return CommandSelectorCommon::getCommandPointerByCol(
        col_, 0, table.cmd1_ + row_, 2, table.cmd2_ + row_, 4,
        table.cmd3_ + row_);
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

void TableView::enterCommandSelector() {
    FourCC *cmdPtr = getCurrentCommandPointer();
    if (!cmdPtr) return;
    // a table belongs to whichever instrument runs it
    DoModal(new CommandSelectorModal(*this, cmdPtr, CommandSelectorPreviewCallback,
                                     channelApplyMask(viewData_,
                                                      viewData_->currentInstrument_)),
            CommandSelectorCallback);
}

void TableView::onCommandSelectorResult(ModalView &d) {
    CommandSelectorModal &modal = (CommandSelectorModal &)d;
    if (modal.GetReturnCode() == 1) {
        FourCC *cmd = getCurrentCommandPointer();
        if (cmd) {
            lastCmd_ = *cmd;
        }
    }
    isDirty_ = true;
}

void TableView::onCommandSelectorPreview(ModalView &) { isDirty_ = true; }

void TableView::pasteLast() {
    uint *i = 0;

    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    switch (col_) {
    case 0:
        i = table.cmd1_ + row_;
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 1:
        break;

    case 2:
        i = table.cmd2_ + row_;
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 3:
        break;

    case 4:
        i = table.cmd3_ + row_;
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 5:
        break;
    }
};

void TableView::ProcessButtonMask(unsigned short mask, bool pressed) {

    if (!pressed) {
        return;
    }
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
        processNormalButtonMask(mask);
    };
}

void TableView::processNormalButtonMask(unsigned short mask) {

    Player *player = Player::GetInstance();

    if (mask & EPBM_B) {
        if (mask & EPBM_LEFT)
            warpToNeighbour(-1);
        if (mask & EPBM_RIGHT)
            warpToNeighbour(+1);
        if (mask & EPBM_DOWN)
            warpToNeighbour(-16);
        if (mask & EPBM_UP)
            warpToNeighbour(16);
        if (mask & EPBM_A)
            cutPosition();
        if (mask & EPBM_R)
            viewMode_ = VM_SELECTION;

    } else {

        // A modifier

        if (mask & EPBM_A) {
            if (mask & EPBM_DOWN) {
                if (isCommandColumn())
                    enterCommandSelector();
                else
                    updateCursorValue(-0x10);
            }
            if (mask & EPBM_UP) {
                if (isCommandColumn())
                    enterCommandSelector();
                else
                    updateCursorValue(0x10);
            }
            if (mask & EPBM_LEFT)
                updateCursorValue(-0x01);
            if (mask & EPBM_RIGHT)
                updateCursorValue(0x01);
            if (mask == EPBM_A)
                pasteLast();
            if (mask & EPBM_R)
                pasteClipboard();
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_UP) {
                    ViewType vt =
                        (viewType_ == VT_TABLE ? VT_PHRASE : VT_INSTRUMENT);
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_LEFT) {
                    if (viewType_ == VT_TABLE2) {
                        ViewType vt = VT_TABLE;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        SetChanged();
                        NotifyObservers(&ve);
                    } else if (VT_TABLE) {
                        ViewType vt = VT_MIXER;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        SetChanged();
                        NotifyObservers(&ve);
                    }
                }
                if (mask & EPBM_RIGHT) {
                    if (viewType_ == VT_TABLE) {
                        ViewType vt = VT_TABLE2;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        SetChanged();
                        NotifyObservers(&ve);
                    }
                }
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }

            } else {
                // L MOdifier
                if (mask & EPBM_R) {
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
}

void TableView::processSelectionButtonMask(unsigned short mask) {

    Player *player = Player::GetInstance();

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
            if (mask & EPBM_R)
                cutSelection();
            //		if (mask&EPBM_R) switchSoloMode() ;
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_UP) {
                    // whichever column this table is in, up is the
                    // thing the map draws above it
                    ViewType vt = (viewType_ == VT_TABLE2) ? VT_INSTRUMENT
                                                           : VT_PHRASE;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
                /*			if (mask&EPBM_L) unMuteAll() ;
                 */
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
}

void TableView::setTextProps(GUITextProperties &props, int row, int col,
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
}

/* One table: three command columns and their parameters. */
#define TABLE_UNDO_BYTES (3*TABLE_STEPS*(sizeof(FourCC)+sizeof(ushort)))
int TableView::UndoSize() { return TABLE_UNDO_BYTES ; }
int TableView::UndoContext() { return viewData_->currentTable_ ; }
void TableView::UndoCapture(unsigned char *dst) {
	Table &t=TableHolder::GetInstance()->GetTable(viewData_->currentTable_) ;
	unsigned char *d=dst ;
	memcpy(d,t.cmd1_,sizeof(t.cmd1_)) ;     d+=sizeof(t.cmd1_) ;
	memcpy(d,t.param1_,sizeof(t.param1_)) ; d+=sizeof(t.param1_) ;
	memcpy(d,t.cmd2_,sizeof(t.cmd2_)) ;     d+=sizeof(t.cmd2_) ;
	memcpy(d,t.param2_,sizeof(t.param2_)) ; d+=sizeof(t.param2_) ;
	memcpy(d,t.cmd3_,sizeof(t.cmd3_)) ;     d+=sizeof(t.cmd3_) ;
	memcpy(d,t.param3_,sizeof(t.param3_)) ;
}
void TableView::UndoRestore(int context,const unsigned char *src) {
	viewData_->currentTable_=context ;
	Table &t=TableHolder::GetInstance()->GetTable(context) ;
	const unsigned char *d=src ;
	memcpy(t.cmd1_,d,sizeof(t.cmd1_)) ;     d+=sizeof(t.cmd1_) ;
	memcpy(t.param1_,d,sizeof(t.param1_)) ; d+=sizeof(t.param1_) ;
	memcpy(t.cmd2_,d,sizeof(t.cmd2_)) ;     d+=sizeof(t.cmd2_) ;
	memcpy(t.param2_,d,sizeof(t.param2_)) ; d+=sizeof(t.param2_) ;
	memcpy(t.cmd3_,d,sizeof(t.cmd3_)) ;     d+=sizeof(t.cmd3_) ;
	memcpy(t.param3_,d,sizeof(t.param3_)) ;
}

void TableView::DrawView() {

    Clear();

    GUITextProperties props;
    GUIPoint pos;

    Table &table =
        TableHolder::GetInstance()->GetTable(viewData_->currentTable_);

    // Title strip, like every other screen -- this view and Groove were
    // the two the reskin never reached.

    // Say WHICH table this is. The editor occupies two cells of the
    // nav map -- under phrase and under instr -- and they look
    // identical once you are inside one, so the only way to tell what
    // you are about to edit was to remember how you got here.
    char title[32];
    sprintf(title, "TABLE %2.2X  %s", viewData_->currentTable_,
            (viewType_ == VT_TABLE2) ? "of instr" : "of phrase");
    DrawTitleStrip(title, "");
    {
        // grid frame, same treatment as phrase and chain
        AppWindow &app = (AppWindow &)w_;
        GUIPoint a = GetAnchor();
        // wide enough to take in the transpose column on the left
        app.OpFrame(a._x * 8 - 62, a._y * 8 - 6, 300, 16 * 8 + 12, CD_ROW);
    }

    // Compute song grid location

    GUIPoint anchor = GetAnchor();

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

    // Draw command 1

    pos = anchor;

    FourCC *f = table.cmd1_;

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        setTextProps(props, 0, j, false);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 0, j, true);
        pos._y++;
        // ...or on its parameter. Standing on the parameter is
        // exactly when you need to be told what it means, and
        // the help used to disappear there.
        if (j == row_ && (col_ == 0 || col_ == 1)) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params 1

    pos = anchor;
    pos._x += 5;

    ushort *param = table.param1_;
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 1, j, false);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 1, j, true);
        pos._y++;
    }

    // Draw commands 2

    pos = anchor;
    pos._x += 10;

    f = table.cmd2_;

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        setTextProps(props, 2, j, false);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 2, j, true);
        pos._y++;
        if (j == row_ && (col_ == 2 || col_ == 3)) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params

    pos = anchor;
    pos._x += 15;

    param = table.param2_;
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 3, j, false);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 3, j, true);
        pos._y++;
    }

    // Draw command 3

    pos = anchor;
    pos._x += 20;

    f = table.cmd3_;

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        setTextProps(props, 4, j, false);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 4, j, true);
        pos._y++;
        // this block draws column 4; columns 0 and 2 test their own
        // index, and testing 5 here put the help on the param column
        // next door, which meant the third command never showed any
        if (j == row_ && (col_ == 4 || col_ == 5)) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params 3

    pos = anchor;
    pos._x += 25;

    param = table.param3_;
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 5, j, false);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 5, j, true);
        pos._y++;
    }

    /* Draw the transpose column.
    
       On the LEFT, outside the row numbers, because there is no room
       on the right: the anchor is column 10 for every view and the
       third parameter column ends at 39, so the grid already reaches
       the edge of the screen. Columns 0 to 6 are empty on this screen
       and nothing else wants them. */

    pos = anchor;
    pos._x -= 7;

    for (int j = 0; j < 16; j++) {
        int t = table.transpose_[j];
        setTextProps(props, 6, j, false);
        if (t == 0) {
            // a row that does nothing should look like it
            DrawString(pos._x, pos._y, " --", props);
        } else {
            char tb[5];
            tb[0] = (t < 0) ? '-' : '+';
            int a = (t < 0) ? -t : t;
            tb[1] = '0' + (a / 10);
            tb[2] = '0' + (a % 10);
            tb[3] = 0;
            DrawString(pos._x, pos._y, tb, props);
        }
        setTextProps(props, 6, j, true);
        pos._y++;
    }

    if ((viewMode_ != VM_SELECTION) &&
        ((col_ == 1) || (col_ == 3) || (col_ == 5))) {
        cmdEditField_->SetFocus();
        cmdEditField_->Draw(w_);
    };

    // (map removed — R+arrows nav lives in the hint bar)
    drawNotes();

    Player *player = Player::GetInstance();

    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    };
}

void TableView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {

    GUITextProperties props;
    GUIPoint anchor = GetAnchor();
    GUIPoint pos;

    pos._x = anchor._x - 1;
    pos._y = anchor._y + lastPosition_[0];
    DrawString(pos._x, pos._y, " ", props);

    pos._x += 10;
    pos._y = anchor._y + lastPosition_[1];
    DrawString(pos._x, pos._y, " ", props);

    pos._x += 10;
    pos._y = anchor._y + lastPosition_[2];
    DrawString(pos._x, pos._y, " ", props);

    TableHolder *th = TableHolder::GetInstance();
    // Get current channel
    int channel = viewData_->songX_;
    // Table associated to the channel playerpb
    TablePlayback &tpb = TablePlayback::GetTablePlayback(channel);
    Table *playbackTable = tpb.GetTable();
    // Table we're viewing
    Table &viewTable = th->GetTable(viewData_->currentTable_);
    if (playbackTable == &viewTable && viewData_->playMode_ != PM_AUDITION) {

        lastPosition_[0] = tpb.GetPlaybackPosition(0);
        lastPosition_[1] = tpb.GetPlaybackPosition(1);
        lastPosition_[2] = tpb.GetPlaybackPosition(2);

        pos._x = anchor._x - 1;
        pos._y = anchor._y + lastPosition_[0];
        SetColor(CD_PLAY);
        DrawString(pos._x, pos._y, ">", props);

        pos._x += 10;
        pos._y = anchor._y + lastPosition_[1];
        DrawString(pos._x, pos._y, ">", props);

        pos._x += 10;
        pos._y = anchor._y + lastPosition_[2];
        DrawString(pos._x, pos._y, ">", props);
    };
    drawNotes();
    DrawHintBar("O+</> edit  X+<> table  R+^ phrase");
}

void TableView::printHelpLegend(FourCC command, GUITextProperties props) {
    DrawCommandHelp(command);
}
