
#ifndef _SAMPLE_POOL_H_
#define _SAMPLE_POOL_H_

#include "Foundation/T_Singleton.h"
#include "WavFile.h"
#include "Application/Model/Song.h"
#include "Foundation/Observable.h"

#define MAX_PIG_SAMPLES MAX_SAMPLEINSTRUMENT_COUNT

enum SamplePoolEventType {
	SPET_INSERT,
	SPET_DELETE
} ;

struct SamplePoolEvent: public I_ObservableData {
	SamplePoolEventType type_ ;
	int index_ ;
} ;

// SamplePool::Load combines these with |=, so they have to be disjoint
// bits. INPUT_FILE used to be 5, which is MAX_SAMPLES|INVALID_DIR --
// one unreadable wav would have reported itself as two unrelated
// errors, had anyone been checking for it at all.
enum SampleLoadResult {
    SLOAD_OK = 0,
    SLOAD_ERR_MAX_SAMPLES = 1,
    SLOAD_ERR_MAX_SOUNDFONTS = 2,
    SLOAD_ERR_INVALID_DIR = 4,
    SLOAD_ERR_INPUT_FILE = 8,
    SLOAD_ERR_OUTPUT_FILE = 16,
};

class SamplePool: public T_Singleton<SamplePool>,public Observable {
public:
  unsigned int Load();
  void Sort();
  SamplePool();
  void Reset();
  ~SamplePool();
  SoundSource *GetSource(int i);
  // where the leading run of baked sounds ends: the KIT/WAV boundary
  // measured from what the pool actually holds
  int GetBakedEnd();
  char **GetNameList();
  int GetNameListSize();
  int ImportSample(Path &path);
  bool IsImported(std::string name);
  // int InsertSample(const std::string& sampleName, bool imported, std::string fi);
  int Reassign(std::string name, bool imported);
  void PurgeSample(int i);
  const char *GetSampleLib();
protected:
  // the synthesised kit, inserted ahead of anything on disk
  void bakeDrums();

 public:
  // Boot is not instant: the kit is SYNTHESISED, twenty-four drums of
  // it, and then any wavs and soundfonts on the stick are read. On a
  // handheld that is several seconds of black screen, which reads as
  // a machine that has hung rather than one that is working. Whoever
  // owns the screen registers a callback and draws something.
  typedef void (*ProgressFn)(const char *what, int done, int total);
  static void SetProgressCallback(ProgressFn fn);
 private:
  static ProgressFn progress_;
  static void report(const char *what, int done, int total);
  void unload(int i);
  int loadSample(const char *path);
  int loadSoundFont(const char *path);
  int getIndexOf(const char *path);
  /* The baked kit is identical every time it is made, so it is made
     ONCE and kept across project loads. Without this it was baked on
     every project open and thrown away on every project close. */
  bool drumsBaked_;
  int count_;
  char *names_[MAX_PIG_SAMPLES];
  SoundSource *wav_[MAX_PIG_SAMPLES];
};

#endif
