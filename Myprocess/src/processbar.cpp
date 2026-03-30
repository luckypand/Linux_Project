#include "processbar.h"
#include<string.h>
#include<unistd.h>
#define SIZE 101  //留末尾一个'/0'
#define STYLE '='

static const char* lable = "|/-\\" ;
static int int_rate = 0;
static int previous_rate = 0;
static int lable_index = 0;
int first_flag = 1;
static char processbuffer[SIZE];

void FlashaProcess(double total,double curr) //单次的进度条，循环在主函数中
{
    if(curr >= total)
    {
        curr = total;
    }
    double rate = curr / total * 100;  //进度条换算
    int_rate = (int) rate;
    if(first_flag) //第一次调用processbuffer
    {
        memset(processbuffer,'\0',sizeof(processbuffer));
        first_flag = 0;        
    }
    for(int i = previous_rate;i <= int_rate;i++)  //记录上次下载的地方，继续添加进度条
    {
        processbuffer[i] = STYLE;
    }
    previous_rate = int_rate;

    printf("[%-100s][%.1lf%%][%c]\r",processbuffer,rate,lable[(lable_index++) % strlen(lable)]);//打印进度条
    fflush(stdout);
    if(curr == total)
    {
        printf("\n");
    }
}

void Process()
{
    //const char* lable = "|/-\\" ; //（\ 需转义为 \\）
    int len = strlen(lable);
    char processbuff[SIZE];
    memset(processbuff,'\0',SIZE * sizeof(char));

    int cnt = 0;
    while(cnt <= 100)
    {
        printf("[%-100s][%d%%][%c]\r",processbuff,cnt,lable[cnt % len]);
        fflush(stdout);
        processbuff[cnt++] = '=';
        usleep(10000);
    }
    printf("\n");
    printf("Process is end");
    printf("\n");
}