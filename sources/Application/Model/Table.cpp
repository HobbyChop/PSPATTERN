#include "Table.h"
#include "Application/Persistency/Checksum.h"

#include "System/System/System.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Utils/HexBuffers.h"
#include "Application/Utils/char.h"
#include "Song.h"

Table::Table() {
	Reset() ;
} ;

void Table::Reset() {
	SYS_MEMSET(cmd1_,'-',sizeof(cmd1_[0])*TABLE_STEPS) ;
	SYS_MEMSET(param1_,0,sizeof(param1_[0])*TABLE_STEPS) ;
	SYS_MEMSET(cmd2_,'-',sizeof(cmd2_[0])*TABLE_STEPS) ;
	SYS_MEMSET(param2_,0,sizeof(param2_[0])*TABLE_STEPS) ;
	SYS_MEMSET(cmd3_,'-',sizeof(cmd3_[0])*TABLE_STEPS) ;
	SYS_MEMSET(param3_,0,sizeof(param3_[0])*TABLE_STEPS) ;
	SYS_MEMSET(transpose_,0,sizeof(transpose_[0])*TABLE_STEPS) ;
} ;

void Table::Copy(const Table &other) {
	SYS_MEMCPY(cmd1_,other.cmd1_,sizeof(cmd1_[0])*TABLE_STEPS) ;
	SYS_MEMCPY(param1_,other.param1_,sizeof(param1_[0])*TABLE_STEPS) ;
	SYS_MEMCPY(cmd2_,other.cmd2_,sizeof(cmd2_[0])*TABLE_STEPS) ;
	SYS_MEMCPY(param2_,other.param2_,sizeof(param2_[0])*TABLE_STEPS) ;
	SYS_MEMCPY(cmd3_,other.cmd3_,sizeof(cmd3_[0])*TABLE_STEPS) ;
	SYS_MEMCPY(param3_,other.param3_,sizeof(param3_[0])*TABLE_STEPS) ;
	SYS_MEMCPY(transpose_,other.transpose_,sizeof(transpose_[0])*TABLE_STEPS) ;
} ;

bool Table::IsEmpty() {

	for (int i=0;i<TABLE_STEPS;i++) {
		if (cmd1_[i]!=I_CMD_NONE) {
			return false ;
		} ;
		if (cmd2_[i]!=I_CMD_NONE) {
			return false ;
		} ;
		if (cmd3_[i]!=I_CMD_NONE) {
			return false ;
		} ;
		if (param1_[i]!=0) {
			return false ;
		} ;
		if (param2_[i]!=0) {
			return false ;
		} ;
		if (param3_[i]!=0) {
			return false ;
		} ;
		if (transpose_[i]!=0) {
			return false ;
		} ;
	}
	return true ;
}
//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////

TableHolder::TableHolder():Persistent("TABLES") {
	Reset() ;
}

void TableHolder::Reset()  {
	/* Every table, not the first eight.
	   
	   This reset table_[i] for i under SONG_CHANNEL_COUNT while
	   marking all TABLE_COUNT of them unallocated, so opening a
	   project left tables 08 to 7F holding whatever the last project
	   had in them -- invisible until something allocated one and
	   found it already full of somebody else's commands. */
	for (int i=0;i<TABLE_COUNT;i++) {
		table_[i].Reset() ;
	}
	for (int i=0;i<TABLE_COUNT;i++) {
		allocation_[i]=false ;
	} ;
} ;

Table &TableHolder::GetTable(int table) {
	NAssert((table>=0)&&(table<TABLE_COUNT)) ;
	return table_[table] ;
}

unsigned int TableHolder::Checksum(unsigned int h) {
	// Table is plain arrays with no padding of consequence, but hash
	// the members rather than the struct so a later field can't be
	// silently left out of the sum.
	for (int i=0;i<TABLE_COUNT;i++) {
		Table &t=table_[i] ;
		h=checksumBytes(h,t.cmd1_,sizeof(t.cmd1_)) ;
		h=checksumBytes(h,t.param1_,sizeof(t.param1_)) ;
		h=checksumBytes(h,t.cmd2_,sizeof(t.cmd2_)) ;
		h=checksumBytes(h,t.param2_,sizeof(t.param2_)) ;
		h=checksumBytes(h,t.cmd3_,sizeof(t.cmd3_)) ;
		h=checksumBytes(h,t.param3_,sizeof(t.param3_)) ;
		h=checksumBytes(h,t.transpose_,sizeof(t.transpose_)) ;
	}
	return h ;
} ;

