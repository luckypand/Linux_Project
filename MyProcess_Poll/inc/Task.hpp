#pragma once
#include <vector>
#include <iostream>

typedef void (*task_)(); //函数指针写法(*func())

void task1() { //模拟实际任务
    std::cout << "任务1 : 数据初始化(Data Initialization)" << std::endl;
}
void task2(){
    std::cout << "任务2 : 更新数据(Update Data)" << std::endl;
}
void task3(){
    std::cout << "任务3 : 获取数据(Retrieve Data)" << std::endl;
}
void task4(){
    std::cout << "任务4 : 数据验证(Data Validation)" << std::endl;
}
void task5(){
    std::cout << "任务5 : 生成报告(Generate Report)" << std::endl;
}
void task6(){
    std::cout << "任务6 : 备份数据(Backup Data)" << std::endl;
}

void Load_Task(std::vector<task_> *tasks)//vector存储函数任务
{   
    tasks->push_back(task1);//放入函数任务1
    tasks->push_back(task2);//放入函数任务2
    tasks->push_back(task3);//放入函数任务3
    tasks->push_back(task4);//放入函数任务4
    tasks->push_back(task5);//放入函数任务5
    tasks->push_back(task6);//放入函数任务6
}