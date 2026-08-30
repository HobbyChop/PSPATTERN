#include "SynthPresets.h"
#include "I_Instrument.h"
#include "Foundation/Variables/Variable.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Console/Trace.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

namespace SynthPresets {

static std::vector<std::string> names_ ;   // display names, no extension
static std::vector<std::string> files_ ;   // filenames as the card returns them

// FAT gives 8.3-length names back UPPERCASE (FM-BELL.PTX) while longer
// ones keep their stored case -- so the extension strip and the reopen
// must both be case-blind, and the file is opened by the exact name the
// listing produced, never by a rebuilt one.
static bool hasPtxExt(const std::string &n) {
	if (n.size()<4) return false ;
	const char *e=n.c_str()+n.size()-4 ;
	return e[0]=='.'&&tolower(e[1])=='p'&&tolower(e[2])=='t'&&tolower(e[3])=='x' ;
}

// song bindings are not sound: a preset must not drag a table along
static bool excluded(const char *name) {
	return (!strcmp(name,"table"))||(!strcmp(name,"table automation")) ;
}

static std::string presetPath(const char *name) {
	Path p("bin:presets") ;
	return p.GetPath()+"/"+name+".ptx" ;
}

int Scan() {
	names_.clear() ;
	files_.clear() ;
	std::vector<std::pair<std::string,std::string> > found ;
	Path dir("bin:presets") ;
	I_Dir *d=FileSystem::GetInstance()->Open(dir.GetPath().c_str()) ;
	if (d) {
		d->GetContent((char *)"*.ptx") ;
		IteratorPtr<Path> it(d->GetIterator()) ;
		for (it->Begin();!it->IsDone();it->Next()) {
			Path &cur=it->CurrentItem() ;
			if (!cur.IsFile()) continue ;
			std::string f=cur.GetName() ;
			if (!hasPtxExt(f)) continue ;
			std::string n=f.substr(0,f.size()-4) ;
			if (!n.empty()) found.push_back(std::make_pair(n,f)) ;
		}
		delete d ;
	}
	std::sort(found.begin(),found.end()) ;
	for (size_t i=0;i<found.size();i++) {
		names_.push_back(found[i].first) ;
		files_.push_back(found[i].second) ;
	}
	return (int)names_.size() ;
}

int Count() { return (int)names_.size() ; }

const char *Name(int i) {
	if (i<0||i>=(int)names_.size()) return "?" ;
	return names_[i].c_str() ;
}

bool ReadPreset(int i, ParamSnapshot &out) {
	out.clear() ;
	if (i<0||i>=(int)names_.size()) return false ;
	Path dir("bin:presets") ;
	std::string full=dir.GetPath()+"/"+files_[i] ;
	I_File *f=FileSystem::GetInstance()->Open(full.c_str(),(char *)"r") ;
	if (!f) return false ;
	f->Seek(0,SEEK_END) ; long n=f->Tell() ; f->Seek(0,SEEK_SET) ;
	if (n<=0||n>16384) { f->Close() ; delete f ; return false ; }
	std::string text ; text.resize((size_t)n) ;
	int got=f->Read(&text[0],1,(int)n) ;
	f->Close() ; delete f ;
	if (got<=0) return false ;
	text.resize((size_t)got) ;
	size_t pos=0 ;
	while (pos<text.size()) {
		size_t eol=text.find('\n',pos) ;
		if (eol==std::string::npos) eol=text.size() ;
		std::string line=text.substr(pos,eol-pos) ;
		pos=eol+1 ;
		if (!line.empty()&&line[line.size()-1]=='\r')
			line.erase(line.size()-1) ;
		size_t eq=line.find('=') ;
		if (eq==std::string::npos||eq==0) continue ;
		std::string key=line.substr(0,eq) ;
		if (excluded(key.c_str())) continue ;
		out.push_back(std::make_pair(key,line.substr(eq+1))) ;
	}
	return !out.empty() ;
}

bool Load(int i, I_Instrument *instr) {
	if (!instr||i<0||i>=(int)names_.size()) return false ;
	Path dir("bin:presets") ;
	std::string full=dir.GetPath()+"/"+files_[i] ;
	I_File *f=FileSystem::GetInstance()->Open(full.c_str(),(char *)"r") ;
	if (!f) return false ;
	f->Seek(0,SEEK_END) ; long n=f->Tell() ; f->Seek(0,SEEK_SET) ;
	if (n<=0||n>16384) { f->Close() ; delete f ; return false ; }
	std::string text ; text.resize((size_t)n) ;
	int got=f->Read(&text[0],1,(int)n) ;
	f->Close() ; delete f ;
	if (got<=0) return false ;
	text.resize((size_t)got) ;

	/* two passes: the engine line first, so its rebuild of meaning
	   (which wave list applies, which rows exist) happens before the
	   rest of the values land on it */
	for (int pass=0;pass<2;pass++) {
		size_t pos=0 ;
		while (pos<text.size()) {
			size_t eol=text.find('\n',pos) ;
			if (eol==std::string::npos) eol=text.size() ;
			std::string line=text.substr(pos,eol-pos) ;
			pos=eol+1 ;
			if (!line.empty()&&line[line.size()-1]=='\r')
				line.erase(line.size()-1) ;
			size_t eq=line.find('=') ;
			if (eq==std::string::npos||eq==0) continue ;
			std::string key=line.substr(0,eq) ;
			std::string val=line.substr(eq+1) ;
			bool isEngine=(key=="engine") ;
			if ((pass==0)!=isEngine) continue ;
			if (excluded(key.c_str())) continue ;
			Variable *v=instr->FindVariable(key.c_str()) ;
			if (v) v->SetString(val.c_str()) ;
		}
	}
	return true ;
}

bool Save(const char *name, I_Instrument *instr) {
	if (!instr||!name||!name[0]) return false ;
	Path dir("bin:presets") ;
	FileSystem::GetInstance()->MakeDir(dir.GetPath().c_str()) ;
	std::string full=presetPath(name) ;
	I_File *f=FileSystem::GetInstance()->Open(full.c_str(),(char *)"w") ;
	if (!f) { Trace::Log("PRESET","cannot write %s",full.c_str()) ; return false ; }
	IteratorPtr<Variable> it(instr->GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &v=it->CurrentItem() ;
		if (excluded(v.GetName())) continue ;
		char line[128] ;
		int len=snprintf(line,sizeof(line),"%s=%s\n",v.GetName(),v.GetString()) ;
		if (len>0) f->Write(line,1,len) ;
	}
	f->Close() ; delete f ;
	return true ;
}

void Capture(I_Instrument *instr, ParamSnapshot &out) {
	out.clear() ;
	if (!instr) return ;
	IteratorPtr<Variable> it(instr->GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &v=it->CurrentItem() ;
		if (excluded(v.GetName())) continue ;
		out.push_back(std::make_pair(std::string(v.GetName()),
		                             std::string(v.GetString()))) ;
	}
}

void Restore(const ParamSnapshot &snap, I_Instrument *instr) {
	if (!instr) return ;
	// engine first, same reason as Load: it decides what the rest mean
	for (int pass=0;pass<2;pass++) {
		for (size_t i=0;i<snap.size();i++) {
			bool isEngine=(snap[i].first=="engine") ;
			if ((pass==0)!=isEngine) continue ;
			Variable *v=instr->FindVariable(snap[i].first.c_str()) ;
			if (v) v->SetString(snap[i].second.c_str()) ;
		}
	}
}

} // namespace SynthPresets
