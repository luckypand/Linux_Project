#include <sys/types.h>
#include <sys/wait.h>
 
void ChildRun()
{
    
    int cnt = 5;
    while (cnt)
    {
        printf("I am child process, pid: %d, ppid:%d, cnt: %d\n", getpid(), getppid(), cnt);
        sleep(1);
        cnt--;
     
    }
}
 
int main()
{
    int status = 0;

    printf("I am father, pid: %d, ppid:%d\n", getpid(), getppid());
 
    pid_t id = fork();
    if (id == 0)//子进程内容
    {
        ChildRun();
        printf("child quit ...\n");
        exit(0);
    }
    sleep(10);
    // fahter
    pid_t rid = waitpid(id,&status,WNOHANG);
 
    if (rid > 0)//等到了子进程结束
    {
        printf("wait success, child exit code: %d\n", WEXITSTATUS(status));
    }
    else//没等到
    {
        printf("wait failed !\n");
    }
    sleep(3);
    printf("father quit...\n");
 
}