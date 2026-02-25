import csv
import matplotlib.pyplot as plt
import numpy as np

# ========== 数据存储初始化 ==========
# 模式一数据（flow_stats2.csv）
mode1_data = {
    'time': [],
    'throughput': [[], [], [], []],  # [link1, link2, link3, link4]
    'lossrate': [[], [], [], []],
    'rtt': [[], [], [], []],
    'jitter': [[], [], [], []]
}

# 模式二数据（flow_stats3.csv）
mode2_data = {
    'time': [],
    'throughput': [[], [], [], []],
    'lossrate': [[], [], [], []],
    'rtt': [[], [], [], []],
    'jitter': [[], [], [], []]
}

# ========== 通用读取函数 ==========
def read_flow_csv(filename, data_dict):
    """读取CSV文件并存储到数据字典中"""
    try:
        with open(filename, 'r') as file:
            reader = csv.reader(file)
            next(reader)  # 跳过表头
            for row in reader:
                if len(row) < 17:  # 确保行数据完整
                    continue
                # 读取时间
                try:
                    time_val = float(row[0])
                    data_dict['time'].append(time_val)
                    
                    # 读取4条链路的4个指标
                    for link_idx in range(4):
                        col_base = 1 + link_idx * 4
                        throughput = float(row[col_base])
                        lossrate = float(row[col_base + 1])
                        rtt = float(row[col_base + 2])
                        jitter = float(row[col_base + 3])
                        
                        data_dict['throughput'][link_idx].append(throughput)
                        data_dict['lossrate'][link_idx].append(lossrate)
                        data_dict['rtt'][link_idx].append(rtt)
                        data_dict['jitter'][link_idx].append(jitter)
                except ValueError as e:
                    print(f"警告：跳过无效数据行 - {e}")
                    continue
        print(f"成功读取 {filename}：{len(data_dict['time'])} 个时间点")
    except FileNotFoundError:
        print(f"错误：找不到文件 {filename}")
        exit(1)

# ========== 读取两个模式的文件 ==========
read_flow_csv('flow_stats2.csv', mode1_data)
read_flow_csv('flow_stats3.csv', mode2_data)

# ========== 时间轴对齐和数据插值 ==========
def align_time_series(data1, data2):
    """
    对齐两个数据集的时间轴，使用插值填充缺失数据
    返回：统一的时间轴 + 对齐后的两个数据集
    """
    # 获取两个时间轴的并集，排序后作为统一时间轴
    all_times = sorted(list(set(data1['time']) | set(data2['time'])))
    if not all_times:
        print("错误：没有有效时间数据")
        exit(1)
    
    # 插值函数：根据已有数据插值得到指定时间点的值
    def interpolate_data(time_series, value_series, target_times):
        if len(time_series) < 2:
            # 如果数据点太少，直接填充为常量
            return np.full(len(target_times), value_series[0] if value_series else 0)
        return np.interp(target_times, time_series, value_series)
    
    # 对齐数据集1
    aligned1 = {
        'time': all_times,
        'throughput': [],
        'lossrate': [],
        'rtt': [],
        'jitter': []
    }
    
    # 对齐数据集2
    aligned2 = {
        'time': all_times,
        'throughput': [],
        'lossrate': [],
        'rtt': [],
        'jitter': []
    }
    
    # 对每个指标的4条链路进行插值
    for metric in ['throughput', 'lossrate', 'rtt', 'jitter']:
        for link_idx in range(4):
            # 数据集1插值
            values1 = interpolate_data(
                data1['time'], data1[metric][link_idx], all_times
            )
            aligned1[metric].append(values1)
            
            # 数据集2插值
            values2 = interpolate_data(
                data2['time'], data2[metric][link_idx], all_times
            )
            aligned2[metric].append(values2)
    
    print(f"时间轴对齐完成：{len(all_times)} 个统一时间点")
    return aligned1, aligned2

# 执行时间轴对齐
aligned1, aligned2 = align_time_series(mode1_data, mode2_data)
time = aligned1['time']  # 统一的时间轴

# ========== 绘图样式设置 ==========
# 模式一：实线 + 原有标记
markers1 = ['o', 's', '^', 'D']
markerfacecolors1 = ['black', 'white', 'black', 'white']
linestyles1 = ['-', '-', '-', '-']
colors1 = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']  # 专业配色

# 模式二：虚线 + 对应标记（颜色相同，线型不同）
markers2 = ['o', 's', '^', 'D']
markerfacecolors2 = ['gray', 'lightgray', 'gray', 'lightgray']
linestyles2 = ['--', '--', '--', '--']
colors2 = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']  # 与模式一颜色对应

