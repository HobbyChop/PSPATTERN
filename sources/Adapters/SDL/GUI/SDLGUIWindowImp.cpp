#include "SDLGUIWindowImp.h"
#include "UIFramework/SimpleBaseClasses/GUIWindow.h"
#include "System/System/System.h"
#include "Application/Model/Config.h"
#include "UIFramework/BasicDatas/FontConfig.h"
#include "System/Console/Trace.h"
#include <string.h>
#include "System/Console/n_assert.h"
#include "Application/Utils/char.h"
#include <assert.h>
#include "UIFramework/BasicDatas/GUIEvent.h"
#ifdef PLATFORM_PSP
#include <pspdisplay.h>
#endif

#ifdef _SHOW_GP2X_
 #include <SDL/SDL_image.h>

 #include "Application/Commands/EventDispatcher.h"
#endif

SDLGUIWindowImp *instance_ ;

#ifdef _SHOW_GP2X_
enum Bitmaps {
	BM_FRONT,
	BM_LS,
	BM_RS,
	BM_A,
	BM_B,
	BM_START,
	BM_SELECT,
	BM_LEFT,
	BM_UP,
	BM_DOWN,
	BM_RIGHT,
	BM_OVERLAY,
	BM_LAST
} ;


SDL_Surface *bitmaps_[BM_LAST] ;
int originX_[BM_LAST] ;
int originY_[BM_LAST] ;

int scaleFactor=6 ;
int gp2xAnchorX=34 ;
int gp2xAnchorY=90 ;

#endif

unsigned short appWidth=320 ;
unsigned short appHeight=240 ;

// PSP: the app's 40x30 char grid renders full-bleed at 480x272 in
// 12x9 pixel cells (8x8 glyph ink centered horizontally, one leading
// line at the cell bottom) instead of 8x8 cells floating in a border.
// App coordinates stay in the 320x240/8x8 space the views know.
#ifdef PLATFORM_PSP
#define CELL_W 12
#define CELL_H 9
#define GLYPH_PAD_X 2
#else
#define CELL_W 8
#define CELL_H 8
#define GLYPH_PAD_X 0
#endif


SDLGUIWindowImp::SDLGUIWindowImp(GUICreateWindowParams &p) 
{

  SDLCreateWindowParams &sdlP=(SDLCreateWindowParams &)p;
  cacheFonts_=sdlP.cacheFonts_ ;
  framebuffer_=sdlP.framebuffer_ ;
  repaintPending_=false ;
  
  // By default if we are not running a framebuffer device
  // we assumed it's windowed

  windowed_ = !framebuffer_;

  const SDL_VideoInfo* videoInfo = SDL_GetVideoInfo();
  NAssert(videoInfo != NULL);
 
 #if defined(PLATFORM_GP2X)
  int screenWidth = 320;
  int screenHeight = 240;
 #elif defined(PLATFORM_PSP)
  int screenWidth = 480; 
  int screenHeight = 272;
  windowed_ = false;
 #elif defined(RS97)
  int screenWidth = 320; 
  int screenHeight = 240;
  windowed_ = false;
 #else
  int screenWidth = videoInfo->current_w;
  int screenHeight = videoInfo->current_h;
 #endif
 
 #if defined(RS97)
  /* Pick the best bitdepth for the RS97 as it will select 32 as its default, even though that's slow */
  bitDepth_ = 16;
 #else
  bitDepth_ = videoInfo->vfmt->BitsPerPixel;
 #endif
  
  char driverName[64] ;
  SDL_VideoDriverName(driverName,64);
  
  Trace::Log("DISPLAY","Using driver %s. Screen (%d,%d) Bpp:%d",driverName,screenWidth,screenHeight,bitDepth_);
  
  bool fullscreen=false ;
  
  const char *fullscreenValue=Config::GetInstance()->GetValue("FULLSCREEN") ;
  if ((fullscreenValue)&&(!strcmp(fullscreenValue,"YES")))
  {
  	fullscreen=true ;
  }
  
  SDL_WM_SetCaption("LittleGPTracker","lgpt");
	
  if (!strcmp(driverName, "fbcon"))
  {
    framebuffer_ = true;
    windowed_ = false;
  }
 
  #ifdef PLATFORM_PSP
  	mult_ = 1;
  #else
	int multFromSize=MIN(screenHeight/appHeight,screenWidth/appWidth);
	const char *mult=Config::GetInstance()->GetValue("SCREENMULT") ;
	if (mult)
	{
		mult_=atoi(mult);
	}
	else
	{
		if (framebuffer_)
		{
		mult_ = multFromSize;
		}
		else
		{
		mult_ = 1;
		}
	}
  #endif
  // Create a window that is the requested size
  
  screenRect_._topLeft._x=0;
  screenRect_._topLeft._y=0;
  screenRect_._bottomRight._x=windowed_?appWidth*mult_:screenWidth;
  screenRect_._bottomRight._y=windowed_?appHeight*mult_:screenHeight;

  Trace::Log("DISPLAY","Creating SDL Window (%d,%d)",screenRect_.Width(), screenRect_.Height());
	screen_ = SDL_SetVideoMode(screenRect_.Width(),screenRect_.Height(),bitDepth_ ,fullscreen?SDL_FULLSCREEN:SDL_HWSURFACE);
	NAssert(screen_) ;

	// Compute the x & y offset to locate our app window

	appAnchorX_=(screenRect_.Width()-appWidth/8*CELL_W*mult_)/2 ;
	appAnchorY_=(screenRect_.Height()-appHeight/8*CELL_H*mult_)/2 ;

	SDL_WM_SetIcon(SDL_LoadBMP("lgpt_icon.bmp"), NULL);

    Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
#else
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = 0xff000000;
#endif

	instance_=this ;
	currentColor_=0;
	backgroundColor_=0 ;
	SDL_ShowCursor(SDL_DISABLE);

	FontConfig();
	
	if (cacheFonts_)
  {
    Trace::Log("DISPLAY","Preparing fonts") ;
		prepareFonts() ;
	}
#ifdef _SHOW_GP2X_
	Trace::Log("DISPLAY","Preparing overlay bitmaps") ;
	prepareBitmaps() ;
#endif
	updateCount_=0 ; batchRects_=false ;
} ;

