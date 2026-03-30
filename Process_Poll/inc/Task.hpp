#pragma once

#include <iostream>

#include <vector>

typedef void (*task_t)(); //    定义函数指针

void task1() { 
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


void LoadTask(std::vector<task_t> *tasks){
    tasks->push_back(task1);
    tasks->push_back(task2);
    tasks->push_back(task3);
    tasks->push_back(task4);
    tasks->push_back(task5);
    tasks->push_back(task6);
}
