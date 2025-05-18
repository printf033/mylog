# mylog

**mylog** 是一个自用的 C++ 日志工具，旨在提供高性能、灵活且线程安全的日志解决方案。它支持多级别日志输出、多种输出目标（终端、文件等）以及用户自定义输出方式，适用于各种开发和生产环境。

## 特性

- **多级别日志输出**  
  支持 TRACE、DEBUG、INFO、WARN、ERROR、FATAL 级别的日志输出，方便在开发和生产环境中进行日志过滤和定位问题。

- **灵活的输出目标**  
  内置终端和文件输出方式，同时允许用户通过自定义函数来改变日志输出目标。

- **线程安全**  
  内部采用互斥锁或其他线程同步机制，在多线程环境下保证日志输出的有序性和正确性，无需额外的同步处理。

- **流式接口**  
  使用 C++ 流式操作符（<<）构建日志输出，使用简单直观。

## 安装

1. 将 `mylog` 源代码添加到你的项目中。
2. 使用 [CMake](./CMakeLists.txt) 配置项目，例如：
    ```bash
    mkdir build && cd build
    cmake ..
    make
    ```
3. 在你的项目中包含 `mylog/inc` 目录即可开始使用。

## 使用方法

在项目中包含 `logger.hpp`，在主函数中初始化日志系统，然后通过日志宏输出日志：

```cpp
#include "logger.hpp"

int main()
{
    // 不同级别的日志示例
    LOG_TRACE << "这是 TRACE 日志";
    LOG_DEBUG << "这是 DEBUG 日志";
    LOG_INFO  << "这是 INFO 日志";
    LOG_WARN  << "这是 WARN 日志";
    LOG_ERROR << "这是 ERROR 日志";
    LOG_FATAL << "这是 FATAL 日志";

    return 0;
}
```

## 日志输出示例

运行后输出类似如下内容（示例）：

```text
2025-05-08T22:28:28.169448 DEBUG [17107]animator.hpp:150:calculateFinalTransform >> 当前节点名称#RootNode id#0 变换矩阵#
1#0#0#0#
0#1#0#0#
0#0#1#0#
0#0#0#1#
```

## 自定义日志输出

你可以定义自己的日志输出函数来定制输出方式，例如将日志输出到标准错误：

```cpp
// 自定义输出函数示例（输出到标准错误）
void myCustomLogOutput(const std::string &msg) {
    std::cerr << msg << std::endl;
}

// 在程序启动时设置自定义输出函数
mylog::Logger::_setOutputFunc(myCustomLogOutput);
```

## 线程安全

mylog 内部采用线程同步机制（例如互斥锁）确保在多线程环境下准确记录日志。用户只需直接调用日志宏，无需担心并发问题。

## 后续计划

- 支持更多输出目标（如网络输出、数据库存储等）。
- 提供详细的日志统计与监控功能，便于调试和性能分析。

## License

该项目采用 [MIT License](./LICENSE) 开源，任何人均可自由使用、修改和分发，但需保留原始版权信息。
