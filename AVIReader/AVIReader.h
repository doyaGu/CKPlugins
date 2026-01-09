#include "CKMovieReader.h"

#include "Windows.h"
#include "vfw.h"

#define AVI_READER_VERSION 0x00000001
#define AVI_READER_GUID CKGUID(0x67541bfe, 0x75e510c0)

struct AVIMovieProperties : public CKMovieProperties
{
	// Ctor
	AVIMovieProperties()
	{
		m_Size = sizeof(AVIMovieProperties);
	}
};

/*************************************************
 The AVIReader class is derived from CKMovieReader.
 + It overloads the OpenFile method that will be used
 to load a movie file.
 +
**************************************************/
class AVIReader : public CKMovieReader
{
public:
	AVIReader();
	~AVIReader();

	//-------------------------
	// Called by the engine when the reader is not
	// needed any more
	void Release() { delete this; }

	void ReleaseAVI();

	//-----------------------------------
	// Reader Properties
	CKPluginInfo *GetReaderInfo();

	//-----------------------------------
	// There is no specific options for this reader
	// as it can not save any file...
	int GetOptionsCount() { return 0; }
	CKSTRING GetOptionDescription(int i) { return NULL; }

	//-----------------------------------
	// Movie Reader Capabilities, in our case the reader is only able to
	// load file from a local disk
	virtual CK_DATAREADER_FLAGS GetFlags() { return CK_DATAREADER_FILELOAD; }

	// Frame Count
	virtual int GetMovieFrameCount();
	// Length in ms
	virtual int GetMovieLength();

	//-----------------------------------
	// Synchronous Reading from file or URL
	virtual CKERROR OpenFile(char *name);
	// Decode a frame of the movie
	virtual CKERROR ReadFrame(int f, CKMovieProperties **);

	//----------------------------------------
	// Not implemented methods

	// Synchronous Reading from memory (Not implemented)
	virtual CKERROR OpenMemory(char *name) { return CKERR_NOTIMPLEMENTED; }
	// ASynchronous Reading from file or URL (Not implemented)
	virtual CKERROR OpenAsynchronousFile(char *name) { return CKERR_NOTIMPLEMENTED; }

protected:
	// Properties
	AVIMovieProperties m_Properties;
	PAVISTREAM m_Stream;
	PGETFRAME m_Frame;
	int m_FrameCount;
};