SDLGUIWindowImp::~SDLGUIWindowImp() {

}

#ifdef _SHOW_GP2X_

SDL_Surface *SDLGUIWindowImp::load_image(int which,char *filename,int x,int y) {

	Trace::Debug("loading %s",filename) ;
    //Temporary storage for the image that's loaded
    SDL_Surface* loadedImage = NULL;

    //The optimized image that will be used
    SDL_Surface* optimizedImage = NULL;

    //Load the image
    loadedImage = IMG_Load(filename);

    //If nothing went wrong in loading the image
    if (loadedImage != NULL) {
		// First convert to bit depth
		SDL_Surface *converted=SDL_DisplayFormatAlpha(loadedImage); 
		// Scale using factor
		if (scaleFactor!=1) {

			Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			rmask = 0xff000000;
			gmask = 0x00ff0000;
			bmask = 0x0000ff00;
			amask = 0x000000ff;
#else
			rmask = 0x000000ff;
			gmask = 0x0000ff00;
			bmask = 0x00ff0000;
			amask = 0xff000000;
#endif


			SDL_Surface *scaled = SDL_CreateRGBSurface(SDL_HWSURFACE|SDL_SRCALPHA, converted->w*scaleFactor, converted->h*scaleFactor, bitDepth_,
			                               rmask, gmask, bmask, amask);
			if (scaled==NULL) {
				Trace::Error("Failed to scaled surface for %s",filename) ;
			} else {
        Trace::Debug("Scaling %d,%d",scaled->w,scaled->h) ;
				SDL_LockSurface(scaled) ;
				SDL_LockSurface(converted) ;
				unsigned int *src=(unsigned int *)converted->pixels;
				unsigned int *dest=(unsigned int *)scaled->pixels;

				for (int i=0;i<converted->h;i++) {
					for (int ii=0;ii<scaleFactor;ii++) {
						for (int j=0;j<converted->w;j++) {
								for (int jj=0;jj<scaleFactor;jj++) {
									*dest++=src[j] ;
								}
						} ;
						dest+=scaled->pitch/sizeof(int)-scaleFactor*converted->w ;
					}
					src+=converted->pitch/sizeof(int) ; ;
				}
				SDL_UnlockSurface(scaled) ;
				SDL_UnlockSurface(converted) ;
				bitmaps_[which] = scaled;
				SDL_FreeSurface(converted) ;
			}
		} else {
        //Create an optimized image
	        bitmaps_[which] = SDL_DisplayFormatAlpha(loadedImage);
		}
//		SDL_SetAlpha(optimizedImage,SDL_SRCALPHA,SDL_ALPHA_TRANSPARENT) ;
        //Free the old image
Trace::Debug("Freeing image") ;
        SDL_FreeSurface(loadedImage);
    } else {
	 	Trace::Debug("**ERR** IMG_Load: %s\n", IMG_GetError());

	}
Trace::Debug("storing point %d",which) ;
	originX_[which]=x ;
	originY_[which]=y ;
Trace::Debug("returning") ;

    return optimizedImage;
}

