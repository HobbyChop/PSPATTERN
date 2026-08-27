#ifndef _FDN_REVERB_H_
#define _FDN_REVERB_H_

#include <stdlib.h>   /* malloc/free, as the rest of SendFx uses */

/******************************************************************
 FdnReverb: a four-line Feedback Delay Network.

 Built to be a VFPU-native reverb rather than a port of the
 Freeverb-style comb bank, which fought vectorisation (mismatched
 comb lengths gather, serial chains, and an integer limit-cycle tail
 -- see tools/reverbfloat_test.cc). An FDN's expensive core is its
 feedback matrix: every output is a weighted sum of every line, which
 is a 4x4 matrix times a 4-vector -- 16 multiply-adds a sample done
 in ONE vtfm4.q on the VFPU. Everything else is a handful of vec4
 ops. So the topology the VFPU is naturally good at is also a
 respected, high-quality reverb.

 Signal flow, per sample:
     s[i]   = line[i][pos[i]]              (the four delayed outputs)
     lp[i] += dc*(s[i]-lp[i])             (one-pole damping per line)
     fed    = g * (A . lp)                (lossless mix, scaled to decay)
     line[i][pos[i]] = in*b[i] + fed[i]   (inject and recirculate)
     outL = cL . s ,  outR = cR . s       (decorrelated stereo taps)

 A is the normalised 4x4 Hadamard matrix -- orthogonal, so the mix
 loses no energy and g alone sets the decay. This header holds the
 portable scalar version; the VFPU version lives behind the build
 flag and computes the identical maths four lanes wide.
 ******************************************************************/

class FdnReverb {
public:
	static const int NLINE = 4 ;

	FdnReverb() {
		for (int i=0;i<NLINE;i++) buf_[i]=0 ;
		size_=160 ; damp_=110 ;
		g_=0.85f ; dc_=0.7f ;
	}

#ifndef FDN_NDIFF
#define FDN_NDIFF 4        /* input diffusers; fewer is cheaper, sparser */
#endif
	static const int NDIFF = FDN_NDIFF ;

	void Init() {
		// Mutually-prime line lengths, ~35-59ms at 44100: dense enough
		// modes for a smooth tail, spread enough not to beat together.
		static const int L[NLINE] = { 1553, 1889, 2251, 2609 } ;
		for (int i=0;i<NLINE;i++) {
			len_[i]=L[i] ;
			buf_[i]=(float*)malloc(sizeof(float)*len_[i]) ;
			if (buf_[i]) for (int j=0;j<len_[i];j++) buf_[i][j]=0.0f ;
			pos_[i]=0 ; lp_[i]=0.0f ;
		}
		// Input diffusers: short allpasses that smear a transient into
		// a dense burst before it reaches the network, so the tail
		// fills in instead of arriving as a sparse metallic train. The
		// lengths and gains are the Dattorro plate's input diffusor.
		static const int DL[4] = { 142, 107, 379, 277 } ;
		static const float DG[4] = { 0.75f, 0.75f, 0.625f, 0.625f } ;
		for (int i=0;i<NDIFF;i++) {
			difLen_[i]=DL[i] ; difG_[i]=DG[i] ;
			dif_[i]=(float*)malloc(sizeof(float)*difLen_[i]) ;
			if (dif_[i]) for (int j=0;j<difLen_[i];j++) dif_[i][j]=0.0f ;
			difPos_[i]=0 ;
		}
		rebuild() ;
	}

	void Close() {
		for (int i=0;i<NLINE;i++) { if (buf_[i]) { free(buf_[i]); buf_[i]=0 ; } }
		for (int i=0;i<NDIFF;i++) { if (dif_[i]) { free(dif_[i]); dif_[i]=0 ; } }
	}

	void Flush() {
		for (int i=0;i<NLINE;i++) {
			if (buf_[i]) for (int j=0;j<len_[i];j++) buf_[i][j]=0.0f ;
			lp_[i]=0.0f ;
		}
		for (int i=0;i<NDIFF;i++)
			if (dif_[i]) for (int j=0;j<difLen_[i];j++) dif_[i][j]=0.0f ;
	}

	// One mono sample through the input diffuser chain.
	inline float diffuse(float x) {
		for (int d=0;d<NDIFF;d++) {
			float delayed = dif_[d][difPos_[d]] ;
			float w = x + difG_[d]*delayed ;
			dif_[d][difPos_[d]] = w ;
			x = delayed - difG_[d]*w ;
			if (++difPos_[d]>=difLen_[d]) difPos_[d]=0 ;
		}
		return x ;
	}

