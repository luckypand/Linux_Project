#include"processbar.h"
#include<unistd.h>
#include<time.h>
#include<stdlib.h>
const double gtotal=1024.0;//文件总大小
const double speed=1.0;//下载速度
extern int first_flag;
typedef void(*pProcess)(double,double);

double Internet_analog(double start,double range)
{
    return start + (rand() % (int)range) + (range - int(range));
}

void Download(int total,pProcess pP)  //total是下载文件大小
{
    srand(time(0));
    double cur = 0;
    while(1)
    {
        if(cur >= total)
        {
            cur = total;
            pP(total,cur);
            break;
        }
        pP(total,cur);
        cur += Internet_analog(speed,3.6);
        usleep(10000);
    }
    first_flag = 1;
}

int main()
{
    Download(40000,FlashaProcess);

    return 0;
}

/*实现顺序
    1.单独的进度条实现  
    2.带旋转的进度条实现
    2.5 实现反馈下载进度的进度条实现
    3.嵌入进下载场景实现  
    4.实现模拟网络的虚拟波动
    5.实现函数指针的调用，进行代码解耦和
*/