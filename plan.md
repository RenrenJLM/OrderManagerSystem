# OrderManagerSystem 分阶段实施计划

## 规划原则
- 遵循 `requirements.md` 与 `AGENTS.md`，使用 C++、Qt 6、Qt Widgets、CMake、SQLite。
- 按“先跑通最小闭环，再补充功能”的顺序推进，避免一次性铺开全部能力。
- 当前阶段只规划最小可运行版本，不提前引入导出、权限、网络同步、复杂报表等后续能力。

## Milestone 1：数据库初始化与建表

### 目标
- 程序启动时能够自动创建 SQLite 数据库并完成核心表初始化。
- 明确订单、订单组件、模板、模板组件、发货记录之间的关系字段。
- 保证重复启动不会重复建表报错，为后续录单和发货提供稳定数据基础。

### 范围
- 初始化数据库连接。
- 建立最小核心表：
  - `product_models`
  - `option_templates`
  - `option_template_components`
  - `order_items`
  - `order_item_components`
  - `shipment_records`
- 约定主键、外键关联字段和必要的数量字段。
- 视需要写入最小演示数据，便于后续界面联调。

### 涉及文件
- `databasemanager.h`
- `databasemanager.cpp`
- `main.cpp`
- `CMakeLists.txt`

### 完成标准
- 应用启动后可自动创建数据库文件。
- 核心表结构集中在数据库管理层维护，不分散在 UI 代码中。
- 数据表设计能够支撑模板展开为订单快照，且保留发货历史。

### 验证方式
- 构建验证：
  - `cmake -S . -B build`
  - `cmake --build build`
- 运行程序两次，确认数据库初始化成功且重复执行不报错。
- 使用 SQLite 查看表结构，确认核心表已创建且关键字段齐全。

## Milestone 2：订单录入最小闭环

### 目标
- 打通“录入订单 -> 选择模板或自定义配置 -> 展开订单组件 -> 保存订单”的最小闭环。
- 明确订单项是最小业务单位，模板仅作为录单辅助，最终以订单组件快照落库。
- 让用户可以完成最基本的订单录入与保存，不依赖发货和复杂查询功能。

### 范围
- 在主界面提供最小可用的订单录入区域。
- 支持选择产品型号与配置模板。
- 支持手动录入自定义组件。
- 保存订单主记录到 `order_items`。
- 保存订单实际组件到 `order_item_components`。
- 处理同名组件合并、总需求数量计算等最小业务规则。

### 涉及文件
- `mainwindow.h`
- `mainwindow.cpp`
- `mainwindow.ui`
- `databasemanager.h`
- `databasemanager.cpp`
- `main.cpp`

### 完成标准
- 能录入一条订单并成功保存。
- 使用模板配置时，模板组件能展开为订单实际组件。
- 使用自定义配置时，组件数据只写入订单，不回写模板。
- 历史订单保存后不受后续模板变更影响。

### 验证方式
- 构建验证：
  - `cmake -S . -B build`
  - `cmake --build build`
- 通过界面分别录入：
  - 一条模板配置订单
  - 一条自定义配置订单
- 检查数据库，确认：
  - `order_items` 有对应订单记录
  - `order_item_components` 有正确的组件快照与数量

## Milestone 3：发货功能

### 目标
- 实现订单级和组件级发货登记，支持多次追加发货并保留历史。
- 能实时看到已发数量与未发数量，满足基础跟踪需求。
- 保持“追加记录”模式，不用覆盖历史数据。

### 范围
- 增加发货录入界面或页面。
- 支持按订单登记发货数量。
- 支持按订单组件登记发货数量。
- 将每次发货保存为独立记录。
- 更新订单和订单组件的已发/未发数量字段或可计算状态。

### 涉及文件
- `mainwindow.h`
- `mainwindow.cpp`
- `mainwindow.ui`
- `databasemanager.h`
- `databasemanager.cpp`

### 完成标准
- 对同一订单可多次追加发货。
- 发货后历史记录可追溯，不被覆盖。
- 订单级和组件级未发数量能正确变化。
- 不因一次发货破坏原始订单组件快照。

### 验证方式
- 构建验证：
  - `cmake -S . -B build`
  - `cmake --build build`
- 对同一订单执行多次发货登记，检查：
  - `shipment_records` 中有多条历史记录
  - `order_items` 与 `order_item_components` 的已发/未发数量正确
- 验证超发、空发货等异常输入被界面或逻辑层拦截。

## Milestone 4：查询与汇总

### 目标
- 提供订单查询、订单明细查看和基础汇总能力。
- 支持按客户、产品型号、日期、未发状态等常见条件筛选。
- 为日常跟单与统计提供最小可用查询页面。

### 范围
- 增加订单查询区域。
- 展示订单列表。
- 查看单条订单的组件明细与发货情况。
- 提供常用筛选条件。
- 提供最小汇总结果，例如订单数量、总套数、未发套数等。

### 涉及文件
- `mainwindow.h`
- `mainwindow.cpp`
- `mainwindow.ui`
- `databasemanager.h`
- `databasemanager.cpp`

### 完成标准
- 用户可按常用条件筛选订单。
- 查询结果能联动查看订单组件与发货情况。
- 汇总结果与明细数据一致。
- 页面仍保持轻量、清晰、可录入优先的 Qt Widgets 风格。

### 验证方式
- 构建验证：
  - `cmake -S . -B build`
  - `cmake --build build`
- 准备包含不同客户、型号、日期、发货状态的测试数据。
- 通过界面执行查询，确认筛选结果正确。
- 对比汇总结果与数据库明细记录，确认统计一致。

## 里程碑依赖关系
- Milestone 1 完成后，才能稳定推进录单与发货。
- Milestone 2 依赖 Milestone 1 的表结构与初始化能力。
- Milestone 3 依赖 Milestone 2 已有订单与订单组件数据。
- Milestone 4 依赖前 3 个 Milestone 的基础数据链路已打通。

## 当前执行建议
1. 先完成 Milestone 1，固定数据库表结构和初始化入口。
2. 再完成 Milestone 2，优先实现订单录入最小闭环。
3. 随后实现 Milestone 3，补齐发货记录与未发跟踪。
4. 最后实现 Milestone 4，提供查询和基础汇总能力。

