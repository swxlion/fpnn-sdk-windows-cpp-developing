## ClientEngine

### 介绍

ClientEngine 为全局的 Client(TCP Client & UDP Client) 和网络连接的管理模块。为单例实现。

**注意：请勿使用非文档化的 API。**  
非文档化的 API，或为内部使用，或因历史原因遗留，或为将来设计，后续版本均可能存在变动。

### 关键定义

	class ClientEngine;
	typedef std::shared_ptr<ClientEngine> ClientEnginePtr;

	struct MemoryPoolInitParams
	{
		int32_t initBlocks;
		int32_t perAppendBlocks;
		int32_t perfectBlocks;
	};

	struct ClientEngineInitParams
	{
		int globalConnectTimeoutSeconds;
		int globalQuestTimeoutSeconds;
		int residentTaskThread;
		int maxTaskThreads;
		int IOCPWorkThreadCount;
		struct MemoryPoolInitParams overLappedPoolInitParams;

		ClientEngineInitParams();
	};

	class ClientEngine: virtual public IConcurrentSender
	{
	public:
		static ClientEnginePtr create(const ClientEngineInitParams* params = NULL);
		static ClientEnginePtr instance();
		virtual ~ClientEngine();

		inline static void setQuestTimeout(int seconds);
		inline static int getQuestTimeout();
		inline static void setConnectTimeout(int seconds);
		inline static int getConnectTimeout();

		inline static bool runTask(std::shared_ptr<ITaskThreadPool::ITask> task);
		inline static bool runTask(std::function<void ()> task);

		inline void setDestructorCleanupAction(std::function<void ()> cleanupAction);
		inline void enableDestructorCleanupAction(bool enable);
	};

### 创建与构造

#### ClientEngineInitParams

ClientEngine 初始化参数结构。  
创建后，所有字段均已初始化为默认值。仅修改需要配置的字段即可。

**字段说明**

* **`int globalConnectTimeoutSeconds`**

	全局所有客户端的默认连接超时。单位：秒。默认值：5秒。

* **`int globalQuestTimeoutSeconds`**

	全局所有客户端的默认请求超时。单位：秒。默认值：5秒。

* **`int residentTaskThread`**

	全局所有客户端共享的任务线程池的**常驻线程**数上限。默认值：4。

* **`int maxTaskThreads`**

	全局所有客户端共享的任务线程池的线程数上限。默认值：64。

* **`int IOCPWorkThreadCount`**

	IOCP 工作线程数量。默认 0，为 CPU 总核心数目。

* **`struct MemoryPoolInitParams overLappedPoolInitParams`**

	IOCP 所需 OverLapped 内存池配置信息。

	+ **`int32_t initBlocks`**

		初始化时，预先分配的 OverLapped 内存块数量。默认值：2。

	+ **`int32_t perAppendBlocks`**

		内存池每次追加 OverLapped 内存块时，批量追加的 OverLapped 内存块数量。默认值：2。

	+ **`int32_t perfectBlocks`**

		常驻内存的 OverLapped 内存块的数量上限。默认值：10。


#### create

	static ClientEnginePtr create(const ClientEngineInitParams* params = NULL);

ClientEngine 创建接口。

除非需要修改默认参数配置，否则无需手动调用。

**注意**

如果需要修改默认配置，`ClientEngine::create` 接口需要在任何客户端被创建前，和任何 ClientEngine 接口调用前被调用。


### 成员函数

#### instance

	static ClientEnginePtr instance();

返回 ClientEngine 实例。如果 ClientEngine 未被创建，则创建后返回。

#### setQuestTimeout

	inline static void setQuestTimeout(int seconds);

设置全局所有客户端的默认请求超时。单位：秒。

#### getQuestTimeout

	inline static int getQuestTimeout();

获取全局所有客户端的默认请求超时。单位：秒。

#### setConnectTimeout

	inline static void setConnectTimeout(int seconds);

设置全局所有客户端的默认连接超时。单位：秒。

#### getConnectTimeout

	inline static int getConnectTimeout();

获取全局所有客户端的默认连接超时。单位：秒。


#### runTask

	inline static bool runTask(std::shared_ptr<ITaskThreadPool::ITask> task);
	inline static bool runTask(std::function<void ()> task);

借用全局所有客户端共享的任务线程池执行任务。

**参数说明**

* **`std::shared_ptr<ITaskThreadPool::ITask> task`**

	线程池执行接口的实现实例。具体请参见 [ITaskThreadPool::ITask](base.md#ITask)。

* **`std::function<void ()> task`**

	不带参数的无返回值的 lambda 函数。

#### setDestructorCleanupAction

	inline void setDestructorCleanupAction(std::function<void ()> cleanupAction);

设置 ClientEngine 析构函数返回前，需执行的清理函数。

**注意**

* ClientEngine 一般是在 main() 函数返回后，才会开始析构。
* 默认情况下，ClientEngine 析构最后，会调用 `WSACleanup()` 函数。设置需要执行的清理函数后，将改变这一行为。此时 ClientEngine 将不再调用 `WSACleanup()` 函数。

#### enableDestructorCleanupAction

	inline void enableDestructorCleanupAction(bool enable);

是否在 ClientEngine 析构函数返回前，执行设置的清理函数。如果不执行，则 ClientEngine 在没有设置清理函数时，也不会调用 `WSACleanup()` 函数。