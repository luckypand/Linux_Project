#include<stdio.h>
#include<unistd.h>

int main()
{
    int cnt =10;
    while(cnt)
    {
        printf("count is:%-2d\r",cnt);
        fflush(stdout); //Ë¢ĞÂÏÔÊ¾Æ÷
        sleep(1);
        cnt--;
    }

    return 0;
}