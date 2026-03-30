#include "processbar.h"
#include<string.h>
#include<unistd.h>
#define SIZE 101
#define STYLE '='

void FlashProcess(double total,double curr)
{
   if(curr>total)
      curr=total;
   double rate=curr/total*100;//1024.0/512.0 -> 0.5*100=50.0
   int cnt=(int)rate;//50.8, 49.9 -> 50,49
   char processbuff[SIZE];
   memset(processbuff,'\0',sizeof(processbuff));
   for(int i=0;i<cnt;i++)
       processbuff[i]=STYLE;
    
   static const char *lable="|/-\\";
   static int index=0;
   //刷新
   printf("[%-100s][%.1lf%%][%c]\r",processbuff,rate,lable[index++]);
   index%=strlen(lable);
   fflush(stdout);
   if(curr>=total)
   { 
     printf("\n");
   }
}



void Process()
{
   const char* lable ="|/-\\";// 动态旋转光标字符集：循环显示 '|' '/' '-' '\'，模拟加载动画（\ 需转义为 \\）
   int len = strlen(lable);// 计算旋转光标字符集长度（用于循环取模，实现光标循环切换）
   char processbuff[SIZE];
   memset(processbuff,'\0',sizeof(processbuff));//初始化
   int cnt=0;
   while(cnt<=100)
   {
     printf("[%-100s] [%d%%][%c]\r",processbuff,cnt,lable[cnt%len]);
     fflush(stdout);//强制刷新标准输出缓冲区：printf 无 \n 时默认缓存，fflush 确保实时显示进度
     processbuff[cnt++]=STYLE;//延时 50 毫秒（50000 微秒）：模拟任务执行耗时，控制进度条刷新速度（数值越小刷新越快）
     usleep(50000);
   }
   printf("\n");// 进度完成后换行：避免后续终端输出与进度条同行覆盖
}
