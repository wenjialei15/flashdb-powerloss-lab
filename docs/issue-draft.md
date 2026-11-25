# 断电恢复后 TSDB 进入两种异常状态，KVDB 可能丢键

> 这是一份可直接提交到 [armink/FlashDB](https://github.com/armink/FlashDB/issues) 的 Issue 草稿。
> 复现环境是 PC 模拟器（见下方仓库），不是真机；但同一组结果在 Windows + zig 与 Ubuntu + GCC 上都能复现。

## 环境

- FlashDB `6c722f1`（`FDB_SW_VERSION` 2.2.99），FAL 模式
- 模拟 Flash：64 KiB NOR，4 KiB 扇区，`FDB_WRITE_GRAN = 1`
- 分区：`kvdb` 24 KiB（6 扇区）、`tsdb` 12 KiB（3 扇区）
- 单线程，无锁
- 复现实验台：https://github.com/wenjialei15/flashdb-powerloss-lab

断电语义：在第 N 次 `erase/write` **之前**切断电源，该次操作不执行；重启 = RAM 清零 + 重新 `fdb_kvdb_init` / `fdb_tsdb_init`。

## 复现

```bash
git clone https://github.com/wenjialei15/flashdb-powerloss-lab
cd flashdb-powerloss-lab
git submodule update --init third_party/FlashDB
make && ./build/powerloss-lab cut 46
```

---

## 现象 1：KVDB 丢键（cut 46 / 338）

负载：96 个键，逐个 `fdb_kv_set` 写入初值；随后更新前 48 个键。

在第 46 次擦写之前断电，重启后：

```
    key006 missing
  cut   46: 1 problem(s)
    key006 missing
      after a second reboot: 1 problem(s)
```

`key006` 既读不到旧值，也读不到新值；再次重启也不会恢复。

**预期**：`fdb_kv_get("key006")` 返回旧值或新值。
**实际**：返回 `NULL`，键消失。

日志（打开 `FDB_DEBUG_ENABLE` 后）：

```
[FlashDB][kv][env][kvdb] Found an KV (key006) which has changed value failed. Now will recovery it.
[FlashDB][kv][env][kvdb] Error: The KV @0x00002010 length has an error.
```

---

## 现象 2：TSDB 恢复后不可写，`append` 返回 7（cut 1546 / 2064）

负载：追加 520 条记录（负载 16 B），3 个扇区，开启 rollover。

在这些点断电后，重启能正常初始化、已有记录也能读，但**再追加一条新记录失败**：

```
      append after reboot failed: rc=7
  cut 1546: 1 problem(s)
```

`7` 是 `FDB_SAVED_FULL`。

**预期**：rollover 模式下，最旧扇区被回收，`fdb_tsl_append` 成功。
**实际**：数据库被判定为满，之后所有写入失败（只读）。

打开日志后可见：

```
[FlashDB][tsl][log][tsdb] Error: update the sector status failed (7)
```

---

## 现象 3：TSDB 迭代输出垃圾且不收敛（cut 1550 / 1551）

同一个负载，在这两个点断电后，`fdb_tsl_iter` 会读出负的时间戳、负载与时间戳完全对不上的记录，并且**不会正常结束**：

```
      ts=1801739606 status=2 payload[0..3]=03 0a 11 18 expect=6a 71 78 7f
      ts=-269950502 status=2 payload[0..3]=03 0a 11 18 expect=66 6d 74 7b
      ts=-269950502 out of order (previous 235340025)
      ts=1818582615 status=2 payload[0..3]=03 0a 11 18 expect=89 90 97 9e
      ts=-253107493 status=2 payload[0..3]=03 0a 11 18 expect=85 8c 93 9a
      ts=-253107493 out of order (previous 252183034)
      iteration did not converge: >5000 records, first=100127 last=1397507390
```

第一条记录的 `time` 就已经是 `1801739606`，而本次负载的时间戳范围是 `100000..100519`——说明迭代器一开始就踩到了错误的扇区/记录头。

**预期**：只返回完整记录，时间戳严格递增；不完整记录被跳过。
**实际**：返回垃圾记录，迭代无法收敛（在真机上等价于看门狗复位）。

---

## 观察

- 5 个异常点全部集中在 **KV 的 GC** 和 **TSDB 的扇区轮转**：这两处都是"先写元数据/状态表，再搬数据"。
- 现象 2 和 3 出现在同一段（第 1546–1551 次操作），中间只差几次擦写，怀疑是扇区状态表（sector status table）在断电后处于半更新状态：
  - 一种情况下 rollover 无法判定可回收扇区 → 误报 `FDB_SAVED_FULL`；
  - 另一种情况下按损坏的扇区头继续遍历 → 读出垃圾。
- 现象 1 的 `key006` 在 GC 的 "changed" 恢复路径里被删除，怀疑旧副本已经被搬走、新副本尚未落盘，恢复逻辑选择了丢弃。

## 说明

- 模拟器只模拟"操作之前"掉电，不模拟擦除/写入执行到一半的物理时序；如果上游认为这种时序不构成有效场景，可以关掉这个 Issue。
- 没有在真机上复现，也没有定位到具体代码行；如果需要，我可以补最小用例或按指引进 `tests/`。
