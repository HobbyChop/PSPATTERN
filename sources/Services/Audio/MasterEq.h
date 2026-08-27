#ifndef _MASTER_EQ_H_
#define _MASTER_EQ_H_

#include "Application/Utils/fixed.h"

#define MASTER_EQ_BANDS 10
#define MASTER_EQ_FLAT  64      /* the value that means "do nothing" */
#define MASTER_EQ_MAX   127

/********************************************************
 MasterEq:
    A ten band graphic EQ on the master bus.

    NOT ten biquads. Nine one-pole lowpasses at octave
    spaced cutoffs, where each band is the DIFFERENCE
    between adjacent lowpass outputs and the top band is
    whatever is left over:

        band 0 = lp[0]
        band k = lp[k] - lp[k-1]
        band 9 = input - lp[8]

    Ten bands for nine filters, and because the bands are
    differences of the same signal they sum back to the
    input exactly when every gain is unity. Flat is
    genuinely flat, not approximately flat -- which matters
    for a control somebody leaves alone.

    Lifted from PSPECTRA, which runs the same design on the
    same chip at the same sample rate. The coefficients are
    its coefficients.

    COST: about 150 operations a sample for stereo, which is
    a few percent of an audio block on this machine. That
    would be too much to spend unconditionally, so it is not
    spent unconditionally: Active() is false while every
    band sits at flat, and the whole stage is skipped. A
    user who never touches it never pays for it.
 ********************************************************/

class MasterEq {
public:

	MasterEq() { Reset() ; }

	void Reset() {
		for (int k=0;k<MASTER_EQ_BANDS;k++) SetBand(k,MASTER_EQ_FLAT) ;
		for (int k=0;k<9;k++) { lpL_[k]=0 ; lpR_[k]=0 ; }
		for (int k=0;k<9;k++) { lpLf_[k]=0.0f ; lpRf_[k]=0.0f ; }
	}

	/* v is 0..127 with 64 flat, matching the control the user sees.
	   The curve is v*v*8: unity at 64, silence at 0, and x3.94
	   (+11.9dB) at 127. Squared, so the control is fine where it
	   matters -- near flat -- and coarse out at the ends. */
	void SetBand(int k,int v) {
		if (k<0||k>=MASTER_EQ_BANDS) return ;
		if (v<0) v=0 ;
		if (v>MASTER_EQ_MAX) v=MASTER_EQ_MAX ;
		gain_[k]=v*v*8 ;
		rebuild() ;
	}

	bool Active() const { return active_ ; }
	// How many one-pole filters this setting actually costs. The
	// whole point of the difference form: a tone control that moves
	// one or two bands runs two or three filters, not nine.
	int ActiveFilters() const { return nActive_ ; }

	/* Process one interleaved stereo buffer in place.

	   Two implementations of the same EQ. The fixed difference form is
	   the default and ships everywhere: it runs only the bands that
	   are not flat against their neighbour, so a light touch costs two
	   or three filters. The float full-ladder runs all nine every
	   sample but with no branch and no gather, which is what the VFPU
	   wants -- four bands to a lane -- so its cost is flat regardless
	   of how many bands are moved, and it caps the heavy-sculpt case
	   that the difference form makes expensive. Behind PSP_VFPU_EQ,
	   off by default; RenderFloatScalar is the portable mirror and the
	   host-test reference. */
	void Render(fixed *buffer,int samplecount) {
		if (!active_) return ;
#if defined(__PSP__) && defined(PSP_VFPU_EQ)
		RenderVfpu(buffer,samplecount) ;
#else
		RenderFixed(buffer,samplecount) ;
#endif
	}

	void RenderFixed(fixed *buffer,int samplecount) {

		fixed *p=buffer ;
		const int n=nActive_ ;
		for (int i=0;i<samplecount;i++) {

			int l=(int)p[0] ;
			int r=(int)p[1] ;

			long long accL=(long long)l*g9_ ;
			long long accR=(long long)r*g9_ ;

			for (int a=0;a<n;a++) {
				const int k=idx_[a] ;
				const int dg=dg_[a] ;
				/* 64 bit for the one-pole multiply, and it is not
				   optional here.

				   PSPECTRA runs this in a 16-bit sample domain where
				   coef * (x - lp) fits an int with room to spare. Our
				   'fixed' is Q15 with full scale at 32767<<15, about
				   2^30, so the same product reaches 10^13 and wraps.

				   It hid, too: the bands are differences of adjacent
				   lowpasses, so at flat they telescope and reconstruct
				   the input no matter what the filter states contain.
				   A flat EQ measured perfect while every filter in it
				   was full of wrapped garbage. On MIPS this is one
				   mult instruction, not a library call. */
				lpL_[k]+=(int)(((long long)coef_[k]*(l-lpL_[k]))>>15) ;
				lpR_[k]+=(int)(((long long)coef_[k]*(r-lpR_[k]))>>15) ;
				accL+=(long long)lpL_[k]*dg ;
				accR+=(long long)lpR_[k]*dg ;
			}

			p[0]=(fixed)(accL>>15) ;
			p[1]=(fixed)(accR>>15) ;
			p+=2 ;
		}
	}

