# FPNN Windows C++ SDK

## 介绍

FPNN Windows C++ SDK 是独立于 [FPNN C++ SDK](https://github.com/highras/fpnn-sdk-cpp) 的 Windows 平台专属 SDK。

适用于 CentOS(Linux)、Ubuntu(Linux)、MacOS、iOS 及 Android(NDK) 的 C++ SDK 请移步 [FPNN C++ SDK](https://github.com/highras/fpnn-sdk-cpp)。

本 SDK 提供的客户端为 IOCP 操作聚合客户端，并非双线程客户端。因此配置完毕后，可以同时运行数百个客户端实例，而无需担心线程暴涨。

### 支持特性

* 支持 IPv4
* 支持 IPv6
* 支持 msgPack 编码
* 支持 json 编码
* 支持 可选参数
* 支持 不定类型/可变类型参数
* 支持 不定长度不定类型参数
* 支持 接口灰度兼容
* 支持 Server Push
* 支持 异步调用
* 支持 同步调用
* 支持 应答提前返回
* 支持 应答异步返回
* TCP：支持加密链接
* TCP：支持自动保活
* UDP：支持自动保活
* UDP：支持可靠连接
* UDP：支持可丢弃数据和不可丢弃数据混发
* UDP：支持乱序重排
* UDP：支持大数据自动切割 & 自动组装
* UDP：支持零散数据合并发送
* UDP：支持毫秒级超时控制

## 使用

### 基本信息

* 语言版本：C++11
* 项目版本：Visual Studio 2017


### 使用配置

1. 请在项目的“引用”项中，添加对 fpnn-sdk 项目的引用
1. 请在项目“属性” -> “配置属性” -> “C/C++” -> “常规” -> “附加包含目录” 中，添加以下条目：

		$(SolutionDir)\fpnn-sdk\base
		$(SolutionDir)\fpnn-sdk\proto
		$(SolutionDir)\fpnn-sdk\proto\msgpack
		$(SolutionDir)\fpnn-sdk\core

详细配置过程，请参见 [SDK使用配置](docs/install.md)。

### 开发文档

* [SDK使用配置](docs/install.md)
* [SDK使用向导](docs/guide.md)
* [样例演示](docs/examples.md)
* [API手册](docs/API.md)
* [嵌入模式](docs/embedMode.md)
* [SDK配置](docs/config.md)
* [内置工具](docs/tools.md)
* [测试介绍](docs/tests.md)
* [错误代码](docs/errorCode.md)


### 解决方案目录结构

* **\<解决方案根目录\>\\fpnn-sdk**

	SDK 核心代码。

* **\<解决方案根目录\>\\docs**

	SDK 文档目录。

* **\<解决方案根目录\>\\examples**

	SDK 主要功能演示。

* **\<解决方案根目录\>\\tests**

	测试程序代码。

* **\<解决方案根目录\>\\tests\\embedModeTests**

	嵌入模式测试程序代码。嵌入模式请参见 [嵌入模式](docs/embedMode.md)。

* **\<解决方案根目录\>\\tools**

	SDK 内置工具。