void TableHolder::SaveContent(TiXmlNode *node) {

	char hex[3] ;
	for (int i=0;i<TABLE_COUNT;i++) {
		TiXmlElement data("TABLE") ;
		hex2char(i,hex) ;
		data.SetAttribute("ID",hex) ;

		Table &table=table_[i] ;
		if (!table.IsEmpty()) {
			TiXmlNode *dataNode=node->InsertEndChild(data) ;
			for (int i=0; i<16; i++)
			{
				table.param1_[i] = Swap16(table.param1_[i]);
				table.param2_[i] = Swap16(table.param2_[i]);
				table.param3_[i] = Swap16(table.param3_[i]);
			}
			saveHexBuffer(dataNode,"CMD1",table.cmd1_,TABLE_STEPS) ;
			saveHexBuffer(dataNode,"PARAM1",table.param1_,TABLE_STEPS) ;
			saveHexBuffer(dataNode,"CMD2",table.cmd2_,TABLE_STEPS) ;
			saveHexBuffer(dataNode,"PARAM2",table.param2_,TABLE_STEPS) ;
			saveHexBuffer(dataNode,"CMD3",table.cmd3_,TABLE_STEPS) ;
			saveHexBuffer(dataNode,"PARAM3",table.param3_,TABLE_STEPS) ;
			saveHexBuffer(dataNode,"TRSP",
			              (unsigned char *)table.transpose_,TABLE_STEPS) ;
		}
	}
} ;

void TableHolder::RestoreContent(TiXmlElement *element) {

	TiXmlElement *current=element->FirstChildElement() ;
	while (current) {

		// Check it is an table
		
		if (!strcmp(current->Value(),"TABLE")) {

			// Get the table ID
			
			const char* hexid=current->Attribute("ID") ;
			// ID is file data: missing it used to be a null deref, and a
			// value past TABLE_COUNT wrote off the end of the array
			if ((!hexid)||(!hexid[0])||(!hexid[1])) {
				current=current->NextSiblingElement() ;
				continue ;
			}
			unsigned char b1=(c2h__(hexid[0]))<<4 ;
			unsigned char b2=c2h__(hexid[1]) ;
			unsigned char id=b1+b2 ;
			if (id>=TABLE_COUNT) {
				current=current->NextSiblingElement() ;
				continue ;
			}

			Table &table=table_[id] ;

			TiXmlElement *sub=current->FirstChildElement() ;
			while(sub) {
				const char *value=sub->Value() ;
				if (!strcmp("CMD1",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.cmd1_,sizeof(table.cmd1_)) ;
				} ;
				if (!strcmp("PARAM1",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.param1_,sizeof(table.param1_)) ;
				} ;
				if (!strcmp("CMD2",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.cmd2_,sizeof(table.cmd2_)) ;
				} ;
				if (!strcmp("PARAM2",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.param2_,sizeof(table.param2_)) ;
				} ;
				if (!strcmp("CMD3",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.cmd3_,sizeof(table.cmd3_)) ;
				} ;
				if (!strcmp("PARAM3",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.param3_,sizeof(table.param3_)) ;
				} ;
				if (!strcmp("TRSP",value)) {
					restoreHexBuffer(sub,(unsigned char *)table.transpose_,sizeof(table.transpose_)) ;
				} ;
				
				for (int i=0; i<16; i++)
				{
					table.param1_[i] = Swap16(table.param1_[i]);
					table.param2_[i] = Swap16(table.param2_[i]);
					table.param3_[i] = Swap16(table.param3_[i]);
				}

				
				sub=sub->NextSiblingElement() ;
			}
			allocation_[id]=!table.IsEmpty() ;
		}
		current=current->NextSiblingElement() ;
	} ;
}

void TableHolder::SetUsed(int i) {
	if (i>=TABLE_COUNT) {
		NAssert(i<128) ;
	}
	allocation_[i]=true ;
} ;

int TableHolder::GetNext() {
	for (int i=0;i<TABLE_COUNT;i++) {
		if (!allocation_[i]) {
			if (table_[i].IsEmpty()) {
				allocation_[i]=true ;
				return i ;
			}
		} ;
	} ;
	return NO_MORE_TABLE ;
} ;

int TableHolder::Clone(int table) {
	// Every caller passes a number the user or the file can reach: a
	// TABL parameter is a full ushort, and the instrument screen clones
	// whatever field happens to be focused, cutoff sliders included.
	if ((table<0)||(table>=TABLE_COUNT)) return NO_MORE_TABLE ;
	int target=GetNext() ;
	if (target!=NO_MORE_TABLE) {
		table_[target].Copy(table_[table]) ;
	} ;
	return target ;
} ;