#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// -------------------------- 通用任务框架 --------------------------
// 任务状态枚举（可扩展更多状态）
typedef enum {
    TASK_STATUS_INIT,    // 初始化
    TASK_STATUS_RUNNING, // 运行中
    TASK_STATUS_DONE     // 已完成
} TaskStatus;

// 任务私有数据（不同任务的私有信息，用union/struct区分）
typedef struct {
    // 任务1私有数据：等待的两个文件路径
    struct {
        const char* file1;
        const char* file2;
        int step; // 0:等待file1, 1:等待file2
    } file_wait;

    // 任务2私有数据：计时等待（秒）
    struct {
        double wait_sec;       // 总等待时长
        struct timespec start; // 开始计时时间
    } sleep_wait;
} TaskData;

// 任务执行函数原型（所有任务需遵循此接口）
typedef void (*TaskFunc)(void* task_ctx);

// 任务控制块（通用结构，新增任务只需填充此结构）
typedef struct Task {
    TaskStatus status;   // 任务状态
    TaskData data;       // 任务私有数据
    TaskFunc func;       // 任务执行函数
    struct Task* next;   // 链表节点（用于管理多个任务）
} Task;

// 全局任务链表（管理所有任务）
static Task* g_task_list = NULL;

// 添加任务到全局链表（通用接口）
void task_add(Task* task) {
    if (!task) return;
    task->status = TASK_STATUS_INIT;
    task->next = g_task_list;
    g_task_list = task;
}

// 检查所有任务是否已完成（通用接口）
int all_tasks_done() {
    Task* iter = g_task_list;
    while (iter) {
        if (iter->status != TASK_STATUS_DONE) {
            return 0;
        }
        iter = iter->next;
    }
    return 1;
}

// -------------------------- 具体任务实现 --------------------------
// 任务1：等待两个文件从0字节变为1字节，依次打印
void task1_func(void* task_ctx) {
    Task* task = (Task*)task_ctx;
    struct stat st;

    switch (task->status) {
        case TASK_STATUS_INIT:
            // 初始化：第一步等待第一个文件
            task->data.file_wait.step = 0;
            task->status = TASK_STATUS_RUNNING;
            printf("Task1: 开始等待文件 %s\n", task->data.file_wait.file1);
            break;

        case TASK_STATUS_RUNNING:
            if (task->data.file_wait.step == 0) {
                // 检查第一个文件（非阻塞）
                if (stat(task->data.file_wait.file1, &st) == 0 && st.st_size >= 1) {
                    printf("Task1: hello-a\n");
                    // 切换到第二步：等待第二个文件
                    task->data.file_wait.step = 1;
                    printf("Task1: 开始等待文件 %s\n", task->data.file_wait.file2);
                }
            } else if (task->data.file_wait.step == 1) {
                // 检查第二个文件（非阻塞）
                if (stat(task->data.file_wait.file2, &st) == 0 && st.st_size >= 1) {
                    printf("Task1: hello-b\n");
                    task->status = TASK_STATUS_DONE; // 任务1完成
                }
            }
            break;

        case TASK_STATUS_DONE:
            break;
    }
}

// 任务2：等待N秒（随机）后打印，非阻塞计时
void task2_func(void* task_ctx) {
    Task* task = (Task*)task_ctx;
    struct timespec now;
    double elapsed;

    switch (task->status) {
        case TASK_STATUS_INIT:
            // 初始化：记录开始时间
            clock_gettime(CLOCK_MONOTONIC, &task->data.sleep_wait.start);
            task->status = TASK_STATUS_RUNNING;
            printf("Task2: 开始等待 %.1f 秒\n", task->data.sleep_wait.wait_sec);
            break;

        case TASK_STATUS_RUNNING:
            // 计算已等待时间（非阻塞）
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - task->data.sleep_wait.start.tv_sec) +
                      (now.tv_nsec - task->data.sleep_wait.start.tv_nsec) / 1e9;

            if (elapsed >= task->data.sleep_wait.wait_sec) {
                printf("Task2: hello\n");
                task->status = TASK_STATUS_DONE; // 任务2完成
            }
            break;

        case TASK_STATUS_DONE:
            break;
    }
}

// -------------------------- 主调度逻辑 --------------------------
int main() {
    // 1. 初始化任务1（等待/tmp/a.txt和/tmp/b.txt）
    Task task1;
    memset(&task1, 0, sizeof(Task));
    task1.func = task1_func;
    task1.data.file_wait.file1 = "/tmp/a.txt";
    task1.data.file_wait.file2 = "/tmp/b.txt";
    task_add(&task1);

    // 2. 初始化任务2（等待2~5秒随机时间）
    Task task2;
    memset(&task2, 0, sizeof(Task));
    task2.func = task2_func;
    // 生成2~5秒的随机等待时间
    srand((unsigned int)time(NULL));
    task2.data.sleep_wait.wait_sec = 2.0 + (rand() % 40) / 10.0; // 2.0~5.9秒
    task_add(&task2);

    // 3. 主循环：轮询调度所有任务（核心）
    printf("主调度开始，按Ctrl+C结束（可手动创建/tmp/a.txt和/tmp/b.txt）\n");
    while (!all_tasks_done()) {
        Task* iter = g_task_list;
        // 遍历所有任务，调用执行函数推进状态
        while (iter) {
            if (iter->status != TASK_STATUS_DONE) {
                iter->func(iter);
            }
            iter = iter->next;
        }
        // 降低CPU占用（可调整间隔，不影响任务逻辑）
        usleep(100000); // 100毫秒
    }

    printf("所有任务完成，程序退出\n");
    return 0;
}