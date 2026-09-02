
#ifndef _MODAL_VIEW_H_
#define _MODAL_VIEW_H_

#include "View.h"

class ModalView : public View {
  public:
    ModalView(View &);
    virtual ~ModalView();

    bool IsFinished();
    int GetReturnCode();

  protected:
    void SetWindow(int width, int height);
    // the whole screen, no frame: a modal that is a screen of its own
    // and draws with the title strip, panels and hint bar of the real ones
    void SetFullScreen();
    virtual void ClearRect(int x, int y, int w, int h);
    virtual void DrawString(int x, int y, const char *txt,
                            GUITextProperties &props);
    void EndModal(int returnCode);

  private:
    bool finished_;
    int returnCode_;
    int left_;
    int top_;
};
#endif