void SDLGUIWindowImp::prepareBitmaps() {

	load_image(BM_FRONT,"../bitmaps/GP2X_bg.png",0,0) ;
	load_image(BM_LS,"../bitmaps/GP2X_LS.png",0,0) ;
	load_image(BM_RS,"../bitmaps/GP2X_RS.png",77,0) ;
	load_image(BM_A,"../bitmaps/GP2X_Button.png",76,17) ;
	load_image(BM_B,"../bitmaps/GP2X_Button.png",81,12) ;
	load_image(BM_SELECT,"../bitmaps/GP2X_SS.png",79,33) ;
	load_image(BM_START,"../bitmaps/GP2X_SS.png",85,33) ;
	load_image(BM_LEFT,"../bitmaps/GP2X_LEFT.png",7,17) ;
	load_image(BM_UP,"../bitmaps/GP2X_UP.png",7,17) ;
	load_image(BM_DOWN,"../bitmaps/GP2X_DOWN.png",7,17) ;
	load_image(BM_RIGHT,"../bitmaps/GP2X_RIGHT.png",7,17) ;
	load_image(BM_OVERLAY,"../bitmaps/GP2X_overlay.png",0,0) ;
}
#endif


static SDL_Surface *fonts[FONT_COUNT] ;

void SDLGUIWindowImp::prepareFullFonts()
{
  Trace::Log("DISPLAY","Preparing full font cache") ;
  Uint32 rmask, gmask, bmask, amask;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
  rmask = 0xff000000;
  gmask = 0x00ff0000;
  bmask = 0x0000ff00;
  amask = 0x000000ff;
#else
  rmask = 0x000000ff;
  gmask = 0x0000ff00;
  bmask = 0x00ff0000;
  amask = 0xff000000;
#endif

	for (int i=0;i<FONT_COUNT;i++)
  {
    
	  fonts[i] = SDL_CreateRGBSurface(
                 SDL_HWSURFACE, 
                 8*mult_, 8*mult_, 
                 bitDepth_,
                 0, 0, 0, 0);
		if (fonts[i]==NULL) 
    {
			Trace::Error("[DISPLAY] Failed to create font surface %d",i) ;
		}
    else
    {
			SDL_LockSurface(fonts[i]) ;
			int pixelSize=fonts[i]->format->BytesPerPixel ;
			unsigned char *bgPtr=(unsigned char *)&backgroundColor_ ;
			unsigned char *fgPtr=(unsigned char *)&foregroundColor_ ;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			bgPtr+=(4-pixelSize) ;
			fgPtr+=(4-pixelSize) ;
#endif
			const unsigned char *src=font+i*8 ;
			unsigned char *dest=(unsigned char *)fonts[i]->pixels;
      for (int y = 0; y < 8; y++)
      {
				for (int n=0;n<mult_;n++)
        {
					for (int x = 0; x < 8; x++)
          {
						unsigned char *pxlPtr=src[x]?bgPtr:fgPtr ;
						for (int m=0;m<mult_;m++)
            {
							memcpy(dest,pxlPtr,pixelSize) ;
							dest+=pixelSize ;
						}
					}
					dest+=fonts[i]->pitch-8*pixelSize*mult_ ;
        }
				if (y<7) src+=FONT_WIDTH ;
      }
			SDL_UnlockSurface(fonts[i]) ;
		};
	};
}

void SDLGUIWindowImp::prepareFonts() 
{

  Trace::Log("DISPLAY","Preparing font cache") ;
  Config *config=Config::GetInstance() ;
  
  unsigned char r,g,b ;
  const char *value=config->GetValue("BACKGROUND") ;
  if (value)
  {
    char2hex(value,&r) ;
    char2hex(value+2,&g) ;
    char2hex(value+4,&b) ;
  } 
  else
  {
    r=0xF1 ;
    g=0xF1 ;
    b=0x96 ;      
  }
  backgroundColor_=SDL_MapRGB(screen_->format, r,g,b) ;
           
  value=config->GetValue("FOREGROUND") ;
  if (value)
  {
    char2hex(value,&r) ;
    char2hex(value+2,&g) ;
    char2hex(value+4,&b) ;
  } 
  else
  {
    r=0x77 ;
    g=0x6B ;
    b=0x56 ;      
  }
  foregroundColor_=SDL_MapRGB(screen_->format, r,g,b) ;
        
  prepareFullFonts() ;
}

