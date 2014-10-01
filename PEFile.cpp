#include "stdafx.h"
#include "PEFile.h"
#include "MD5Sum.h"

/************************************************************************/
//
/************************************************************************/
CPEFile::CPEFile(void)
{
}


/************************************************************************/
//
/************************************************************************/
CPEFile::~CPEFile(void)
{
}

/************************************************************************/
/*                                                                      */
/************************************************************************/
std::vector<byte> * CPEFile::GetEOFSegnemt()
{
	return &_eof_dump;
}

//////////////////////////////////////////////////////////////////////////
//	exe file dump
// [IMAGE_DOS_HEADER .e_lfanew offset to the IMAGE_NT_HEADERS][DOS stub][IMAGE_NT_HEADERS]
// [IMAGE_SECTION_HEADER * IMAGE_NT_HEADERS.IMAGE_FILE_HEADER.NumberOfSections]
// [Sections * IMAGE_NT_HEADERS.IMAGE_FILE_HEADER.NumberOfSections]
//		-	offet - IMAGE_SECTION_HEADER.PointerToRawData; size -  IMAGE_SECTION_HEADER.SizeOfRawData
//	[.text]		- executable code
//	[.rdata]	- resource data
//	[.data]		- initializing data
//  [.rsec]		- resource table 
//	[nsis data]
//	[certificate table] - IMAGE_NT_HEADERS.IMAGE_OPTIONAL_HEADER.DataDirectory[4]
//				Certificate table address and size
/************************************************************************/
//	load and parsing file
/************************************************************************/
bool	CPEFile::LoadAndParseFile(char * filename)
{
	//	resize dump
	_pe_full_dump.resize(0);
	//	try open and read file
	CFile file;
	if (TRUE == file.Open(filename,CFile::modeRead,NULL))
	{
		int length = (int)file.GetLength();
		_pe_full_dump.resize(length+1);
		file.Read(&_pe_full_dump[0],length);
		file.Close();
	}
	//	check result;
	if (_pe_full_dump.size() == 0)
	{
		return false;
	}
	int off = 0;
	//	copy dos header
	memcpy(&_pe_dos_header,&_pe_full_dump[off],sizeof(_pe_dos_header));
	off +=_pe_dos_header.e_lfanew;
	//	copy ms dos stab
	_pe_msdos_stub.insert(_pe_msdos_stub.begin(),&_pe_full_dump[sizeof(_pe_dos_header)],&_pe_full_dump[off]);
	//	copy nt header
	memcpy(&_pe_nt_header,&_pe_full_dump[off],sizeof(_pe_nt_header));

	DWORD oldcrc = _pe_nt_header.OptionalHeader.CheckSum;
	_pe_nt_header.OptionalHeader.CheckSum = 0;

	memcpy(&_pe_full_dump[off],&_pe_nt_header,sizeof(_pe_nt_header));
	off+= sizeof(_pe_nt_header);

	//	copy section header
	int count = _pe_nt_header.FileHeader.NumberOfSections;
	for (int i= 0x00;i<count;i++)
	{
		IMAGE_SECTION_HEADER ish  = *(IMAGE_SECTION_HEADER*)&_pe_full_dump[off];
		off+= sizeof(IMAGE_SECTION_HEADER);
		_pe_section_headers.push_back(ish);
	}
	//	copy the section data
	unsigned soff = 0;
	unsigned ssize = 0;
	unsigned nsisoff = 0;
	for (unsigned i= 0x00;i<_pe_section_headers.size();i++)
	{
		IMAGE_SECTION_HEADER ish = _pe_section_headers[i];
		soff = ish.PointerToRawData;
		ssize = ish.SizeOfRawData;
		DWORD sname = *(DWORD*)&ish.Name[0];
		switch (sname)
		{
			// .tex
		case 0x7865742e:
			{
				_pe_dot_text_section.insert(_pe_dot_text_section.begin(),&_pe_full_dump[soff],&_pe_full_dump[soff+ssize]);
				nsisoff = max(soff+ssize,nsisoff);
			}
			break;
		case 0x6164722e:
			{
				_pe_dot_rdata_section.insert(_pe_dot_rdata_section.begin(),&_pe_full_dump[soff],&_pe_full_dump[soff+ssize]);
				nsisoff = max(soff+ssize,nsisoff);
			}
			break;
		case 0x7461642e:
			{
				_pe_dot_data_section.insert(_pe_dot_data_section.begin(),&_pe_full_dump[soff],&_pe_full_dump[soff+ssize]);
				nsisoff = max(soff+ssize,nsisoff);
			}
			break;
		case 0x7273722e:
			{
				_pe_dot_rsrc_section.insert(_pe_dot_rsrc_section.begin(),&_pe_full_dump[soff],&_pe_full_dump[soff+ssize]);
				nsisoff = max(soff+ssize,nsisoff);
			}
			break;
		case 0x6c65722e:
			{
				_pe_dot_reloc_section.insert(_pe_dot_reloc_section.begin(),&_pe_full_dump[soff],&_pe_full_dump[soff+ssize]);
				nsisoff = max(soff+ssize,nsisoff+ssize);
			}
			break;
		case 0x61646e2e:
			{
				_ndata_size = ish.Misc.VirtualSize;
				/*
				int c = ish.Misc.VirtualSize;
				c/= (NSIS_MAX_STRLEN*sizeof(WCHAR));
				_global_vars.SetVarCount(c);
				*/
			}
			break;
		default:
			break;
		}
	}

	unsigned nsissize = 0;
	//	load nsis data;
	if (_pe_nt_header.OptionalHeader.DataDirectory[4].VirtualAddress > 0)
	{
		nsissize = _pe_nt_header.OptionalHeader.DataDirectory[4].VirtualAddress - nsisoff;
	}
	else
	{
		nsissize = _pe_full_dump.size()-1-nsisoff;
	}

	_eof_dump.insert(_eof_dump.begin(),&_pe_full_dump[nsisoff],&_pe_full_dump[nsisoff+nsissize]);

	//  copy certificate table;
	soff = _pe_nt_header.OptionalHeader.DataDirectory[4].VirtualAddress;
	ssize = _pe_nt_header.OptionalHeader.DataDirectory[4].Size;
	ssize = min(ssize,_pe_full_dump.size()-1 - soff);
	_pe_certificatr_table.insert(_pe_certificatr_table.begin(),&_pe_full_dump[soff],&_pe_full_dump[soff+ssize]);

	return true;
}

