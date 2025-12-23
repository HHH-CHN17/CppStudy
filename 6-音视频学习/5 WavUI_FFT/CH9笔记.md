## 前置基础

下面把 **物理频率（Hz）**、**采样率（$F_s$）**、**归一化频率**、以及 **DFT 频点索引 $k$** 串成一套可互相换算的关系。

## 1) 物理频率 Hz 是什么

连续时间的正弦波通常写作：
$$
x(t)=A\sin(2\pi Ft+\phi)
$$

- $F$：**物理频率**，单位 Hz（cycles/second，每秒多少个周期）
- $\phi$：初相
- $t$：时间

## 2) 采样率 $F_s$ 是什么

采样率 $F_s$ 的单位是 samples/second：每秒取多少个样本点。采样周期 $T_s=1/F_s$。

把上面的连续信号按 $t=nT_s=n/F_s$ 采样：
$$
x[n]=x\!\left(\frac{n}{F_s}\right)=A\sin\!\left(2\pi\frac{F}{F_s}n+\phi\right)
$$
这一步直接引出 **归一化频率**。

## 3) 归一化频率 $f$

定义：
$$
f=\frac{F}{F_s}\quad(\text{cycles/sample})
$$
于是离散信号写成：
$$
x[n]=A\sin(2\pi f n+\phi)
$$
书里也明确说过一种常用的横轴标法是“**以采样率的分数**来表示频率，范围是 0 到 0.5”，并给出换算 $f=k/N$。backmat_合并

### Nyquist（为什么 $f$ 只到 0.5）

采样定理：想“正确采样”，信号频率成分必须低于采样率的一半（$F$）。backmat_合并
 超过一半会发生混叠（aliasing），例如书里举例：0.95 倍采样率会“伪装成”0.05 倍采样率。backmat_合并

## 4) 角频率 $\omega$

另一种常见写法是：
$$
\omega = 2\pi f = 2\pi \frac{F}{F_s}\quad(\text{radians/sample})
$$
书里把这种用法称为 natural frequency（以弧度计），并指出它是把 $f$ 乘以 $2\pi$。backmat_合并

## 5) DFT 的 $k$ 是什么（bin index / “N 点里有几圈”）

对 **N 点 DFT**，基函数写作：
$$
\cos\left(2\pi\frac{k}{N}n\right),\ \sin\left(2\pi\frac{k}{N}n\right)
$$
其中 $k$ 决定频率。书里强调一个关键直觉：**$k$ 等于在 N 个点里完成的周期数**。backmat_合并

因此：
$$
f_k=\frac{k}{N}\quad(\text{cycles/sample})
$$
书里也直接写了 **$f=k/N$** 的换算。backmat_合并

把它再换回 Hz：
$$
F_k = f_k F_s = \frac{k}{N}F_s
$$

### “1024 点里有多少个周期”怎么求

记录长度是 $N$ 个样本，对应时间长度 $N/F_s$ 秒。一个频率为 $F$ Hz 的正弦波，在这段记录里完成的周期数：
$$
\text{cycles in record} = F\cdot\frac{N}{F_s}
$$
而这个数正好就是它在 DFT 里的“理想索引”：
$$
k_0 = \frac{F N}{F_s}
$$

- 若 $k_0$ 是整数：频率正好对齐某个 bin（coherent）
- 若 $k_0$ 不是整数：频率夹在两个 bin 之间（容易出现泄露/拖尾）

## 6) 用书里的例子把概念钉牢

书里举了：90 cycles/second 的正弦以 1000 samples/second 采样，此时是采样率的 0.09，且每个周期约 11.1 个采样点。backmat_合并
 这对应：

- $F=90$ Hz，$F_s=1000$ Hz
- $f=F/F_s=0.09$ cycles/sample
- 每周期采样点数 $=F_s/F\approx 11.1$

## 7) 零填充（zero padding）与这些量的关系

零填充只改变 **DFT 点数 $N$**，不改变 **采样率 $F_s$**、不改变 **真实频率 $F$**、不改变 **归一化频率 $f$**。书里明确：补零不改变频谱形状，只是提供更多频域采样点。backmat_合并
 所以当你把 $N$ 从 1024 变 2048 时，同一个 $F$ 对应的索引会变为：
$$
k_0'=\frac{F\cdot 2048}{F_s}=2k_0
$$