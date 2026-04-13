#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdarg.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "my_blockqueue.hpp"
#include "my_buffer.hpp"

#define private public
#include "my_log.hpp"
#undef private

namespace
{

std::string MakeTempDir(const std::string& tag)
{
	std::ostringstream oss;
	oss << "/tmp/tinywebsocket_log_" << tag << "_" << getpid();
	std::string path = oss.str();
	mkdir(path.c_str(), 0777);
	return path;
}

std::string MakeCurrentLogFile(const std::string& dir, const std::string& suffix)
{
	time_t timer = time(nullptr);
	struct tm* systime = localtime(&timer);

	char fileName[256] = {0};
	snprintf(fileName, sizeof(fileName), "%s/%04d_%02d_%02d%s",
		dir.c_str(),
		systime->tm_year + 1900,
		systime->tm_mon + 1,
		systime->tm_mday,
		suffix.c_str());
	return fileName;
}

std::string ReadFileToString(const std::string& path)
{
	std::ifstream ifs(path.c_str(), std::ios::in | std::ios::binary);
	assert(ifs.is_open());

	std::ostringstream oss;
	oss << ifs.rdbuf();
	return oss.str();
}

void TestSyncWriteAndTitles()
{
	std::string logDir = MakeTempDir("sync");
	Log* log = Log::Instance();
	log->Init(0, logDir.c_str(), 0, ".log");

	Log_Base(LOG_DEBUG, "debug message: %d", 1);
	LOG_INFO(0, "info message: %s", "hello");
	LOG_WARN(0, "warn message: %.1f", 3.5);
	LOG_ERROR(0, "error message: %c", 'X');
	Log_Base(99, "fallback level message");
	log->flush();

	std::string content = ReadFileToString(MakeCurrentLogFile(logDir, ".log"));
	assert(content.find("[debug]: ") != std::string::npos);
	assert(content.find("[info] : ") != std::string::npos);
	assert(content.find("[warn] : ") != std::string::npos);
	assert(content.find("[error]: ") != std::string::npos);
	assert(content.find("debug message: 1") != std::string::npos);
	assert(content.find("info message: hello") != std::string::npos);
	assert(content.find("warn message: 3.5") != std::string::npos);
	assert(content.find("error message: X") != std::string::npos);
	assert(content.find("fallback level message") != std::string::npos);
}

void TestFileRotationByLineCount()
{
	std::string logDir = MakeTempDir("rotate");
	Log* log = Log::Instance();
	log->Init(0, logDir.c_str(), 0, ".log");

	LOG_INFO(0, "before rotation");
	log->flush();

	log->lineCount_ = Log::MAX_LINES;
	LOG_INFO(0, "after rotation");
	log->flush();

	time_t timer = time(nullptr);
	struct tm* systime = localtime(&timer);
	char baseFile[256] = {0};
	char rotatedFile[256] = {0};
	snprintf(baseFile, sizeof(baseFile), "%s/%04d_%02d_%02d.log",
		logDir.c_str(),
		systime->tm_year + 1900,
		systime->tm_mon + 1,
		systime->tm_mday);
	snprintf(rotatedFile, sizeof(rotatedFile), "%s/%04d_%02d_%02d-1.log",
		logDir.c_str(),
		systime->tm_year + 1900,
		systime->tm_mon + 1,
		systime->tm_mday);

	std::string firstFile = ReadFileToString(baseFile);
	std::string secondFile = ReadFileToString(rotatedFile);
	assert(firstFile.find("before rotation") != std::string::npos);
	assert(secondFile.find("after rotation") != std::string::npos);
}

} // namespace

int main()
{
	TestSyncWriteAndTitles();
	TestFileRotationByLineCount();

	std::cout << "All Log tests passed." << std::endl;
	return 0;
}