	/* The same EQ in float, all nine one-poles every sample, no branch
	   and no gather -- the portable mirror of the VFPU kernel and the
	   host-test reference.

	   Worked in the fp2fl value domain (full scale 32768.0), not the
	   raw Q30 fixed the difference form uses. That is the whole reason
	   float is safe here: a one-pole at the lowest coefficient only
	   fails to integrate when the input sits within about 1e-5 of the
	   filter state -- i.e. already settled -- because float carries
	   RELATIVE precision and the increment scales with the state. The
	   Q30 domain, where the state runs to 2^30 and small increments
	   fall off the bottom of the mantissa, is what made float look
	   impossible; this domain does not have that problem. eq_float_test
	   checks it against the fixed form. */
	void RenderFloatScalar(fixed *buffer,int samplecount) {
		fixed *p=buffer ;
		for (int i=0;i<samplecount;i++) {
			float x=fp2fl(p[0]) ;
			float y=fp2fl(p[1]) ;
			float accL=x*g9f_ ;
			float accR=y*g9f_ ;
			for (int k=0;k<9;k++) {
				lpLf_[k]+=coeff_[k]*(x-lpLf_[k]) ;
				lpRf_[k]+=coeff_[k]*(y-lpRf_[k]) ;
				accL+=lpLf_[k]*dgf_[k] ;
				accR+=lpRf_[k]*dgf_[k] ;
			}
			p[0]=fl2fp(accL) ;
			p[1]=fl2fp(accR) ;
			p+=2 ;
		}
	}

#if defined(__PSP__) && defined(PSP_VFPU_EQ)
	void RenderVfpu(fixed *buffer,int samplecount) ;
#endif

#ifndef __PSP__
	// Host-test hook: run the float ladder directly, whatever the
	// build flags. See tools/eq_float_test.cc.
	void RenderFloatForTest(fixed *buffer,int samplecount) {
		if (!active_) return ;
		RenderFloatScalar(buffer,samplecount) ;
	}
#endif

	/* Nominal centres at 44100, for the screen. The one-pole
	   coefficients below work out to roughly these. */
	static const char *BandName(int k) {
		static const char *n[MASTER_EQ_BANDS]={
			"40","80","160","330","670","1k3","2k7","5k9","13k","hi" } ;
		return (k>=0&&k<MASTER_EQ_BANDS)?n[k]:"" ;
	}

private:
	/* Q15 one-pole coefficients, octave spaced. At 44100 these are
	   about 42, 85, 168, 335, 671, 1346, 2745, 5905 and 12900 Hz. */
	/* Rebuild the list of filters that actually matter.

	   out = SUM over k of lp[k]*(G[k]-G[k+1]) + x*G[9]

	   is the same sum written the other way round, and it says
	   something useful: where two neighbouring bands hold the SAME
	   gain that difference is zero, and the filter between them
	   contributes nothing at all. Move one band and only the two
	   filters either side of it are needed -- two instead of nine.

	   That is the difference between an EQ that fits in the audio
	   block and one that does not. The full nine are only ever run
	   when all ten bands genuinely differ, which is not what a tone
	   control looks like in use. */
	void rebuild() {
		nActive_=0 ;
		for (int k=0;k<9;k++) {
			int dg=gain_[k]-gain_[k+1] ;
			// The float ladder carries the full nine differences,
			// zeros and all, because it never branches on which bands
			// are active -- that is what lets it vectorise.
			dgf_[k]=dg*(1.0f/32768.0f) ;
			if (dg==0) continue ;
			idx_[nActive_]=k ;
			dg_[nActive_]=dg ;
			nActive_++ ;
		}
		g9_=gain_[9] ;
		g9f_=g9_*(1.0f/32768.0f) ;
		active_=(nActive_!=0)||(g9_!=32768) ;
	}

	static const int coef_[9] ;
	// The Q15 coefficients as float smoothing factors, coef/32768.
	static const float coeff_[9] ;

	int gain_[MASTER_EQ_BANDS] ;
	int lpL_[9], lpR_[9] ;
	/* The filters that are not flat against their neighbour, and the
	   gain difference each one carries. */
	int idx_[9], dg_[9] ;
	int nActive_ ;
	int g9_ ;
	bool active_ ;

	/* Float mirror of the state, for the full-ladder / VFPU path. Its
	   own lp state so the two paths never alias; the difference gains
	   and the top-band gain in float. */
	float lpLf_[9], lpRf_[9] ;
	float dgf_[9] ;
	float g9f_ ;
} ;

#endif
