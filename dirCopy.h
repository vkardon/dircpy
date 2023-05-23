//
// dirCopy.h
//
#ifndef __DIR_COPY_H__
#define __DIR_COPY_H__

#include "dirReader.h"
#include <list>
#include <mutex>
#include <condition_variable>
#include <atomic>

class DirCopy : public DirReader
{
public:
    DirCopy(int threadCount=4) : mThreadCount(threadCount) {}
    virtual ~DirCopy() = default;

    bool Copy(const std::string& srcDir, const std::string& destDir, size_t sparseBlockSize=0);

private:
    virtual void* OnDirectory(const char* dirName, const char* baseName, void* param) override;
    virtual void OnDirectoryEnd(const char* /*dirName*/, void* param) override;
    virtual void OnFile(const char* dirName, const char* baseName, void* param) override;

    bool CopyDir(const std::string& srcDir, const std::string& destDir);
    bool CopyFile(const std::string& srcFile, const std::string& destFile, bool updateProgress=false);
    void UpdateDirProgress();
    inline void SetError(const std::string& err);
    inline void Stop();

    struct DirReaderParam
    {
        std::string destDir;
    };

    struct FileInfo
    {
        FileInfo(const char* baseName, const char* srcDir, const char* destDir) :
            mBaseName(baseName), mSrcDir(srcDir), mDestDir(destDir) {}
        std::string mBaseName;
        std::string mSrcDir;
        std::string mDestDir;
    };

private:
    size_t mSparseBlockSize{0};
    int mThreadCount{0};
    std::mutex mProgressMutex;
    size_t mSavedDirAndFiles{0};
    size_t mTotalDirAndFiles{0};
    int mProgress{-1};
    bool mHasMore{true};

    std::list<FileInfo> mFileList;
    std::condition_variable mFileCv;
    std::mutex mFileMutex;
    std::mutex mErrMsgMutex;
};

#endif // __DIR_COPY_H__