void SDLGUIWindowImp::DrawChar(const char c, GUIPoint &pos, GUITextProperties &p) 
{
  int xx,yy;
  transform(pos, &xx, &yy);

  if ((xx<0) || (yy<0)) return;
  // full cell must fit: at full bleed there is no border to spill into
  if ((xx+CELL_W*mult_>screenRect_._bottomRight._x) ||
      (yy+CELL_H*mult_>screenRect_._bottomRight._y))
       return ;
	if ((!framebuffer_)&&(updateCount_<MAX_OVERLAYS)) {
		SDL_Rect *area=updateRects_+updateCount_++ ;
		area->x=xx ;
		area->y=yy ;
		area->h=CELL_H*mult_ ;
		area->w=CELL_W*mult_ ;
	}

	if ((CELL_W==8)&&((cacheFonts_)&&(currentColor_==foregroundColor_)&&(!p.invert_)&&(!p.transparent_))) {

		SDL_Rect srcRect ;
		srcRect.x=0 ;
		srcRect.y=0 ;
		srcRect.w=8*mult_ ;
		srcRect.h=8*mult_ ;

		SDL_Rect dstRect ;
		dstRect.x=xx ;
		dstRect.y=yy ;
		dstRect.w=8*mult_ ;
		dstRect.h=8*mult_ ;

		unsigned int fontID=c ;
		if (fontID<FONT_COUNT) {
			SDL_BlitSurface(fonts[fontID], &srcRect,screen_, &dstRect);
		}

	} else {
		// prepare bg & fg pixel ptr
		int pixelSize=screen_->format->BytesPerPixel ;
		unsigned char *bgPtr=(unsigned char *)&backgroundColor_ ;
		unsigned char *fgPtr=(unsigned char *)&currentColor_ ;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		bgPtr+=(4-pixelSize) ;
		fgPtr+=(4-pixelSize) ;
#endif
		const unsigned char *src=font+c*8 ;
		unsigned char *dest=((unsigned char *)screen_->pixels) + (yy*screen_->pitch) + xx*pixelSize;

		/* The generic loop below calls a library memcpy PER PIXEL --
		   108 of them per glyph on the PSP's 12x9 cells -- and the
		   cached-blit branch above is dead there (CELL_W is 12 and
		   cacheFonts_ is off). These two typed loops cover the real
		   cases (16 and 32bpp at mult 1) with direct stores; anything
		   else falls through to the old loop unchanged. */
		if (mult_==1 && pixelSize==2) {
			unsigned short fg ; memcpy(&fg,fgPtr,2) ;
			unsigned short bg ; memcpy(&bg,bgPtr,2) ;
			unsigned char inv=(unsigned char)p.invert_ ;
			for (int y=0;y<CELL_H;y++) {
				unsigned short *d=(unsigned short *)dest ;
				for (int x=0;x<CELL_W;x++) {
					int gx=x-GLYPH_PAD_X ;
					unsigned char v=((y<8)&&(gx>=0)&&(gx<8))?src[gx]:1 ;
					bool isBg=((v)^inv)!=0 ;
					if (!(p.transparent_&&isBg)) d[x]=isBg?bg:fg ;
				}
				dest+=screen_->pitch ;
				if (y<7) src+=FONT_WIDTH ;
			}
		} else if (mult_==1 && pixelSize==4) {
			unsigned int fg ; memcpy(&fg,fgPtr,4) ;
			unsigned int bg ; memcpy(&bg,bgPtr,4) ;
			unsigned char inv=(unsigned char)p.invert_ ;
			for (int y=0;y<CELL_H;y++) {
				unsigned int *d=(unsigned int *)dest ;
				for (int x=0;x<CELL_W;x++) {
					int gx=x-GLYPH_PAD_X ;
					unsigned char v=((y<8)&&(gx>=0)&&(gx<8))?src[gx]:1 ;
					bool isBg=((v)^inv)!=0 ;
					if (!(p.transparent_&&isBg)) d[x]=isBg?bg:fg ;
				}
				dest+=screen_->pitch ;
				if (y<7) src+=FONT_WIDTH ;
			}
		} else {
		for (int y = 0; y < CELL_H; y++) {
			for (int n=0;n<mult_;n++) {
				for (int x = 0; x < CELL_W; x++) {
					// cell padding around the 8x8 ink reads as background
					int gx=x-GLYPH_PAD_X ;
					unsigned char v=((y<8)&&(gx>=0)&&(gx<8))?src[gx]:1 ;
					bool isBg=((v)^(unsigned char)p.invert_)!=0 ;
					if (p.transparent_&&isBg) { dest+=pixelSize*mult_ ; continue ; }
					unsigned char *pxlPtr=isBg?bgPtr:fgPtr ;
					for (int m=0;m<mult_;m++) {
						memcpy(dest,pxlPtr,pixelSize) ;
						dest+=pixelSize ;
					}
				}
				dest+=screen_->pitch-CELL_W*pixelSize*mult_ ;
    		}
			if (y<7) src+=FONT_WIDTH ;
  		}
		}

	}
}