	// 0..255, same controls the old reverb exposed.
	void SetSize(int s) { size_ = s<0?0:(s>255?255:s) ; rebuild() ; }
	void SetDamp(int d) { damp_ = d<0?0:(d>255?255:d) ; rebuild() ; }

	/* Process one interleaved stereo block. `in` is the send going to
	   the reverb (interleaved), `out` receives the wet (interleaved).
	   in and out may be the same buffer. */
	// One interleaved stereo sample in, one wet stereo sample out.
	// Dispatches to the VFPU core when built for it; the scalar body is
	// the reference and the fallback.
	inline void ProcessOne(float inL,float inR,float &outL,float &outR) {
#if defined(__PSP__) && defined(PSP_VFPU_REVERB)
		ProcessOneVfpu(inL,inR,outL,outR) ;
#else
		ProcessOneScalar(inL,inR,outL,outR) ;
#endif
	}

	inline void ProcessOneScalar(float inL,float inR,float &outL,float &outR) {
		const float g=g_, dc=dc_ ;
		// one mono send drives the network; the stereo comes back out
		// of the decorrelated output taps. Diffuse the input first so
		// the tail is dense from the start.
		float x = diffuse(0.5f*(inL+inR)) ;

		float s0=buf_[0][pos_[0]], s1=buf_[1][pos_[1]] ;
		float s2=buf_[2][pos_[2]], s3=buf_[3][pos_[3]] ;

		// per-line damping one-pole
		lp_[0]+=dc*(s0-lp_[0]) ; lp_[1]+=dc*(s1-lp_[1]) ;
		lp_[2]+=dc*(s2-lp_[2]) ; lp_[3]+=dc*(s3-lp_[3]) ;
		float f0=lp_[0], f1=lp_[1], f2=lp_[2], f3=lp_[3] ;

		// Hadamard feedback (normalised by 1/2), scaled by g
		float m0=0.5f*( f0+f1+f2+f3) ;
		float m1=0.5f*( f0-f1+f2-f3) ;
		float m2=0.5f*( f0+f1-f2-f3) ;
		float m3=0.5f*( f0-f1-f2+f3) ;

		// inject and recirculate
		buf_[0][pos_[0]] = x + g*m0 ;
		buf_[1][pos_[1]] = x + g*m1 ;
		buf_[2][pos_[2]] = x + g*m2 ;
		buf_[3][pos_[3]] = x + g*m3 ;

		for (int i=0;i<NLINE;i++) if (++pos_[i]>=len_[i]) pos_[i]=0 ;

		// decorrelated stereo taps
		outL = s0 + s2 - 0.5f*(s1+s3) ;
		outR = s1 + s3 - 0.5f*(s0+s2) ;
	}

	void ProcessScalar(const float *in, float *out, int n) {
		for (int k=0;k<n;k++)
			ProcessOneScalar(in[k*2],in[k*2+1],out[k*2],out[k*2+1]) ;
	}

#if defined(__PSP__) && defined(PSP_VFPU_REVERB)
	/* The same FDN, but the four-line feedback -- s minus lp, damped,
	   then the 4x4 Hadamard mix -- runs on the VFPU. The mix, which is
	   sixteen multiply-adds in the scalar body, is a single vtfm4.q
	   against the pre-scaled matrix. The diffuser stays scalar (it is
	   a serial allpass chain, no gather to vectorise), and the four
	   line reads and writes stay scalar loads/stores because the lines
	   are at four different addresses. UNVERIFIED off the device --
	   assembles under psp-gcc and computes what ProcessOneScalar (the
	   reference the ear already approved) computes.

	   The g-scaled Hadamard is symmetric, so loading it row-major into
	   the four columns of a VFPU matrix gives the matrix vtfm4.q wants
	   with no transpose. Constants and state are loaded per sample
	   from aligned scratch rather than kept resident, so nothing is
	   assumed about VFPU registers surviving the scalar code between
	   samples. */
	/* Load the constant matrix, the damping coefficient and the line
	   state into VFPU registers ONCE, so the per-sample core reloads
	   none of them -- reloading the matrix every sample (four lv.q)
	   was eating the whole point of doing the mix in one vtfm4.q.

	   Between this and EndVfpuBlock the only code that runs is the
	   per-sample core and the caller's own scalar/integer work (the
	   delay, the sends), none of which touches a V register, so
	   M100 / C200 / S230 survive. Call it once around a render loop,
	   with EndVfpuBlock at the end to write the state back. */
	inline void BeginVfpuBlock() {
		int dcbits ; float dcv=dc_ ;
		__builtin_memcpy(&dcbits,&dcv,4) ;
		__asm__ volatile (
			"lv.q C100, 0(%[m])\n"  "lv.q C110, 16(%[m])\n"
			"lv.q C120, 32(%[m])\n" "lv.q C130, 48(%[m])\n"   // A' -> matrix 1
			"lv.q C200, 0(%[lp])\n"                            // lp  (resident)
			"mtv  %[dc], S230\n"                               // dc  (resident)
			:: [m]"r"(mtx_), [lp]"r"(lp_), [dc]"r"(dcbits) : "memory") ;
	}
	inline void EndVfpuBlock() {
		__asm__ volatile ("sv.q C200, 0(%[lp])\n"
			"vflush\n"                       // drain before the CPU reads lp_
			:: [lp]"r"(lp_) : "memory") ;
	}

