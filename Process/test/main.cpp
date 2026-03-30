#include"processbar.h"
#include<unistd.h>
#include<time.h>
#include<stdlib.h>
double gtotal=1024.0;//文件总大小
double speed=1.0;//下载速度

typedef void(*pFlash)(double,double);

//start为网速基础值，range 为网速浮动
double SpeedFloat(double start,double range)
{
  int int_range=(int)range;
  return start + rand()%int_range + (range - int_range);
}

//pf:回调函数
void DownLoad(int total,pFlash pf) 
{
   srand(time(NULL));//随机数种子
   double curr=0.0;
   while(1)
   {   
     if(curr >= total)
      {   
          curr=total;
          pf(total,curr);
          break;
      }   
     pf(total,curr);//更新进度，按照下载进度，进行更新进度条
     curr+=SpeedFloat(speed,4.3);//模拟下载行为
     usleep(30000);
   }   
}


int main()
{
 DownLoad(20.0,FlashProcess);
 DownLoad(200.0,FlashProcess);
 DownLoad(2000.0,FlashProcess);
 DownLoad(20000.0,FlashProcess);
 return 0;
}                                                                                                                                      
