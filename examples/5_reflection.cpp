#include "logger.hpp"
#include <string>

/**
 * @file 5_reflection.cpp
 * @brief C++26 静态反射与敏感字段脱敏示例
 *
 * 演示：
 * 1. 作用域枚举（Scoped Enum）基于 C++26 静态反射自动转换为字面量标识符输出；
 * 2. 聚合结构体（Aggregate Struct）基于反射元信息全自动展开格式化；
 * 3. 敏感字段注解 [[= mylog::sensitive]] 自动掩码脱敏为 "******"，保障日志安全。
 */

enum class TaskStatus
{
    Queued,
    Running,
    Completed,
    Failed
};

struct UserSession
{
    uint64_t userId;
    std::string username;
    [[= mylog::sensitive]] std::string password; // 自动脱敏
    [[= mylog::sensitive]] std::string apiKey;   // 自动脱敏
    double balance;
    TaskStatus status;
};

int main()
{
    // 1. 作用域枚举直接格式化输出（自动反射输出其名称字符串 "Queued"）
    mylog::info("任务初始状态: {}", TaskStatus::Queued);

    // 2. 聚合结构体直接格式化输出（敏感字段自动脱敏为 "******"）
    UserSession session{.userId = 10086,
                        .username = "alice",
                        .password = "PlainPassword123!",
                        .apiKey = "sk-live-abcdef0123456789",
                        .balance = 256.78,
                        .status = TaskStatus::Running};

    // 打印脱敏后的结构体内容，杜绝密码与 API Key 等敏感信息泄露
    mylog::info("活跃用户会话: {}", session);

    // 3. 动态更新结构体字段后打印
    session.status = TaskStatus::Completed;
    session.balance += 50.0;
    mylog::info("任务处理完成: {}", session);

    // 4. 现代无宏 API 格式化输出
    mylog::warn("用户余额变动提醒: 当前余额为 {:.2f}", session.balance);
    mylog::debug("会话追踪流程结束");

    return 0;
}
