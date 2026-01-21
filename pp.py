import csv
import matplotlib.pyplot as plt

def read_flow_stats(filename):
    """读取 CSV 数据并提取 Link 1 和 Link 3 的指标"""
    time = []
    # 结构: data[link_id] = {metric_idx: [values]}
    # 只存储 link 1 (idx 0) 和 link 3 (idx 2)
    selected_links = {0: [[] for _ in range(4)], 2: [[] for _ in range(4)]}
    
    with open(filename, 'r') as file:
        reader = csv.reader(file)
        next(reader)
        for row in reader:
            time.append(float(row[0]))
            # 提取 Link 1 (Columns 1-4)
            for m in range(4):
                selected_links[0][m].append(float(row[1 + m]))
            # 提取 Link 3 (Columns 9-12)
            for m in range(4):
                selected_links[2][m].append(float(row[9 + m]))
    return time, selected_links

# ========== 1. 读取数据 ==========
# 请确保文件名与您的本地文件一致
time_pre, data_pre = read_flow_stats('flow_stats.csv')
time_post, data_post = read_flow_stats('flow_stats3.csv')

# ========== 2. 样式配置 ==========
# 保持 Link 1 为蓝色，Link 3 为绿色
configs = {
    0: {'label': 'Link 1', 'color': '#1f77b4', 'marker': 'o'},
    2: {'label': 'Link 3', 'color': '#2ca02c', 'marker': '^'}
}

# ========== 3. 绘图函数 ==========
def plot_comparison(ax, metric_idx, ylabel, subtitle):
    for lid in [0, 2]: # 只遍历 Link 1 和 Link 3
        cfg = configs[lid]
        
        # 修改前 (Before): 虚线 + 空心标记 + 较低透明度
        ax.plot(time_pre, data_pre[lid][metric_idx], 
                label=f"{cfg['label']} (Original)", 
                color=cfg['color'], linestyle='--', alpha=0.4,
                marker=cfg['marker'], markerfacecolor='none', markersize=5)
        
        # 修改后 (After): 实线 + 实心标记 + 高饱和度
        ax.plot(time_post, data_post[lid][metric_idx], 
                label=f"{cfg['label']} (Optimized)", 
                color=cfg['color'], linestyle='-', linewidth=2,
                marker=cfg['marker'], markersize=6)

    ax.set_ylabel(ylabel, fontsize=10)
    ax.set_xlabel('Time (s)', fontsize=10)
    ax.grid(True, linestyle=':', alpha=0.6)
    
    # 设置 X 轴刻度步长为 2
    max_t = max(max(time_pre), max(time_post))
    ax.set_xticks(range(0, int(max_t) + 1, 2))
    
    # 布局美化
    ax.legend(fontsize=9, loc='best', frameon=True)
    ax.set_title(subtitle, y=-0.25, fontsize=12, fontweight='bold')

# ========== 4. 创建 2x2 画布 ==========
fig, axs = plt.subplots(2, 2, figsize=(14, 10))

plot_comparison(axs[0, 0], 0, "Throughput (Kbps)", "(a) Throughput")
plot_comparison(axs[0, 1], 1, "Loss Rate (%)", "(b) Loss Rate")
plot_comparison(axs[1, 0], 2, "Average RTT (ms)", "(c) Average RTT")
plot_comparison(axs[1, 1], 3, "Jitter (ms)", "(d) Jitter")

# ========== 5. 保存与展示 ==========
plt.tight_layout()
plt.subplots_adjust(hspace=0.4, bottom=0.15)
plt.savefig("link_comparison_selective.png", dpi=300, bbox_inches='tight')
plt.show()