void SDLGUIWindowImp::transform(const GUIRect &srcRect,SDL_Rect *dstRect)
{
  // scale EDGES, not width/height: scaling w independently of x
  // truncates differently and leaves 1px seams between adjacent
  // rects (the slider-gradient black lines)
  int x1 = (srcRect.Left()+srcRect.Width()) * CELL_W/8 * mult_ ;
  int y1 = (srcRect.Top()+srcRect.Height()) * CELL_H/8 * mult_ ;
  dstRect->x = srcRect.Left() * CELL_W/8 * mult_ + appAnchorX_;
  dstRect->y = srcRect.Top() * CELL_H/8 * mult_  + appAnchorY_;
  dstRect->w = x1 + appAnchorX_ - dstRect->x ;
  dstRect->h = y1 + appAnchorY_ - dstRect->y ;
}

void SDLGUIWindowImp::transform(const GUIPoint &srcPoint, int *x, int *y)
{
 	*x=appAnchorX_ + srcPoint._x*CELL_W/8*mult_ ;
	*y=appAnchorY_ + srcPoint._y*CELL_H/8*mult_ ;
}

void SDLGUIWindowImp::DrawString(const char *string,GUIPoint &pos,GUITextProperties &p,bool overlay) 
{

	int len=int(strlen(string)) ;
  int xx,yy;
  transform(pos, &xx , &yy);

  if ((xx<0)||(yy<0)||(yy+CELL_H*mult_>screenRect_._bottomRight._y)) return ;

	if ((!framebuffer_)&&(updateCount_<MAX_OVERLAYS))
  {
		SDL_Rect *area=updateRects_+updateCount_++ ;
		area->x=xx ;
		area->y=yy ;
		area->h=CELL_H*mult_ ;
		area->w=len*CELL_W*mult_ ;
	}

	for (int l=0;l<len;l++)
  {
		if (xx+CELL_W*mult_>screenRect_._bottomRight._x) break ;
		if ((CELL_W==8)&&((cacheFonts_)&&(currentColor_==foregroundColor_)&&(!p.invert_)&&(!p.transparent_)))
    {
			SDL_Rect srcRect ;
			srcRect.x=0 ;
			srcRect.y=0 ;
			srcRect.w=8*mult_ ;
			srcRect.h=8*mult_ ;

			SDL_Rect dstRect ;
			dstRect.x=xx ;
			dstRect.y=yy ;
			dstRect.w=8*mult_ ;
			dstRect.h=8*mult_ ;

			unsigned int fontID=string[l] ;
			if (fontID<FONT_COUNT)
      {
				SDL_BlitSurface(fonts[fontID], &srcRect,screen_, &dstRect);
			}
		} 
    else
    {
			// prepare bg & fg pixel ptr
			int pixelSize=screen_->format->BytesPerPixel ;
			unsigned char *bgPtr=(unsigned char *)&backgroundColor_ ;
			unsigned char *fgPtr=(unsigned char *)&currentColor_ ;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			bgPtr+=(4-pixelSize) ;
			fgPtr+=(4-pixelSize) ;
#endif
			const unsigned char *src=font+(string[l]*8) ;
			unsigned char *dest=((unsigned char *)screen_->pixels) + (yy*screen_->pitch) + xx*pixelSize;

			for (int y = 0; y < CELL_H; y++) {
				for (int n=0;n<mult_;n++) {
					for (int x = 0; x < CELL_W; x++) {
						// cell padding around the 8x8 ink reads as background
						int gx=x-GLYPH_PAD_X ;
						unsigned char v=((y<8)&&(gx>=0)&&(gx<8))?src[gx]:1 ;
						bool isBg=((v)^(unsigned char)p.invert_)!=0 ;
						if (p.transparent_&&isBg) { dest+=pixelSize*mult_ ; continue ; }
						unsigned char *pxlPtr=isBg?bgPtr:fgPtr ;
						for (int m=0;m<mult_;m++) {
							memcpy(dest,pxlPtr,pixelSize) ;
							dest+=pixelSize ;
						}
					}
					dest+=screen_->pitch-CELL_W*pixelSize*mult_ ;
        		}
				if (y<7) src+=FONT_WIDTH ;
	  		}

		}
    xx+=CELL_W*mult_ ;
	}
}

