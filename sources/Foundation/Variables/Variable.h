#ifndef _VARIABLE_H_
#define _VARIABLE_H_

#include "Foundation/Types/Types.h"

#define VAR_OFF -1
#include <string>

class Variable {
  public:
    void SetUnmatchedIndex(int i) { unmatchedIndex_ = i; }

public:
	enum Type {
		INT,
		FLOAT,
		BOOL,
		CHAR_LIST,
		STRING
	}  ;
	
public:
  Variable(const char *name, FourCC id, int value = 0, int max = 0);
  Variable(const char *name, FourCC id, float value = 0.0f);
  Variable(const char *name, FourCC id, bool value = false);
  Variable(const char *name, FourCC id, const char *value = 0);
  Variable(const char *name, FourCC id, char **list, int size, int index = -1);
  Variable(const char *name, FourCC id, const char *const *list, int size,
           int index = -1);

  virtual ~Variable();

  FourCC GetID();
  const char *GetName();

  Type GetType();
  void SetInt(int value, bool notify = true);
  int GetInt();
  void SetFloat(float value, bool notify = true);
  float GetFloat();
  void SetString(const char *string, bool notify = true);
  const char *GetString();
  void SetBool(bool value, bool notify = true);
  bool GetBool();
  void CopyFrom(Variable &other);
  // Not very clean !
  int GetListSize();
  char **GetListPointer();
  void Reset();
  static const int MAX_NAME_LENGTH = 25;

protected:
	virtual void onChange() {} ;

	std::string name_ ;
	FourCC id_;
	Type type_ ;
	
	union {
		int int_ ;
		float float_ ;
		bool bool_ ;
		int index_ ;
	} value_ ;

    union {
        int int_ ;
		float float_ ;
		bool bool_ ;
		int index_ ;
    } maxValue_;

    union {
        int int_ ;
		float float_ ;
		bool bool_ ;
		int index_ ;
    } defaultValue_;

    union {
        char **char_ ;
    } list_;

    std::string stringValue_;
    // A CHAR_LIST value from the project file that matched nothing in the
    // list -- a sample that is not loaded, an enum from a newer build.
    // Kept so GetString() writes the ORIGINAL name back out instead of
    // silently rewriting the project with whatever index 0 happens to be.
    std::string unmatched_;
    std::string stringDefaultValue_ ;

    int listSize_;
    // index used when a CHAR_LIST value resolves to nothing. 0 keeps enums
    // safe (they index fixed arrays); the sample variable sets -1, which
    // its instrument already understands as "no sample".
    int unmatchedIndex_;

    char string_[40];
} ;
#endif

