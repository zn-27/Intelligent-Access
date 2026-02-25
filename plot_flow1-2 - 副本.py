import csv
import matplotlib.pyplot as plt

# ========== 初始化数据列表 ==========
time = []  # 时间轴（两个文件共用，确保长度一致）

# 模式1（flow_stats1.csv）：2条链路（Link 1跨域、Link 2域内）
tp1_mode1 = []; lr1_mode1 = []; rtt1_mode1 = []; jit1_mode1 = []
tp2_mode1 = []; lr2_mode1 = []; rtt2_mode1 = []; jit2_mode1 = []

# 模式2（flow_stats2.csv）：2条链路（Link 1跨域、Link 2域内）
tp1_mode2 = []; lr1_mode2 = []; rtt1_mode2 = []; jit1_mode2 = []
tp2_mode2 = []; lr2_mode2 = []; rtt2_mode2 = []; jit2_mode2 = []

# ========== 读取模式1数据（修复时间读取逻辑） ==========
try:
    with open('flow_stats1.csv', 'r') as file:
        reader = csv.reader(file)
        header = next(reader)  # 跳过表头
        for row in reader:
            # 每次循环都添加时间（确保time与数据行数一致）
            time.append(float(row[0]))
            # 模式1 - Link 1（跨域）：列1-4
            tp1_mode1.append(float(row[1]))
            lr1_mode1.append(float(row[2]))
            rtt1_mode1.append(float(row[3]))
            jit1_mode1.append(float(row[4]))
            # 模式1 - Link 2（域内）：列5-8
            tp2_mode1.append(float(row[5]))
            lr2_mode1.append(float(row[6]))
            rtt2_mode1.append(float(row[7]))
            jit2_mode1.append(float(row[8]))
    print(f"模式1数据读取成功：{len(time)}行")
except FileNotFoundError:
    print("错误：找不到flow_stats1.csv文件，请检查文件路径")
    exit(1)
except IndexError:
    print("错误：flow_stats1.csv文件格式错误，列数不足")
    exit(1)
except ValueError:
    print("错误：flow_stats1.csv文件包含非数值数据")
    exit(1)

# ========== 读取模式2数据（增加长度校验） ==========
try:
    with open('flow_stats2.csv', 'r') as file:
        reader = csv.reader(file)
        header = next(reader)  # 跳过表头
        row_count = 0
        for row in reader:
            row_count += 1
            # 模式2 - Link 1（跨域）：列1-4
            tp1_mode2.append(float(row[1]))
            lr1_mode2.append(float(row[2]))
            rtt1_mode2.append(float(row[3]))
            jit1_mode2.append(float(row[4]))
            # 模式2 - Link 2（域内）：列5-8
            tp2_mode2.append(float(row[5]))
            lr2_mode2.append(float(row[6]))
            rtt2_mode2.append(float(row[7]))
            jit2_mode2.append(float(row[8]))
    
    # 校验模式2数据长度是否与模式1一致
    if row_count != len(time):
        print(f"警告：模式2数据行数（{row_count}）与模式1（{len(time)}）不一致")
        # 截断或填充数据以匹配长度（避免绘图错误）
        min_len = min(row_count, len(time))
        time = time[:min_len]
        # 模式1数据截断
        tp1_mode1, lr1_mode1, rtt1_mode1, jit1_mode1 = [x[:min_len] for x in [tp1_mode1, lr1_mode1, rtt1_mode1, jit1_mode1]]
        tp2_mode1, lr2_mode1, rtt2_mode1, jit2_mode1 = [x[:min_len] for x in [tp2_mode1, lr2_mode1, rtt2_mode1, jit2_mode1]]
        # 模式2数据截断
        tp1_mode2, lr1_mode2, rtt1_mode2, jit1_mode2 = [x[:min_len] for x in [tp1_mode2, lr1_mode2, rtt1_mode2, jit1_mode2]]
        tp2_mode2, lr2_mode2, rtt2_mode2, jit2_mode2 = [x[:min_len] for x in [tp2_mode2, lr2_mode2, rtt2_mode2, jit2_mode2]]
        print(f"已自动截断数据至{min_len}行")
    else:
        print(f"模式2数据读取成功：{row_count}行")
except FileNotFoundError:
    print("错误：找不到flow_stats2.csv文件，请检查文件路径")
    exit(1)
except IndexError:
    print("错误：flow_stats2.csv文件格式错误，列数不足")
    exit(1)