void SDLGUIWindowImp::SetBatchRects(bool on) { batchRects_=on ; }

void SDLGUIWindowImp::DrawRect(GUIRect &r)
{
  SDL_Rect rect;
  transform(r, &rect);
  SDL_FillRect(screen_, &rect,currentColor_) ;
  // overlay rects must reach the display like char updates do
  if ((!framebuffer_)&&(!batchRects_)&&(updateCount_<MAX_OVERLAYS)) {
    updateRects_[updateCount_++]=rect ;
  }
} ;

void SDLGUIWindowImp::Clear(GUIColor &c,bool overlay) 
{
  SDL_Rect rect;
  rect.x = 0;
  rect.y = 0;
  rect.w = screenRect_.Width();
  rect.h = screenRect_.Height();
 
	backgroundColor_=SDL_MapRGB(screen_->format,c._r&0xFF,c._g&0xFF,c._b&0xFF);
  SDL_FillRect(screen_, &rect,backgroundColor_) ;  

#ifdef _SHOW_GP2X_
	drawGP2X() ;
	rect.x=appAnchorX_;
	rect.y=appAnchorY_;
	rect.w=320;
	rect.h=240;
  SDL_FillRect(screen_, &rect,backgroundColor_) ;  
#endif

	if (!framebuffer_)
  {		
		SDL_Rect *area=updateRects_;
		area->x=rect.x ;
		area->y=rect.y ;
		area->w=rect.w ;
		area->h=rect.h ;
		updateCount_=1 ;
	}
}

void SDLGUIWindowImp::ClearRect(GUIRect &r) 
{
  SDL_Rect rect;
  transform(r, &rect);
  SDL_FillRect(screen_, &rect,backgroundColor_) ;
} ;

// To the app we might have a smaller window
// than the effective one (PSP)

GUIRect SDLGUIWindowImp::GetRect() 
{
	return GUIRect(0,0,appWidth,appHeight) ;
}

// Pushback a SDL event to specify screen has to be redrawn.

/* Two threads ask for repaints -- the audio thread once per rendered
   block, the UI ticker on a wall clock -- and both used to push
   unconditionally. Queued repaints do not compound: by the time the
   main loop gets to the second one the first has already drawn
   everything. Coalescing keeps the SDL event queue (128 deep in 1.2)
   from filling with duplicates and dropping real input behind them. */
void SDLGUIWindowImp::Invalidate() 
{
	if (repaintPending_) return ;
	repaintPending_=true ;
	SDL_Event event ;
	event.type=SDL_VIDEOEXPOSE ;
	if (SDL_PushEvent(&event)<0) {
		repaintPending_=false ;   // queue full: let the next one try
	}
}

void SDLGUIWindowImp::SetColor(GUIColor &c) 
{
	currentColor_=SDL_MapRGB(screen_->format,c._r&0xFF,c._g&0xFF,c._b&0xFF);
}

void SDLGUIWindowImp::Lock() 
{
  if (framebuffer_)
  {
    return;
  }

	if (SDL_MUSTLOCK(screen_)) 
  {
		SDL_LockSurface(screen_) ;
	}
}

void SDLGUIWindowImp::Unlock() 
{
  if (framebuffer_)
  {
    return;
  }

	if (SDL_MUSTLOCK(screen_)) 
  {
		SDL_UnlockSurface(screen_) ;
	}
}

