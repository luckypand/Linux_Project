#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
 
int main()
{
	char* const argv[] = { const_cast<char*>("ls"), const_cast<char*>("-l"), NULL };
	char* const envp[] = { const_cast<char*>("PATH=/bin:/usr/bin"),const_cast<char*>("TERM=console"), NULL };
    execv("/usr/bin/ls",argv,nullptr); //p lv e
	execl("/bin/ps", "ps", "-ef", NULL);
	// 带p的，可以使用环境变量PATH，无需写全路径
	execlp("ps", "ps", "-ef", NULL);
	// 带e的，需要自己组装环境变量
	execle("ps", "ps", "-ef", NULL, envp);
	execv("/bin/ps", argv);
	// 带p的，可以使用环境变量PATH，无需写全路径
	execvp("ps", argv);
	// 带e的，需要自己组装环境变量
	execve("/bin/ps", argv, envp);
	exit(0);
}