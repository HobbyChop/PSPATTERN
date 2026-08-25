#include "CommandSelectorModal.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include <string.h>

// the full list INCLUDING "----" at the front: without it there is no
// way to clear a command from the picker
static int GetDisplayCommandCount() { return CommandList::GetCount(); }

static FourCC GetDisplayCommandAt(int index) {
    return CommandList::GetAt(index);
}

CommandSelectorModal::CommandSelectorModal(View &parentView,
                                           FourCC *liveTarget,
                                           ModalViewCallback previewCb,
                                           int applyMask):
    ModalView(parentView),
    selectedRow_(0),
    selectedCol_(0),
    selectedCommand_(I_CMD_NONE),
    parentView_(parentView),
    liveTarget_(liveTarget),
    savedCmd_(liveTarget ? *liveTarget : I_CMD_NONE),
    previewCb_(previewCb),
    applyMask_(applyMask) {
    // start on whatever the step already holds, including "----", so
    // confirming straight away never changes anything by surprise
    moveToCommand(liveTarget_ ? *liveTarget_ : I_CMD_NONE);
}

CommandSelectorModal::~CommandSelectorModal() {}

int CommandSelectorModal::commandToRow(FourCC command) const {
    int idx = CommandList::IndexOf(command);
    if (idx < 0) {
        return 0;
    }
    return idx / GRID_COLUMNS;
}

int CommandSelectorModal::commandToCol(FourCC command) const {
    int idx = CommandList::IndexOf(command);
    if (idx < 0) {
        return 0;
    }
    return idx % GRID_COLUMNS;
}

// "----" is a real choice now, so an out-of-range cell can no longer
// be signalled by returning it — say so explicitly
bool CommandSelectorModal::cellAtGridPos(int row, int col, FourCC &out) const {
    int index = row * GRID_COLUMNS + col;
    if (index < 0 || index >= GetDisplayCommandCount()) {
        return false;
    }
    out = GetDisplayCommandAt(index);
    return true;
}

void CommandSelectorModal::moveToCommand(FourCC command) {
    int idx = CommandList::IndexOf(command);
    if (idx < 0) {
        command = I_CMD_NONE;
    }
    selectedCommand_ = command;
    selectedRow_ = commandToRow(command);
    selectedCol_ = commandToCol(command);
}

void CommandSelectorModal::navigateGrid(int deltaRow, int deltaCol) {
    int count = GetDisplayCommandCount();
    int rows = (count + GRID_COLUMNS - 1) / GRID_COLUMNS;

    int newRow = selectedRow_;
    int newCol = selectedCol_;
    int tries = rows * GRID_COLUMNS;

    while (tries-- > 0) {
        newRow += deltaRow;
        newCol += deltaCol;

        if (newRow < 0) {
            newRow = rows - 1;
        } else if (newRow >= rows) {
            newRow = 0;
        }

        if (newCol < 0) {
            newCol = GRID_COLUMNS - 1;
        } else if (newCol >= GRID_COLUMNS) {
            newCol = 0;
        }

        FourCC candidate;
        if (cellAtGridPos(newRow, newCol, candidate)) {
            selectedRow_ = newRow;
            selectedCol_ = newCol;
            selectedCommand_ = candidate;
            if (liveTarget_) {
                *liveTarget_ = selectedCommand_;
            }
            if (previewCb_) {
                previewCb_(parentView_, *this);
            }
            return;
        }
    }
}

void CommandSelectorModal::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed) {
        return;
    }

    if (mask & EPBM_UP) {
        navigateGrid(-1, 0);
        isDirty_ = true;
    } else if (mask & EPBM_DOWN) {
        navigateGrid(1, 0);
        isDirty_ = true;
    } else if (mask & EPBM_LEFT) {
        navigateGrid(0, -1);
        isDirty_ = true;
    } else if (mask & EPBM_RIGHT) {
        navigateGrid(0, 1);
        isDirty_ = true;
    } else if (mask & EPBM_A) {
        EndModal(1);  // Confirm selection
    } else if (mask & EPBM_B) {
        if (liveTarget_) {
            *liveTarget_ = savedCmd_;
        }
        EndModal(0);  // Cancel
    }
}

void CommandSelectorModal::DrawView() {
    int count = GetDisplayCommandCount();
    int rows = (count + GRID_COLUMNS - 1) / GRID_COLUMNS;
    // the legend lives INSIDE the window, under the grid: it used to
    // be drawn in absolute screen coords at (10,0..2), which painted
    // over the title strip and the top rows of whatever view was
    // underneath
    const int LEGEND_W = 26;
    int width = GRID_COLUMNS * 5;
    if (width < LEGEND_W) {
        width = LEGEND_W;
    }
    int height = rows + 1 + LEGEND_ROWS;

    SetWindow(width, height);

    GUITextProperties props;

    // Draw grid
    char cellStr[6];
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < GRID_COLUMNS; c++) {
            FourCC cmd;
            if (!cellAtGridPos(r, c, cmd)) {
                continue;
            }
            bool isSelected = (r == selectedRow_ && c == selectedCol_);

            fourCC2char(cmd, cellStr);
            cellStr[4] = 0;

            bool applies = (CommandList::AppliesTo(cmd) & applyMask_) != 0;

            props.invert_ = isSelected;
            // A command the channel's instrument does not implement is
            // ignored without a word, so it is drawn as unavailable
            // instead of looking like every other choice.
            SetColor(isSelected ? CD_HILITE1
                                : (applies ? CD_ROW2 : CD_SONGVIEW00));
            DrawString(c * 5, r, cellStr, props);
        }
    }

    props.invert_ = false;

    // legend: the syntax line in the accent color, the description
    // below it, blanked to the window width so nothing stale shows
    char blank[41];
    memset(blank, ' ', width);
    blank[width] = 0;
    // "----" has no legend of its own; say what picking it does
    static const char *noneLegend[3] = {"---- : no command",
                                        "clears this step", ""};
    std::string *cmdStr = getHelpLegend(selectedCommand_);
    for (int i = 0; i < LEGEND_ROWS; i++) {
        int y = rows + 1 + i;
        SetColor(CD_NORMAL);
        DrawString(0, y, blank, props);
        SetColor(i == 0 ? CD_HILITE2 : CD_NORMAL);
        bool applies =
            (CommandList::AppliesTo(selectedCommand_) & applyMask_) != 0;
        const char *line = (selectedCommand_ == I_CMD_NONE)
                               ? noneLegend[i]
                               : cmdStr[i].c_str();
        // say it plainly on the last line rather than let the user
        // wonder why nothing happened
        if ((!applies) && (i == LEGEND_ROWS - 1)) {
            line = "-- not on this channel --";
            SetColor(CD_HILITE2);
        }
        DrawString(0, y, line, props);
    }
    SetColor(CD_NORMAL);
}

void CommandSelectorModal::OnPlayerUpdate(PlayerEventType, unsigned int) {}
void CommandSelectorModal::OnFocus() {}