#if defined(PLATFORM_PSP) && defined(PSP_GU_DISPLAY)
#include <pspgu.h>
#include <pspge.h>
// Phase 1: render the scope on the GPU. AppWindow's OOP_SCOPE stops drawing
// the scope in software and instead QUEUES it here; the GU draws it straight
// into SDL's framebuffer AFTER the software present each frame, so the CPU
// no longer plots the ~184 one-pixel columns per frame.
static unsigned int __attribute__((aligned(16))) s_guList[128*1024/4];
static bool s_guReady = false;
struct GuVtxC { unsigned int color; short x, y, z; };
struct GuScope { int x,y,w,h,n; bool live; unsigned int wave,bg; short lo[96],hi[96]; };
static GuScope s_scopes[4];
static int s_scopeCount = 0;
struct GuRect { int x,y,w,h; unsigned int color; };
static GuRect s_rects[256];
static int s_rectCount = 0;
static void guGpuInit() {
	void *fbTop = 0; int fbw = 512, fbpf = 0;
	sceDisplayGetFrameBuf(&fbTop, &fbw, &fbpf, PSP_DISPLAY_SETBUF_NEXTFRAME);
	unsigned int edram = (unsigned int)sceGeEdramGetAddr();
	unsigned int off = ((unsigned int)fbTop & 0x1FFFFFFF) - (edram & 0x1FFFFFFF);
	sceGuInit();
	sceGuStart(GU_DIRECT, s_guList);
	sceGuDrawBuffer(fbpf, (void*)off, fbw);
	sceGuOffset(2048 - (480/2), 2048 - (272/2));
	sceGuViewport(2048, 2048, 480, 272);
	sceGuScissor(0, 0, 480, 272);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuFinish();
	sceGuSync(0, 0);
	s_guReady = true;
}
static void guDrawOverlays() {
	if (!s_scopeCount && !s_rectCount) return;
	if (!s_guReady) guGpuInit();
	const int fmt = GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D;
	sceGuStart(GU_DIRECT, s_guList);
	if (s_rectCount) {
		GuVtxC *rv = (GuVtxC*)sceGuGetMemory(2 * s_rectCount * sizeof(GuVtxC));
		for (int i = 0; i < s_rectCount; i++) {
			GuRect &r = s_rects[i];
			rv[2*i].color   = r.color; rv[2*i].x   = r.x;       rv[2*i].y   = r.y;       rv[2*i].z = 0;
			rv[2*i+1].color = r.color; rv[2*i+1].x = r.x + r.w; rv[2*i+1].y = r.y + r.h; rv[2*i+1].z = 0;
		}
		sceGuDrawArray(GU_SPRITES, fmt, 2 * s_rectCount, 0, rv);
	}
	for (int si = 0; si < s_scopeCount; si++) {
		GuScope &sc = s_scopes[si];
		GuVtxC *bg = (GuVtxC*)sceGuGetMemory(2 * sizeof(GuVtxC));
		bg[0].color = sc.bg; bg[0].x = sc.x;        bg[0].y = sc.y;        bg[0].z = 0;
		bg[1].color = sc.bg; bg[1].x = sc.x + sc.w; bg[1].y = sc.y + sc.h; bg[1].z = 0;
		sceGuDrawArray(GU_SPRITES, fmt, 2, 0, bg);
		int amp = sc.h/2 - 1, mid = sc.y + sc.h/2, w = sc.w, n = sc.n;
		if (amp < 1 || n < 1 || w < 1) continue;
		GuVtxC *col = (GuVtxC*)sceGuGetMemory(2 * w * sizeof(GuVtxC));
		for (int j = 0; j < w; j++) {
			int k = j * n / w;
			int top = sc.live ? -((int)sc.hi[k] * amp) / 16384 : 0;
			int bot = sc.live ? -((int)sc.lo[k] * amp) / 16384 : 0;
			if (top < -amp) top = -amp; if (bot > amp) bot = amp;
			if (top >  amp) top =  amp; if (bot < -amp) bot = -amp;
			col[2*j].color   = sc.wave; col[2*j].x   = sc.x + j;     col[2*j].y   = mid + top;     col[2*j].z = 0;
			col[2*j+1].color = sc.wave; col[2*j+1].x = sc.x + j + 1; col[2*j+1].y = mid + bot + 1; col[2*j+1].z = 0;
		}
		sceGuDrawArray(GU_SPRITES, fmt, 2 * w, 0, col);
	}
	sceGuFinish();
	sceGuSync(0, 0);
	s_scopeCount = 0;
	s_rectCount = 0;
}
void SDLGUIWindowImp::GuQueueScope(int x,int y,int w,int h,const short*lo,const short*hi,
                                   int n,bool live,unsigned int wave,unsigned int bg) {
	if (s_scopeCount >= 4) return;
	if (n > 96) n = 96;
	// map app coords to screen pixels exactly like the software DrawRect
	GUIRect r(x, y, x + w, y + h);
	SDL_Rect sr; transform(r, &sr);
	GuScope &sc = s_scopes[s_scopeCount++];
	sc.x=sr.x; sc.y=sr.y; sc.w=sr.w; sc.h=sr.h; sc.n=n;
	if (sc.w > 200) sc.w = 200;
	sc.live=live; sc.wave=wave; sc.bg=bg;
	for (int j=0;j<n;j++) { sc.lo[j]=lo[j]; sc.hi[j]=hi[j]; }
}
void SDLGUIWindowImp::GuQueueRect(int x,int y,int w,int h,unsigned int color) {
	if (s_rectCount >= 256) return;
	GUIRect gr(x, y, x + w, y + h);
	SDL_Rect sr; transform(gr, &sr);
	GuRect &r = s_rects[s_rectCount++];
	r.x = sr.x; r.y = sr.y; r.w = sr.w; r.h = sr.h; r.color = color;
}
#endif

