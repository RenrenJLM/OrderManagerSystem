# OrderManagerSystem 开发日志

## Milestone 1：数据库初始化与建表

### 阶段目标
- 程序启动时自动创建 SQLite 数据库文件。
- 初始化 6 张核心表：
  - `product_models`
  - `option_templates`
  - `option_template_components`
  - `order_items`
  - `order_item_components`
  - `shipment_records`
- 将建表逻辑集中在数据库层，不分散到 UI 代码中。

### 本阶段完成内容
- 在 `DatabaseManager` 中完成数据库连接、外键启用、建表语句执行。
- 程序启动时先完成数据库初始化，再显示主窗口。
- 核心业务表已能重复启动自动检查并保持可用，不会因重复建表报错。

### 验证结果
- 在 Windows `Qt 6.7.3 + MSVC 2019` 环境下完成编译与运行验证。
- 运行程序后成功生成 `OrderManagerSystem.db`。
- 数据库中确认存在 6 张核心表。

### 遇到的问题
- 早期曾在 WSL 命令行中尝试执行 `cmake -S . -B build`。
- 但本项目实际开发方式是：
  - 使用 WSL 作为 Codex 辅助分析与代码修改环境
  - 使用 Windows 下的 Qt Creator / Qt 工具链进行实际编译、运行和功能验证
- 因此 WSL 本身并不是本项目的目标构建验证环境。
- 同时，WSL 中可见的 Qt 版本为 `6.2.4`，也不满足项目 `CMakeLists.txt` 中 `Qt 6.5` 的要求。

### 解决方案
- 明确将 WSL 仅作为 Codex 辅助环境，不将其作为 Qt 项目的实际构建验证环境。
- 实际编译、运行与功能验证统一放在 Windows 下已经安装并可工作的 `Qt 6.7.3 + MSVC 2019` 环境中完成。
- 后续涉及 Qt/CMake 构建验证，统一以 Windows Qt Creator 构建结果为准，而不是以 WSL 命令行结果为准。

### 本阶段结论
- Milestone 1 已完成。
- 数据库初始化、建表和启动入口已打通，为后续录单闭环提供了稳定的数据基础。

---

## Milestone 2：订单录入最小闭环

### 阶段目标
- 实现“录入订单 -> 选择模板或自定义配置 -> 展开订单组件 -> 保存订单”的最小闭环。
- 模板仅作为录单辅助，最终订单组件必须以快照方式写入 `order_item_components`。

### 本阶段完成内容
- 在主窗口实现最小可用录单界面，包含：
  - 订单日期
  - 客户名称
  - 产品型号选择
  - 配置模板选择
  - 订单套数
  - 单价
  - 模板配置 / 自定义配置切换
  - 订单组件表格
- 在数据库层新增最小录单相关接口：
  - 读取产品型号
  - 按产品读取模板
  - 读取模板组件
  - 保存订单主记录
  - 保存订单组件快照
- 增加最小演示数据写入逻辑，保证产品型号与模板可直接用于界面联调。
- 实现订单保存时的事务处理，先写 `order_items`，再写 `order_item_components`。

### 已实现的业务规则
- 模板组件会展开为订单组件快照。
- 自定义组件只写当前订单，不回写模板表。
- 订单组件总需求数量按“每套数量 × 订单套数”计算。
- 订单初始发货状态为未发：
  - `shipped_sets = 0`
  - `unshipped_sets = quantity_sets`
- 组件初始发货状态为未发：
  - `shipped_quantity = 0`
  - `unshipped_quantity = total_required_quantity`
- 同名组件的最终合并保证放在数据库保存前处理。

### 验证结果
- 在 Windows `Qt 6.7.3 + MSVC 2019` 下可以成功编译运行。
- 当前数据库中已确认存在产品、模板、模板组件基础数据。
- 已成功保存多条模板配置订单和自定义配置订单。
- 已检查 `order_items` 与 `order_item_components`：
  - 模板订单组件快照与模板明细一致
  - 自定义订单组件以 `manual` 来源写入
  - 总需求数量计算正确
  - 未发数量初始值正确

### 遇到的问题 1：需要明确区分辅助环境与实际验证环境
- 与 Milestone 1 一致，WSL 主要用于 Codex 辅助分析和代码修改。
- 本项目的 Qt 编译、运行和功能验证实际发生在 Windows Qt 环境中。
- 因此 WSL 命令行并不作为本项目的正式构建验证渠道。
- 同时，WSL 中的 Qt 版本也不满足项目要求，不适合作为参考构建结果。

### 解决方案 1
- 持续使用 Windows `Qt 6.7.3 + MSVC 2019` 作为唯一有效构建验证环境。
- WSL 只承担辅助作用，不再把 WSL 中的 Qt/CMake 结果作为功能是否完成的判断依据。

### 遇到的问题 2：程序关闭时出现 MSVC Debug Error
- 现象：
  - 点击右上角关闭按钮时，MSVC Debug 运行库报错：
  - `Run-Time Check Failure #2 - Stack around the variable 'w' was corrupted`
- 初始表现为：
  - 即使不做任何操作，启动后直接关闭也会报错。

### 问题分析
- 报错点出现在 `main.cpp` 中的主窗口栈对象 `w`，但实际问题并不一定在 `main.cpp` 本身。
- 重点排查后发现风险主要集中在：
  - `MainWindow` 的初始化/销毁生命周期
  - `QTableWidget` 相关信号回调
  - UI 更新时可能发生的重入修改

### 解决方案 2
- 将主窗口从栈对象改为堆对象持有，减少运行时检查只在 `w` 上暴露问题的情况。
- 为 `MainWindow` 增加初始化中和关闭中的状态保护：
  - `m_isInitializing`
  - `m_isShuttingDown`
- 收紧组件表更新路径：
  - 不再在危险回调中重建整张表
  - 表格批量更新时统一使用 `QSignalBlocker`
  - 删除组件时不再触发表格重建
- 在关键位置加入最小 `qDebug()` 输出，辅助确认启动和关闭链路。

### 修复后验证结果
- 关闭窗口时不再出现 Debug Error。
- 应用输出中可正常看到：
  - `MainWindow initialized`
  - `Showing main window`
  - `MainWindow shutting down`
- 关闭进程返回成功。
- 添加组件等基本操作也未再触发退出时报错。

### 本阶段结论
- Milestone 2 已基本完成并通过当前范围内验证。
- 订单录入、模板展开、自定义组件录入、订单保存与组件快照保存链路已打通。
- 当前实现仍保持在最小闭环范围内，未提前实现发货、查询或汇总功能。
