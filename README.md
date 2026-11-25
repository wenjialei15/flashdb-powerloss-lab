# flashdb-powerloss-lab

把 [FlashDB](https://github.com/armink/FlashDB) 的**真实源码**编译到 PC 上，在每一次 Flash 擦除/写入之前切断电源，然后重新上电，检查数据是否还在、是否被写坏。

不依赖开发板，只需要一个 C 编译器。

**2454 个断电点，5 个异常。**

![实验台页面](docs/preview.png)

| 场景 | 分区 | 一次完整流程的操作数 | 断电点 | 异常 |
|---|---|---|---|---|
| KVDB：96 个键，更新其中 48 个 | 24 KB / 6 扇区 | 338 | 338 | 1 |
| TSDB：追加 520 条记录，触发扇区轮转 | 12 KB / 3 扇区 | 2116 | 2116 | 4 |

5 个异常点分成三类，不是同一个问题：

| # | 位置 | 现象 | 类型 |
|---|---|---|---|
| 1 | KVDB cut 46 | `key006` 重启后消失，旧值新值都没有；再次重启也不恢复 | 丢键 |
| 2 | TSDB cut 1546 / 2064 | 重启后能读，但再追加一条返回 `7`（`FDB_SAVED_FULL`），库变只读 | 不可写 |
| 3 | TSDB cut 1550 / 1551 | 迭代器读出负时间戳的垃圾记录，且不收敛（超过 5000 条仍未结束） | 迭代失控 |

完整输出见 [`docs/results.txt`](docs/results.txt)，
可直接提交的 Issue 草稿见 [`docs/issue-draft.md`](docs/issue-draft.md)。

## 为什么要做

FlashDB 的卖点是"掉电安全"，但这句话通常只在 README 里，缺少可以自己跑的验证。官方的示例和测试都跑在真实 MCU 上（`demos/stm32f103ve`、`tests/fdb_kvdb_tc.c`），没有 PC 侧的断电注入工具。

这个实验台把 FAL 的 Flash 设备换成内存实现，于是所有擦写都能被计数、被拦截——不用买板子，也能把"掉电安全"逐点验证一遍。

## 怎么做的

```
src/main.c             建库、跑负载、逐点断电、重启、校验
      │
      ▼
FlashDB src/*.c        ← 上游源码，未修改
      │
      ▼
FAL port/fal/src/*.c   ← 上游源码，未修改
      │
      ▼
port/sim_flash.c       ← 本仓库：64 KB NOR 模型 + 操作计数 + longjmp 断电
```

两个关键细节：

1. **NOR 语义**：写入是 `flash &= data`，只能把 1 变成 0，和真实 NOR 一致。用 `memcpy` 会让"没擦除就写"这种错误状态被悄悄修好，测不出问题。
2. **断电语义**：数到第 N 次擦写时直接 `longjmp`，**这次操作不执行**，等价于"在第 N 次操作之前掉电"。

## 校验什么

- **KVDB**：96 个键必须都还在；被更新的 48 个键，值只能是旧值或新值，不能是第三种东西；重启后数据库还能继续写。
- **TSDB**：每一条还能读到的记录，负载必须和它的时间戳对得上，时间必须严格递增。

## 逐个复现异常点

```bash
./build/powerloss-lab cut 46      # 丢键：key006 missing
./build/powerloss-lab cut 1546    # 不可写：append rc=7
./build/powerloss-lab cut 1550    # 迭代失控：负时间戳、不收敛
```

输出示例（cut 46）：

```
== KVDB: update 48 of 96 keys ==
baseline: 338 flash ops, 6 sectors, 64 bytes per value

    key006 missing
  cut   46: 1 problem(s)
    key006 missing
      after a second reboot: 1 problem(s)
KVDB: 338 cut points, 1 failed
```

输出示例（cut 1546，TSDB）：

```
      append after reboot failed: rc=7
  cut 1546: 1 problem(s)
```

输出示例（cut 1550，TSDB）：

```
      ts=1801739606 status=2 payload[0..3]=03 0a 11 18 expect=6a 71 78 7f
      ts=-269950502 status=2 payload[0..3]=03 0a 11 18 expect=66 6d 74 7b
      ts=-269950502 out of order (previous 235340025)
      iteration did not converge: >5000 records, first=100127 last=1397507390
```

## 边界

- 这是**实验台发现的疑似问题**，不是已经确认的上游缺陷。没有提 issue，也没有打补丁。
- 只模拟"操作之前"掉电，不模拟擦除做到一半、写入只完成一部分的物理时序。
- 单线程，不覆盖多任务并发写同一个库的情况。
- 重启是进程内 `memset` + 重新 init（FlashDB 没有文件级静态状态），不等价于真实复位的外设状态。

## 许可

Apache-2.0。`third_party/FlashDB` 及其 FAL 组件版权归其作者所有。
