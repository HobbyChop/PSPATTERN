#ifndef _COMMAND_SELECTOR_MODAL_H_
#define _COMMAND_SELECTOR_MODAL_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include "Application/Views/BaseClasses/View.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Views/CommandSelectorCommon.h"

class CommandSelectorModal : public ModalView {
  public:
    // applyMask says which instrument type this channel carries, so
    // commands that cannot reach it can be shown as unavailable rather
    // than silently doing nothing when the user picks one.
    CommandSelectorModal(View &parentView, FourCC *liveTarget,
                        ModalViewCallback previewCb = 0,
                        int applyMask = CMD_ON_ALL);
    virtual ~CommandSelectorModal();

    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();

  private:
    void navigateGrid(int deltaRow, int deltaCol);
    void moveToCommand(FourCC command);
    int commandToRow(FourCC command) const;
    int commandToCol(FourCC command) const;
    bool cellAtGridPos(int row, int col, FourCC &out) const;

    int selectedRow_;
    int selectedCol_;
    FourCC selectedCommand_;
    View &parentView_;
    FourCC *liveTarget_;
    FourCC savedCmd_;
    ModalViewCallback previewCb_;
    int applyMask_;

    static const int GRID_COLUMNS = CommandSelectorCommon::kColumns;
    static const int LEGEND_ROWS = 3;
};

#endif
