#include "my_buffer.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

namespace
{

void TestBasicAppendAndRetrieve()
{
	Buffer buffer(8);
	assert(buffer.WritableBytes() == 8);
	assert(buffer.ReadableBytes() == 0);

	buffer.Append("abc", 3);
	assert(buffer.ReadableBytes() == 3);
	assert(buffer.WritableBytes() == 5);
	assert(std::string(buffer.BeginRead(), buffer.ReadableBytes()) == "abc");

	buffer.Append(std::string("de"));
	assert(buffer.ReadableBytes() == 5);
	assert(buffer.RetrieveAllToStr() == "abcde");
	assert(buffer.ReadableBytes() == 0);
	assert(buffer.WritableBytes() == 8);
}

void TestMakeSpaceAndAppendBuffer()
{
	Buffer buffer(5);
	buffer.Append("hello", 5);
	assert(buffer.ReadableBytes() == 5);

	std::string all = buffer.RetrieveAllToStr();
	assert(all == "hello");

	buffer.Append("12", 2);
	buffer.Append("345", 3);
	assert(buffer.ReadableBytes() == 5);

	Buffer extra(4);
	extra.Append("xy", 2);
	buffer.Append(extra);

	assert(buffer.ReadableBytes() == 7);
	assert(buffer.RetrieveAllToStr() == "12345xy");
}

void TestReadFdAndWriteFd()
{
	int pipefd[2];
	int err = 0;
	assert(pipe(pipefd) == 0);

	const char* message = "socket buffer test";
	ssize_t written = write(pipefd[1], message, std::strlen(message));
	assert(written == static_cast<ssize_t>(std::strlen(message)));

	Buffer buffer(4);
	ssize_t readLen = buffer.ReadFd(pipefd[0], &err);
	assert(readLen == static_cast<ssize_t>(std::strlen(message)));
	assert(buffer.ReadableBytes() == std::strlen(message));
	assert(buffer.RetrieveAllToStr() == message);

	const char* output = "write fd";
	buffer.Append(output, std::strlen(output));
	ssize_t sendLen = buffer.WriteFd(pipefd[1], &err);
	assert(sendLen == static_cast<ssize_t>(std::strlen(output)));
	assert(buffer.ReadableBytes() == 0);

	char recvBuf[32] = {0};
	ssize_t recvLen = read(pipefd[0], recvBuf, sizeof(recvBuf));
	assert(recvLen == static_cast<ssize_t>(std::strlen(output)));
	assert(std::string(recvBuf, recvLen) == output);

	close(pipefd[0]);
	close(pipefd[1]);
}

} // namespace

int main()
{
	TestBasicAppendAndRetrieve();
	TestMakeSpaceAndAppendBuffer();
	TestReadFdAndWriteFd();

	std::cout << "All Buffer tests passed." << std::endl;
	return 0;
}
