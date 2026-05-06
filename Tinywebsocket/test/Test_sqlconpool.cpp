#include "my_sqlconpool.hpp"

#include <cerrno>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

struct MysqlTestConfig
{
	std::string host;
	int port;
	std::string user;
	std::string pwd;
	std::string dbName;
	int connSize;
};

std::string ReadEnvOrDefault(const char* name, const std::string& fallback)
{
	const char* value = std::getenv(name);
	return value ? value : fallback;
}

int ReadEnvIntOrDefault(const char* name, int fallback)
{
	const char* value = std::getenv(name);
	if(!value || *value == '\0')
	{
		return fallback;
	}

	char* end = nullptr;
	errno = 0;
	long parsed = std::strtol(value, &end, 10);
	if(errno != 0 || end == value || *end != '\0' || parsed <= 0)
	{
		return fallback;
	}
	return static_cast<int>(parsed);
}

MysqlTestConfig LoadMysqlTestConfig()
{
	MysqlTestConfig config;
	config.host = ReadEnvOrDefault("MYSQL_TEST_HOST", "127.0.0.1");
	config.port = ReadEnvIntOrDefault("MYSQL_TEST_PORT", 3306);
	config.user = ReadEnvOrDefault("MYSQL_TEST_USER", "root");
	config.pwd = ReadEnvOrDefault("MYSQL_TEST_PWD", "");
	config.dbName = ReadEnvOrDefault("MYSQL_TEST_DB", "test");
	config.connSize = ReadEnvIntOrDefault("MYSQL_TEST_POOL_SIZE", 1);
	return config;
}

bool CanOpenDirectMysqlConnection(const MysqlTestConfig& config)
{
	MYSQL* conn = mysql_init(nullptr);
	if(!conn)
	{
		return false;
	}

	MYSQL* connected = mysql_real_connect(conn,
		config.host.c_str(),
		config.user.c_str(),
		config.pwd.c_str(),
		config.dbName.c_str(),
		config.port,
		nullptr,
		0);
	if(!connected)
	{
		mysql_close(conn);
		return false;
	}

	mysql_close(connected);
	return true;
}

void TestSingletonInstance()
{
	// 测试内容：验证连接池是单例，重复获取实例应当得到同一个对象地址。
	Sqlconpool& first = Sqlconpool::Instance();
	Sqlconpool& second = Sqlconpool::Instance();
	assert(&first == &second);
}

void TestInitAndBorrowReturn(Sqlconpool& pool, const MysqlTestConfig& config)
{
	// 测试内容：验证 Init 会创建指定数量的连接，并且 Getconnect / Freeconnect 能正确改变剩余数量。
	pool.Init(config.host.c_str(), config.port,
		config.user.c_str(), config.pwd.c_str(),
		config.dbName.c_str(), config.connSize);

	assert(pool.GetFreeconnet() == static_cast<size_t>(config.connSize));

	MYSQL* conn = pool.Getconnect();
	assert(conn != nullptr);
	assert(pool.GetFreeconnet() == static_cast<size_t>(config.connSize - 1));

	pool.Freeconnect(conn);
	assert(pool.GetFreeconnet() == static_cast<size_t>(config.connSize));
}

void TestRAIIReturnsConnection(Sqlconpool& pool, const MysqlTestConfig& config)
{
	// 测试内容：验证 SqlconRAII 在作用域结束时会自动把连接归还到连接池。
	MYSQL* conn = nullptr;
	{
		SqlconRAII guard(conn, &pool);
		assert(conn != nullptr);
		assert(pool.GetFreeconnet() == static_cast<size_t>(config.connSize - 1));
	}
	assert(pool.GetFreeconnet() == static_cast<size_t>(config.connSize));
}

void TestClosePool(Sqlconpool& pool)
{
	// 测试内容：验证 ClosePool 能正常关闭池中的连接并清理 MySQL 库资源。
	pool.ClosePool();
}

} // namespace

int main()
{
	const MysqlTestConfig config = LoadMysqlTestConfig();
	if(!CanOpenDirectMysqlConnection(config))
	{
		std::cout << "Skip Sqlconpool tests: MySQL server is not reachable with current config." << std::endl;
		std::cout << "Set MYSQL_TEST_HOST, MYSQL_TEST_PORT, MYSQL_TEST_USER, MYSQL_TEST_PWD, MYSQL_TEST_DB, MYSQL_TEST_POOL_SIZE to run the tests." << std::endl;
		return 0;
	}

	Sqlconpool& pool = Sqlconpool::Instance();
	TestSingletonInstance();
	TestInitAndBorrowReturn(pool, config);
	TestRAIIReturnsConnection(pool, config);
	TestClosePool(pool);

	std::cout << "All Sqlconpool tests passed." << std::endl;
	return 0;
}