	// Per-sample core. Requires BeginVfpuBlock to have loaded the
	// resident matrix / lp / dc; only the four line reads and writes
	// and the input touch memory.
	inline void ProcessOneVfpu(float inL,float inR,float &outL,float &outR) {
		float x = diffuse(0.5f*(inL+inR)) ;

		float __attribute__((aligned(16))) sIn[4], sOut[4] ;
		sIn[0]=buf_[0][pos_[0]] ; sIn[1]=buf_[1][pos_[1]] ;
		sIn[2]=buf_[2][pos_[2]] ; sIn[3]=buf_[3][pos_[3]] ;

		int xbits ; __builtin_memcpy(&xbits,&x,4) ;

		__asm__ volatile (
			"lv.q C000, 0(%[s])\n"                // s (the four line reads)
			"vsub.q C010, C000, C200\n"           // s - lp   (resident lp)
			"vscl.q C010, C010, S230\n"           // * dc     (resident dc)
			"vadd.q C200, C200, C010\n"           // lp += dc*(s-lp) -> f
			"vtfm4.q C020, M100, C200\n"          // fed = A'.f  (resident matrix)
			"mtv  %[x], S030\n"
			"vadd.q C020, C020, C030[x,x,x,x]\n"  // + x broadcast
			"sv.q C020, 0(%[out])\n"              // new line inputs
			:
			: [s]"r"(sIn), [out]"r"(sOut), [x]"r"(xbits)
			: "memory") ;

		buf_[0][pos_[0]]=sOut[0] ; buf_[1][pos_[1]]=sOut[1] ;
		buf_[2][pos_[2]]=sOut[2] ; buf_[3][pos_[3]]=sOut[3] ;
		for (int i=0;i<NLINE;i++) if (++pos_[i]>=len_[i]) pos_[i]=0 ;

		outL = sIn[0] + sIn[2] - 0.5f*(sIn[1]+sIn[3]) ;
		outR = sIn[1] + sIn[3] - 0.5f*(sIn[0]+sIn[2]) ;
	}
#else
	// No-ops off the VFPU build, so the caller can bracket its loop
	// unconditionally.
	inline void BeginVfpuBlock() {}
	inline void EndVfpuBlock() {}
#endif

	// The knob-to-coefficient map, exposed so a test can sweep it.
	float FeedbackGain() const { return g_ ; }
	float DampCoef() const { return dc_ ; }
	int LineLen(int i) const { return len_[i] ; }

private:
	void rebuild() {
		// size 0..255 -> feedback 0.5..0.95: short slap to a long hall.
		g_ = 0.5f + (size_/255.0f)*0.45f ;
		// damp 0..255 -> one-pole coef 1.0..0.25: none to heavy HF loss.
		dc_ = 1.0f - (damp_/255.0f)*0.75f ;
		// The feedback matrix the VFPU multiplies: the normalised (1/2)
		// Hadamard, pre-scaled by g so vtfm4.q does the whole feedback
		// in one op. Symmetric, so this row-major layout loads straight
		// into the matrix columns. Rebuilt only on a knob move.
		static const float H[16] = {
			1, 1, 1, 1,  1,-1, 1,-1,  1, 1,-1,-1,  1,-1,-1, 1 } ;
		float s = 0.5f*g_ ;
		for (int i=0;i<16;i++) mtx_[i]=s*H[i] ;
	}

	float *buf_[NLINE] ;
	int    len_[NLINE], pos_[NLINE] ;
	float  lp_[NLINE] ;
	float *dif_[NDIFF] ;
	int    difLen_[NDIFF], difPos_[NDIFF] ;
	float  difG_[NDIFF] ;
	int    size_, damp_ ;
	float  g_, dc_ ;
	float __attribute__((aligned(16))) mtx_[16] ;   // g-scaled Hadamard
} ;

#endif