void SDLGUIWindowImp::Flush()
{
#ifdef BUFFERED
    // flip front and back buffers in hardware
    SDL_Flip(screen_);
#endif
#ifdef _SHOW_GP2X_
    drawGP2XOverlay() ;
    SDL_UpdateRect(screen_, 0, 0, rect_.Width(), rect_.Height());
#endif
#ifndef BUFFERED
    // blit partial updates on resource constrained platforms
    if ((!framebuffer_)&&(updateCount_!=0))
    {
#ifdef PLATFORM_PSP
        /* Wait for the vertical blank before the copy.

           There is no double buffering here -- BUFFERED is defined for
           the handhelds that have it and not for the PSP, so SDL_Flip
           is never called and this UpdateRects writes straight into the
           framebuffer the display is scanning out. Anything that moves
           tears, which in practice means the meters, because they are
           the only thing changing every frame.

           This has to be HERE and not earlier in the frame. The first
           attempt waited before the panels were drawn, which is before
           the ops are computed but well before they are written -- by
           the time the copy actually happened the beam had moved on and
           the tearing was unchanged. What matters is when the WRITE
           lands, not when the drawing is decided.

           It costs about two thirds of a millisecond against a 16ms
           frame and locks the copy to the panel instead of letting it
           drift. Audio is on its own thread and does not care. */
        sceDisplayWaitVblankStart();
#endif
        if (updateCount_<MAX_OVERLAYS)
        {
            SDL_UpdateRects(screen_,updateCount_, updateRects_);
        }
        else
        {
            SDL_UpdateRect(screen_, 0, 0, screenRect_.Width(),screenRect_.Height());
        }
    }
    updateCount_=0;
#if defined(PLATFORM_PSP) && defined(PSP_GU_DISPLAY)
    guDrawOverlays();
#endif
#endif
}

void SDLGUIWindowImp::ProcessExpose() 
{
	repaintPending_=false ;
	_window->Update() ;
}

void SDLGUIWindowImp::ProcessQuit()
{
	GUIPoint p;
	GUIEvent e(p,ET_SYSQUIT) ;
	_window->DispatchEvent(e) ;	
} ;

void SDLGUIWindowImp::PushEvent(GUIEvent &event)
{
	SDL_Event sdlevent ;
	sdlevent.type=SDL_USEREVENT ;
	sdlevent.user.data1=&event ;
	SDL_PushEvent(&sdlevent) ;
} ;

void SDLGUIWindowImp::ProcessUserEvent(SDL_Event &event)
{
	GUIEvent *guiEvent=(GUIEvent *)event.user.data1 ;
	_window->DispatchEvent(*guiEvent) ;
	delete(guiEvent) ;
}

#ifdef _SHOW_GP2X_

void SDLGUIWindowImp::drawSub(int which)
{
	SDL_Rect srcRect ;
	srcRect.x=0 ;
	srcRect.y=0 ;
	srcRect.w=bitmaps_[which]->w ;
	srcRect.h=bitmaps_[which]->h ;

	SDL_Rect dstRect ;
	dstRect.x=gp2xAnchorX+originX_[which]*scaleFactor ;
	dstRect.y=gp2xAnchorY+originY_[which]*scaleFactor ;
	dstRect.w=srcRect.w ;
	dstRect.h=srcRect.h ;

	SDL_BlitSurface(bitmaps_[which], &srcRect,screen_, &dstRect);
}

void SDLGUIWindowImp::drawGP2X()
{
	drawSub(BM_FRONT) ;
}

void SDLGUIWindowImp::drawGP2XOverlay()
{
	int mask=EventDispatcher::GetInstance()->GetEventMask() ;
	Trace::Debug("got mask=%d",mask) ;
	drawSub(BM_OVERLAY) ;
	if (mask&(1<<EPBT_L)) drawSub(BM_LS) ;
	if (mask&(1<<EPBT_R)) drawSub(BM_RS) ;
	if (mask&(1<<EPBT_A)) drawSub(BM_A) ;
	if (mask&(1<<EPBT_B)) drawSub(BM_B) ;
	if (mask&(1<<EPBT_START)) drawSub(BM_START) ;
	if (mask&(1<<EPBT_SELECT)) drawSub(BM_SELECT) ;
	if (mask&(1<<EPBT_LEFT)) drawSub(BM_LEFT) ;
	if (mask&(1<<EPBT_UP)) drawSub(BM_UP) ;
	if (mask&(1<<EPBT_RIGHT)) drawSub(BM_RIGHT) ;
	if (mask&(1<<EPBT_DOWN)) drawSub(BM_DOWN) ;
}
#endif
