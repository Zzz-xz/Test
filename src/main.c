#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// -------------------------- 类型定义与枚举 --------------------------
// 任务状态：初始化、运行中、已完成
typedef enum {
    TASK_INIT,        // 未开始
    TASK_RUNNING,     // 执行中
    TASK_FINISHED     // 已完成
} TaskStatus;

// 任务结构体：存储任务信息和状态
typedef struct Task {
    TaskStatus status;          // 任务当前状态
    int step;                   // 记录任务执行到的步骤
    void (*execute)(struct Task*); // 任务执行函数
    union {                     // 任务私有数据
        // 文件等待任务专用数据
        struct {
            const char* file_path_a;  // 第一个等待文件路径
            const char* file_path_b;  // 第二个等待文件路径
        } file_wait_info;
        
        // 计时等待任务专用数据
        struct {
            double total_wait_sec;    // 总等待秒数
            struct timespec start_ts; // 开始计时的时间点
        } timer_info;
    } private_data;              // 私有数据联合体
    struct Task* next;           // 链表节点，用于任务队列
} Task;

// -------------------------- 全局变量 --------------------------
static Task* g_task_queue = NULL;  // 全局任务队列

// -------------------------- 任务框架宏定义 --------------------------
/**
 * 任务开始宏：初始化任务状态和步骤
 * @param task 任务指针
 */
#define TASK_START(task) \
    do { \
        if ((task)->status == TASK_INIT) { \
            (task)->step = 0; \
            (task)->status = TASK_RUNNING; \
        } \
        switch ((task)->step) { \
            case 0:;

/**
 * 任务等待宏：设置暂停点，条件不满足时下次继续执行
 * @param task 任务指针
 * @param condition 继续执行的条件（为真时继续）
 */
#define TASK_PAUSE_WHILE(task, condition) \
            (task)->step = __LINE__; return; case __LINE__:; \
            if (!(condition)) return;

/**
 * 任务结束宏：标记任务为已完成
 */
#define TASK_FINISH \
            (task)->status = TASK_FINISHED; \
        } \
    } while (0)

// -------------------------- 任务管理工具函数 --------------------------
/**
 * 添加任务到全局任务队列
 * @param task 要添加的任务指针
 */
void task_append(Task* task) {
    if (!task) return;
    task->status = TASK_INIT;  // 确保任务初始状态正确
    task->next = g_task_queue;
    g_task_queue = task;
}

/**
 * 检查所有任务是否都已完成
 * @return 1-所有任务完成，0-还有未完成任务
 */
int is_all_tasks_finished() {
    Task* current = g_task_queue;
    while (current) {
        if (current->status != TASK_FINISHED) {
            return 0;
        }
        current = current->next;
    }
    return 1;
}

// -------------------------- 文件操作工具函数 --------------------------
/**
 * 检查文件是否存在且大小至少1字节
 * @param path 文件路径
 * @return 1-文件满足条件，0-不满足
 */
static int is_file_valid(const char* path) {
    struct stat file_info;
    // 文件不存在或获取信息失败，视为无效
    if (stat(path, &file_info) != 0) {
        return 0;
    }
    // 检查文件大小是否至少1字节
    return (file_info.st_size >= 1) ? 1 : 0;
}

// -------------------------- 具体任务实现 --------------------------
/**
 * 任务1：等待两个文件依次变为有效状态后打印信息
 * 流程：等待文件A有效 → 打印hello-a → 等待文件B有效 → 打印hello-b
 */
void file_wait_task(Task* task) {
    TASK_START(task);
        // 第一步：等待第一个文件有效
        printf("Task1：开始等待文件 %s\n", task->private_data.file_wait_info.file_path_a);
        TASK_PAUSE_WHILE(task, is_file_valid(task->private_data.file_wait_info.file_path_a));
        printf("Task1：hello-a\n");

        // 第二步：等待第二个文件有效
        printf("Task1：开始等待文件 %s\n", task->private_data.file_wait_info.file_path_b);
        TASK_PAUSE_WHILE(task, is_file_valid(task->private_data.file_wait_info.file_path_b));
        printf("Task1：hello-b\n");
    TASK_FINISH;
}

/**
 * 任务2：等待指定随机时间后打印信息（非阻塞等待）
 * 流程：记录开始时间 → 循环检查是否达到等待时间 → 打印hello
 */
void timer_wait_task(Task* task) {
    struct timespec current_ts;  // 当前时间
    double elapsed_sec;          // 已流逝的秒数

    TASK_START(task);
        // 第一步：初始化计时起点
        clock_gettime(CLOCK_MONOTONIC, &task->private_data.timer_info.start_ts);
        printf("Task2：开始等待 %.1f 秒\n", task->private_data.timer_info.total_wait_sec);

        // 第二步：等待指定时长（计算已等待时间）
        TASK_PAUSE_WHILE(task, 
            (clock_gettime(CLOCK_MONOTONIC, &current_ts),  // 获取当前时间
             elapsed_sec = (current_ts.tv_sec - task->private_data.timer_info.start_ts.tv_sec) +
                         (current_ts.tv_nsec - task->private_data.timer_info.start_ts.tv_nsec) / 1e9,
             elapsed_sec >= task->private_data.timer_info.total_wait_sec)  // 检查是否超时
        );
        printf("Task2：hello\n");
    TASK_FINISH;
}

// -------------------------- 主函数与调度逻辑 --------------------------
int main() {
    // 1. 初始化文件等待任务（监视/tmp/a.txt和/tmp/b.txt）
    Task file_task;
    memset(&file_task, 0, sizeof(Task));
    file_task.execute = file_wait_task;
    file_task.private_data.file_wait_info.file_path_a = "/tmp/a.txt";
    file_task.private_data.file_wait_info.file_path_b = "/tmp/b.txt";
    task_append(&file_task);

    // 2. 初始化计时等待任务（随机等待2.0-5.9秒）
    Task timer_task;
    memset(&timer_task, 0, sizeof(Task));
    timer_task.execute = timer_wait_task;
    srand((unsigned int)time(NULL));  // 初始化随机数种子
    timer_task.private_data.timer_info.total_wait_sec = 2.0 + (rand() % 40) / 10.0;
    task_append(&timer_task);

    // 3. 主调度循环：轮询执行所有未完成任务
    printf("主调度开始，按Ctrl+C结束（可手动创建/tmp/a.txt和/tmp/b.txt）\n");
    while (!is_all_tasks_finished()) {
        Task* current = g_task_queue;
        while (current) {
            if (current->status != TASK_FINISHED) {
                current->execute(current);  // 执行任务
            }
            current = current->next;
        }
        usleep(100000);  // 每100毫秒轮询一次，降低CPU占用
    }

    printf("所有任务完成，程序退出\n");
    return 0;
}