# ========== X轴刻度优化 ==========
step = 2
if time:
    max_time = max(time)
    min_time = min(time)
    xticks_pos = list(range(int(min_time), int(max_time) + 1, step))
    xticks_label = [str(int(t)) for t in xticks_pos]
else:
    xticks_pos = []
    xticks_label = []

# ========== 创建 2×2 画布 ==========
fig, axs = plt.subplots(2, 2, figsize=(14, 10))
fig.canvas.manager.set_window_title('Flow Statistics Comparison (Mode 1 vs Mode 2)')

def plot_on_axes(ax, data1, data2, metric, ylabel, subtitle):
    """
    在子图上绘制两个模式的指定指标
    data1: 对齐后的模式一数据
    data2: 对齐后的模式二数据
    metric: 要绘制的指标名称（throughput/lossrate/rtt/jitter）
    """
    link_names = ["Link 1", "Link 2", "Link 3", "Link 4"]
    
    # 绘制模式一数据（实线）
    for i in range(4):
        ax.plot(time, data1[metric][i], label=f"Mode 1 - {link_names[i]}",
                marker=markers1[i], markerfacecolor=markerfacecolors1[i],
                markersize=4, linestyle=linestyles1[i], color=colors1[i], 
                linewidth=2, alpha=0.8)
    
    # 绘制模式二数据（虚线）
    for i in range(4):
        ax.plot(time, data2[metric][i], label=f"Mode 2 - {link_names[i]}",
                marker=markers2[i], markerfacecolor=markerfacecolors2[i],
                markersize=4, linestyle=linestyles2[i], color=colors2[i], 
                linewidth=2, alpha=0.8)
    
    # 设置标签
    ax.set_ylabel(ylabel, fontsize=12)
    ax.set_xlabel('Time (s)', fontsize=12)
    
    # 设置X轴刻度
    ax.set_xticks(xticks_pos)
    ax.set_xticklabels(xticks_label)
    
    # 自动适配Y轴范围，预留10%余量
    all_values = []
    for i in range(4):
        all_values.extend(data1[metric][i])
        all_values.extend(data2[metric][i])
    
    if all_values:
        y_min = min(all_values)
        y_max = max(all_values)
        # 处理最小值等于最大值的情况
        if y_min == y_max:
            if y_min == 0:
                ax.set_ylim(-0.1, 0.1)
            else:
                ax.set_ylim(y_min * 0.9, y_max * 1.1)
        else:
            ax.set_ylim(y_min - 0.1*(y_max - y_min), y_max + 0.1*(y_max - y_min))
    
    # 网格和图例
    ax.grid(True, alpha=0.3, linestyle=':')
    ax.legend(fontsize=9, loc='best', framealpha=0.9)
    
    # 子图标题（a）~（d）
    ax.text(0.5, -0.15, subtitle, transform=ax.transAxes,
            ha='center', va='top', fontsize=12, fontweight='bold')

# ========== 绘制四张对比图 ==========
# (a) Throughput
plot_on_axes(axs[0, 0], aligned1, aligned2,
             'throughput', "Throughput (Kbps)", "(a) Throughput Comparison")

# (b) Loss Rate
plot_on_axes(axs[0, 1], aligned1, aligned2,
             'lossrate', "Loss Rate (%)", "(b) Loss Rate Comparison")

# (c) Average RTT
plot_on_axes(axs[1, 0], aligned1, aligned2,
             'rtt', "Average RTT (ms)", "(c) Average RTT Comparison")

# (d) Jitter
plot_on_axes(axs[1, 1], aligned1, aligned2,
             'jitter', "Jitter (ms)", "(d) Jitter Comparison")

# ========== 全局标题和布局调整 ==========
fig.suptitle('Flow Statistics: Mode 1 (flow_stats2.csv) vs Mode 2 (flow_stats3.csv)', 
             fontsize=16, fontweight='bold', y=0.98)

# 调整布局，避免重叠
plt.tight_layout(rect=[0, 0.08, 1, 0.95])
plt.subplots_adjust(hspace=0.4, wspace=0.3, bottom=0.12)

# ========== 保存和显示 ==========
plt.savefig("flow_stats_mode_comparison.png", dpi=300, bbox_inches='tight', 
            facecolor='white', edgecolor='none')
print("图表已保存为 flow_stats_mode_comparison.png")
plt.show()