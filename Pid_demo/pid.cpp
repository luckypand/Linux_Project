#include <stdlib.h>
#include <cstdio>
#include <sys/types.h>
#include <unistd.h>
 
// const int num = 10;
// void worker()
// {
// 	int cnt = 12;
//      while (cnt)
// 	 {
// 		printf("child %d is running,cnt:%d\n", getpid(), cnt); //返回正在执行的子进程
// 		cnt--;
// 		sleep(1);
// 	 }
// }
// int main()
// {
// 	for (int i = 0; i < num; i++)
// 	{
// 		pid_t id = fork();
// 		if (id < 0) break;
// 		if (id == 0)
// 		{
// 			//子进程
// 			worker();
// 			exit(0);//让子进程退出                           
// 		}
// 		printf("father create child process success,child pid:%d\n", id);
// 	}
// 	//只有父亲进程会执行到这里
// 	sleep(15);
// 	return 0;
// }

void worker()
{
    int cnt = 12;

    while(cnt)
    {
        cnt--;
        printf("child %d is running,cnt:%d\n", getpid(), cnt);
        sleep(1);
    }
}

int main()
{
    for(int i = 0;i < 10;i++)
    {
        pid_t id = fork();

        if(id < 0) break;
        if(id == 0)  //代表进入子进程
        {
            worker();  //创建成功执行命令
            exit(0);
        }
        printf("father create child process success,child pid:%d\n", id);
    }

    sleep(15);
    return 0;
}