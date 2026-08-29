#include "Config.h"
#include <stdio.h>
#include <string.h>
#include <string>


Config::Config() 
{
	Path path("bin:config.xml") ;
	Trace::Log("CONFIG","Got config path=%s",path.GetPath().c_str()) ;
	TiXmlDocument *document=new TiXmlDocument(path.GetPath());
	bool loadOkay = document->LoadFile();

	if (loadOkay) 
  { 
		// Check first node is CONFIG/ GPCONFIG

		TiXmlNode* rootnode = 0;

		rootnode = document->FirstChild( "CONFIG" );
		if (!rootnode)
    {
		   rootnode = document->FirstChild( "GPCONFIG" );
    }
    
		if (rootnode)
    {
			TiXmlElement *rootelement = rootnode->ToElement();
			TiXmlNode *node = rootelement->FirstChildElement() ;

			// Loop on all children
		
			if (node)
      {
				TiXmlElement *element = node->ToElement();
				while (element) 
        {
					const char *key=element->Value() ;
					const char *value=element->Attribute("value") ;
					if (!value)
          {
						value=element->Attribute("VALUE") ;
					}
					if (key&&value)
          {
						Variable *v=new Variable(key,0,value) ;
						Insert(v) ;
					}
					element = element->NextSiblingElement(); 
				}
			}

    }
    } else {
		Trace::Log("CONFIG","No (bad?) config.xml") ;
	}
 	delete(document) ;
}


//------------------------------------------------------------------------------

Config::~Config()
{
}


//------------------------------------------------------------------------------

const char *Config::GetValue(const char *key) 
{
	// no Trace::Log here: GetValue runs at frame rate (the song view's
	// status panel reads MIDICTRLDEVICE every repaint), and with LOG=YES
	// that was a printf + stick write 62 times a second
	Variable *v=FindVariable(key) ;
	return v?v->GetString():0 ;
} ;


//------------------------------------------------------------------------------

void Config::ProcessArguments(int argc,char **argv) 
{
	for (int i=1;i<argc;i++) {
		char *pos ;
		char *arg=argv[i] ;
		while (*arg=='-') arg++ ;
		if ((pos=strchr(arg,'='))!=0) {
			*pos=0 ;
			Variable *v=FindVariable(arg) ;
			if (v) {
				v->SetString(pos+1) ;
			} else {
				Variable *v=new Variable(arg,0,pos+1) ;
				Insert(v) ;
			}
		}
	}
} ;


//------------------------------------------------------------------------------
// config.xml write-back. Surgical on the raw text so every comment and
// the hand-tuned layout survive.

static bool inComment(const std::string &t,size_t pos) {
	size_t open=t.rfind("<!--",pos) ;
	if (open==std::string::npos) return false ;
	size_t close=t.find("-->",open) ;
	return (close==std::string::npos||close>pos) ;
}

// Rewrite key's value in place (first live, non-comment element), or add
// a value attribute if the element has none, or append a fresh element.
static void patchOption(std::string &text,const char *key,const char *val) {
	std::string tag=std::string("<")+key ;
	size_t search=0 ;
	while (true) {
		size_t p=text.find(tag,search) ;
		if (p==std::string::npos) break ;
		char nc=(p+tag.size()<text.size())?text[p+tag.size()]:'\0' ;
		bool whole=(nc==' '||nc=='\t'||nc=='/'||nc=='>'||nc=='\n'||nc=='\r') ;
		if (!whole||inComment(text,p)) { search=p+tag.size() ; continue ; }
		size_t end=text.find('>',p) ;
		if (end==std::string::npos) return ;
		std::string seg=text.substr(p,end-p) ;
		size_t vp=seg.find("value") ;
		if (vp==std::string::npos) vp=seg.find("VALUE") ;
		if (vp!=std::string::npos) {
			size_t eq=seg.find('=',vp) ;
			size_t q1=(eq==std::string::npos)?std::string::npos
			                                 :seg.find_first_of("\"'",eq) ;
			if (q1!=std::string::npos) {
				char quote=seg[q1] ;
				size_t q2=seg.find(quote,q1+1) ;
				if (q2!=std::string::npos) {
					text.replace(p+q1+1,q2-q1-1,val) ;
					return ;
				}
			}
		}
		// element present but no value attribute: add one before '>'/'/>'
		size_t ins=end ;
		if (ins>p&&text[ins-1]=='/') ins-- ;
		text.insert(ins,std::string(" value=\"")+val+"\"") ;
		return ;
	}
	// no live element for this key: append before the close tag
	size_t cend=text.find("</CONFIG>") ;
	if (cend==std::string::npos) cend=text.find("</GPCONFIG>") ;
	std::string line=std::string("\t<")+key+" value=\""+val+"\"/>\n" ;
	if (cend!=std::string::npos) text.insert(cend,line) ; else text+=line ;
}

bool Config::Save() {
	Path path("bin:config.xml") ;
	std::string full=path.GetPath() ;
	// Tiny2NosStub.h remaps FILE->I_File and fopen/fread/fseek/ftell/
	// fclose onto the FileSystem abstraction; there is no fwrite macro,
	// so the write goes through I_File::Write directly.
	FILE *fp=fopen(full.c_str(),(char*)"rb") ;
	if (!fp) { Trace::Log("CONFIG","Save: cannot read config.xml") ; return false ; }
	fseek(fp,0,SEEK_END) ; long n=ftell(fp) ; fseek(fp,0,SEEK_SET) ;
	std::string text ;
	if (n>0) { text.resize((size_t)n) ; int got=fread(&text[0],1,(int)n,fp) ; text.resize(got>0?(size_t)got:0) ; }
	fclose(fp) ;
	if (text.empty()) return false ;
	IteratorPtr<Variable> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &v=it->CurrentItem() ;
		patchOption(text,v.GetName(),v.GetString()) ;
	}
	FILE *out=fopen(full.c_str(),(char*)"wb") ;
	if (!out) { Trace::Log("CONFIG","Save: cannot write config.xml") ; return false ; }
	out->Write(text.data(),1,(int)text.size()) ;
	fclose(out) ;
	Trace::Log("CONFIG","Saved config.xml (%d bytes)",(int)text.size()) ;
	return true ;
} ;


//------------------------------------------------------------------------------
