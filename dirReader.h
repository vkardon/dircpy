//
// dirReader.hpp
//
#ifndef __DIR_READER_H__
#define __DIR_READER_H__

#include <string>

class DirReader
{
public:
    DirReader() = default;
    virtual ~DirReader() = default;

    bool Read(const char* dirName, void* param);
    bool Read(const std::string& dirName, void* param) { return Read(dirName.c_str(), param); }
    void Abort(const std::string& errMsg) { mAbort = true; mErrMsg = errMsg; }
    const std::string& GetError() { return mErrMsg; }

    virtual void* OnDirectory(const char* dirName, const char* baseName, void* param) = 0;
    virtual void OnDirectoryEnd(const char* dirName, void* param) = 0;
    virtual void OnFile(const char* dirName, const char* baseName, void* param) = 0;

protected:
    bool mAbort{false};
    std::string mErrMsg;
};


#endif // __DIR_READER_H__

