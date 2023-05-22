//
// dirWriter.cpp
//
#include <experimental/filesystem>  // std::filesystem
#include <sys/stat.h>               // stat()
#include <string.h>                 // strerror()
#include <limits.h>                 // PATH_MAX
#include <libgen.h>                 // basename()
#include <thread>                   // std::thread
#include <iostream>                 // std::cout
#include "fileReader.h"
#include "fileWriter.h"
#include "dirCopy.h"

bool DirCopy::Copy(const std::string& srcName, const std::string& destName, size_t sparseBlockSize /*=0*/)
{
    // Are we copying file or directory?
    struct stat st;
    if(stat(srcName.c_str(), &st) < 0)
    {
        mErrMsg = "stat() failed for srcDir='" + srcName + "': " + strerror(errno);
        return false;
    }

//    std::cout << __func__ << ": From : '" << srcName << "'" << std::endl;
//    std::cout << __func__ << ": To   : '" << destName << "'" << std::endl;
//    std::cout << __func__ << ": Sparse Block : " << sparseBlockSize << " bytes" << std::endl;

    mSparseBlockSize = sparseBlockSize;
    bool res = false;

    if((st.st_mode & S_IFMT) == S_IFDIR)
    {
        // Make a destination directory (if doesn't exist)
        std::error_code err;
        std::experimental::filesystem::create_directories(destName, err);
        if(err)
        {
            mErrMsg = "Failed to make '" + destName + "' directory - " + err.message();
            return 1;
        }

        // Copy directory
        res = CopyDir(srcName, destName);
    }
    else
    {
        // Make file directory (it doesn't exist)
        char buf[PATH_MAX + 1] {};
        strcpy(buf, destName.c_str());
        std::string fileDirName = dirname(buf);

        std::error_code err;
        std::experimental::filesystem::create_directories(fileDirName, err);
        if(err)
        {
            mErrMsg = "Failed to make '" + fileDirName + "' directory - " + err.message();
            return 1;
        }

        // Copy file
        res = CopyFile(srcName, destName, true /*updateProgress*/);
    }

    return res;
}

void* DirCopy::OnDirectory(const char* dirName, const char* baseName, void* param)
{
    DirReaderParam* parentDirParam = (DirReaderParam*)param;
    std::string destDir = parentDirParam->destDir + "/" + baseName;

//    std::cout << __func__ << ": srcDir=" << dirName << "/" << baseName << "/" << std::endl;
//    std::cout << __func__ << ": destDir=" << destDir << "/" << std::endl;
//    std::cout << std::endl;

    //std::cout << destDir << "/" << std::endl;

    // Update total Dir/Files count
    {
        std::unique_lock<std::mutex> lock(mProgressMutex);
        mTotalDirAndFiles++;
    }

    // Make destination directory
    std::error_code error;
    std::experimental::filesystem::create_directories(destDir, error);
    if(error)
    {
        mAbort = true;
        SetError("Failed to make '" + destDir + "' directory - " + error.message());
        return nullptr;
    }

    // Update saved Dir/Files count and report overall progress
    UpdateProgress();

    // We have new directory now. Create corresponding DirReaderParam
    DirReaderParam* dirParam = new (std::nothrow) DirReaderParam;
    if(!dirParam)
    {
        mAbort = true;
        SetError("Out of memory creating DirReaderParam");
        return nullptr;
    }
    dirParam->destDir = std::move(destDir);

    return dirParam;
}

void DirCopy::OnDirectoryEnd(const char* /*dirName*/, void* param)
{
    if(param)
        delete (DirReaderParam*)param;
}

void DirCopy::OnFile(const char* dirName, const char* baseName, void* param)
{
    DirReaderParam* dirParam = (DirReaderParam*)param;
//    std::string destFile = dirParam->destDir + "/" + baseName;
//    std::string srcFile = std::string(dirName) + "/" + baseName;

//    std::cout << __func__ << ": srcFile=" << srcFile << std::endl;
//    std::cout << __func__ << ": destFile=" << destFile << std::endl;
//    std::cout << std::endl;

    mTotalDirAndFiles++;

    std::unique_lock<std::mutex> lock(mFileMutex);
    mFileList.emplace_back(baseName, dirName, dirParam->destDir.c_str());
    mFileCv.notify_one();
}