except ValueError:
    print("错误：flow_stats2.csv文件包含非数值数据")
    exit(1)

# ========== 线条样式设置（保持不变） ==========
linestyles = ['-', '-', '--', '--']  # 顺序：模式1-Link1、模式1-Link2、模式2-Link1、模式2-Link2
markers = ['o', '^', 'o', '^']       # 跨域用o，域内用^
markerfacecolors = ['black', 'darkred', 'white', 'lightcoral']
colors = ['black', 'darkred', 'black', 'darkred']

# ========== X轴刻度优化（避免空标签过多） ==========
if time:
    max_time = int(max(time))
    step = 2 if max_time >= 10 else 1  # 时间较短时用1秒间隔
    xticks_pos = list(range(0, max_time + 1, step))
    xticks_label = [str(t) for t in xticks_pos]
else:
    print("错误：没有读取到有效数据")
    exit(1)

# ========== 创建2×2画布 ==========
fig, axs = plt.subplots(2, 2, figsize=(14, 10))
fig.canvas.manager.set_window_title('Flow Statistics Comparison')

def plot_on_axes(ax, data_list, ylabel, subtitle):
    """
    绘制对比图：data_list顺序为 [模式1-Link1, 模式1-Link2, 模式2-Link1, 模式2-Link2]
    """
    labels = [
        "模式1 - Link1（跨域）",
        "模式1 - Link2（域内）",
        "模式2 - Link1（跨域）",
        "模式2 - Link2（域内）"
    ]
    
    for i in range(4):
        # 确保每个数据列表长度与time一致
        if len(data_list[i]) != len(time):
            print(f"警告：{labels[i]}数据长度与时间轴不一致，已跳过该条曲线")
            continue
        
        ax.plot(time, data_list[i], 
                label=labels[i],
                color=colors[i],
                linestyle=linestyles[i],
                marker=markers[i],
                markerfacecolor=markerfacecolors[i],
                markersize=5,
                linewidth=2,
                alpha=0.8)  # 增加透明度，避免线条重叠时看不清

    ax.set_ylabel(ylabel, fontsize=11)
    ax.set_xlabel('Time (s)', fontsize=11)
    ax.set_xticks(xticks_pos)
    ax.set_xticklabels(xticks_label)

    # 自动适配Y轴范围（处理所有数据不为空的情况）
    all_data = []
    for d in data_list:
        if len(d) == len(time):  # 只考虑长度匹配的数据
            all_data.extend(d)
    if all_data:
        y_min = min(all_data)
        y_max = max(all_data)
        # 处理y_min == y_max的特殊情况（避免除以0）
        if y_min == y_max:
            ax.set_ylim(y_min - 0.1, y_max + 0.1)
        else:
            ax.set_ylim(y_min - 0.1*(y_max - y_min), y_max + 0.1*(y_max - y_min))

    ax.grid(True, alpha=0.3)
    ax.legend(loc='best', fontsize=10)
    # 子图标题（a）~（d）放在图正下方
    ax.text(0.5, -0.15, subtitle, transform=ax.transAxes,
            ha='center', va='top', fontsize=12, fontweight='bold')

# ========== 绘制四张对比图 ==========
# 吞吐量对比
plot_on_axes(axs[0, 0],
             [tp1_mode1, tp2_mode1, tp1_mode2, tp2_mode2],
             "Throughput (Kbps)", "(a) Throughput")

# 丢包率对比
plot_on_axes(axs[0, 1],
             [lr1_mode1, lr2_mode1, lr1_mode2, lr2_mode2],
             "Loss Rate (%)", "(b) Loss Rate")

# 平均RTT对比
plot_on_axes(axs[1, 0],
             [rtt1_mode1, rtt2_mode1, rtt1_mode2, rtt2_mode2],
             "Average RTT (ms)", "(c) Average RTT")

# 抖动对比
plot_on_axes(axs[1, 1],
             [jit1_mode1, jit2_mode1, jit1_mode2, jit2_mode2],
             "Jitter (ms)", "(d) Jitter")

# ========== 布局调整 & 保存 ==========
plt.tight_layout(rect=[0, 0.08, 1, 1])  # 给底部留空间
plt.subplots_adjust(hspace=0.4, bottom=0.12)  # 调整子图间距
plt.savefig("flow_stats_mode_comparison.png", dpi=300, bbox_inches='tight')
print("图表已保存为 flow_stats_mode_comparison.png")
plt.show()