/************************************************************************/
//	������ ������ ������ ��������� ��� ����������
/************************************************************************/
int CPEFile::GetNDataSize()
{
	return _ndata_size;

}

/************************************************************************/
// this is based on the (slow,small) CRC32 implementation from zlib.
/************************************************************************/
DWORD CPEFile::PE_CRC(DWORD  crc, const unsigned char *buf, unsigned int len)
{
	crc = 0;
	WORD * w = (WORD*) buf;
	unsigned  wl = len/2;

	while (wl > 0 )
	{
		crc += *w;
		w++;
		wl--;
		crc = (crc&0xffff) + (crc>>16);
	}
	crc = (crc&0xffff) + (crc>>16);
	return crc;	
}


/************************************************************************/
/*                                                                      */
/************************************************************************/
int CPEFile::GetNsisVarCount() 
{
	int c = _ndata_size;
	c/= (NSIS_MAX_STRLEN*sizeof(WCHAR));
	return c;
}
/************************************************************************/
//	save exe dump afer changes
/************************************************************************/
void CPEFile::SaveExeDump(char * filename)
{
	//	�������� �����
	std::vector<byte> _out;
	byte * p = (byte*)&_pe_dos_header;
	//	dos header
	_out.insert(_out.begin(),p,p+ sizeof(_pe_dos_header));
	//	dos stub
	p = (byte*)&_pe_msdos_stub[0];
	_out.insert(_out.begin()+_out.size(),p,p+_pe_msdos_stub.size());
	
	//	������� crc
	_pe_nt_header.OptionalHeader.CheckSum = 0x00;
	//	������ ���������.
	_pe_nt_header.OptionalHeader.DataDirectory[4].Size = 0;
	_pe_nt_header.OptionalHeader.DataDirectory[4].VirtualAddress = 0;
	//	
	p = (byte*)&_pe_nt_header;
	_out.insert(_out.begin()+_out.size(),p,p+sizeof(_pe_nt_header));

	
	//	copy section header
	int count = _pe_nt_header.FileHeader.NumberOfSections;
	for (int i= 0x00;i<count;i++)
	{
		IMAGE_SECTION_HEADER ish  = _pe_section_headers[i];
		p = (byte*)&ish;
		_out.insert(_out.begin()+_out.size(),p,p+sizeof(IMAGE_SECTION_HEADER));
	}

	//	copy the section data
	for (unsigned i= 0x00;i<_pe_section_headers.size();i++)
	{
		
		IMAGE_SECTION_HEADER ish = _pe_section_headers[i];
		DWORD sname = *(DWORD*)&ish.Name[0];
		unsigned size =0;
		unsigned offset	= ish.PointerToRawData; 

		switch (sname)
		{
			// .tex
		case 0x7865742e:
			size	= _pe_dot_text_section.size();
			p		= (byte*)&_pe_dot_text_section[0];
			break;
		case 0x6164722e:
			size	= _pe_dot_rdata_section.size();
			p		= (byte*)&_pe_dot_rdata_section[0];
			
			break;
		case 0x7461642e:
			size	= _pe_dot_data_section.size();
			p		= (byte*)&_pe_dot_data_section[0];
			break;
		case 0x7273722e:
			size	= _pe_dot_rsrc_section.size();
			p		= (byte*)&_pe_dot_rsrc_section[0];
			break;
		default:
			break;
		}

		if (size > 0 )
		{
			if (offset != _out.size())
			{
				_out.resize(offset);
				int err=1;
			}
			_out.insert(_out.begin()+_out.size(),p,p+size);
		}
	}
	// copy nsis dump
	if (_eof_dump.size()>0)
	{
		p = &_eof_dump[0];
		_out.insert(_out.begin()+_out.size(),p,p+_eof_dump.size());
	}
	
	int _crc_offset = CHECKSUM_OFFSET;
	DWORD crc = PE_CRC(0,&_out[0],_out.size());


	crc = (crc & 0xffff) + (crc >> 16);
	crc = (crc) + (crc >> 16);
	crc = crc & 0xffff;
	crc += _out.size();
	
	_pe_nt_header.OptionalHeader.CheckSum = crc;
	memcpy(&_out[_pe_dos_header.e_lfanew],&_pe_nt_header,sizeof(_pe_nt_header));

	CFile file;
	file.Open(filename,CFile::modeCreate|CFile::modeWrite,NULL);
	file.Write(&_out[0],_out.size());
	file.Close();
	return;
}

/************************************************************************/
//	get the code segment hash
/************************************************************************/
std::string CPEFile::GetCodeSegmentHash()
{
	int i=0;

	MD5Sum md5;
	std::string hash =  md5.Calculate(&_pe_dot_text_section[0],_pe_dot_text_section.size());
	return hash;
}

/************************************************************************/
//	inject the nsis code to the lzma stub
/************************************************************************/
bool CPEFile::SetEofSegnemt(std::vector<byte> *eofseg,int varcount)
{
	for (unsigned i= 0x00;i<_pe_section_headers.size();i++)
	{
		IMAGE_SECTION_HEADER ish = _pe_section_headers[i];
		DWORD sname = *(DWORD*)&ish.Name[0];
		if (sname == 0x61646e2e)
		{
			ish.Misc.VirtualSize = varcount;
			_pe_section_headers[i] = ish;
		}
	}
	_eof_dump  = * eofseg;

	return false;
}