bool DirCopy::CopyDir(const std::string& srcDir, const std::string& destDir)
{
    // Start worker threads
    std::vector<std::thread> threads(mThreadCount);

    for(std::thread& thread : threads)
    {
        thread = std::thread([&]()
        {
            while(mHasMore)
            {
                // Wait for a "new file" notification
                std::unique_lock<std::mutex> lock(mFileMutex);
                while(mFileList.empty() && mHasMore)
                    mFileCv.wait(lock);

                // Pop the front element
                if(!mFileList.empty())
                {
                    FileInfo fi = mFileList.front();
                    mFileList.pop_front();
//                    std::cout << __func__ << ": >>> Got srcPath=" << p.first << std::endl;
                    lock.unlock(); // Don't hold the lock so other threads can proceed

                    // Process here...
                    std::string srcPath = fi.mSrcDir + "/" + fi.mBaseName;
                    std::string destPath = fi.mDestDir + "/" + fi.mBaseName;

                    if(!CopyFile(srcPath, destPath))
                    {
                        Stop(); // Force other threads to stop
                        break;
                    }

                    // Update saved Dir/Files count and report overall progress
                    UpdateProgress();

                    // Are we done?
                    if(mSavedDirAndFiles == mTotalDirAndFiles)
                    {
                        Stop();  // Force other threads to stop
                        break;
                    }
                }
            }
        }); // End of thread lambda
    }

    // Read directory
    mProgress = -1; // Don't report progress until we get complete mSavedDirAndFiles
    DirReaderParam dirParam { destDir };
    if(!Read(srcDir, &dirParam))
        Stop(); // Force threads to stop

    // Done reading directory, mSavedDirAndFiles is at its max.
    // Worker threads are still running but we can star report progress
    {
        std::unique_lock<std::mutex> lock(mProgressMutex);
        mProgress = 0;
    }

    //  Wait for worker threads to complete...
    for(std::thread& thread : threads)
    {
        thread.join();
    }
    threads.clear();

    // We should only have errors if we failed
    return mErrMsg.empty();
}

bool DirCopy::CopyFile(const std::string& srcFile, const std::string& destFile, bool updateProgress/*=false*/)
{
//    std::cout << __func__ << ": srcFile=" << srcFile << std::endl;
//    std::cout << __func__ << ": destFile=" << destFile << std::endl;
//    std::cout << __func__ << ": sparseBlockSize=" << sparseBlockSize << std::endl;
//    std::cout << std::endl;

    FileReader reader;
    if(!reader.OpenFile(srcFile))
    {
        SetError("FileReader error '" + reader.GetError() + "'");
        return false;
    }
    reader.SetSparseBlockSize(mSparseBlockSize);

    FileWriter writer;
    if(!writer.OpenFile(destFile))
    {
        SetError("FileWriter error '" + writer.GetError() + "'");
        return false;
    }

    // Read in maxReadSize chanks
    //static constexpr int maxReadSize = 1024 * 1024 * 3; // 3MB
    static constexpr int maxReadSize = 1024 * 128; // 128KB
    std::string buf;
    bool sparse = true;

    while(reader.HasMore())
    {
        // Read source file
        off_t dataOffset = reader.ReadFile(buf, maxReadSize, sparse);
//        size_t thisRead = buf.size();

        // Write destination file
        //size_t thisWritten = (sparse ? writer.WriteFile(buf, dataOffset) : writer.WriteFile(buf));
        /*size_t thisWritten =*/ writer.WriteFile(buf, dataOffset);
        if(!writer.IsValid())
        {
            SetError("FileWriter error '" + writer.GetError() + "'");
            return false;
        }

//        std::cout << __func__ << ": Offset=" << dataOffset << ": read " << thisRead << ", written " << thisWritten << std::endl;

        // Update file reading/writing progress
        if(updateProgress)
        {
            int progress = (int)(100 * reader.GetReadSize() / reader.GetFileSize());
            if(progress != mProgress)
            {
                mProgress = progress;
                std::cout << progress << '%' << (mProgress == 100 ? '\n' : ' ') << std::flush;
            }
        }
    }

    //std::cout << __func__ << ": Read  total: " << reader.GetReadSize() << std::endl;
    //std::cout << __func__ << ": Write total: " << writer.GetFileSize() << std::endl;

    return true;
}

void DirCopy::UpdateProgress()
{
    std::unique_lock<std::mutex> lock(mProgressMutex);

    mSavedDirAndFiles++;

    if(mProgress < 0)
        return;

    int progress = (int)(100 * mSavedDirAndFiles / mTotalDirAndFiles);

    if(progress != mProgress)
    {
        mProgress = progress;
        std::cout << progress << '%' << (mProgress == 100 ? '\n' : ' ') << std::flush;
    }
}

void DirCopy::SetError(const std::string& err)
{
    // Note: we only set the first error as most relevant
    std::unique_lock<std::mutex> lock(mErrMsgMutex);
    if(mErrMsg.empty())
        mErrMsg = err;
}

void DirCopy::Stop()
{
    std::unique_lock<std::mutex> lock(mFileMutex);
    mHasMore = false;
    mFileCv.notify_all();
}

