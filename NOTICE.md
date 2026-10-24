# NOTICE

本项目的代码为原创实现。其中若干**算法常量**（属于数据而非代码，无法"改写"）来自
社区公开实现，特此说明：

## 酷狗 KGM V2 掩码表

`src/dec_kgm.c` 中的 `table1` / `table2` / `tableV2` / `vprKey` 三张常量表，
与社区公开实现中的同名常量一致（unlock-music 及其衍生项目）。这部分数值最早由
unlock-music 项目社区逆向整理并公开。

参考实现：<https://github.com/onavcn/kugou-audio-unlock>（MIT License）

## 网易云 NCM

`src/dec_ncm.c` 中的容器结构、内置 AES 密钥与元数据密钥，来自社区对 `.ncm`
格式的公开分析。AES 算法本身为 FIPS-197 标准，本项目独立实现。

## 自研部分

- AES-128 实现：S 盒由 GF(2^8) 乘法逆元 + 仿射变换实时推导，未抄录常量表；
  已通过 FIPS-197 标准测试向量验证（`69c4e0d8...70b4c55a`）
- 容器解析、密钥流双模式回退、魔数校验、ID3v2.3 标签写入、ffmpeg 调度、
  Win32 界面、线程与进度上报：全部为本项目原创
